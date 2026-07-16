/*
 * Atari ST / STE display backend — ST Low (320x200, 16 colours), PER-BAND
 * palette via a Timer-B raster split.
 *
 * Mirrors the native ECS Amiga backend: the engine's 256-colour chunky buffer
 * is reduced by the shared BANDED median-cut quantizer (quantize.h) — the frame
 * is split into ST_NBANDS horizontal strips and each is reduced to its own 16
 * colours from the colours that actually appear in it, so the granite chrome
 * stops starving the viewport. Each pixel is remapped through its band's LUT,
 * then converted to ST Low's four word-interleaved bitplanes.
 *
 * The ST has one 16-entry palette, so per-band colours need a RASTER INTERRUPT
 * to reload it mid-frame. MFP Timer B counts display-enable pulses (= visible
 * scanlines): armed in event-count mode with count = ST_RPB, it fires at every
 * band boundary, and its handler writes that band's 16 registers straight to
 * the colour hardware. Because 200 is a multiple of ST_RPB the counter
 * self-phase-locks each frame; a VBL slot loads band 0 at the top and resets
 * the band index. Palettes are the STE 4-bit encoding (LSB in bit 3), so a
 * plain ST reads the 3-bit approximation for free.
 *
 * Re-banding runs only when the palette is dirty (a set_palette), deferred to
 * the next present since it depends on drawn pixels. plat_cursor_* live with
 * the VIDEL backend (inactive here) so the shim composites a software cursor.
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
#define ST_NBANDS   25                  /* 8 scanlines per band (200/25) */
#define ST_RPB      (ST_H / ST_NBANDS)
#define LINE_BYTES  (ST_W * ST_DEPTH / 8)   /* 160 bytes/line, interleaved */
#define SCREEN_BYTES ((long)LINE_BYTES * ST_H)

/* ST colour hardware: 16 word registers at 0xFF8240. */
#define ST_COLORREGS ((volatile short *)0xFFFF8240UL)

static unsigned char *s_screen_raw;
static unsigned char *s_screen;
static unsigned char *s_chunky;
static dsp_surface_t  s_surface;
static short          s_save_rez = -1;
static void          *s_save_phys, *s_save_log;
static short          s_ints_on;

/* Quantizer state. s_band_stpal / s_band_next cross the interrupt boundary. */
static unsigned char           s_clut[256 * 3];
static unsigned char           s_band_pal[ST_NBANDS * ST_NCOL * 3];
static unsigned char           s_band_remap[ST_NBANDS * 256];
static volatile short          s_band_stpal[ST_NBANDS][ST_NCOL];   /* ST-format */
static volatile short          s_band_next;
static short                   s_dirty;
static short                   s_vbl_slot = -1;

/* --- raster-split interrupt handlers (global: referenced from inline asm, so
 * they must not be dead-code-eliminated) --------------------------------- */

/* Write a band's 16 colour registers straight to the hardware. */
static void st_load_band(short b)
{
	volatile short *hw = ST_COLORREGS;
	short i;

	for (i = 0; i < ST_NCOL; i++)
		hw[i] = s_band_stpal[b][i];
}

/* VBL: band 0 at the top of the frame, reset the split index. */
void st_vbl_handler(void)
{
	st_load_band(0);
	s_band_next = 1;
}

/* Timer B fired at a band boundary: load the next band, advance. */
void st_timerb_handler(void)
{
	short b = s_band_next;

	if (b >= 1 && b < ST_NBANDS) {
		st_load_band(b);
		s_band_next = (short)(b + 1);
	}
}

/* VBL trampoline: called from the vblqueue as a subroutine (rts). */
__asm__(
	".globl _st_vbl_trampoline\n"
	"_st_vbl_trampoline:\n"
	"  moveml %d0-%d2/%a0-%a2,%sp@-\n"
	"  jbsr   _st_vbl_handler\n"
	"  moveml %sp@+,%d0-%d2/%a0-%a2\n"
	"  rts\n"
);
extern void st_vbl_trampoline(void);

/* Timer B trampoline: a real MFP interrupt (rte), clears the in-service bit
 * (Timer B = MFP channel 8 = ISRA bit 0 at 0xFFFA0F; write 0xFE to clear it
 * and leave the rest — the MFP clears only the zero bits). */
__asm__(
	".globl _st_timerb_trampoline\n"
	"_st_timerb_trampoline:\n"
	"  moveml %d0-%d2/%a0-%a2,%sp@-\n"
	"  jbsr   _st_timerb_handler\n"
	"  moveml %sp@+,%d0-%d2/%a0-%a2\n"
	"  moveb  #0xFE,0xFFFFFA0F\n"
	"  rte\n"
);
extern void st_timerb_trampoline(void);

static long st_vbl_install_super(void)
{
	long  *queue = *(long **)0x456UL;
	short  nvbls = *(short *)0x454UL;
	short  i;

	for (i = 0; i < nvbls; i++) {
		if (queue[i] == 0) {
			queue[i] = (long)(uintptr_t)st_vbl_trampoline;
			s_vbl_slot = i;
			return 0;
		}
	}
	return -1;
}

static long st_vbl_remove_super(void)
{
	long *queue = *(long **)0x456UL;

	if (s_vbl_slot >= 0) {
		queue[s_vbl_slot] = 0;
		s_vbl_slot = -1;
	}
	return 0;
}

/* --- chunky -> ST-low interleaved planes -------------------------------- */

/* Convert one 16-pixel-aligned span, remapping each pixel through `lut`
 * inline, into ST Low's 4 word-interleaved planes (8 bytes per 16-pixel
 * group). `w` is a multiple of 32 except a possible 16-pixel tail. */
static void st_c2p_span(const unsigned char *src, unsigned char *dst, short w,
                        const unsigned char *lut)
{
	short x;

	for (x = 0; x + 32 <= w; x += 32) {
		c2p_u32 c[8], o[8];
		unsigned char rp[32];
		unsigned short *d = (unsigned short *)(dst + (long)(x / 16) * 8);
		short p, k;

		for (k = 0; k < 32; k++)
			rp[k] = lut[src[x + k]];
		c2p_load32(rp, c);
		c2p_transpose32(c, o);
		for (p = 0; p < ST_DEPTH; p++) {
			d[p]     = (unsigned short)(o[p] >> 16);
			d[p + 4] = (unsigned short)(o[p]);
		}
	}
	if (x < w) {
		c2p_u32 c[8], o[8];
		unsigned char rp[32];
		unsigned short *d = (unsigned short *)(dst + (long)(x / 16) * 8);
		short p, k;

		for (k = 0; k < 16; k++)
			rp[k] = lut[src[x + k]];
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
		short yy = (short)(y0 + y);
		short band = (short)((long)yy * ST_NBANDS / ST_H);
		const unsigned char *lut = s_band_remap + (long)band * 256;
		const unsigned char *src = s_chunky + (long)yy * ST_W + x0;
		unsigned char *dst = s_screen + (long)yy * LINE_BYTES + (long)(x0 / 16) * 8;

		st_c2p_span(src, dst, w, lut);
	}
}

/* Re-band: histogram + per-band reduce, then build the per-band ST-format
 * palettes (STE gun encoding: nibble = (v0 << 3) | (v >> 1)). */
static void st_reband(void)
{
	short b, i;

	quant_banded(s_chunky, ST_W, ST_H, s_clut,
	             ST_NBANDS, ST_NCOL, ST_BITS, s_band_pal, s_band_remap);
	for (b = 0; b < ST_NBANDS; b++) {
		const unsigned char *bp = s_band_pal + (long)b * ST_NCOL * 3;

		for (i = 0; i < ST_NCOL; i++) {
			short vr = bp[i * 3 + 0] >> 4;
			short vg = bp[i * 3 + 1] >> 4;
			short vb = bp[i * 3 + 2] >> 4;
			short rn = (short)(((vr & 1) << 3) | (vr >> 1));
			short gn = (short)(((vg & 1) << 3) | (vg >> 1));
			short bn = (short)(((vb & 1) << 3) | (vb >> 1));

			s_band_stpal[b][i] = (short)((rn << 8) | (gn << 4) | bn);
		}
	}
	s_dirty = 0;
}

/* --- backend entry points ------------------------------------------------ */

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
	Setscreen(s_save_log, s_screen, 0);      /* ST Low; console keeps old log */

	s_surface.width  = ST_W;
	s_surface.height = ST_H;
	s_surface.pitch  = ST_W;
	s_surface.pixels = s_chunky;
	s_dirty   = 1;
	s_band_next = 1;

	/* Install the raster split: a VBL slot (band 0 + reset) and Timer B in
	 * event-count mode firing every ST_RPB display lines. */
	Supexec(st_vbl_install_super);
	Xbtimer(1, 8, ST_RPB, st_timerb_trampoline);   /* timer B, event count */
	s_ints_on = 1;

	dbg_log("ste: ST-low 320x200x4 16-colour, per-band Timer-B palette up");
	return 0;
}

static void st_shutdown(void)
{
	if (s_ints_on) {
		Jdisint(8);                      /* stop Timer B (MFP channel 8) */
		Supexec(st_vbl_remove_super);
		s_ints_on = 0;
	}
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

static void st_present(void)
{
	if (s_dirty)
		st_reband();
	st_blit_rows(0, ST_W, 0, ST_H);
}

static void st_present_rect(short x, short y, short w, short h)
{
	short x1;

	if (s_dirty) {                           /* re-band changed every LUT */
		st_reband();
		st_blit_rows(0, ST_W, 0, ST_H);
		return;
	}
	if (x < 0) { w = (short)(w + x); x = 0; }
	if (y < 0) { h = (short)(h + y); y = 0; }
	if (x + w > ST_W) w = (short)(ST_W - x);
	if (y + h > ST_H) h = (short)(ST_H - y);
	if (w <= 0 || h <= 0)
		return;

	x1 = (short)((x + w + 15) & ~15);        /* 16-pixel plane groups */
	x  = (short)(x & ~15);
	st_blit_rows(x, (short)(x1 - x), y, h);
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
	s_dirty = 1;                             /* re-band at next present */
}

static const dsp_backend_t ste_backend = {
	"Atari ST/STE (ST low, 16-colour banded)",
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
