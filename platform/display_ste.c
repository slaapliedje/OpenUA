/*
 * Atari ST / STE display backend — ST Low (320x200, 16 colours).
 *
 * The bare-ST answer, mirroring the native ECS Amiga backend: the engine's
 * 256-colour chunky buffer is reduced to 16 by the shared median-cut QUANTIZER
 * (platform/include/quantize.h), each pixel remapped through the 256->16 LUT,
 * then converted to ST Low's FOUR word-interleaved bitplanes (the same Atari
 * planar layout the TT backend uses, at 4 planes instead of 8). No line
 * doubling — ST Low is 320x200 natively, so the engine frame maps 1:1.
 *
 * Palette: ST Low is 16 registers. Plain ST is 3 bits/gun, the STE 4 (the
 * quality cliff — see docs/ecs-st-quantizer-plan.md; STE is the real target).
 * We always write the STE 4-bit encoding via Setpalette (XBIOS 6): the STE
 * stores each gun's LSB in bit 3 and the top 3 bits in bits 2-0, so a plain ST
 * (which reads only bits 2-0) sees the 3-bit approximation for free.
 *
 * Mode/screen handling stays on the XBIOS (Setscreen/Getrez/Setpalette,
 * Physbase/Logbase) the ST TOS and EmuTOS provide. As with the TT backend the
 * LOGICAL base stays on the old TOS screen so the VT52 console can't scribble
 * over the display, and plat_cursor_* live with the VIDEL backend — inactive
 * here, so the shim composites the pointer in software through present_rect.
 *
 * v1 gaps (docs/ecs-st-quantizer-plan.md): GLOBAL palette only (the layout-
 * aligned HBL colour bands that make 16 colours read well are the next push);
 * palette cycling re-quantises only on a substantial load.
 */

#include <mint/osbind.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "display.h"
#include "dbglog.h"
#include "c2p32.h"
#include "quantize.h"

#define ST_W        320
#define ST_H        200
#define ST_DEPTH    4                   /* 16 colours */
#define ST_NCOL     (1 << ST_DEPTH)     /* 16 */
#define ST_BITS     4                   /* STE: 4 bits/gun */
#define LINE_BYTES  (ST_W * ST_DEPTH / 8)   /* 160 bytes/line, interleaved */
#define SCREEN_BYTES ((long)LINE_BYTES * ST_H)

static unsigned char *s_screen_raw;     /* Mxalloc'd block            */
static unsigned char *s_screen;         /* 256-aligned screen base    */
static unsigned char *s_chunky;         /* the engine's 8bpp surface  */
static dsp_surface_t  s_surface;
static short          s_save_rez = -1;
static void          *s_save_phys, *s_save_log;

/* Quantizer state: shadow of the 256-entry CLUT + the 256->16 remap LUT. */
static unsigned char  s_clut[256 * 3];
static unsigned char  s_remap[256];
static short          s_have_pal;

/* Convert one 16-pixel-aligned span of a chunky row, remapping each pixel
 * through the 256->16 LUT inline (no full-screen scratch), into ST Low's
 * 4 word-interleaved planes at `dst` (4 words = 8 bytes per 16-pixel group).
 * `w` is a multiple of 32 except a possible 16-pixel tail. */
static void st_c2p_span(const unsigned char *src, unsigned char *dst, short w)
{
	short x;

	for (x = 0; x + 32 <= w; x += 32) {
		c2p_u32 c[8], o[8];
		unsigned char rp[32];
		unsigned short *d = (unsigned short *)(dst + (long)(x / 16) * 8);
		short p, k;

		for (k = 0; k < 32; k++)
			rp[k] = s_remap[src[x + k]];
		c2p_load32(rp, c);
		c2p_transpose32(c, o);
		for (p = 0; p < ST_DEPTH; p++) {
			d[p]     = (unsigned short)(o[p] >> 16);   /* pixels 0-15  */
			d[p + 4] = (unsigned short)(o[p]);         /* pixels 16-31 */
		}
	}
	if (x < w) {                            /* 16-pixel tail */
		c2p_u32 c[8], o[8];
		unsigned char rp[32];
		unsigned short *d = (unsigned short *)(dst + (long)(x / 16) * 8);
		short p, k;

		for (k = 0; k < 16; k++)
			rp[k] = s_remap[src[x + k]];
		memset(rp + 16, 0, 16);
		c2p_load32(rp, c);
		c2p_transpose32(c, o);
		for (p = 0; p < ST_DEPTH; p++)
			d[p] = (unsigned short)(o[p] >> 16);
	}
}

static void st_blit_rows(short x0, short w, short y0, short h)
{
	short y;

	for (y = 0; y < h; y++) {
		const unsigned char *src = s_chunky + (long)(y0 + y) * ST_W + x0;
		unsigned char *dst = s_screen
		    + (long)(y0 + y) * LINE_BYTES
		    + (long)(x0 / 16) * 8;

		st_c2p_span(src, dst, w);
	}
}

static void st_present(void)
{
	st_blit_rows(0, ST_W, 0, ST_H);
}

static void st_present_rect(short x, short y, short w, short h)
{
	short x1;

	if (x < 0) { w = (short)(w + x); x = 0; }
	if (y < 0) { h = (short)(h + y); y = 0; }
	if (x + w > ST_W) w = (short)(ST_W - x);
	if (y + h > ST_H) h = (short)(ST_H - y);
	if (w <= 0 || h <= 0)
		return;

	x1 = (short)((x + w + 15) & ~15);       /* 16-pixel plane groups */
	x  = (short)(x & ~15);
	st_blit_rows(x, (short)(x1 - x), y, h);
}

/* Reduce the shadow CLUT to 16, load the 16 hardware registers in STE format,
 * rebuild the remap LUT. STE gun encoding: register nibble = (v0 << 3) |
 * (v >> 1) for a 4-bit value v — the extra bit is the LSB in bit 3. */
static void st_requantize(void)
{
	unsigned char pal[ST_NCOL * 3];
	static short  word[ST_NCOL];
	short n, i;

	n = quant_reduce(s_clut, ST_NCOL, ST_BITS, pal, s_remap);
	for (i = 0; i < ST_NCOL; i++) {
		short vr = (i < n) ? (pal[i * 3 + 0] >> 4) : 0;
		short vg = (i < n) ? (pal[i * 3 + 1] >> 4) : 0;
		short vb = (i < n) ? (pal[i * 3 + 2] >> 4) : 0;
		short rn = (short)(((vr & 1) << 3) | (vr >> 1));
		short gn = (short)(((vg & 1) << 3) | (vg >> 1));
		short bn = (short)(((vb & 1) << 3) | (vb >> 1));

		word[i] = (short)((rn << 8) | (gn << 4) | bn);
	}
	Setpalette(word);
	s_have_pal = 1;
}

static int st_init(short want_w, short want_h)
{
	(void)want_w; (void)want_h;

	s_screen_raw = (unsigned char *)Mxalloc(SCREEN_BYTES + 256, 0);   /* ST-RAM */
	s_chunky     = (unsigned char *)Mxalloc((long)ST_W * ST_H, 0);
	if (s_screen_raw == NULL || s_chunky == NULL) {
		dbg_log("ste: Mxalloc FAILED");
		if (s_screen_raw) { Mfree(s_screen_raw); s_screen_raw = NULL; }
		if (s_chunky)     { Mfree(s_chunky); s_chunky = NULL; }
		return 1;
	}
	s_screen = (unsigned char *)
	    (((uintptr_t)s_screen_raw + 255) & ~(uintptr_t)255);
	memset(s_screen, 0, SCREEN_BYTES);
	memset(s_chunky, 0, (size_t)ST_W * ST_H);

	s_save_rez  = Getrez();
	s_save_phys = Physbase();
	s_save_log  = Logbase();

	/* Physical base = our screen, ST Low (rez 0); LOGICAL base stays on the
	 * old TOS screen so console prints don't scribble the display. */
	Setscreen(s_save_log, s_screen, 0);

	s_surface.width  = ST_W;
	s_surface.height = ST_H;
	s_surface.pitch  = ST_W;
	s_surface.pixels = s_chunky;
	s_have_pal = 0;
	dbg_log("ste: ST-low 320x200x4 (16-colour quantized) up");
	return 0;
}

static void st_shutdown(void)
{
	if (s_save_rez >= 0) {
		Setscreen(s_save_log, s_save_phys, (short)s_save_rez);
		s_save_rez = -1;
	}
	if (s_screen_raw) { Mfree(s_screen_raw); s_screen_raw = NULL; s_screen = NULL; }
	if (s_chunky)     { Mfree(s_chunky); s_chunky = NULL; }
}

static dsp_surface_t *st_surface(void)
{
	return &s_surface;
}

static void st_set_palette(const dsp_color_t *colors, short first, short count)
{
	short i;

	if (first < 0 || count <= 0 || first >= 256)
		return;
	if (first + count > 256)
		count = (short)(256 - first);
	for (i = 0; i < count; i++) {
		s_clut[(first + i) * 3 + 0] = colors[i].r;
		s_clut[(first + i) * 3 + 1] = colors[i].g;
		s_clut[(first + i) * 3 + 2] = colors[i].b;
	}

	/* Re-quantise only on a substantial load (scene/palette change), not on
	 * the small cycle-range writes — those would churn the median cut and
	 * change the remap out from under the already-converted planes. After a
	 * re-quantise the remap moved, so re-render the surface to match. */
	if (count >= 32 || !s_have_pal) {
		st_requantize();
		st_present();
	}
}

static const dsp_backend_t ste_backend = {
	"Atari ST/STE (ST low, 16-colour quantized)",
	st_init,
	st_shutdown,
	st_surface,
	st_present,
	st_present_rect,
	st_set_palette,
};

const dsp_backend_t *dsp_backend_ste(void)
{
	return &ste_backend;
}
