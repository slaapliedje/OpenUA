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
#include <stdint.h>             /* uintptr_t */
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
extern void plat_sound_stop(void);
extern int  plat_sound_playing(void);
extern int  plat_sound_synth_start(const void *ftsoundrec);
extern void plat_sound_synth_stop(void);
extern void plat_sound_tone(int count, int amp, int duration_ticks);
extern void plat_sound_set_vbl_hook(void (*fn)(void));

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
	/* The alert beep. Now that the HAL renders square waves for the Mac's
	 * swMode synth, the beep is just a tone: the classic Mac beep is ~1.1 kHz,
	 * which in the driver's period units (freq = 783360 / count) is count=710.
	 * `duration` is in ticks, as the Mac passes it. */
	if (duration <= 0)
		duration = 6;
	plat_sound_tone(710, 128, (int)duration);
}

/* --- classic Sound Driver (free-form synth) — FRUA's sfx path --------------
 *
 * See SndDriverWrite in sound.h. The Mac's free-form `count` is a Fixed rate
 * multiplier against the hardware's 22254.5454 Hz base, so
 *     hz = 22254.5454 * count / 65536
 * which we evaluate as count * 22255 / 65536 (integer, <0.01% off — well
 * inside the CODEC's own rate quantisation, which snaps to 49170/n anyway).
 */
#define FF_HEADER_BYTES  6            /* short mode + Fixed count */
#define FF_BASE_HZ       22255L       /* 22254.5454 rounded */

/* Conversion scratch: the Mac samples are unsigned, the Falcon CODEC wants
 * signed, and the source lives in the resident sound library (converting in
 * place would corrupt the cached item on replay). FRUA's effects are short;
 * anything longer is truncated rather than dropped. */
static signed char g_snd_scratch[16384];

/* --- the Mac VBL task (_VInstall) -------------------------------------------
 *
 * FRUA's sound is driven by a VBLTask: L741e installs one whose routine is
 * JT[1091], and every vblank that routine dispatches the SEQUENCER (jt974),
 * which advances each voice's pattern and rewrites the four-tone rates. Without
 * it the music loads and never plays a note.
 *
 * We register it on the sound HAL's vblank — the one already running to keep
 * the synth's DMA loop fed, which is exactly the Mac's arrangement (its sound
 * VBL task and its Sound Driver shared the same interrupt).
 *
 * VBLTask:  qLink(4)  qType(2)  vblAddr(4)  vblCount(2)  vblPhase(2)
 *
 * vblCount counts vblanks down to zero, then the routine runs; the routine
 * re-arms it (JT[1091] sets it back to 1, i.e. "every vblank"). A zero count
 * means dormant — the Mac would dequeue it, we simply skip it.
 */
static unsigned char * volatile g_vbl_task;

static void vbl_task_run(void)
{
	unsigned char *t = (unsigned char *)g_vbl_task;
	short          count;
	void         (*fn)(void);

	if (t == NULL)
		return;
	count = *(short *)(void *)(t + 10);             /* vblCount */
	if (count <= 0)
		return;
	count--;
	*(short *)(void *)(t + 10) = count;
	if (count != 0)
		return;
	fn = (void (*)(void))(uintptr_t)*(long *)(void *)(t + 6);   /* vblAddr */
	if (fn != NULL)
		fn();
}

OSErr VInstall(void *vblTask)
{
	if (vblTask == NULL)
		return -50;                             /* paramErr */
	g_vbl_task = (unsigned char *)vblTask;
	plat_sound_set_vbl_hook(vbl_task_run);
	dbg_log("snd: VBL sound task installed");
	return 0;
}

OSErr VRemove(void *vblTask)
{
	(void)vblTask;
	plat_sound_set_vbl_hook(NULL);
	g_vbl_task = NULL;
	return 0;
}

int SndDriverBusy(void)
{
	return plat_sound_playing();
}

void SndDriverStop(void)
{
	plat_sound_stop();
}

OSErr SndDriverWrite(const void *ff_buffer, long byte_count)
{
	const unsigned char *p = (const unsigned char *)ff_buffer;
	unsigned long        count_fixed;
	long                 n;
	int                  rate_hz;
	long                 i;

	if (p == NULL || byte_count <= 2)
		return (OSErr)-50;              /* paramErr */

	/* Every buffer handed to the .Sound driver starts with the SYNTH MODE
	 * word, and FRUA uses all three synthesizers:
	 *
	 *   ftMode (1)  — the four-tone synth: {mode, FTSoundRec *}. THIS IS THE
	 *                 MUSIC. The record is live — the sequencer rewrites its
	 *                 four rate fields as the song plays — so hand the HAL the
	 *                 pointer, not a copy.
	 *   swMode (-1) — the square-wave synth: {mode, count, amp, duration},
	 *                 count being a period (freq = 783360/count).
	 *   ffMode (0)  — free-form sampled: {mode, Fixed rate, wave...} — the sfx.
	 */
	switch ((short)rd_be16(p)) {
	case 1: {                               /* ftMode */
		const void *rec;

		if (byte_count < 6)
			return (OSErr)-50;
		rec = (const void *)(uintptr_t)rd_be32(p + 2);
		if (rec == NULL)
			return (OSErr)-50;
		/* NO dbg_log here: a looping song re-arms the synth from the
		 * sequencer, which runs at interrupt time, and dbg_log's Cconws is a
		 * trap. */
		return (plat_sound_synth_start(rec) == 0) ? 0 : (OSErr)-1;
	}
	case -1:                                /* swMode */
		if (byte_count < 8)
			return (OSErr)-50;
		plat_sound_tone((int)rd_be16(p + 2),    /* count (period) */
		                (int)rd_be16(p + 4),    /* amplitude      */
		                (int)rd_be16(p + 6));   /* duration ticks */
		return 0;
	default:
		break;                          /* ffMode — the sampled path below */
	}

	if (byte_count <= FF_HEADER_BYTES)
		return (OSErr)-50;              /* paramErr */

	/* p[0..1] = mode (ffMode), p[2..5] = Fixed count, p[6..] = the wave. */
	count_fixed = rd_be32(p + 2);
	n           = byte_count - FF_HEADER_BYTES;

	rate_hz = (int)((count_fixed * FF_BASE_HZ) >> 16);
	if (rate_hz <= 0)
		return (OSErr)-50;              /* paramErr */

	if (n > (long)sizeof g_snd_scratch)
		n = (long)sizeof g_snd_scratch;
	for (i = 0; i < n; i++)
		g_snd_scratch[i] = (signed char)((int)p[FF_HEADER_BYTES + i] - 128);

	dbg_log_num("snd: sfx rate = ", (long)rate_hz);
	if (plat_sound_play_mono8(g_snd_scratch, n, rate_hz) != 0)
		return (OSErr)-1;
	return 0;                               /* noErr */
}
