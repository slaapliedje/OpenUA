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

/* 1 if the DMA chip is currently playing, 0 otherwise. */
int  plat_sound_playing(void);

#endif /* PLATFORM_SOUND_H */
