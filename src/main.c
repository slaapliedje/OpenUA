/*
 * FRUA — Atari Falcon030 / TT030 port.
 *
 * Display backend diagnostic. The on-screen demo crashes too fast to read,
 * so this logs each stage to C:\DEMO.LOG via dbg_log() — the file is closed
 * after every line, so the trail survives a crash and can be read on the
 * host. Run with `make run`, then inspect DEMO.LOG in the project directory.
 *
 * Reverts to the real engine bootstrap once the backend is verified.
 */

#include <mint/osbind.h>

#include "display.h"
#include "dbglog.h"

int main(void)
{
	const dsp_backend_t *dsp;
	dsp_surface_t *surf;
	dsp_color_t    pal[256];
	int            x, y, i;

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

	for (i = 0; i < 256; i++) {
		pal[i].r = (unsigned char)(i & 0xE0);
		pal[i].g = (unsigned char)((i << 3) & 0xE0);
		pal[i].b = (unsigned char)((i << 6) & 0xC0);
	}
	dsp->set_palette(pal, 0, 256);
	dbg_log("main: palette ok");

	for (y = 0; y < surf->height; y++)
		for (x = 0; x < surf->width; x++)
			surf->pixels[y * surf->pitch + x] = (unsigned char)(x ^ y);
	dbg_log("main: pattern drawn");

	dsp->present();
	dbg_log("main: present ok");

	Crawcin();                              /* hold the plaid until a keypress */
	dbg_log("main: hold ok");

	dsp->shutdown();
	dbg_log("main: shutdown ok");
	return 0;
}
