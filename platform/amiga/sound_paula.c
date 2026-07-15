/*
 * Amiga Paula audio backend (ADR-0012) — direct chipset.
 *
 * Implements the plat_sound HAL (plat_sound.h) on Paula's four 8-bit DMA
 * channels. The engine hands us signed 8-bit mono sample buffers and a
 * four-tone synth record (the Mac Sound Manager's square-wave voice); we map
 * those onto Paula channels, set the period from the sample rate, and start
 * audio DMA. Sample buffers handed to Paula MUST live in CHIP RAM.
 *
 * ★ SCAFFOLD STATUS: signatures and the structure are real; the register-level
 * DMA bring-up (AUDxLC/AUDxLEN/AUDxPER/AUDxVOL + DMACON) marked TODO(hw) needs
 * writing against the NDK and validating on amiberry once the toolchain lands.
 * Bodies no-op / return failure so a skeleton is never mistaken for a tested
 * backend. See platform/sound_falcon.c for the Atari analog.
 */

#include "plat_sound.h"

#ifdef FRUA_AMIGA

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>

/* Paula's DMA clock: PAL colour clock 3546895 Hz. period = clock / sample_rate,
 * clamped to >= 124 (the hardware minimum for a stable DMA fetch). */
#define PAULA_CLOCK_PAL  3546895L

static int  s_playing;
static signed char *s_chipbuf;     /* CHIP-RAM copy of the current sample     */
static long s_chipbuf_len;

int plat_sound_init(void)
{
	/* TODO(hw): nothing to reserve up front in the direct model — audio DMA is
	 * started per-sample. If we ever run under the OS, AllocAudio() here. */
	s_playing = 0;
	return 0;
}

void plat_sound_shutdown(void)
{
	plat_sound_stop();
}

int plat_sound_play_mono8(const signed char *samples, long count,
                          int sample_rate)
{
	if (samples == NULL || count <= 0 || sample_rate <= 0)
		return 1;

	/* Paula reads samples via DMA, so they must be in CHIP RAM. Copy in. */
	if (s_chipbuf) { FreeMem(s_chipbuf, s_chipbuf_len); s_chipbuf = NULL; }
	s_chipbuf = AllocMem(count, MEMF_CHIP);
	if (s_chipbuf == NULL)
		return 1;
	CopyMem((APTR)samples, s_chipbuf, count);
	s_chipbuf_len = count;

	/* period = PAULA_CLOCK / rate, clamped. */
	{
		long period = PAULA_CLOCK_PAL / sample_rate;
		if (period < 124) period = 124;
		(void)period;   /* TODO(hw): AUD0LC=s_chipbuf, AUD0LEN=count/2,
		                 * AUD0PER=period, AUD0VOL=64, DMACON |= DMAF_SETCLR|
		                 * DMAF_AUD0|DMAF_MASTER. One-shot: clear on VBL when
		                 * the length has played (or set a repeat of 1 word). */
	}
	s_playing = 1;
	return 0;   /* TODO(hw): the DMA above is not started yet */
}

void plat_sound_stop(void)
{
	/* TODO(hw): DMACON = DMAF_AUD0|1|2|3 (clear, no SETCLR) to halt channels. */
	if (s_chipbuf) { FreeMem(s_chipbuf, s_chipbuf_len); s_chipbuf = NULL; }
	s_chipbuf_len = 0;
	s_playing = 0;
}

int plat_sound_playing(void)
{
	return s_playing;
}

/* --- four-tone square-wave synth (Mac Sound Manager voice) ----------------
 * The engine's music is the Mac four-tone synth (see the music note); each of
 * the up-to-4 voices maps naturally onto one Paula channel by loading a small
 * square-wave (or the voice's wavetable) and setting the period per voice. */
int plat_sound_synth_start(const void *ftsoundrec)
{
	(void)ftsoundrec;
	/* TODO(hw): for each of the 4 FTSoundRec voices, stage its 256-byte
	 * wavetable in CHIP RAM, set AUDxLC/LEN/PER/VOL from the voice's rate/amp,
	 * and enable that channel's DMA. plat_sound_vbl() advances the phase deltas
	 * exactly like the Atari sequencer does. */
	return 1;
}

void plat_sound_synth_stop(void)
{
	/* TODO(hw): clear the 4 audio DMA channels. */
}

void plat_sound_tone(int count, int amp, int duration_ticks)
{
	(void)count; (void)amp; (void)duration_ticks;
	/* TODO(hw): a single beep on one channel; period from `count`, AUDxVOL from
	 * `amp`, auto-off after duration_ticks VBLs. */
}

/* The engine's sound task (the Mac VBL task driving the sequencer). Stored
 * here; run from plat_sound_vbl once the INTB_VERTB server is wired. */
static void (* volatile s_vbl_hook)(void);

void plat_sound_set_vbl_hook(void (*fn)(void))
{
	s_vbl_hook = fn;
}

void plat_sound_vbl(void)
{
	/* TODO(hw): per-frame audio housekeeping — advance the synth phases, retire
	 * one-shot samples whose length has elapsed, clear DMA for finished voices.
	 * ★ Must NEVER call anything that can trap from interrupt context (the same
	 * rule the Atari VBL sink obeys — see platform/dbglog.c). */
	void (*hook)(void) = s_vbl_hook;

	if (hook != NULL)
		hook();
}

#endif /* FRUA_AMIGA */
