/*
 * x11_unix.h — what the X11 display backend (display_x11.c) shares with the
 * X11 input backend (input_x11.c): one connection, one window.
 */
#ifndef PLATFORM_X11_UNIX_H
#define PLATFORM_X11_UNIX_H

#include <X11/Xlib.h>

Display *x11_display(void);	/* NULL until the display backend's init */
Window   x11_window(void);
int      x11_scale(void);	/* window pixels per game pixel */
void     x11_repaint(void);	/* redraw what the window showed (Expose) */
void     x11_flush_due(void);	/* send a presented frame once its tick has passed */

#endif /* PLATFORM_X11_UNIX_H */
