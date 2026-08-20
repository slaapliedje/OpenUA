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
#include "plat_sys.h"           /* plat_have_blitter (#48) */

#define ST_W        320
#define ST_H        200
#define ST_DEPTH    4                   /* 16 colours */
#define ST_NCOL     (1 << ST_DEPTH)     /* 16 */
#define ST_BITS     4                   /* STE: 4 bits/gun */
#define ST_NBANDS   25                  /* 8 scanlines per band (200/25).
                                         * ★ THIS IS THE RASTER RESOLUTION,
                                         * NOT THE NUMBER OF PALETTES. Bands
                                         * are grouped (see s_ngrp): a group
                                         * holds ONE cut and every band inside
                                         * it loads the same registers, so the
                                         * cost that scales with 25 is the
                                         * Timer B fires, not the re-band.
                                         * 8 rows is the granularity the
                                         * viewport needs — it lives at y=24
                                         * and is 88 tall, and 24, 112 and 200
                                         * are all multiples of 8, so a group
                                         * boundary can land exactly on its
                                         * edges. 20-row bands (the previous
                                         * value) cannot express that, and a
                                         * boundary anywhere else cuts through
                                         * content that spans it: a tile whose
                                         * slots were baked against one group
                                         * renders its lower rows in the next
                                         * group's colours. */
#define ST_RPB      (ST_H / ST_NBANDS)
#define ST_MAXGRP   3                   /* above / viewport rows / below */
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
static short          s_save_pal[16];   /* the desktop's colour registers */
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

/* #63(1): "s_used_idx was just rebuilt FROM THIS FRAME, so the scan cannot
 * find anything." st_reband captures s_used_idx from the very pixels it is
 * about to re-convert, so during the force-full that follows it, every one of
 * the 200 new-ink scans is guaranteed to come up empty — and because it comes
 * up empty, s_dt_new_ink stays 0, the `< 4` gate never closes, and all 200
 * rows pay the full 320-byte scan. Measured at 11.7 s of a 200 s boot spent
 * looking for ink that cannot be there.
 *
 * NOT set by st_patch_new_ink's force-full: that one's scan stopped early at
 * the gate, so unpatched unseen indices may genuinely remain in the frame. */
static short         s_ink_fresh;
/* ADR-0016 B3.2 (stable-slot alignment). A re-band re-runs the median-cut, which
 * normally renumbers the 16 slots arbitrarily. We instead PERMUTE the new slots to
 * best-match the previous palette's positions, so a colour that persists across the
 * re-band keeps its slot number. Purely a slot renumbering — the 16 colours and the
 * final remap are identical, so the displayed frame is unchanged (planes encode a
 * slot, the palette supplies the colour). It is also the groundwork B4's page-flip
 * needs: two pages drawn under differently-numbered slots cannot both be valid.
 *
 * The "smart-skip" this used to feed is GONE (removed 2026-07-26, during #61).
 * It let a re-band pass re-c2p only the rows holding a value whose slot moved,
 * via s_remap_old / s_remap_dirty[] / s_remap_changed. B4 superseded it: a
 * re-band must force-full BOTH pages (see st_reband's comment — the single dirty
 * map is computed against ONE page's previous remap and is simply wrong for the
 * other, which was the "brown chrome"), so s_remap_changed was thereafter only
 * ever assigned 0. The skip branches could not execute and the dirty map was
 * computed every re-band and never read. Dead conservative code is not free
 * here: it advertises a fast path that does not run, which is actively
 * misleading when reading this file to chase a redraw artefact. */
/* Superseded by s_gpal_prev[] / s_grem_prev[] / s_gslot_rep[] — the same three
 * things, one set per palette GROUP (see below). */
/* #63: the index->slot map AS IT STANDS the moment before a re-quant, so the
 * stable-slot alignment can maximise the number of indices that keep their
 * slot. Snapshotted at the top of st_reband rather than maintained at the
 * bottom, so it also captures whatever st_patch_new_ink has since changed. */
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

/* --- PALETTE GROUPS (#139) -----------------------------------------------
 *
 * A group is a run of consecutive raster bands that share one cut. ADR-0016 B1
 * put the whole frame in one group — a single 16-colour palette replicated to
 * every band — because per-band palettes produced visible SEAMS (#40): a flat
 * panel spanning a boundary came out in stripes. That is still true of
 * boundaries chosen by arithmetic. It is NOT true of a boundary placed exactly
 * where the content changes, and the dungeon screen has two such lines: the top
 * and bottom edges of the first-person viewport. Split there and the viewport
 * stops sharing its sixteen slots with the roster text and the granite chrome.
 *
 * Measured with tools/quant/qvp on the captured walk frames, against the true
 * CLUT colour of every pixel (mean squared RGB):
 *
 *                       viewport   whole screen
 *   one group             392.5        155.0
 *   three groups          232.5        107.2      -41% / -31%
 *
 * and the second walk capture agrees (395.7 -> 245.3, 161.0 -> 106.9). Both
 * halves improve: the chrome is not paying for the split, because it was the
 * chrome's own colours crowding the viewport out.
 *
 * s_ngrp == 1 reproduces the old behaviour EXACTLY — one cut, expanded to every
 * band, every band identical, so s_tb_uniform stays 1 and Timer B never arms.
 * That is what makes this switchable at runtime rather than at compile time, and
 * it is also why every screen WITHOUT a viewport is unaffected by the default
 * flip: they were already the s_ngrp == 1 path and still are. */
static short                   s_ngrp = 1;      /* live groups (1 .. ST_MAXGRP) */
static short                   s_grp_b0[ST_MAXGRP + 1]; /* group -> first band  */
static unsigned char           s_gpal[ST_MAXGRP][ST_NCOL * 3];
static unsigned char           s_grem[ST_MAXGRP][256];
static unsigned char           s_gpal_prev[ST_MAXGRP][ST_NCOL * 3];
static unsigned char           s_grem_prev[ST_MAXGRP][256];
static unsigned char           s_gslot_rep[ST_MAXGRP][ST_NCOL];
/* Per-group used-index sets. The GLOBAL s_used_idx stays what it always was
 * (anything on screen anywhere) because the new-ink scan and the repalette
 * guard both read it that way; these are the per-group subsets the slot
 * alignment needs, and they are collected by the same pass. */
static unsigned char           s_gused[ST_MAXGRP][256];
/* ON BY DEFAULT since 2026-08-20. It shipped opt-in for one day while the screen
 * sweep ran: boot, title and the main menu stay at one group and are AE=0 against
 * the pre-split build; the dungeon walk, three town/BIGPIC events and the treasure
 * screen all render correctly at three; and combat measures exactly 16 colours in
 * the game area, i.e. one group, which is what s_vp_active being clear looks like
 * from outside. Marginal cost over the dungeon phase is +2.3% once Timer B fires
 * on group boundaries instead of band boundaries.
 *
 * Runtime, not compile-time, and that stays true now the default has flipped: one
 * binary has to hold both arms or an A/B is comparing two builds, which has misled
 * this port more than once. video.cfg `vpbands=off` is the escape hatch — and it is
 * the one to reach for first if a REAL ST ever disagrees with Hatari about the
 * one-line-early fire and the spin, which is the part of this no emulator can
 * settle. */
short                          st_vp_bands = 1;

short  st_band_stpal[ST_NBANDS + 1][ST_NCOL];   /* ST-format, +sentinel  */

/* --- Timer B fires on GROUP boundaries, not band boundaries ---------------
 *
 * ★ THE COST OF THE SPLIT WAS THE INTERRUPT, NOT THE HANDLER. The raster
 * resolution is 8 rows because the viewport's edges have to land on a boundary
 * (y=24, 88 tall), but only s_ngrp-1 of the 25 boundaries are a real palette
 * change. Firing at all 25 cost ~800 cycles apiece — 12% of the machine to
 * install the same sixteen colours 23 extra times — and a fast exit for the
 * unchanged ones was written and MEASURED at 807 cycles/fire against 771 for
 * the full handler: entry and exit are the whole bill at that cadence, so a
 * cheaper handler is not the lever. Fewer fires is.
 *
 * So the timer is reprogrammed per fire. MFP68901 event-count mode: a write to
 * TBDR while the timer RUNS sets the data register only — the counter keeps
 * the value it reloaded at the last underflow — so a written interval always
 * takes effect one fire later. Each descriptor therefore carries both halves:
 *
 *   expect  the counter's reload while THIS fire runs. The spin waits for the
 *           counter to drop below it, which is how the boundary line's display
 *           ending is detected; it used to be a constant (ST_RPB) and cannot be
 *           now.
 *   next    what to program into TBDR during this fire, i.e. the interval that
 *           will run after the NEXT one.
 *
 * The last live fire programs 255, which cannot underflow again inside 200
 * display lines, so the frame ends with no further interrupts and the VBL
 * re-phases from scratch. A trailing sentinel makes a stray fire harmless. */
struct st_tb_fire {
	short         pal[ST_NCOL];     /* 32 bytes: moveml straight to 0xFF8240 */
	unsigned char expect;
	unsigned char next;
};
/* The handler walks these with one postincrementing pointer, so the struct's
 * size IS the stride — it must stay at 34. */
typedef char st_tb_fire_is_34[(sizeof(struct st_tb_fire) == 34) ? 1 : -1];
struct st_tb_fire  st_tb_tab[ST_MAXGRP + 2];    /* live fires + sentinels   */
struct st_tb_fire *st_tb_ptr;                   /* the handler's cursor     */
static short       st_tb_nfire;                 /* fires per frame (ngrp-1) */
static unsigned char st_tb_first;               /* VBL: counter, one early  */
static unsigned char st_tb_data0;               /* VBL: the first reload    */
/* ★ THE COST OF THE SPLIT IS THE INTERRUPT, NOT THE HANDLER. Only two of the
 * 25 band boundaries are a real palette change; the rest re-load registers that
 * already hold those values. A fast exit for them was written and MEASURED — a
 * flag array tested before the d0-d7 save, so an unchanged band cost a test, a
 * pointer bump and an rte — and st_prof_tbcost reported 807 cycles a fire
 * against 771 for the full handler. No change. Entry and exit are the whole
 * bill at this cadence, so the only lever is FEWER FIRES: a variable TBDR that
 * reloads with each group's height and interrupts twice a frame instead of
 * twenty-five times. That is the open item, and it is in the handler's spin
 * test (it compares against a constant reload), which is the one place in this
 * file with a documented deadlock (#91). The fast-exit version was reverted
 * rather than shipped: complexity in an IPL-6 handler for a measured zero. */
static short                   s_dirty;
static short                   s_have_pal;
static short                   s_vbl_slot = -1;

#ifdef FRUA_STPROF
/* B3.0a: st_blit_full sets this to record whether the LAST full present took the
 * force-full path (every LUT moved → all 200 rows) or the row-diff path. Declared
 * here because st_blit_full is above the main FRUA_STPROF block. */
static short                   sp_forced_flag;
/* Same reason as sp_forced_flag: st_reband is above the main FRUA_STPROF block. */
static long                    sp_rb_noskew;   /* re-bands that moved no used index */
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
/* ADR-0016 B5: the viewport's planes as the engine stamped them, in PAGE layout
 * (same interleaved form and pitch as a screen page, absolute screen coords) so
 * the composite is a copy rather than a conversion. VP_MAX rows is enough for a
 * viewport whose bottom edge is inside VP_MAX, which st_vp_commit enforces. */
static unsigned char s_vp_ilv[(long)LINE_BYTES * VP_MAX];
static short         s_vp_planar;               /* s_vp_ilv holds this frame  */
/* ★ THE A/B KNOB, and it is RUNTIME so both arms are one binary. 0 makes
 * st_vp_planes() hand back NULL, the engine keeps to the chunky scratch, and the
 * composite converts exactly as it did before — the old path, same build. */
short st_planar_viewport = 1;
static short         s_vp_x, s_vp_y, s_vp_w, s_vp_h;
static short         s_vp_active;               /* a committed rect awaits composite */
/* ★ #61: WHICH PAGES STILL OWE THE COMPOSITE.
 *
 * st_vp_composite was one-shot per commit — and there are NPAGES pages. A full
 * present builds the BACK page from s_chunky, whose viewport rows are frozen
 * stale by design (ADR-0016 B2.1: the 3D view renders into the planar scratch,
 * never into chunky), then composites and flips. The NEXT full present targets
 * the OTHER page, rebuilds it from that same stale chunky, and finds the
 * composite already spent — so it flips in a page showing the PREVIOUS 3D
 * frame. Observed on the HEIRS door as the floor band and the stars reverting
 * for one frame and coming back (#61, reproduced 2026-07-28).
 *
 * Per-page rather than the `s_force_full` count idiom next door, because
 * st_present_rect composites the SHOWN page without flipping and would
 * otherwise consume the other page's credit. */
static unsigned char s_vp_owe[NPAGES];          /* pages still owing it       */
static short         s_vp_have;                 /* scratch holds a valid rect */

/* ★ REBUILDING A ROW FROM s_chunky ERASES THE VIEWPORT'S SHARE OF IT (#148).
 *
 * The viewport deliberately does NOT live in s_chunky (ADR-0016 B2) — that is
 * what keeps the roster and chrome on its scanlines static so the row-diff can
 * skip them — so s_chunky's viewport hole is BLACK (measured: 0 of 88 columns
 * non-zero against 88 of 88 in the scratch). Both row writers below convert a
 * WHOLE 320-pixel row, so rebuilding one that crosses the viewport paints 88
 * columns of black straight through it.
 *
 * A row is only rebuilt when it CHANGES, which is why the damage is a band and
 * not the whole pane: rows 24..102 carry static chrome beside a constant hole
 * and never rebuild, while the compass and the coordinate/clock box sit at rows
 * ~103..125 and the clock ticks. Nine black rows across the bottom of the 3D
 * view, stopping exactly where the viewport ends at row 111, because rows 112+
 * have no viewport to erase. It shipped, and it was in every affected walk frame.
 *
 * Nothing put it back because st_vp_composite returns unless the page OWES a
 * composite, and #90 clears both owes at the end of every one — correct on its
 * own terms (the composite really does write both pages) but it assumes nothing
 * else overwrites the viewport afterwards. So re-arm the owe here and the
 * st_vp_composite() that already follows every present repaints it.
 *
 * s_vp_have, NOT s_vp_active: the scratch and the stamped planes both persist
 * after the commit is consumed, which is what makes the repaint possible.
 *
 * ★ AND IT HAS TO SIT IN BOTH WRITERS. The first attempt at this hooked only
 * st_blit_rows and measured NO EFFECT, which read as a refutation and was not:
 * CPU68K=68000 implies FRUA_PLANAR, so the ST's full present goes through
 * st_dt_present_full/st_dt_build_row and never calls st_blit_rows at all. The
 * hook was on a path the walk does not take. */
#ifdef FRUA_STPROF
/* Same reason as sp_forced_flag above: st_vp_touched sits ABOVE the main
 * FRUA_STPROF block, so its counter has to be declared here. */
static long sp_vp_rearm;                  /* #148 viewport repaints forced */
#endif

/* Runtime, like st_vp_bands and st_planar_viewport and for the same reason: one
 * binary has to carry both arms or an A/B compares two builds. `vprepair=off`
 * restores the shipped behaviour — nine black rows at the bottom of the 3D view
 * — which is what makes the control runnable at all. */
short st_vp_repair = 1;

static void st_vp_touched(short y0, short h)
{
	if (st_vp_repair && s_vp_have && s_vp_h > 0
	    && y0 < (short)(s_vp_y + s_vp_h) && (short)(y0 + h) > s_vp_y) {
		s_vp_owe[0] = 1;
		s_vp_owe[1] = 1;
#ifdef FRUA_STPROF
		sp_vp_rearm++;
#endif
	}
}

static short         s_st_active;               /* this backend is the live one */
static unsigned char *st_vp_scratch(short *pitch);
static void           st_vp_commit(short x, short y, short w, short h);
static void           st_vp_overwrite(short x, short y, short w, short h);
static short s_vp_chunky_ok;            /* chunky scratch refreshed this frame */
/* ★ THE PLANES BAKE SLOT NUMBERS, SO A REMAP CHANGE INVALIDATES THEM. This is
 * the B5 hazard, found live: the viewport's planes carry slot indices, and when
 * st_reband re-quantises or st_patch_new_ink re-points an index, those same slot
 * numbers now mean DIFFERENT colours. Re-copying them paints the walls in
 * whatever the slots became — cyan/red freckles over grey stone, against clean
 * stone on the Falcon and on this same binary with vpplanar=off.
 *
 * Before B5 nothing had to track this: the composite converted from the chunky
 * scratch through the CURRENT remap every present, so a palette change fixed
 * itself. The copy path has no such self-correction, so the generation is now
 * explicit — bumped wherever the remap moves, recorded when planes are
 * committed, and compared before trusting them. */
static unsigned long s_remap_gen;       /* bumped on every remap change      */
static unsigned long s_vp_gen;          /* generation the planes were stamped for */
static long          s_vp_gen_stale;    /* composites that fell back to c2p   */
static long          s_vp_copy_n;       /* composites that used the planes    */
static long  s_rb_stale;                /* re-bands that quantised a STALE one */
static long  s_rb_seen;                 /* re-bands that reached that decision */

/* ★ OVER-PREDICT ON PURPOSE. s_dirty alone is not the re-band condition — the
 * present also skips when the CLUT matches the one already banded — so this says
 * "pending" more often than a re-band actually fires. That costs an unnecessary
 * chunky pass; the opposite error costs a palette derived from last frame's
 * walls. Wrong in the cheap direction by construction. */
static int st_reband_pending(void)
{
	return (s_dirty || !s_banded_valid || !s_have_pal) ? 1 : 0;
}

/* ★ AND COUNT WHETHER THE PASS IS ACTUALLY BEING SKIPPED. "AE = 0 and no stale
 * re-band" is exactly what a prediction that ALWAYS says pending would report —
 * correct picture, zero staleness, and no saving whatever. Without this the only
 * evidence of laziness is an inferred profile delta. Logged every 512 frames so
 * the trace cost is nil. */
static long s_lazy_skip, s_lazy_draw;

static void st_vp_chunky_valid(short valid)
{
	s_vp_chunky_ok = valid;
	if (valid)
		s_lazy_draw++;
	else
		s_lazy_skip++;
	/* ★ NOT PER RENDER. dbg_log writes through Cconws, which paints into screen
	 * memory on this machine, so a per-frame line corrupts the display AND skews
	 * the timing it is meant to measure. Every 256th render is enough to see the
	 * ratio; the stale case says so the moment it happens, which is the part
	 * that cannot wait. Measured 2026-08-18 over a fixture walk: 6 skipped, 2
	 * drawn, 2 re-bands seen, 0 stale. */
	if (((s_lazy_skip + s_lazy_draw) & 255) == 0) {
		dbg_log_num("ste: lazy chunky skipped = ", s_lazy_skip);
		dbg_log_num("ste: lazy chunky drawn   = ", s_lazy_draw);
		dbg_log_num("ste: reband-on-stale     = ", s_rb_stale);
		dbg_log_num("ste: planes-stale c2p   = ", s_vp_gen_stale);
	}
}

static unsigned char *st_vp_planes_buf(short *pitch);
static void           st_vp_commit_planes(short x, short y, short w, short h);
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

/* MFP interrupt registers. Timer B is MFP channel 8 = BIT 0 of the *A* set
 * (Compendium B.38, p.758: IERA 0xFFFA06, IPRA 0xFFFA0A, ISRA 0xFFFA0E — the
 * byte lives at the odd address). Writing IPRA/ISRA clears only the bits
 * written as ZERO, hence the 0xFE masks. */
#ifdef FRUA_STPROF
volatile long g_tb_fires;       /* #61: band fires in the CURRENT frame (asm) */
static long sp_frames, sp_starved, sp_fires_lost, sp_fires_min = 99;
static long sp_tb_total;        /* #63: band fires since boot (bench deltas) */

/* #63 PLAY-LOOP profile. Everything measured on this target so far has been
 * the boot or a menu — st_prof_hot_dump's window is full presents, and a
 * dungeon walk issues RECT presents, so the walk has literally never been
 * timed. These count the two things the walk actually runs: the viewport
 * composite (chunky scratch -> planes -> both pages) and the rect present.
 * Wall ticks alongside them give the share: if composite+rect is a small
 * fraction of the wall, the 8 MHz ceiling is the ENGINE's 3D render and no
 * amount of c2p tuning will move it.
 *
 * 200 Hz (5 ms) rather than TickCount's 60 Hz — a walk step is a few tens of
 * ms and 60 Hz would quantise most of it away. The Supexec is a trap, but it
 * is two per composite against a body that touches ~7700 pixels. */
static long st_prof_hz200(void)
{
	return *(volatile long *)0x4BAUL;
}
static long sp_vp_n, sp_vp_t, sp_rect_n, sp_rect_t, sp_play_t0 = -1;
static long sp_vp_conv, sp_vp_blit;     /* composite split: c2p vs plane blit */
static long sp_vp_flat, sp_vp_tex, sp_vp_col8; /* fast-composite span census:
                                         * flat 32-blocks (fills), textured
                                         * 32-blocks (walls), 8px edge columns.
                                         * Zero-timing: the ratio + the known
                                         * c2p4st_32_flat vs _32 cost gap sizes
                                         * the Stage-A(fills) vs C(walls) win. */

/* #63 FULL-PRESENT phase split. The HEIRS drive put 32.5% of all play time
 * inside st_present — ~1008 presents at ~1.6 s each — which does not square
 * with #90's finding that post-menu presents convert ZERO rows, since a
 * full-frame c2p is only 1.21 s. Either the conversions are back, or a
 * present that converts nothing is still doing 1.6 s of something. These name
 * which. Coarse on purpose: five Supexec pairs per present, not per row. */
static long sp_ph_band, sp_ph_pass1, sp_ph_copy, sp_ph_n;
static long sp_ph_conv_rows, sp_ph_chg_rows;
/* #63: pass 1 is still the biggest phase after the cursor fix. Is that the
 * remaining scan, or the fixed cost around it? Count the rows actually diffed
 * and time the gather separately — pass1 minus gather, over scanned rows, is
 * the real per-row price. */
static long sp_ph_scanned, sp_ph_gather;
/* #63 PASS-1 ATTRIBUTION (the residual hunt). pass 1 is 31.7 t200 a present
 * with no dominant component: ~41 scanned rows, ~10 changed, 0.8 built do not
 * add up to it under any per-row constant, and the shortfall looked like ~2x.
 *
 * The obvious instrument — a Supexec pair around each phase INSIDE ready_row —
 * costs ~20k traps a drive and distorts what it measures by ~8%. These do the
 * same job for a few adds per present: count the WORK EXACTLY (words compared,
 * ink bytes scanned, rows built), then price each unit ONCE in a calibration
 * bench. Legitimate here because the 68000 has no cache: a bench loop over the
 * real buffers runs the same cycles the present does. */
static long sp_ph_cmpwords;     /* longs actually compared (early exit aware) */
static long sp_ph_inkbytes;     /* bytes the new-ink scan actually read       */
static long sp_ph_built;        /* st_dt_build_row calls                      */
/* ...and the PASS-1 SHARE of each. The raw counters above are since-boot over
 * every phase, and the force-full branch runs the same primitives 200 rows at
 * a time OUTSIDE the pass-1 timer: force-fulls contributed 9000 of the first
 * run's 9776 build_row calls, so subtracting the raw figure from pass1 would
 * have charged pass 1 for twelve times the conversions it actually did.
 * Accumulated as a delta across the timed region, three subtracts a present. */
static long sp_p1_cmpwords, sp_p1_inkbytes, sp_p1_built;
/* #63 REBAND SPLIT. The boot's first 16 presents spend 90.7 s inside
 * st_present against 11 rebands — `band` alone is 36.7 s, ~3.3 s per reband,
 * for what is nominally ONE median cut over at most 256 colours. That is far
 * too much for the cut itself, so the cost must be in the full-frame passes
 * around it: quant_banded's own histogram, the separate 64000-iteration
 * s_used_idx capture right after it, and the viewport overlay memcpy. Time
 * them apart before touching any of them. */
static long sp_rb_vpcopy, sp_rb_quant, sp_rb_used, sp_rb_align, sp_rb_ffull;
static long sp_pc_hit, sp_pc_miss;        /* #139 palette cache        */
static long sp_rb_ffrows, sp_rb_ffcopy;   /* force-full: builds vs the 192 KB */
static long sp_rb_n;
static long sp_cal_cmp, sp_cal_ink, sp_cal_bld, sp_cal_loop;  /* bench t200   */
static short sp_ph1cal_done;
#endif

#define IERA (*(volatile unsigned char *)0xFFFFFA07UL)
#define IPRA (*(volatile unsigned char *)0xFFFFFA0BUL)
#define TB_BIT       0x01
#define TB_CLR_MASK  0xFE

/* ★ #63: THE SPLIT IS ONLY ARMED WHEN THE BANDS ACTUALLY DIFFER.
 *
 * The raster split exists to give each band its own 16 colours. Strategy B
 * (B1/B4 Phase-0) made the remap SCENE-STABLE, and the way it did that was to
 * quantize ONE palette for the whole frame and replicate it to every band —
 * see st_build_hw_palette. So on this build every band's palette is identical
 * by construction, and Timer B loads ten times a frame the colours the VBL
 * already loaded. That is not free: ~500 interrupts a second, each saving nine
 * registers, pre-loading eight, and then SPINNING to the end of a display line
 * so the store lands in the border (~500 cycles = a whole scanline, worst
 * case). Measured against a fixed c2p workload with st_prof_tbcost().
 *
 * The check is at RUNTIME, over the encoded hardware palettes, rather than a
 * "this build has one palette" #ifdef, because it cannot rot: restore a
 * per-band quantizer and the bands stop matching, the flag clears and the
 * split comes back on its own. s_tb_live tracks whether the hardware is
 * actually running so the disarm happens exactly once, in the VBL, which is
 * already supervisor. */
static short s_tb_uniform = 1;  /* band palettes all equal -> split is idle   */
static short s_tb_live;         /* Timer B is currently armed in hardware     */
#ifdef FRUA_STPROF
static short s_tb_force = -1;   /* bench override: -1 none, 0 force off, 1 on */
#endif

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
 * the live test.
 *
 * ★ THE RE-PHASE MUST BE UNINTERRUPTIBLE BY TIMER B ITSELF (task #91). This
 * handler runs at IPL 4, so the level-6 Timer-B ISR can preempt it — and if it
 * lands in the window where TBCR is 0, the machine DEADLOCKS: the ISR spins on
 * TBDR waiting for the display line to end, TBDR only moves while the timer is
 * counting, and the timer is stopped by the very handler the ISR just
 * preempted. Nothing at IPL 4 can run to restart it, so the boot hangs on the
 * title screen with interrupts apparently still alive. Masking Timer B in IERA
 * across the window closes it. (Disabling an MFP channel also clears its
 * pending bit, so at most one band fire is lost — one frame of one band's
 * palette arriving late, versus a hard hang.)
 *
 * The trigger that made this fire ~40% of boots was #48's BLiTTER copies: a
 * force-full seeds 2 pages x (32000 plane + 64000 shadow) bytes in HOG mode,
 * ~24 ms during which the CPU cannot service ANY interrupt, so the VBL and a
 * Timer-B request come due together and land in exactly this window. */
void st_vbl_handler(void)
{
	volatile short *hw = ST_COLORREGS;
	short i;
	short arm = !s_tb_uniform;      /* #63: nothing to switch -> do not arm */

#ifdef FRUA_STPROF
	if (s_tb_force >= 0)
		arm = s_tb_force;
#endif
	if (arm) {
		IERA &= (unsigned char)~TB_BIT; /* Timer B cannot preempt the re-phase  */
		TBCR = 0;                       /* stop: the writes must not race       */
		TBDR = st_tb_first;             /* first fire ONE LINE EARLY. Stopped,
		                                 * so this loads the COUNTER too        */
		TBCR = 8;                       /* event-count mode, re-armed in phase  */
		TBDR = st_tb_data0;             /* reload for the fire AFTER the first
		                                 * (the MFP only picks a write up at the
		                                 * next underflow, so the -1 folded into
		                                 * st_tb_first stands for the first)     */
		IPRA = TB_CLR_MASK;             /* drop anything latched while masked   */
		IERA |= TB_BIT;
		s_tb_live = 1;
	} else if (s_tb_live) {                 /* #63: armed -> idle, stop it once */
		IERA &= (unsigned char)~TB_BIT;
		TBCR = 0;
		IPRA = TB_CLR_MASK;
		s_tb_live = 0;
	}

#ifdef FRUA_STPROF
	{	/* #61: how many band palettes did the frame just past actually get?
		 * A frame starved of interrupts renders its lower bands with a stale
		 * palette — the artefact, counted rather than eyeballed. */
		long f = g_tb_fires;

		g_tb_fires = 0;
		sp_tb_total += f;               /* #63: cumulative, for the A/B bench */
		if (sp_frames > 0) {            /* frame 0 is partial by construction */
			if (f < st_tb_nfire) {
				sp_starved++;
				sp_fires_lost += (st_tb_nfire - f);
				if (f < sp_fires_min) sp_fires_min = f;
			}
		}
		sp_frames++;
	}
#endif
	for (i = 0; i < ST_NCOL; i++)
		hw[i] = st_band_stpal[0][i];
	st_tb_ptr = &st_tb_tab[0];
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
 * bits of the written mask, hence 0xFE.
 *
 * ★ THE SPIN IS BOUNDED BY A LIVENESS TEST, NOT A COUNT (task #91). TBDR only
 * moves while the timer is COUNTING, so if this ISR is ever entered with the
 * timer stopped (TBCR == 0) the condition can never become false and the
 * machine deadlocks at IPL 6 — no IPL-4 code can run to restart it. The VBL's
 * re-phase window is the one place that stops the timer, and it now masks
 * Timer B for exactly that window, so this test should never fire. It stays
 * anyway: an unbounded spin inside an interrupt handler is a landmine, and
 * teardown paths stop the timer too. Falling through just stores the palette a
 * line early — a cosmetic band offset, which is the correct thing to prefer
 * over a hang. */
/* #61: count band fires per frame so interrupt STARVATION is measurable
 * directly, instead of inferred from pixels. One addq per band; STPROF only. */
#ifdef FRUA_STPROF
#define TB_COUNT_INSN "  addql #1,_g_tb_fires\n"
#else
#define TB_COUNT_INSN ""
#endif

__asm__(
	".globl _st_timerb_trampoline\n"
	"_st_timerb_trampoline:\n"
	TB_COUNT_INSN
	"  moveml %d0-%d7/%a0-%a1,%sp@-\n"
	"  movel  _st_tb_ptr,%a0\n"
	/* ★ THE PALETTE GOES INTO d0-d6 AND a1, NOT d0-d7. Eight registers is
	 * still 32 bytes, and it buys d7 as a scratch for the spin's compare
	 * value — which is per-fire now, so it cannot be an immediate any more.
	 * moveml's register order is fixed, so the load and the store agree. */
	"  moveml %a0@+,%d0-%d6/%a1\n"  /* palette; a0 -> expect               */
	"  moveb  %a0@+,%d7\n"          /* the counter's reload for this fire  */
	"1:\n"
	"  cmpb   0xFFFFFA21,%d7\n"     /* TBDR still at that reload?          */
	"  jne    2f\n"                 /* no: the line ended — store now      */
	"  tstb   0xFFFFFA1B\n"         /* TBCR == 0 -> timer STOPPED, TBDR is */
	"  jne    1b\n"                 /*   frozen: spinning would deadlock   */
	"2:\n"
	"  moveml %d0-%d6/%a1,0xFFFF8240\n"
	"  moveb  %a0@+,0xFFFFFA21\n"   /* program the interval after the next */
	"  movel  %a0,_st_tb_ptr\n"
	"  moveml %sp@+,%d0-%d7/%a0-%a1\n"
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

#ifdef FRUA_STPROF
/* #41 hot-row attribution: span conversions per row across BOTH present paths
 * (rect c2p here + the diffed s_dt rebuilds; force-fulls excluded — an epoch
 * wipe converts everything and indicts no writer). Dumped + reset every 64
 * full presents as b4hot lines (y*10000+count), with a why bitmask (bit 0:
 * rowcov short, bit 1: idx mismatch) + first mismatching x per row. */
static unsigned short s_prof_convrow[ST_H];
static unsigned char  s_prof_convwhy[ST_H];
static short          s_prof_mmx[ST_H];
#endif

static void st_vp_touched(short y0, short h);

static void st_blit_rows(short x0, short w, short y0, short h)
{
	short y;

	st_vp_touched(y0, h);

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
		s_prof_convrow[yy]++;
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

	/* B4: a re-band leaves BOTH pages' planes stale, so s_force_full's branch
	 * services every page in this one present. The row-diff runs against THIS
	 * page's shadow (s_shadow = s_shadow_pg[s_back]), so each page independently
	 * tracks to the current chunky frame. */
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
		return;
	}
#ifdef FRUA_STPROF
	sp_forced_flag = 0;
#endif
	for (y = 0; y < ST_H; y++) {
		const unsigned char *crow = s_chunky + (long)y * ST_W;

		if (memcmp(crow, s_shadow + (long)y * ST_W, ST_W) != 0)
			st_blit_rows(0, ST_W, y, 1);
	}
}

/* Squared RGB distance between two packed 3-byte colours (for slot alignment). */
static long st_coldist(const unsigned char *a, const unsigned char *b)
{
	long dr = (long)a[0] - b[0];
	long dg = (long)a[1] - b[1];
	long db = (long)a[2] - b[2];

	return dr * dr + dg * dg + db * db;
}

/* Whole-buffer long-wise compare, for the reband path's "did anything move
 * since the last present?" test (#63). Same primitive as st_row_differs below,
 * but it must live ABOVE the FRUA_PLANAR block: st_present calls it on EVERY
 * build, and defining it next to its sibling broke the 020 Falcon target.
 * Both buffers are Mxalloc-based and the length is a multiple of 4. */
static int st_buf_differs(const unsigned char *a, const unsigned char *b,
                          long nbytes)
{
	const long *p = (const long *)a;
	const long *q = (const long *)b;
	long w, n = nbytes / 4;

	for (w = 0; w < n; w++)
		if (p[w] != q[w])
			return 1;
	return 0;
}

/* Decide the group layout for the re-band about to run. Boundaries are BAND
 * indices, so they are multiples of ST_RPB by construction and can never fall
 * mid-band.
 *
 * Three groups only when a viewport is actually committed: on a menu or a
 * full-screen picture there is no content line to split on, and an arbitrary
 * split is the #40 seam all over again. Degenerate rects (a viewport touching
 * the top or bottom of the screen) fall back to one group rather than
 * producing an empty group — quant_banded on zero rows has nothing to cut. */
static void st_group_layout(void)
{
	short t, b;

	s_ngrp     = 1;
	s_grp_b0[0] = 0;
	s_grp_b0[1] = ST_NBANDS;
	if (!st_vp_bands || !s_vp_active || s_vp_h <= 0)
		return;
	t = (short)(s_vp_y / ST_RPB);                       /* first viewport band */
	b = (short)((s_vp_y + s_vp_h + ST_RPB - 1) / ST_RPB); /* one past the last */
	if (t <= 0 || b >= ST_NBANDS || b <= t)
		return;
	s_ngrp     = 3;
	s_grp_b0[0] = 0;
	s_grp_b0[1] = t;
	s_grp_b0[2] = b;
	s_grp_b0[3] = ST_NBANDS;
}

/* Fan the groups out into the per-band arrays the rest of the backend reads:
 * the c2p picks its LUT with band = y * ST_NBANDS / ST_H, and so does every
 * draw-time writer in the engine (dsp_planar_remap). Neither knows about
 * groups, and neither needs to — every band inside a group holds an identical
 * copy, so a tile that straddles a band boundary WITHIN a group still bakes
 * the right slots. Only a group boundary is a real edge, and those sit on the
 * viewport's own edges. */
static void st_expand_groups(void)
{
	short g, b;

	for (g = 0; g < s_ngrp; g++)
		for (b = s_grp_b0[g]; b < s_grp_b0[g + 1]; b++) {
			memcpy(s_band_pal + (long)b * ST_NCOL * 3, s_gpal[g],
			       (size_t)(ST_NCOL * 3));
			memcpy(s_band_remap + (long)b * 256, s_grem[g], 256);
		}
}

/* Fan the groups out (st_expand_groups) and encode the per-band ST-format
 * hardware palettes (STE gun encoding: nibble = (v0 << 3) | (v >> 1)), plus the
 * sentinel row (see st_band_stpal) and the CLUT snapshot the reband-skip guard
 * compares against. The GROUP remaps are not rebuilt here — only st_reband and
 * st_patch_new_ink change those; a palette-only refresh (st_repalette) reuses
 * them and just re-encodes the RGB. Shared by st_reband and st_repalette. */
static void st_build_hw_palette(void)
{
	short b, i;

	st_expand_groups();             /* groups -> the per-band arrays */
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
	{	/* #63: does the split have anything to do this scene? With a single
		 * group the answer is always no, and Timer B is 1250 interrupts a
		 * second of pure overhead, so it is not armed at all.
		 *
		 * With three groups only two of the twenty-five boundaries are a
		 * real change, but the handler still fires at all of them — see the
		 * st_band_chg note above for why a fast exit did not help. */
		short uniform = 1;

		for (b = 1; b < ST_NBANDS && uniform; b++)
			for (i = 0; i < ST_NCOL; i++)
				if (st_band_stpal[b][i] != st_band_stpal[0][i]) {
					uniform = 0;
					break;
				}
		s_tb_uniform = uniform;

		{	/* The schedule itself: one fire per GROUP boundary, in
			 * scanlines. st_tb_first folds in the "one line early" that
			 * lets the handler spin for the boundary line to end, and
			 * st_tb_data0 is the reload the FIRST fire will find — the MFP
			 * takes a running write one underflow later, so the value a
			 * fire needs was always programmed by its predecessor. */
			short k, nf = (short)(uniform ? 0 : s_ngrp - 1);
			short y[ST_MAXGRP];
			unsigned char data;

			for (k = 0; k < nf; k++)
				y[k] = (short)(s_grp_b0[k + 1] * ST_RPB);
			st_tb_nfire = nf;
			st_tb_first = (unsigned char)(nf > 0 ? y[0] - 1 : 255);
			data        = (unsigned char)(nf > 1 ? y[1] - y[0] : 255);
			st_tb_data0 = data;
			for (k = 0; k < nf; k++) {
				unsigned char nd = (unsigned char)
				    ((k + 2 < nf) ? (y[k + 2] - y[k + 1]) : 255);

				memcpy(st_tb_tab[k].pal,
				       st_band_stpal[s_grp_b0[k + 1]],
				       (size_t)(ST_NCOL * 2));
				st_tb_tab[k].expect = data;
				st_tb_tab[k].next   = nd;
				data = nd;
			}
			/* Sentinels. A fire that arrives after the schedule is spent —
			 * a request latched while masked, serviced late (#91) — installs
			 * the last group's colours again and asks for an interval that
			 * cannot underflow inside 200 display lines, instead of walking
			 * off the table. */
			for (k = nf; k < ST_MAXGRP + 2; k++) {
				memcpy(st_tb_tab[k].pal,
				       st_band_stpal[nf > 0 ? s_grp_b0[nf] : 0],
				       (size_t)(ST_NCOL * 2));
				st_tb_tab[k].expect = 255;
				st_tb_tab[k].next   = 255;
			}
		}
	}
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
	short g, s, i;

	for (g = 0; g < s_ngrp; g++)
	for (s = 0; s < ST_NCOL; s++) {
		long  bestd = 0x7fffffffL;
		short bi = 0;

		for (i = 0; i < 256; i++) {
			long d;
			if (s_grem[g][i] != s)
				continue;
			d = st_coldist(s_clut + (long)i * 3, s_gpal[g] + (long)s * 3);
			if (d < bestd) { bestd = d; bi = i; }
		}
		s_gslot_rep[g][s] = (unsigned char)bi;
	}
}

/* B4 Phase-0: a palette-only refresh for a within-scene palette change (surface
 * content unchanged). Keep the fixed index->slot remap — so the on-screen planes
 * stay valid — and rebuild only the slot->RGB hardware palette from the NEW CLUT
 * via the captured representative indices. No re-quant, no re-c2p: st_blit_full's
 * row-diff then finds nothing changed and the raster split loads the new colours.
 * This is the "reband = palette-register-only" invariant Strategy B is built on. */
#ifdef FRUA_PLANAR
/* --- draw-time plane target buffers (ADR-0016 B4) ------------------------
 *
 * The writer-by-writer transition off the chunky+c2p path. Converted Toolbox/
 * engine writers stamp their pixels straight into s_dt (through the same per-band
 * remap the c2p uses) in parallel with their existing chunky store. s_dt_cov marks
 * which screen pixels a converted writer owns and s_dt_idx the index it laid, so
 * the present can bridge the rest from the c2p and detect unconverted overwrites.
 * Compiled out of the shipping build. (Definitions of st_dt_target / the self-
 * check live just above st_init; these are up here so the reband path can reset
 * the epoch.) */
static unsigned char *s_dt;             /* draw-time plane accumulation buffer */
static unsigned char *s_dt_cov;         /* ST_W*ST_H: 1 where a writer drew   */
static unsigned char *s_dt_idx;         /* ST_W*ST_H: the index it laid there  */
static short         *s_dt_rowcov;      /* ST_H: covered-pixel count per row   */

/* Reset the draw-time coverage epoch: a re-band renumbers the palette slots, so
 * any plane bits a converted writer stamped under the PREVIOUS remap are stale.
 * Clear coverage so only pixels drawn under the current remap are trusted (the
 * rest fall to the c2p bridge). Called from st_reband / st_repalette. */
#ifdef FRUA_PLANAR_DIAG
static long s_dt_checks;                /* selfcheck budget (see st_dt_selfcheck) */
static long s_dt_epoch;                 /* epoch serial, for log correlation      */
#endif

static void st_dt_epoch_reset(void)
{
	if (s_dt_cov)
		memset(s_dt_cov, 0, (size_t)ST_W * ST_H);
	if (s_dt_rowcov)
		memset(s_dt_rowcov, 0, ST_H * sizeof(short));
#ifdef FRUA_PLANAR_DIAG
	/* Re-arm the selfcheck: each epoch (scene/palette change) gets audited,
	 * so the DUNGEON frames are checked too — the flip's original "dungeon
	 * breakage" verdict shipped unaudited because the menu consumed a global
	 * budget. */
	s_dt_checks = 0;
	s_dt_epoch++;
#endif
}
#endif

/* CLUT indices the last re-quant actually saw in the frame (captured over the
 * reband's own quant source, incl. the wall-pin overlay). Only these can prove
 * a merged slot: absent indices ride the luma fallback and must not veto the
 * repalette fast path (Phase-0's whole win). */
static unsigned char s_used_idx[256];

/* A content-unchanged palette load may still SPLIT two indices the last quant
 * MERGED into one slot: their RGBs matched then (e.g. the transient
 * clut[text]==clut[panel] grey during a recompose — the grey-on-grey HUD-text
 * family, jt1089), so median-cut folded them together — and a slot->RGB reload
 * (st_repalette) can never un-merge them, leaving the text invisible no matter
 * which colour the slot reloads to. Detect it: two USED indices sharing a slot
 * whose NEW CLUT colours diverge means the merge is stale -> take the full
 * re-quant instead. Threshold 512 (squared RGB): the documented collapse case
 * is a 28-unit red gap (784), comfortably above; genuine same-colour aliases
 * sit near 0. */
static int st_remap_split(void)
{
	short anchor[ST_NCOL];
	short g, i, s;

	/* Per GROUP: a merge only matters where both indices are actually drawn,
	 * and two groups can legitimately fold different pairs together. */
	for (g = 0; g < s_ngrp; g++) {
		for (s = 0; s < ST_NCOL; s++)
			anchor[s] = -1;
		for (i = 0; i < 256; i++) {
			if (!s_gused[g][i])
				continue;
			s = s_grem[g][i];
			if (anchor[s] < 0) {
				anchor[s] = i;
				continue;
			}
			if (st_coldist(s_clut + (long)i * 3,
			               s_clut + (long)anchor[s] * 3) > 512)
				return 1;
		}
	}
	return 0;
}

/* ★ COLOUR REGISTER 0 IS THE BORDER, AND WITH A RASTER SPLIT EVERY GROUP GETS A
 * TURN AT IT. Each group's register 0 is displayed in the border on that
 * group's own scanlines, so three independent cuts put three different colours
 * there and the border grows a horizontal band exactly as tall as the viewport.
 * It is outside the 320x200 image, which is why it survives every check that
 * crops to the image — this one was found by looking at a full screenshot.
 *
 * The alignment already claims position 0 by colour within each group, which
 * keeps it stable over time but does not make the groups AGREE. So: pick, in
 * each later group, the slot nearest group 0's slot 0, permute it into position
 * 0 (palette and remap together, so no pixel changes meaning), and then set its
 * RGB to group 0's exactly. The permutation is free; the assignment costs a
 * little fidelity for whatever indices live in that slot, and buys a border
 * that is one colour.
 *
 * ★ may_permute IS NOT A TUNING KNOB. st_repalette's whole premise is that the
 * slot numbering does not move, so the planes already on screen stay valid and
 * no force-full is needed. Permuting there would renumber slots behind those
 * planes — the "brown chrome" failure — so that path passes 0 and takes the RGB
 * assignment alone. st_reband has just re-quantised and will force-full anyway,
 * so it passes 1. */
static void st_unify_border(short may_permute)
{
	short g, s, n, best;
	long  bestd;
	unsigned char newpal[ST_NCOL * 3];
	unsigned char pos[ST_NCOL];

	for (g = 1; g < s_ngrp; g++) {
		best  = 0;
		bestd = 0x7fffffffL;
		for (s = 0; s < ST_NCOL; s++) {
			long d = st_coldist(s_gpal[g] + (long)s * 3, s_gpal[0]);

			if (d < bestd) { bestd = d; best = s; }
		}
		if (may_permute && best != 0) {  /* swap `best` and 0 */
			for (n = 0; n < ST_NCOL; n++)
				pos[n] = (unsigned char)n;
			pos[best] = 0;
			pos[0]    = (unsigned char)best;
			for (n = 0; n < ST_NCOL; n++)
				memcpy(newpal + (long)pos[n] * 3,
				       s_gpal[g] + (long)n * 3, 3);
			memcpy(s_gpal[g], newpal, sizeof newpal);
			for (n = 0; n < 256; n++)
				s_grem[g][n] = pos[s_grem[g][n]];
		}
		memcpy(s_gpal[g], s_gpal[0], 3);   /* exactly, so the border is one colour */
	}
}

/* Defined with the rest of the palette cache, below st_buf_differs — which
 * this path predates. */
static void st_pc_flush(void);

static void st_repalette(void)
{
	short g, s;

#ifdef FRUA_PLANAR
	st_dt_epoch_reset();                     /* slots renumber: draw-time epoch */
#endif
	for (g = 0; g < s_ngrp; g++) {
		for (s = 0; s < ST_NCOL; s++) {
			unsigned char idx = s_gslot_rep[g][s];

			s_gpal[g][s * 3 + 0] = quant_snap(s_clut[idx * 3 + 0], ST_BITS);
			s_gpal[g][s * 3 + 1] = quant_snap(s_clut[idx * 3 + 1], ST_BITS);
			s_gpal[g][s * 3 + 2] = quant_snap(s_clut[idx * 3 + 2], ST_BITS);
		}
	}
	/* The reps are per group, so each group re-derives its own slot 0 and the
	 * border would band again on the fast path exactly as it does on the slow
	 * one. Only the RGB assignment matters here — the slot numbering is
	 * unchanged by construction on this path, so the permutation inside is a
	 * no-op and the planes stay valid. */
	st_unify_border(0);
	for (g = 0; g < s_ngrp; g++)
		memcpy(s_gpal_prev[g], s_gpal[g], (size_t)(ST_NCOL * 3));
	st_build_hw_palette();
	/* ★ THIS PATH INVALIDATES THE PALETTE CACHE. It keeps the slot numbering
	 * and moves the RGB, so an entry cut under the old CLUT still LOOKS
	 * current (its stored CLUT would match a later install) while its
	 * palette now describes colours that are gone. Cheaper and safer to drop
	 * the lot than to reason about which entries survived. */
	st_pc_flush();
	s_force_full   = 0;      /* planes unchanged: nothing to re-convert */
}

/* --- THE PALETTE CACHE (#139 "pin it") -----------------------------------
 *
 * ★ WHAT THE WALK'S RE-BANDS ACTUALLY ARE. #142 recorded that st_reband "is NOT
 * driven by qd_set_palette: every count>=32 palette write happens before the
 * walk starts". That is wrong, and a 300-key HEIRS drive says so plainly: 85
 * re-bands, of which the new-ink overflow path (st_patch_new_ink declining past
 * INK_MAX) accounts for **11**. The other 74 are CLUT installs. The walk is not
 * one scene whose palette drifts — it is a handful of scenes ALTERNATING, and
 * every switch back re-cuts a palette this code has already cut.
 *
 * How few scenes: over the 76 play-phase re-bands of that drive there are **18
 * distinct CLUTs**, and 58 of the 76 land on a CLUT cut before. The backend
 * already remembers one (s_clut_banded, the skip in st_present) — but ONE is
 * exactly the wrong number for an alternation, and the LRU curve measured off
 * the capture set says so:
 *
 *     size  1    2    3    4    5    6    8   13
 *     hit   0%   2%   2%  53%  55%  65%  73%  76%
 *
 * Nothing until 4, because the cycle is walk -> cleared viewport -> walk -> event
 * picture and a 1-deep memory never sees the same CLUT twice running. ST_PCACHE
 * is 8: two thirds of the ceiling for 20 KB, where 13 buys three more points for
 * another 12 KB.
 *
 * ★ IT IS A PIN, NOT A SHORTCUT — the restored palette was cut from DIFFERENT
 * pixels, so it is not what re-cutting would produce. Priced offline over the
 * repeats (tools/quant/qpin, which compares only within one CLUT because
 * comparing across two is meaningless): viewport error +7.9%, everything
 * outside it -3.7%. The chrome improving is not a fluke — a fresh cut on a
 * frame whose viewport just filled with a portrait re-partitions the roster
 * panel's colours too, and the cached cut does not.
 *
 * A hit still runs the used-index scan, the slot alignment, the border
 * unification and the hardware-palette build; only the median cut is replaced.
 * That matters for what to expect: quant_banded was measured at 0.73 s of a
 * ~3.2 s re-band, so the cut alone is not the prize. The prize is that a
 * restored palette is far likelier to survive st_align_group with no USED index
 * moving slot, which skips the force-full — the single largest item in a
 * re-band. */
#define ST_PCACHE 8
struct st_pcent {
	unsigned long  hash;                     /* of the 768-byte CLUT         */
	unsigned char  clut[768];                /* verified, never trusted raw  */
	unsigned char  gpal[ST_MAXGRP][ST_NCOL * 3];
	unsigned char  grem[ST_MAXGRP][256];
	unsigned char  gused[ST_MAXGRP][256];    /* what the cut actually saw    */
	short          ngrp;
	short          b0[ST_MAXGRP + 1];
	short          valid;
};
static struct st_pcent s_pcache[ST_PCACHE];
static short           s_pc_mru[ST_PCACHE];      /* entry indices, MRU first */
static short           s_pc_n;                   /* live entries             */
/* Runtime, like st_vp_bands and for the same reason: an A/B that needs two
 * binaries is an A/B of two binaries. video.cfg `palcache=off`. */
short                  st_pal_cache = 1;

/* Long-wise, because memcmp costs 93 cycles/byte on this target and this runs
 * on every re-band (see st_row_differs for the same substitution). */
static unsigned long st_pc_hash(const unsigned char *clut)
{
	const unsigned long *p = (const unsigned long *)clut;
	unsigned long        h = 2166136261UL;
	short                i;

	for (i = 0; i < 768 / 4; i++)
		h = (h ^ p[i]) * 16777619UL;
	return h;
}

/* -1 on a miss. The hash narrows it to one candidate; the CLUT is then
 * compared in full, because a false hit installs the WRONG sixteen colours
 * behind live planes and that is not a failure mode worth saving 5 ms on. */
static short st_pc_find(void)
{
	unsigned long h = st_pc_hash(s_clut);
	short         k, g;

	for (k = 0; k < s_pc_n; k++) {
		struct st_pcent *e = &s_pcache[s_pc_mru[k]];

		if (!e->valid || e->hash != h || e->ngrp != s_ngrp)
			continue;
		for (g = 0; g <= s_ngrp; g++)
			if (e->b0[g] != s_grp_b0[g])
				break;
		if (g <= s_ngrp)
			continue;                /* same CLUT, different split */
		if (st_buf_differs(e->clut, s_clut, 768))
			continue;
		/* promote to MRU */
		if (k) {
			short v = s_pc_mru[k];
			for (; k > 0; k--)
				s_pc_mru[k] = s_pc_mru[k - 1];
			s_pc_mru[0] = v;
		}
		return s_pc_mru[0];
	}
	return -1;
}

/* Insert the palettes as they stand — AFTER the alignment and the border
 * unification, so a restore reproduces a state this backend has already
 * shipped rather than a half-finished one. */
static void st_pc_store(void)
{
	short slot, g, k;

	if (s_pc_n < ST_PCACHE) {
		slot = s_pc_n++;
	} else {
		slot = s_pc_mru[ST_PCACHE - 1];   /* evict the LRU */
	}
	{
		struct st_pcent *e = &s_pcache[slot];

		e->hash = st_pc_hash(s_clut);
		memcpy(e->clut, s_clut, 768);
		e->ngrp = s_ngrp;
		for (g = 0; g <= s_ngrp; g++)
			e->b0[g] = s_grp_b0[g];
		for (g = 0; g < s_ngrp; g++) {
			memcpy(e->gpal[g],  s_gpal[g],  (size_t)(ST_NCOL * 3));
			memcpy(e->grem[g],  s_grem[g],  256);
			memcpy(e->gused[g], s_gused[g], 256);
		}
		e->valid = 1;
	}
	for (k = (short)(s_pc_n - 1); k > 0; k--)
		s_pc_mru[k] = s_pc_mru[k - 1];
	s_pc_mru[0] = slot;
}

/* Every re-band renumbers slots, so a cached palette is only usable while the
 * colour space it was cut in is still current. Nothing else here invalidates
 * it — st_repalette moves the RGB behind fixed slots, which changes what the
 * cached CLUT means, so that path drops the whole cache rather than reason
 * about which entries survived. */
static void st_pc_flush(void)
{
	short k;

	for (k = 0; k < ST_PCACHE; k++)
		s_pcache[k].valid = 0;
	s_pc_n = 0;
}

/* The stable-slot alignment (#146), for ONE group.
 *
 * Extracted from st_reband so that every group gets it. The correspondence has
 * to be between a group's OWN previous cut and its new one: matching across
 * groups would pair a viewport slot with a chrome slot, which is the opposite
 * of what this is for. Permutes the new palette and remap in place so that as
 * many USED indices as possible keep the slot number they already had.
 *
 * Now: build the 16x16 table of how many used indices move from old slot p to
 * new slot n, and match the STRONGEST correspondences first. That optimises
 * the quantity being measured directly. Slots with no correspondence at all
 * (genuinely new colours) still fall back to nearest-colour, which keeps them
 * somewhere sensible. Greedy rather than a full Hungarian matching: 16x16, and
 * the table is dominated by a few large cells, so max-first lands on or very
 * near the optimum for a few thousand cycles. */
static void st_align_group(short g)
{
	unsigned char       *pal  = s_gpal[g];
	unsigned char       *rem  = s_grem[g];
	const unsigned char *palp = s_gpal_prev[g];
	const unsigned char *remp = s_grem_prev[g];
	const unsigned char *used = s_gused[g];
	unsigned char tkn[ST_NCOL], tkp[ST_NCOL];
	unsigned char pos[ST_NCOL];             /* pos[newslot] = its position   */
	unsigned char newpal[ST_NCOL * 3];
	short tab[ST_NCOL][ST_NCOL];            /* tab[new][old] = index count   */
	short p, n, v, k;

	for (n = 0; n < ST_NCOL; n++) {
		tkn[n] = tkp[n] = 0;
		pos[n] = (unsigned char)n;
		for (p = 0; p < ST_NCOL; p++)
			tab[n][p] = 0;
	}
	for (v = 0; v < 256; v++)
		if (used[v])
			tab[rem[v]][remp[v]]++;

	/* ★ SLOT 0 IS THE BORDER, AND IT IS CLAIMED BY COLOUR FIRST — but only in
	 * the group that actually owns the border. The renumber is invariant for
	 * every PIXEL (palette and remap are permuted together, so index v keeps
	 * its colour), yet the ST shows colour register 0 in the border, where no
	 * pixel index is involved at all. Let the correspondence greedy have
	 * position 0 and the border changes from black to whatever won it:
	 * measured, 174976 pixels of the menu grab, entirely outside the 320x200
	 * image.
	 *
	 * The ORIGINAL code walked old positions 0..15 in order, so position 0
	 * always got first pick by colour — its order dependence was, for this one
	 * slot, load-bearing. Keep exactly that and let the correspondence matching
	 * have the other fifteen. With a raster split EVERY group's register 0
	 * reaches the border on its own scanlines, so the claim has to happen
	 * to hold the same colour in every group — see st_unify_border. */
	{
		short best = 0;
		long  bestd = 0x7fffffffL;

		for (n = 0; n < ST_NCOL; n++) {
			long d = st_coldist(pal + (long)n * 3, palp);

			if (d < bestd) { bestd = d; best = n; }
		}
		tkn[best] = 1; tkp[0] = 1;
		pos[best] = 0;
	}

	/* 1) strongest index correspondence first */
	for (k = 0; k < ST_NCOL; k++) {
		short bn = -1, bp = -1, bv = 0;

		for (n = 0; n < ST_NCOL; n++) {
			if (tkn[n]) continue;
			for (p = 0; p < ST_NCOL; p++) {
				if (tkp[p]) continue;
				if (tab[n][p] > bv)
					{ bv = tab[n][p]; bn = n; bp = p; }
			}
		}
		if (bn < 0)
			break;                  /* nothing left to correspond */
		tkn[bn] = 1; tkp[bp] = 1;
		pos[bn] = (unsigned char)bp;
	}
	/* 2) the rest: nearest colour, as before */
	for (n = 0; n < ST_NCOL; n++) {
		short best = -1;
		long  bestd = 0x7fffffffL;

		if (tkn[n]) continue;
		for (p = 0; p < ST_NCOL; p++) {
			long d;
			if (tkp[p]) continue;
			d = st_coldist(pal + (long)n * 3, palp + (long)p * 3);
			if (d < bestd) { bestd = d; best = p; }
		}
		if (best < 0)
			continue;               /* cannot happen: counts match */
		tkn[n] = 1; tkp[best] = 1;
		pos[n] = (unsigned char)best;
	}
	for (n = 0; n < ST_NCOL; n++)
		memcpy(newpal + (long)pos[n] * 3, pal + (long)n * 3, 3);
	memcpy(pal, newpal, sizeof newpal);
	for (v = 0; v < 256; v++)
		rem[v] = pos[rem[v]];
#ifdef FRUA_STPROF
	/* ★ IS THE CHURN AVOIDABLE? #146 measured 57.2% of used indices moving
	 * slot per re-band, and the obvious reading is "the permutation is
	 * weak". But a permutation can only RELABEL — it cannot preserve a
	 * colour the new median cut no longer produces. So split the moves:
	 * an index whose new slot holds the SAME colour its old slot did was
	 * moved by labelling and could have been kept; one whose colour
	 * genuinely changed was moved by the CUT and no labelling could have
	 * saved it. That decides whether #146 belongs in this function or in
	 * quantize.h. */
	{
		short mv_avoid = 0, mv_cut = 0, mv_near = 0;

		for (v = 0; v < 256; v++) {
			short os, ns;
			long d;

			if (!used[v])
				continue;
			os = remp[v];
			ns = rem[v];
			if (os == ns)
				continue;
			d = st_coldist(pal + (long)ns * 3, palp + (long)os * 3);
			if (d == 0)                  mv_avoid++;
			else if (d <= 3 * 32L * 32L) mv_near++;
			else                         mv_cut++;
		}
		dbg_log_num("b146: moved-but-same-colour = ", (long)mv_avoid);
		dbg_log_num("b146:   moved-near-colour   = ", (long)mv_near);
		dbg_log_num("b146:   moved-CUT-changed   = ", (long)mv_cut);
	}
#endif
}

/* Re-band: one median cut per palette GROUP, the stable-slot alignment per
 * group, then the per-band ST-format palettes. The sentinel row (see
 * st_band_stpal) is a copy of the last band. */
static void st_reband(void)
{
	const unsigned char *qsrc = s_chunky;
	short g, i, first;
	short old_ngrp = s_ngrp;
	short pc;                                /* cache entry, or -1 on a miss */

#ifdef FRUA_PLANAR
	st_dt_epoch_reset();                     /* slots renumber: draw-time epoch */
#endif

	/* Decide the split BEFORE anything reads s_ngrp. A layout change has no
	 * usable predecessor — the middle group's previous cut covered different
	 * rows, so the correspondence would match colours that never shared a
	 * region — so treat it exactly like the first re-band. */
	st_group_layout();
	first = (short)(!s_have_prev_pal || s_ngrp != old_ngrp);
	/* AFTER st_group_layout: an entry is only valid for the split it was cut
	 * under, so the lookup needs the live boundaries. */
	pc = st_pal_cache ? st_pc_find() : -1;

	/* Pin the composited walls' colours (ADR-0016 B1). After B2.1 the dungeon
	 * viewport renders into the planar SCRATCH, not s_chunky, so the reband never
	 * saw the wall/backdrop colours and the composite mapped their CLUT indices
	 * through the luma fallback — walls came out in HUD greys, not their authored
	 * stone/wood/sky. When a viewport is committed (s_vp_active, still set here —
	 * the composite that clears it runs after us), quant over a copy of s_chunky
	 * with the scratch's viewport rect overlaid, so the fixed palette is derived
	 * from the walls too and their indices get exact slots. The temp lives in
	 * s_shadow, which the forced-full blit right after this rebuilds anyway.
	 *
	 * #63: snapshot the LIVE index->slot map first — the stable-slot alignment
	 * below needs it, and taking it here also captures anything st_patch_new_ink
	 * changed since the last re-quant. */
	if (!first)
		for (g = 0; g < s_ngrp; g++)
			memcpy(s_grem_prev[g], s_grem[g], 256);

#ifdef FRUA_STPROF
	{ long tv = Supexec(st_prof_hz200);
#endif
	if (s_vp_active) {
		short r;

		/* ★ THE CONTROL FOR THE PREDICTION. If a re-band ever fires on a frame
		 * the engine skipped the chunky pass for, the palette is being derived
		 * from the PREVIOUS frame's walls — the exact failure the prediction can
		 * have, and one that would otherwise show up only as a subtle wrong
		 * shade nobody traces back here. Count it, and say so once. */
		s_rb_seen++;
#ifdef FRUA_PALTRACE
		dbg_log_num("pt: reband RUN vp_active=1 chunky_ok = ",
		            (long)s_vp_chunky_ok);
#endif
		if (!s_vp_chunky_ok) {
			s_rb_stale++;
			if (s_rb_stale == 1)
				dbg_log("ste: RE-BAND ON A STALE VIEWPORT (lazy chunky missed)");
		}
		memcpy(s_shadow, s_chunky, (long)ST_W * ST_H);
		for (r = 0; r < s_vp_h; r++) {
			short yy = (short)(s_vp_y + r);
			memcpy(s_shadow + (long)yy * ST_W + s_vp_x,
			       s_vp_scratch + (long)yy * VP_SCR_PITCH + s_vp_x,
			       (size_t)s_vp_w);
		}
		qsrc = s_shadow;
	}
#ifdef FRUA_STPROF
	sp_rb_vpcopy += Supexec(st_prof_hz200) - tv;
	sp_rb_n++;
	}
#endif

#ifdef FRUA_QDUMP
	/* ★ CAPTURE THE QUANTISER'S INPUTS, so tools/quant can be run on a real
	 * frame instead of a synthetic one. Writes the 320x200 index surface and
	 * the 768-byte CLUT exactly as quant_banded is about to see them — the
	 * viewport overlay included, since qsrc is s_shadow when a viewport is
	 * committed. One pair per re-band, numbered; the tools take the pair as
	 * arguments. GEMDOS, so they land in the mounted gamedata dir.
	 *
	 * These files are DERIVED FROM COPYRIGHTED ART — they are frame buffers of
	 * the game. Never commit them (data/ is git-ignored for the same reason). */
	{
		static short qd_n;
		char nm[16];
		short fh;

		nm[0] = 'q'; nm[1] = (char)('0' + (qd_n / 10) % 10);
		nm[2] = (char)('0' + qd_n % 10);
		nm[3] = '.'; nm[4] = 'f'; nm[5] = 'r'; nm[6] = 'm'; nm[7] = 0;
		fh = (short)Fcreate(nm, 0);
		if (fh >= 0) { Fwrite(fh, (long)ST_W * ST_H, qsrc); Fclose(fh); }
		nm[4] = 'c'; nm[5] = 'l'; nm[6] = 't';
		fh = (short)Fcreate(nm, 0);
		if (fh >= 0) { Fwrite(fh, 768L, s_clut); Fclose(fh); }
		qd_n++;
	}
#endif

	/* --- the cut, once per group -------------------------------------------
	 *
	 * ADR-0016 B1 put the whole frame in one group and replicated the result to
	 * every band, because per-band palettes produced the #40 SEAMS: a flat panel
	 * spanning a boundary in stripes. That argument holds for boundaries chosen
	 * by arithmetic and not for boundaries placed where the content changes, so
	 * s_grp_b0 puts them on the viewport's own edges (st_group_layout) and
	 * nowhere else. With s_ngrp == 1 this loop is byte-for-byte the old path.
	 *
	 * The used-index scan is folded in here rather than run as its own 64,000-
	 * pixel pass: each pixel is still visited exactly once, and it yields the
	 * PER-GROUP sets the alignment needs as well as the global one. */
#ifdef FRUA_STPROF
	{ long tq = Supexec(st_prof_hz200);
#endif
	for (g = 0; g < s_ngrp; g++) {
		short y0 = (short)(s_grp_b0[g] * ST_RPB);
		short y1 = (short)(s_grp_b0[g + 1] * ST_RPB);
		long  n, n0 = (long)y0 * ST_W, n1 = (long)y1 * ST_W;

#ifdef FRUA_STPROF
		{ long tu = Supexec(st_prof_hz200);
#endif
		memset(s_gused[g], 0, 256);
		for (n = n0; n < n1; n++)
			s_gused[g][qsrc[n]] = 1;
#ifdef FRUA_STPROF
		sp_rb_used += Supexec(st_prof_hz200) - tu;
		}
#endif
		if (pc >= 0) {
			/* PIN: reuse the palette this CLUT was cut with. Indices the
			 * cached cut never saw would otherwise keep quant_banded's
			 * fallback for an ABSENT colour, which is the coarse 4x8x4
			 * bucket table — the same fallback that makes a coloured glyph
			 * land on whatever background matches its brightness. Give them
			 * nearest-in-RGB instead, exactly as st_patch_new_ink does; the
			 * loop only touches indices that are new to this palette. */
			const struct st_pcent *e = &s_pcache[pc];

			memcpy(s_gpal[g], e->gpal[g], (size_t)(ST_NCOL * 3));
			memcpy(s_grem[g], e->grem[g], 256);
			for (i = 0; i < 256; i++) {
				short sl, best = 0;
				long  bestd = 0x7fffffffL;

				if (!s_gused[g][i] || e->gused[g][i])
					continue;
				for (sl = 0; sl < ST_NCOL; sl++) {
					long d = st_coldist(s_clut + (long)i * 3,
					                    s_gpal[g] + (long)sl * 3);
					if (d < bestd) { bestd = d; best = sl; }
				}
				s_grem[g][i] = (unsigned char)best;
			}
		} else {
			quant_banded(qsrc + n0, ST_W, (short)(y1 - y0), s_clut,
			             1, ST_NCOL, ST_BITS, s_gpal[g], s_grem[g]);
		}
		if (!first) {
#ifdef FRUA_STPROF
			long ta = Supexec(st_prof_hz200);
#endif
			st_align_group(g);
#ifdef FRUA_STPROF
			sp_rb_align += Supexec(st_prof_hz200) - ta;
#endif
		}
	}
	st_unify_border(1);
	/* Store the FINAL palettes — post-alignment, post-border — so a restore
	 * reproduces a state this backend has already shipped. Only on a miss: a
	 * hit has just been promoted to MRU and re-storing it would rewrite the
	 * entry with a copy of itself. */
	if (pc < 0 && st_pal_cache)
		st_pc_store();
#ifdef FRUA_STPROF
	if (pc >= 0) sp_pc_hit++; else sp_pc_miss++;
#endif
	for (g = 0; g < s_ngrp; g++)
		memcpy(s_gpal_prev[g], s_gpal[g], (size_t)(ST_NCOL * 3));
	memset(s_used_idx, 0, sizeof s_used_idx);
	for (g = 0; g < s_ngrp; g++)
		for (i = 0; i < 256; i++)
			if (s_gused[g][i])
				s_used_idx[i] = 1;
	s_remap_gen++;                           /* planes stamped before this are stale */
#ifdef FRUA_STPROF
	sp_rb_quant += Supexec(st_prof_hz200) - tq;
	}
#endif

#ifdef FRUA_PALTRACE
	/* ★ HOW MANY COLOURS IS THIS FRAME ASKING FOR? The task title guesses a
	 * stale remap; the speckle is equally what an OVERRUN BUDGET looks like —
	 * 16 slots cannot reproduce a frame with far more distinct indices, so the
	 * excess resolves through the RGB-nearest fallback and the walls freckle.
	 * One number decides which story it is. */
	{
		short ui, un = 0;

		for (ui = 0; ui < 256; ui++)
			if (s_used_idx[ui])
				un++;
		dbg_log_num("pt: distinct indices in frame = ", (long)un);
		dbg_log_num("pt: palette groups            = ", (long)s_ngrp);
	}
#endif

	s_have_prev_pal = 1;
	st_compute_slot_reps();          /* B4 Phase-0: reps for palette-only rebands */
	st_build_hw_palette();           /* expands the groups into the bands */

	/* B3.2: the FIRST re-band has no aligned predecessor, and the viewport path
	 * clobbers s_shadow (its temp), so both must convert everything.
	 *
	 * B4: FORCE-FULL on every re-band. B3.2's "smart-skip" (re-c2p only the rows
	 * holding a value whose slot moved) was tried and cannot work here: its dirty
	 * map is computed ONCE per re-band, against the remap ONE page was last drawn
	 * with, and the two pages alternate — so the map is simply wrong for the other
	 * page, which is what the "brown chrome" was. The force-full path converts
	 * BOTH pages in one present against the current palette, so a single flag
	 * suffices and both pages stay on the SAME palette (fixing the grey-on-grey
	 * roster: a page force-fulled on an older CLUT never gets shown). Costs 2
	 * c2p's on a re-band only; re-bands are rare and the flat-fill already tamed
	 * the c2p. The smart-skip's machinery was removed 2026-07-26 — it had been
	 * unreachable ever since this decision. */
	/* ★ A RE-BAND THAT MOVED NOTHING NEEDS NO REBUILD. B3.2's stable-slot
	 * alignment permutes the new slots to match the old positions, and it
	 * sometimes succeeds completely: measured over 19 re-bands of a fixture
	 * walk, one moved 0 of 59 used indices while the others moved 25-86%
	 * (57.2% overall). When no USED index changed slot, every plane on screen
	 * still encodes the right colour and the force-full — 297 t200, the single
	 * largest item in a re-band — is pure waste.
	 *
	 * Unused indices may still have moved: harmless, because by definition no
	 * pixel carries one, and anything drawn afterwards is stamped through the
	 * new remap anyway.
	 *
	 * The viewport's planes are covered by the same argument, so s_remap_gen
	 * does not move either — which keeps B5's copy path (2b1acdd7) instead of
	 * dropping it to the conversion for a change that changed nothing.
	 *
	 * NOT a revival of the removed "smart-skip": that tried to rebuild a SUBSET
	 * of rows from a per-page dirty map and was wrong for the alternate page
	 * (the "brown chrome"). This is all-or-nothing and page-independent.
	 *
	 * ★ THE GUARD TESTS `first`, NOT `s_have_prev_pal`. It used to test the
	 * latter, which this function sets to 1 a few lines above — so the
	 * no-predecessor arm was dead and the first re-band compared against an
	 * all-zero s_grem_prev. It happened to force a rebuild anyway (index 0 is
	 * almost never slot 0 in a fresh cut), but only by luck. */
	{
		short mi, umoved = 0;

		if (first) {
			umoved = 1;              /* no predecessor: convert everything */
		} else {
			for (g = 0; g < s_ngrp && !umoved; g++)
				for (mi = 0; mi < 256; mi++)
					if (s_gused[g][mi]
					 && s_grem_prev[g][mi] != s_grem[g][mi]) {
						umoved = 1;
						break;
					}
		}
		if (!umoved) {
			s_remap_gen--;           /* undo the bump: nothing went stale */
#ifdef FRUA_STPROF
			sp_rb_noskew++;
#endif
#ifdef FRUA_PALTRACE
			dbg_log("pt: reband moved NO used index -> no force-full");
#endif
			s_ink_fresh = 1;
			return;
		}
	}
	s_force_full   = 1;
	/* #63(1): s_used_idx above was captured from THIS frame, so the
	 * force-full's per-row new-ink scan cannot find anything. Skip it. */
	s_ink_fresh    = 1;
}

#ifdef FRUA_PLANAR
static int st_dt_target(struct dsp_planar_dt *dt)
{
	if (!s_have_pal || s_dt == NULL)
		return 0;                        /* no palette / no remap yet */
	dt->planes       = s_dt;
	dt->remap        = s_band_remap;
	dt->cov          = s_dt_cov;
	dt->idx          = s_dt_idx;
	dt->rowcov       = s_dt_rowcov;
	dt->chunky       = s_chunky;
	dt->chunky_pitch = ST_W;
	dt->line_bytes   = LINE_BYTES;
	dt->plane_bytes  = 0;            /* interleaved layout: unused */
	dt->w            = ST_W;
	dt->h            = ST_H;
	dt->nplanes      = ST_DEPTH;
	dt->nbands       = ST_NBANDS;
	return 1;
}

#ifdef FRUA_PLANAR_DIAG
/* Decode pixel (x,y)'s slot out of an ST-Low interleaved plane buffer — the
 * inverse of planar_put_stlow (plane p's word for 16-px group g at
 * y*LINE_BYTES + g*ST_DEPTH*2 + p*2, MSB = leftmost). */
static unsigned char st_slot_at(const unsigned char *buf, short x, short y)
{
	short g = (short)(x >> 4), bit = (short)(x & 15);
	short byte = (short)(bit >> 3), p;
	unsigned char mask = (unsigned char)(0x80u >> (bit & 7)), s = 0;
	const unsigned char *grp =
	    buf + (long)y * LINE_BYTES + (long)g * ST_DEPTH * 2;

	for (p = 0; p < ST_DEPTH; p++)
		if (grp[(long)p * 2 + byte] & mask)
			s = (unsigned char)(s | (1 << p));
	return s;
}

/* B4 step 3a self-check: on the LIVE menu, prove the converted DrawChar / fill
 * writers stamp s_dt with the SAME slot the c2p would. The oracle is recomputed
 * per pixel as remap[band][chunky] (immune to s_screen's row-diff staleness).
 * Only pixels the writer still OWNS are tested: skip any an unconverted writer
 * later overwrote (s_dt_idx != chunky), and the epoch reset on re-band drops
 * stale-slot pixels. Logs owned / mismatches / overwritten for the first few
 * frames; changes NOTHING on screen (display stays on the chunky c2p path). A
 * zero mismatch count over a large owned set is the in-situ proof; the
 * overwritten count is the chrome the bridge (or a converted chrome writer)
 * must still supply. */
static void st_dt_selfcheck(void)
{
	long owned = 0, bad = 0, overwritten = 0, x, y, shown = 0;

	/* ONE audit per epoch: the 64000-px scan takes seconds at 8 MHz, and
	 * auditing every present skews the autoplay pacing enough to break the
	 * scripted dungeon entry (found live: 4/epoch stalled the hall). */
	if (s_dt == NULL || s_dt_cov == NULL || s_dt_idx == NULL || s_dt_checks >= 1)
		return;
	for (y = 0; y < ST_H; y++) {
		short band = (short)((long)y * ST_NBANDS / ST_H);
		const unsigned char *lut = s_band_remap + (long)band * 256;
		for (x = 0; x < ST_W; x++) {
			long c = (long)y * ST_W + x;
			unsigned char chk;
			if (!s_dt_cov[c])
				continue;
			chk = s_chunky[c];
			/* An unconverted writer (chrome blit, pattern/bitwise fill) that
			 * overwrote this pixel AFTER our converted writer left chunky !=
			 * the index we laid: it's no longer ours — the bridge owns it. */
			if (s_dt_idx[c] != chk) {
				overwritten++;
				continue;
			}
			/* Where our converted writer IS the last writer and the epoch is
			 * current, s_dt must equal the c2p's slot for this pixel. */
			owned++;
			if (st_slot_at(s_dt, (short)x, (short)y) != lut[chk]) {
				bad++;
				if (shown < 6) {
					dbg_log_num("b4dt  miss x= ", x);
					dbg_log_num("b4dt  miss y= ", y);
					dbg_log_num("b4dt   chunky= ", chk);
					dbg_log_num("b4dt   want  = ", lut[chk]);
					dbg_log_num("b4dt   got   = ",
					            st_slot_at(s_dt, (short)x, (short)y));
					shown++;
				}
			}
		}
	}
	if (owned + overwritten > 0) {
		dbg_log_num("b4dt: epoch       = ", s_dt_epoch);
		dbg_log_num("b4dt: owned px    = ", owned);
		dbg_log_num("b4dt: mismatches  = ", bad);
		dbg_log_num("b4dt: overwritten = ", overwritten);
		s_dt_checks++;
	}
}
#endif /* FRUA_PLANAR_DIAG */

/* B4 step 4 (the flip): build one scanline of the native-planar frame in s_dt.
 * Owned pixels trusted; the rest bridged from chunky. */
/* NEW-INK detector (the roster-text fix). The quantizer maps colours ABSENT
 * from its source frame through a nearest-LUMA fallback — so a chromatic ink
 * drawn AFTER the re-band (the gold roster text: its luma ~= the panel grey's)
 * lands on the panel's slot and renders INVISIBLE. The baseline c2p dodges it
 * only by cadence (slower presents -> the re-band usually fires after the text
 * exists). Count converted pixels whose index the last quant never saw; the
 * present tail schedules a re-quant when enough arrive — one present later the
 * ink has a real slot. Same class the B1 wall-pinning solved for walls. */
static long s_dt_new_ink;

/* #63: WHICH rows and WHICH indices carried the unseen ink.
 *
 * The trigger used to answer only "how many?", and its response to "4 or more"
 * was to clear s_banded_valid and force a whole re-quant — measured as 5 of 22
 * rebands firing with a BYTE-IDENTICAL CLUT, each costing ~6.7 s and visibly
 * reshuffling the palette. The palette does not need re-partitioning to give a
 * never-seen index a slot; it needs that one index mapped to the nearest slot
 * it already has. Recording the index lets us patch it, and recording the row
 * lets us rebuild just that row instead of the frame.
 *
 * Bounded: past INK_MAX distinct indices the cheap patch stops being obviously
 * right (that many unseen colours IS a scene change), so we fall back to the
 * old force-a-requant behaviour. */
#define INK_MAX 24
static unsigned char s_ink_idx[256];    /* index seen unmapped this present  */
static short         s_ink_n;           /* distinct indices recorded         */
static short         s_ink_over;        /* blew INK_MAX -> full re-quant     */

/* #142: SIZE THE NEW-INK RE-QUANT BEFORE TOUCHING INK_MAX.
 *
 * ★ THE FIRST SENTENCE OF THIS BLOCK USED TO BE FALSE, AND IT AIMED THE TASK AT
 * THE WRONG LEVER. It read "st_reband ... is NOT driven by qd_set_palette: every
 * count>=32 palette write happens before the walk starts, so s_dirty is never set
 * that way during it". A 300-key HEIRS drive says otherwise: 85 re-bands, and
 * THIS path — a step draws indices the band palettes do not cover,
 * st_patch_new_ink() places them incrementally, and past INK_MAX distinct new
 * indices it declines and the whole frame is re-quantised — accounts for 11 of
 * them. The other 74 are palette installs. See the palette cache above, which is
 * what that measurement led to.
 *
 * st_reband is 4.5% of the walk.
 *
 * Raising INK_MAX trades fidelity for speed (more indices land on nearest-existing
 * slots rather than a proper re-partition), so it needs numbers first: how often
 * the patch is asked, how often it declines and WHY, and — the one the threshold
 * actually depends on — how far past 24 the true distinct count goes. The shipping
 * code cannot answer that last one because it stops counting at the threshold
 * (`break` on overflow), so under this flag it keeps counting instead. That makes
 * the diagnostic build scan more, but leaves the DECISION identical: s_ink_over is
 * still set at exactly the same point. */
#ifdef FRUA_PALDIAG
static unsigned long pk_present, pk_calls, pk_ok, pk_over, pk_empty, pk_nopal;
static unsigned long pk_reband, pk_repal, pk_next = 64;
static unsigned long pk_b25_32, pk_b33_48, pk_b49_64, pk_b65up;
static short         pk_true_n, pk_true_max;

static void pk_dump(void)
{
	dbg_file_num("inkdiag: presents      = ", (long)pk_present);
	dbg_file_num("inkdiag: patch calls   = ", (long)pk_calls);
	dbg_file_num("inkdiag:   ACCEPTED    = ", (long)pk_ok);
	dbg_file_num("inkdiag:   decline over= ", (long)pk_over);
	dbg_file_num("inkdiag:   decline none= ", (long)pk_empty);
	dbg_file_num("inkdiag:   decline nopal=", (long)pk_nopal);
	dbg_file_num("inkdiag: st_reband     = ", (long)pk_reband);
	dbg_file_num("inkdiag: st_repalette  = ", (long)pk_repal);
	dbg_file_num("inkdiag: true-n max    = ", (long)pk_true_max);
	dbg_file_num("inkdiag:   true 25..32 = ", (long)pk_b25_32);
	dbg_file_num("inkdiag:   true 33..48 = ", (long)pk_b33_48);
	dbg_file_num("inkdiag:   true 49..64 = ", (long)pk_b49_64);
	dbg_file_num("inkdiag:   true 65+    = ", (long)pk_b65up);
}
#endif


/* Does a 320-byte surface row differ? (#63)
 *
 * This is THE hot primitive of the whole backend. The full present compares
 * all 200 rows against the page's shadow on every present, and the HEIRS drive
 * measured that scan at 76% of a present and the present at 32.5% of play —
 * while finding only ~10 changed rows and converting 0.8. The engine spends a
 * quarter of its life discovering that nothing moved.
 *
 * `memcmp` costs **93 cycles per byte** on this target; this loop costs **30**
 * (measured back to back in one boot, 16 sweeps of the real 64000-byte
 * surface: 2395 vs 776 t200). Rows are Mxalloc-based and ST_W is a multiple
 * of 4, so the long accesses are aligned. Same answer, a third of the time.
 *
 * The real fix is to not scan at all — the writers already know which rows
 * they touched — but that reaches into the shim, and this is ten lines. */
static int st_row_differs(const unsigned char *a, const unsigned char *b)
{
#ifdef FRUA_ROWDIFF_MEMCMP
	return memcmp(a, b, ST_W) != 0;  /* A/B arm: the old primitive, one flag */
#else
	const long *p = (const long *)a;
	const long *q = (const long *)b;
	short w;

	for (w = 0; w < ST_W / 4; w++)
		if (p[w] != q[w]) {
#ifdef FRUA_STPROF
			/* #63 attribution: the WORK, not the call count. This loop
			 * early-exits, so "rows scanned x 80 words" is an upper bound
			 * that a row differing in its first word misses by 80x. One
			 * add per call at the exit, not one per word. */
			sp_ph_cmpwords += (long)w + 1;
#endif
			return 1;
		}
#ifdef FRUA_STPROF
	sp_ph_cmpwords += ST_W / 4;
#endif
	return 0;
#endif
}

static void st_dt_build_row(short y)
{
	short band = (short)((long)y * ST_NBANDS / ST_H);
	const unsigned char *lut  = s_band_remap + (long)band * 256;
	const unsigned char *crow = s_chunky + (long)y * ST_W;

	/* Rebuild the row through the OPTIMIZED span converter (nibble transpose
	 * + flat fast path). Byte-identical to trusting the draw-time stamps:
	 * ownership (cov && idx==chunky, same epoch) implies the stamp equals
	 * lut[chunky] — 3a-pinned at 0 mismatches — while the per-pixel bridge
	 * the first cut used cost ~2x the c2p on transitions (b4perf). */
	st_c2p_span(crow, s_dt + (long)y * LINE_BYTES, ST_W, lut);
#ifdef FRUA_STPROF
	sp_ph_built++;
#endif

	/* #41 SELF-HEALING OWNERSHIP: the row just built IS remap[chunky], which
	 * is precisely the ownership invariant — so claim it. A row whose skip
	 * failed (a coverage hole left by a pre-epoch backdrop, or a direct
	 * engine writer's overwrite) converts ONCE and then skips on every later
	 * present whose changes came through stamping writers; a direct writer's
	 * next overwrite breaks idx==chunky and re-converts (what the un-healed
	 * path paid every time). Without this, a coverage hole was permanent
	 * until the writer that owned it happened to redraw. */
	if (s_dt_cov != NULL && s_dt_idx != NULL) {
		memset(s_dt_cov + (long)y * ST_W, 1, ST_W);
		memcpy(s_dt_idx + (long)y * ST_W, crow, ST_W);
		if (s_dt_rowcov != NULL)
			s_dt_rowcov[y] = ST_W;
	}
}

/* Prepare row y of s_dt for presentation. NEW-INK scan always runs (a stamped
 * ink the quant never saw still needs its re-quant — see the detector). Then
 * the #41 skip: a row whose EVERY pixel was stamped this epoch (rowcov == W)
 * with idx matching chunky (no unconverted writer overwrote it — one memcmp)
 * is already correct in s_dt, so the span conversion is skipped entirely: the
 * writers' draw-time work replaces the present-time transpose, the end-state
 * payoff of the draw-time model. Returns 1 if the row was converted. */
static int st_dt_ready_row(short y)
{
	const unsigned char *crow = s_chunky + (long)y * ST_W;
	short x;

#ifndef FRUA_NOINK
	/* #63: this scan measured 33% of pass 1 — 1.04 t200 per changed row, or
	 * ~130 cycles a pixel for a table lookup and a conditional increment,
	 * four times the per-byte cost of the row compare next to it. Two exact
	 * changes, neither of which alters what the trigger decides:
	 *
	 *   1. THRESHOLD GATE. The only consumer is `s_dt_new_ink >= 4`, and the
	 *      counter resets every present, so once four have been seen the rest
	 *      of the present's scans cannot change the outcome. Skip them.
	 *   2. LOCAL ACCUMULATOR + pointer walk. s_dt_new_ink is a file static
	 *      and s_used_idx a static array, so the compiler had to assume the
	 *      increment might alias the table and could not keep either in a
	 *      register across the loop.
	 *
	 * (Under FRUA_PLANAR_DIAG the b4ink line now prints a count that stops at
	 * the threshold rather than the true total — the trigger is unaffected.) */
	/* ★ THE THRESHOLD GATE STAYS. Dropping it — so the scan could collect
	 * every unseen index rather than stopping at four pixels — DOUBLED
	 * in-present at boot, 17913 -> 35653 t200. The gate is not the 3% it
	 * looked like in the play loop: there few rows change, while at boot
	 * nearly all of them do, and without it every changed row of every
	 * present pays a full 320-byte table-lookup scan. Trading a 6.7 s reband
	 * for ~88 s of scanning is not a trade.
	 *
	 * Recording identities inside the gate is free, so that is what happens:
	 * one lookup per byte in the common case, exactly as before. */
	if (!s_ink_fresh && s_dt_new_ink < 4) {
		const unsigned char *p   = crow;
		const unsigned char *end = crow + ST_W;
		const unsigned char *tab = s_used_idx;
		long                 ink = s_dt_new_ink;

		while (p < end) {
			unsigned char c = *p++;

			if (!tab[c]) {
				ink++;
				if (!s_ink_idx[c]) {
					s_ink_idx[c] = 1;
#ifdef FRUA_PALDIAG
					pk_true_n++;
					if (++s_ink_n > INK_MAX)
						s_ink_over = 1;  /* same decision, keep counting */
#else
					if (++s_ink_n > INK_MAX)
						{ s_ink_over = 1; break; }
#endif
				}
			}
		}
		s_dt_new_ink = ink;
#ifdef FRUA_STPROF
		sp_ph_inkbytes += ST_W;
#endif
	}
	(void)x;
#else
	/* #63 ABLATION ONLY (never ships): skip the new-ink scan to price it.
	 * Disables the re-quant trigger, so compare rebands too — if they move,
	 * the timing comparison is confounded. */
	(void)x;
#endif
	if (s_dt_rowcov != NULL && s_dt_rowcov[y] == ST_W
	    && !st_row_differs(s_dt_idx + (long)y * ST_W, crow))
		return 0;                        /* writer-stamped: s_dt authoritative */
#ifdef FRUA_STPROF
	/* #41 attribution: WHY did the skip fail — coverage hole (an unconverted
	 * writer never stamped part of the row since the epoch) or a stamp
	 * mismatch (a direct writer overwrote shim-stamped pixels)? For mismatch
	 * rows also record the first differing x (names the overwriter's rect). */
	if (s_dt_rowcov == NULL || s_dt_rowcov[y] != ST_W)
		s_prof_convwhy[y] |= 1;
	else {
		const unsigned char *irow = s_dt_idx + (long)y * ST_W;

		s_prof_convwhy[y] |= 2;
		for (x = 0; x < ST_W; x++)
			if (irow[x] != crow[x]) { s_prof_mmx[y] = x; break; }
	}
#endif
	st_dt_build_row(y);
	return 1;
}

/* --- BLiTTER word copy (task #48) ---------------------------------------
 *
 * Register map from the Atari Compendium B.32 (p.752-753). For a plain
 * aligned copy: HOP = 2 (source only), OP = 3 (D = S), all three endmasks
 * $FFFF, both X/Y increments +2, SKEW/FXSR/NFSR all clear. The 24-bit source
 * and destination addresses are written as LONGS to $FF8A24 / $FF8A32 — the
 * high byte lands in the unused top half of the first word, which is why the
 * hardware splits them that way.
 *
 * Started by setting BUSY (bit 7) with HOG (bit 6) also set, so the blitter
 * keeps the bus until the blit finishes; the CPU is halted meanwhile, which
 * is what we want during a present since there is nothing else to do. The
 * BUSY poll afterwards is belt-and-braces.
 *
 * ★ SUPERVISOR. $FF8A00 is in the protected I/O page, so every one of these
 * writes must run supervisor — hence the _super suffix and Supexec, the same
 * pattern st_flip_super uses. Supexec is a TRAP, so it must be amortised: a
 * per-row Supexec around an 80-word blit would spend more time entering
 * supervisor than blitting. Batch whole presents into one call. */
#define BLT_SRC_XINC (*(volatile short *)0xFFFF8A20UL)
#define BLT_SRC_YINC (*(volatile short *)0xFFFF8A22UL)
#define BLT_SRC      (*(volatile unsigned long *)0xFFFF8A24UL)
#define BLT_END1     (*(volatile unsigned short *)0xFFFF8A28UL)
#define BLT_END2     (*(volatile unsigned short *)0xFFFF8A2AUL)
#define BLT_END3     (*(volatile unsigned short *)0xFFFF8A2CUL)
#define BLT_DST_XINC (*(volatile short *)0xFFFF8A2EUL)
#define BLT_DST_YINC (*(volatile short *)0xFFFF8A30UL)
#define BLT_DST      (*(volatile unsigned long *)0xFFFF8A32UL)
#define BLT_XCOUNT   (*(volatile unsigned short *)0xFFFF8A36UL)
#define BLT_YCOUNT   (*(volatile unsigned short *)0xFFFF8A38UL)
#define BLT_HOP      (*(volatile unsigned char *)0xFFFF8A3AUL)
#define BLT_OP       (*(volatile unsigned char *)0xFFFF8A3BUL)
#define BLT_CTRL     (*(volatile unsigned char *)0xFFFF8A3CUL)
#define BLT_SKEW     (*(volatile unsigned char *)0xFFFF8A3DUL)

/* Words per blit. HOG mode halts the CPU for the whole blit, so one unbroken
 * 32000-word copy is ~8 ms with NO interrupt serviced — and a force-full issues
 * four of them, ~24 ms, well over a frame (task #61: measured 119 frames during
 * boot rendering with missing band palettes, one with NONE at all). The ST's 16
 * colours are a per-band Timer-B palette split, so a frame that misses its band
 * interrupts draws its lower bands in a stale palette. Capping each blit bounds
 * that: pending interrupts are taken at the instruction boundary after each
 * chunk.
 *
 * 512 words is ~0.125 ms. Measured on the boot sequence (STPROF b61 counters):
 * unbounded = 107 starved frames and 448 lost band fires, worst frame missing
 * ALL ten; 2048 words = 10 and 10; 512 words = ZERO, matching the no-blitter
 * reference exactly. It is not a trade: real-speed boot-to-menu wall clock went
 * 371 s unbounded -> 356 s at 512 words, because the CPU is halted for the
 * whole of a HOG blit and the per-chunk cost is ~4 register writes per 512 word
 * moves.
 *
 * Chunked HOG rather than shared-bus (HOG clear): between chunks the blitter is
 * IDLE, so an interrupt handler that uses the blitter itself cannot corrupt a
 * transfer in flight. Sharing the bus would leave it exposed for the whole
 * copy. */
#define BLT_CHUNK_WORDS 512

/* Words per blit, 0 = unbounded. Set at init; a variable rather than a
 * constant so the A/B below can hold the binary identical. */
static unsigned short s_blt_chunk = BLT_CHUNK_WORDS;

/* One aligned word-copy. SUPERVISOR ONLY — never call this from user code. */
static void st_blt_copy(void *dst, const void *src, unsigned short words)
{
	unsigned char       *d = (unsigned char *)dst;
	const unsigned char *s = (const unsigned char *)src;

	BLT_SRC_XINC = 2;  BLT_SRC_YINC = 2;
	BLT_DST_XINC = 2;  BLT_DST_YINC = 2;
	BLT_END1     = 0xFFFF; BLT_END2 = 0xFFFF; BLT_END3 = 0xFFFF;
	BLT_HOP      = 2;                        /* HOP: source */
	BLT_OP       = 3;                        /* OP: D = S    */
	BLT_SKEW     = 0;                        /* no FXSR/NFSR, no shift */

	while (words) {
		unsigned short n = words;

		if (s_blt_chunk && n > s_blt_chunk)
			n = s_blt_chunk;

		BLT_SRC    = (unsigned long)(uintptr_t)s;
		BLT_DST    = (unsigned long)(uintptr_t)d;
		BLT_XCOUNT = n;
		/* ★ PER CHUNK, NOT ONCE. The blitter DECREMENTS Y count as it runs,
		 * so it reads 0 after a completed blit — hoisting this out of the
		 * loop leaves every chunk after the first with YCOUNT = 0 and
		 * corrupts the screen. Cost one word write; found by the first
		 * walk-screen render ever captured on STE. */
		BLT_YCOUNT = 1;
		BLT_CTRL   = 0xC0;               /* BUSY | HOG -> run to completion */
		while (BLT_CTRL & 0x80)
			;
		s     += (long)n * 2;
		d     += (long)n * 2;
		words -= n;
	}
}

/* Changed-row RUNS for one present. ST_H alternating rows is the worst case,
 * so ST_H/2 + 1 entries can never overflow. */
static struct { short y0, n; } s_runs[ST_H / 2 + 1];
static short s_nruns;
static int   s_use_blt;          /* plat_have_blitter(), latched at init */

/* Copy every recorded run, plane bytes + chunky shadow, with the BLiTTER.
 * SUPERVISOR — one Supexec for the whole present, which is the only way the
 * trap is cheap enough to be worth it (see st_blt_copy's note). */
static long st_dt_blit_runs_super(void)
{
	short i;

	for (i = 0; i < s_nruns; i++) {
		long y0 = s_runs[i].y0, n = s_runs[i].n;

		st_blt_copy(s_page[s_back] + y0 * LINE_BYTES,
		            s_dt + y0 * LINE_BYTES,
		            (unsigned short)(n * LINE_BYTES / 2));
		st_blt_copy(s_shadow + y0 * ST_W, s_chunky + y0 * ST_W,
		            (unsigned short)(n * ST_W / 2));
	}
	return 0;
}

/* Seed BOTH pages and both shadows from scratch. SUPERVISOR. */
static long st_dt_blit_forcefull_super(void)
{
	short pg;

	for (pg = 0; pg < NPAGES; pg++) {
		st_blt_copy(s_page[pg], s_dt, SCREEN_BYTES / 2);
		st_blt_copy(s_shadow_pg[pg], s_chunky,
		            (unsigned short)((long)ST_W * ST_H / 2));
	}
	return 0;
}

/* B4 step 4: present from s_dt (row-diffed copy), replacing the full c2p.
 * Mirrors st_blit_full's page/shadow/counter bookkeeping.
 *
 * ★ RUN-COALESCED (task #48). This used to issue two memcpy calls PER CHANGED
 * ROW — 160 plane bytes and 320 shadow bytes. Measured on an emulated STE, 200
 * separate 160-byte memcpys cost 38.3 ms against 20.5 ms for ONE 32000-byte
 * memcpy of exactly the same bytes: the per-call overhead was nearly half the
 * copy time. Changed rows are also overwhelmingly contiguous in practice (the
 * viewport, a text line, the HUD bar), so coalescing them into runs recovers
 * most of that — and it does so on every ST, blitter or not.
 *
 * With a BLiTTER present the runs then go through it instead, batched into a
 * single Supexec. Same measurement: 3.76x on a full-screen copy, but only
 * 1.41x on 160-byte rows because the ~15 register writes per blit dominate at
 * that size. Coalescing is what makes the blitter worth using here: it turns
 * many below-break-even blits into few above-break-even ones. */
/* #63 per-page pending rows. The shim reports which rows changed since the
 * last FULL present, but the two pages catch up independently — a row dirtied
 * while page A was the target still has to be rebuilt when page B next comes
 * round. So the report is folded into BOTH pages' sets, and a page clears only
 * its own as it handles them. This is the same invariant the per-page shadows
 * encode; the set just avoids having to READ 64000 bytes to rediscover it.
 * Starts all-ones so the first present of each page converts everything. */
#ifdef FRUA_DIRTYCHECK
long g_dirtycheck_miss;                 /* rows that changed unannounced */
#endif
static unsigned char s_pend[NPAGES][ST_H];
static short         s_pend_init;


static void st_pend_all(void)
{
	memset(s_pend, 1, sizeof s_pend);
	s_pend_init = 1;
}

/* Fold this frame's shim report into both pages. */
static void st_pend_gather(void)
{
	const unsigned char *drows;
	short pg, y;
#ifdef FRUA_STPROF
	long tg = Supexec(st_prof_hz200);
#endif

	if (!s_pend_init)
		st_pend_all();
	else if (planar_dirty_rows(&drows))          /* "scan everything" */
		st_pend_all();
	else
		for (y = 0; y < ST_H; y++)
			if (drows[y])
				for (pg = 0; pg < NPAGES; pg++)
					s_pend[pg][y] = 1;
#ifdef FRUA_STPROF
	sp_ph_gather += Supexec(st_prof_hz200) - tg;
#endif
}

/* #63 NEW-INK PATCH — the cheap answer to "an index the quant never saw".
 *
 * Give each recorded index the slot whose palette colour is nearest in RGB and
 * mark it seen. No re-partition, so EVERY already-mapped index keeps its slot:
 * the on-screen planes stay valid, there is no epoch reset and no force-full,
 * and nothing visibly reshuffles. Only the rows that actually carried the ink
 * are flagged for a rebuild.
 *
 * Strictly better than what the rows had, too: quant_banded's fallback for an
 * absent colour is nearest in LUMINANCE, which is what makes a distinctly
 * coloured glyph land on the background it happens to match in brightness —
 * the "invisible text after a re-band" this detector was written for. Nearest
 * in RGB cannot do that.
 *
 * Returns 1 if it handled the ink, 0 to fall back to a full re-quant. */
static int st_patch_new_ink(void)
{
	short c, s, y, patched = 0;

#ifdef FRUA_PALDIAG
	pk_calls++;
	if (s_ink_over)        pk_over++;
	else if (s_ink_n <= 0) pk_empty++;
	else if (!s_have_pal)  pk_nopal++;
#endif
	if (s_ink_over || s_ink_n <= 0 || !s_have_pal)
		return 0;
#ifdef FRUA_PALDIAG
	pk_ok++;
#endif

	for (c = 0; c < 256; c++) {
		short g;

		if (!s_ink_idx[c])
			continue;
		/* Nearest slot in EACH group's own palette — the groups hold
		 * different sixteens, so one answer cannot serve them all. */
		for (g = 0; g < s_ngrp; g++) {
			short best = 0;
			long  bestd = 0x7fffffffL;

			for (s = 0; s < ST_NCOL; s++) {
				long d = st_coldist(s_clut + (long)c * 3,
				                    s_gpal[g] + (long)s * 3);
				if (d < bestd) { bestd = d; best = s; }
			}
			s_grem[g][c] = (unsigned char)best;
			s_gused[g][c] = 1;
		}
		s_used_idx[c] = 1;
		patched++;
	}
	st_expand_groups();             /* the bands read by the c2p and the engine */
	if (!patched)
		return 0;
	s_remap_gen++;                           /* re-pointed indices: see s_vp_gen */

	/* Every row holding the patched index was stamped through the OLD
	 * mapping, so its planes are stale. We do NOT know which rows those are:
	 * the scan stops at four pixels (see the gate in st_dt_ready_row — losing
	 * it doubled in-present), so the recorded set would be partial, and a
	 * partial rebuild leaves the rest wrong FOREVER because the index is now
	 * marked seen and will never register as new ink again. So: rebuild the
	 * frame. That is still half the old cost — the re-quant, the epoch reset
	 * and the visible palette reshuffle are all gone, and only the rebuild
	 * remains, which is #63 item (1). */
	(void)y;
	s_force_full = 1;
#ifdef FRUA_PLANAR_DIAG
	dbg_log_num("b63ink: patched indices    = ", (long)patched);
#endif
	return 1;
}

static void st_dt_present_full(void)
{
	short y, pg, run0 = -1;
#ifdef FRUA_STPROF
	long rows_conv = 0, rows_skip = 0;
#endif

	if (s_force_full > 0) {
#ifdef FRUA_STPROF
		/* #63: the force-full branch `goto log`s past the pass-1 timer, so
		 * every reband's whole-frame rebuild landed in the UNATTRIBUTED
		 * remainder of in-present (46% of the boot). Name it. */
		long tff = Supexec(st_prof_hz200), tfc;
#endif
		for (y = 0; y < ST_H; y++)
#ifdef FRUA_STPROF
			if (st_dt_ready_row(y)) rows_conv++; else rows_skip++;
#else
			st_dt_ready_row(y);
#endif
		/* The single most expensive copy the backend does: 2 x 32000 plane
		 * bytes + 2 x 64000 shadow bytes, and the one shape where the
		 * BLiTTER measured a clear 3.76x. One Supexec for all four. */
#ifdef FRUA_STPROF
		/* #63(1): the 200 row builds and the 192 KB of copies are very
		 * different problems — row skipping can only touch the first.
		 * Split them before choosing. */
		sp_rb_ffrows += Supexec(st_prof_hz200) - tff;
		tfc = Supexec(st_prof_hz200);
#endif
		s_ink_fresh = 0;          /* one force-full only */
		if (s_use_blt)
			Supexec(st_dt_blit_forcefull_super);
		else
			for (pg = 0; pg < NPAGES; pg++) {
				memcpy(s_page[pg], s_dt, SCREEN_BYTES);
				memcpy(s_shadow_pg[pg], s_chunky, (size_t)ST_W * ST_H);
			}
		s_screen        = s_page[s_back];
		s_shadow        = s_shadow_pg[s_back];
		s_force_full    = 0;
		/* #61: this just rewrote BOTH pages from s_dt, which does not
		 * carry the composited viewport (the composite writes the PAGE,
		 * not s_dt). Both pages owe it again. */
		if (s_vp_have) {
			s_vp_owe[0] = 1;
			s_vp_owe[1] = 1;
		}
		memset(s_pend, 0, sizeof s_pend);   /* #63: both pages are current */
		s_pend_init     = 1;
#ifdef FRUA_STPROF
		sp_rb_ffcopy += Supexec(st_prof_hz200) - tfc;
		sp_rb_ffull  += Supexec(st_prof_hz200) - tff;
#endif
		goto log;
	}

	/* pass 1 — ready the changed rows and gather them into runs */
#ifdef FRUA_STPROF
	{
	long tp1 = Supexec(st_prof_hz200);
	long w0 = sp_ph_cmpwords, i0 = sp_ph_inkbytes, b0 = sp_ph_built;
#endif
	st_pend_gather();
	s_nruns = 0;
	for (y = 0; y < ST_H; y++) {
		const unsigned char *crow = s_chunky + (long)y * ST_W;
		/* #63: only rows a writer announced can have moved. Everything
		 * else keeps its shadow and is skipped without being read. */
		int changed = 0;

		if (s_pend[s_back][y]) {
			s_pend[s_back][y] = 0;
#ifdef FRUA_STPROF
			sp_ph_scanned++;
#endif
			changed = st_row_differs(crow, s_shadow + (long)y * ST_W);
		}
#ifdef FRUA_DIRTYCHECK
		/* THE POLICE. Re-run the old unconditional diff on the rows the
		 * set said to skip; any hit is a writer that changed a row without
		 * announcing it, which would leave that row stale on screen
		 * forever. Must read ZERO over a full drive before anyone trusts
		 * a narrowed scan. */
		else if (st_row_differs(crow, s_shadow + (long)y * ST_W)) {
			extern long g_dirtycheck_miss;
			g_dirtycheck_miss++;
			dbg_log_num("b63MISS unannounced row = ", (long)y);
			changed = 1;                     /* self-heal, then report */
		}
#endif
		if (changed) {
#ifdef FRUA_STPROF
			if (st_dt_ready_row(y)) { rows_conv++; s_prof_convrow[y]++; }
			else rows_skip++;
#else
			st_dt_ready_row(y);
#endif
			if (run0 < 0)
				run0 = y;
		}
		/* close the open run at the first unchanged row, or at the end */
		if (run0 >= 0 && (!changed || y == ST_H - 1)) {
			short last = changed ? y : (short)(y - 1);

			s_runs[s_nruns].y0 = run0;
			s_runs[s_nruns].n  = (short)(last - run0 + 1);
			s_nruns++;
			run0 = -1;
		}
	}
#ifdef FRUA_STPROF
	sp_ph_pass1 += Supexec(st_prof_hz200) - tp1;
	sp_p1_cmpwords += sp_ph_cmpwords - w0;
	sp_p1_inkbytes += sp_ph_inkbytes - i0;
	sp_p1_built    += sp_ph_built    - b0;
	sp_ph_conv_rows += rows_conv;
	sp_ph_chg_rows  += rows_conv + rows_skip;
	}
#endif
	if (s_nruns == 0)
		goto log;

	/* ★ #148: A RUN COPY ERASES THE VIEWPORT, EXACTLY AS THE FORCE-FULL DOES.
	 *
	 * #61 already found this hazard and fixed it for the force-full branch above
	 * — "this just rewrote BOTH pages from s_dt, which does not carry the
	 * composited viewport (the composite writes the PAGE, not s_dt). Both pages
	 * owe it again." The incremental path copies from the same s_dt and was never
	 * given the same treatment, and that path is the common case on the walk.
	 *
	 * What it looks like: s_dt's viewport columns are built from s_chunky, whose
	 * viewport hole is BLACK (measured: 0 of 88 columns non-zero against 88 of 88
	 * in the scratch), because ADR-0016 B2 keeps the viewport out of the shared
	 * surface on purpose. A row is only rebuilt when it CHANGES, so rows 24..102
	 * — static chrome beside a constant hole — never move, while the compass and
	 * the coordinate/clock box at rows ~103..125 tick. The result was nine black
	 * rows across the bottom of the 3D view, stopping at 111 where the viewport
	 * ends. It shipped.
	 *
	 * Re-arming here rather than at the build site is deliberate: s_dt PERSISTS
	 * with the hole in it, so a row built once can be re-copied on any later
	 * present with no build to notice. Hooking the build repaired the first copy
	 * and left every subsequent one broken — measured, one of three drives still
	 * showed the band. The copy is the event that damages the page, so the copy
	 * is what must re-arm. */
	{
		short i;

		for (i = 0; i < s_nruns; i++)
			st_vp_touched(s_runs[i].y0, s_runs[i].n);
	}

	/* pass 2 — one copy per run */
#ifdef FRUA_STPROF
	{
	long tcp = Supexec(st_prof_hz200);
#endif
	if (s_use_blt) {
		Supexec(st_dt_blit_runs_super);
	} else {
		short i;
		for (i = 0; i < s_nruns; i++) {
			long y0 = s_runs[i].y0, n = s_runs[i].n;

			memcpy(s_page[s_back] + y0 * LINE_BYTES,
			       s_dt + y0 * LINE_BYTES, (size_t)(n * LINE_BYTES));
			memcpy(s_shadow + y0 * ST_W, s_chunky + y0 * ST_W,
			       (size_t)(n * ST_W));
		}
	}
#ifdef FRUA_STPROF
	sp_ph_copy += Supexec(st_prof_hz200) - tcp;
	}
#endif
log:
#ifdef FRUA_STPROF
	{
		static long logs;
		if ((rows_conv | rows_skip) && logs < 48) {
			logs++;
			dbg_log_num("b4skip conv rows = ", rows_conv);
			dbg_log_num("b4skip skip rows = ", rows_skip);
		}
	}
#endif
	(void)0;
}

#ifdef FRUA_PLANAR_DIAG
/* Debug probe: after the present + composite, decode the SHOWN-to-be page at two
 * pixels — a viewport wall pixel and a roster text pixel — and log page slot vs
 * s_dt slot vs lut[chunky] + ownership. One short log per full present; the
 * dungeon blackout's divergence names itself here. */
static void st_dt_probe(const char *tag, short x, short y)
{
	short band = (short)((long)y * ST_NBANDS / ST_H);
	long  c    = (long)y * ST_W + x;
	long  v;

	/* pack cov,page,s_dt,want as decimal fields: c_PP_SS_WW */
	v = (long)(s_dt_cov[c] ? 1 : 0)            * 1000000L
	  + (long)st_slot_at(s_page[s_back], x, y) * 10000L
	  + (long)st_slot_at(s_dt, x, y)           * 100L
	  + (long)s_band_remap[(long)band * 256 + s_chunky[c]];
	dbg_log_num(tag, v);
}

/* Roster-NAME rect probe (surface y=26..38, x=115..300 — the whole "LADY ILLIS
 * -4 84" line, so a single-row placement error can't blind it). Counts pixels
 * holding the HUD text clut 23 and pixels differing from the panel reference,
 * and logs the remap slot + CLUT RGB of both indices. One reading separates the
 * three worlds:
 *   c23 == 0            -> text never drawn into chunky (engine-side ordering)
 *   c23 > 0, r23 == rref -> text present but remap-COLLAPSED (backend: widen
 *                           the split guard)
 *   c23 > 0, r23 != rref, RGBs equal -> engine painted grey-on-grey (faithful;
 *                           the baseline would show it too at this instant)   */
static void st_dt_probe_span(void)
{
	unsigned char ref = s_chunky[30L * ST_W + 240];
	const unsigned char *lut = s_band_remap
	    + (long)(30 * ST_NBANDS / ST_H) * 256;
	long c23 = 0, diff = 0;
	short x, y;

	for (y = 26; y <= 38; y++) {
		const unsigned char *row = s_chunky + (long)y * ST_W;
		for (x = 115; x < 300; x++) {
			if (row[x] == 23) c23++;
			if (row[x] != ref) diff++;
		}
	}
	dbg_log_num("b4span ref  = ", ref);
	dbg_log_num("b4span c23  = ", c23);
	dbg_log_num("b4span diff = ", diff);
	dbg_log_num("b4span r23  = ", lut[23]);
	dbg_log_num("b4span rref = ", lut[ref]);
	dbg_log_num("b4span rgb23= ", (long)s_clut[23 * 3] * 65536L
	    + (long)s_clut[23 * 3 + 1] * 256L + s_clut[23 * 3 + 2]);
	dbg_log_num("b4span rgbrf= ", (long)s_clut[(long)ref * 3] * 65536L
	    + (long)s_clut[(long)ref * 3 + 1] * 256L + s_clut[(long)ref * 3 + 2]);

	/* Name the culprit: the DOMINANT non-panel index in the rect (= the text
	 * ink when text is present), its slot, and the RGB that slot actually
	 * DISPLAYS (s_band_pal) vs the panel slot's. tdisp==pdisp with tcnt>0 is
	 * the palette-level collapse caught red-handed (text present in chunky,
	 * mapped to a slot displaying panel grey). */
	{
		static short hist[256];
		short t = 0;

		memset(hist, 0, sizeof hist);
		for (y = 26; y <= 38; y++) {
			const unsigned char *row = s_chunky + (long)y * ST_W;
			for (x = 115; x < 300; x++)
				if (row[x] != 23)
					hist[row[x]]++;
		}
		for (x = 1; x < 256; x++)
			if (hist[x] > hist[t])
				t = x;
		dbg_log_num("b4span tidx = ", t);
		dbg_log_num("b4span tcnt = ", hist[t]);
		dbg_log_num("b4span tslot= ", lut[t]);
		dbg_log_num("b4span tdisp= ", (long)s_band_pal[(long)lut[t] * 3] * 65536L
		    + (long)s_band_pal[(long)lut[t] * 3 + 1] * 256L
		    + s_band_pal[(long)lut[t] * 3 + 2]);
		dbg_log_num("b4span pdisp= ", (long)s_band_pal[(long)lut[23] * 3] * 65536L
		    + (long)s_band_pal[(long)lut[23] * 3 + 1] * 256L
		    + s_band_pal[(long)lut[23] * 3 + 2]);
	}
}
#endif /* FRUA_PLANAR_DIAG */
#endif /* FRUA_PLANAR */

/* --- backend entry points ------------------------------------------------ */

#ifdef FRUA_BLITBENCH
/* Task #48: is the BLiTTER worth wiring into the present path?
 *
 * Measures the THREE shapes st_dt_present_full actually issues, not an
 * abstract memory bandwidth number:
 *
 *   full   one 32000-byte plane copy   (the s_force_full path, x NPAGES)
 *   rows   200 x 160-byte plane copies (the row-diff path at worst case)
 *   shadow 200 x 320-byte chunky copies (the OTHER half of the row cost —
 *          included because the blitter can accelerate it too, and because
 *          if it dominates then speeding the plane copy alone is pointless)
 *
 * All blitter phases run inside ONE Supexec so the trap is amortised the way
 * a real batched present would do it; timing a per-row Supexec would measure
 * the trap, not the hardware. Reported in 200 Hz ticks (5 ms), with the
 * iteration count chosen to put each phase in the 1-2 s range. */
static void *s_bb_dst, *s_bb_src;

#define BB_FULL_ITERS   64
#define BB_ROW_ITERS    64
#define BB_SHADOW_ITERS 32

static long st_bb_read_hz200(void)
{
	return *(volatile long *)0x4BAUL;
}

static long st_bb_blit_full_super(void)
{
	short i;
	for (i = 0; i < BB_FULL_ITERS; i++)
		st_blt_copy(s_bb_dst, s_bb_src, SCREEN_BYTES / 2);
	return 0;
}

static long st_bb_blit_rows_super(void)
{
	short i, y;
	for (i = 0; i < BB_ROW_ITERS; i++)
		for (y = 0; y < ST_H; y++)
			st_blt_copy((char *)s_bb_dst + (long)y * LINE_BYTES,
			            (const char *)s_bb_src + (long)y * LINE_BYTES,
			            LINE_BYTES / 2);
	return 0;
}

static long st_bb_blit_shadow_super(void)
{
	short i, y;
	for (i = 0; i < BB_SHADOW_ITERS; i++)
		for (y = 0; y < ST_H; y++)
			st_blt_copy((char *)s_bb_dst + (long)y * ST_W,
			            (const char *)s_bb_src + (long)y * ST_W,
			            ST_W / 2);
	return 0;
}

static void st_blitbench(void)
{
	long t0, cpu_full, blt_full, cpu_rows, blt_rows, cpu_shad, blt_shad;
	short i, y;
	int have = plat_have_blitter();

	dbg_log_num("blitbench: plat_have_blitter = ", (long)have);
	s_bb_dst = (void *)plat_stram_alloc((long)ST_W * ST_H); /* 64000: every shape */
	s_bb_src = (void *)plat_stram_alloc((long)ST_W * ST_H);
	if (s_bb_dst == NULL || s_bb_src == NULL) {
		dbg_log("blitbench: Mxalloc FAILED — skipped");
		if (s_bb_dst) Mfree(s_bb_dst);
		if (s_bb_src) Mfree(s_bb_src);
		return;
	}
	memset(s_bb_src, 0xA5, (size_t)ST_W * ST_H);

	t0 = Supexec(st_bb_read_hz200);
	for (i = 0; i < BB_FULL_ITERS; i++)
		memcpy(s_bb_dst, s_bb_src, SCREEN_BYTES);
	cpu_full = Supexec(st_bb_read_hz200) - t0;

	t0 = Supexec(st_bb_read_hz200);
	for (i = 0; i < BB_ROW_ITERS; i++)
		for (y = 0; y < ST_H; y++)
			memcpy((char *)s_bb_dst + (long)y * LINE_BYTES,
			       (const char *)s_bb_src + (long)y * LINE_BYTES,
			       LINE_BYTES);
	cpu_rows = Supexec(st_bb_read_hz200) - t0;

	t0 = Supexec(st_bb_read_hz200);
	for (i = 0; i < BB_SHADOW_ITERS; i++)
		for (y = 0; y < ST_H; y++)
			memcpy((char *)s_bb_dst + (long)y * ST_W,
			       (const char *)s_bb_src + (long)y * ST_W, ST_W);
	cpu_shad = Supexec(st_bb_read_hz200) - t0;

	blt_full = blt_rows = blt_shad = -1;
	if (have) {
		t0 = Supexec(st_bb_read_hz200);
		Supexec(st_bb_blit_full_super);
		blt_full = Supexec(st_bb_read_hz200) - t0;

		t0 = Supexec(st_bb_read_hz200);
		Supexec(st_bb_blit_rows_super);
		blt_rows = Supexec(st_bb_read_hz200) - t0;

		t0 = Supexec(st_bb_read_hz200);
		Supexec(st_bb_blit_shadow_super);
		blt_shad = Supexec(st_bb_read_hz200) - t0;

		/* Correctness matters as much as speed: a fast wrong copy is
		 * worthless. Verify the last blit actually moved the bytes. */
		dbg_log_num("blitbench: verify (0=OK) = ",
		            (long)memcmp(s_bb_dst, s_bb_src, (size_t)ST_W * ST_H));
	}

	dbg_log_num("blitbench iters full/rows/shadow = ",
	            (long)BB_FULL_ITERS * 1000000L + (long)BB_ROW_ITERS * 1000L
	            + BB_SHADOW_ITERS);
	dbg_log_num("blitbench full  cpu 200Hz ticks = ", cpu_full);
	dbg_log_num("blitbench full  blt 200Hz ticks = ", blt_full);
	dbg_log_num("blitbench rows  cpu 200Hz ticks = ", cpu_rows);
	dbg_log_num("blitbench rows  blt 200Hz ticks = ", blt_rows);
	dbg_log_num("blitbench shadw cpu 200Hz ticks = ", cpu_shad);
	dbg_log_num("blitbench shadw blt 200Hz ticks = ", blt_shad);

	Mfree(s_bb_dst); s_bb_dst = NULL;
	Mfree(s_bb_src); s_bb_src = NULL;
}
#endif /* FRUA_BLITBENCH */

static int st_init(short want_w, short want_h)
{
	(void)want_w; (void)want_h;

	s_screen_raw = (unsigned char *)plat_stram_alloc(NPAGES * SCREEN_BYTES + 256); /* ST-RAM */
	s_chunky     = (unsigned char *)plat_stram_alloc((long)ST_W * ST_H);
	s_shadow_raw = (unsigned char *)plat_stram_alloc((long)NPAGES * ST_W * ST_H);
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

#ifdef FRUA_BOOTTRACE
	dbg_file_str("bt:", "st_init: allocs+pages done");
#endif
	s_save_rez  = Getrez();
	s_save_phys = Physbase();
	s_save_log  = Logbase();
	/* ...AND THE DESKTOP'S 16 COLOUR REGISTERS. Restoring the resolution and the
	 * screen base alone put the desktop back with OUR palette still loaded —
	 * reported from real hardware as black icons and black window contents after
	 * quitting. This backend owns 0xFF8240 outright (the per-band raster split
	 * rewrites all 16 registers several times per frame), so whatever the desktop
	 * had is long gone by exit unless it is saved here. Same idiom the Falcon
	 * backend already uses (Setcolor inquire -> Setpalette). */
	{
		short i;
		for (i = 0; i < 16; i++)
			s_save_pal[i] = Setcolor(i, COL_INQUIRE);
	}
	Setscreen(s_save_log, s_page[0], 0);     /* ST Low; show page 0; console keeps log */
	dbg_log_screen_owned();   /* see the videl backend: keep Cconws off the picture */
#ifdef FRUA_BOOTTRACE
	dbg_file_str("bt:", "st_init: Setscreen done (screen black now)");
#endif

	s_surface.width  = ST_W;
	s_surface.height = ST_H;
	s_surface.pitch  = ST_W;
	s_surface.pixels = s_chunky;
	s_dirty    = 1;
	s_have_pal = 0;
	st_tb_ptr = &st_tb_tab[0];               /* valid before the first fire */

	/* Install the raster split: a VBL slot (re-phases Timer B + loads band 0)
	 * and Timer B in event-count mode firing every ST_RPB display lines. */
	Supexec(st_vbl_install_super);
#ifdef FRUA_BOOTTRACE
	dbg_file_str("bt:", "st_init: VBL installed");
#endif
	Xbtimer(1, 8, ST_RPB, st_timerb_trampoline);   /* timer B, event count */
#ifdef FRUA_BOOTTRACE
	dbg_file_str("bt:", "st_init: Timer-B armed (split LIVE)");
#endif
	/* #63: Xbtimer both installs the vector AND enables the channel, so the
	 * split is LIVE from here. The VBL owns arming from now on and will stop
	 * it on the first frame that finds the bands uniform — which, until a
	 * per-band quantizer returns, is every frame. */
	s_tb_live    = 1;
	s_tb_uniform = 1;
	s_ints_on = 1;

	/* Take over the dungeon-viewport composite (ADR-0016 B2). */
	s_vp_active = 0;
	s_st_active = 1;
	/* The cache holds palettes cut against a CLUT that a re-init has no reason
	 * to preserve, and the statics survive one. Zeroed at load, so this only
	 * matters the second time through — which is exactly when a stale entry
	 * would be hardest to explain. */
	st_pc_flush();
	planar_viewport_register(st_vp_scratch, st_vp_commit);
	planar_viewport_planes_register(st_vp_planes_buf, st_vp_commit_planes);
	planar_reband_query_register(st_reband_pending, st_vp_chunky_valid);
	planar_viewport_overwrite_register(st_vp_overwrite);

#ifdef FRUA_PLANAR
	/* Draw-time plane accumulation buffer + hook (ADR-0016 B4). */
	s_dt        = (unsigned char *)plat_stram_alloc(SCREEN_BYTES);
	s_dt_cov    = (unsigned char *)plat_stram_alloc((long)ST_W * ST_H);
	s_dt_idx    = (unsigned char *)plat_stram_alloc((long)ST_W * ST_H);
	s_dt_rowcov = (short *)plat_stram_alloc(ST_H * sizeof(short));
	if (s_dt != NULL)
		memset(s_dt, 0, SCREEN_BYTES);
	if (s_dt_cov != NULL)
		memset(s_dt_cov, 0, (size_t)ST_W * ST_H);
	if (s_dt_idx != NULL)
		memset(s_dt_idx, 0, (size_t)ST_W * ST_H);
	if (s_dt_rowcov != NULL)
		memset(s_dt_rowcov, 0, ST_H * sizeof(short));
	planar_draw_target_register(st_dt_target);
#ifdef FRUA_BOOTTRACE
	dbg_file_str("bt:", "st_init: planar buffers up");
#endif
#endif

#ifdef FRUA_PLANAR
	/* #48: latch BLiTTER availability once. Guarded because the run and
	 * force-full blit paths that read s_use_blt are planar-only — the
	 * chunky 020 targets never reach them, and the variable itself only
	 * exists under FRUA_PLANAR. */
	s_use_blt = plat_have_blitter();
	dbg_log_num("ste: blitter = ", (long)s_use_blt);
#endif

#ifdef FRUA_BLITBENCH
	st_blitbench();
#endif

#ifdef FRUA_STPROF
	/* B3.0b scratch: a non-displayed ST-RAM page the c2p can target for timing. */
	s_offpage = (unsigned char *)plat_stram_alloc(SCREEN_BYTES);
	if (s_offpage != NULL)
		memset(s_offpage, 0, SCREEN_BYTES);
#endif

	/* ★ THE A/B ARM, SELECTABLE WITHOUT A REBUILD. `vpplanar=off` in video.cfg
	 * puts the viewport back on the chunky scratch + c2p composite, so a
	 * before/after can be measured with ONE binary — the same code, the same
	 * layout, the same alignment. Two builds would differ in more than the thing
	 * under test, which is how a performance claim quietly becomes a claim about
	 * codegen. Same reader as display_nova.c's novalut key. */
	{
		char  buf[192];
		short fh = (short)Fopen("video.cfg", 0);

		if (fh >= 0) {
			long n = Fread(fh, (long)sizeof buf - 1, buf);
			int  i;

			Fclose(fh);
			if (n > 0) {
				buf[n] = '\0';
				for (i = 0; buf[i] != '\0'; i++)
					if (buf[i] >= 'A' && buf[i] <= 'Z')
						buf[i] = (char)(buf[i] + 32);
				if (strstr(buf, "vpplanar=off") != NULL) {
					st_planar_viewport = 0;
					dbg_log("ste: viewport planes DISABLED (video.cfg)");
				}
				/* #139: give the viewport's rows their own cut.
				 * Same reasoning as vpplanar — ONE binary, both
				 * arms, so an A/B compares the split and nothing
				 * else. */
				/* Both directions, so a config written against
				 * either default keeps meaning what it says. */
				if (strstr(buf, "vpbands=off") != NULL) {
					st_vp_bands = 0;
					dbg_log("ste: viewport palette group DISABLED (video.cfg)");
				} else if (strstr(buf, "vpbands=on") != NULL) {
					st_vp_bands = 1;
					dbg_log("ste: viewport palette group ENABLED (video.cfg)");
				}
				/* #139: reuse a palette already cut for this CLUT
				 * instead of re-cutting it. Both keys, same reason
				 * as vpbands. */
				/* #148: repaint the viewport after a copy that
				 * crossed its rows. Off = the shipped bug, for
				 * the control arm. */
				if (strstr(buf, "vprepair=off") != NULL) {
					st_vp_repair = 0;
					dbg_log("ste: viewport repair DISABLED (video.cfg)");
				} else if (strstr(buf, "vprepair=on") != NULL) {
					st_vp_repair = 1;
					dbg_log("ste: viewport repair ENABLED (video.cfg)");
				}
				if (strstr(buf, "palcache=off") != NULL) {
					st_pal_cache = 0;
					dbg_log("ste: palette cache DISABLED (video.cfg)");
				} else if (strstr(buf, "palcache=on") != NULL) {
					st_pal_cache = 1;
					dbg_log("ste: palette cache ENABLED (video.cfg)");
				}
			}
		}
	}
	dbg_log("ste: ST-low 320x200x4 16-colour, per-band Timer-B palette up");
	return 0;
}

static void st_shutdown(void)
{
	if (s_st_active) {
		planar_reband_query_register((int (*)(void))0, (void (*)(short))0);
		planar_viewport_planes_register((unsigned char *(*)(short *))0,
		                                (void (*)(short, short, short, short))0);
		planar_viewport_register((unsigned char *(*)(short *))0,
		                         (void (*)(short, short, short, short))0);
		planar_viewport_overwrite_register(
		    (void (*)(short, short, short, short))0);
#ifdef FRUA_PLANAR
		planar_draw_target_register((int (*)(struct dsp_planar_dt *))0);
		if (s_dt)        { Mfree(s_dt); s_dt = NULL; }
		if (s_dt_cov)    { Mfree(s_dt_cov); s_dt_cov = NULL; }
		if (s_dt_idx)    { Mfree(s_dt_idx); s_dt_idx = NULL; }
		if (s_dt_rowcov) { Mfree(s_dt_rowcov); s_dt_rowcov = NULL; }
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
		/* Palette AFTER the mode change, matching the Falcon backend's default
		 * exit order: Setscreen can reinitialise the VDI (and with it its colour
		 * table), so putting the registers back last gives them the final word.
		 * Without this the desktop returned wearing the game's palette — black
		 * icons and black window contents on real hardware. Interrupts are
		 * already down above, so nothing repaints over it. */
		Setpalette(s_save_pal);
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
	if (w <= 0 || h <= 0) { s_vp_active = 0; s_vp_have = 0; return; }
	if (x < 0 || y < 0 || x + w > VP_MAX || y + h > VP_MAX) {
		s_vp_active = 0;                 /* out of the buffer's reach: skip */
		s_vp_have   = 0;
		return;
	}
	s_vp_x = x; s_vp_y = y; s_vp_w = w; s_vp_h = h;
	s_vp_active = 1;
	s_vp_planar = 0;                         /* chunky commit: convert as before */
	s_vp_have   = 1;
	s_vp_owe[0] = 1;                         /* #61: EVERY page owes it */
	s_vp_owe[1] = 1;
}

/* A screen blit landed on rect (x,y,w,h) of the shared chunky surface. If it
 * touches the committed viewport, the scratch is history — drop it, or the
 * next force-full re-arms the owes (see l1649) and the composite paints the
 * STALE 3D view back over whatever the blit drew. That was the ST wrong-event-
 * picture bug: the caravan bigpic replaced the corridor in chunky/s_dt, the
 * event's palette install triggered reband -> force-full -> re-arm, and every
 * subsequent present wore the corridor through the new palette's remap.
 *
 * ANY overlap invalidates, not just full coverage: compositing the whole
 * scratch would clobber the blit's pixels wherever they intersect. The cost of
 * over-invalidating is small and self-healing — the next engine 3D render
 * re-commits a fresh scratch (every walk step does) — while under-invalidating
 * is this bug. Partial overlap does leave the UNCOVERED part of the old
 * viewport to whatever s_dt/chunky holds (the composite never wrote s_dt, so
 * that can be pre-viewport content), which is strictly less wrong than the
 * whole rect showing a stale frame, and the event pictures that trigger this
 * in practice cover the viewport box entirely. */
static void st_vp_overwrite(short x, short y, short w, short h)
{
	if (!s_vp_have || w <= 0 || h <= 0)
		return;
	if (x >= (short)(s_vp_x + s_vp_w) || (short)(x + w) <= s_vp_x ||
	    y >= (short)(s_vp_y + s_vp_h) || (short)(y + h) <= s_vp_y)
		return;                          /* disjoint: scratch still valid */
	s_vp_have   = 0;
	s_vp_active = 0;
	s_vp_owe[0] = 0;
	s_vp_owe[1] = 0;
}

/* Convert EIGHT chunky pixels straight into their four ST-Low plane bytes.
 *
 * An ST-Low 16-pixel group is four consecutive words, one per plane, and each
 * word's high byte is pixels 0-7 of the group, its low byte pixels 8-15. So an
 * 8-pixel column that starts on a multiple of 8 lands on FOUR WHOLE BYTES —
 * `(x >> 4) * 8` picks the group, `(x >> 3) & 1` the half, and plane p is at
 * +2p. No read-modify-write and no masking: every destination bit belongs to
 * this span, which is the whole reason the aligned path can exist. Bit 7 of a
 * byte is its leftmost pixel, matching c2p4st_32's bit-15-is-leftmost. */
static void st_c2p8(const unsigned char *src, const unsigned char *lut,
                    unsigned char *dstrow, short x)
{
	unsigned char *g = dstrow + (long)(x >> 4) * 8 + ((x >> 3) & 1);
	unsigned char pb[4];

	/* #90: the 8px edge column through the word-parallel half-group c2p — the
	 * scalar per-bit scatter this replaces was ~45% of the walk composite
	 * (unaligned 88px viewport = 3 of these columns per row). c2p4st_8 fills
	 * plane 0..3 bytes; the ST-Low group places them at the +0/+2/+4/+6 words. */
	c2p4st_8(src, lut, pb);
	g[0] = pb[0]; g[2] = pb[1]; g[4] = pb[2]; g[6] = pb[3];
}

/* #63: chunky viewport -> ST-Low planes, straight into each page.
 *
 * Replaces a two-stage scalar path that was costing 4.75 s of emulated time per
 * dungeon step: chunky -> separate planes one bit at a time (1.03 s), then
 * planar_blit_stlow shifting those planes into the interleaved screen one bit
 * per pixel per plane, twice (3.72 s, ~1900 cycles PER PIXEL). Both stages
 * disappear here — there is no intermediate buffer, no memset, and the 32-pixel
 * bulk goes through the same c2p4st_32 the full-frame path uses, flat-span fast
 * path included.
 *
 * Requires an 8-pixel-aligned x and width; the caller checks and keeps the old
 * path for anything else.
 *
 * #90: composite a 16-ALIGNED span, not the raw viewport, so every row is fast
 * c2p4st_32 blocks and the scalar 8px edge columns disappear. Those columns
 * measured ~43% of the composite and were OVERHEAD-bound (528 calls/present +
 * strided single-byte video stores), so aligning is a bigger win than any
 * faster edge c2p. The alignment adds up to 15px of the frame border on each
 * side; those pixels come from s_chunky (the live surface, static chrome here)
 * so the composited border is byte-identical to what is already on screen — the
 * viewport interior still comes from the scratch. The live viewport is 88x88 at
 * (24,24): the span is x=16..112 (96px = three 32-blocks), so the left edge
 * gains an 8px border strip and the right edge is already aligned.
 *
 * Shadows are deliberately untouched, exactly as before: the viewport's rows in
 * s_chunky are frozen, the row-diff therefore skips them, and this write is
 * what makes the planes right. Both pages are converted because a full present
 * flips, and the other page's hole would otherwise show a stale viewport. */
static void st_vp_composite_fast(void)
{
	short ax   = (short)(s_vp_x & ~15);              /* 16-aligned span start */
	short aend = (short)((s_vp_x + s_vp_w + 15) & ~15);
	short lx0 = ax, lx1 = s_vp_x;                     /* left border  [lx0,lx1) */
	short rx0 = (short)(s_vp_x + s_vp_w), rx1 = aend; /* right border [rx0,rx1) */
	short pg, r, x;

	/* Seed the alignment-added border strips into the scratch from the live
	 * surface (page-independent, so once — not per page). Empty when the edge
	 * is already 16-aligned. */
	for (r = 0; r < s_vp_h; r++) {
		short yy = (short)(s_vp_y + r);
		unsigned char       *scr = s_vp_scratch + (long)yy * VP_SCR_PITCH;
		const unsigned char *chk = s_chunky + (long)yy * ST_W;
		for (x = lx0; x < lx1; x++) scr[x] = chk[x];
		for (x = rx0; x < rx1; x++) scr[x] = chk[x];
	}

	/* ★ CONVERT ONCE, REPLICATE THE BYTES (#142). The transpose is
	 * PAGE-INDEPENDENT: `scr` is the shared scratch and `lut` is
	 * s_band_remap + band*256 where band = yy * ST_NBANDS / ST_H, so for a given
	 * (row, x) c2p4st_32 computes byte-identical plane words for page 0 and page
	 * 1. Running the `for (pg...)` loop around the conversion therefore did the
	 * whole c2p TWICE to produce two copies of the same bytes — half the walk's
	 * composite, spent recomputing a known answer.
	 *
	 * The slow path in this same file has always done it the right way ("c2p once
	 * + blit twice", per st_vp_composite's own comment); only the fast path
	 * carried the duplicate. Now it converts into page 0 and copies that row's
	 * span to the others, which is 48 bytes (12 longs) against three 32-pixel
	 * transposes — the copy is a small fraction of what it replaces.
	 *
	 * The span is contiguous and wholly ours: block x writes 8 words at
	 * (x >> 4) * 8, so [ax, aend) maps to bytes [(ax>>4)*8, (aend>>4)*8) — for
	 * the live 96px viewport, drow+8 .. drow+56. The trailing st_c2p8 column
	 * writes inside that same range, so copying after both loops covers it. */
	{
		long sb = (long)(ax   >> 4) * 8;         /* first byte of the span */
		long se = (long)(aend >> 4) * 8;         /* one past the last      */

		for (r = 0; r < s_vp_h; r++) {
			short yy   = (short)(s_vp_y + r);
			short band = (short)((long)yy * ST_NBANDS / ST_H);
			const unsigned char *lut = s_band_remap + (long)band * 256;
			const unsigned char *scr =
			    s_vp_scratch + (long)yy * VP_SCR_PITCH;
			unsigned char *drow = s_page[0] + (long)yy * LINE_BYTES;

			for (x = ax; x + 32 <= aend; x = (short)(x + 32)) {
				const unsigned char *sp = scr + x;
				unsigned short *d =
				    (unsigned short *)(drow + (long)(x >> 4) * 8);

				if (c2p4st_is_flat(sp, 32)) {
					c2p4st_32_flat(sp[0], lut, d);
#ifdef FRUA_STPROF
					sp_vp_flat++;
#endif
				} else {
					c2p4st_32(sp, lut, d);
#ifdef FRUA_STPROF
					sp_vp_tex++;
#endif
				}
			}
			/* Trailing 16px group (only for an exotic non-32-multiple aligned
			 * width; the live 96px viewport never reaches here). */
			while (x < aend) {
				st_c2p8(scr + x, lut, drow, x);
#ifdef FRUA_STPROF
				sp_vp_col8++;
#endif
				x = (short)(x + 8);
			}

			/* Replicate this row's converted span to the other page(s), while
			 * it is still warm. Long-wise: the span is 8-byte aligned by
			 * construction and its length is a multiple of 8. */
			for (pg = 1; pg < NPAGES; pg++) {
				const unsigned long *sr =
				    (const unsigned long *)(drow + sb);
				unsigned long *dr = (unsigned long *)
				    (s_page[pg] + (long)yy * LINE_BYTES + sb);
				long n = (se - sb) >> 2;

				while (n-- > 0)
					*dr++ = *sr++;
			}
		}
	}
}

/* Convert the committed rect (chunky, per-band remap) to ST-Low planes and drop
 * it into the viewport hole. One-shot: cleared after compositing so a later
 * full recompose (menu/combat) that did NOT re-commit leaves the surface alone.
 * Called at the tail of every present. */
static void st_vp_composite_slow(void)
{
	unsigned char *pp[ST_DEPTH];
	long planebytes;
	short r, c, p;
#ifdef FRUA_STPROF
	long t0 = Supexec(st_prof_hz200);
#endif

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

#ifdef FRUA_STPROF
	sp_vp_conv += Supexec(st_prof_hz200) - t0;
#endif
	/* B4: drop the converted viewport into BOTH pages' holes, not just the one being
	 * drawn. The commit is one-shot (a full present that flips to the other page would
	 * otherwise show that page's stale/black viewport hole), and the viewport is tiny
	 * (88x88) so blitting it twice is cheap. */
	{
		short pg;
#ifdef FRUA_STPROF
		long tb = Supexec(st_prof_hz200);
#endif
		for (pg = 0; pg < NPAGES; pg++)
			planar_blit_stlow(pp, VP_PLANE_STRIDE, s_vp_w, s_vp_h, ST_DEPTH,
			                  s_page[pg], LINE_BYTES, ST_W, ST_H,
			                  s_vp_x, s_vp_y);
#ifdef FRUA_STPROF
		sp_vp_blit += Supexec(st_prof_hz200) - tb;
#endif
	}
}

/* --- ADR-0016 B5: engine-stamped planes ---------------------------------- */

static unsigned char *st_vp_planes_buf(short *pitch)
{
	if (!st_planar_viewport)
		return (unsigned char *)0;       /* A/B arm: chunky scratch as before */
	if (pitch)
		*pitch = LINE_BYTES;
	return s_vp_ilv;
}

static void st_vp_commit_planes(short x, short y, short w, short h)
{
	st_vp_commit(x, y, w, h);                /* same bookkeeping and rejects */
	if (s_vp_active) {
		s_vp_planar = 1;
		s_vp_gen    = s_remap_gen;
	}
}

/*
 * Copy the engine's planes into every page's hole.
 *
 * ★ THE EDGE GROUPS ARE MERGED, NOT COPIED. A 16-pixel group is the smallest
 * unit of ST-Low plane storage, and the viewport's left edge (x = 24) sits in
 * the MIDDLE of group 1 — pixels 16..23 of that group belong to the chrome
 * around the hole. Copying whole groups would blank eight columns of the stone
 * frame every frame. So each group carries a mask of the pixels we own; a fully
 * owned group is two long stores, a partial one is four masked word merges.
 * With the live 88x88 viewport at x=24 that is exactly one partial group per
 * row and five whole ones.
 */
static void st_vp_composite_copy(void)
{
	short gx0 = (short)(s_vp_x >> 4);
	short gx1 = (short)(((s_vp_x + s_vp_w) + 15) >> 4);
	unsigned short gm[(VP_MAX / 16) + 2];
	short g, r, pg, i;
#ifdef FRUA_STPROF
	long t0 = Supexec(st_prof_hz200);
#endif

	if (gx1 - gx0 > (short)(sizeof gm / sizeof gm[0]))
		return;                          /* cannot happen: VP_MAX bounds it */
	for (g = gx0; g < gx1; g++) {
		short lo = (short)(g * 16);
		short a  = (s_vp_x > lo) ? s_vp_x : lo;
		short b  = ((short)(s_vp_x + s_vp_w) < (short)(lo + 16))
		         ? (short)(s_vp_x + s_vp_w) : (short)(lo + 16);
		unsigned short m = 0;

		for (i = (short)(a - lo); i < (short)(b - lo); i++)
			m |= (unsigned short)(0x8000u >> i);
		gm[g - gx0] = m;
	}

	for (r = 0; r < s_vp_h; r++) {
		short yy = (short)(s_vp_y + r);
		const unsigned char *srow = s_vp_ilv + (long)yy * LINE_BYTES;

		for (pg = 0; pg < NPAGES; pg++) {
			unsigned char *drow = s_page[pg] + (long)yy * LINE_BYTES;

			for (g = gx0; g < gx1; g++) {
				unsigned short m = gm[g - gx0];
				const unsigned short *sp =
				    (const unsigned short *)(srow + (long)g * 8);
				unsigned short *dp =
				    (unsigned short *)(drow + (long)g * 8);

				if (m == 0xFFFFu) {
					((unsigned long *)dp)[0] =
					    ((const unsigned long *)sp)[0];
					((unsigned long *)dp)[1] =
					    ((const unsigned long *)sp)[1];
				} else if (m) {
					for (i = 0; i < ST_DEPTH; i++)
						dp[i] = (unsigned short)
						    ((dp[i] & ~m) | (sp[i] & m));
				}
			}
		}
	}
#ifdef FRUA_STPROF
	sp_vp_blit += Supexec(st_prof_hz200) - t0;
#endif
}

static void st_vp_composite(void)
{
#ifdef FRUA_STPROF
	long t0;
#endif

	/* #61: per PAGE, not per commit. s_screen names the page the caller is
	 * writing — the back page for a full present, the shown one for a rect. */
	{
		short pg = (s_screen == s_page[1]) ? 1 : 0;

		if (!s_vp_have || !s_vp_owe[pg])
			return;
		/* ★ THE DOUBLE-REDRAW FIX (#90). Both composite paths below write the
		 * viewport into BOTH pages in this single call (fast: c2p per page;
		 * slow: c2p once + blit twice — the B4 "drop into BOTH pages" comment).
		 * So clear BOTH owes here, not just this page's. The per-page clear is a
		 * leftover from #61, when the composite touched only one page: it left
		 * the OTHER page owing, so the SECOND present of every walk step (l63c0
		 * issues jt312's qd_present_rect then a qd_present) re-ran the whole
		 * both-page composite — a full redundant viewport c2p per step, ~30% of
		 * the walk redraw. Both pages are still current (the composite did both),
		 * so #61's "no stale page on a flip" guarantee is preserved; we just stop
		 * doing it twice. */
		s_vp_owe[0] = 0;
		s_vp_owe[1] = 0;
	}
	s_vp_active = 0;                         /* unchanged for the other users */
	if (!s_have_pal)
		return;                          /* no palette yet: nothing to map */
#ifdef FRUA_STPROF
	t0 = Supexec(st_prof_hz200);
#endif
	/* #63: measured 2026-07-27, this was the dungeon walk's ENTIRE display
	 * cost — 4.75 s of emulated time per step, 31-36% of the step. The
	 * aligned path is the same work through the fast c2p; the slow one stays
	 * for a commit the fast one cannot take (none is issued today).
	 *
	 * Two conditions, both of which the live viewport meets: 8-pixel aligned
	 * (so every destination byte is wholly ours) and wholly on screen. The
	 * second is what planar_blit_stlow was paying a per-pixel bounds test for;
	 * st_vp_commit already rejects anything outside VP_MAX, which is smaller
	 * than the screen either way, so this is belt-and-braces against a future
	 * caller that widens the viewport. */
	/* ★ SAY WHICH PATH IS LIVE. A copy composite that silently never engaged
	 * would fall back to the c2p and produce a PIXEL-IDENTICAL screen — the one
	 * failure this change cannot be caught by looking at. One marker each, first
	 * use only, so DBG.LOG answers it instead of an assumption. */
	if (s_vp_planar && s_vp_gen != s_remap_gen) {
		/* ★ SAY WHEN THE GUARD FIRES. A generation check that never triggers is
		 * indistinguishable from one that works, so count the falls back to the
		 * conversion instead of inferring them from a clean frame. */
		static short said_stale;

		s_vp_gen_stale++;
		if (!said_stale) {
			said_stale = 1;
			dbg_log("ste: viewport planes STALE (remap moved) -> c2p");
		}
	}
	if (s_vp_planar && s_vp_gen == s_remap_gen) {
		static short said;

		if (!said) { said = 1; dbg_log("ste: viewport composite = COPY (B5)"); }
		s_vp_copy_n++;
		st_vp_composite_copy();          /* B5: engine stamped the planes */
	}
	else if (((s_vp_x | s_vp_w) & 7) == 0
	    && s_vp_x >= 0 && s_vp_y >= 0
	    && (short)(s_vp_x + s_vp_w) <= ST_W
	    && (short)(s_vp_y + s_vp_h) <= ST_H) {
		static short said;

		if (!said) { said = 1; dbg_log("ste: viewport composite = c2p (fast)"); }
		st_vp_composite_fast();
	} else {
		static short said;

		if (!said) { said = 1; dbg_log("ste: viewport composite = c2p (slow)"); }
		st_vp_composite_slow();
	}
	/* ★ THE BALANCE, not just "both paths fired". If the generation guard sends
	 * most presents to the conversion, B5's copy is largely notional and the
	 * planar composite wants re-examining rather than defending. Every 32
	 * composites — these are per present, not per frame. */
	{
		static long bn;

		/* First call AND every 64th: "no output" and "a reading of zero" are the
		 * same text, and a threshold above the event count has produced silence
		 * I misread as data five times in this work. The first-call line makes
		 * the two distinguishable for free. */
		if ((++bn) == 1 || (bn & 63) == 0) {
			dbg_log_num("ste: vp COPY presents = ", s_vp_copy_n);
			dbg_log_num("ste: vp c2p fallbacks = ", s_vp_gen_stale);
		}
	}

#ifdef FRUA_STPROF
	sp_vp_t += Supexec(st_prof_hz200) - t0;
	sp_vp_n++;
#endif
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
static short sp_tbcost_done;            /* #63 raster-split A/B bench: once */
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

/* #63: what does the per-band raster split actually COST?
 *
 * Times the SAME fixed workload — 16 full-frame c2p passes to the off-screen
 * page — with Timer B armed and with it stopped, one after the other in a
 * single boot. Both arms are the same binary at the same instant on the same
 * content, so neither BSS layout (the #91 trap) nor scene differences can
 * confound it; the only variable is the interrupt. Reported in 200 Hz ticks
 * (5 ms) rather than TickCount's 60 Hz, for 3x the resolution on a ~2 s arm.
 *
 * The arming override goes through s_tb_force and is applied by the VBL, so
 * the hardware is only touched from supervisor code that already owns it. Two
 * VBLs are allowed to pass after each switch before timing starts. */
static void st_prof_tb_settle(void)
{
	long t = Supexec(st_prof_hz200);

	while (Supexec(st_prof_hz200) - t < 8L)   /* 40 ms = 2 frames  */
		;
}

static long st_prof_tb_arm(short on, long *fires)
{
	long a, f, t, i;
	const long reps = 16;

	s_tb_force = on;
	st_prof_tb_settle();                     /* the VBL applies it, 2 frames  */
	f = sp_tb_total;
	a = Supexec(st_prof_hz200);
	for (i = 0; i < reps; i++)
		st_c2p_page(s_offpage);
	t = Supexec(st_prof_hz200) - a;
	*fires = sp_tb_total - f;
	return t;
}

static void st_prof_tbcost(void)
{
	long on1, off, on2, f_on1, f_off, f_on2;

	if (s_offpage == NULL)
		return;
	/* ★ THIS INSTRUMENT NO LONGER MEASURES WHAT IT USED TO, AND IT MUST SAY SO.
	 * It arms Timer B by force to price the split, which worked when the timer
	 * fired at a fixed cadence regardless of content. It now fires on a
	 * SCHEDULE built from the palette groups, so forcing it on a uniform screen
	 * arms a timer with nothing to do — st_tb_first is 255, which cannot
	 * underflow inside 200 display lines — and the A/B would report the cost of
	 * an idle timer as the cost of the split. Refuse rather than print it. The
	 * measurement that replaced it is the wall-clock fixture A/B with
	 * vpbands=on vs off, which is one binary and needs no forcing. */
	if (st_tb_nfire <= 0) {
		dbg_log("stprof tb: no schedule (uniform palettes) - not priced");
		return;
	}

	/* A/B/A. The arms run back to back in one boot, so the only thing that
	 * could still confound them is ORDER (a warm-up effect, an unrelated
	 * background cost that fades). Repeating the ON arm at the end prices
	 * that: if on1 and on2 agree, the middle number is the split's own cost. */
	on1 = st_prof_tb_arm(1, &f_on1);
	off = st_prof_tb_arm(0, &f_off);
	on2 = st_prof_tb_arm(1, &f_on2);
	s_tb_force = -1;                         /* back to the uniformity decision */

	/* #63 CALIBRATION. The present-phase split blames the 200-row memcmp scan
	 * for 76% of a present, which works out at ~156 cycles per byte compared —
	 * absurd for any memcmp, and the same ~10x over-expectation the c2p and the
	 * composite both show. So price the primitive directly, in the same clock,
	 * before believing any of it: 16 sweeps of the real 64000-byte surface, row
	 * by row exactly as pass 1 does it. If this lands near 150 cycles/byte then
	 * memcmp genuinely is the cost and the scan is the bug; if it lands near 10
	 * then the phase timer is inflated and the attribution is wrong.
	 *
	 * ★ THE B-SIDE USED TO BE s_offpage, WHICH IS SCREEN_BYTES (32000) — and
	 * both arms index it to y=199, i.e. 64000 bytes, running 32 KB off the end
	 * of an Mxalloc block on every sweep. No MMU, so it never faulted and the
	 * numbers looked fine. s_shadow_pg[] is ST_W*ST_H, the size this actually
	 * wants. The published memcmp-vs-longcmp RATIO is unaffected (both arms
	 * read the identical bytes, before and after), which is the part the
	 * shipped change rests on. */
	{
		long a, tm, tl, i;
		short y;
		volatile long sink = 0;
		const unsigned char *s_cal_b = s_shadow_pg[NPAGES - 1];

		if (s_cal_b == NULL)
			goto skip_cal;

		/* Arm A: exactly what pass 1 runs — 200 row memcmps, 16 times. */
		a = Supexec(st_prof_hz200);
		for (i = 0; i < 16; i++)
			for (y = 0; y < ST_H; y++)
				sink += memcmp(s_chunky + (long)y * ST_W,
				               s_cal_b + (long)y * ST_W, ST_W) != 0;
		tm = Supexec(st_prof_hz200) - a;

		/* Arm B: the same comparison as a LONG-wise loop. Both buffers are
		 * Mxalloc'd (long-aligned) and ST_W is a multiple of 4, so this is a
		 * straight 80-iteration compare per row. A ratio, deliberately: it is
		 * immune to whatever the absolute cycles-per-byte puzzle turns out to
		 * be, because both arms carry it equally. */
		a = Supexec(st_prof_hz200);
		for (i = 0; i < 16; i++)
			for (y = 0; y < ST_H; y++) {
				const long *p = (const long *)(s_chunky + (long)y * ST_W);
				const long *q = (const long *)(s_cal_b + (long)y * ST_W);
				short w, diff = 0;

				for (w = 0; w < ST_W / 4; w++)
					if (p[w] != q[w]) { diff = 1; break; }
				sink += diff;
			}
		tl = Supexec(st_prof_hz200) - a;

		dbg_log_num("stprof cal: memcmp  x16 t200 = ", tm);
		dbg_log_num("stprof cal: longcmp x16 t200 = ", tl);
		/* (t/16) ticks * 5 ms * 8 MHz / 64000 bytes = t / 25.6, so t*5/128. */
		dbg_log_num("stprof cal: memcmp  cyc/byte = ", tm * 5L / 128L);
		dbg_log_num("stprof cal: longcmp cyc/byte = ", tl * 5L / 128L);
	}
skip_cal:

	dbg_log_num("stprof tb: uniform bands     = ", (long)s_tb_uniform);
	dbg_log_num("stprof tb: ON  #1 x16 t200   = ", on1);
	dbg_log_num("stprof tb: OFF    x16 t200   = ", off);
	dbg_log_num("stprof tb: ON  #2 x16 t200   = ", on2);
	/* Three lines, not one packed number: an arm fires ~13500 times, so the
	 * obvious f_on1*1000000 encoding silently WRAPS a 32-bit long. */
	dbg_log_num("stprof tb: fires ON  #1      = ", f_on1);
	dbg_log_num("stprof tb: fires OFF         = ", f_off);
	dbg_log_num("stprof tb: fires ON  #2      = ", f_on2);
	dbg_log_num("stprof tb: tax per 1000      = ",
	            off > 0 ? (((on1 + on2) / 2 - off) * 1000L) / off : -1L);
	/* Cycles per band interrupt, the sanity check on the tax: (ticks saved)
	 * x 5 ms x 8 MHz / fires. A plain ISR — entry, two movems, one line of
	 * spin — should land near 600; a number in the thousands says the spin
	 * is running long, which is a bug in the handler, not a cost of banding. */
	if (f_on1 > 0)
		dbg_log_num("stprof tb: cycles per fire   = ",
		            ((on1 - off) * 40000L) / f_on1);
}

/* work units x (bench t200 / bench units), scaled to survive a 32-bit long:
 * sp_p1_cmpwords reaches millions and a bench arm a few hundred t200, so the
 * naive product overflows. Divide both sides down first — the ratio is what
 * matters and the counts are far too coarse for the lost precision to show. */
static long sp_model(long work, long cal, long units)
{
	if (units <= 0)
		return 0;
	while (work > 100000L && units >= 10) {
		work  /= 10;
		units /= 10;
	}
	return (work * cal) / units;
}

/* #63 PASS-1 CALIBRATION. One-shot, ~6 s emulated.
 *
 * Prices each unit of pass-1 work ONCE, so the exact work counters
 * (sp_p1_cmpwords / sp_p1_inkbytes / sp_p1_built) can be turned into t200 and
 * subtracted from the measured pass1. Whatever is left is the residual — the
 * thing this whole exercise is trying to name — and it is named by ARITHMETIC
 * rather than by 20000 Supexec traps.
 *
 * Why a bench is exact here: the 68000 has NO CACHE. There is no warm/cold
 * distinction to get wrong, so a loop over the real buffers costs what the same
 * loop costs inside a present. (This is the one measurement style that would
 * NOT transfer to the 030 targets, which is fine — this is the ST/STe path.)
 *
 * Each arm runs the REAL function, not a copy of its body, so the numbers stay
 * honest if the function changes. */
static void st_prof_ph1cal(void)
{
	long a, i, saved_words, saved_ink, saved_built;
	short y;
	volatile long sink = 0;
	const long reps = 8;

	if (s_chunky == NULL || s_dt == NULL || s_shadow_pg[NPAGES - 1] == NULL)
		return;

	/* The bench drives the very counters it is calibrating. */
	saved_words = sp_ph_cmpwords;
	saved_ink   = sp_ph_inkbytes;
	saved_built = sp_ph_built;

	/* A: st_row_differs, full 80 words (equal rows). 8 x 200 calls.
	 *
	 * ★ THE FIRST CUT PASSED THE SAME POINTER TWICE AND MEASURED 6 t200 —
	 * 0.5 cycles a byte, an impossibility that is the whole reason to sanity-
	 * check a bench against the machine. st_row_differs is static and small,
	 * so GCC inlines it; with a == b it proves p[w] == q[w] and folds the
	 * loop away, leaving only the counter increment (which it must keep, so
	 * the WORK counter still looked plausible). Compare against a real second
	 * buffer holding a copy: equal, so the full 80 words run, but the equality
	 * is a runtime fact the compiler cannot see. */
	memcpy(s_shadow_pg[NPAGES - 1], s_chunky, (size_t)ST_W * ST_H);
	a = Supexec(st_prof_hz200);
	for (i = 0; i < reps; i++)
		for (y = 0; y < ST_H; y++)
			sink += st_row_differs(s_chunky + (long)y * ST_W,
			                       s_shadow_pg[NPAGES - 1]
			                           + (long)y * ST_W);
	sp_cal_cmp = Supexec(st_prof_hz200) - a;

	/* B: the new-ink scan, byte for byte as ready_row runs it. */
	a = Supexec(st_prof_hz200);
	for (i = 0; i < reps; i++)
		for (y = 0; y < ST_H; y++) {
			const unsigned char *p   = s_chunky + (long)y * ST_W;
			const unsigned char *end = p + ST_W;
			const unsigned char *tab = s_used_idx;
			long                 ink = 0;

			while (p < end)
				if (!tab[*p++])
					ink++;
			sink += ink;
		}
	sp_cal_ink = Supexec(st_prof_hz200) - a;

	/* C: st_dt_build_row — the span c2p plus the ownership self-heal.
	 * It MUTATES s_dt/s_dt_idx/s_dt_cov, but into exactly the state the
	 * invariant wants (dt == remap[chunky], fully owned), which is what a
	 * force-full leaves behind. Force one anyway so neither that nor arm A's
	 * clobbered shadow is inherited by the next present. */
	a = Supexec(st_prof_hz200);
	for (i = 0; i < reps; i++)
		for (y = 0; y < ST_H; y++)
			st_dt_build_row(y);
	sp_cal_bld = Supexec(st_prof_hz200) - a;
	s_force_full = 1;

	/* D: the loop FLOOR — 200 iterations of the pend test with nothing
	 * pending. Not a faithful copy of pass 1's body (no run bookkeeping),
	 * so read it as a lower bound on the fixed per-present overhead. */
	a = Supexec(st_prof_hz200);
	for (i = 0; i < reps; i++)
		for (y = 0; y < ST_H; y++)
			if (s_pend[0][y])
				sink++;
	sp_cal_loop = Supexec(st_prof_hz200) - a;

	sp_ph_cmpwords = saved_words;
	sp_ph_inkbytes = saved_ink;
	sp_ph_built    = saved_built;
	(void)sink;
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

/* Show the just-drawn back page and make the old front the next draw target.
 *
 * CORRECTED 2026-07-26 (#61 investigation). This comment used to claim "the
 * engine presents a full recompose NPAGES times (the `pages` contract), so both
 * pages end up carrying the frame". That is FALSE and was worth catching, since
 * it describes a coherence mechanism that is not running: ste_backend declares
 * `pages = 1` (deliberately — see its own comment, and #151), so the engine
 * presents ONCE and only the back page receives the full frame. Anyone
 * debugging a stale-page artefact from the old comment would look in the wrong
 * place. The three mechanisms that ACTUALLY keep the pages coherent are:
 *
 *   1. PER-PAGE shadows (s_shadow_pg[]). A page's row-diff runs against its own
 *      shadow, so whatever the other page missed still reads as changed and is
 *      rebuilt the next time that page is the target. This is what makes
 *      present_rect's in-place draw safe: it updates the SHOWN page and the
 *      SHOWN page's shadow, leaving the back page's shadow correctly stale.
 *   2. s_force_full is a per-page COUNT, not a flag. A re-band sets it so BOTH
 *      pages get seeded — necessary because a re-band renumbers slots and
 *      invalidates the other page's planes wholesale.
 *   3. st_vp_composite blits the viewport into BOTH pages explicitly. It has to:
 *      the viewport rows are frozen in s_chunky, so the row-diff would never
 *      notice the other page's hole was stale.
 *
 * present_rect does NOT flip — it draws the SHOWN page in place (see
 * st_present_rect), so the walk's small viewport update never desyncs the pages
 * (an earlier flip-on-present_rect showed the back page's stale/blank HUD). */
static void st_flip_full(void)
{
	s_flip_target = s_page[s_back];
	Supexec(st_flip_super);
	s_shown = s_back;
	s_back ^= 1;                             /* the old front is the next target */
}

#ifdef FRUA_STPROF
/* #41 hot-row dump: rows still paying span conversions this 64-present window,
 * encoded y*10000+count (threshold 2 keeps transition noise out of the log
 * budget; the summary line counts every touched row regardless). */
static void st_prof_hot_dump(void)
{
	short y, logs = 0;
	long  touched = 0;
	/* #48 follow-up: the per-row dump above is a SAMPLE (count >= 2, first 24
	 * rows). These are the totals — of every row that converted this window,
	 * how many did so because no native writer covered it (a coverage hole,
	 * why bit 0) versus because a direct writer overwrote shim-stamped pixels
	 * (a stamp mismatch, why bit 1)? That split decides where the remaining
	 * native-writer work goes: holes mean "convert another writer", mismatches
	 * mean "an existing writer is being clobbered". */
	long  why_hole = 0, why_mismatch = 0, why_both = 0;

	for (y = 0; y < ST_H; y++) {
		if (s_prof_convrow[y] > 0)
			touched++;
		if (s_prof_convwhy[y] == 1)      why_hole++;
		else if (s_prof_convwhy[y] == 2) why_mismatch++;
		else if (s_prof_convwhy[y] == 3) why_both++;
		if (s_prof_convrow[y] >= 2 && logs < 24) {
			logs++;
			dbg_log_num("b4hot y*100000+why*10000+cnt = ",
			            (long)y * 100000L
			            + (long)s_prof_convwhy[y] * 10000L
			            + s_prof_convrow[y]);
			if (s_prof_convwhy[y] & 2)
				dbg_log_num("b4hot   mismatch y*1000+x = ",
				            (long)y * 1000L + s_prof_mmx[y]);
		}
	}
	dbg_log_num("b4hot rows touched = ", touched);
	dbg_log_num("b61 frames              = ", sp_frames);
	dbg_log_num("b61 starved frames      = ", sp_starved);
	dbg_log_num("b61 band fires lost     = ", sp_fires_lost);
	dbg_log_num("b61 worst fires in frame= ", sp_fires_min);
	dbg_log_num("b4why hole-only rows     = ", why_hole);
	dbg_log_num("b4why mismatch-only rows = ", why_mismatch);
	dbg_log_num("b4why both rows          = ", why_both);
	memset(s_prof_convrow, 0, sizeof s_prof_convrow);
	memset(s_prof_convwhy, 0, sizeof s_prof_convwhy);
	memset(s_prof_mmx, 0, sizeof s_prof_mmx);
}
#endif

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
#ifdef FRUA_STPROF
	{
	long tb0 = Supexec(st_prof_hz200);
#endif
	if (s_dirty) {
		/* B1: only re-band when the CLUT actually moved since the last one;
		 * a matching CLUT would reproduce the same band palettes. */
		if (s_banded_valid &&
		    memcmp(s_clut, s_clut_banded, sizeof s_clut) == 0) {
#ifdef FRUA_PALTRACE
			dbg_log("pt: reband SKIP (clut unchanged)");
#endif
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
			/* #63: was memcmp over all 64000 bytes. memcmp costs 93
			 * cycles/byte on this target where the long-wise loop
			 * costs ~53 — the same substitution that halved pass 1,
			 * never applied here. Identical answer, and it runs on
			 * EVERY dirty present, not just the ones that reband. */
			int content_same = s_banded_valid && !s_vp_active &&
			    !st_buf_differs(s_chunky, s_shadow, (long)ST_W * ST_H) &&
			    !st_remap_split();  /* a CLUT load that SPLITS a merged
			                         * slot invalidates the remap even
			                         * with content unchanged (the
			                         * grey-on-grey HUD-text family) */
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
				/* #89: WHICH of the four conjuncts vetoed the cheap path?
				 * "content_same is false" is not actionable on its own —
				 * a stale band, a pending viewport, real redraw and a
				 * remap split need completely different fixes. Encoded
				 * decimal so one line carries all four:
				 *   1000s !s_banded_valid   100s s_vp_active
				 *     10s content differs     1s st_remap_split() */
				dbg_log_num("b4audit:   veto b/v/c/s  = ",
				            (long)(!s_banded_valid) * 1000
				            + (long)(s_vp_active != 0) * 100
				            + (long)(crows != 0) * 10
				            + (long)(s_banded_valid && st_remap_split()));
				dbg_log(content_same ? "b4audit:   -> repalette (registers only)"
				                     : "b4audit:   -> reband (re-quant)");
			}
#endif
			if (content_same) {
#ifdef FRUA_PALDIAG
				pk_repal++;
#endif
				st_repalette();
			} else {
#ifdef FRUA_PALDIAG
				pk_reband++;
#endif
#ifdef FRUA_STPROF
				/* #89 thread 2: a PARTIAL epoch reset is only worth
				 * building if a reband typically leaves most of the
				 * index->slot map alone. Measure it: snapshot band 0's
				 * remap, re-quant, then count how many indices landed
				 * on a different slot. If nearly all move, row
				 * ownership could not have survived anyway and the
				 * partial reset is dead on arrival. */
				unsigned char sp_before[256];
				memcpy(sp_before, s_band_remap, 256);
#endif
				st_reband();
#ifdef FRUA_STPROF
				{
					short i, moved = 0, used = 0, umoved = 0;
					for (i = 0; i < 256; i++) {
						if (sp_before[i] != s_band_remap[i])
							moved++;
						if (s_used_idx[i]) {
							used++;
							if (sp_before[i] != s_band_remap[i])
								umoved++;
						}
					}
					dbg_log_num("b4audit:   idx slot moved= ", moved);
					dbg_log_num("b4audit:   used idx      = ", used);
					dbg_log_num("b4audit:   used moved    = ", umoved);
				}
#endif
			}
		}
	}
#ifdef FRUA_STPROF
	sp_ph_band += Supexec(st_prof_hz200) - tb0;
	}
#endif
#ifdef FRUA_PLANAR
#ifdef FRUA_PLANAR_DIAG
	st_dt_selfcheck();                       /* diag: owned == c2p (before build) */
#endif
	if (s_have_pal)
		st_dt_present_full();            /* step 4: flip-copy s_dt, NO full c2p */
	else
		st_blit_full();
#else
	st_blit_full();
#endif
	st_vp_composite();                       /* overlay the planar viewport */
#ifdef FRUA_PLANAR
#ifdef FRUA_PLANAR_DIAG
	/* Probes AFTER the composite, on the page about to be shown: viewport wall
	 * pixel (60,60) and roster text row pixel (180,30). Fields c_PP_SS_WW =
	 * cov, page slot, s_dt slot, remap[chunky]. */
	if (s_have_pal) {
		st_dt_probe("b4probe vp(60,60)  = ", 60, 60);
		st_dt_probe("b4probe hud(180,30)= ", 180, 30);
		st_dt_probe_span();
	}
#endif
	/* NEW-INK re-quant trigger (see st_dt_build_row): enough converted pixels
	 * carried indices the last quant never saw (post-re-band inks riding the
	 * luma fallback -> invisible text). Schedule a re-quant: s_dirty alone is
	 * NOT enough — the CLUT-guard would skip it (the CLUT did not change; the
	 * CONTENT did), so invalidate the banded snapshot too. The re-quant's own
	 * s_used_idx refresh then covers the ink, so this cannot loop. */
	if (s_have_pal && s_banded_valid && s_dt_new_ink > 0) {
#ifdef FRUA_PLANAR_DIAG
		dbg_log_num("b4ink: new-ink px, n = ", s_dt_new_ink);
#endif
		/* #63: PATCH FIRST, re-quant only if the patch declines. This
		 * used to go straight to `s_dirty = 1; s_banded_valid = 0`,
		 * i.e. a full re-quant plus a whole-frame rebuild, for ink that
		 * had not moved the CLUT by a single byte. */
		{
			int patched = st_patch_new_ink();
#ifdef FRUA_PALTRACE
			dbg_log_num("pt: newink n = ", s_dt_new_ink);
			dbg_log(patched ? "pt: newink PATCHED"
			                : "pt: newink DECLINED -> requant");
#endif
			if (!patched) {
				s_dirty        = 1;
				s_banded_valid = 0;
			}
		}
	}
#ifdef FRUA_PALDIAG
	if (pk_true_n > pk_true_max)
		pk_true_max = pk_true_n;
	if      (pk_true_n >= 65) pk_b65up++;
	else if (pk_true_n >= 49) pk_b49_64++;
	else if (pk_true_n >= 33) pk_b33_48++;
	else if (pk_true_n >= 25) pk_b25_32++;
	pk_true_n = 0;
	pk_present++;
	if (pk_present >= pk_next) {
		pk_next = pk_present + 64;
		pk_dump();
	}
#endif
	s_dt_new_ink = 0;
	if (s_ink_n) {
		memset(s_ink_idx, 0, sizeof s_ink_idx);
		s_ink_n = 0;
	}
	s_ink_over = 0;
#endif
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
	{
		/* Per-present cost, first 48 presents — the flip-vs-c2p comparison
		 * number (b30a's row counts don't cover the flip path, which never
		 * calls st_blit_rows). Same boot sequence, both builds, diff these. */
		long in_ticks = TickCount() - t0;

		sp_in += in_ticks;
		sp_ph_n++;
		if (sp_n < 48)
			dbg_log_num("b4perf present ticks = ", in_ticks);
	}
	sp_rows = g_stprof_rows;
	/* B3.0b fires on the first few FULL presents once a palette exists — full
	 * presents are rare (scene/menu recomposes; dungeon walk uses rect presents),
	 * so gating on a 64-present boundary rarely triggers. The c2p cost is
	 * content-independent, so any handful of samples answers compute-vs-contention. */
	if (s_have_pal && sp_b30b_done < 6) {
		sp_b30b_done++;
		st_prof_b30b();
	}
	if (s_have_pal && !sp_tbcost_done) {     /* #63: one-shot, ~4 s emulated */
		sp_tbcost_done = 1;
		st_prof_tbcost();
	}
	if (s_have_pal && !sp_ph1cal_done) {     /* #63: one-shot, ~6 s emulated */
		sp_ph1cal_done = 1;
		st_prof_ph1cal();
	}
	/* Window was 64 presents, which a scripted headless drive NEVER reaches
	 * (a boot + dungeon + short walk produces under 20), so the hot-row and
	 * why-attribution dumps below had effectively never fired. 16 still
	 * averages out transition noise but actually reports. */
	if ((++sp_n & 15) == 0) {
		dbg_log_num("stprof: presents = ", sp_n);
		dbg_log_num("stprof: wall ticks = ", TickCount() - sp_wall0);
		dbg_log_num("stprof: in-present ticks = ", sp_in);
		dbg_log_num("stprof: rows converted = ", sp_rows);
		dbg_log_num("stprof: rebands = ", sp_reband);
		dbg_log_num("stprof: reband skips = ", sp_reband_skip);
		/* #63: WHERE does a full present's ~1.6 s go? All t200. `pass1` is
		 * the 200-row diff plus every st_dt_ready_row it triggers (the
		 * new-ink scan and any conversion); `copy` is the run blits; `band`
		 * is the reband/repalette branch. in-present minus the three is the
		 * composite, the flip and the profiler's own overhead. */
		dbg_log_num("b63pr: presents        = ", sp_ph_n);
		dbg_log_num("b63pr: in-present t200 = ", sp_in * 10 / 3);
		dbg_log_num("b63pr:   band   t200   = ", sp_ph_band);
		dbg_log_num("b63pr:   pass1  t200   = ", sp_ph_pass1);
		dbg_log_num("b63pr:   of which gather= ", sp_ph_gather);
		dbg_log_num("b63pr: rows SCANNED     = ", sp_ph_scanned);
		dbg_log_num("b63pr:   copy   t200   = ", sp_ph_copy);
		dbg_log_num("b63pr:   vpcomp t200   = ", sp_vp_t);
		dbg_log_num("b63pr:   vpcomp COUNT  = ", sp_vp_n);
		dbg_log_num("b63pr: rows changed    = ", sp_ph_chg_rows);
		dbg_log_num("b63pr: rows converted  = ", sp_ph_conv_rows);
		/* #63 REBAND SPLIT — where the boot's `band` + force-full time goes.
		 * vpcopy/quant/used/align sum to st_reband; ffull is the whole-frame
		 * rebuild each reband forces afterwards. */
		dbg_log_num("b63rb: rebands run     = ", sp_rb_n);
		dbg_log_num("b63rb:   no-skew (skipped)= ", sp_rb_noskew);
		dbg_log_num("b63rb:   vp overlay    = ", sp_rb_vpcopy);
		dbg_log_num("b63rb:   quant_banded  = ", sp_rb_quant);
		dbg_log_num("b63rb:   used_idx scan = ", sp_rb_used);
		dbg_log_num("b63rb:   slot align    = ", sp_rb_align);
		dbg_log_num("b63rb:   FORCE-FULL    = ", sp_rb_ffull);
		dbg_log_num("b139pc: cache HITS    = ", sp_pc_hit);
		dbg_log_num("b139pc: cache misses  = ", sp_pc_miss);
		dbg_log_num("b148: vp repaints forced= ", sp_vp_rearm);
			dbg_log_num("b63rb:     ff rowbuilds= ", sp_rb_ffrows);
			dbg_log_num("b63rb:     ff copies   = ", sp_rb_ffcopy);
		/* #63 PASS-1 ATTRIBUTION. Exact work counters x calibrated unit
		 * costs. `residual` is pass1 minus gather minus all four: if it
		 * is near zero the model is complete and pass 1 is fully named;
		 * if it stays large the cost is NOT in the primitives and the
		 * next place to look is the loop around them. Every line is t200
		 * so they subtract directly. */
		if (sp_ph1cal_done) {
			long b_cmp  = sp_model(sp_p1_cmpwords, sp_cal_cmp,
			                       8L * ST_H * (ST_W / 4));
			long b_ink  = sp_model(sp_p1_inkbytes, sp_cal_ink,
			                       8L * ST_H * ST_W);
			long b_bld  = sp_model(sp_p1_built, sp_cal_bld,
			                       8L * ST_H);
			long b_loop = sp_model(sp_ph_n * (long)ST_H, sp_cal_loop,
			                       8L * ST_H);

			dbg_log_num("b63a: cal cmp  x8 t200 = ", sp_cal_cmp);
			dbg_log_num("b63a: cal ink  x8 t200 = ", sp_cal_ink);
			dbg_log_num("b63a: cal bld  x8 t200 = ", sp_cal_bld);
			dbg_log_num("b63a: cal loop x8 t200 = ", sp_cal_loop);
			/* PASS-1 SHARE first (what the model uses), then the
			 * since-boot totals — the gap between them IS the
			 * force-full branch, which pass 1 must not be charged. */
			dbg_log_num("b63a: p1 words compared= ", sp_p1_cmpwords);
			dbg_log_num("b63a: p1 ink bytes     = ", sp_p1_inkbytes);
			dbg_log_num("b63a: p1 rows built    = ", sp_p1_built);
			dbg_log_num("b63a: ALL words compared= ", sp_ph_cmpwords);
			dbg_log_num("b63a: ALL ink bytes    = ", sp_ph_inkbytes);
			dbg_log_num("b63a: ALL rows built   = ", sp_ph_built);
			dbg_log_num("b63a: MODEL cmp  t200  = ", b_cmp);
			dbg_log_num("b63a: MODEL ink  t200  = ", b_ink);
			dbg_log_num("b63a: MODEL bld  t200  = ", b_bld);
			dbg_log_num("b63a: MODEL loop t200  = ", b_loop);
			dbg_log_num("b63a: RESIDUAL t200    = ",
			            sp_ph_pass1 - sp_ph_gather
			            - b_cmp - b_ink - b_bld - b_loop);
		}
#ifdef FRUA_DIRTYCHECK
		{ extern long g_dirtycheck_miss;
		  dbg_log_num("b63pr: UNANNOUNCED rows = ", g_dirtycheck_miss); }
#endif
		{	/* #63: which shim write path marks the surface touched? */
			extern long g_qdt_hits[8];
			dbg_log_num("b63qdt: 0 pointer grab = ", g_qdt_hits[0]);
			dbg_log_num("b63qdt: 1 pixmap fill  = ", g_qdt_hits[1]);
			dbg_log_num("b63qdt: 2 CopyBits     = ", g_qdt_hits[2]);
			dbg_log_num("b63qdt: 3 set_palette  = ", g_qdt_hits[3]);
			dbg_log_num("b63qdt: 4 cursor track = ", g_qdt_hits[4]);
			dbg_log_num("b63qdt: 5 DrawChar     = ", g_qdt_hits[5]);
			dbg_log_num("b63qdt: 6 SKIPPED clean = ", g_qdt_hits[6]);
			/* #63: per-call-site grab counts, encoded line*100000+hits
			 * so one line carries both. Cross-reference against
			 * src/engine/boot.c — 25 of the 28 sites live there. */
			/* TWO lines per site. Packing line*100000+hits overflows
			 * a 32-bit long the moment a boot.c five-digit line number
			 * shows up — the same encoding trap as the band-fire counts. */
			{ extern long g_qdt_site[32][2]; int i;
			  for (i = 0; i < 32 && g_qdt_site[i][0]; i++) {
				dbg_log_num("b63site line = ", g_qdt_site[i][0]);
				dbg_log_num("b63site hits = ", g_qdt_site[i][1]);
			  } }
		}
		st_prof_hot_dump();              /* #41: hot-row attribution window */
		st_prof_b30b();                  /* B3.0b: compute-vs-contention sample */
#ifdef FRUA_SNDPROF
		{ extern void plat_sound_prof_dump(void); plat_sound_prof_dump(); }
#endif
	}
#endif
	st_flip_full();                          /* B4: show this page, advance */
}

/* #63: the walk's per-step display cost, and what share of the step it is.
 * Dumped every 8 rect presents (a walk step is one), so a short scripted drive
 * actually reports — unlike the 16-FULL-present window, which a dungeon walk
 * never reaches. */
#ifdef FRUA_STPROF
static void st_prof_play_dump(void)
{
	long now  = Supexec(st_prof_hz200);
	long wall = (sp_play_t0 < 0) ? 0 : now - sp_play_t0;

	sp_play_t0 = now;
	{ extern long g_jt312_render_n;   /* #90: CUMULATIVE 3D renders (not reset) —
	                                   * drive-independent; halves with the
	                                   * two-redraw fix vs FRUA_NO2REDRAW. */
	  dbg_log_num("b63play: jt312 RENDERS  = ", g_jt312_render_n); }
	dbg_log_num("b63play: rect presents  = ", sp_rect_n);
	dbg_log_num("b63play: rect t200      = ", sp_rect_t);
	dbg_log_num("b63play: composites     = ", sp_vp_n);
	dbg_log_num("b63play: vp w*1000+h    = ", (long)s_vp_w * 1000L + s_vp_h);
	dbg_log_num("b63play:   of which c2p = ", sp_vp_conv);
	dbg_log_num("b63play:   of which blit= ", sp_vp_blit);
	/* Stage A/C sizing: the fast-composite span census. NOTE since #142 this is
	 * ONE page's blocks, not two: the fast composite converts once and copies the
	 * bytes to the other page, so a per-composite total that used to read 528 now
	 * reads 264. Halving here is the fix landing, not the census breaking.
	 * flat 32-blocks = the floor/ceiling FILLS (Stage A target); textured =
	 * the WALL tiles (Stage C). A big flat share means Stage A is already cheap
	 * (c2p4st_32_flat) and the win is in the walls. */
	dbg_log_num("b63play:   flat32 blocks = ", sp_vp_flat);
	dbg_log_num("b63play:   tex32  blocks = ", sp_vp_tex);
	dbg_log_num("b63play:   col8   spans  = ", sp_vp_col8);
	dbg_log_num("b63play: composite t200 = ", sp_vp_t);   /* SUBSET of rect  */
	dbg_log_num("b63play: wall t200      = ", wall);
	/* The number the lever choice turns on: per mille of wall clock spent
	 * inside the display layer. Small => the ceiling is the engine's own 3D
	 * render and c2p tuning cannot reach it. rect ALONE — the composite runs
	 * inside it on the walk path, so adding the two would double-count. */
	if (wall > 0)
		dbg_log_num("b63play: display per 1000= ",
		            (sp_rect_t * 1000L) / wall);
#ifdef FRUA_SNDPROF
	/* #96/#95: the synth's own bucket counts, on the WALK path. The
	 * full-present dump never fires here, which is exactly how the walk's
	 * sound cost stayed invisible for as long as its display cost did. */
	{ extern void plat_sound_prof_dump(void); plat_sound_prof_dump(); }
#endif
#ifdef FRUA_MULPROF
	{ extern void mul_prof_dump(void); mul_prof_dump(); }
#endif
	sp_rect_n = sp_rect_t = sp_vp_n = sp_vp_t = 0;
	sp_vp_conv = sp_vp_blit = 0;
	sp_vp_flat = sp_vp_tex = sp_vp_col8 = 0;
}
#endif

static void st_present_rect(short x, short y, short w, short h)
{
	short x1;
#ifdef FRUA_STPROF
	long t_rect0 = Supexec(st_prof_hz200);

	if (sp_play_t0 < 0)
		sp_play_t0 = t_rect0;
#endif

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
		return;                          /* clipped away: not a display cost */

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
#ifdef FRUA_STPROF
		sp_rect_t += Supexec(st_prof_hz200) - t_rect0;
		if ((++sp_rect_n & 7) == 0)
			st_prof_play_dump();
#endif
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
	sp_rect_t += Supexec(st_prof_hz200) - t_rect0;
	if ((++sp_rect_n & 7) == 0)
		st_prof_play_dump();
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
#ifdef FRUA_PALTRACE
	/* #141: the speckle is the RGB/luma fallback showing, so trace every event
	 * that can move the remap and diff a GOOD run against a BAD one. Per
	 * palette event, not per frame — these are rare. */
	dbg_log_num("pt: setpal first*1000+count = ",
	            (long)first * 1000 + (long)count);
#endif
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
	0,                      /* hw_palette: NO — 4 planes hold a quantised 16-slot
	                         * value, not the index, so a CLUT move can invalidate
	                         * converted pixels. (Explicit, to reach the field
	                         * below; the value is the historical default.) */
#ifdef FRUA_STE_PALBLANKET
	0,                      /* A/B arm: the historical blanket row mark. */
#else
	1,                      /* #63 palette_self_invalidates — the ECS fix
	                         * (3cb6c121) applied here, the chain being identical:
	                         * st_set_palette's `count >= 32` sets s_dirty, and
	                         * st_present turns that into st_repalette() or
	                         * st_reband(), whose tail sets s_force_full and
	                         * rebuilds BOTH pages — a branch that bypasses pass 1
	                         * entirely. A smaller write sets nothing, and pass 1
	                         * compares chunky against its shadow, which a palette
	                         * write does not touch. So the blanket row mark can
	                         * only ever make pass 1 re-read 200 unchanged rows.
	                         * g_qd_touched is still set (qd_touch_present_only),
	                         * which matters MORE here than on ECS: pages == 1, so
	                         * this backend is the one the #152 clean-present skip
	                         * actually applies to. */
#endif
};

const dsp_backend_t *dsp_backend_ste(void)
{
	return &ste_backend;
}
