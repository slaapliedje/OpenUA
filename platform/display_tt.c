/*
 * Atari TT030 display backend — the TT shifter's TT-low mode.
 *
 * TT low is 320x480 in 8 word-interleaved bitplanes with a 256-entry
 * palette (4 bits per gun) — the same colour depth the engine's chunky
 * buffer carries, at double the line count. The engine's 320x200 frame is
 * therefore LINE-DOUBLED into a 320x400 image centred in the 480 lines,
 * with 40-line black borders top and bottom (the letterbox): every present
 * converts a chunky row once (the shared c2p32 transpose) and writes the
 * planar line twice.
 *
 * Unlike the Falcon backend (VIDEL, 16bpp + LUT blit) this is a paletted
 * target, so palette animation (the fireplace) needs no re-present — the
 * same free ride the Amiga's copper palette gives, via EsetPalette.
 *
 * Mode/screen handling stays on the XBIOS the TT TOS (and EmuTOS) provide:
 * EgetShift/EsetShift for the shifter mode, Physbase/Setscreen for the
 * base, EsetPalette for the colour RAM. Single displayed buffer (rect
 * updates land directly; a small unsynchronised write risks one frame of
 * shear inside the cell, the same policy as the other backends' bring-up).
 *
 * plat_cursor_* stays with the VIDEL backend's definitions (one binary
 * serves both machines); without the VIDEL VBL flip installed those report
 * inactive, so the Toolbox shim composites the pointer in software and
 * pushes it through present_rect — correct, just not sprite-assisted.
 */

#include <mint/osbind.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "display.h"
#include "dbglog.h"
#include "c2p32.h"

#define TT_W        320
#define TT_H        480
#define ENGINE_H    200
#define TOP_BORDER  ((TT_H - ENGINE_H * 2) / 2)     /* 40 letterbox lines */
#define LINE_BYTES  (TT_W)                          /* 8bpl: 1 byte/px    */
#define SCREEN_BYTES ((long)LINE_BYTES * TT_H)

#define TT_SHIFT_TTLOW  0x0700      /* 0xFF8262 mode field: TT low */

static unsigned char *g_screen_raw;         /* Mxalloc'd block          */
static unsigned char *g_screen;             /* 256-aligned screen base  */
static unsigned char  g_chunky[TT_W * ENGINE_H];
static dsp_surface_t  g_surface;
static short          g_save_shift = -1;
static void          *g_save_phys, *g_save_log;

/* Convert one 16-pixel-aligned span of a chunky row into TT interleaved
 * planes at `dst` (8 words per 16-pixel group: plane 0..7), then the caller
 * duplicates the line. `w` and the source offset are multiples of 32 except
 * a possible 16-pixel tail. */
static void tt_c2p_span(const unsigned char *src, unsigned char *dst, short w)
{
	short x;

	for (x = 0; x + 32 <= w; x += 32) {
		c2p_u32 c[8], o[8];
		unsigned short *d = (unsigned short *)(dst + (long)(x / 16) * 16);
		short p;

		c2p_load32(src + x, c);
		c2p_transpose32(c, o);
		for (p = 0; p < 8; p++) {
			d[p]     = (unsigned short)(o[p] >> 16);   /* pixels 0-15  */
			d[p + 8] = (unsigned short)(o[p]);         /* pixels 16-31 */
		}
	}
	if (x < w) {                            /* 16-pixel tail */
		c2p_u32 c[8], o[8];
		unsigned char pad[32];
		unsigned short *d = (unsigned short *)(dst + (long)(x / 16) * 16);
		short p;

		memcpy(pad, src + x, 16);
		memset(pad + 16, 0, 16);
		c2p_load32(pad, c);
		c2p_transpose32(c, o);
		for (p = 0; p < 8; p++)
			d[p] = (unsigned short)(o[p] >> 16);
	}
}

static int tt_init(short want_w, short want_h)
{
	(void)want_w; (void)want_h;             /* fixed 320x200 engine frame */

	g_screen_raw = (unsigned char *)Mxalloc(SCREEN_BYTES + 256, 0); /* ST-RAM */
	if (g_screen_raw == NULL) {
		dbg_log("tt: Mxalloc screen FAILED");
		return 1;
	}
	g_screen = (unsigned char *)
	    (((uintptr_t)g_screen_raw + 255) & ~(uintptr_t)255);
	memset(g_screen, 0, SCREEN_BYTES);      /* black letterbox borders */
	memset(g_chunky, 0, sizeof g_chunky);

	g_save_shift = EgetShift();
	g_save_phys  = Physbase();
	g_save_log   = Logbase();

	dbg_log_num("tt: old shift mode = ", (long)g_save_shift);
	/* Physical base = our screen; LOGICAL base stays on the old TOS
	 * screen so the VT52 console (Cconws boot/debug prints) keeps
	 * rendering into the hidden buffer instead of scribbling over the
	 * letterbox. */
	Setscreen(g_save_log, g_screen, -1);
	EsetShift(TT_SHIFT_TTLOW);
	dbg_log("tt: TT-low takeover done");

	g_surface.width  = TT_W;
	g_surface.height = ENGINE_H;
	g_surface.pitch  = TT_W;
	g_surface.pixels = g_chunky;
	return 0;
}

static void tt_shutdown(void)
{
	if (g_save_shift >= 0) {
		EsetShift((short)g_save_shift);
		Setscreen(g_save_log, g_save_phys, -1);
		g_save_shift = -1;
	}
	if (g_screen_raw != NULL) {
		Mfree(g_screen_raw);
		g_screen_raw = NULL;
		g_screen     = NULL;
	}
}

static dsp_surface_t *tt_surface(void)
{
	return &g_surface;
}

/* Convert + line-double the given chunky rows into the letterboxed screen. */
static void tt_blit_rows(short x0, short w, short y0, short h)
{
	short y;

	for (y = 0; y < h; y++) {
		const unsigned char *src =
		    g_chunky + (long)(y0 + y) * TT_W + x0;
		unsigned char *dst = g_screen
		    + (long)(TOP_BORDER + (y0 + y) * 2) * LINE_BYTES
		    + (long)(x0 / 16) * 16;

		tt_c2p_span(src, dst, w);
		memcpy(dst + LINE_BYTES, dst, (size_t)((w / 16) * 16));
	}
}

static void tt_present(void)
{
	tt_blit_rows(0, TT_W, 0, ENGINE_H);
}

static void tt_present_rect(short x, short y, short w, short h)
{
	short x1;

	if (x < 0) { w = (short)(w + x); x = 0; }
	if (y < 0) { h = (short)(h + y); y = 0; }
	if (x + w > TT_W)     w = (short)(TT_W - x);
	if (y + h > ENGINE_H) h = (short)(ENGINE_H - y);
	if (w <= 0 || h <= 0)
		return;

	x1 = (short)((x + w + 15) & ~15);       /* 16-pixel plane groups */
	x  = (short)(x & ~15);
	tt_blit_rows(x, (short)(x1 - x), y, h);
}

static void tt_set_palette(const dsp_color_t *colors, short first, short count)
{
	static unsigned short pal[256];
	short i;

	if (first < 0 || count <= 0 || first >= 256)
		return;
	if (first + count > 256)
		count = (short)(256 - first);
	for (i = 0; i < count; i++) {
		/* TT colour word: %0000 RRRR GGGG BBBB, 4 bits per gun. */
		pal[first + i] = (unsigned short)
		    (((colors[i].r & 0xF0) << 4)
		     | (colors[i].g & 0xF0)
		     | ((colors[i].b & 0xF0) >> 4));
	}
	EsetPalette(first, count, (short *)&pal[first]);
}

static const dsp_backend_t tt_backend = {
	"TT shifter (TT low, line-doubled)",
	tt_init,
	tt_shutdown,
	tt_surface,
	tt_present,
	tt_present_rect,
	tt_set_palette,
};

const dsp_backend_t *dsp_backend_tt(void)
{
	return &tt_backend;
}
