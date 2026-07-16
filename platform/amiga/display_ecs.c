/*
 * Amiga ECS/OCS display backend — native bitplanes, 32 colours.
 *
 * The bare-chipset answer for a machine with no AGA and no graphics card: a
 * 320x200 lores screen in FIVE bitplanes (32 colours). The engine renders one
 * 256-colour chunky buffer, so this backend runs the shared median-cut
 * QUANTIZER (platform/include/quantize.h) at set_palette time — reducing the
 * live 256-entry CLUT to 32 registers and building a 256->32 remap LUT — then
 * remaps each pixel through the LUT before the 5-plane chunky->planar
 * (c2p_amiga_n). The palette lives in the copper (32 COLORxx writes, 4 bits/
 * gun); the plane pointers flip per present, exactly like the AGA backend.
 *
 * Coexistence: this is a THIRD backend in the same binary (AGA / RTG / ECS),
 * picked by dsp_detect at runtime. Like the RTG backend it defines NO cursor
 * functions — plat_cursor_active() (display_aga.c) reports inactive because the
 * AGA sprites never init, so the shim composites a SOFTWARE cursor into the
 * chunky surface, which flows through present/present_rect for free.
 *
 * v1 scope / known gaps (see docs/ecs-st-quantizer-plan.md):
 *   - GLOBAL palette (no per-line copper banding yet — the banding win is the
 *     next push and is free on the copper).
 *   - Palette CYCLING (the fireplace) is re-quantised + re-presented only on a
 *     substantial palette load; small cycle-range writes update the shadow CLUT
 *     but don't animate, to avoid churn and stale-plane colour corruption.
 *   - A separate 64000-byte remap pass precedes c2p (folding the LUT into the
 *     c2p load is a later optimisation).
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

#include "quantize.h"            /* quant_reduce — the median-cut reducer */

#define CUSTOM ((volatile struct Custom *)0xDFF000)

#define ECS_W       320
#define ECS_H       200
#define ECS_DEPTH   5                   /* 32 colours */
#define ECS_NCOL    (1 << ECS_DEPTH)    /* 32 */
#define ECS_PITCH   (ECS_W / 8)         /* 40 bytes per bitplane row */
#define ECS_BITS    4                   /* 4 bits/gun (ECS/STE-class palette) */

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

/* Quantizer state: the shadow of the engine's 256-entry CLUT, and the LUT
 * that maps each original index to one of the 32 survivors. */
static unsigned char  s_clut[256 * 3];
static unsigned char  s_remap[256];
static short          s_have_pal;

#define FRAME_BYTES ((ULONG)ECS_PITCH * ECS_H * ECS_DEPTH)

/* --- the copper list ------------------------------------------------------
 * prologue (BPLCONx / modulos / DIW / DDF) + 5 plane pointers (patched per
 * flip) + 32 COLORxx writes (patched by set_palette) + WAIT. No AGA palette
 * banks, no LOCT, no sprites. */

#define COP_PROLOGUE_MOVES  9
#define COP_PLANE_MOVES     (ECS_DEPTH * 2)
#define COP_PAL_MOVES       ECS_NCOL
#define COP_WORDS           ((COP_PROLOGUE_MOVES + COP_PLANE_MOVES \
                             + COP_PAL_MOVES) * 2 + 2)

static UWORD *s_cop;
static UWORD *s_cop_bpl;                 /* first BPLxPTH operand word        */
static UWORD *s_cop_pal[ECS_NCOL];       /* COLORxx operand word per colour   */

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

static void cop_build(void)
{
	UWORD *cl = s_cop;
	short p, c;

	/* BPLCON0: COLOR composite enable (bit 9) + BPU = 5 (bits 14-12). */
	cl = cop_move(cl, R_BPLCON0, (UWORD)(0x0200 | (ECS_DEPTH << 12)));
	cl = cop_move(cl, R_BPLCON1, 0x0000);
	cl = cop_move(cl, R_BPLCON2, 0x0000);   /* no sprites (software cursor) */
	cl = cop_move(cl, R_BPL1MOD, 0x0000);
	cl = cop_move(cl, R_BPL2MOD, 0x0000);
	/* Standard lores window: top-left 0x2C,0x81; 200 lines -> bottom 0xF4. */
	cl = cop_move(cl, R_DIWSTRT, 0x2C81);
	cl = cop_move(cl, R_DIWSTOP, 0xF4C1);
	cl = cop_move(cl, R_DDFSTRT, 0x0038);
	cl = cop_move(cl, R_DDFSTOP, 0x00D0);

	/* Plane pointers (patched by cop_point_planes on every flip). */
	s_cop_bpl = cl + 1;
	for (p = 0; p < ECS_DEPTH; p++) {
		cl = cop_move(cl, (UWORD)(R_BPL1PTH + p * 4),     0);
		cl = cop_move(cl, (UWORD)(R_BPL1PTH + p * 4 + 2), 0);
	}

	/* 32 colour registers (single 4-bit-per-gun write each). */
	for (c = 0; c < ECS_NCOL; c++) {
		s_cop_pal[c] = cl + 1;
		cl = cop_move(cl, (UWORD)(R_COLOR00 + c * 2), 0);
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
	s_have_pal = 0;

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
	dbg_log("ecs: 320x200x5 (32-colour quantized) up");
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

/* Remap a rect of the chunky surface through the 256->32 LUT into s_remap_buf. */
static void remap_rect(short x, short y, short w, short h)
{
	short r;

	for (r = 0; r < h; r++) {
		const unsigned char *src = s_chunky + (long)(y + r) * ECS_W + x;
		unsigned char *dst = s_remap_buf + (long)(y + r) * ECS_W + x;
		short c;

		for (c = 0; c < w; c++)
			dst[c] = s_remap[src[c]];
	}
}

/* Full render: remap the whole surface, convert to the back plane set, flip. */
static void ecs_render(void)
{
	unsigned char *back = s_planes[s_front ^ 1];
	unsigned char *planes[ECS_DEPTH];
	short p;

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

/* Reduce the shadow CLUT to 32, load the copper colour registers, rebuild the
 * remap LUT. Snapped 8-bit reps -> 4-bit-per-gun COLORxx words. */
static void ecs_requantize(void)
{
	unsigned char pal[ECS_NCOL * 3];
	short n, i;

	n = quant_reduce(s_clut, ECS_NCOL, ECS_BITS, pal, s_remap);
	for (i = 0; i < ECS_NCOL; i++) {
		unsigned char r = (i < n) ? pal[i * 3 + 0] : 0;
		unsigned char g = (i < n) ? pal[i * 3 + 1] : 0;
		unsigned char b = (i < n) ? pal[i * 3 + 2] : 0;

		*s_cop_pal[i] = (UWORD)(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
	}
	s_have_pal = 1;
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

	/* Re-quantise only on a substantial load (a scene/palette change), not on
	 * the small-range writes palette CYCLING makes — those would churn the
	 * median cut and, worse, change the remap out from under the already-
	 * converted planes (stale-colour corruption). After a re-quantise the
	 * remap moved, so re-render the current surface to match. */
	if (count >= 32 || !s_have_pal) {
		ecs_requantize();
		ecs_render();
	}
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
