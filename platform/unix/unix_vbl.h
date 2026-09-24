/*
 * unix_vbl.h — the Unix port's stand-in for a vertical-blank interrupt: the
 * input pump calls unix_vbl_poll(), which runs the engine's VBL task once
 * per elapsed 60 Hz tick (sound_null.c).
 */
#ifndef PLATFORM_UNIX_VBL_H
#define PLATFORM_UNIX_VBL_H

void unix_vbl_poll(void);

/* The 60 Hz tick count, for the port's own use: unlike plat_ticks() it
 * never counts as the engine waiting (see input_x11.c). */
unsigned long unix_ticks(void);

#endif /* PLATFORM_UNIX_VBL_H */
