/*
 * Amiga ECS/OCS display backend — native bitplanes, 32 colours, PER-BAND
 * copper palette.
 *
 * The bare-chipset answer for a machine with no AGA and no graphics card: a
 * 320x200 lores screen in FIVE bitplanes. The engine renders one 256-colour
 * chunky buffer, so this backend runs the shared BANDED median-cut quantizer
 * (platform/include/quantize.h): the frame is split into ECS_NBANDS horizontal
 * strips and EACH is reduced to its own 32 colours from the colours that
 * actually appear in it — so the granite chrome stops starving the viewport.
 * The copper reloads all 32 registers at every band boundary for free (a WAIT +
 * 32 COLOR moves per band), which is exactly what makes per-region palettes
 * cost nothing on the Amiga.
 *
 * Each present remaps every pixel through its band's 256->32 LUT, converts to
 * 5 planes (c2p_amiga_n), and flips. Re-banding (the histogram + per-band
 * reduce) runs only when the palette is marked dirty — a set_palette — so it is
 * per scene change, not per frame.
 *
 * Coexistence: a THIRD backend in one binary (AGA / RTG / ECS), picked by
 * dsp_detect. Like RTG it defines NO cursor functions — plat_cursor_active()
 * (display_aga.c) reports inactive, so the shim composites a software cursor.
 */

#include "display.h"
#include "dbglog.h"

#ifdef FRUA_AMIGA

#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "quantize.h"            /* quant_banded — the banded median-cut reducer */

#define CUSTOM ((volatile struct Custom *)0xDFF000)

#define ECS_W       320
#define ECS_H       200
#define ECS_DEPTH   5                   /* 32 colours */
#define ECS_NCOL    (1 << ECS_DEPTH)    /* 32 */
#define ECS_PITCH   (ECS_W / 8)         /* 40 bytes per bitplane row */
#define ECS_BITS    4                   /* 4 bits/gun */
#define ECS_NBANDS  25                  /* 8 scanlines per band (200/25) */
#define ECS_RPB     (ECS_H / ECS_NBANDS)

extern void c2p_amiga_n(const unsigned char *chunky, unsigned char *const planes[],
                        short w, short h, short plane_pitch, short nplanes);
extern void c2p_amiga_n_rect(const unsigned char *chunky, short chunky_pitch,
                             unsigned char *const planes[], short plane_pitch,
                             short x0, short y0, short w, short h, short nplanes);

extern struct GfxBase *GfxBase;         /* opened/owned by display_aga.c */

/* --- backend state ------------------------------------------------------- */

static unsigned char *s_chunky;         /* the engine's 8bpp surface        */
static unsigned char *s_remap_buf;      /* chunky remapped to 0..31         */
static unsigned char *s_planes[2];      /* double-buffered 5-plane sets     */
static int            s_front;
static dsp_surface_t  s_surface;
static struct View   *s_oldview;

/* Quantizer state: shadow CLUT + per-band palettes and remap LUTs. */
static unsigned char  s_clut[256 * 3];
static unsigned char  s_band_pal[ECS_NBANDS * ECS_NCOL * 3];
static unsigned char  s_band_remap[ECS_NBANDS * 256];
static short          s_dirty;

#define FRAME_BYTES ((ULONG)ECS_PITCH * ECS_H * ECS_DEPTH)

/* --- the copper list ------------------------------------------------------
 * prologue (BPLCONx / modulos / DIW / DDF) + 5 plane pointers (patched per
 * flip) + band 0's 32 COLOR writes at frame top + (ECS_NBANDS-1) blocks of
 * { WAIT band-line ; 32 COLOR writes } + WAIT end. The palette words are
 * patched by ecs_reband. */

#define COP_WORDS ((9 + ECS_DEPTH * 2 + ECS_NCOL) * 2 \
                  + (ECS_NBANDS - 1) * (2 + ECS_NCOL * 2) + 2)

static UWORD *s_cop;
static UWORD *s_cop_bpl;                         /* first BPLxPTH operand    */
static UWORD *s_cop_pal[ECS_NBANDS][ECS_NCOL];   /* per-band COLOR operands  */

#define R_BPLCON0  0x100
#define R_BPLCON1  0x102
#define R_BPLCON2  0x104
#define R_BPL1MOD  0x108
#define R_BPL2MOD  0x10A
#define R_DIWSTRT  0x08E
#define R_DIWSTOP  0x090
#define R_DDFSTRT  0x092
#define R_DDFSTOP  0x094
#define R_BPL1PTH  0x0E0                 /* +4 per plane */
#define R_COLOR00  0x180

static UWORD *cop_move(UWORD *cl, UWORD reg, UWORD val)
{
	*cl++ = reg;
	*cl++ = val;
	return cl;
}

static UWORD *cop_wait(UWORD *cl, short vpos, short hpos)
{
	*cl++ = (UWORD)(((vpos & 0xFF) << 8) | ((hpos & 0x7F) << 1) | 1);
	*cl++ = 0xFFFE;                  /* compare all bits, blitter-finish off */
	return cl;
}

static void cop_build(void)
{
	UWORD *cl = s_cop;
	short p, b, c;

	/* BPLCON0: COLOR composite enable (bit 9) + BPU = 5 (bits 14-12). */
	cl = cop_move(cl, R_BPLCON0, (UWORD)(0x0200 | (ECS_DEPTH << 12)));
	cl = cop_move(cl, R_BPLCON1, 0x0000);
	cl = cop_move(cl, R_BPLCON2, 0x0000);   /* no sprites (software cursor) */
	cl = cop_move(cl, R_BPL1MOD, 0x0000);
	cl = cop_move(cl, R_BPL2MOD, 0x0000);
	cl = cop_move(cl, R_DIWSTRT, 0x2C81);
	cl = cop_move(cl, R_DIWSTOP, 0xF4C1);
	cl = cop_move(cl, R_DDFSTRT, 0x0038);
	cl = cop_move(cl, R_DDFSTOP, 0x00D0);

	s_cop_bpl = cl + 1;
	for (p = 0; p < ECS_DEPTH; p++) {
		cl = cop_move(cl, (UWORD)(R_BPL1PTH + p * 4),     0);
		cl = cop_move(cl, (UWORD)(R_BPL1PTH + p * 4 + 2), 0);
	}

	/* Band 0 palette loads at frame top (before line 0x2C). */
	for (c = 0; c < ECS_NCOL; c++) {
		s_cop_pal[0][c] = cl + 1;
		cl = cop_move(cl, (UWORD)(R_COLOR00 + c * 2), 0);
	}
	/* Bands 1..N-1: reload at the band's first scanline. */
	for (b = 1; b < ECS_NBANDS; b++) {
		short line = (short)(0x2C + b * ECS_RPB);

		cl = cop_wait(cl, line, 0);
		for (c = 0; c < ECS_NCOL; c++) {
			s_cop_pal[b][c] = cl + 1;
			cl = cop_move(cl, (UWORD)(R_COLOR00 + c * 2), 0);
		}
	}

	*cl++ = 0xFFFF;
	*cl++ = 0xFFFE;
}

static void cop_point_planes(unsigned char *set)
{
	short p;

	for (p = 0; p < ECS_DEPTH; p++) {
		ULONG addr = (ULONG)(set + (ULONG)p * ECS_PITCH * ECS_H);
		s_cop_bpl[p * 4]     = (UWORD)(addr >> 16);
		s_cop_bpl[p * 4 + 2] = (UWORD)(addr & 0xFFFF);
	}
}

/* --- backend entry points ------------------------------------------------ */

static void ecs_shutdown_partial(void);

static int ecs_init(short want_w, short want_h)
{
	(void)want_w; (void)want_h;

	if (GfxBase == NULL)
		GfxBase = (struct GfxBase *)
		    OpenLibrary((CONST_STRPTR)"graphics.library", 39);
	if (GfxBase == NULL) {
		dbg_log("ecs: graphics.library open failed");
		return 1;
	}

	s_chunky    = AllocMem((ULONG)ECS_W * ECS_H, MEMF_ANY | MEMF_CLEAR);
	s_remap_buf = AllocMem((ULONG)ECS_W * ECS_H, MEMF_ANY | MEMF_CLEAR);
	s_planes[0] = AllocMem(FRAME_BYTES, MEMF_CHIP | MEMF_CLEAR);
	s_planes[1] = AllocMem(FRAME_BYTES, MEMF_CHIP | MEMF_CLEAR);
	s_cop       = AllocMem(COP_WORDS * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);
	if (s_chunky == NULL || s_remap_buf == NULL || s_planes[0] == NULL
	    || s_planes[1] == NULL || s_cop == NULL) {
		dbg_log("ecs: AllocMem failed (chip for planes/copper?)");
		ecs_shutdown_partial();
		return 1;
	}
	s_front = 0;
	s_dirty = 1;                    /* first present builds the bands */

	s_surface.width  = ECS_W;
	s_surface.height = ECS_H;
	s_surface.pitch  = ECS_W;
	s_surface.pixels = s_chunky;

	cop_build();
	cop_point_planes(s_planes[0]);

	s_oldview = GfxBase->ActiView;
	LoadView(NULL);
	WaitTOF();
	WaitTOF();
	CUSTOM->cop1lc  = (ULONG)s_cop;
	CUSTOM->copjmp1 = 0;
	CUSTOM->dmacon  = (UWORD)(DMAF_SETCLR | DMAF_MASTER
	                          | DMAF_RASTER | DMAF_COPPER);
	dbg_log("ecs: 320x200x5 32-colour, per-band copper palette up");
	return 0;
}

static void ecs_shutdown_partial(void)
{
	if (s_cop)       { FreeMem(s_cop, COP_WORDS * sizeof(UWORD)); s_cop = NULL; }
	if (s_planes[0]) { FreeMem(s_planes[0], FRAME_BYTES); s_planes[0] = NULL; }
	if (s_planes[1]) { FreeMem(s_planes[1], FRAME_BYTES); s_planes[1] = NULL; }
	if (s_remap_buf) { FreeMem(s_remap_buf, (ULONG)ECS_W * ECS_H); s_remap_buf = NULL; }
	if (s_chunky)    { FreeMem(s_chunky, (ULONG)ECS_W * ECS_H); s_chunky = NULL; }
	/* GfxBase belongs to display_aga.c; leave it open. */
}

static void ecs_shutdown(void)
{
	if (GfxBase != NULL && s_oldview != NULL) {
		LoadView(s_oldview);
		WaitTOF();
		WaitTOF();
		CUSTOM->cop1lc  = (ULONG)GfxBase->copinit;
		CUSTOM->copjmp1 = 0;
	}
	ecs_shutdown_partial();
}

static dsp_surface_t *ecs_surface(void)
{
	return &s_surface;
}

/* Remap a rect of the chunky surface into s_remap_buf, each row through ITS
 * band's 256->32 LUT. */
static void remap_rect(short x, short y, short w, short h)
{
	short r;

	for (r = 0; r < h; r++) {
		short yy = (short)(y + r);
		short band = (short)((long)yy * ECS_NBANDS / ECS_H);
		const unsigned char *lut = s_band_remap + (long)band * 256;
		const unsigned char *src = s_chunky + (long)yy * ECS_W + x;
		unsigned char *dst = s_remap_buf + (long)yy * ECS_W + x;
		short c;

		for (c = 0; c < w; c++)
			dst[c] = lut[src[c]];
	}
}

/* Re-band: histogram + per-band reduce over the current surface, then patch the
 * copper's per-band COLOR words. */
static void ecs_reband(void)
{
	short b, i;

	quant_banded(s_chunky, ECS_W, ECS_H, s_clut,
	             ECS_NBANDS, ECS_NCOL, ECS_BITS, s_band_pal, s_band_remap);
	for (b = 0; b < ECS_NBANDS; b++) {
		const unsigned char *bp = s_band_pal + (long)b * ECS_NCOL * 3;

		for (i = 0; i < ECS_NCOL; i++)
			*s_cop_pal[b][i] = (UWORD)(((bp[i * 3 + 0] >> 4) << 8)
			                          | ((bp[i * 3 + 1] >> 4) << 4)
			                          | (bp[i * 3 + 2] >> 4));
	}
	s_dirty = 0;
}

/* Full render: (re-band if dirty), remap the whole surface, convert to the
 * back plane set, flip. */
static void ecs_render(void)
{
	unsigned char *back = s_planes[s_front ^ 1];
	unsigned char *planes[ECS_DEPTH];
	short p;

	if (s_dirty)
		ecs_reband();
	remap_rect(0, 0, ECS_W, ECS_H);
	for (p = 0; p < ECS_DEPTH; p++)
		planes[p] = back + (ULONG)p * ECS_PITCH * ECS_H;
	c2p_amiga_n(s_remap_buf, planes, ECS_W, ECS_H, ECS_PITCH, ECS_DEPTH);
	cop_point_planes(back);
	s_front ^= 1;
}

static void ecs_present(void)
{
	ecs_render();
}

static void ecs_present_rect(short x, short y, short w, short h)
{
	unsigned char *front = s_planes[s_front];
	unsigned char *planes[ECS_DEPTH];
	short p, x1;

	/* A pending re-band changes every band's remap, so the whole frame must
	 * be rebuilt — promote to a full render. */
	if (s_dirty) {
		ecs_render();
		return;
	}

	if (x < 0) { w = (short)(w + x); x = 0; }
	if (y < 0) { h = (short)(h + y); y = 0; }
	if (x + w > ECS_W) w = (short)(ECS_W - x);
	if (y + h > ECS_H) h = (short)(ECS_H - y);
	if (w <= 0 || h <= 0)
		return;

	x1 = (short)((x + w + 7) & ~7);
	x  = (short)(x & ~7);
	w  = (short)(x1 - x);

	remap_rect(x, y, w, h);
	for (p = 0; p < ECS_DEPTH; p++)
		planes[p] = front + (ULONG)p * ECS_PITCH * ECS_H;
	c2p_amiga_n_rect(s_remap_buf, ECS_W, planes, ECS_PITCH,
	                 x, y, w, h, ECS_DEPTH);
}

static void ecs_set_palette(const dsp_color_t *colors, short first, short count)
{
	short i;

	if (s_cop == NULL)
		return;
	for (i = 0; i < count; i++) {
		short idx = (short)(first + i);

		if (idx < 0 || idx > 255)
			continue;
		s_clut[idx * 3 + 0] = colors[i].r;
		s_clut[idx * 3 + 1] = colors[i].g;
		s_clut[idx * 3 + 2] = colors[i].b;
	}
	/* Defer the re-band to the next present, when the surface is drawn: the
	 * band palettes depend on pixel content, not just the CLUT. */
	s_dirty = 1;
}

static const dsp_backend_t ecs_backend = {
	"amiga-ecs",
	ecs_init,
	ecs_shutdown,
	ecs_surface,
	ecs_present,
	ecs_present_rect,
	ecs_set_palette,
};

const dsp_backend_t *dsp_backend_ecs(void)
{
	return &ecs_backend;
}

#endif /* FRUA_AMIGA */
