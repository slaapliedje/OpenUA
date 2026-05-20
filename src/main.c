/*
 * FRUA — Atari Falcon030 / TT030 port.
 *
 * Display bring-up demo / diagnostic. Prints progress to the console, then
 * brings up the VIDEL backend, draws an XOR test pattern under a 3-3-2 RGB
 * palette, and waits for a key. Boot it with `make run` (Hatari, Falcon).
 *
 * Each stage is reported and every exit path pauses, so a failure is
 * readable rather than a flash back to the desktop.
 *
 * This is the program entry point; it becomes the real engine bootstrap
 * once the lifted engine is wired up.
 */

#include <mint/osbind.h>

#include "display.h"

int main(void)
{
	const dsp_backend_t *dsp = dsp_detect();
	dsp_surface_t *surf;
	dsp_color_t    pal[256];
	int            x, y, i;

	Cconws("\r\nFRUA VIDEL display demo\r\n");
	Cconws("backend: ");
	Cconws(dsp->name);
	Cconws("\r\ninitialising display...\r\n");

	if (dsp->init(320, 240) != 0) {
		Cconws("display init FAILED -- press a key\r\n");
		Cconin();
		return 1;
	}

	/* The video mode is switched now; the picture takes the screen. */
	surf = dsp->surface();

	for (i = 0; i < 256; i++) {
		pal[i].r = (unsigned char)(i & 0xE0);
		pal[i].g = (unsigned char)((i << 3) & 0xE0);
		pal[i].b = (unsigned char)((i << 6) & 0xC0);
	}
	dsp->set_palette(pal, 0, 256);

	/* An XOR pattern — a clean plaid confirms the c2p and stride. */
	for (y = 0; y < surf->height; y++)
		for (x = 0; x < surf->width; x++)
			surf->pixels[y * surf->pitch + x] = (unsigned char)(x ^ y);

	dsp->present();
	Cconin();                       /* wait for a key, on the picture */
	dsp->shutdown();

	Cconws("display demo done -- press a key\r\n");
	Cconin();
	return 0;
}
