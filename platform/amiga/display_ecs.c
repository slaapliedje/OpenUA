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
#include <string.h>              /* memcmp/memcpy (was implicitly declared) */

#include "quantize.h"            /* quant_banded — the banded median-cut reducer */
#include "planar.h"              /* draw-time plane path (B4) — hook + puts */

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
static unsigned char *s_shadow;         /* chunky as of the last convert    */
static short          s_force_full;     /* LUTs changed: diffing is void    */
static unsigned char *s_planes[2];      /* double-buffered 5-plane sets     */
static int            s_front;
static dsp_surface_t  s_surface;
static struct View   *s_oldview;

/* Quantizer state: shadow CLUT + per-band palettes and remap LUTs. */
static unsigned char  s_clut[256 * 3];
static unsigned char  s_band_pal[ECS_NBANDS * ECS_NCOL * 3];
static unsigned char  s_band_remap[ECS_NBANDS * 256];
static short          s_dirty;
static short          s_have_pal;

#ifdef FRUA_PLANAR
/* Draw-time plane path buffers (bodies further down, past ecs_repalette). */
static unsigned char *e_dt;
static unsigned char *e_dt_cov;
static unsigned char *e_dt_idx;
static short         *e_dt_rowcov;
static int ecs_dt_target(struct dsp_planar_dt *dt);
#endif

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
	s_shadow    = AllocMem((ULONG)ECS_W * ECS_H, MEMF_ANY | MEMF_CLEAR);
	s_planes[0] = AllocMem(FRAME_BYTES, MEMF_CHIP | MEMF_CLEAR);
	s_planes[1] = AllocMem(FRAME_BYTES, MEMF_CHIP | MEMF_CLEAR);
	s_cop       = AllocMem(COP_WORDS * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);
	if (s_chunky == NULL || s_remap_buf == NULL || s_shadow == NULL
	    || s_planes[0] == NULL || s_planes[1] == NULL || s_cop == NULL) {
		dbg_log("ecs: AllocMem failed (chip for planes/copper?)");
		ecs_shutdown_partial();
		return 1;
	}
	s_front = 0;
	s_dirty = 1;                    /* first present builds the bands */
	s_force_full = 1;
#ifdef FRUA_PLANAR
	/* Draw-time buffers (CPU-only: fast RAM is fine) + the shim hook. */
	e_dt        = AllocMem(FRAME_BYTES, MEMF_ANY | MEMF_CLEAR);
	e_dt_cov    = AllocMem((ULONG)ECS_W * ECS_H, MEMF_ANY | MEMF_CLEAR);
	e_dt_idx    = AllocMem((ULONG)ECS_W * ECS_H, MEMF_ANY | MEMF_CLEAR);
	e_dt_rowcov = AllocMem(ECS_H * sizeof(short), MEMF_ANY | MEMF_CLEAR);
	if (e_dt && e_dt_cov && e_dt_idx && e_dt_rowcov)
		planar_draw_target_register(ecs_dt_target);
#endif

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
#ifdef FRUA_PLANAR
	planar_draw_target_register((int (*)(struct dsp_planar_dt *))0);
	if (e_dt)        { FreeMem(e_dt, FRAME_BYTES); e_dt = NULL; }
	if (e_dt_cov)    { FreeMem(e_dt_cov, (ULONG)ECS_W * ECS_H); e_dt_cov = NULL; }
	if (e_dt_idx)    { FreeMem(e_dt_idx, (ULONG)ECS_W * ECS_H); e_dt_idx = NULL; }
	if (e_dt_rowcov) { FreeMem(e_dt_rowcov, ECS_H * sizeof(short)); e_dt_rowcov = NULL; }
#endif
	if (s_cop)       { FreeMem(s_cop, COP_WORDS * sizeof(UWORD)); s_cop = NULL; }
	if (s_planes[0]) { FreeMem(s_planes[0], FRAME_BYTES); s_planes[0] = NULL; }
	if (s_planes[1]) { FreeMem(s_planes[1], FRAME_BYTES); s_planes[1] = NULL; }
	if (s_remap_buf) { FreeMem(s_remap_buf, (ULONG)ECS_W * ECS_H); s_remap_buf = NULL; }
	if (s_shadow)    { FreeMem(s_shadow, (ULONG)ECS_W * ECS_H); s_shadow = NULL; }
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

/* NEW-INK detector (ported from the ST backend, where it cured the invisible
 * roster text). quant_banded maps colours ABSENT from its source frame through
 * a nearest-LUMA fallback — so a chromatic ink drawn AFTER the re-band (HUD
 * text whose luma ~= its panel's) lands on the panel's slot and renders
 * invisible; whether a machine shows it is pure present-cadence luck. Capture
 * which CLUT indices the last re-band actually saw; count converted pixels
 * carrying unseen indices (piggybacked on remap_rect's existing per-pixel
 * pass, near-free); the present tail schedules a re-quant when enough arrive.
 * Cannot loop: the re-quant's own capture covers the ink. */
static unsigned char e_used_idx[256];
static long          e_new_ink;

/* --- B1/Phase-0 palette machinery (ST-backend parity, ADR-0016) ----------
 *
 * Before this, EVERY substantial set_palette re-quantized: histogram + 25
 * band reduces on a 7 MHz 68000 — the scene-change hitch — even when the
 * engine was defensively re-installing an identical CLUT (a full recompose
 * re-seats the same palette). Ported from the ST backend:
 *   - CLUT-guard: a load whose CLUT matches the snapshot the bands were
 *     built from would reproduce them — skip the quant outright.
 *   - repalette: a changed CLUT with UNCHANGED content (a within-scene fade/
 *     settle) keeps the index->slot remaps valid; only slot->RGB moved.
 *     Rewrite the copper COLOR words via per-band slot representatives — no
 *     re-quant, no force-full.
 *   - split-guard: a content-same load that SPLITS two used indices sharing
 *     a slot (their RGBs matched at quant time; the new CLUT moves them
 *     apart — the invisible-HUD-text family) invalidates the remap: take
 *     the full re-quant instead (a repalette can never un-merge a slot). */
static unsigned char e_clut_quant[256 * 3];        /* CLUT the bands were built from */
static short         e_quant_valid;
static unsigned char e_used_band[ECS_NBANDS][256]; /* per-band used capture   */
static unsigned char e_slot_rep[ECS_NBANDS][ECS_NCOL]; /* rep CLUT idx / slot;
                                                        * 0xFF = empty slot   */

static long e_coldist(const unsigned char *a, const unsigned char *b)
{
	long dr = (long)a[0] - b[0];
	long dg = (long)a[1] - b[1];
	long db = (long)a[2] - b[2];

	return dr * dr + dg * dg + db * db;
}

static int ecs_remap_split(void)
{
	short b, i;

	for (b = 0; b < ECS_NBANDS; b++) {
		short anchor[ECS_NCOL];
		const unsigned char *brem = s_band_remap + (long)b * 256;

		for (i = 0; i < ECS_NCOL; i++)
			anchor[i] = -1;
		for (i = 0; i < 256; i++) {
			short s;

			if (!e_used_band[b][i])
				continue;
			s = brem[i];
			if (anchor[s] < 0) {
				anchor[s] = i;
				continue;
			}
			if (e_coldist(s_clut + (long)i * 3,
			              s_clut + (long)anchor[s] * 3) > 512)
				return 1;
		}
	}
	return 0;
}

/* Content-same palette change: reload the copper COLOR words from the NEW
 * CLUT via each band-slot's representative index. Same 4-bit encoding as
 * ecs_reband. Empty slots (rep 0xFF) keep their words. */
static void ecs_repalette(void)
{
	short b, i;

	for (b = 0; b < ECS_NBANDS; b++)
		for (i = 0; i < ECS_NCOL; i++) {
			unsigned char rep = e_slot_rep[b][i];

			if (rep == 0xFF)
				continue;
			*s_cop_pal[b][i] = (UWORD)(((s_clut[rep * 3 + 0] >> 4) << 8)
			                          | ((s_clut[rep * 3 + 1] >> 4) << 4)
			                          | (s_clut[rep * 3 + 2] >> 4));
		}
	memcpy(e_clut_quant, s_clut, sizeof e_clut_quant);
}

#ifdef FRUA_PLANAR
/* --- draw-time plane path (ADR-0016 B4, ST-backend parity) ---------------
 *
 * The converted Toolbox writers stamp e_dt (SEPARATE planes, the Amiga
 * layout — the shim's DC_PUT resolves to planar_put_amiga here) in parallel
 * with their chunky writes. The row-diff present then SKIPS the remap+c2p for
 * a row whose every pixel was stamped this epoch and matches chunky, copying
 * the finished planes instead. The force path stays ecs_render untouched:
 * the re-band's epoch reset clears coverage, so stale e_dt rows can never be
 * trusted and re-bridge lazily on their next change. ecs_repalette does NOT
 * reset the epoch — the remaps are unchanged there, so stamps stay valid. */
static void ecs_dt_epoch_reset(void)
{
	if (e_dt_cov)
		memset(e_dt_cov, 0, (long)ECS_W * ECS_H);
	if (e_dt_rowcov)
		memset(e_dt_rowcov, 0, ECS_H * sizeof(short));
}

static int ecs_dt_target(struct dsp_planar_dt *dt)
{
	if (!s_have_pal || e_dt == NULL)
		return 0;
	dt->planes       = e_dt;
	dt->remap        = s_band_remap;
	dt->cov          = e_dt_cov;
	dt->idx          = e_dt_idx;
	dt->rowcov       = e_dt_rowcov;
	dt->chunky       = s_chunky;
	dt->chunky_pitch = ECS_W;
	dt->line_bytes   = ECS_PITCH;            /* one plane's pitch */
	dt->plane_bytes  = (long)ECS_PITCH * ECS_H;
	dt->w            = ECS_W;
	dt->h            = ECS_H;
	dt->nplanes      = ECS_DEPTH;
	dt->nbands       = ECS_NBANDS;
	return 1;
}

static void remap_rect(short x, short y, short w, short h);

/* Prepare row y of e_dt: NEW-INK scan, then skip (writer-stamped) or bridge
 * (remap + c2p the row into e_dt). Returns 1 if bridged. */
static int ecs_dt_ready_row(short y)
{
	const unsigned char *crow = s_chunky + (long)y * ECS_W;
	unsigned char *pl[ECS_DEPTH];
	short x, p;

	for (x = 0; x < ECS_W; x++)
		if (!e_used_idx[crow[x]])
			e_new_ink++;
	if (e_dt_rowcov[y] == ECS_W
	    && memcmp(e_dt_idx + (long)y * ECS_W, crow, ECS_W) == 0)
		return 0;
	remap_rect(0, y, ECS_W, 1);
	for (p = 0; p < ECS_DEPTH; p++)
		pl[p] = e_dt + (long)p * ECS_PITCH * ECS_H;
	c2p_amiga_n_rect(s_remap_buf, ECS_W, pl, ECS_PITCH,
	                 0, y, ECS_W, 1, ECS_DEPTH);
	return 1;
}

/* Copy row y's five plane slices from e_dt into a display plane set. */
static void ecs_dt_copy_row(unsigned char *set, short y)
{
	short p;

	for (p = 0; p < ECS_DEPTH; p++)
		CopyMem(e_dt + (long)p * ECS_PITCH * ECS_H + (long)y * ECS_PITCH,
		        set  + (ULONG)p * ECS_PITCH * ECS_H + (long)y * ECS_PITCH,
		        ECS_PITCH);
}
#endif /* FRUA_PLANAR */

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

		for (c = 0; c < w; c++) {
			if (!e_used_idx[src[c]])
				e_new_ink++;
			dst[c] = lut[src[c]];
		}
	}
}

/* Re-band: histogram + per-band reduce over the current surface, then patch the
 * copper's per-band COLOR words. */
static void ecs_reband(void)
{
	short b, i;

	quant_banded(s_chunky, ECS_W, ECS_H, s_clut,
	             ECS_NBANDS, ECS_NCOL, ECS_BITS, s_band_pal, s_band_remap);
	/* Capture what this quant saw: the global used set (the new-ink
	 * detector's domain) and the per-band sets (the split-guard's). */
	{
		long n;
		short y;

		memset(e_used_idx, 0, sizeof e_used_idx);
		memset(e_used_band, 0, sizeof e_used_band);
		for (y = 0; y < ECS_H; y++) {
			short bb = (short)((long)y * ECS_NBANDS / ECS_H);
			const unsigned char *row = s_chunky + (long)y * ECS_W;

			for (n = 0; n < ECS_W; n++) {
				e_used_idx[row[n]]      = 1;
				e_used_band[bb][row[n]] = 1;
			}
		}
	}
	/* Per-band slot representatives (the repalette's map): for each used
	 * index, keep the one whose CLUT colour sits nearest its slot's reduced
	 * RGB. One distance per used index — cheap next to the quant itself. */
	{
		long best[ECS_NCOL];
		short bnd, i;

		for (bnd = 0; bnd < ECS_NBANDS; bnd++) {
			const unsigned char *brem = s_band_remap + (long)bnd * 256;
			const unsigned char *bpal = s_band_pal + (long)bnd * ECS_NCOL * 3;

			for (i = 0; i < ECS_NCOL; i++) {
				e_slot_rep[bnd][i] = 0xFF;
				best[i] = 0x7fffffffL;
			}
			for (i = 0; i < 256; i++) {
				short s;
				long d;

				if (!e_used_band[bnd][i])
					continue;
				s = brem[i];
				d = e_coldist(s_clut + (long)i * 3,
				              bpal + (long)s * 3);
				if (d < best[s]) {
					best[s] = d;
					e_slot_rep[bnd][s] = (unsigned char)i;
				}
			}
		}
	}
	memcpy(e_clut_quant, s_clut, sizeof e_clut_quant);
	e_quant_valid = 1;
#ifdef FRUA_PLANAR
	ecs_dt_epoch_reset();            /* slots renumbered: stamps are stale */
#endif
	for (b = 0; b < ECS_NBANDS; b++) {
		const unsigned char *bp = s_band_pal + (long)b * ECS_NCOL * 3;

		for (i = 0; i < ECS_NCOL; i++)
			*s_cop_pal[b][i] = (UWORD)(((bp[i * 3 + 0] >> 4) << 8)
			                          | ((bp[i * 3 + 1] >> 4) << 4)
			                          | (bp[i * 3 + 2] >> 4));
	}
	s_dirty = 0;
	s_have_pal = 1;
	s_force_full = 1;               /* every LUT moved: row diffing is void */
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
	CopyMem(s_chunky, s_shadow, (ULONG)ECS_W * ECS_H);
	cop_point_planes(back);
	s_front ^= 1;
	s_force_full = 0;
}

/* Full present with ROW DIFFING (the ST backend's fix, ported: a real bare-ECS
 * machine is a 7MHz 68000, slower than the STE). The engine's modal loops end
 * every pass in a full present; converting all 64000 pixels each time reads as
 * frozen input at that speed. Diffed rows convert straight into the DISPLAYED
 * plane set — the same policy present_rect already uses (at worst one frame of
 * shear inside a changed row); the tear-free back-buffer flip is reserved for
 * the force-full path (a re-band), where every row converts anyway. */
static void ecs_present(void)
{
	unsigned char *front;
	unsigned char *planes[ECS_DEPTH];
	short p, y;

	if (s_dirty && e_quant_valid) {
		if (memcmp(s_clut, e_clut_quant, sizeof e_clut_quant) == 0) {
			/* CLUT-guard: identical CLUT would reproduce identical
			 * bands — the engine's defensive re-install. Skip. */
			dbg_log("ecs: quant skipped (CLUT unchanged)");
			s_dirty = 0;
		} else if (!s_force_full
		           && memcmp(s_chunky, s_shadow,
		                     (long)ECS_W * ECS_H) == 0
		           && !ecs_remap_split()) {
			/* Content unchanged: remaps stay valid, only slot->RGB
			 * moved — copper reload, no re-quant, no re-render. */
			dbg_log("ecs: repalette (content unchanged)");
			ecs_repalette();
			s_dirty = 0;
		}
	}
	if (s_force_full || s_dirty) {
		ecs_render();
	} else {
		front = s_planes[s_front];
		for (p = 0; p < ECS_DEPTH; p++)
			planes[p] = front + (ULONG)p * ECS_PITCH * ECS_H;
		for (y = 0; y < ECS_H; y++) {
			if (memcmp(s_chunky + (long)y * ECS_W,
			           s_shadow + (long)y * ECS_W, ECS_W) == 0)
				continue;
#ifdef FRUA_PLANAR
			if (e_dt != NULL && s_have_pal) {
				/* skip-or-bridge via the draw-time stamps, then
				 * copy the finished plane row to the display. */
				(void)ecs_dt_ready_row(y);
				ecs_dt_copy_row(front, y);
			} else
#endif
			{
				remap_rect(0, y, ECS_W, 1);
				c2p_amiga_n_rect(s_remap_buf, ECS_W, planes,
				                 ECS_PITCH, 0, y, ECS_W, 1,
				                 ECS_DEPTH);
			}
			CopyMem(s_chunky + (long)y * ECS_W,
			        s_shadow + (long)y * ECS_W, ECS_W);
		}
	}
	/* NEW-INK re-quant trigger: SCHEDULE only — the next full present
	 * re-bands against the complete frame (the standing mid-draw policy). */
	if (s_have_pal && e_new_ink >= 4) {
		s_dirty       = 1;
		e_quant_valid = 0;  /* bypass the CLUT-guard: content changed */
	}
	e_new_ink = 0;
}

static void ecs_present_rect(short x, short y, short w, short h)
{
	unsigned char *front = s_planes[s_front];
	unsigned char *planes[ECS_DEPTH];
	short p, x1;

	/* NEVER re-band here (same policy as the ST backend): a dirty palette
	 * means a scene change is mid-draw, and re-banding against a half-drawn
	 * frame bakes wrong palettes in. Rect draws go through the CURRENT LUTs;
	 * the next FULL present re-bands against the complete frame. */
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
	/* Keep the row-diff shadow current for the converted spans. */
	{
		short r;

		for (r = 0; r < h; r++)
			CopyMem(s_chunky + (long)(y + r) * ECS_W + x,
			        s_shadow + (long)(y + r) * ECS_W + x, w);
	}
	/* NEW-INK re-quant trigger — schedule only; rect presents NEVER re-band
	 * (mid-draw policy above), the next full present does. */
	if (s_have_pal && e_new_ink >= 4) {
		s_dirty       = 1;
		e_quant_valid = 0;  /* bypass the CLUT-guard: content changed */
	}
	e_new_ink = 0;
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
	/* Only a SUBSTANTIAL load (a scene change) marks the bands dirty; small
	 * writes are palette cycling, whose re-band + full re-render churn is
	 * what froze the ST live test (same policy there). Deferred to the next
	 * full present, when the surface is completely drawn. */
	if (count >= 32 || !s_have_pal)
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
	2,                      /* page-flipped (ecs_present flips s_front) — see
	                         * the AGA note; both this and AGA left it 0 and
	                         * were driven single-buffered. */
};

const dsp_backend_t *dsp_backend_ecs(void)
{
	return &ecs_backend;
}

#endif /* FRUA_AMIGA */
