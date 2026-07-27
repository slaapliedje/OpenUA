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
static unsigned char           s_band_pal_prev[ST_NCOL * 3];
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

/* #63 FULL-PRESENT phase split. The HEIRS drive put 32.5% of all play time
 * inside st_present — ~1008 presents at ~1.6 s each — which does not square
 * with #90's finding that post-menu presents convert ZERO rows, since a
 * full-frame c2p is only 1.21 s. Either the conversions are back, or a
 * present that converts nothing is still doing 1.6 s of something. These name
 * which. Coarse on purpose: five Supexec pairs per present, not per row. */
static long sp_ph_band, sp_ph_pass1, sp_ph_copy, sp_ph_n;
static long sp_ph_conv_rows, sp_ph_chg_rows;
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
		TBDR = ST_RPB - 1;              /* first fire ONE LINE EARLY            */
		TBCR = 8;                       /* event-count mode, re-armed in phase  */
		TBDR = ST_RPB;                  /* reload for all LATER fires (the MFP
		                                 * only picks this up at the next
		                                 * underflow, so the -1 above stands
		                                 * for the first) */
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
			if (f < ST_NBANDS) {
				sp_starved++;
				sp_fires_lost += (ST_NBANDS - f);
				if (f < sp_fires_min) sp_fires_min = f;
			}
		}
		sp_frames++;
	}
#endif
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
typedef char st_asm_assumes_rpb_20[(ST_RPB == 20) ? 1 : -1];
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
	"  moveml %d0-%d7/%a0,%sp@-\n"
	"  movel  _st_band_ptr,%a0\n"
	"  moveml %a0@+,%d0-%d7\n"
	"  movel  %a0,_st_band_ptr\n"
	"1:\n"
	"  cmpib  #20,0xFFFFFA21\n"     /* TBDR still at reload (ST_RPB)?      */
	"  jne    2f\n"                 /* no: the line ended — store now      */
	"  tstb   0xFFFFFA1B\n"         /* TBCR == 0 -> timer STOPPED, TBDR is */
	"  jne    1b\n"                 /*   frozen: spinning would deadlock   */
	"2:\n"
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
	{	/* #63: does the split have anything to do this scene? See the flag's
		 * comment — with a single replicated palette the answer is always no,
		 * and Timer B is ~500 interrupts a second of pure overhead. */
		short uniform = 1;

		for (b = 1; b < ST_NBANDS && uniform; b++)
			for (i = 0; i < ST_NCOL; i++)
				if (st_band_stpal[b][i] != st_band_stpal[0][i]) {
					uniform = 0;
					break;
				}
		s_tb_uniform = uniform;
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
	short i, s;

	for (s = 0; s < ST_NCOL; s++)
		anchor[s] = -1;
	for (i = 0; i < 256; i++) {
		if (!s_used_idx[i])
			continue;
		s = s_band_remap[i];             /* band 0 = representative (B1) */
		if (anchor[s] < 0) {
			anchor[s] = i;
			continue;
		}
		if (st_coldist(s_clut + (long)i * 3,
		               s_clut + (long)anchor[s] * 3) > 512)
			return 1;
	}
	return 0;
}

static void st_repalette(void)
{
	short s;

#ifdef FRUA_PLANAR
	st_dt_epoch_reset();                     /* slots renumber: draw-time epoch */
#endif
	for (s = 0; s < ST_NCOL; s++) {
		unsigned char idx = s_slot_rep[s];

		s_band_pal[(long)s * 3 + 0] = quant_snap(s_clut[idx * 3 + 0], ST_BITS);
		s_band_pal[(long)s * 3 + 1] = quant_snap(s_clut[idx * 3 + 1], ST_BITS);
		s_band_pal[(long)s * 3 + 2] = quant_snap(s_clut[idx * 3 + 2], ST_BITS);
	}
	memcpy(s_band_pal_prev, s_band_pal, sizeof s_band_pal_prev);
	st_build_hw_palette();
	s_force_full   = 0;      /* planes unchanged: nothing to re-convert */
}

/* Re-band: histogram + per-band reduce, then build the per-band ST-format
 * palettes (STE gun encoding: nibble = (v0 << 3) | (v >> 1)). The sentinel
 * row (see st_band_stpal) is a copy of the last band. */
static void st_reband(void)
{
	short b;
	const unsigned char *qsrc = s_chunky;
	short first = !s_have_prev_pal;

#ifdef FRUA_PLANAR
	st_dt_epoch_reset();                     /* slots renumber: draw-time epoch */
#endif

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

	/* Capture which indices this quant actually saw (st_remap_split's domain). */
	{
		long n;
		memset(s_used_idx, 0, sizeof s_used_idx);
		for (n = 0; n < (long)ST_W * ST_H; n++)
			s_used_idx[qsrc[n]] = 1;
	}

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

	memcpy(s_band_pal_prev, s_band_pal, sizeof s_band_pal_prev);
	s_have_prev_pal = 1;

	/* Replicate the fixed remap to every band (the pal replicate + hardware
	 * encode happen in st_build_hw_palette). */
	for (b = 1; b < ST_NBANDS; b++)
		memcpy(s_band_remap + (long)b * 256, s_band_remap, 256);

	st_compute_slot_reps();          /* B4 Phase-0: reps for palette-only rebands */
	st_build_hw_palette();

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
	(void)first;
	s_force_full   = 1;
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
		if (p[w] != q[w])
			return 1;
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

	for (x = 0; x < ST_W; x++)
		if (!s_used_idx[crow[x]])
			s_dt_new_ink++;
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
static void st_dt_present_full(void)
{
	short y, pg, run0 = -1;
#ifdef FRUA_STPROF
	long rows_conv = 0, rows_skip = 0;
#endif

	if (s_force_full > 0) {
		for (y = 0; y < ST_H; y++)
#ifdef FRUA_STPROF
			if (st_dt_ready_row(y)) rows_conv++; else rows_skip++;
#else
			st_dt_ready_row(y);
#endif
		/* The single most expensive copy the backend does: 2 x 32000 plane
		 * bytes + 2 x 64000 shadow bytes, and the one shape where the
		 * BLiTTER measured a clear 3.76x. One Supexec for all four. */
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
		goto log;
	}

	/* pass 1 — ready the changed rows and gather them into runs */
#ifdef FRUA_STPROF
	{
	long tp1 = Supexec(st_prof_hz200);
#endif
	s_nruns = 0;
	for (y = 0; y < ST_H; y++) {
		const unsigned char *crow = s_chunky + (long)y * ST_W;
		int changed = st_row_differs(crow, s_shadow + (long)y * ST_W);

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
	sp_ph_conv_rows += rows_conv;
	sp_ph_chg_rows  += rows_conv + rows_skip;
	}
#endif
	if (s_nruns == 0)
		goto log;

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
	s_bb_dst = (void *)Mxalloc((long)ST_W * ST_H, 0); /* 64000: every shape */
	s_bb_src = (void *)Mxalloc((long)ST_W * ST_H, 0);
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
	planar_viewport_register(st_vp_scratch, st_vp_commit);

#ifdef FRUA_PLANAR
	/* Draw-time plane accumulation buffer + hook (ADR-0016 B4). */
	s_dt        = (unsigned char *)Mxalloc(SCREEN_BYTES, 0);
	s_dt_cov    = (unsigned char *)Mxalloc((long)ST_W * ST_H, 0);
	s_dt_idx    = (unsigned char *)Mxalloc((long)ST_W * ST_H, 0);
	s_dt_rowcov = (short *)Mxalloc(ST_H * sizeof(short), 0);
	if (s_dt != NULL)
		memset(s_dt, 0, SCREEN_BYTES);
	if (s_dt_cov != NULL)
		memset(s_dt_cov, 0, (size_t)ST_W * ST_H);
	if (s_dt_idx != NULL)
		memset(s_dt_idx, 0, (size_t)ST_W * ST_H);
	if (s_dt_rowcov != NULL)
		memset(s_dt_rowcov, 0, ST_H * sizeof(short));
	planar_draw_target_register(st_dt_target);
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
	unsigned char b0 = 0, b1 = 0, b2 = 0, b3 = 0;
	short i;

	for (i = 0; i < 8; i++) {
		unsigned char s = lut[src[i]];
		unsigned char m = (unsigned char)(0x80u >> i);

		if (s & 1) b0 |= m;
		if (s & 2) b1 |= m;
		if (s & 4) b2 |= m;
		if (s & 8) b3 |= m;
	}
	g[0] = b0; g[2] = b1; g[4] = b2; g[6] = b3;
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
 * path for anything else. The live viewport is 88x88 at (24,24), so the middle
 * loop does two 32-pixel blocks and the edges one 8-pixel column each side.
 *
 * Shadows are deliberately untouched, exactly as before: the viewport's rows in
 * s_chunky are frozen, the row-diff therefore skips them, and this write is
 * what makes the planes right. Both pages are converted because a full present
 * flips, and the other page's hole would otherwise show a stale viewport. */
static void st_vp_composite_fast(void)
{
	short pg, r;

	for (pg = 0; pg < NPAGES; pg++) {
		for (r = 0; r < s_vp_h; r++) {
			short yy   = (short)(s_vp_y + r);
			short band = (short)((long)yy * ST_NBANDS / ST_H);
			const unsigned char *lut = s_band_remap + (long)band * 256;
			const unsigned char *sp  =
			    s_vp_scratch + (long)yy * VP_SCR_PITCH + s_vp_x;
			unsigned char *drow = s_page[pg] + (long)yy * LINE_BYTES;
			short x = s_vp_x, n = s_vp_w;

			/* lead-in 8px columns until x is 32-aligned */
			while (n >= 8 && (x & 31) != 0) {
				st_c2p8(sp, lut, drow, x);
				sp += 8; x = (short)(x + 8); n = (short)(n - 8);
			}
			while (n >= 32) {
				unsigned short *d =
				    (unsigned short *)(drow + (long)(x >> 4) * 8);

				if (c2p4st_is_flat(sp, 32))
					c2p4st_32_flat(sp[0], lut, d);
				else
					c2p4st_32(sp, lut, d);
				sp += 32; x = (short)(x + 32); n = (short)(n - 32);
			}
			while (n >= 8) {                 /* trailing columns */
				st_c2p8(sp, lut, drow, x);
				sp += 8; x = (short)(x + 8); n = (short)(n - 8);
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

static void st_vp_composite(void)
{
#ifdef FRUA_STPROF
	long t0;
#endif

	if (!s_vp_active)
		return;
	s_vp_active = 0;                         /* one-shot per commit */
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
	if (((s_vp_x | s_vp_w) & 7) == 0
	    && s_vp_x >= 0 && s_vp_y >= 0
	    && (short)(s_vp_x + s_vp_w) <= ST_W
	    && (short)(s_vp_y + s_vp_h) <= ST_H)
		st_vp_composite_fast();
	else
		st_vp_composite_slow();
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
	 * then the phase timer is inflated and the attribution is wrong. */
	{
		long a, tm, tl, i;
		short y;
		volatile long sink = 0;

		/* Arm A: exactly what pass 1 runs — 200 row memcmps, 16 times. */
		a = Supexec(st_prof_hz200);
		for (i = 0; i < 16; i++)
			for (y = 0; y < ST_H; y++)
				sink += memcmp(s_chunky + (long)y * ST_W,
				               s_offpage + (long)y * ST_W, ST_W) != 0;
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
				const long *q = (const long *)(s_offpage + (long)y * ST_W);
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
			    memcmp(s_chunky, s_shadow, (long)ST_W * ST_H) == 0 &&
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
				st_repalette();
			} else {
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
	if (s_have_pal && s_banded_valid && s_dt_new_ink >= 4) {
#ifdef FRUA_PLANAR_DIAG
		dbg_log_num("b4ink: new-ink px -> requant, n = ", s_dt_new_ink);
#endif
		s_dirty        = 1;
		s_banded_valid = 0;
	}
	s_dt_new_ink = 0;
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
		dbg_log_num("b63pr:   copy   t200   = ", sp_ph_copy);
		dbg_log_num("b63pr:   vpcomp t200   = ", sp_vp_t);
		dbg_log_num("b63pr: rows changed    = ", sp_ph_chg_rows);
		dbg_log_num("b63pr: rows converted  = ", sp_ph_conv_rows);
		st_prof_hot_dump();              /* #41: hot-row attribution window */
		st_prof_b30b();                  /* B3.0b: compute-vs-contention sample */
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
	dbg_log_num("b63play: rect presents  = ", sp_rect_n);
	dbg_log_num("b63play: rect t200      = ", sp_rect_t);
	dbg_log_num("b63play: composites     = ", sp_vp_n);
	dbg_log_num("b63play: vp w*1000+h    = ", (long)s_vp_w * 1000L + s_vp_h);
	dbg_log_num("b63play:   of which c2p = ", sp_vp_conv);
	dbg_log_num("b63play:   of which blit= ", sp_vp_blit);
	dbg_log_num("b63play: composite t200 = ", sp_vp_t);   /* SUBSET of rect  */
	dbg_log_num("b63play: wall t200      = ", wall);
	/* The number the lever choice turns on: per mille of wall clock spent
	 * inside the display layer. Small => the ceiling is the engine's own 3D
	 * render and c2p tuning cannot reach it. rect ALONE — the composite runs
	 * inside it on the walk path, so adding the two would double-count. */
	if (wall > 0)
		dbg_log_num("b63play: display per 1000= ",
		            (sp_rect_t * 1000L) / wall);
	sp_rect_n = sp_rect_t = sp_vp_n = sp_vp_t = 0;
	sp_vp_conv = sp_vp_blit = 0;
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
