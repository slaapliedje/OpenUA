/*
 * Falcon VIDEL display backend (ADR-0005) — 16bpp TrueColor LUT path.
 *
 * The engine draws into a chunky 8-bit surface (unchanged). Instead of the
 * old chunky-to-planar (C2P) conversion onto an 8-plane 256-colour screen —
 * which shuffles bits across 8 interleaved planes and cost a full pass plus a
 * Vsync per present — this backend runs the VIDEL in 16bpp TrueColor and
 * present() is a tight 8->16 LUT blit: each chunky byte indexes a 256-entry
 * RGB565 lookup table, written as one word to the screen. No plane shuffling,
 * no palette hardware in the hot path (the LUT is rebuilt only on a palette
 * change), and the flip latches at the next vblank without the per-present
 * Vsync stall that caused the input lag.
 *
 * Double-buffered in ST-RAM; VsetScreen latches physbase at the next vblank,
 * so present() returns immediately and the beam still only scans a complete
 * buffer. The 8-bit surface is sized to the VIDEL mode (V_X_MAX x V_Y_MAX);
 * the game uses its top 320x200, the rest is the letterbox surround.
 *
 * First cut: portable C LUT blit. The 68030 asm inner loop (longword reads,
 * 4 px/iteration) is a follow-up behind a self-test gate, like the old C2P.
 */

#include <mint/osbind.h>
#include <mint/falcon.h>
#include <mint/linea.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "dbglog.h"

static dsp_surface_t  g_surface;          /* the 8-bit chunky buffer the game draws */

/* Double-buffered 16bpp screen in ST-RAM. present() always writes the back
 * (off-screen) buffer, then points the VIDEL base at it; the latch happens at
 * the next vblank, so the beam never scans a half-blitted frame. */
static void          *g_screen_raw[2];    /* the two Mxalloc'd blocks         */
static unsigned short *g_screen[2];        /* 256-byte-aligned 16bpp screens   */
static short          g_front;            /* index currently displayed        */
static short          g_scr_words;        /* 16bpp screen rowbytes, in words  */
static long           g_screen_bytes;     /* size of one 16bpp buffer         */
static short          g_save_mode;
static void          *g_save_log;
static void          *g_save_phys;
static long           g_save_palette[256];

/* 8-bit palette index -> RGB565 word. Rebuilt by videl_set_palette. */
static unsigned short g_lut[256];

static int videl_init(short want_w, short want_h)
{
	short w, h, newmode;
	long  bytes, raw;
	short b;

	(void)want_w;
	(void)want_h;

	g_save_mode = VsetMode(-1);
	g_save_log  = (void *)Logbase();
	g_save_phys = (void *)Physbase();
	VgetRGB(0, 256, g_save_palette);

	/* 16bpp TrueColor, same geometry as the 8bpp path: 320x240 on VGA
	 * (VGA + double-line), 320x200 on an RGB/TV monitor. BPS16 swaps the
	 * 256-colour planar mode for a 1-word-per-pixel chunky screen, so no
	 * c2p. (If a monitor needs COL80 for 320-wide 16bpp, that is the knob
	 * to flip — the logged width below tells.) */
	if (g_save_mode & VGA)
		newmode = (short)(VGA | VERTFLAG | BPS16);   /* 320x240 16bpp */
	else
		newmode = (short)(BPS16);                    /* 320x200 16bpp */
	dbg_log_num("  videl_init: old mode = ", g_save_mode);
	dbg_log_num("  videl_init: new mode = ", newmode);

	VsetMode(newmode);
	linea0();
	w = (short)V_X_MAX;
	bytes = VgetSize(newmode);                   /* 16bpp: W*H*2 bytes */
	h = (short)(bytes / ((long)w * 2));
	g_scr_words = (short)(bytes / h / 2);        /* rowbytes / 2 = words/row */
	dbg_log_num("  videl_init: width    = ", w);
	dbg_log_num("  videl_init: height   = ", h);
	dbg_log_num("  videl_init: bytes    = ", bytes);
	dbg_log_num("  videl_init: words/row= ", g_scr_words);

	/* The 8-bit chunky surface the game renders into (unchanged shape). */
	g_surface.pixels = malloc((size_t)((long)w * h));
	if (g_surface.pixels == NULL) {
		dbg_log("  videl_init: surface malloc FAILED");
		VsetMode(g_save_mode);
		return -1;
	}
	g_surface.width  = w;
	g_surface.height = h;
	g_surface.pitch  = w;
	memset(g_surface.pixels, 0, (size_t)((long)w * h));

	/* Two 16bpp ST-RAM screens (the VIDEL DMAs them, so ST-RAM). */
	g_screen_bytes = bytes;
	for (b = 0; b < 2; b++) {
		raw = Mxalloc(bytes + 256, 0);           /* 0 = ST-RAM */
		if (raw <= 0) {
			dbg_log("  videl_init: Mxalloc FAILED");
			if (b > 0) Mfree(g_screen_raw[0]);
			free(g_surface.pixels);
			g_surface.pixels = NULL;
			VsetMode(g_save_mode);
			return -1;
		}
		g_screen_raw[b] = (void *)raw;
		g_screen[b] = (unsigned short *)((raw + 255) & ~255L);
		memset(g_screen[b], 0, (size_t)bytes);   /* black, incl. letterbox */
	}
	g_front = 0;
	VsetScreen(g_screen[0], g_screen[0], -1, -1);

	/* Black the VIDEL overscan border: even in TrueColor the border colour is
	 * driven by Falcon palette register 0 (the desktop left it light). The
	 * engine no longer uses the hardware palette — pixels go through the LUT —
	 * so index 0 is free to pin black. */
	{
		long black = 0;
		VsetRGB(0, 1, &black);
	}

	/* Seed the LUT from the desktop palette so an early present isn't pure
	 * black; the engine reinstalls its own CLUT (clut 129) on boot. */
	{
		short i;
		for (i = 0; i < 256; i++) {
			unsigned long e = (unsigned long)g_save_palette[i];
			unsigned short r = (unsigned short)((e >> 16) & 0xff);
			unsigned short gg = (unsigned short)((e >> 8) & 0xff);
			unsigned short bb = (unsigned short)(e & 0xff);
			g_lut[i] = (unsigned short)(((r & 0xf8) << 8)
			                          | ((gg & 0xfc) << 3)
			                          | (bb >> 3));
		}
	}

	dbg_log("  videl_init: done (16bpp LUT)");
	return 0;
}

static void videl_shutdown(void)
{
	short b;
	VsetMode(g_save_mode);
	VsetScreen(g_save_log, g_save_phys, -1, -1);
	VsetRGB(0, 256, g_save_palette);
	for (b = 0; b < 2; b++)
		if (g_screen_raw[b] != NULL) {
			Mfree(g_screen_raw[b]);
			g_screen_raw[b] = NULL;
		}
	free(g_surface.pixels);
	g_surface.pixels = NULL;
}

static dsp_surface_t *videl_surface(void)
{
	return &g_surface;
}

/* 8->16 LUT blit: chunky rows [y0,y1), columns [x0,x1) -> the 16bpp buffer
 * `dst`. One word per pixel via the RGB565 LUT. The C reference; a 68030 asm
 * inner loop (longword reads, 4 px/iter) is the follow-up. */
static void videl_lut_blit(unsigned short *dst, short y0, short y1,
                           short x0, short x1)
{
	short y, x, n = (short)(x1 - x0);
	const unsigned char *src = g_surface.pixels
	                         + (long)y0 * g_surface.pitch + x0;
	unsigned short      *row = dst + (long)y0 * g_scr_words + x0;

	for (y = y0; y < y1; y++) {
		for (x = 0; x < n; x++)
			row[x] = g_lut[src[x]];
		src += g_surface.pitch;
		row += g_scr_words;
	}
}

/* Flip: point the VIDEL base at the just-blitted back buffer. VsetScreen
 * latches physbase at the next vblank, so this returns immediately (no Vsync
 * stall) and the beam still only scans a complete buffer. */
static void videl_flip(unsigned short *back)
{
	VsetScreen(back, back, -1, -1);
}

static void videl_present(void)
{
	short back = (short)(1 - g_front);
	videl_lut_blit(g_screen[back], 0, g_surface.height, 0, g_surface.width);
	videl_flip(g_screen[back]);
	g_front = back;
}

/* Present only the dirty rect. The surround is identical in both buffers, so
 * only the rect needs re-blitting. */
static void videl_present_rect(short x, short y, short w, short h)
{
	short back = (short)(1 - g_front);
	short x1 = (short)(x + w);
	short y1 = (short)(y + h);

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x1 > g_surface.width)  x1 = g_surface.width;
	if (y1 > g_surface.height) y1 = g_surface.height;
	if (x1 <= x || y1 <= y)
		return;
	videl_lut_blit(g_screen[back], y, y1, x, x1);
	videl_flip(g_screen[back]);
	g_front = back;
}

/* Rebuild the 8->RGB565 LUT from the engine's palette. The 8-bit RGB
 * components are truncated to 5/6/5 bits. */
static void videl_set_palette(const dsp_color_t *colors, short first,
                              short count)
{
	short i;

	if (first < 0) first = 0;
	if (first + count > 256) count = (short)(256 - first);
	for (i = 0; i < count; i++) {
		unsigned short r = colors[i].r;
		unsigned short g = colors[i].g;
		unsigned short b = colors[i].b;
		g_lut[first + i] = (unsigned short)(((r & 0xf8) << 8)
		                                  | ((g & 0xfc) << 3)
		                                  | (b >> 3));
	}
}

static const dsp_backend_t videl_backend = {
	"VIDEL (Falcon 16bpp)",
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
	 * (TT-shifter) and pick the matching backend. Only VIDEL today. */
	return &videl_backend;
}
