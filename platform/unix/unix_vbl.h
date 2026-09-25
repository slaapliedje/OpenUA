/*
 * unix_vbl.h — the Unix port's stand-in for a vertical-blank interrupt: the
 * input pump (and FSRead, between the pieces of a large read) calls
 * unix_vbl_poll(), which tops up the sound and runs the engine's VBL task
 * once per elapsed 60 Hz tick (sound_unix.c).
 */
#ifndef PLATFORM_UNIX_VBL_H
#define PLATFORM_UNIX_VBL_H

void unix_vbl_poll(void);

/* Nonzero while the VBLs run: plat_ticks is then just the clock, and a
 * nested unix_vbl_poll does nothing. */
extern volatile int unix_in_vbl;

/* The 60 Hz tick count, for the port's own use: unlike plat_ticks() it
 * never counts as the engine waiting (see input_x11.c). */
unsigned long unix_ticks(void);

#endif /* PLATFORM_UNIX_VBL_H */
