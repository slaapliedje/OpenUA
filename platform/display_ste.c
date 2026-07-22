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
#include "c2p4st.h"             /* the nibble-optimized 4-plane span */
#include "quantize.h"
#include "planar.h"             /* dungeon-viewport planar composite (B2) */

#define ST_W        320
#define ST_H        200
#define ST_DEPTH    4                   /* 16 colours */
#define ST_NCOL     (1 << ST_DEPTH)     /* 16 */
#define ST_BITS     4                   /* STE: 4 bits/gun */
#define ST_NBANDS   10                  /* 20 scanlines per band (200/10).
                                         * The banded prototype showed 4-12
                                         * bands capture most of the win; 10
                                         * costs 2.5x less per re-band and
                                         * 2.5x fewer raster interrupts than
                                         * the first-cut 25. */
#define ST_RPB      (ST_H / ST_NBANDS)
#define LINE_BYTES  (ST_W * ST_DEPTH / 8)   /* 160 bytes/line, interleaved */
#define SCREEN_BYTES ((long)LINE_BYTES * ST_H)
#define NPAGES      2                        /* B4: double-buffered page flip.
                                              * SCREEN_BYTES (32000) is a multiple
                                              * of 256, so both pages stay 256-byte
                                              * aligned = a valid ST video base. */

/* ST colour hardware: 16 word registers at 0xFF8240. */
#define ST_COLORREGS ((volatile short *)0xFFFF8240UL)

static unsigned char *s_screen_raw;     /* the 2-page display allocation    */
static unsigned char *s_page[NPAGES];   /* the two 256-aligned display pages */
static unsigned char *s_screen;         /* = s_page[s_back]: this present's target */
static unsigned char *s_chunky;
static unsigned char *s_shadow_raw;     /* the 2-shadow allocation          */
static unsigned char *s_shadow_pg[NPAGES]; /* per-page "chunky as of last convert" */
static unsigned char *s_shadow;         /* = s_shadow_pg[s_back]            */
static short          s_back;           /* hidden page = the full-present draw target */
static short          s_shown;          /* page currently displayed        */
static void          *s_flip_target;    /* video base to latch at the next VBL */
/* B4: after a re-band BOTH pages' planes are stale (old palette / renumbered
 * slots), so the force-full and smart-skip must repeat for NPAGES presents, not
 * one. These are now COUNTS of pages still owing the treatment (set to NPAGES on
 * init/re-band, decremented as each present consumes one). */
static short          s_force_full;     /* pages still owing a full convert */
static dsp_surface_t  s_surface;
static short          s_save_rez = -1;
static void          *s_save_phys, *s_save_log;
static short          s_ints_on;

/* Quantizer state. st_band_stpal / st_band_ptr cross the interrupt boundary
 * and are referenced by name from the asm handlers, so they are non-static
 * (survive -O2 renaming/DCE). st_band_stpal has ONE SENTINEL ROW beyond the
 * last band: Timer B fires once more at the very last display line, and the
 * branch-free handler happily loads "band 25" — the sentinel (a copy of the
 * last band) makes that read harmless. */
static unsigned char           s_clut[256 * 3];
static unsigned char           s_band_pal[ST_NBANDS * ST_NCOL * 3];
static unsigned char           s_band_remap[ST_NBANDS * 256];
/* ADR-0016 B1: the CLUT the current band palettes were built from. A re-band
 * (median-cut over 32000 sampled pixels + 10 per-band reduces) is the ST's big
 * per-scene-change cost, and set_palette marks the bands dirty on EVERY
 * substantial load — including the engine's defensive re-installs of a palette
 * that did not change (a full recompose re-seats the same granite/menu CLUT).
 * The band palettes are a function of the CLUT (the scene's colour identity;
 * pixel-only changes never re-band — walking reuses them), so a re-band whose
 * CLUT matches this snapshot would reproduce the same palettes: skip it. */
static unsigned char           s_clut_banded[256 * 3];
static short                   s_banded_valid;
/* ADR-0016 B3.2 (stable-slot alignment + smart-skip). A re-band re-runs the
 * median-cut, which normally renumbers the 16 slots arbitrarily — so EVERY value
 * looks like it moved and the whole screen must re-c2p (the force-full). We instead
 * PERMUTE the new slots to best-match the previous palette's positions, so a colour
 * that persists across the re-band keeps its slot number; its remap entry is then
 * unchanged, and st_blit_full can leave its pixels' planes alone. s_remap_dirty[v]
 * marks the values whose slot actually moved this re-band; s_remap_changed tells the
 * blit a re-band happened so it consults the dirty map instead of forcing full.
 * Purely a slot renumbering — the 16 colours and the final remap are identical, so
 * the displayed frame is unchanged (planes encode a slot, the palette supplies the
 * colour). */
static unsigned char           s_band_pal_prev[ST_NCOL * 3];
static unsigned char           s_remap_old[256];
static unsigned char           s_remap_dirty[256];
static short                   s_remap_changed;
static short                   s_have_prev_pal;
/* ADR-0016 B4 Phase-0 (scene-stable remap): a representative CLUT index per
 * slot, captured at each re-quant (the used index whose colour is nearest the
 * slot's median-cut centroid). When a palette change arrives with the surface
 * CONTENT unchanged (a within-scene fade / settle — de-risk #1 found these:
 * rebands #8/#14), the index->slot remap is unchanged so the on-screen planes
 * are already correct; only the slot->RGB hardware palette moved. st_repalette()
 * rebuilds st_band_stpal from the NEW CLUT via these reps — a pure palette-
 * register reload, no re-quant and no re-c2p. This is the invariant Strategy B
 * needs (a within-scene palette change never invalidates planes) and, in the
 * current chunky model, also skips the ~2.2s force-full those rebands used to
 * pay. A genuine scene change (content differs) still re-quants (st_reband). */
static unsigned char           s_slot_rep[ST_NCOL];
short  st_band_stpal[ST_NBANDS + 1][ST_NCOL];   /* ST-format, +sentinel  */
short *st_band_ptr;                             /* next band for Timer B */
static short                   s_dirty;
static short                   s_have_pal;
static short                   s_vbl_slot = -1;

#ifdef FRUA_STPROF
/* B3.0a: st_blit_full sets this to record whether the LAST full present took the
 * force-full path (every LUT moved → all 200 rows) or the row-diff path. Declared
 * here because st_blit_full is above the main FRUA_STPROF block. */
static short                   sp_forced_flag;
static unsigned char          *s_offpage;   /* B3.0b: non-displayed ST-RAM page */
#endif

/* --- dungeon-viewport planar composite (ADR-0016 B2) ---------------------
 *
 * The engine renders the first-person viewport into s_vp_scratch (a private
 * chunky buffer, addressed in ABSOLUTE screen coords so the existing wall/fill
 * placement math is untouched) rather than into s_chunky. At present time the
 * committed rect is converted to ST-Low planes through the SAME per-band remap
 * the c2p uses (so it shares the fixed per-scene palette) and dropped into the
 * viewport hole with planar_blit_stlow. Because the viewport no longer touches
 * s_chunky, the roster/HUD/chrome sharing those scanlines is static there and
 * st_blit_full's row-diff skips it — the point of the exercise.
 *
 * VP_MAX bounds the buffers; the live viewport is 88x88 at (24,24). The scratch
 * is addressed absolutely, so it must span up to the viewport's bottom-right. */
#define VP_MAX          128                     /* max viewport extent (abs) */
#define VP_SCR_PITCH    VP_MAX
#define VP_PLANE_STRIDE ((VP_MAX + 15) / 16 * 2)/* 16 bytes/plane row         */
static unsigned char s_vp_scratch[(long)VP_SCR_PITCH * VP_MAX];
static unsigned char s_vp_planes[ST_DEPTH * VP_PLANE_STRIDE * VP_MAX];
static short         s_vp_x, s_vp_y, s_vp_w, s_vp_h;
static short         s_vp_active;               /* a committed rect awaits composite */
static short         s_st_active;               /* this backend is the live one */
static unsigned char *st_vp_scratch(short *pitch);
static void           st_vp_commit(short x, short y, short w, short h);
static void           st_vp_composite(void);

/* --- raster-split interrupt handlers -------------------------------------
 *
 * MFP Timer B registers (byte-wide, odd addresses):
 *   TBCR 0xFFFA1B — control: 0 = stopped, 8 = event-count mode (counts DE,
 *                   i.e. visible scanlines)
 *   TBDR 0xFFFA21 — the reload count
 * The counter FREE-RUNS once armed, and 200 % ST_RPB == 0, so whatever line
 * phase it starts on PERSISTS FOREVER. The VBL therefore re-phases it every
 * frame: stop, reload ST_RPB, restart — the first fire is then exactly at the
 * end of display line ST_RPB-1, every frame. (The un-phased first version had
 * every band's palette arriving a constant k lines late — visible as gnarly
 * band offsets. Live-tested by the user, 2026-07-15.)
 */
#define TBCR (*(volatile unsigned char *)0xFFFFFA1BUL)
#define TBDR (*(volatile unsigned char *)0xFFFFFA21UL)

/* VBL (C, via the rts trampoline below — the vertical blank has time to
 * spare): re-phase Timer B, load band 0's palette, point the raster handler
 * at band 1.
 *
 * The timer is phased ONE LINE EARLY: the first fire comes after ST_RPB-1
 * display lines (the LAST line of the band), and the reload register is then
 * set back to ST_RPB for every later fire. The handler uses that early line
 * to get all its latency out of the way — interrupt entry, register save,
 * palette pre-load — then SPINS on the MFP's own count register until the
 * line's display ends and drops the 16 colours entirely inside the border
 * (see the trampoline below). Fired at the boundary itself, the store landed
 * ~120 cycles into the new band's first visible line — the "weird lines" of
 * the live test. */
void st_vbl_handler(void)
{
	volatile short *hw = ST_COLORREGS;
	short i;

	TBCR = 0;                       /* stop: the writes must not race       */
	TBDR = ST_RPB - 1;              /* first fire ONE LINE EARLY            */
	TBCR = 8;                       /* event-count mode, re-armed in phase  */
	TBDR = ST_RPB;                  /* reload for all LATER fires (the MFP
	                                 * only picks this up at the next
	                                 * underflow, so the -1 above stands
	                                 * for the first) */

	for (i = 0; i < ST_NCOL; i++)
		hw[i] = st_band_stpal[0][i];
	st_band_ptr = &st_band_stpal[1][0];
}

__asm__(
	".globl _st_vbl_trampoline\n"
	"_st_vbl_trampoline:\n"
	"  moveml %d0-%d2/%a0-%a2,%sp@-\n"
	"  jbsr   _st_vbl_handler\n"
	"  moveml %sp@+,%d0-%d2/%a0-%a2\n"
	"  rts\n"
);
extern void st_vbl_trampoline(void);

/* Timer B: pure asm, fired one line EARLY (see st_vbl_handler). The visible
 * line before a band boundary absorbs all the slow parts — interrupt entry,
 * the movem register save, the movem palette pre-load into d0-d7. Then the
 * handler spins on TBDR: the MFP decrements it at the end of each display
 * line, so the value dropping below ST_RPB IS the boundary line's display
 * ending. The 16-register movem store (~80 cycles) then lands entirely inside
 * the ~192-cycle border/blank window — no mid-line palette switch, at worst
 * one poll (~20 cycles) of jitter. If the handler was entered so late that
 * the line already ended, the spin falls straight through. ISRA bit 0
 * (Timer B = MFP channel 8) is cleared on exit; the MFP clears only the ZERO
 * bits of the written mask, hence 0xFE. */
typedef char st_asm_assumes_rpb_20[(ST_RPB == 20) ? 1 : -1];
__asm__(
	".globl _st_timerb_trampoline\n"
	"_st_timerb_trampoline:\n"
	"  moveml %d0-%d7/%a0,%sp@-\n"
	"  movel  _st_band_ptr,%a0\n"
	"  moveml %a0@+,%d0-%d7\n"
	"  movel  %a0,_st_band_ptr\n"
	"1:\n"
	"  cmpib  #20,0xFFFFFA21\n"     /* TBDR still at reload (ST_RPB)?      */
	"  jeq    1b\n"                 /* yes: the line is still displaying   */
	"  moveml %d0-%d7,0xFFFF8240\n"
	"  moveml %sp@+,%d0-%d7/%a0\n"
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
 * group). `w` is a multiple of 32 except a possible 16-pixel tail. The
 * 32-pixel body is the nibble-optimized c2p4st_32 (see c2p4st.h) — roughly
 * half the general path's cost, and this is the hot loop under every screen
 * repaint on the 8MHz targets. */
static void st_c2p_span(const unsigned char *src, unsigned char *dst, short w,
                        const unsigned char *lut)
{
	short x;

	for (x = 0; x + 32 <= w; x += 32) {
		unsigned short *out = (unsigned short *)(dst + (long)(x / 16) * 8);

		/* ADR-0016 B3.2: a flat 32-px span skips the transpose entirely. */
		if (c2p4st_is_flat(src + x, 32))
			c2p4st_32_flat(src[x], lut, out);
		else
			c2p4st_32(src + x, lut, out);
	}
	if (x < w) {                            /* 16-pixel tail */
		unsigned char pad[32];
		unsigned short d[8];
		unsigned short *out = (unsigned short *)(dst + (long)(x / 16) * 8);
		short p;

		if (c2p4st_is_flat(src + x, 16)) {
			c2p4st_32_flat(src[x], lut, d);
		} else {
			memcpy(pad, src + x, 16);
			memset(pad + 16, 0, 16);        /* pads land only in d[4..7] */
			c2p4st_32(pad, lut, d);
		}
		for (p = 0; p < ST_DEPTH; p++)
			out[p] = d[p];          /* store pixels 0-15 only */
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
		memcpy(s_shadow + (long)yy * ST_W + x0, src, (size_t)w);
#ifdef FRUA_STPROF
		{ extern long g_stprof_rows; g_stprof_rows++; }
#endif
	}
}

/* Full present with ROW DIFFING: convert only the rows whose chunky content
 * changed since the last convert (tracked in s_shadow). The engine's modal
 * loops end EVERY pass in a full present; converting all 64000 pixels each
 * time cost ~a second at 8MHz whether anything moved or not — the "really
 * slow to respond" of the live test. A row memcmp is ~2 orders of magnitude
 * cheaper than its remap+c2p, so an idle pass collapses to the compare scan.
 * After a re-band every LUT changed, so the diff is void — convert all. */
static void st_blit_full(void)
{
	short y;

	/* B4: s_force_full / s_remap_changed are per-page COUNTS — a re-band leaves BOTH
	 * pages' planes stale, so the treatment repeats until every page is serviced. The
	 * row-diff / smart-skip run against THIS page's shadow (s_shadow = s_shadow_pg
	 * [s_back]), so each page independently tracks to the current chunky frame. */
	if (s_force_full > 0) {
		short pg;
#ifdef FRUA_STPROF
		sp_forced_flag = 1;
#endif
		/* B4: convert BOTH pages in THIS present. A re-band re-quantizes the whole
		 * palette, so a page force-fulled on an earlier re-band holds planes from an
		 * older CLUT; if the flip later shows it, the roster/HUD renders under the
		 * wrong palette — the "grey-on-grey" roster (clut 23/1 == the panel grey in
		 * that stale CLUT). Doing both pages here keeps them on the SAME (latest)
		 * palette, so whichever is shown is consistent. st_blit_rows writes s_screen/
		 * s_shadow, so repoint per page, then restore the back page for the composite. */
		for (pg = 0; pg < NPAGES; pg++) {
			s_screen = s_page[pg];
			s_shadow = s_shadow_pg[pg];
			st_blit_rows(0, ST_W, 0, ST_H);
		}
		s_screen = s_page[s_back];
		s_shadow = s_shadow_pg[s_back];
		s_force_full   = 0;
		s_remap_changed = 0;
		return;
	}
#ifdef FRUA_STPROF
	sp_forced_flag = 0;
#endif
	for (y = 0; y < ST_H; y++) {
		const unsigned char *crow = s_chunky + (long)y * ST_W;
		int conv = memcmp(crow, s_shadow + (long)y * ST_W, ST_W) != 0;

		/* ADR-0016 B3.2: after a re-band the content of a row can be UNCHANGED yet
		 * still need re-c2p if one of its colours moved to a new slot. Stable-slot
		 * alignment keeps most colours put, so this scan (only on a re-band pass,
		 * early-exit on the first moved value) leaves the static chrome/HUD alone
		 * instead of the old blanket force-full. */
		if (!conv && s_remap_changed > 0) {
			short x;
			for (x = 0; x < ST_W; x++)
				if (s_remap_dirty[crow[x]]) { conv = 1; break; }
		}
		if (conv)
			st_blit_rows(0, ST_W, y, 1);
	}
	if (s_remap_changed > 0)
		s_remap_changed--;
}

/* Squared RGB distance between two packed 3-byte colours (for slot alignment). */
static long st_coldist(const unsigned char *a, const unsigned char *b)
{
	long dr = (long)a[0] - b[0];
	long dg = (long)a[1] - b[1];
	long db = (long)a[2] - b[2];

	return dr * dr + dg * dg + db * db;
}

/* Replicate band 0's reduced palette to every band and encode the per-band
 * ST-format hardware palettes (STE gun encoding: nibble = (v0 << 3) | (v >> 1)),
 * plus the sentinel row (see st_band_stpal) and the CLUT snapshot the reband-skip
 * guard compares against. The remap is NOT touched here — only st_reband rebuilds
 * it; a palette-only refresh (st_repalette) reuses the fixed remap and just
 * re-encodes the RGB. Shared by st_reband and st_repalette. */
static void st_build_hw_palette(void)
{
	short b, i;

	for (b = 1; b < ST_NBANDS; b++)
		memcpy(s_band_pal + (long)b * ST_NCOL * 3, s_band_pal,
		       (size_t)(ST_NCOL * 3));
	for (b = 0; b < ST_NBANDS; b++) {
		const unsigned char *bp = s_band_pal + (long)b * ST_NCOL * 3;

		for (i = 0; i < ST_NCOL; i++) {
			short vr = bp[i * 3 + 0] >> 4;
			short vg = bp[i * 3 + 1] >> 4;
			short vb = bp[i * 3 + 2] >> 4;
			short rn = (short)(((vr & 1) << 3) | (vr >> 1));
			short gn = (short)(((vg & 1) << 3) | (vg >> 1));
			short bn = (short)(((vb & 1) << 3) | (vb >> 1));

			st_band_stpal[b][i] = (short)((rn << 8) | (gn << 4) | bn);
		}
	}
	for (i = 0; i < ST_NCOL; i++)
		st_band_stpal[ST_NBANDS][i] = st_band_stpal[ST_NBANDS - 1][i];
	memcpy(s_clut_banded, s_clut, sizeof s_clut);   /* B1: snapshot the CLUT */
	s_banded_valid = 1;
	s_dirty = 0;
	s_have_pal = 1;
}

/* B4 Phase-0: after a re-quant, capture one representative CLUT index per slot —
 * the index (band 0's remap) whose colour is nearest that slot's centroid. A
 * within-scene palette change then re-derives the slot's RGB by tracking THIS
 * actual palette entry through the new CLUT (faithful for a fade; a scan over
 * all 256 naturally prefers a used index, whose colour sits near the centroid,
 * over a luma-fallback one far from it). */
static void st_compute_slot_reps(void)
{
	short s, i;

	for (s = 0; s < ST_NCOL; s++) {
		long  bestd = 0x7fffffffL;
		short bi = 0;

		for (i = 0; i < 256; i++) {
			long d;
			if (s_band_remap[i] != s)
				continue;
			d = st_coldist(s_clut + (long)i * 3, s_band_pal + (long)s * 3);
			if (d < bestd) { bestd = d; bi = i; }
		}
		s_slot_rep[s] = (unsigned char)bi;
	}
}

/* B4 Phase-0: a palette-only refresh for a within-scene palette change (surface
 * content unchanged). Keep the fixed index->slot remap — so the on-screen planes
 * stay valid — and rebuild only the slot->RGB hardware palette from the NEW CLUT
 * via the captured representative indices. No re-quant, no re-c2p: st_blit_full's
 * row-diff then finds nothing changed and the raster split loads the new colours.
 * This is the "reband = palette-register-only" invariant Strategy B is built on. */
static void st_repalette(void)
{
	short s;

	for (s = 0; s < ST_NCOL; s++) {
		unsigned char idx = s_slot_rep[s];

		s_band_pal[(long)s * 3 + 0] = quant_snap(s_clut[idx * 3 + 0], ST_BITS);
		s_band_pal[(long)s * 3 + 1] = quant_snap(s_clut[idx * 3 + 1], ST_BITS);
		s_band_pal[(long)s * 3 + 2] = quant_snap(s_clut[idx * 3 + 2], ST_BITS);
	}
	memcpy(s_band_pal_prev, s_band_pal, sizeof s_band_pal_prev);
	st_build_hw_palette();
	s_force_full   = 0;      /* planes unchanged: nothing to re-convert */
	s_remap_changed = 0;
}

/* Re-band: histogram + per-band reduce, then build the per-band ST-format
 * palettes (STE gun encoding: nibble = (v0 << 3) | (v >> 1)). The sentinel
 * row (see st_band_stpal) is a copy of the last band. */
static void st_reband(void)
{
	short b, i;
	const unsigned char *qsrc = s_chunky;
	short first = !s_have_prev_pal;

	/* Snapshot the remap we're about to replace, so the smart-skip can tell which
	 * values actually move slot this re-band (B3.2). Band 0 is representative —
	 * every band carries the same remap after the B1 replicate below. */
	memcpy(s_remap_old, s_band_remap, 256);

	/* Pin the composited walls' colours (ADR-0016 B1). After B2.1 the dungeon
	 * viewport renders into the planar SCRATCH, not s_chunky, so the reband never
	 * saw the wall/backdrop colours and the composite mapped their CLUT indices
	 * through the luma fallback — walls came out in HUD greys, not their authored
	 * stone/wood/sky. When a viewport is committed (s_vp_active, still set here —
	 * the composite that clears it runs after us), quant over a copy of s_chunky
	 * with the scratch's viewport rect overlaid, so the fixed palette is derived
	 * from the walls too and their indices get exact slots. The temp lives in
	 * s_shadow, which the forced-full blit right after this rebuilds anyway. */
	if (s_vp_active) {
		short r;
		memcpy(s_shadow, s_chunky, (long)ST_W * ST_H);
		for (r = 0; r < s_vp_h; r++) {
			short yy = (short)(s_vp_y + r);
			memcpy(s_shadow + (long)yy * ST_W + s_vp_x,
			       s_vp_scratch + (long)yy * VP_SCR_PITCH + s_vp_x,
			       (size_t)s_vp_w);
		}
		qsrc = s_shadow;
	}

	/* ADR-0016 B1 (fixed per-scene palette): ONE global reduce over the whole
	 * frame (nbands=1 histograms all rows), replicated to every band. A flat
	 * colour that spans bands is now the SAME slot+RGB everywhere, so the per-band
	 * palette SEAMS — the visible "banding" (#40), a uniform panel rendered as
	 * brown/green/olive stripes — vanish. This is also approach B's target
	 * palette model: the per-band scheme existed to stop the granite chrome
	 * starving the viewport, but post-B2.1 the viewport is composited as its own
	 * planar region, so the shared surface holds only the (flat, seam-prone) HUD
	 * plus the overlaid walls above, which one 16-colour palette covers. The
	 * raster-split machinery stays (identical per-band loads) so per-band
	 * anchoring can return later if an art-heavy screen needs the extra colours. */
	quant_banded(qsrc, ST_W, ST_H, s_clut,
	             1, ST_NCOL, ST_BITS, s_band_pal, s_band_remap);

	/* B3.2 STABLE-SLOT ALIGNMENT: permute band 0's fresh 16 slots so each lands at
	 * the position holding the closest colour in the PREVIOUS palette — a colour
	 * that persists across the re-band keeps its slot number, so its remap entry
	 * doesn't move and the smart-skip leaves the static chrome/HUD un-converted. A
	 * pure renumber (colours + final remap unchanged), so the frame is identical. */
	if (!first) {
		unsigned char used[ST_NCOL];
		unsigned char pos[ST_NCOL];             /* pos[newslot] = its position   */
		unsigned char newpal[ST_NCOL * 3];
		short p, n, v;

		for (n = 0; n < ST_NCOL; n++) used[n] = 0;
		for (p = 0; p < ST_NCOL; p++) {         /* each old position claims one  */
			short best = 0;
			long  bestd = 0x7fffffffL;
			for (n = 0; n < ST_NCOL; n++) {
				long d;
				if (used[n]) continue;
				d = st_coldist(s_band_pal + (long)n * 3,
				               s_band_pal_prev + (long)p * 3);
				if (d < bestd) { bestd = d; best = n; }
			}
			used[best] = 1;
			pos[best]  = (unsigned char)p;
		}
		for (n = 0; n < ST_NCOL; n++)
			memcpy(newpal + (long)pos[n] * 3, s_band_pal + (long)n * 3, 3);
		memcpy(s_band_pal, newpal, sizeof newpal);
		for (v = 0; v < 256; v++)
			s_band_remap[v] = pos[s_band_remap[v]];
	}

	/* Which values changed slot vs the last convert — the smart-skip's dirty map. */
	for (i = 0; i < 256; i++)
		s_remap_dirty[i] = (unsigned char)(s_remap_old[i] != s_band_remap[i]);
	memcpy(s_band_pal_prev, s_band_pal, sizeof s_band_pal_prev);
	s_have_prev_pal = 1;

	/* Replicate the fixed remap to every band (the pal replicate + hardware
	 * encode happen in st_build_hw_palette). */
	for (b = 1; b < ST_NBANDS; b++)
		memcpy(s_band_remap + (long)b * 256, s_band_remap, 256);

	st_compute_slot_reps();          /* B4 Phase-0: reps for palette-only rebands */
	st_build_hw_palette();

	/* B3.2: the FIRST re-band has no aligned predecessor, and the viewport path
	 * clobbers s_shadow (its temp), so both must convert everything. Otherwise the
	 * stable-slot alignment kept most slots put, so arm the smart-skip: st_blit_full
	 * re-c2p's only rows whose content changed OR that hold a value that moved slot.
	 * The static granite chrome (slots preserved) is then left alone across the
	 * re-band instead of the old blanket full convert. */
	/* B4: FORCE-FULL on every re-band (not the smart-skip). The smart-skip's
	 * s_remap_dirty is computed ONCE per re-band (old remap vs new), but the two
	 * pages were last drawn with DIFFERENT remaps (they alternate), so that single
	 * dirty map is wrong for the other page (the "brown chrome"). st_blit_full's
	 * force-full path now converts BOTH pages in one present against the current
	 * palette, so a single flag suffices — and both pages stay on the SAME palette,
	 * fixing the grey-on-grey roster (a page force-fulled on an older CLUT never
	 * gets shown). Costs 2 c2p's on a re-band only; re-bands are rare and the
	 * flat-fill already tamed the c2p. */
	(void)first;
	s_force_full   = 1;
	s_remap_changed = 0;
}

#ifdef FRUA_PLANAR
/* --- draw-time plane target (ADR-0016 B4) --------------------------------
 *
 * The writer-by-writer transition off the chunky+c2p path. Converted Toolbox/
 * engine writers stamp their pixels straight into this plane buffer (through the
 * same per-band remap the c2p uses) in parallel with their existing chunky store.
 * s_dt accumulates the converted writers' output; once EVERY writer for a screen
 * is converted it is a complete plane image and the present can flip it instead
 * of c2p'ing s_chunky (a later step). Compiled out of the shipping build. */
static unsigned char *s_dt;             /* draw-time plane accumulation buffer */

static int st_dt_target(struct dsp_planar_dt *dt)
{
	if (!s_have_pal || s_dt == NULL)
		return 0;                        /* no palette / no remap yet */
	dt->planes     = s_dt;
	dt->remap      = s_band_remap;
	dt->line_bytes = LINE_BYTES;
	dt->w          = ST_W;
	dt->h          = ST_H;
	dt->nplanes    = ST_DEPTH;
	dt->nbands     = ST_NBANDS;
	return 1;
}
#endif /* FRUA_PLANAR */

/* --- backend entry points ------------------------------------------------ */

static int st_init(short want_w, short want_h)
{
	(void)want_w; (void)want_h;

	s_screen_raw = (unsigned char *)Mxalloc(NPAGES * SCREEN_BYTES + 256, 0); /* ST-RAM */
	s_chunky     = (unsigned char *)Mxalloc((long)ST_W * ST_H, 0);
	s_shadow_raw = (unsigned char *)Mxalloc((long)NPAGES * ST_W * ST_H, 0);
	if (s_screen_raw == NULL || s_chunky == NULL || s_shadow_raw == NULL) {
		dbg_log("ste: Mxalloc FAILED");
		if (s_screen_raw) { Mfree(s_screen_raw); s_screen_raw = NULL; }
		if (s_chunky)     { Mfree(s_chunky); s_chunky = NULL; }
		if (s_shadow_raw) { Mfree(s_shadow_raw); s_shadow_raw = NULL; }
		return 1;
	}
	{
		short p;
		unsigned char *base = (unsigned char *)
		    (((uintptr_t)s_screen_raw + 255) & ~(uintptr_t)255);
		for (p = 0; p < NPAGES; p++) {
			s_page[p]      = base + (long)p * SCREEN_BYTES;
			s_shadow_pg[p] = s_shadow_raw + (long)p * ST_W * ST_H;
			memset(s_page[p], 0, SCREEN_BYTES);
			memset(s_shadow_pg[p], 0, (size_t)ST_W * ST_H);
		}
	}
	memset(s_chunky, 0, (size_t)ST_W * ST_H);
	s_back       = NPAGES - 1;               /* draw the back page; show page 0 */
	s_shown      = 0;
	s_screen     = s_page[s_back];
	s_shadow     = s_shadow_pg[s_back];
	s_force_full = 1;                         /* first present converts both pages */

	s_save_rez  = Getrez();
	s_save_phys = Physbase();
	s_save_log  = Logbase();
	Setscreen(s_save_log, s_page[0], 0);     /* ST Low; show page 0; console keeps log */

	s_surface.width  = ST_W;
	s_surface.height = ST_H;
	s_surface.pitch  = ST_W;
	s_surface.pixels = s_chunky;
	s_dirty    = 1;
	s_have_pal = 0;
	st_band_ptr = &st_band_stpal[1][0];      /* valid before the first fire */

	/* Install the raster split: a VBL slot (re-phases Timer B + loads band 0)
	 * and Timer B in event-count mode firing every ST_RPB display lines. */
	Supexec(st_vbl_install_super);
	Xbtimer(1, 8, ST_RPB, st_timerb_trampoline);   /* timer B, event count */
	s_ints_on = 1;

	/* Take over the dungeon-viewport composite (ADR-0016 B2). */
	s_vp_active = 0;
	s_st_active = 1;
	planar_viewport_register(st_vp_scratch, st_vp_commit);

#ifdef FRUA_PLANAR
	/* Draw-time plane accumulation buffer + hook (ADR-0016 B4). */
	s_dt = (unsigned char *)Mxalloc(SCREEN_BYTES, 0);
	if (s_dt != NULL)
		memset(s_dt, 0, SCREEN_BYTES);
	planar_draw_target_register(st_dt_target);
#endif

#ifdef FRUA_STPROF
	/* B3.0b scratch: a non-displayed ST-RAM page the c2p can target for timing. */
	s_offpage = (unsigned char *)Mxalloc(SCREEN_BYTES, 0);
	if (s_offpage != NULL)
		memset(s_offpage, 0, SCREEN_BYTES);
#endif

	dbg_log("ste: ST-low 320x200x4 16-colour, per-band Timer-B palette up");
	return 0;
}

static void st_shutdown(void)
{
	if (s_st_active) {
		planar_viewport_register((unsigned char *(*)(short *))0,
		                         (void (*)(short, short, short, short))0);
#ifdef FRUA_PLANAR
		planar_draw_target_register((int (*)(struct dsp_planar_dt *))0);
		if (s_dt) { Mfree(s_dt); s_dt = NULL; }
#endif
		s_st_active = 0;
		s_vp_active = 0;
	}
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
	if (s_shadow_raw) { Mfree(s_shadow_raw); s_shadow_raw = NULL; s_shadow = NULL; }
#ifdef FRUA_STPROF
	if (s_offpage)    { Mfree(s_offpage); s_offpage = NULL; }
#endif
}

static dsp_surface_t *st_surface(void)
{
	return &s_surface;
}

/* ADR-0016 (approach B): expose the fixed per-band remap so the engine's
 * native-planar writers convert to the SAME 16-colour slots this backend's c2p
 * uses. Valid once a palette has been installed (s_have_pal); the reband keeps
 * it current per scene. */
const unsigned char *dsp_planar_remap(short *nbands, short *screen_h)
{
	if (nbands)   *nbands   = ST_NBANDS;
	if (screen_h) *screen_h = ST_H;
	return s_have_pal ? s_band_remap : (const unsigned char *)0;
}

/* --- dungeon-viewport composite hooks (registered via planar_viewport_register) */

/* The engine's viewport render target: our private chunky scratch, addressed in
 * absolute screen coords. */
static unsigned char *st_vp_scratch(short *pitch)
{
	if (pitch)
		*pitch = VP_SCR_PITCH;
	return s_vp_scratch;
}

/* Record the just-rendered viewport rect; the next present converts + composites
 * it (deferred so it runs AFTER any re-band, i.e. against the live palette). */
static void st_vp_commit(short x, short y, short w, short h)
{
	if (w <= 0 || h <= 0) { s_vp_active = 0; return; }
	if (x < 0 || y < 0 || x + w > VP_MAX || y + h > VP_MAX) {
		s_vp_active = 0;                 /* out of the buffer's reach: skip */
		return;
	}
	s_vp_x = x; s_vp_y = y; s_vp_w = w; s_vp_h = h;
	s_vp_active = 1;
}

/* Convert the committed rect (chunky, per-band remap) to ST-Low planes and drop
 * it into the viewport hole. One-shot: cleared after compositing so a later
 * full recompose (menu/combat) that did NOT re-commit leaves the surface alone.
 * Called at the tail of every present. */
static void st_vp_composite(void)
{
	unsigned char *pp[ST_DEPTH];
	long planebytes;
	short r, c, p;

	if (!s_vp_active)
		return;
	s_vp_active = 0;                         /* one-shot per commit */
	if (!s_have_pal)
		return;                          /* no palette yet: nothing to map */

	planebytes = (long)VP_PLANE_STRIDE * s_vp_h;
	for (p = 0; p < ST_DEPTH; p++)
		pp[p] = s_vp_planes + (long)p * planebytes;
	memset(s_vp_planes, 0, (size_t)(planebytes * ST_DEPTH));

	/* chunky -> separate planes, remapping each pixel through its band's LUT
	 * (the viewport spans bands 1..5; the row's band picks the map). */
	for (r = 0; r < s_vp_h; r++) {
		short yy   = (short)(s_vp_y + r);
		short band = (short)((long)yy * ST_NBANDS / ST_H);
		const unsigned char *lut = s_band_remap + (long)band * 256;
		const unsigned char *srow =
		    s_vp_scratch + (long)yy * VP_SCR_PITCH + s_vp_x;
		long rowoff = (long)r * VP_PLANE_STRIDE;

		for (c = 0; c < s_vp_w; c++) {
			unsigned char slot = lut[srow[c]];
			unsigned char bit  = (unsigned char)(0x80u >> (c & 7));
			short byte = (short)(c >> 3);

			for (p = 0; p < ST_DEPTH; p++)
				if ((slot >> p) & 1)
					pp[p][rowoff + byte] |= bit;
		}
	}

	/* B4: drop the converted viewport into BOTH pages' holes, not just the one being
	 * drawn. The commit is one-shot (a full present that flips to the other page would
	 * otherwise show that page's stale/black viewport hole), and the viewport is tiny
	 * (88x88) so blitting it twice is cheap. */
	{
		short pg;
		for (pg = 0; pg < NPAGES; pg++)
			planar_blit_stlow(pp, VP_PLANE_STRIDE, s_vp_w, s_vp_h, ST_DEPTH,
			                  s_page[pg], LINE_BYTES, ST_W, ST_H,
			                  s_vp_x, s_vp_y);
	}
}

#ifdef FRUA_STPROF
/* Coarse present-path profile: every 128 full presents, log wall ticks vs
 * ticks spent inside present and the rows actually converted. TickCount is
 * the compat layer's 60Hz tick — a layering reach-down, debug-only. */
extern long TickCount(void);
static long sp_n, sp_rows, sp_in, sp_wall0 = -1, sp_reband, sp_reband_skip;
static long sp_rows_prev;               /* g_stprof_rows at end of prev present */
static long sp_logs;                    /* B3.0a per-present logs emitted (capped) */
static long sp_b30b_done;               /* B3.0b samples taken (capped one-shot) */
long g_stprof_rows;                     /* incremented in st_blit_rows */

/* Full-frame chunky->ST-Low c2p to an ARBITRARY base (no shadow write, no diff),
 * identical work to st_blit_rows' inner loop. Used by B3.0b to time the same c2p
 * to the live screen vs an off-screen page. */
static void st_c2p_page(unsigned char *dstbase)
{
	short y;
	for (y = 0; y < ST_H; y++) {
		short band = (short)((long)y * ST_NBANDS / ST_H);
		const unsigned char *lut = s_band_remap + (long)band * 256;
		const unsigned char *src = s_chunky + (long)y * ST_W;
		unsigned char *dst = dstbase + (long)y * LINE_BYTES;
		st_c2p_span(src, dst, ST_W, lut);
	}
}

/* B3.0b: is the c2p cost COMPUTE or CONTENTION? Time an identical full-frame c2p
 * to the LIVE displayed screen vs a non-displayed ST-RAM page. Both are ST-RAM and
 * both suffer the Timer-B raster-split interrupts; only the live page also contends
 * with the video shifter's DMA fetch. A large live>offscreen gap => contention
 * dominates (double-buffer wins cheap, B3.1); near-equal => compute is the floor
 * (native-planar writers needed, B3.2+). 8 reps to average out 60Hz tick coarseness. */
static void st_prof_b30b(void)
{
	long a, tl, to, i;
	const long reps = 4;

	if (s_offpage == NULL)
		return;
	a = TickCount();
	for (i = 0; i < reps; i++)
		st_c2p_page(s_screen);
	tl = TickCount() - a;
	a = TickCount();
	for (i = 0; i < reps; i++)
		st_c2p_page(s_offpage);
	to = TickCount() - a;
	dbg_log_num("stprof b30b: live x4 ticks     = ", tl);
	dbg_log_num("stprof b30b: offscreen x4 ticks= ", to);
}
#endif

/* Latch a new video base (supervisor: the base registers are protected). The
 * shifter reloads the base at the next VBL, so this is a NON-BLOCKING flip — the
 * freshly-drawn back page appears atomically at vblank, no mid-c2p tearing. Only
 * the hi/mid base bytes are written: pages are 256-aligned so the STE low byte
 * ($820D) is always 0, which is its power-on value — leaving it out keeps this
 * correct on a plain ST too (no $820D there). */
static long st_flip_super(void)
{
	unsigned long a = (unsigned long)(uintptr_t)s_flip_target;

	*(volatile unsigned char *)0xFFFF8201UL = (unsigned char)(a >> 16);
	*(volatile unsigned char *)0xFFFF8203UL = (unsigned char)(a >> 8);
	*(volatile unsigned char *)0xFFFF820DUL = (unsigned char)(a);   /* STE low byte */
	return 0;
}

/* Show the just-drawn back page and make the old front the next draw target. A
 * FULL present does this: the engine presents a full recompose NPAGES times (the
 * `pages` contract), so both pages end up carrying the frame. present_rect does
 * NOT flip — it draws the SHOWN page in place (see st_present_rect), so the walk's
 * small viewport update never desyncs the pages (an earlier flip-on-present_rect
 * showed the back page's stale/blank HUD). */
static void st_flip_full(void)
{
	s_flip_target = s_page[s_back];
	Supexec(st_flip_super);
	s_shown = s_back;
	s_back ^= 1;                             /* the old front is the next target */
}

static void st_present(void)
{
#ifdef FRUA_STPROF
	long t0 = TickCount();

	if (sp_wall0 < 0)
		sp_wall0 = t0;
	if (s_dirty)
		sp_reband++;
#endif
	/* B4: a full present targets the HIDDEN page, then flips to it. */
	s_screen = s_page[s_back];
	s_shadow = s_shadow_pg[s_back];
	if (s_dirty) {
		/* B1: only re-band when the CLUT actually moved since the last one;
		 * a matching CLUT would reproduce the same band palettes. */
		if (s_banded_valid &&
		    memcmp(s_clut, s_clut_banded, sizeof s_clut) == 0) {
			s_dirty = 0;
#ifdef FRUA_STPROF
			sp_reband_skip++;
#endif
		} else {
			/* B4 Phase-0 (scene-stable remap): a palette change whose surface
			 * content is UNCHANGED (a within-scene fade / settle — de-risk #1's
			 * rebands #8/#14) keeps the index->slot remap fixed, so the on-screen
			 * planes stay valid; only the slot->RGB hardware palette moved. Take
			 * the palette-register-only path (no re-quant, no force-full re-c2p).
			 * A genuine scene change (content differs, or a viewport is pending)
			 * re-quantises. s_shadow == s_chunky after any completed present, so
			 * an all-zero diff means nothing was drawn since. (st_reband borrows
			 * s_shadow as a temp, so this MUST be sampled before the dispatch.) */
			int content_same = s_banded_valid && !s_vp_active &&
			    memcmp(s_chunky, s_shadow, (long)ST_W * ST_H) == 0;
#ifdef FRUA_STPROF
			{
				short yy, ci;
				long  crows = 0, clut_moved = 0;
				for (yy = 0; yy < ST_H; yy++)
					if (memcmp(s_chunky + (long)yy * ST_W,
					           s_shadow + (long)yy * ST_W, ST_W) != 0)
						crows++;
				for (ci = 0; ci < 256 * 3; ci++)
					if (s_clut[ci] != s_clut_banded[ci]) clut_moved++;
				dbg_log_num("b4audit: reband #        = ", sp_reband);
				dbg_log_num("b4audit:   content rows  = ", crows);
				dbg_log_num("b4audit:   clut bytes mvd= ", clut_moved);
				dbg_log(content_same ? "b4audit:   -> repalette (registers only)"
				                     : "b4audit:   -> reband (re-quant)");
			}
#endif
			if (content_same)
				st_repalette();
			else
				st_reband();
		}
	}
	st_blit_full();
	st_vp_composite();                       /* overlay the planar viewport */
#ifdef FRUA_STPROF
	{
		/* B3.0a: log EACH present that actually converted rows, tagged by which
		 * path it took. A menu keypress that redraws a highlight should diff a
		 * handful of rows; if instead it forces all 200, the per-keypress lag is
		 * a spurious force-full (cheap to fix) rather than inherent c2p cost. */
		long rows_now = g_stprof_rows - sp_rows_prev;

		sp_rows_prev = g_stprof_rows;
		if (rows_now > 0 && sp_logs < 80) {
			sp_logs++;
			if (sp_forced_flag)
				dbg_log_num("b30a FORCED-full rows = ", rows_now);
			else
				dbg_log_num("b30a diffed rows      = ", rows_now);
		}
	}
	sp_in += TickCount() - t0;
	sp_rows = g_stprof_rows;
	/* B3.0b fires on the first few FULL presents once a palette exists — full
	 * presents are rare (scene/menu recomposes; dungeon walk uses rect presents),
	 * so gating on a 64-present boundary rarely triggers. The c2p cost is
	 * content-independent, so any handful of samples answers compute-vs-contention. */
	if (s_have_pal && sp_b30b_done < 6) {
		sp_b30b_done++;
		st_prof_b30b();
	}
	if ((++sp_n & 63) == 0) {
		dbg_log_num("stprof: presents = ", sp_n);
		dbg_log_num("stprof: wall ticks = ", TickCount() - sp_wall0);
		dbg_log_num("stprof: in-present ticks = ", sp_in);
		dbg_log_num("stprof: rows converted = ", sp_rows);
		dbg_log_num("stprof: rebands = ", sp_reband);
		dbg_log_num("stprof: reband skips = ", sp_reband_skip);
		st_prof_b30b();                  /* B3.0b: compute-vs-contention sample */
	}
#endif
	st_flip_full();                          /* B4: show this page, advance */
}

static void st_present_rect(short x, short y, short w, short h)
{
	short x1;

	/* B4: a partial update draws the SHOWN page IN PLACE and does not flip — the
	 * back page keeps whatever it had and catches up on the next full present. This
	 * is what keeps the two pages coherent (only full recomposes, presented NPAGES
	 * times, seed both). The in-place write tears only within this small rect (the
	 * dungeon viewport), not the whole screen. */
	s_screen = s_page[s_shown];
	s_shadow = s_shadow_pg[s_shown];

	/* NEVER re-band here. A dirty palette means a scene change is mid-draw
	 * (the intro blits its screens piece by piece); re-banding against a
	 * half-drawn frame bakes wrong palettes in, and doing it per piece on an
	 * 8MHz 68000 queued full-frame work faster than it drained — the "intro
	 * froze" the live test found. Rect draws go through the CURRENT LUTs
	 * (transiently wrong colours in the rect at worst); the next FULL present
	 * re-bands against the complete frame and settles everything. */
	if (x < 0) { w = (short)(w + x); x = 0; }
	if (y < 0) { h = (short)(h + y); y = 0; }
	if (x + w > ST_W) w = (short)(ST_W - x);
	if (y + h > ST_H) h = (short)(ST_H - y);
	if (w <= 0 || h <= 0)
		return;

	/* ADR-0016 B2.2: when the requested rect lies entirely within the active
	 * planar viewport, the composite below is authoritative for those pixels, so
	 * the chunky c2p of that (frozen, composite-overwritten) region is pure waste
	 * — skip it. The dungeon walk step presents exactly the 88x88 viewport rect,
	 * so this drops its per-step cost from a full c2p of the rect to just the
	 * plane blit. Other rects (chrome/text updates) take the normal c2p path. */
	if (s_vp_active
	    && x >= s_vp_x && y >= s_vp_y
	    && (short)(x + w) <= (short)(s_vp_x + s_vp_w)
	    && (short)(y + h) <= (short)(s_vp_y + s_vp_h)) {
		st_vp_composite();
		return;
	}

	x1 = (short)((x + w + 15) & ~15);        /* 16-pixel plane groups */
	x  = (short)(x & ~15);
	st_blit_rows(x, (short)(x1 - x), y, h);
	st_vp_composite();                       /* overlay the planar viewport */
#ifdef FRUA_STPROF
	/* B3.0a: keep the full-present row accounting caller-agnostic — attribute
	 * rect-converted rows here so they don't inflate the next st_present's count. */
	{
		long rows_now = g_stprof_rows - sp_rows_prev;

		sp_rows_prev = g_stprof_rows;
		if (rows_now > 0 && sp_logs < 80) {
			sp_logs++;
			dbg_log_num("b30a rect rows        = ", rows_now);
		}
	}
#endif
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
	/* Only a SUBSTANTIAL load (a scene/palette change) marks the bands
	 * dirty. Small-range writes are palette CYCLING (the intro's twinkling
	 * stars, the tavern fireplace) stepping several times a second — each
	 * re-band + full re-blit takes the better part of a second on an 8MHz
	 * 68000, so honouring them queued unbounded full-frame work (the live
	 * test's freeze). The shadow CLUT still updates; the cycle just doesn't
	 * animate on this target (matching the pre-banding behaviour — reserved
	 * cycle slots are the future fix, see the plan doc). */
	if (count >= 32 || !s_have_pal)
		s_dirty = 1;                     /* re-band at next full present */
}

static const dsp_backend_t ste_backend = {
	"Atari ST/STE (ST low, 16-colour banded)",
	st_init,
	st_shutdown,
	st_surface,
	st_present,
	st_present_rect,
	st_set_palette,
	1,                      /* B4: present ONCE per recompose. The backend
	                         * double-buffers INTERNALLY (present draws the hidden
	                         * page then flips), so the shown page is always freshly
	                         * drawn — no need to seed both pages (that would double
	                         * the c2p). present_rect draws the shown page in place. */
};

const dsp_backend_t *dsp_backend_ste(void)
{
	return &ste_backend;
}
