/*
 * Platform sound HAL.
 *
 * Falcon target: routes through the XBIOS Locksnd / Setbuffer /
 * Setmode / Devconnect / Buffoper sequence to drive DMA playback off
 * the CODEC. The Sound Manager shim (compat/sound.c) parses Mac `snd `
 * resources and calls plat_sound_play_mono8 with the converted (Mac
 * unsigned → Falcon signed) sample buffer.
 *
 * Mono 8-bit playback is the first beachhead; stereo / 16-bit / loop
 * support arrives when an engine path actually demands them. The
 * Falcon driver allocates its DMA buffer from ST-RAM via Mxalloc(0);
 * the host-side HAL stub fakes the same surface with a no-op.
 */

#ifndef PLATFORM_SOUND_H
#define PLATFORM_SOUND_H

/*
 * Claim the sound hardware. Returns 0 on success, -1 on failure (the
 * Falcon DMA chip is locked by another process — usually the Sound
 * Manager itself). Pair with plat_sound_shutdown.
 */
int  plat_sound_init(void);

/* Stop any current playback and release the sound hardware. */
void plat_sound_shutdown(void);

/*
 * Play `count` signed-8-bit mono samples at approximately `rate_hz` Hz.
 * The HAL copies into an ST-RAM DMA buffer (allocated on demand and
 * resized when needed) and kicks the Falcon DMA chip. Returns 0 on
 * success, -1 on failure. Asynchronous — playback continues until
 * the buffer ends or plat_sound_stop fires.
 */
int  plat_sound_play_mono8(const signed char *samples, long count,
                           int rate_hz);

/* Stop the current playback (if any). */
void plat_sound_stop(void);

/* 1 if an effect is still playing, 0 otherwise. */
int  plat_sound_playing(void);

/* --- the four-tone synth (FRUA's music) -------------------------------------
 *
 * FRUA's music is NOT sampled: the engine drives the Mac Sound Driver's
 * four-tone synthesizer, which the Mac ROM rendered in software. The port has
 * to render it too, so the HAL owns a 4-voice wavetable synth.
 *
 * `ftsoundrec` is the Mac FTSoundRec the engine keeps in its A5 world and
 * rewrites LIVE while the note plays (jt974 -> jt1131 -> jt1122 poke the four
 * rate fields every tick), so the HAL holds the POINTER and re-reads it every
 * refill rather than copying:
 *
 *     +0   word   duration
 *     +2   Fixed  sound1Rate      (16.16; 0 = voice silent)
 *     +6   long   sound1Phase
 *     ...            .. four voices, 8 bytes each ..
 *     +34  ptr    sound1Wave      (256 unsigned bytes, 128 = centre)
 *     ...            .. four wave pointers ..
 *
 * A rate is a Fixed step through the 256-byte wave at the Mac's 22254.5454 Hz;
 * the HAL rescales it to the CODEC rate it can actually clock.
 *
 * Once started, the synth owns the DMA in loop mode and the VBL keeps it fed —
 * so it must never be programmed from interrupt context again. Returns 0 on
 * success. plat_sound_synth_stop() silences the voices but leaves the loop
 * running (a stopped synth renders silence).
 */
int  plat_sound_synth_start(const void *ftsoundrec);
void plat_sound_synth_stop(void);

/*
 * Play one square-wave tone: the Mac's swMode, whose `count` is a period —
 * frequency = 783360 / count — with `amp` 0..255 and `duration` in ticks.
 */
void plat_sound_tone(int count, int amp, int duration_ticks);

/*
 * Refill the synth's DMA loop. Installed on the VBL by the HAL itself; does
 * nothing until the synth has been started. Touches memory only (no XBIOS), so
 * it is safe at interrupt time.
 */
void plat_sound_vbl(void);

#endif /* PLATFORM_SOUND_H */
