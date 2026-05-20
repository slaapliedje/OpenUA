/*
 * FRUA — Atari Falcon030 / TT030 port.
 *
 * Display bring-up demo: detect the display HAL backend, set a 256-colour
 * mode, draw an XOR test pattern under a 3-3-2 RGB palette, and wait for a
 * key. Boot it with `make run` (Hatari, Falcon mode) to verify the VIDEL
 * backend end to end — mode set, palette, and the chunky-to-planar present.
 *
 * A clean, symmetric plaid means the c2p and stride are right; sheared or
 * scrambled output points at the c2p or the VIDEL mode word.
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

	if (dsp->init(320, 240) != 0) {
		Cconws("display backend init failed\r\n");
		return 1;
	}
	surf = dsp->surface();

	/* A 3-3-2 RGB palette: each index i is its own colour. */
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
	Cconin();                       /* wait for a key */
	dsp->shutdown();
	return 0;
}
