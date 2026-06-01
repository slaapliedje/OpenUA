/*
 * Falcon VIDEL display backend (ADR-0005).
 *
 * Implements the display HAL (platform/include/display.h) on the Atari
 * Falcon030's VIDEL video. The engine draws into a chunky 8-bit surface;
 * the Falcon's 256-colour mode is 8 interleaved bitplanes, so present()
 * does a chunky-to-planar conversion onto the screen.
 *
 * init() switches the VIDEL to a native 256-colour mode (the desktop
 * mode with STMODES cleared and 8 planes / VERTFLAG set) and sizes the
 * planar screen from VgetSize().
 *
 * First cut — single-buffered, with a naive (correct but slow) c2p. A fast
 * c2p and double-buffering are follow-ups.
 */

#include <mint/osbind.h>
#include <mint/falcon.h>
#include <mint/linea.h>
#include <stdlib.h>

#include "display.h"
#include "dbglog.h"

static dsp_surface_t  g_surface;
static void          *g_screen_raw;     /* the Mxalloc'd block            */
static unsigned char *g_screen;         /* 256-byte-aligned planar screen */
static short          g_save_mode;
static void          *g_save_log;
static void          *g_save_phys;
static long           g_save_palette[256];   /* desktop palette, restored on exit */

static int videl_init(short want_w, short want_h)
{
	short w, h, newmode;
	long  bytes, raw;

	(void)want_w;
	(void)want_h;

	/* Save the current mode, screen base, and palette for shutdown. */
	g_save_mode = VsetMode(-1);
	g_save_log  = (void *)Logbase();
	g_save_phys = (void *)Physbase();
	VgetRGB(0, 256, g_save_palette);

	/* 256 colours needs a VIDEL-native mode: clear STMODES — the
	 * ST-shifter compatibility flag, which caps at 16 colours — and set
	 * 8 planes. VERTFLAG selects the 400-line variant. Switch the mode
	 * first, then size the screen from VgetSize(): line-A's V_Y_MAX
	 * reports 200 for this mode and can't be trusted for the height. */
	newmode = (short)((g_save_mode & ~(STMODES | 7)) | BPS8 | VERTFLAG);
	dbg_log_num("  videl_init: old mode = ", g_save_mode);
	dbg_log_num("  videl_init: new mode = ", newmode);

	VsetMode(newmode);
	linea0();
	w = (short)V_X_MAX;
	bytes = VgetSize(newmode);                   /* 8bpp planar: W*H bytes */
	h = (short)(bytes / w);
	dbg_log_num("  videl_init: width    = ", w);
	dbg_log_num("  videl_init: height   = ", h);
	dbg_log_num("  videl_init: bytes    = ", bytes);
	dbg_log_num("  videl_init: V_Y_MAX  = ", V_Y_MAX);

	g_surface.pixels = malloc((size_t)bytes);
	if (g_surface.pixels == NULL) {
		dbg_log("  videl_init: malloc FAILED");
		VsetMode(g_save_mode);
		return -1;
	}
	g_surface.width  = w;
	g_surface.height = h;
	g_surface.pitch  = w;

	raw = Mxalloc(bytes + 256, 0);               /* 0 = ST-RAM */
	if (raw <= 0) {
		dbg_log("  videl_init: Mxalloc FAILED");
		free(g_surface.pixels);
		g_surface.pixels = NULL;
		VsetMode(g_save_mode);
		return -1;
	}
	g_screen_raw = (void *)raw;
	g_screen = (unsigned char *)((raw + 255) & ~255L);

	VsetScreen(g_screen, g_screen, -1, -1);      /* point the VIDEL at it */
	dbg_log("  videl_init: done");
	return 0;
}

static void videl_shutdown(void)
{
	VsetMode(g_save_mode);                     /* restore the desktop mode  */
	VsetScreen(g_save_log, g_save_phys, -1, -1);
	VsetRGB(0, 256, g_save_palette);           /* restore the desktop palette */
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
 * 8x8 bit-matrix transpose (Hacker's Delight): the 8 bytes packed into x
 * (byte i = matrix row i) come back with rows<->columns swapped. The c2p
 * uses it to gather, for each of 8 source pixels, bit b of all of them
 * into plane b — turning the per-pixel/per-plane double loop (128 bit
 * tests per 16 px) into two transposes.
 */
static unsigned long long c2p_transpose8(unsigned long long x)
{
	x = (x & 0xAA55AA55AA55AA55ULL)
	  | ((x & 0x00AA00AA00AA00AAULL) << 7)
	  | ((x >> 7) & 0x00AA00AA00AA00AAULL);
	x = (x & 0xCCCC3333CCCC3333ULL)
	  | ((x & 0x0000CCCC0000CCCCULL) << 14)
	  | ((x >> 14) & 0x0000CCCC0000CCCCULL);
	x = (x & 0xF0F0F0F00F0F0F0FULL)
	  | ((x & 0x00000000F0F0F0F0ULL) << 28)
	  | ((x >> 28) & 0x00000000F0F0F0F0ULL);
	return x;
}

/*
 * Chunky 8-bit -> Falcon 8-plane interleaved, via the bit transpose above.
 * Per 16-pixel group the screen holds eight 16-bit plane-words; pixel p
 * contributes its bit b to plane b at bit 15-p. We split the 16 pixels
 * into two 8-pixel halves, transpose each (so transposed byte b holds bit
 * b of the 8 source pixels, pixel 0 in the MSB), and pack: plane b's high
 * byte = bit-b row of pixels 0..7, low byte = pixels 8..15. Verified
 * byte-identical to the naive loop. (Vsync first, as before.)
 */
/* c2p the 16-pixel-group columns [g0,g1) of rows [y0,y1) of the surface. */
static void videl_c2p_rows(short y0, short y1, short g0, short g1)
{
	short w = g_surface.width;
	const unsigned char *src = g_surface.pixels + (long)y0 * g_surface.pitch;
	unsigned char       *row = g_screen + (long)y0 * w;
	short y, g, b;

	for (y = y0; y < y1; y++) {
		for (g = g0; g < g1; g++) {
			const unsigned char *s = src + g * 16;
			unsigned short      *pw = (unsigned short *)(row + g * 16);
			unsigned long long   xhi = 0, xlo = 0, thi, tlo;
			short                i;

			for (i = 0; i < 8; i++) {
				xhi = (xhi << 8) | s[i];
				xlo = (xlo << 8) | s[i + 8];
			}
			thi = c2p_transpose8(xhi);
			tlo = c2p_transpose8(xlo);
			for (b = 0; b < 8; b++)
				pw[b] = (unsigned short)
				        ((((thi >> (8 * b)) & 0xff) << 8)
				         | ((tlo >> (8 * b)) & 0xff));
		}
		src += g_surface.pitch;
		row += w;                        /* 8bpp planar rowbytes == width */
	}
}

static void videl_present(void)
{
	Vsync();
	videl_c2p_rows(0, g_surface.height, 0, g_surface.width / 16);
}

/* Convert only the dirty rect, snapped out to 16-pixel group columns and
 * clamped to the surface — the static parts of the screen are left as-is. */
static void videl_present_rect(short x, short y, short w, short h)
{
	short gmax = g_surface.width / 16;
	short g0 = (short)(x / 16);
	short g1 = (short)((x + w + 15) / 16);
	short y1 = (short)(y + h);

	if (y < 0) y = 0;
	if (y1 > g_surface.height) y1 = g_surface.height;
	if (g0 < 0) g0 = 0;
	if (g1 > gmax) g1 = gmax;
	if (y1 <= y || g1 <= g0)
		return;
	Vsync();
	videl_c2p_rows(y, y1, g0, g1);
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
	videl_present_rect,
	videl_set_palette,
};

const dsp_backend_t *dsp_detect(void)
{
	/* TODO: probe the _VDO cookie to tell Falcon (VIDEL) from TT
	 * (TT-shifter). Until the TT backend exists, return VIDEL. */
	return &videl_backend;
}
