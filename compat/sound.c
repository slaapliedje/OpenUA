/*
 * Mac Sound Manager shim — stub implementations. See sound.h.
 *
 * No audio yet. Channel state is tracked (alloc / dispose / queue
 * accounting) so the engine paths that walk SndChannel by offset stay
 * coherent, but no samples leave the host. The Falcon DMA backend
 * (XBIOS Dsound / Buffptr) and the YM2149 fallback slot in here as
 * a follow-up — the shim's API surface won't change when they land.
 *
 * `dbg_log` is wired up so SndPlay / SndDoCommand calls show in the
 * boot trace; useful for tracking down which sound paths the engine
 * exercises before any backend lands.
 */

#include <stddef.h>             /* NULL */
#include <string.h>             /* memset */

#include "dbglog.h"
#include "macmemory.h"
#include "sound.h"

/* Platform HAL — the Falcon DMA backend. The host-side build replaces
 * this with a stub object (sound_host.c) that satisfies the same
 * surface as no-ops. */
#include <stddef.h>
extern int  plat_sound_play_mono8(const signed char *samples, long count,
                                  int rate_hz);

/* Big-endian readers for the `snd ` resource. */
static unsigned short rd_be16(const unsigned char *p)
{
	return (unsigned short)(((unsigned)p[0] << 8) | p[1]);
}

static unsigned long rd_be32(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16)
	     | ((unsigned long)p[2] << 8)  |  (unsigned long)p[3];
}

/*
 * Parse a Mac `snd ` format-1 resource and play its sampled-sound
 * header through the platform backend. Layout:
 *
 *   short format          // 1
 *   short numSynths
 *   per synth (6 bytes): short synthID, long initOption
 *   short numCmds
 *   per cmd (8 bytes):   short cmd, short param1, long param2
 *
 * Among the commands one of bufferCmd (81) or soundCmd (80) carries a
 * param2 = offset (from the resource start) to a SoundHeader. The Mac
 * sets the high bit of `cmd` (dataOffsetFlag = 0x8000) to mark
 * "param2 is offset" — we ignore the flag and always treat param2 as
 * offset, which is FRUA's actual usage.
 *
 * SoundHeader (stdSH only — encode byte at offset 20 must be 0):
 *   long  samplePtr        // 0 = samples follow header
 *   long  length           // sample count (bytes for 8-bit)
 *   Fixed sampleRate       // Hz << 16
 *   long  loopStart / loopEnd
 *   byte  encode
 *   byte  baseFrequency
 *   byte  sampleData[]
 *
 * Returns 0 on success.
 */
static int snd_parse_and_play(Handle sndHandle)
{
	const unsigned char *base;
	long                 size, off;
	short                format, numSynths, numCmds, i;

	if (sndHandle == NULL || *sndHandle == NULL)
		return -1;
	base = (const unsigned char *)*sndHandle;
	size = GetHandleSize(sndHandle);
	if (size < 6)
		return -1;

	format = (short)rd_be16(base);
	if (format != 1)
		return -1;
	numSynths = (short)rd_be16(base + 2);
	off = 4 + (long)numSynths * 6;
	if (off + 2 > size)
		return -1;
	numCmds = (short)rd_be16(base + off);
	off += 2;

	for (i = 0; i < numCmds; i++) {
		unsigned short cmd, base_cmd;
		long           param2, hdr_off, samp_len, samples_off;
		unsigned long  rate_fixed;
		int            rate_hz;
		unsigned char  encode;

		if (off + 8 > size)
			break;
		cmd    = rd_be16(base + off);
		param2 = (long)rd_be32(base + off + 4);
		off   += 8;

		base_cmd = (unsigned short)(cmd & 0x7FFF);
		if (base_cmd != bufferCmd && base_cmd != soundCmd)
			continue;

		hdr_off = param2;
		if (hdr_off < 0 || hdr_off + 22 > size)
			continue;

		samp_len   = (long)rd_be32(base + hdr_off + 4);
		rate_fixed = rd_be32(base + hdr_off + 8);
		rate_hz    = (int)(rate_fixed >> 16);
		encode     = base[hdr_off + 20];
		if (encode != 0)
			continue;       /* only stdSH supported in v1 */

		samples_off = hdr_off + 22;
		if (samples_off + samp_len > size)
			samp_len = size - samples_off;
		if (samp_len <= 0)
			continue;

		{
			signed char *conv = (signed char *)NewPtr((Size)samp_len);
			long         j;
			int          rc;

			if (conv == NULL)
				return -108;
			for (j = 0; j < samp_len; j++)
				conv[j] = (signed char)
				    ((int)base[samples_off + j] - 128);
			rc = plat_sound_play_mono8(conv, samp_len, rate_hz);
			DisposePtr((Ptr)conv);
			return (rc == 0) ? 0 : -1;
		}
	}
	return -1;
}

OSErr SndNewChannel(SndChannelPtr *chan, short synth, long init,
                    void *userRoutine)
{
	SndChannelPtr c;

	(void)synth;
	(void)init;
	(void)userRoutine;
	if (chan == NULL)
		return -50;     /* paramErr */
	if (*chan == NULL) {
		c = (SndChannelPtr)NewPtr((Size)sizeof *c);
		if (c == NULL)
			return -108;        /* memFullErr */
		*chan = c;
	} else {
		c = *chan;
	}
	memset(c, 0, sizeof *c);
	c->qLength = (short)(sizeof c->queue / sizeof c->queue[0]);
	dbg_log("snd: SndNewChannel");
	return 0;
}

OSErr SndDisposeChannel(SndChannelPtr chan, Boolean quietNow)
{
	(void)quietNow;
	if (chan == NULL)
		return -50;
	dbg_log("snd: SndDisposeChannel");
	/* The shim never knows whether the caller supplied storage or
	 * relied on SndNewChannel to allocate — neither does the Mac in
	 * the SndDisposeChannel surface alone. The Mac convention says the
	 * caller must DisposePtr the channel; we follow suit and leave the
	 * block alone here. */
	return 0;
}

OSErr SndPlay(SndChannelPtr chan, Handle sndHandle, Boolean async)
{
	int rc;

	(void)chan;
	(void)async;
	if (sndHandle == NULL || *sndHandle == NULL)
		return -50;
	dbg_log("snd: SndPlay");
	rc = snd_parse_and_play(sndHandle);
	if (rc != 0)
		dbg_log_num("snd: SndPlay parse rc = ", (long)rc);
	return 0;       /* engine path tolerates no-sound; never fail it */
}

OSErr SndDoCommand(SndChannelPtr chan, const SndCommand *cmd, Boolean noWait)
{
	(void)noWait;
	if (chan == NULL || cmd == NULL)
		return -50;
	dbg_log_num("snd: SndDoCommand cmd = ", (long)cmd->cmd);
	chan->cmdInProgress = *cmd;
	return 0;
}

OSErr SndDoImmediate(SndChannelPtr chan, const SndCommand *cmd)
{
	if (chan == NULL || cmd == NULL)
		return -50;
	dbg_log_num("snd: SndDoImmediate cmd = ", (long)cmd->cmd);
	chan->cmdInProgress = *cmd;
	return 0;
}

NumVersion SndSoundManagerVersion(void)
{
	NumVersion v;

	v.majorRev       = 3;
	v.minorAndBugRev = 0;
	v.stage          = 0x80;        /* finalStage */
	v.nonRelRev      = 0;
	return v;
}

void SysBeep(short duration)
{
	/* No tone generator yet (the DMA path is sample-only). Log it so the
	 * error/alert paths are visible in the boot trace; TODO: emit a short
	 * tone via XBIOS Dosound / the YM2149. */
	dbg_log_num("snd: SysBeep ticks = ", (long)duration);
}
