/*
 * Atari ST High monochrome display backend — 640x400, 1 bit.
 *
 * ST High is the Mac's window size exactly (the Mac renders 640x400 by
 * doubling its 320x200-logical art), and 1-bit is where the planar tax
 * vanishes: 1-bit chunky IS planar. The engine's 320x200 8-bit frame maps to
 * the screen as 2x2 cells — and those four bits per logical pixel buy a
 * 5-level ORDERED DITHER (Bayer 2x2) for free: set_palette() rebuilds two
 * 256-entry LUTs mapping a palette index's luminance to its top/bottom-row
 * 2-bit patterns, and present() packs four logical pixels per screen byte
 * through them. No quantizer, no palette bands, no raster interrupts — this
 * is the fastest Atari presentation of the game, for SM124-class monitors.
 *
 * This is phase 1 (a luminance rendering of the COLOUR game). Phase 2 — the
 * Mac's real B&W mode, selecting the 1-bit art set the resources ship — is
 * engine-side work tracked in docs/ecs-st-quantizer-plan.md.
 *
 * Row-diff presents as in the ST-low backend (the engine's modal loops full-
 * present every pass; diffing keeps idle passes cheap). plat_cursor_* live
 * with the VIDEL backend (inactive here): software cursor, composited.
 */

#include <mint/osbind.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "display.h"
#include "dbglog.h"

#define EN_W        320                 /* engine surface (colour-dither mode) */
#define EN_H        200
#define HI_W        640                 /* ST high screen           */
#define HI_H        400
#define LINE_BYTES  (HI_W / 8)          /* 80 bytes per screen row  */
#define SCREEN_BYTES ((long)LINE_BYTES * HI_H)   /* 32000            */

/* Engine-B&W mode: the Mac's 1-bit game runs a 480x300 window with its own
 * layout (sized for 512x342 compact Macs; the art is 1:1). The surface is
 * byte-per-pixel (0 = white, nonzero = ink) and present packs it 1:1 into
 * the ST High screen, centred. Toggled by FRUA_BWMODE at build time while
 * the B&W arms are brought up (see docs/ecs-st-quantizer-plan.md phase 2). */
#define BW_W        480
#define BW_H        300
#define BW_OX       ((HI_W - BW_W) / 2)          /* 80  */
#define BW_OY       ((HI_H - BW_H) / 2)          /* 50  */

/* Nonzero when the ST High backend is active — the engine's colour-mode
 * derivation reads it (via the port bootstrap) to take the Mac's B&W arms. */
int g_dsp_mono_active;

static unsigned char *s_screen_raw;
static unsigned char *s_screen;
static unsigned char *s_chunky;
static unsigned char *s_shadow;         /* chunky at last convert   */
static dsp_surface_t  s_surface;
static short          s_save_rez = -1;
static void          *s_save_phys, *s_save_log;
static short          s_force_full;

/* Per-palette-index 2-bit dither patterns, top and bottom row of the 2x2
 * cell. Bit set = BLACK (the ST-high default palette: planar 1 = black on a
 * white field, matching the Mac's white desktop). Patterns are stored in the
 * two LOW bits; the packer shifts them into place. */
static unsigned char s_dith_top[256];
static unsigned char s_dith_bot[256];

#ifdef FRUA_BWMODE
/* Engine-B&W ink LUT: the surface stays COLOUR-INDEXED (so every colour-arm
 * draw — text bg fills, plates, art — works unchanged) and present maps each
 * index to ink/paper by PALETTE LUMINANCE. Rebuilt by set_palette; the
 * default marks only index 0 (black) as ink until a palette arrives.
 * EXPORTED: the engine's mono planar-page shim (the jt995 codec bracket in
 * boot.c) classifies surface pixels through the SAME table, so the codec's
 * 1-bit view and the present always agree on what is ink. */
unsigned char g_dsp_ink[256];
#endif

/* Bayer 2x2 threshold matrix [top-left, top-right; bottom-left, bottom-right]
 * = [0,2;3,1], expressed as per-level 2-bit patterns. Live-verified polarity
 * (Hatari --monitor mono, TOS 2.06): a SET bit renders WHITE, so level 0
 * (black) is 0x0 and level 4 (white) is 0x3. */
static const unsigned char k_lvl_top[5] = { 0x0, 0x0, 0x2, 0x2, 0x3 };
static const unsigned char k_lvl_bot[5] = { 0x0, 0x2, 0x2, 0x3, 0x3 };

#ifdef FRUA_BWMODE
#define SURF_W BW_W
#define SURF_H BW_H
#else
#define SURF_W EN_W
#define SURF_H EN_H
#endif

static int sthigh_init(short want_w, short want_h)
{
	(void)want_w; (void)want_h;

	s_screen_raw = (unsigned char *)Mxalloc(SCREEN_BYTES + 256, 0);
	s_chunky     = (unsigned char *)Mxalloc((long)SURF_W * SURF_H, 0);
	s_shadow     = (unsigned char *)Mxalloc((long)SURF_W * SURF_H, 0);
	if (s_screen_raw == NULL || s_chunky == NULL || s_shadow == NULL) {
		dbg_log("sthigh: Mxalloc FAILED");
		if (s_screen_raw) { Mfree(s_screen_raw); s_screen_raw = NULL; }
		if (s_chunky)     { Mfree(s_chunky); s_chunky = NULL; }
		if (s_shadow)     { Mfree(s_shadow); s_shadow = NULL; }
		return 1;
	}
	s_screen = (unsigned char *)
	    (((uintptr_t)s_screen_raw + 255) & ~(uintptr_t)255);
	memset(s_screen, 0, SCREEN_BYTES);      /* white field           */
	memset(s_chunky, 0, (size_t)SURF_W * SURF_H);
	memset(s_shadow, 0, (size_t)SURF_W * SURF_H);
	s_force_full = 1;

	s_save_rez  = Getrez();
	s_save_phys = Physbase();
	s_save_log  = Logbase();
	/* Keep the mono rez the machine booted in; move only the base (the
	 * LOGICAL base stays on the old screen so console prints don't
	 * scribble the display, as in the other Atari backends). */
	Setscreen(s_save_log, s_screen, -1);

	/* Default all-white until the first palette arrives. */
	memset(s_dith_top, 0, sizeof s_dith_top);
	memset(s_dith_bot, 0, sizeof s_dith_bot);
#ifdef FRUA_BWMODE
	memset(g_dsp_ink, 0, sizeof g_dsp_ink);
	g_dsp_ink[0] = 1;                   /* index 0 = black until a palette lands */
#endif

	s_surface.width  = SURF_W;
	s_surface.height = SURF_H;
	s_surface.pitch  = SURF_W;
	s_surface.pixels = s_chunky;
	g_dsp_mono_active = 1;
#ifdef FRUA_BWMODE
	dbg_log("sthigh: 640x400 mono, ENGINE B&W 480x300 1:1 up");
#else
	dbg_log("sthigh: 640x400 mono (2x2 Bayer, 5 levels) up");
#endif
	return 0;
}

static void sthigh_shutdown(void)
{
	if (s_save_rez >= 0) {
		Setscreen(s_save_log, s_save_phys, (short)s_save_rez);
		s_save_rez = -1;
	}
	if (s_screen_raw) { Mfree(s_screen_raw); s_screen_raw = NULL; s_screen = NULL; }
	if (s_chunky)     { Mfree(s_chunky); s_chunky = NULL; }
	if (s_shadow)     { Mfree(s_shadow); s_shadow = NULL; }
}

static dsp_surface_t *sthigh_surface(void)
{
	return &s_surface;
}

#ifdef FRUA_BWMODE

/* Engine-B&W: pack colour-indexed rows 1:1 into the centred 480x300 window
 * through the luminance ink LUT. A SET screen bit renders WHITE on ST High
 * (live-verified): paper -> bit set, ink -> bit clear. */
static void hi_blit_rows(short x0, short w, short y0, short h)
{
	short y, i;

	for (y = 0; y < h; y++) {
		short yy = (short)(y0 + y);
		const unsigned char *src = s_chunky + (long)yy * SURF_W + x0;
		unsigned char *d = s_screen
		    + (long)(BW_OY + yy) * LINE_BYTES + ((BW_OX + x0) >> 3);

		for (i = 0; i < w; i += 8) {
			unsigned char b = 0;
			short k;

			for (k = 0; k < 8; k++)
				if (!g_dsp_ink[src[i + k]])
					b |= (unsigned char)(0x80 >> k);   /* paper */
			*d++ = b;
		}
		memcpy(s_shadow + (long)yy * SURF_W + x0, src, (size_t)w);
	}
}

#else /* colour-dither mode */

/* Convert logical rows [y0, y0+h) x [x0, x0+w): each logical row packs into
 * TWO screen rows, four logical pixels per byte, through the dither LUTs.
 * x0 and w are 4-pixel aligned (one screen byte = 4 logical pixels). */
static void hi_blit_rows(short x0, short w, short y0, short h)
{
	short y, i;

	for (y = 0; y < h; y++) {
		short yy = (short)(y0 + y);
		const unsigned char *src = s_chunky + (long)yy * SURF_W + x0;
		unsigned char *d0 = s_screen + (long)(yy * 2) * LINE_BYTES + (x0 >> 2);
		unsigned char *d1 = d0 + LINE_BYTES;

		for (i = 0; i < w; i += 4) {
			unsigned char t, b;

			t = (unsigned char)((s_dith_top[src[i]]     << 6)
			                  | (s_dith_top[src[i + 1]] << 4)
			                  | (s_dith_top[src[i + 2]] << 2)
			                  |  s_dith_top[src[i + 3]]);
			b = (unsigned char)((s_dith_bot[src[i]]     << 6)
			                  | (s_dith_bot[src[i + 1]] << 4)
			                  | (s_dith_bot[src[i + 2]] << 2)
			                  |  s_dith_bot[src[i + 3]]);
			*d0++ = t;
			*d1++ = b;
		}
		memcpy(s_shadow + (long)yy * SURF_W + x0, src, (size_t)w);
	}
}

#endif /* FRUA_BWMODE */

static void sthigh_present(void)
{
	short y;

	if (s_force_full) {
		hi_blit_rows(0, SURF_W, 0, SURF_H);
		s_force_full = 0;
		return;
	}
	for (y = 0; y < SURF_H; y++) {
		if (memcmp(s_chunky + (long)y * SURF_W,
		           s_shadow + (long)y * SURF_W, SURF_W) != 0)
			hi_blit_rows(0, SURF_W, y, 1);
	}
}

static void sthigh_present_rect(short x, short y, short w, short h)
{
	short x1;

	if (x < 0) { w = (short)(w + x); x = 0; }
	if (y < 0) { h = (short)(h + y); y = 0; }
	if (x + w > SURF_W) w = (short)(SURF_W - x);
	if (y + h > SURF_H) h = (short)(SURF_H - y);
	if (w <= 0 || h <= 0)
		return;

#ifdef FRUA_BWMODE
	x1 = (short)((x + w + 7) & ~7);         /* 8-pixel screen bytes  */
	x  = (short)(x & ~7);
#else
	x1 = (short)((x + w + 3) & ~3);         /* 4-logical-pixel bytes */
	x  = (short)(x & ~3);
#endif
	hi_blit_rows(x, (short)(x1 - x), y, h);
}

static void sthigh_set_palette(const dsp_color_t *colors, short first, short count)
{
	short i;

	if (first < 0 || count <= 0 || first >= 256)
		return;
	if (first + count > 256)
		count = (short)(256 - first);
	for (i = 0; i < count; i++) {
		short idx = (short)(first + i);
		short lum = (short)((2 * colors[i].r
		                   + 5 * colors[i].g
		                   + colors[i].b) >> 3);
		/* 5 dither levels; thresholds place pure black/white exactly. */
		short lvl = (short)((lum + 25) / 51);

		if (lvl > 4)
			lvl = 4;
		s_dith_top[idx] = k_lvl_top[lvl];
		s_dith_bot[idx] = k_lvl_bot[lvl];
#ifdef FRUA_BWMODE
		g_dsp_ink[idx] = (unsigned char)(lum < 112);
#endif
	}
	/* The mapping changed under already-converted rows. */
	s_force_full = 1;
}

static const dsp_backend_t sthigh_backend = {
	"Atari ST high (640x400 mono, 2x2 dither)",
	sthigh_init,
	sthigh_shutdown,
	sthigh_surface,
	sthigh_present,
	sthigh_present_rect,
	sthigh_set_palette,
};

const dsp_backend_t *dsp_backend_sthigh(void)
{
	return &sthigh_backend;
}
