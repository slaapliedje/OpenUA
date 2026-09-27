/*
 * The software mixer behind the DMA-sound backends: FRUA's four-tone music
 * (the Mac Sound Driver's ftMode, which the Mac ROM rendered in software),
 * the swMode square-wave tone, and one sampled effect mixed on top. It
 * renders signed 8-bit mono at mix_hz; the backend owns getting the
 * samples to the hardware (a DMA ring on the Falcon and the TT under TOS,
 * /dev/audio under Unix). See platform/sound_mix.c.
 */
#ifndef SOUND_MIX_H
#define SOUND_MIX_H

/* The rate the mixer renders at, set by the backend before anything
 * sounds: 24585 (Falcon CODEC) or 25033 (STe/TT DMA sound). */
extern long mix_hz;

/* Zero the four voices' phases (a fresh loop). */
void mix_reset_phases(void);

/* The LIVE FTSoundRec the sequencer rewrites while it plays (see
 * plat_sound.h), or NULL to silence the voices. */
void mix_set_record(const void *ftsoundrec);

/* 1 if a voice, the tone or the effect would make a sound. */
int  mix_audible(void);

/* Render n samples of voices + tone + effect into dst. */
void mix_render(char *dst, long n);

/* The same as n interleaved left/right frames (2n bytes), the voices
 * panned apart (see sound_mix.c). */
void mix_render_stereo(char *dst, long n);

/* swMode: frequency = 783360 / count, amp 0..255, duration in ticks. */
void mix_tone(int count, int amp, int duration_ticks);
void mix_tone_off(void);

/* A sampled effect (signed 8-bit mono at rate_hz), resampled to mix_hz
 * and mixed on top of the music until it runs out. */
void mix_effect(const signed char *samples, long count, int rate_hz);
void mix_effect_stop(void);
int  mix_effect_playing(void);

/* An optional high-pass filter for a finished buffer: two one-pole stages
 * (12 dB/octave) at hz, or off for hz <= 0. FRUA's music reaches down to
 * ~26 Hz, which a Mac's speaker never reproduced; a full-range speaker
 * does, as a deep drone under the tune. */
void mix_set_highpass(int hz);
void mix_highpass(char *buf, long n);
void mix_highpass_stereo(char *buf, long n);	/* n frames */

#endif /* SOUND_MIX_H */
