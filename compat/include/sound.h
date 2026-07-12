/*
 * Mac Sound Manager shim (ADR-0003). Skeleton.
 *
 * The Mac engine plays its audio through `SndPlay` against `snd `
 * resources, queuing `SndCommand`s via `SndDoCommand` / `SndDoImmediate`.
 * The Atari side will eventually route those into Falcon DMA sound
 * (XBIOS Dsound / Buffptr) with a YM2149 fallback on TT030, but the
 * engine path needs the API surface long before the hardware path is
 * ready.
 *
 * Here so far: the SndChannel / SndCommand layouts at the Mac record
 * offsets, NumVersion for SndSoundManagerVersion, the standard synth
 * and command constants, and stub entry points that accept calls,
 * report them through dbg_log when PROBE is on, and return noErr. The
 * stubs let the engine's audio paths run end-to-end without producing
 * audio; the Falcon DMA / YM2149 backend slots in here when the engine
 * actually demands sound.
 */

#ifndef COMPAT_SOUND_H
#define COMPAT_SOUND_H

#include "macmemory.h"          /* Handle, OSErr, Ptr  */
#include "quickdraw.h"          /* Boolean             */

/* SndCommand — the Mac 8-byte command record (cmd, param1, param2). */
typedef struct SndCommand {
	unsigned short cmd;
	short          param1;
	long           param2;
} SndCommand;

/*
 * SndChannel — the Mac record. Most engine code treats this as opaque
 * and only stores the SndChannelPtr; the fields are exposed for the
 * rare engine path that pokes by offset. The 128-entry queue is the
 * classic-Mac default and matches the size SndNewChannel reserves on
 * the original. Trailing-comment numbers are Mac field offsets.
 */
typedef struct SndChannel {
	struct SndChannel *nextChan;    /* 0   next in the global chain */
	Ptr                firstMod;     /* 4   modifiers list           */
	void              *callBack;     /* 8   SndCallBackUPP           */
	long               userInfo;     /* 12  app-defined              */
	long               wait;         /* 16  reserved                 */
	SndCommand         cmdInProgress;/* 20  cmd currently executing  */
	short              flags;        /* 28  channel flags            */
	short              qLength;      /* 30  queue capacity           */
	short              qHead;        /* 32  next-to-execute index    */
	short              qTail;        /* 34  next-to-fill index       */
	SndCommand         queue[128];   /* 36  command queue            */
} SndChannel, *SndChannelPtr;

/* --- synth IDs (the `synth` arg to SndNewChannel) ----------------- */
#define squareWaveSynth   1
#define waveTableSynth    3
#define sampledSynth      5

/* --- common Sound Manager command codes ---------------------------- */
#define nullCmd           0
#define quietCmd          3
#define flushCmd          4
#define waitCmd           10
#define pauseCmd          11
#define resumeCmd         12
#define callBackCmd       13
#define syncCmd           14
#define freqCmd           40
#define ampCmd            41
#define timbreCmd         42
#define freqDurationCmd   44
#define restCmd           45
#define soundCmd          80
#define bufferCmd         81
#define rateCmd           82

/* NumVersion — the four-byte Toolbox version stamp. */
typedef struct {
	unsigned char majorRev;
	unsigned char minorAndBugRev;
	unsigned char stage;
	unsigned char nonRelRev;
} NumVersion;

/* --- API ---------------------------------------------------------- */

/*
 * Open a new Sound Manager channel of the given synth type. The shim
 * allocates a SndChannel (zero-initialised, qLength = 128) when
 * *chan is NULL on entry, or uses caller-supplied storage when not.
 * `init` carries the synth-specific init bits (initChanLeft / initMono
 * / etc.) the engine passes; we accept and ignore them for now.
 * `userRoutine` is the optional completion callback — also accepted
 * and ignored. Returns noErr; allocation failure returns memFullErr.
 */
OSErr SndNewChannel(SndChannelPtr *chan, short synth, long init,
                    void *userRoutine);

/*
 * Close a channel. `quietNow != 0` aborts any in-progress sound; the
 * shim has no real playback, so this is bookkeeping. The caller-
 * supplied storage path is NOT freed (matches the Mac).
 */
OSErr SndDisposeChannel(SndChannelPtr chan, Boolean quietNow);

/*
 * Play an `snd ` resource through `chan` (or open a temporary system
 * channel when `chan` is NULL). `async != 0` queues; `async == 0`
 * blocks until done. The skeleton accepts both and returns noErr.
 */
OSErr SndPlay(SndChannelPtr chan, Handle sndHandle, Boolean async);

/*
 * Queue a command. `noWait != 0` returns notEnoughBufferSpace when
 * the queue is full instead of blocking; the skeleton always succeeds.
 */
OSErr SndDoCommand(SndChannelPtr chan, const SndCommand *cmd, Boolean noWait);

/* Execute a command synchronously, bypassing the queue. */
OSErr SndDoImmediate(SndChannelPtr chan, const SndCommand *cmd);

/* Return the Sound Manager version. The shim reports 3.0 — the
 * version the Mac System 7 build FRUA was compiled against ships. */
NumVersion SndSoundManagerVersion(void);

/*
 * SysBeep — the classic alert beep (l036a's error dialog calls it).
 * `duration` is the Mac tick count, ignored here. The port has no tone
 * generator yet (the DMA path is sample-only), so this is a documented
 * no-op for now; TODO: emit a short tone via XBIOS Dosound / YM2149.
 */
void SysBeep(short duration);

/*
 * SndDriverWrite — the classic Sound DRIVER (not Manager) free-form
 * synth write. FRUA's sfx leaf (CODE 5 L7ee0) doesn't use `snd `
 * resources: it builds an FFSynthRec in place at the head of the sound
 * library's item and PBWrites it to the ".Sound" driver. The buffer is
 * exactly what the Mac driver receives:
 *
 *   short mode;          // free-form synth mode word
 *   Fixed count;         // rate multiplier, 1.0 (0x10000) = 22254.5454 Hz
 *   Byte  waveBytes[];   // unsigned 8-bit samples, 0x80 = silence
 *
 * `byte_count` is the driver's ioReqCount — the whole buffer including
 * the 6-byte header. The shim decodes the rate, converts the Mac's
 * unsigned samples to the Falcon CODEC's signed ones, and hands them to
 * the sound HAL. Returns noErr, or a Mac error when the buffer is
 * malformed / playback fails.
 */
OSErr SndDriverWrite(const void *ff_buffer, long byte_count);

/* 1 while the sound HAL is still playing (the driver-busy poll L7ee0
 * spins on before it queues the next effect). */
int SndDriverBusy(void);

/* Stop playback immediately (the driver KillIO L7ee0 issues first). */
void SndDriverStop(void);

/*
 * _VInstall / _VRemove — the Mac VBL task queue. FRUA uses exactly one task:
 * the sound sequencer (L741e installs it, JT[1091] is its routine), which must
 * run every vblank or the music never advances a note. The shim registers it on
 * the sound HAL's vblank. `vblTask` is the Mac VBLTask record:
 *   qLink(4) qType(2) vblAddr(4) vblCount(2) vblPhase(2)
 */
OSErr VInstall(void *vblTask);
OSErr VRemove(void *vblTask);

#endif /* COMPAT_SOUND_H */
