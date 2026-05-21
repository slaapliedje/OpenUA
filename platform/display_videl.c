/*
 * Falcon VIDEL display backend (ADR-0005).
 *
 * Implements the display HAL (platform/include/display.h) on the Atari
 * Falcon030's VIDEL video. The engine draws into a chunky 8-bit surface;
 * the Falcon's 256-colour mode is 8 interleaved bitplanes, so present()
 * does a chunky-to-planar conversion onto the screen.
 *
 * First cut — single-buffered, fixed 320x240, with a naive (correct but
 * slow) c2p. Two things still want verification on Hatari/Falcon: the
 * VIDEL_MODE word (VGA assumed; RGB/TV monitors not handled), and the c2p
 * itself. A fast c2p and true double-buffering are follow-ups.
 */

#include <mint/osbind.h>
#include <mint/falcon.h>
#include <stdlib.h>

#include "display.h"
#include "dbglog.h"

#define VIDEL_W     320
#define VIDEL_H     240

/*
 * VIDEL mode word: VGA monitor, 256 colours (8 planes), 320 px wide.
 * TODO: verify the exact bits on target; handle RGB and TV monitors.
 */
#define VIDEL_MODE  (VGA | VERTFLAG | BPS8)

static dsp_surface_t  g_surface;
static void          *g_screen_raw;     /* the Mxalloc'd block            */
static unsigned char *g_screen;         /* 256-byte-aligned planar screen */
static short          g_save_mode;
static void          *g_save_log;
static void          *g_save_phys;

static int videl_init(short want_w, short want_h)
{
	long size = (long)VIDEL_W * VIDEL_H;        /* 8bpp planar: W*H bytes */
	long raw;

	(void)want_w;
	(void)want_h;

	dbg_log("  videl_init: malloc chunky surface");
	g_surface.pixels = malloc((size_t)size);
	if (g_surface.pixels == NULL) {
		dbg_log("  videl_init: malloc FAILED");
		return -1;
	}
	g_surface.width  = VIDEL_W;
	g_surface.height = VIDEL_H;
	g_surface.pitch  = VIDEL_W;

	dbg_log("  videl_init: Mxalloc screen");
	raw = Mxalloc(size + 256, 0);                /* 0 = ST-RAM */
	if (raw <= 0) {
		dbg_log("  videl_init: Mxalloc FAILED");
		free(g_surface.pixels);
		g_surface.pixels = NULL;
		return -1;
	}
	g_screen_raw = (void *)raw;
	g_screen = (unsigned char *)((raw + 255) & ~255L);

	dbg_log("  videl_init: VsetMode inquire");
	g_save_mode = VsetMode(-1);
	dbg_log("  videl_init: Logbase / Physbase");
	g_save_log  = (void *)Logbase();
	g_save_phys = (void *)Physbase();

	dbg_log("  videl_init: VsetScreen");
	VsetScreen(g_screen, g_screen, -1, VIDEL_MODE);
	dbg_log("  videl_init: done");
	return 0;
}

static void videl_shutdown(void)
{
	VsetScreen(g_save_log, g_save_phys, -1, g_save_mode);
	if (g_screen_raw != NULL) {
		Mfree(g_screen_raw);
		g_screen_raw = NULL;
	}
	free(g_surface.pixels);
	g_surface.pixels = NULL;
}

static dsp_surface_t *videl_surface(void)
{
	return &g_surface;
}

/*
 * Chunky 8-bit -> Falcon 8-plane interleaved. For each group of 16 pixels
 * the screen holds eight 16-bit plane-words; pixel p contributes its bit b
 * to plane b at bit 15-p. Correctness-first — a fast c2p is a known TODO.
 */
static void videl_present(void)
{
	const unsigned char *src = g_surface.pixels;
	unsigned char       *row = g_screen;
	short y, g, p, b;

	Vsync();
	for (y = 0; y < VIDEL_H; y++) {
		for (g = 0; g < VIDEL_W / 16; g++) {
			unsigned short *pw = (unsigned short *)(row + g * 16);
			unsigned short  plane[8];

			for (b = 0; b < 8; b++)
				plane[b] = 0;
			for (p = 0; p < 16; p++) {
				unsigned char c = src[g * 16 + p];
				for (b = 0; b < 8; b++)
					if (c & (1 << b))
						plane[b] |= (unsigned short)(1 << (15 - p));
			}
			for (b = 0; b < 8; b++)
				pw[b] = plane[b];
		}
		src += g_surface.pitch;
		row += VIDEL_W;                  /* 8bpp planar rowbytes == width */
	}
}

static void videl_set_palette(const dsp_color_t *colors, short first,
                              short count)
{
	long  entry[256];
	short i;

	if (count > 256)
		count = 256;
	for (i = 0; i < count; i++)
		entry[i] = ((long)colors[i].r << 16)
		         | ((long)colors[i].g << 8)
		         |  (long)colors[i].b;
	VsetRGB(first, count, entry);
}

static const dsp_backend_t videl_backend = {
	"VIDEL (Falcon)",
	videl_init,
	videl_shutdown,
	videl_surface,
	videl_present,
	videl_set_palette,
};

const dsp_backend_t *dsp_detect(void)
{
	/* TODO: probe the _VDO cookie to tell Falcon (VIDEL) from TT
	 * (TT-shifter). Until the TT backend exists, return VIDEL. */
	return &videl_backend;
}
