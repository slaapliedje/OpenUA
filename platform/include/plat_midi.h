/*
 * MIDI music — the DOS release's XMIDI scores (RODQ1-3.XMI, the Roland
 * MT-32 arrangement) played out the machine's MIDI port instead of the Mac
 * four-tone synth. The generic half is platform/midi_seq.c; the port is
 * platform/midi_out_acia.c on Atari and platform/midi_none.c elsewhere.
 *
 * Opt-in through sound.cfg next to the program (see midi_out_acia.c).
 */
#ifndef PLAT_MIDI_H
#define PLAT_MIDI_H

/* Read sound.cfg and arm the player. 0 = MIDI music is on, -1 = it is not
 * (no file, music=synth, no port). Called by the sound HAL once the vblank
 * that drives plat_midi_vbl() exists. */
int  plat_midi_init(void);
int  plat_midi_enabled(void);

/* Parse one XMIDI file out of the caller's buffer (nothing is kept of it)
 * and start playing it, looping. 0 when the port took the song, -1 when it
 * did not — the four-tone synth plays it then. Normal context only. */
int  plat_midi_song(const unsigned char *xmi, long len);
void plat_midi_stop(void);              /* notes off, forget the song */
void plat_midi_mute(int muted);         /* the engine's sound on/off latch */
int  plat_midi_playing(void);

/* From the sound vblank (supervisor): advance the score by the 200 Hz
 * count and transmit a few queued bytes. */
void plat_midi_vbl(unsigned long hz200);
/* From the main loop: transmit whatever is queued. Cheap when idle. */
void plat_midi_poll(void);
void plat_midi_shutdown(void);

/* --- the platform half (midi_out_*.c) --------------------------------- */
int  midi_hw_ready(void);               /* transmitter empty (supervisor) */
void midi_hw_put(unsigned char b);      /* one byte to the port (supervisor) */
void midi_hw_flush(void);               /* drain the queue from normal context */

/* --- what midi_seq.c offers the platform half ---------------------------- */
void plat_midi_configure(int enabled, int gm);
/* Parse n bytes of sound.cfg in buf (room for one more) and configure;
 * 0 when MIDI music is on. */
int  midi_cfg_apply(char *buf, long n);
int  midi_seq_pending(void);
/* Send up to `budget` bytes (-1 = all), waiting at most `wait_iters` status
 * reads for each. Supervisor context. */
void midi_seq_drain(long budget, long wait_iters);

#endif /* PLAT_MIDI_H */
