/*
 * FRUA — Atari Falcon030 / TT030 port.
 *
 * QuickDraw / display-HAL diagnostic. Brings up the VIDEL backend, binds
 * QuickDraw to the back buffer via qd_attach_screen, then exercises
 * EraseRect / PaintRect on the screen port — one full-screen erase plus a
 * nested foreground rect. Each stage logs to C:\DEMO.LOG via dbg_log() so
 * the trail survives a crash and can be read on the host.
 *
 * Reverts to the real engine bootstrap once the path is verified.
 */

#include <mint/osbind.h>

#include "display.h"
#include "dbglog.h"
#include "quickdraw.h"

int main(void)
{
	const dsp_backend_t *dsp;
	dsp_surface_t *surf;
	dsp_color_t    pal[256];
	Rect           r;
	int            i;

	dbg_log("main: entered");

	dsp = dsp_detect();
	dbg_log("main: backend =");
	dbg_log(dsp->name);

	if (dsp->init(320, 240) != 0) {
		dbg_log("main: init returned failure");
		return 1;
	}
	dbg_log("main: init ok");

	surf = dsp->surface();
	dbg_log("main: surface ok");

	/* A 332 RGB ramp so the index-as-colour scheme makes sense visually:
	 * index 0 is black, index 255 is white, the middle is colour. */
	for (i = 0; i < 256; i++) {
		pal[i].r = (unsigned char)(i & 0xE0);
		pal[i].g = (unsigned char)((i << 3) & 0xE0);
		pal[i].b = (unsigned char)((i << 6) & 0xC0);
	}
	dsp->set_palette(pal, 0, 256);
	dbg_log("main: palette ok");

	qd_attach_screen(surf->pixels, surf->pitch, surf->width, surf->height);
	dbg_log("main: screen port attached");

	SetRect(&r, 0, 0, surf->width, surf->height);
	EraseRect(&r);                          /* fill background (index 0)   */
	dbg_log("main: erase ok");

	SetRect(&r, 40, 40, surf->width - 40, surf->height - 40);
	PaintRect(&r);                          /* fill foreground (index 255) */
	dbg_log("main: paint ok");

	dsp->present();
	dbg_log("main: present ok");

	Crawcin();                              /* hold until a keypress */
	dbg_log("main: hold ok");

	dsp->shutdown();
	dbg_log("main: shutdown ok");
	return 0;
}
