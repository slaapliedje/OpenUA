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
	(void)chan;
	(void)async;
	if (sndHandle == NULL || *sndHandle == NULL)
		return -50;
	dbg_log("snd: SndPlay");
	return 0;
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
