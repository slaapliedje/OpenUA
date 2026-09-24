/*
 * unix_vbl.h — the Unix port's stand-in for a vertical-blank interrupt: the
 * input pump calls unix_vbl_poll(), which runs the engine's VBL task once
 * per elapsed 60 Hz tick (sound_null.c).
 */
#ifndef PLATFORM_UNIX_VBL_H
#define PLATFORM_UNIX_VBL_H

void unix_vbl_poll(void);

#endif /* PLATFORM_UNIX_VBL_H */
