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
#include "amiga_prof.h"         /* FRUA_AMIGAPROF: fine play-loop timer */

#ifdef FRUA_AMIGA

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>              /* memcmp/memcpy (was implicitly declared) */

#if defined(FRUA_ECSTRACE) && !defined(FRUA_AMIGAPROF)
#error "FRUA_ECSTRACE timestamps need amiga_prof_rl -- build with FRUA_AMIGAPROF too"
#endif
#ifdef FRUA_AMIGAPROF
#define QUANT_PROF
#define QUANT_PROF_T() amiga_prof_rl()
#endif
#include "quantize.h"            /* quant_banded — the banded median-cut reducer */
#include "planar.h"             /* planar_viewport_register (#139 group rect) */
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
/* When an incremental present's changed rows carry heavy NEW INK — indices the
 * current band palettes cannot show — render the frame properly NOW instead of
 * painting it through the wrong palette. Runtime knob: video.cfg inkhold=off. */
short ecs_ink_hold = 1;
short ecs_ink_adopt = 1;       /* video.cfg inkadopt=off */
short ecs_title_defer = 1;     /* video.cfg titledefer=off */
/* A500 walk-speckle A/B set (see the video.cfg parser in ecs_init).
 * ecs_eagerq defaults OFF: the eager re-quant-at-install is unverified on
 * real silicon and is the prime suspect for the accumulating speckle in
 * the 2026-08-23 field video; the veto half (ecs_majq) runs inside the
 * present flow with a full render behind it and stays on. */
short ecs_majq          = 1;   /* video.cfg majq=off    */
short ecs_eagerq        = 0;   /* video.cfg eagerq=on   */
short ecs_sprite_disarm = 1;   /* video.cfg sprites=on  */
/* #165 tear-free threshold: at or above this many changed rows, a full
 * present rebuilds the whole page and flips instead of converting rows into
 * the page the copper is showing (see the full present). 50 of 200 rows = a
 * quarter of the screen — comfortably above any HUD/glyph/command-bar update
 * and below a scene change. video.cfg `tearfree=<rows>`; 0 restores the
 * pre-#165 always-direct behaviour for A/B. Declared here, NOT beside its
 * use: the video.cfg parser in ecs_init runs first in the file (the
 * recurring ordering trap noted just below). */
#define ECS_TEARFREE_DEFAULT 50
short ecs_tearfree      = ECS_TEARFREE_DEFAULT;
/* Defined with the disk palette cache, below — the video.cfg parser in
 * ecs_init runs first in the file. (The recurring ordering trap; see
 * sp_vp_rearm on the ST.) */
extern short ecs_pal_cache;
extern short ecs_clut_cache;   /* CLUT-keyed RAM cache, defined beside it */
/* #139 viewport palette groups — defined mid-file, used by ecs_init. */
extern short ecs_vp_groups;
static void  ecs_vp_note(short x, short y, short w, short h);
static short e_held_once;      /* one hold per burst — never two in a row */
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
	/* Bands 1..N-1: reload during the PRECEDING line's blanking.
	 *
	 * ★ #167. This used to wait for the band's OWN first scanline at
	 * hpos 0 and then issue the 32 COLOR moves. That does not fit: a
	 * copper MOVE costs 2 copper cycles = 4 colour clocks, so 32 of them
	 * need 128 CCK, and the display window opens at DIWSTRT hpos 0x81 =
	 * 129 CCK. One colour clock of margin — before the four memory-refresh
	 * slots at the top of every scanline are subtracted, and before any
	 * blitter contention. So the tail of the palette load lands INSIDE the
	 * visible line, or slips a whole line, and that scanline is displayed
	 * with the previous band's colours for the registers that had not been
	 * rewritten yet.
	 *
	 * On screen that is a thin wrong-coloured line at every band boundary —
	 * DASHED, because only the pixels whose slots differ between the two
	 * bands' palettes look wrong. Reported from the real A500 as "the
	 * bigpics all had leftover pixels"; measured here, every artefact row
	 * sat at an exact multiple of ECS_RPB (game rows 32, 48, 56, 72, 80,
	 * 96 — mod 8 == 0), and the same rows appeared on two different
	 * pictures, which is what proved it positional rather than content.
	 * The Falcon renders the same picture from the same save with no
	 * artefacts at all: no copper, no band palettes.
	 *
	 * Waiting on (line - 1, late hpos) instead gives the load the tail of
	 * the previous line plus the whole horizontal blank — comfortably more
	 * than the 128 CCK it needs — so every register is in place before the
	 * band's first pixel. hpos 0x64 = CCK 200, past DIWSTOP's 0xC1 = 193,
	 * so nothing is written while the previous line is still displaying.
	 * b starts at 1, so line - 1 >= 0x2C + ECS_RPB - 1 and the wait can
	 * never precede the display window's first line. */
	for (b = 1; b < ECS_NBANDS; b++) {
		short line = (short)(0x2C + b * ECS_RPB);

		cl = cop_wait(cl, (short)(line - 1), 0x64);
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

	/* ★ #43: ask for what this backend ACTUALLY uses — V33. It calls exactly
	 * two graphics.library functions, LoadView and WaitTOF, both present since
	 * Kickstart 1.2; everything else is a hand-built copper list and direct
	 * register pokes. The 39 here was inherited from display_aga.c, where V39
	 * is genuine (AA chipset detection via ChipRevBits0).
	 *
	 * It was INERT in practice — GfxBase is already open by the time ecs_init
	 * runs, so this call never fired; measured on Kickstart 2.05, where the
	 * ECS build boots to the main menu with GfxBase->lib_Version == 37. But a
	 * link-order change that stopped pre-opening GfxBase would have silently
	 * cost every 2.x machine, and 2.x machines are exactly the ECS audience.
	 * Asking for 33 makes the stated dependency match the real one. */
	if (GfxBase == NULL)
		GfxBase = (struct GfxBase *)
		    OpenLibrary((CONST_STRPTR)"graphics.library", 33);
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

	/* video.cfg, same contract as the ST/Nova backends: runtime knobs so an
	 * A/B is one binary. The Amiga side never had a reader — the current
	 * directory is the game dir (DH0:), same place DBG.LOG lands. */
	{
		FILE *cf = fopen("video.cfg", "r");

		if (cf != NULL) {
			char buf[256];
			size_t n = fread(buf, 1, sizeof buf - 1, cf);
			size_t i;

			fclose(cf);
			buf[n > 0 ? n : 0] = '\0';
			for (i = 0; buf[i] != '\0'; i++)
				if (buf[i] >= 'A' && buf[i] <= 'Z')
					buf[i] = (char)(buf[i] + 32);
			if (strstr(buf, "palcache=off") != NULL) {
				ecs_pal_cache = 0;
				dbg_log("ecs: disk palette cache DISABLED (video.cfg)");
			} else if (strstr(buf, "palcache=on") != NULL) {
				ecs_pal_cache = 1;
			}
			if (strstr(buf, "clutcache=off") != NULL) {
				ecs_clut_cache = 0;
				dbg_log("ecs: CLUT-keyed RAM cache DISABLED (video.cfg)");
			} else if (strstr(buf, "clutcache=on") != NULL) {
				ecs_clut_cache = 1;
			}
			if (strstr(buf, "inkhold=off") != NULL) {
				ecs_ink_hold = 0;
				dbg_log("ecs: ink-hold DISABLED (video.cfg)");
			} else if (strstr(buf, "inkhold=on") != NULL) {
				ecs_ink_hold = 1;
				dbg_log("ecs: ink-hold ENABLED (video.cfg)");
			}
			if (strstr(buf, "titledefer=off") != NULL) {
				ecs_title_defer = 0;
				dbg_log("ecs: title cut-defer DISABLED (video.cfg)");
			} else if (strstr(buf, "titledefer=on") != NULL) {
				ecs_title_defer = 1;
				dbg_log("ecs: title cut-defer ENABLED (video.cfg)");
			}
			if (strstr(buf, "inkadopt=off") != NULL) {
				ecs_ink_adopt = 0;
				dbg_log("ecs: ink-adopt DISABLED (video.cfg)");
			} else if (strstr(buf, "inkadopt=on") != NULL) {
				ecs_ink_adopt = 1;
				dbg_log("ecs: ink-adopt ENABLED (video.cfg)");
			}
			if (strstr(buf, "vpgroups=off") != NULL) {
				ecs_vp_groups = 0;
				dbg_log("ecs: viewport palette groups DISABLED (video.cfg)");
			} else if (strstr(buf, "vpgroups=on") != NULL) {
				ecs_vp_groups = 1;
			}
			/* A500 walk-speckle A/B set (2026-08-23, field video):
			 * one binary, three levers. majq gates BOTH halves of the
			 * major-CLUT-change work (the repalette veto and the eager
			 * re-quant); eagerq gates just the eager half (default OFF
			 * here — unverified on silicon, the prime suspect);
			 * sprites=on skips the 0.9.13 takeover sprite disarm
			 * (diagnostic: brings the pointer-over-title bug back). */
			if (strstr(buf, "majq=off") != NULL) {
				ecs_majq = 0;
				dbg_log("ecs: major-CLUT-change handling DISABLED (video.cfg)");
			}
			if (strstr(buf, "eagerq=on") != NULL) {
				ecs_eagerq = 1;
				dbg_log("ecs: eager re-quant ENABLED (video.cfg)");
			}
			if (strstr(buf, "sprites=on") != NULL) {
				ecs_sprite_disarm = 0;
				dbg_log("ecs: sprite disarm SKIPPED (video.cfg)");
			}
			/* #165 A/B: tearfree=<rows>, 0 = always write the
			 * visible page (the pre-#165 behaviour). */
			{
				const char *tf = strstr(buf, "tearfree=");

				if (tf != NULL) {
					short v = 0;

					tf += 9;
					while (*tf >= '0' && *tf <= '9')
						v = (short)(v * 10 + (*tf++ - '0'));
					ecs_tearfree = v;
					dbg_log_num("ecs: tear-free threshold rows = ",
					            (long)v);
				}
			}
		}
	}
	/* #139 groups: listen for the engine's viewport announce (the commit
	 * hook never fires on a backend with no scratch — learned the hard
	 * way: the first cut registered it and grouping never engaged). */
	planar_viewport_note_register(ecs_vp_note);
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
	/* ★ KILL THE SPRITES (A1200 real-hardware find, 2026-08-23). The old
	 * write below only SET raster+copper DMA — sprite DMA stayed however
	 * Workbench left it (ON), and BPLCON2=0 is sprite PRIORITY, not sprite
	 * off. Result on real silicon: the Intuition pointer floating over the
	 * title before any menu, and dangling sprite fetches drawing a garbage
	 * column at the left edge. Emulated runs never showed either. The ECS
	 * cursor is software, so: clear sprite DMA, then disarm every channel
	 * (an armed sprite keeps DISPLAYING its last data even with DMA off —
	 * writing SPRxCTL disarms it until the next SPRxPOS). */
	if (ecs_sprite_disarm) {
		short sp;

		CUSTOM->dmacon = DMAF_SPRITE;       /* clear (no SETCLR) */
		for (sp = 0; sp < 8; sp++) {
			CUSTOM->spr[sp].ctl = 0;
			CUSTOM->spr[sp].dataa = 0;
			CUSTOM->spr[sp].datab = 0;
		}
	}
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
		CUSTOM->dmacon = (UWORD)(DMAF_SETCLR | DMAF_SPRITE);
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
#define ECS_TITLEDEFER_MAX 24   /* presents; a lost title beats a lost game */
static short         e_title_defer_n;

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

/* The Atari twin's st_clut_major_change: a CLUT REPLACED (a title/BigPic
 * committing its own palette) invalidates every remap at once, and the
 * split heuristic below cannot see it because everything moved. Without
 * this the present after the commit takes the repalette shortcut and the
 * screen keeps the PREVIOUS scene's cut — the v0.9.7 title regression
 * ("title screens in multiple passes / wrong colours", A500 field report). */
static int ecs_clut_major_change(void)
{
	short i, moved = 0;

	for (i = 0; i < 256; i++) {
		short d = (short)(
		    (s_clut[i * 3 + 0] > e_clut_quant[i * 3 + 0]
		     ? s_clut[i * 3 + 0] - e_clut_quant[i * 3 + 0]
		     : e_clut_quant[i * 3 + 0] - s_clut[i * 3 + 0])
		  + (s_clut[i * 3 + 1] > e_clut_quant[i * 3 + 1]
		     ? s_clut[i * 3 + 1] - e_clut_quant[i * 3 + 1]
		     : e_clut_quant[i * 3 + 1] - s_clut[i * 3 + 1])
		  + (s_clut[i * 3 + 2] > e_clut_quant[i * 3 + 2]
		     ? s_clut[i * 3 + 2] - e_clut_quant[i * 3 + 2]
		     : e_clut_quant[i * 3 + 2] - s_clut[i * 3 + 2]));
		if (d >= 48)
			moved++;
	}
#ifdef FRUA_MAJDIAG
	dbg_log_num("ecsmaj: entries moved = ", (long)moved);
#endif
	return moved >= 64;
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

/* ★ COLOR00 IS THE AMIGA'S BORDER TOO, AND THERE ARE TWENTY-FIVE OF THEM.
 *
 * DIW is exactly 320x200, so every scanline of the overscan outside it shows
 * COLOR00 — and the copper rewrites all 32 registers, COLOR00 included, at each
 * of the 25 band boundaries. Twenty-five independent cuts put twenty-five
 * different colours there, so the left and right overscan strips came out
 * STRIPED: measured on a boot frame, 21 distinct colours in 28 runs down a
 * single border column, changing every 8 scanlines, one of them pure white.
 *
 * It had shipped since the per-band copper palette went in. Nothing caught it
 * because every check — the AE comparisons, the colour counts, the montages —
 * looked at the 320x200 image, where there is nothing wrong. The ST hit the
 * identical bug the day the raster split landed there (st_unify_border, #139);
 * this is the same fix, and the ST's three bands flattered it by comparison.
 *
 * Pick, in each later band, the slot nearest band 0's slot 0, permute it into
 * position 0 (palette and remap together, so no pixel changes meaning), then
 * set its RGB to band 0's exactly. Matching band 0 rather than forcing black is
 * deliberate: uniform is the requirement, and band 0's colour is the one the
 * border already had before the split existed.
 *
 * ★ may_permute IS NOT A TUNING KNOB. ecs_repalette's premise is that slot
 * numbering does not move, so the planes on screen stay valid and no force-full
 * is needed; permuting there would renumber slots behind those planes. That
 * path forces the copper WORD alone — see the tail of ecs_repalette. */
static void ecs_unify_border(short may_permute)
{
	const unsigned char *p0 = s_band_pal;         /* band 0, slot 0 */
	unsigned char newpal[ECS_NCOL * 3];
	unsigned char pos[ECS_NCOL];
	short b, s, n, best;
	long  bestd;

	for (b = 1; b < ECS_NBANDS; b++) {
		unsigned char *bp = s_band_pal + (long)b * ECS_NCOL * 3;
		unsigned char *br = s_band_remap + (long)b * 256;

		best  = 0;
		bestd = 0x7fffffffL;
		for (s = 0; s < ECS_NCOL; s++) {
			long d = e_coldist(bp + (long)s * 3, p0);

			if (d < bestd) { bestd = d; best = s; }
		}
		if (may_permute && best != 0) {          /* swap `best` and 0 */
			for (n = 0; n < ECS_NCOL; n++)
				pos[n] = (unsigned char)n;
			pos[best] = 0;
			pos[0]    = (unsigned char)best;
			for (n = 0; n < ECS_NCOL; n++)
				memcpy(newpal + (long)pos[n] * 3,
				       bp + (long)n * 3, 3);
			memcpy(bp, newpal, sizeof newpal);
			for (n = 0; n < 256; n++)
				br[n] = pos[br[n]];
		}
		memcpy(bp, p0, 3);      /* exactly, so the border is one colour */
	}
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
	/* The reps are per band, so each band re-derives its own slot 0 from a
	 * different index and the border would stripe again on the fast path
	 * exactly as it does on the slow one. Only the copper WORD is forced —
	 * no slot number moves, so the planes on screen stay valid. */
	for (b = 1; b < ECS_NBANDS; b++)
		*s_cop_pal[b][0] = *s_cop_pal[0][0];
#ifdef FRUA_ECSTITLE
	/* #16: how many band slots just took a SENTINEL colour from their
	 * representative? A slot painted magenta paints every pixel mapped to
	 * it, not just the sentinel ones. */
	{
		short bb, ii; long nsent = 0, nlive = 0;

		for (bb = 0; bb < ECS_NBANDS; bb++)
			for (ii = 0; ii < ECS_NCOL; ii++) {
				unsigned char r = e_slot_rep[bb][ii];

				if (r == 0xFF) continue;
				nlive++;
				if (s_clut[r * 3 + 0] > 200 && s_clut[r * 3 + 1] < 60
				    && s_clut[r * 3 + 2] > 200)
					nsent++;
			}
		dbg_log_num("ttl: repalette live slots      = ", nlive);
		dbg_log_num("ttl:   ...taking SENTINEL rgb  = ", nsent);
	}
#endif
	memcpy(e_clut_quant, s_clut, sizeof e_clut_quant);
}

/* #117: "did this row change?" — the ST's st_row_differs, ported. `memcmp` is
 * the wrong primitive on a 68000: timed back to back on the ST over the real
 * 64000-byte surface it ran 93 cycles/byte against 30 for the same compare
 * written long-wise, and swapping it HALVED the full present there (322 ->
 * 168 t200, in-present 32.5% -> 19.1% of play). A bare ECS machine is a 7 MHz
 * 68000 — slower than the STE that measurement came from.
 *
 * Both operands are AllocMem'd (>= 8-byte aligned) and ECS_W is 320, a
 * multiple of 4, so every row start is long-aligned. That matters: an
 * unaligned long read here would be an address error, not a slow path.
 *
 * Early-exits, like the ST's, so the common "differs in the first few words"
 * case never scans the row. */
static int ecs_row_differs(const unsigned char *a, const unsigned char *b,
                           long bytes)
{
#ifdef FRUA_ROWDIFF_MEMCMP
	return memcmp(a, b, (size_t)bytes) != 0;   /* A/B arm, one flag apart */
#else
	const long *p = (const long *)(const void *)a;
	const long *q = (const long *)(const void *)b;
	long        w, n = bytes / 4;

	for (w = 0; w < n; w++)
		if (p[w] != q[w])
			return 1;
	return 0;
#endif
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
	    && !ecs_row_differs(e_dt_idx + (long)y * ECS_W, crow, ECS_W))
		return 0;
	remap_rect(0, y, ECS_W, 1);
	for (p = 0; p < ECS_DEPTH; p++)
		pl[p] = e_dt + (long)p * ECS_PITCH * ECS_H;
	c2p_amiga_n_rect(s_remap_buf, ECS_W, pl, ECS_PITCH,
	                 0, y, ECS_W, 1, ECS_DEPTH);
	/* #41 SELF-HEALING OWNERSHIP (ST parity): the row just bridged IS
	 * remap[chunky] — the ownership invariant — so claim it. A coverage hole
	 * (pre-epoch backdrop) bridges ONCE and skips thereafter; a direct
	 * writer's overwrite breaks idx==chunky and re-bridges. The epoch reset
	 * still wipes cov/rowcov, so a re-band invalidates healed rows too. */
	memset(e_dt_cov + (long)y * ECS_W, 1, ECS_W);
	memcpy(e_dt_idx + (long)y * ECS_W, crow, ECS_W);
	e_dt_rowcov[y] = ECS_W;
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
#ifdef FRUA_ECSINK
			/* #dim: report NEW ink (an index the last quant never
			 * saw) once per (band, index) — with the slot it lands
			 * on, how far away that slot is, and how many of the
			 * band's 32 slots are actually in use. If the band has
			 * free slots, the grey glyph is pure loss. */
			if (!e_used_idx[src[c]]) {
				static unsigned char seen[ECS_NBANDS][256];
				if (!seen[band][src[c]]) {
					const unsigned char *bpal =
					    s_band_pal + (long)band * ECS_NCOL * 3;
					short sl = lut[src[c]], k, nused = 0;
					long d = e_coldist(s_clut + (long)src[c] * 3,
					                   bpal + (long)sl * 3);
					seen[band][src[c]] = 1;
					for (k = 0; k < ECS_NCOL; k++)
						if (e_slot_rep[band][k] != 0xFF)
							nused++;
					dbg_log_num("ink: band*1000+idx = ",
					            (long)band * 1000 + src[c]);
					dbg_log_num("     slot*10000+dist = ",
					            (long)sl * 10000 + (d > 9999 ? 9999 : d));
					dbg_log_num("     slots used of 32 = ", (long)nused);
				}
			}
#endif
			dst[c] = lut[src[c]];
		}
	}
}

/* Re-band: histogram + per-band reduce over the current surface, then patch the
 * copper's per-band COLOR words. */
/* --- THE DISK PALETTE CACHE (the user's question, verbatim: "is that because
 * you're doing a quantization every time you load the game rather than caching
 * it on disk?" — yes, it was) --------------------------------------------
 *
 * A boot runs ~5 re-bands, each 25 median cuts + bucket tables on a 7 MHz
 * 68000 — ~47 s of quantisation per boot computing IDENTICAL answers every
 * time: same title art, same CLUTs, deterministic quantiser. So cache the
 * results on disk, keyed on (CLUT hash, frame hash), and a warm boot replaces
 * each re-band's cut with a file lookup and two memcpys.
 *
 * - The entry stores the POST-unify-border state, so a hit skips
 *   ecs_unify_border too; everything downstream (used-set capture, slot reps,
 *   the copper install) runs identically on both paths.
 * - The hash is shift-add (djb2-style), NOT FNV: FNV's multiply is __mulsi3 on
 *   a 68000 and hashing the 64000-byte frame would cost ~1.8 s — a tenth of
 *   the re-band it is trying to save. Shift-add is ~0.1 s. Two independent
 *   32-bit hashes (CLUT and frame) key an entry; a collision would install a
 *   wrong palette, so the pairing matters and the file carries a VERSION to
 *   invalidate wholesale if the quantiser ever changes.
 * - PROGDIR:PALCACHE.ECS lives in the game dir (next to DBG.LOG, which
 *   also uses PROGDIR:). A bare relative path wrote/read it against the CWD,
 *   which is NOT the game dir when frua is launched from a Workbench icon or a
 *   Shell in another drawer — so the cache never persisted and every boot
 *   re-quantised (A500, 2026-08-22). ~8.8 KB per
 *   entry, 16 entries max. Deleting the file is always safe — it regenerates.
 * - video.cfg `palcache=off` disables both read and write (one-binary A/B).
 * - EPC_VERSION must be bumped with ANY change to quantize.h's output. The
 *   cache stores that output verbatim; a stale file after a quantiser change
 *   would silently pin the OLD palettes. */
/* --- #139 PORT: VIEWPORT PALETTE GROUPS (2026-08-23) ----------------------
 *
 * The re-band used to run the median cut PER BAND — 25 cuts a frame — and the
 * phase profile put cut+buckets at 86% of a ~9.5 s re-band on the 7 MHz 68000.
 * The ST solved this in #139: split at the two lines where the content really
 * changes (the first-person viewport's top and bottom edges) and run ONE cut
 * per group. Ported here for the WALK SCREEN ONLY: with a committed viewport
 * the frame cuts as 3 groups (chrome above / viewport / chrome below), 32
 * colours each — 96 slots for a walk frame whose whole colour count is ~63,
 * so fidelity holds while the cut cost drops ~8x. Screens with no viewport
 * (title, menus, event pictures) keep the full 25 per-band cuts: they are
 * one-time work (the disk cache replays them forever) and per-band fidelity
 * on big pictures is worth keeping — and their existing cache entries stay
 * bit-valid because that path is untouched.
 *
 * The rect comes from the engine itself: dsp_viewport_commit fires after
 * every 3D render even when the backend supplies no scratch (the ST needs
 * the scratch; we only record the rect). e_vp_commit is cleared at the end
 * of EVERY present, so a re-band groups only when the frame being quantised
 * is the one whose render just committed the viewport — a re-band on any
 * later frame (a menu, a picture) falls back to the 25-band path rather
 * than splitting a picture on a stale boundary (the #40 seam hazard).
 * video.cfg `vpgroups=off` restores the old path wholesale (one-binary A/B;
 * the runtime-switch discipline that #91 taught). */
short        ecs_vp_groups = 1;         /* video.cfg vpgroups=off */
static short e_vp_x, e_vp_y, e_vp_ww, e_vp_hh;  /* last announced viewport   */
static short e_vp_commit;               /* sticky; invalidated by content    */
static unsigned char e_vp_sig[16];      /* fingerprint of the rendered vp    */

/* Sample 16 spread pixels of the viewport region of s_chunky. The announce
 * fires at the end of a 3D render, when the viewport's pixels are final; a
 * re-band later re-samples the same offsets. Anything that legitimately
 * replaces the viewport (an event picture, a menu — always full-screen
 * draws) changes them; HUD/clock/bar updates outside the rect do not. This
 * is what makes the sticky flag safe: grouping engages only while the frame
 * being quantised still CONTAINS the rendered viewport, with no dependence
 * on the order of presents between render and re-band (the first cut
 * cleared the flag per present and most walk re-bands missed it). */
static void ecs_vp_sample(unsigned char *out)
{
	short i;

	for (i = 0; i < 16; i++) {
		short yy = (short)(e_vp_y + ((long)e_vp_hh * (2 * i + 1)) / 32);
		short xx = (short)(e_vp_x + ((long)e_vp_ww * (2 * i + 1)) / 32);

		out[i] = s_chunky[(long)yy * ECS_W + xx];
	}
}

static void ecs_vp_note(short x, short y, short w, short h)
{
	if (w <= 0 || h <= 0)
		return;
	e_vp_x = x; e_vp_y = y; e_vp_ww = w; e_vp_hh = h;
	ecs_vp_sample(e_vp_sig);
	e_vp_commit = 1;
}

/* ★ DISK-BACKED CACHE (2026-08-22). It used to hold EPC_MAX=16 blobs in RAM
 * and STOP caching once full — every distinct frame past the 16th re-quantised
 * on EVERY view, forever (a dungeon has far more than 16 scenes, so most of the
 * game re-converted per screen: the A500 "converting every screen" misery).
 * Now the KEYS live in RAM (8 bytes each, cheap) and the 8.8 KB blobs live on
 * disk in PALCACHE.ECS; a hit reads its blob back, a miss appends a new one.
 * Unbounded in practice (EPC_CAP keys), and it saves ~141 KB of RAM. The file
 * layout is unchanged (hdr + nent*(key+blob)), so v1 files still load. */
#define EPC_VERSION 1
#define EPC_CAP     2048                        /* keys in RAM: 16 KB          */
#define EPC_PAL     (ECS_NBANDS * ECS_NCOL * 3) /* 2400                        */
#define EPC_REMAP   (ECS_NBANDS * 256)          /* 6400                        */
#define EPC_BLOB    (EPC_PAL + EPC_REMAP)       /* 8800                        */
#define EPC_FILE    "PROGDIR:PALCACHE.ECS"
struct epc_hdr { ULONG version; ULONG nent; };
struct epc_key { ULONG clut_h; ULONG frm_h; };
#define EPC_ENTRY   ((long)sizeof(struct epc_key) + EPC_BLOB)
static struct epc_key  e_pc_key[EPC_CAP];
static short           e_pc_n;
static short           e_pc_loaded;
static short           e_pc_last = -1;          /* index whose blob is live in s_band_* */
short                  ecs_pal_cache = 1;       /* video.cfg palcache=off; extern'd above */

/* --- CLUT-keyed RAM palette cache (the ST's st_pcache, ported) -----------
 *
 * The disk cache above keys on clut hash + FRAME hash — exact, but useless
 * mid-walk: every step is a new frame, so the town streets (whose wall-set
 * CLUTs alternate between a handful of palettes) re-CUT on every step —
 * measured 7 full re-bands in 16 moves, each dragging a full re-render
 * behind it. On a 7 MHz A500 that is the "CLUT unloading/loading every
 * step" field report. This cache keys on the CLUT alone: a walk step whose
 * install brings back a palette already cut reuses that cut. Two guards
 * carried over from the ST implementation:
 *   - indices the cached cut never saw (`used`) are re-pointed at their
 *     nearest slot in each band's palette, so new-to-this-frame art cannot
 *     fall through the cut's absent-colour bucketing;
 *   - when the restored remaps equal the live ones, the planes on screen
 *     are still VALID — no force-full, no epoch reset, just the copper
 *     palette words (the alternation's common case: same content classes,
 *     different RGB). */
#define ERC_N 6
struct erc_ent {
	ULONG         clut_h;
	short         valid;
	unsigned char clut[768];        /* verified, never trusted raw */
	unsigned char pal[EPC_PAL];
	unsigned char rem[EPC_REMAP];
	unsigned char used[256];        /* what the cut actually saw   */
};
static struct erc_ent s_erc[ERC_N];
static short          s_erc_mru[ERC_N];
static short          s_erc_n;
short                 ecs_clut_cache = 1;       /* video.cfg clutcache=off */

static ULONG epc_hash(const unsigned char *p, long n)
{
	ULONG h = 5381;

	while (n-- > 0)
		h = ((h << 5) + h) ^ *p++;              /* h*33 ^ b, no multiply */
	return h;
}

static void epc_load(void)
{
	FILE *f;
	struct epc_hdr hd;

	long size, nent, i;

	e_pc_loaded = 1;
	e_pc_last = -1;
	f = fopen(EPC_FILE, "rb");
	if (f == NULL) {                        /* first run: create with a header */
		f = fopen(EPC_FILE, "wb");
		if (f != NULL) {
			hd.version = EPC_VERSION;
			hd.nent    = 0;                 /* informational; count is by size */
			fwrite(&hd, sizeof hd, 1, f);
			fclose(f);
		}
		return;
	}
	if (fread(&hd, sizeof hd, 1, f) != 1 || hd.version != EPC_VERSION) {
		dbg_log("ecs: palette cache stale/foreign - ignored");
		fclose(f);
		return;
	}
	/* The authoritative count is the file SIZE, not the header field — append
	 * (below) never rewrites the header, which is what an in-place "r+b" update
	 * needs and the Amiga ncrt0 stdio would not do reliably. */
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	nent = (size - (long)sizeof(struct epc_hdr)) / EPC_ENTRY;
	fseek(f, (long)sizeof(struct epc_hdr), SEEK_SET);
	for (i = 0; i < nent && e_pc_n < EPC_CAP; i++) {
		if (fread(&e_pc_key[e_pc_n], sizeof(struct epc_key), 1, f) != 1)
			break;
		if (fseek(f, EPC_BLOB, SEEK_CUR) != 0)   /* blob stays on disk */
			break;
		e_pc_n++;
	}
	dbg_log_num("ecs: palette cache entries loaded = ", (long)e_pc_n);
	fclose(f);
}

/* Load entry k's blob from disk into the live band buffers. e_pc_last skips
 * the read when the wanted blob is already live (repeated same-frame presents). */
static int epc_read_blob(short k)
{
	FILE *f;
	int ok = 0;

	if (k == e_pc_last)
		return 1;
	f = fopen(EPC_FILE, "rb");
	if (f == NULL)
		return 0;
	if (fseek(f, (long)sizeof(struct epc_hdr) + (long)k * EPC_ENTRY
	             + (long)sizeof(struct epc_key), SEEK_SET) == 0
	    && fread(s_band_pal,   EPC_PAL,   1, f) == 1
	    && fread(s_band_remap, EPC_REMAP, 1, f) == 1) {
		e_pc_last = k;
		ok = 1;
	}
	fclose(f);
	return ok;
}

static void epc_append(ULONG ch, ULONG fh)
{
	struct epc_key key;
	FILE *f;

	if (e_pc_n >= EPC_CAP)
		return;
	key.clut_h = ch;
	key.frm_h  = fh;
	/* epc_load guarantees the file + header exist, so a plain append at EOF
	 * adds the entry; the count comes from the file size on the next load. */
	f = fopen(EPC_FILE, "ab");
	if (f == NULL)
		return;
	if (fwrite(&key,         sizeof key, 1, f) == 1
	    && fwrite(s_band_pal,   EPC_PAL,   1, f) == 1
	    && fwrite(s_band_remap, EPC_REMAP, 1, f) == 1) {
		e_pc_key[e_pc_n] = key;
		e_pc_last = e_pc_n;
		e_pc_n++;
	}
	fclose(f);
}

/* The first-load "PLEASE WAIT... CONVERTING ART" notice, its progress bar,
 * the per-quant campfire bard, and the bar-driven interrupt sampler were
 * REMOVED (2026-08-23, on the A500 verdict). They earned their keep when a
 * cold quant froze the screen for ~17 s with no way to tell "converting"
 * from "hung"; with the disk cache (once per scene, ever) and the viewport
 * groups (~1 s on the walk), the ceremony itself — a black box painted over
 * the art, a music start/stop, several pops on ONE title screen — outweighed
 * the wait it announced. The quant now just runs; the git history has the
 * notice if a future 10-s-class conversion ever needs it back.
 */

/* #165: the copper palette words, staged. Writing them is a separate act
 * from computing them so a full render can install the new palette AT THE
 * FLIP rather than 12 seconds ahead of the planes it belongs to (see the
 * comment at the end of ecs_reband). */
static short e_cop_pending;

static void ecs_cop_pal_commit(void)
{
	short b, i;

	for (b = 0; b < ECS_NBANDS; b++) {
		const unsigned char *bp = s_band_pal + (long)b * ECS_NCOL * 3;

		for (i = 0; i < ECS_NCOL; i++)
			*s_cop_pal[b][i] = (UWORD)(((bp[i * 3 + 0] >> 4) << 8)
			                          | ((bp[i * 3 + 1] >> 4) << 4)
			                          | (bp[i * 3 + 2] >> 4));
	}
	e_cop_pending = 0;
}

static void ecs_reband(short defer)
{
	ULONG ch = 0, fh = 0;
	short hit = -1;
	short rhit = -1;                /* CLUT-keyed RAM cache entry, or -1 */
	short remap_same = 0;           /* rhit's remaps == the live remaps  */

	if (ecs_pal_cache || ecs_clut_cache)
		ch = epc_hash(s_clut, 768);
	if (ecs_pal_cache) {
		short k;

		if (!e_pc_loaded)
			epc_load();
		fh = epc_hash(s_chunky, (long)ECS_W * ECS_H);
		for (k = 0; k < e_pc_n; k++)
			if (e_pc_key[k].clut_h == ch && e_pc_key[k].frm_h == fh) {
				hit = k;
				break;
			}
	}
	if (hit < 0 && ecs_clut_cache) {
		short k;

		for (k = 0; k < s_erc_n; k++) {
			struct erc_ent *e = &s_erc[s_erc_mru[k]];

			if (e->valid && e->clut_h == ch
			    && memcmp(e->clut, s_clut, 768) == 0) {
				rhit = s_erc_mru[k];
				if (k) {        /* promote to MRU */
					short v = s_erc_mru[k];
					for (; k > 0; k--)
						s_erc_mru[k] = s_erc_mru[k - 1];
					s_erc_mru[0] = v;
				}
				break;
			}
		}
	}
	if (hit >= 0 && epc_read_blob(hit)) {
#ifdef FRUA_ECSTRACE
		dbg_log_num("et: reband CACHE HIT entry = ", (long)hit);
#endif
	} else if (rhit >= 0) {
		struct erc_ent *e = &s_erc[rhit];

		/* Compare BEFORE the copy: "same remaps" means the slots on the
		 * glass keep their meaning and only the RGB behind them moves. */
		remap_same = e_quant_valid
		    && memcmp(s_band_remap, e->rem, EPC_REMAP) == 0;
		memcpy(s_band_pal,   e->pal, EPC_PAL);
		memcpy(s_band_remap, e->rem, EPC_REMAP);
		dbg_log_num("ecs: reband CLUT-cache hit, remap_same = ",
		            (long)remap_same);
	} else {
		{
			short ngrp = 0, gb0[4];

			if (ecs_vp_groups && e_vp_commit && e_vp_hh > 0) {
				unsigned char now[16];

				ecs_vp_sample(now);
				if (memcmp(now, e_vp_sig, 16) != 0) {
					e_vp_commit = 0;   /* vp overdrawn: stale */
				} else {
					short t = (short)(e_vp_y / ECS_RPB);
					short b = (short)((e_vp_y + e_vp_hh
					                   + ECS_RPB - 1) / ECS_RPB);
					if (t > 0 && b < ECS_NBANDS && b > t) {
						ngrp = 3;
						gb0[0] = 0; gb0[1] = t;
						gb0[2] = b; gb0[3] = ECS_NBANDS;
					}
				}
			}
			if (ngrp == 3) {
				unsigned char gpal[ECS_NCOL * 3], grem[256];
				short g, band;
#ifdef FRUA_AMIGAPROF
				long g0 = amiga_prof_rl();
#endif

				for (g = 0; g < 3; g++) {
					short r0   = (short)(gb0[g] * ECS_RPB);
					short rows = (short)((gb0[g + 1] - gb0[g])
					                     * ECS_RPB);

					quant_banded(s_chunky + (long)r0 * ECS_W,
					             ECS_W, rows, s_clut,
					             1, ECS_NCOL, ECS_BITS,
					             gpal, grem);
					for (band = gb0[g]; band < gb0[g + 1]; band++) {
						memcpy(s_band_pal
						       + (long)band * ECS_NCOL * 3,
						       gpal, ECS_NCOL * 3);
						memcpy(s_band_remap
						       + (long)band * 256, grem, 256);
					}
				}
				dbg_log_num("ecs: reband GROUPED bands t*100+b = ",
				            (long)gb0[1] * 100 + gb0[2]);
#ifdef FRUA_AMIGAPROF
				dbg_log_num("ecs: GROUPED cut rl = ",
				            amiga_prof_rl() - g0);
#endif
			} else {
				quant_banded(s_chunky, ECS_W, ECS_H, s_clut,
				             ECS_NBANDS, ECS_NCOL, ECS_BITS,
				             s_band_pal, s_band_remap);
			}
		}
		ecs_unify_border(1);    /* one COLOR00 for the whole border */
		if (ecs_pal_cache)
			epc_append(ch, fh);     /* -> disk (unbounded) */
	}
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
	/* RAM-cache hit: indices THIS frame uses that the cached cut never saw
	 * would resolve through the cut's absent-colour bucketing (the ST's
	 * grey-glyph family). Re-point each at its nearest slot per band.
	 * Any patch moves a remap, so the palettes-only shortcut is off. */
	if (rhit >= 0) {
		const struct erc_ent *e = &s_erc[rhit];
		short c;

		for (c = 0; c < 256; c++) {
			short bnd;

			if (!e_used_idx[c] || e->used[c])
				continue;
			for (bnd = 0; bnd < ECS_NBANDS; bnd++) {
				const unsigned char *bpal =
				    s_band_pal + (long)bnd * ECS_NCOL * 3;
				short s, bs = 0;
				long  bd = 0x7fffffffL;

				for (s = 0; s < ECS_NCOL; s++) {
					long d = e_coldist(s_clut + (long)c * 3,
					                   bpal + (long)s * 3);
					if (d < bd) { bd = d; bs = s; }
				}
				s_band_remap[(long)bnd * 256 + c] = (unsigned char)bs;
			}
			remap_same = 0;
		}
	}
	/* Remember this cut for the next time its CLUT comes around (the walk
	 * alternation). Fresh cuts and exact disk hits both store; a RAM hit
	 * is already stored. */
	if (ecs_clut_cache && rhit < 0) {
		short slot, k;
		struct erc_ent *e;

		if (s_erc_n < ERC_N) {
			slot = s_erc_n++;
		} else {
			slot = s_erc_mru[ERC_N - 1];    /* evict the LRU */
		}
		e = &s_erc[slot];
		e->clut_h = ch;
		e->valid  = 1;
		memcpy(e->clut, s_clut, 768);
		memcpy(e->pal,  s_band_pal, EPC_PAL);
		memcpy(e->rem,  s_band_remap, EPC_REMAP);
		memcpy(e->used, e_used_idx, 256);
		for (k = 0; k < s_erc_n; k++)   /* promote to MRU */
			if (s_erc_mru[k] == slot) break;
		if (k >= s_erc_n) k = (short)(s_erc_n - 1);
		for (; k > 0; k--)
			s_erc_mru[k] = s_erc_mru[k - 1];
		s_erc_mru[0] = slot;
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
#ifdef FRUA_ECSERR
	/* #19: how FAR is any used colour from the slot it was mapped to?
	 * Normal quantisation error is small; a colour landing somewhere
	 * unrelated (a red gradient pixel painted beige) is the artefact. */
	{
		short bb, ii;
		for (bb = 0; bb < ECS_NBANDS; bb++) {
			const unsigned char *brem = s_band_remap + (long)bb * 256;
			const unsigned char *bpal = s_band_pal + (long)bb * ECS_NCOL * 3;
			long worst = 0; short wi = -1;

			for (ii = 0; ii < 256; ii++) {
				long d;
				if (!e_used_band[bb][ii]) continue;
				d = e_coldist(s_clut + (long)ii * 3,
				              bpal + (long)brem[ii] * 3);
				if (d > worst) { worst = d; wi = ii; }
			}
			if (worst > 2000)
				dbg_log_num("err: band*100000+worst = ",
				            (long)bb * 100000L + (worst > 99999 ? 99999 : worst));
			if (worst > 2000 && wi >= 0) {
				const unsigned char *cc = s_clut + (long)wi * 3;
				const unsigned char *sc = bpal + (long)brem[wi] * 3;
				dbg_log_num("err:   idx*1000+slot = ",
				            (long)wi * 1000 + brem[wi]);
				dbg_log_num("err:   WANT rgb = ",
				            ((long)cc[0] << 16) | ((long)cc[1] << 8) | cc[2]);
				dbg_log_num("err:   GOT  rgb = ",
				            ((long)sc[0] << 16) | ((long)sc[1] << 8) | sc[2]);
			}
		}
	}
#endif
#ifdef FRUA_ECSTITLE
	/* #16: after the cut, does any BAND PALETTE hold the magenta sentinel?
	 * A slot holding it paints every pixel mapped to that slot. */
	{
		short bb, ii; long nmag = 0; short firstb = -1, firsts = -1;

		for (bb = 0; bb < ECS_NBANDS; bb++) {
			const unsigned char *bp = s_band_pal + (long)bb * ECS_NCOL * 3;

			for (ii = 0; ii < ECS_NCOL; ii++)
				if (bp[ii*3+0] > 200 && bp[ii*3+1] < 60 && bp[ii*3+2] > 200) {
					nmag++;
					if (firstb < 0) { firstb = bb; firsts = ii; }
				}
		}
		dbg_log_num("ttl: band-palette MAGENTA slots = ", nmag);
		if (nmag)
			dbg_log_num("ttl:   first band*100+slot     = ",
			            (long)firstb * 100 + firsts);
	}
#endif
	/* ★ #165: DO NOT show a new palette over the old planes. This used to
	 * write the copper words right here — but when the caller is
	 * ecs_render(), the planes this palette describes do not exist yet:
	 * the whole-screen remap + c2p that build them take ~12 SECONDS on a
	 * 7 MHz 68000 (measured, FRUA_ECSTRACE: render start rl 446597 -> end
	 * 637848), and the copper spends every one of them showing the
	 * OUTGOING screen's pixels under the INCOMING screen's per-band
	 * palettes. That is the A500 title corruption exactly: the old image
	 * in wrong colours, sliced horizontally because the bands ARE the
	 * copper's palette regions, resolving only when the flip finally
	 * lands.
	 *
	 * So when a flip is coming, stage the words and let ecs_render commit
	 * them WITH the plane switch. The content-same (remap_same) arm still
	 * writes immediately and must: there the planes on screen are already
	 * correct and only the RGB behind the slots moved — that is the
	 * repalette shortcut, and deferring it would be the #140 bug (a
	 * palette install must not wait for a present).
	 *
	 * The ST never showed this because its full present draws the hidden
	 * page and flips (dsp pages == 1), so its palette and planes have
	 * always changed together — which is why the same build shows clean
	 * STe titles and corrupt ECS titles. */
	if (defer && !remap_same)
		e_cop_pending = 1;
	else
		ecs_cop_pal_commit();
	s_dirty = 0;
	s_have_pal = 1;
	if (remap_same) {
		/* The restored cut assigns every index the SAME slot the live one
		 * did — the planes (and the draw-time stamps) on screen are still
		 * right, only the RGB behind the slots moved, and the copper words
		 * above already carry it. No force-full, no epoch reset: this is
		 * what makes the walk's palette alternation cost a copper reload
		 * instead of a full re-render. */
		dbg_log("ecs: reband -> palettes only (remaps unchanged)");
	} else {
#ifdef FRUA_PLANAR
		ecs_dt_epoch_reset();   /* slots renumbered: stamps are stale */
#endif
		s_force_full = 1;       /* every LUT moved: row diffing is void */
	}
}

#ifdef FRUA_AMIGAPROF
/* Declared HERE, above ecs_render, not with the other ap_* counters further
 * down — ecs_render precedes them in the file. (Same ordering trap the ST's
 * sp_vp_rearm hit; it costs a build and, if the rc is not checked, a whole run
 * on the STALE binary that looks like a measurement.) */
static long ap_quant_t, ap_quant_n;   /* ecs_reband alone (the 25 cuts)      */
static long ap_remap_t, ap_c2pf_t;    /* whole-screen remap / c2p in render  */
#endif

#ifdef FRUA_ECSTITLE
static short ecs_sentinel_clut(void);
static long  ecs_sentinel_pixels(void);
#endif

/* Full render: (re-band if dirty), remap the whole surface, convert to the
 * back plane set, flip. */
static void ecs_render(void)
{
	unsigned char *back = s_planes[s_front ^ 1];
#ifdef FRUA_TITLESEQ
	dbg_log("ecsp: RENDER begin");
#endif
	unsigned char *planes[ECS_DEPTH];
	short p;

	if (s_dirty) {
#ifdef FRUA_AMIGAPROF
		long q0 = amiga_prof_rl();
#endif
#ifdef FRUA_ECSTITLE
		dbg_log_num("ttl: RENDER, sentinel CLUT entries = ",
		            (long)ecs_sentinel_clut());
		dbg_log_num("ttl:   chunky px on sentinel slots = ",
		            ecs_sentinel_pixels());
#endif
		ecs_reband(1);            /* #165: palette lands at the flip */
#ifdef FRUA_AMIGAPROF
		ap_quant_t += amiga_prof_rl() - q0; ap_quant_n++;
#endif
	}
#ifdef FRUA_AMIGAPROF
	{ long r0 = amiga_prof_rl();
#endif
	remap_rect(0, 0, ECS_W, ECS_H);
#ifdef FRUA_AMIGAPROF
	ap_remap_t += amiga_prof_rl() - r0; }
#endif
	for (p = 0; p < ECS_DEPTH; p++)
		planes[p] = back + (ULONG)p * ECS_PITCH * ECS_H;
#ifdef FRUA_AMIGAPROF
	{ long p0 = amiga_prof_rl();
#endif
	c2p_amiga_n(s_remap_buf, planes, ECS_W, ECS_H, ECS_PITCH, ECS_DEPTH);
#ifdef FRUA_AMIGAPROF
	ap_c2pf_t += amiga_prof_rl() - p0; }
#endif
	CopyMem(s_chunky, s_shadow, (ULONG)ECS_W * ECS_H);
	/* #165: planes and palette change together — the staged words go in
	 * beside the new plane pointers, so no frame ever shows one screen's
	 * pixels under another screen's colours. */
#ifdef FRUA_TITLESEQ
	dbg_log("ecsp: RENDER end (flip)");
#endif
	if (e_cop_pending)
		ecs_cop_pal_commit();
	cop_point_planes(back);
	s_front ^= 1;
#ifdef FRUA_ECSTRACE
	dbg_log_num("et: FLIP to buffer      = ", (long)s_front);
	dbg_log_num("et: flip at rl          = ", amiga_prof_rl());
#endif
	s_force_full = 0;
}

/* Full present with ROW DIFFING (the ST backend's fix, ported: a real bare-ECS
 * machine is a 7MHz 68000, slower than the STE). The engine's modal loops end
 * every pass in a full present; converting all 64000 pixels each time reads as
 * frozen input at that speed. Diffed rows convert straight into the DISPLAYED
 * plane set — the same policy present_rect already uses (at worst one frame of
 * shear inside a changed row); the tear-free back-buffer flip is reserved for
 * the force-full path (a re-band), where every row converts anyway. */
/* #63 SCAN NARROWING (ported from the ST backend, 2026-08-08).
 *
 * The full present used to run ecs_row_differs over ALL 200 rows to learn that a
 * handful moved — and FRUA_AMIGAPROF's first census showed exactly that: full
 * presents outnumbering rect presents while converting ~0 rows, the whole cost
 * being the scan. Only rows a writer ANNOUNCED (planar_touch_rows, the shared
 * machine-neutral set in platform/planar.c that the ST already drives) can have
 * changed, so scan those and skip the rest without reading them.
 *
 * ONE pending set, not the ST's per-page pair: the ST alternates pages on every
 * full present, but here the incremental path always writes s_planes[s_front]
 * and never flips — only ecs_render flips, and it rebuilds the whole page and
 * re-syncs the shadow, so the page it flips in is current by construction (the
 * gather is reset there).
 *
 * CONSERVATIVE BY DEFAULT: before the first gather, and whenever the shared set
 * says "scan everything" (planar_dirty_rows != 0), every row is pending — so a
 * backend that never sees an announcement behaves exactly as before. The unsafe
 * direction is a writer changing a row WITHOUT announcing (that row would stay
 * stale forever), which is what FRUA_DIRTYCHECK below polices. */
static unsigned char e_pend[ECS_H];
static short         e_pend_init;

/* #165: rows the counting pass found actually changed (see the tear-free
 * comment in the full present). Separate from e_pend so the diff runs once. */
static unsigned char e_chg[ECS_H];

extern short ecs_tearfree;      /* declared with the other video.cfg flags */
#define ECS_TEARFREE_ROWS (ecs_tearfree > 0 ? ecs_tearfree : ECS_H + 1)
#ifdef FRUA_DIRTYCHECK
long g_ecs_dirtycheck_miss;
#endif

static void ecs_pend_all(void)
{
	memset(e_pend, 1, sizeof e_pend);
	e_pend_init = 1;
}

static void ecs_pend_gather(void)
{
	const unsigned char *drows;
	short y;

	if (!e_pend_init)
		ecs_pend_all();
	else if (planar_dirty_rows(&drows))          /* "scan everything" */
		ecs_pend_all();
	else
		for (y = 0; y < ECS_H; y++)
			if (drows[y])
				e_pend[y] = 1;
}

#ifdef FRUA_AMIGAPROF
/* Play-loop census — the ECS analog of the Atari b63play (FRUA_STPROF). All
 * times are rasterlines (~64us); "conv" is the c2p (remap + c2p_amiga_n_rect),
 * the walk's real cost and the same thing the ST composite work attacked. Dumps
 * to DBG.LOG every 8 rect presents (the walk-step present is a rect). */
static long ap_rect_n, ap_rect_t, ap_full_n, ap_full_t;
static long ap_conv_t, ap_conv_rows, ap_wall0 = -1;
static long ap_scanned_rows;    /* rows the narrowed scan actually READ (#63):
                                 * 200/full-present before narrowing. */

static void ecs_prof_dump(void)
{
	long now = amiga_prof_rl();
	long wall = (ap_wall0 < 0) ? 0 : now - ap_wall0;

	ap_wall0 = now;
	dbg_log_num("apecs: rect presents = ", ap_rect_n);
	dbg_log_num("apecs: rect rl       = ", ap_rect_t);
	dbg_log_num("apecs: full presents = ", ap_full_n);
	dbg_log_num("apecs: full rl       = ", ap_full_t);
	dbg_log_num("apecs:  of which conv= ", ap_conv_t);
	dbg_log_num("apecs:  conv rows    = ", ap_conv_rows);
	dbg_log_num("apecs:  rows SCANNED = ", ap_scanned_rows);
	dbg_log_num("apecs:  REBANDS      = ", ap_quant_n);
	dbg_log_num("apecs:  reband rl    = ", ap_quant_t);
	{ extern long quant_ph_hist, quant_ph_keep, quant_ph_cut,
	              quant_ph_remap, quant_ph_buck;
	dbg_log_num("apecs:   q hist      = ", quant_ph_hist);
	dbg_log_num("apecs:   q keep      = ", quant_ph_keep);
	dbg_log_num("apecs:   q cut       = ", quant_ph_cut);
	dbg_log_num("apecs:   q remap     = ", quant_ph_remap);
	dbg_log_num("apecs:   q buckets   = ", quant_ph_buck); }
	dbg_log_num("apecs:  full remap rl= ", ap_remap_t);
	dbg_log_num("apecs:  full c2p rl  = ", ap_c2pf_t);
#ifdef FRUA_DIRTYCHECK
	dbg_log_num("apecs:  UNANNOUNCED  = ", g_ecs_dirtycheck_miss);
#endif
	/* WHICH primitive claimed the whole frame. Same site labels the TT dumps
	 * (compat/quickdraw.c g_qdt_hits) — label the data, never infer: a
	 * "rows SCANNED == 200 x full presents" reading means somebody called
	 * qd_touch_all, and this names them. */
	{
		extern long g_qdt_hits[8];
		dbg_log_num("apecs:  qdt0 grab    = ", g_qdt_hits[0]);
		dbg_log_num("apecs:  qdt1 fill    = ", g_qdt_hits[1]);
		dbg_log_num("apecs:  qdt2 blit    = ", g_qdt_hits[2]);
		dbg_log_num("apecs:  qdt3 palette = ", g_qdt_hits[3]);
		dbg_log_num("apecs:  qdt4 cursor  = ", g_qdt_hits[4]);
		dbg_log_num("apecs:  qdt5 glyph   = ", g_qdt_hits[5]);
	}
	dbg_log_num("apecs: wall rl       = ", wall);
	if (wall > 0)
		dbg_log_num("apecs: display per1000= ",
		            ((ap_rect_t + ap_full_t) * 1000L) / wall);
	ap_rect_n = ap_rect_t = ap_full_n = ap_full_t = 0;
	ap_conv_t = ap_conv_rows = ap_scanned_rows = 0;
}
#endif

/* ★ GIVE NEW INK A SLOT INSTEAD OF THE NEAREST WRONG ONE (#dim).
 *
 * The per-band cut is computed from the surface AS IT STOOD. Anything drawn
 * afterwards is not in that band's palette, so remap_rect resolves it through
 * the absent-colour bucket — the nearest slot the band happens to own. On the
 * ECS that is the reported bug: an event's first text line lands in the band
 * the PICTURE occupies, which owns a cyan slot because the picture is cyan
 * there, and the following lines land in the bands BELOW the picture, which
 * never saw a cyan pixel and resolve the same glyphs to grey. Measured on the
 * HEIRS innkeeper event: line 1 ink #00bbbb, lines 2 and 3 #888888, and the
 * split falls exactly on the band boundary (line 1 in band 13, lines 2-3 in
 * bands 14-15).
 *
 * The remedy was a placeholder: ecs_ink_hold skips ONE present in case a
 * palette install is following, and failing that the ink "paints on the NEXT
 * present via the bucket fallback exactly as before". The bucket fallback is
 * the grey glyph.
 *
 * ★ AND THE BAND HAD ROOM THE WHOLE TIME. Instrumenting the text-box bands
 * says they use 7 to 11 of their 32 slots, so 21+ sit empty while the glyph is
 * forced onto a slot 3675 away. Nothing needs re-quantising and nothing needs
 * to be sacrificed: hand the colour an EMPTY slot.
 *
 * Writing a free slot's COLOR word is safe in a way a palette install is not
 * (#165): free means no used index maps to it, so no pixel on screen wears
 * that slot and changing its RGB cannot recolour the outgoing frame.
 *
 * Only adopts when the bucket is actually bad (ECS_INK_NEAR), so a glyph whose
 * colour the band already holds costs one distance test and nothing else.
 * Per-band, because a colour absent HERE may be present one band down. */
#define ECS_INK_NEAR 256        /* bucket this close: leave it alone */

static short ecs_ink_adopt_scan(void)
{
	short y, adopted = 0;

	if (!ecs_ink_adopt || !s_have_pal || !e_quant_valid)
		return 0;

	for (y = 0; y < ECS_H; y++) {
		short band = (short)((long)y * ECS_NBANDS / ECS_H);
		const unsigned char *row = s_chunky + (long)y * ECS_W;
		unsigned char *bpal = s_band_pal + (long)band * ECS_NCOL * 3;
		unsigned char *brem = s_band_remap + (long)band * 256;
		short x;

		if (!e_pend[y])
			continue;

		for (x = 0; x < ECS_W; x++) {
			unsigned char c = row[x];
			const unsigned char *cc = s_clut + (long)c * 3;
			short s, slot, free_slot = -1;
			long bd = 0x7fffffffL;

			if (e_used_band[band][c])
				continue;               /* the band already knows this ink */

			for (s = 0; s < ECS_NCOL; s++) {
				long d;

				if (e_slot_rep[band][s] == 0xFF) {
					if (free_slot < 0)
						free_slot = s;
					continue;           /* empty slots hold no colour yet */
				}
				d = e_coldist(cc, bpal + (long)s * 3);
				if (d < bd) { bd = d; }
			}

			e_used_band[band][c] = 1;   /* decided either way — do it once */

			if (bd <= ECS_INK_NEAR || free_slot < 0)
				continue;               /* good enough, or no room */

			slot = free_slot;
			bpal[slot * 3 + 0] = quant_snap(cc[0], ECS_BITS);
			bpal[slot * 3 + 1] = quant_snap(cc[1], ECS_BITS);
			bpal[slot * 3 + 2] = quant_snap(cc[2], ECS_BITS);
			brem[c] = (unsigned char)slot;
			e_slot_rep[band][slot] = c;
			e_used_idx[c] = 1;          /* stops the ink-hold re-firing */
			*s_cop_pal[band][slot] =
				(UWORD)(((bpal[slot * 3 + 0] >> 4) << 8)
					  | ((bpal[slot * 3 + 1] >> 4) << 4)
					  |  (bpal[slot * 3 + 2] >> 4));
			adopted++;
#ifdef FRUA_PLANAR
			/* Rows in this band already stamped by a draw-time writer used the
			 * OLD remap for c, so they show the bucket slot. Drop the band's
			 * coverage and let them re-bridge with the corrected map. */
			{
				short r0 = (short)((long)band * ECS_H / ECS_NBANDS);
				short r1 = (short)((long)(band + 1) * ECS_H / ECS_NBANDS);
				short r;

				for (r = r0; r < r1 && r < ECS_H; r++) {
					e_dt_rowcov[r] = 0;
					e_pend[r] = 1;      /* re-announce: its planes are stale */
				}
			}
#endif
		}
	}
	if (adopted)
		dbg_log_num("ecs: ink adopted free slots = ", (long)adopted);
	return adopted;
}

#ifdef FRUA_ECSERR
/* #19: does the COPPER actually show what the cut computed?
 *
 * FRUA_ECSERR's other probe checks s_band_pal against the CLUT — i.e. that
 * the cut chose sensible colours. This checks the OTHER half of the path:
 * that the words the copper reads still agree with s_band_pal. A code path
 * that writes one without the other diverges silently, and the screen then
 * shows a colour nobody computed. In a smooth vertical gradient one wrong
 * slot in one band paints exactly the 3-60px horizontal run #19 describes.
 *
 * Encoding must match every writer: 4 bits per gun, ((r>>4)<<8)|((g>>4)<<4)|
 * (b>>4). */
static void ecs_cop_verify(short tag)
{
	short b, i, bad = 0;

	for (b = 0; b < ECS_NBANDS; b++) {
		const unsigned char *bp = s_band_pal + (long)b * ECS_NCOL * 3;

		for (i = 0; i < ECS_NCOL; i++) {
			UWORD want = (UWORD)(((bp[i * 3 + 0] >> 4) << 8)
			                   | ((bp[i * 3 + 1] >> 4) << 4)
			                   |  (bp[i * 3 + 2] >> 4));
			UWORD got = *s_cop_pal[b][i];

			if (want != got) {
				if (bad < 6)
					dbg_log_num("cop: MISMATCH band*100+slot = ",
					            (long)b * 100 + i);
				if (bad < 6)
					dbg_log_num("cop:   want*65536+got = ",
					            ((long)want << 16) | got);
				bad++;
			}
		}
	}
	/* ★ A ZERO IS ONLY MEANINGFUL IF THE PROBE RAN. Report the checked
	 * count periodically, so "no mismatches" cannot be confused with "never
	 * executed" — that confusion has cost this project three debugging
	 * rounds (a missing objdump, a missing binary, a converter pointed at a
	 * missing file, all reading as a clean pass). */
	{
		static long calls;

		if ((calls++ % 8) == 0)
			dbg_log_num("cop: checked words*1000+bad = ",
			            (long)ECS_NBANDS * ECS_NCOL * 1000L + bad);
	}
	if (bad)
		dbg_log_num("cop: total mismatched words (tag) = ",
		            (long)tag * 10000 + bad);
}
#endif

static void ecs_present(void)
{
	unsigned char *front;
#ifdef FRUA_TITLESEQ
	{ static long np; dbg_log_num("ecsp: present#*100+dirty*10+force = ",
	  (long)(++np) * 100 + (long)(s_dirty ? 1 : 0) * 10 + (s_force_full ? 1 : 0)); }
#endif
	unsigned char *planes[ECS_DEPTH];
	short p, y;
#ifdef FRUA_AMIGAPROF
	long ap_t0 = amiga_prof_rl();
#endif

	if (s_dirty && e_quant_valid) {
		if (memcmp(s_clut, e_clut_quant, sizeof e_clut_quant) == 0) {
			/* CLUT-guard: identical CLUT would reproduce identical
			 * bands — the engine's defensive re-install. Skip. */
			dbg_log("ecs: quant skipped (CLUT unchanged)");
			s_dirty = 0;
		} else if (!s_force_full
		           && !ecs_row_differs(s_chunky, s_shadow,
		                               (long)ECS_W * ECS_H)
		           && !ecs_remap_split()
		           && !(ecs_majq && ecs_clut_major_change())) {
			/* Content unchanged: remaps stay valid, only slot->RGB
			 * moved — copper reload, no re-quant, no re-render. */
			dbg_log("ecs: repalette (content unchanged)");
			ecs_repalette();
			s_dirty = 0;
		}
	}
#ifdef FRUA_AMIGAPROF
	{ long ap_c0 = amiga_prof_rl();
#endif
	/* ★ #16 DO NOT CUT A PALETTE THAT HAS NOT FINISHED ARRIVING.
	 *
	 * l19d4 brackets each title's composition in qd_palette_blackout().
	 * On a hardware-palette backend that pushes black so the half-built
	 * screen is simply not seen, and the finished title pops in whole.
	 * A quantiser cannot do that — its planes hold slots, not indices —
	 * and it has the worse problem anyway: a CUT taken mid-bracket is
	 * BAKED IN. Measured on the A500 title (docs/TODO.md #16): 975 chunky
	 * pixels still referenced CLUT entries holding the art's magenta
	 * defer sentinel, and the cut spent 10 of every band's 32 slots on
	 * magenta — a fifth of the budget gone, which is why the screen read
	 * as "colours all inverted" rather than merely speckled.
	 *
	 * So defer: keep showing the last COMPLETE frame and let the palette
	 * finish. The engine's own install at the end of the bracket sets
	 * s_dirty, and this present then cuts against a whole CLUT.
	 *
	 * ★ BOUNDED, because #8 taught what an unbounded wait costs. If the
	 * bracket somehow never closes we lose the title, not the game: after
	 * ECS_TITLEDEFER_MAX presents we cut anyway and take the old
	 * behaviour. The bound is on PRESENTS, not wall-clock, for the same
	 * machine-independence reason as #8's. */
	if (ecs_title_defer && (s_force_full || s_dirty)
	    && dsp_palette_incomplete() && e_quant_valid
	    && e_title_defer_n < ECS_TITLEDEFER_MAX) {
		e_title_defer_n++;
		dbg_log_num("ecs: title cut DEFERRED, present = ",
		            (long)e_title_defer_n);
		return;
	}
	e_title_defer_n = 0;

	if (s_force_full || s_dirty) {
#ifdef FRUA_ECSTRACE
		dbg_log_num("et: FULL render start rl= ", amiga_prof_rl());
#endif
		ecs_render();
#ifdef FRUA_ECSTRACE
		dbg_log_num("et: FULL render end rl  = ", amiga_prof_rl());
#endif
		/* The flipped-in page was rebuilt whole and the shadow re-synced:
		 * nothing is owed. (Matches the ST force-full's pend reset.) */
		memset(e_pend, 0, sizeof e_pend);
		e_pend_init = 1;
	} else {
		front = s_planes[s_front];
		for (p = 0; p < ECS_DEPTH; p++)
			planes[p] = front + (ULONG)p * ECS_PITCH * ECS_H;
		ecs_pend_gather();

		/* ★ DO NOT PAINT A SCREEN THE PALETTE CANNOT SHOW (the boot's
		 * 70-second bar). The title sequence draws each screen's art and
		 * presents BEFORE installing its palette, so this branch would
		 * convert the new art through the PREVIOUS screen's bands — on a
		 * real boot that rendered as black with one garbled strip, held for
		 * the length of the following re-band.
		 *
		 * ★ AND RENDERING NOW IS ALSO WRONG — the first cut of this fix
		 * re-banded and rendered in this present, reasoning that a held
		 * frame with no follow-up present would stay stale forever. Measured
		 * on the boot: the re-quant runs against the CLUT AS IT STANDS,
		 * which is exactly what is stale — it painted the whole SSI screen
		 * in well-quantised garbage colours and added a 9.5 s re-band per
		 * title. No amount of quantising fixes indices whose CLUT entries
		 * have not arrived.
		 *
		 * So: HOLD, once. Keep the announcements pending (e_pend survives —
		 * it is only consumed per-row by conversion or wholesale by a full
		 * render) and show the last complete frame. The title case gets its
		 * palette install immediately after, which sets s_dirty and takes
		 * the full-render path with the REAL palette — the garbage is never
		 * painted. The legitimate case — new ink whose CLUT entries are
		 * fine, an event portrait say — paints on the NEXT present via the
		 * bucket fallback exactly as before, one modal-loop pass later,
		 * because a second consecutive hold is not allowed.
		 *
		 * Threshold 8, not the scheduler's 4: a false fire here delays a
		 * paint by one present, but chrome/glyph updates should never trip
		 * it. One table lookup per pixel of the announced rows. */
		if (ecs_ink_hold && s_have_pal && !e_held_once) {
			long ink = 0;
			short yy;

			for (yy = 0; yy < ECS_H && ink < 8; yy++) {
				const unsigned char *row;
				short xx;

				if (!e_pend[yy])
					continue;
				row = s_chunky + (long)yy * ECS_W;
				for (xx = 0; xx < ECS_W; xx++)
					if (!e_used_idx[row[xx]] && ++ink >= 8)
						break;
			}
			if (ink >= 8) {
#ifdef FRUA_ECSTRACE
				dbg_log_num("et: INK-HOLD (skip paint), rl = ",
				            amiga_prof_rl());
#endif
				e_held_once = 1;
				return;
			}
		}
		e_held_once = 0;

		/* #dim: before converting, give any new ink a free slot. */
		ecs_ink_adopt_scan();

		/* ★ #165 SCENE CHANGES GO THROUGH THE FLIP, NOT THE VISIBLE PAGE.
		 * This loop converts changed rows STRAIGHT INTO s_planes[s_front]
		 * — the page the copper is showing. At 7 MHz a screen's worth of
		 * rows takes seconds to convert, so a scene change is watched
		 * arriving band by band over the outgoing screen, and the rows
		 * still to come are the OLD image under the NEW palette (the
		 * palette commits before the blits on a quantiser). That is the
		 * A500 field report verbatim: "it loads half, then runs out of
		 * buffer, then loads the other half, with various mixing in of
		 * CLUT swapping" — captured in amiberry as the AD&D screen
		 * banding in over the SSI screen.
		 *
		 * The ST never showed this because it double-buffers INTERNALLY
		 * (dsp pages == 1: every full present draws the hidden page and
		 * flips), which is why the user reports the STe titles clean and
		 * the ECS titles corrupt on the same build.
		 *
		 * Per-row direct writes stay for SMALL updates — a HUD line, a
		 * glyph box — where the tear is one row of one frame and the
		 * alternative (a whole-page rebuild) costs far more than it
		 * saves. Above the threshold, hand the frame to ecs_render(),
		 * which rebuilds the whole page in the BACK buffer and flips it
		 * in atomically. A change that big was going to convert most of
		 * the screen anyway, so the extra work is bounded by design.
		 *
		 * Counting first (into e_chg) rather than deciding mid-loop keeps
		 * the diff work single-pass: the rows we counted are exactly the
		 * rows the conversion loop below consumes. */
		{
			short nchg = 0;

			for (y = 0; y < ECS_H; y++) {
				e_chg[y] = 0;
				if (!e_pend[y])
					continue;
#ifdef FRUA_AMIGAPROF
				ap_scanned_rows++;
#endif
				if (ecs_row_differs(s_chunky + (long)y * ECS_W,
				                    s_shadow + (long)y * ECS_W,
				                    ECS_W)) {
					e_chg[y] = 1;
					nchg++;
				}
				e_pend[y] = 0;
			}
			if (nchg >= ECS_TEARFREE_ROWS) {
#ifdef FRUA_ECSTRACE
				dbg_log_num("et: TEAR-FREE full render, rows = ",
				            (long)nchg);
#endif
				ecs_render();           /* whole page + flip */
				memset(e_pend, 0, sizeof e_pend);
				e_pend_init = 1;
				return;
			}
		}

		for (y = 0; y < ECS_H; y++) {
			/* #63: only an announced row can have moved; the rest keep
			 * their shadow and are skipped without being read. */
			int changed = e_chg[y];

#ifdef FRUA_DIRTYCHECK
			/* THE POLICE (the ST's, ported): re-run the OLD unconditional
			 * diff on the rows the counting pass skipped (unannounced).
			 * Any hit is a writer that changed a row without announcing
			 * it — which would leave that row stale on screen forever.
			 * Must read ZERO over a full drive before anyone trusts the
			 * narrowed scan. */
			if (!changed
			 && ecs_row_differs(s_chunky + (long)y * ECS_W,
			                    s_shadow + (long)y * ECS_W, ECS_W)) {
				g_ecs_dirtycheck_miss++;
				dbg_log_num("apecs MISS unannounced row = ", (long)y);
				changed = 1;             /* self-heal, then report */
			}
#endif
			if (!changed)
				continue;
#ifdef FRUA_AMIGAPROF
			ap_conv_rows++;
#endif
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
#ifdef FRUA_ECSTRACE
		{
			static long et_n;
			short yy, lo = -1, hi = -1, nrows = 0;

			/* recount from the shadow-sync we just did? cheaper: track in
			 * the loop would touch shipping code; a coarse summary is
			 * enough — log the pend gather result once per present. */
			(void)yy; (void)lo; (void)hi; (void)nrows;
			dbg_log_num("et: INCR present #      = ", ++et_n);
			dbg_log_num("et: INCR at rl          = ", amiga_prof_rl());
		}
#endif
	}
#ifdef FRUA_AMIGAPROF
	ap_conv_t += amiga_prof_rl() - ap_c0; }
#endif
	/* NEW-INK re-quant trigger: SCHEDULE only — the next full present
	 * re-bands against the complete frame (the standing mid-draw policy). */
	if (s_have_pal && e_new_ink >= 4) {
		s_dirty       = 1;
		e_quant_valid = 0;  /* bypass the CLUT-guard: content changed */
	}
	e_new_ink = 0;
#ifdef FRUA_ECSERR
	/* #19: end of present — the frame now up is what the copper is showing,
	 * so this is the moment the words must agree with the cut. */
	if (s_have_pal)
		ecs_cop_verify(1);
#endif
#ifdef FRUA_AMIGAPROF
	ap_full_t += amiga_prof_rl() - ap_t0;
	ap_full_n++;
	/* The dump also has to fire on FULL presents. It hung off the rect
	 * path alone, and the boot/title sequence issues no rect presents — so the
	 * UNANNOUNCED counter was compiled, incremented, and never printed. */
	if ((ap_full_n & 7) == 0)
		ecs_prof_dump();
#endif
}

static void ecs_present_rect(short x, short y, short w, short h)
{
	unsigned char *front = s_planes[s_front];
#ifdef FRUA_ECSTRACE
	dbg_log_num("et: RECT y*1000+h at rl = ", (long)y * 1000 + h);
#endif
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
#ifdef FRUA_AMIGAPROF
	long ap_t0 = amiga_prof_rl();
#endif

	x1 = (short)((x + w + 7) & ~7);
	x  = (short)(x & ~7);
	w  = (short)(x1 - x);

#ifdef FRUA_AMIGAPROF
	{ long ap_c0 = amiga_prof_rl();
#endif
	remap_rect(x, y, w, h);
	for (p = 0; p < ECS_DEPTH; p++)
		planes[p] = front + (ULONG)p * ECS_PITCH * ECS_H;
	c2p_amiga_n_rect(s_remap_buf, ECS_W, planes, ECS_PITCH,
	                 x, y, w, h, ECS_DEPTH);
#ifdef FRUA_AMIGAPROF
	ap_conv_t += amiga_prof_rl() - ap_c0; ap_conv_rows += h; }
#endif
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
#ifdef FRUA_AMIGAPROF
	ap_rect_t += amiga_prof_rl() - ap_t0;
	if ((++ap_rect_n & 7) == 0)
		ecs_prof_dump();
#endif
}


#ifdef FRUA_ECSTITLE
/* #16 — is the title being quantised against a CLUT that has not arrived?
 * The art's unused/defer slots are the magenta sentinel (255,0,255); on a
 * hardware-palette backend qd_palette_blackout hides them, but that is a
 * NO-OP on a quantiser, so on ECS they can be quantised into the bands. */
static short ecs_sentinel_clut(void)
{
	short i, n = 0;

	for (i = 0; i < 256; i++)
		if (s_clut[i * 3 + 0] > 200 && s_clut[i * 3 + 1] < 60
		    && s_clut[i * 3 + 2] > 200)
			n++;
	return n;
}

static long ecs_sentinel_pixels(void)
{
	long n = 0, k;

	for (k = 0; k < (long)ECS_W * ECS_H; k++) {
		unsigned char c = s_chunky[k];

		if (s_clut[c * 3 + 0] > 200 && s_clut[c * 3 + 1] < 60
		    && s_clut[c * 3 + 2] > 200)
			n++;
	}
	return n;
}
#endif

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
#ifdef FRUA_TITLESEQ
	dbg_log_num("ecsp: set_palette first*1000+count = ",
	            (long)first * 1000 + count);
	dbg_log_num("ecsp:   incomplete-bracket open? = ",
	            (long)(dsp_palette_incomplete() ? 1 : 0));
#endif
#ifdef FRUA_ECSTITLE
	dbg_log_num("ttl: set_palette first*1000+count = ",
	            (long)first * 1000 + count);
	dbg_log_num("ttl:   sentinel CLUT entries now  = ", (long)ecs_sentinel_clut());
#endif
	/* Only a SUBSTANTIAL load (a scene change) marks the bands dirty; small
	 * writes are palette cycling, whose re-band + full re-render churn is
	 * what froze the ST live test (same policy there). Deferred to the next
	 * full present, when the surface is completely drawn.
	 *
	 * ★ ...EXCEPT WHILE A SCREEN IS COMPOSING, where SIZE IS THE WRONG
	 * TEST. The count>=32 heuristic asks "is this big enough to be a scene
	 * change?", and a title whose art declares FEWER THAN 32 colours
	 * answers no — so its palette was filed as cycling, the bands were
	 * never re-cut, and the screen was remapped through the PREVIOUS
	 * screen's palette. At boot that previous palette is the black startup
	 * one, so the screen renders BLACK and the title simply never appears.
	 *
	 * That is the A500 report "the SSI / Micromagic logo screen no longer
	 * even displays" (2026-09-02) — measured here as: at its show present
	 * the SSI title had dirty=0/force=0 while the next title had dirty=1,
	 * and only the second one rendered. It is also why the screen is
	 * MISSING rather than mis-coloured, which no palette-quality theory
	 * explained.
	 *
	 * l19d4 brackets each composition (qd_palette_blackout), so during one
	 * we KNOW a screen is being built and any palette write belongs to it,
	 * whatever its size. Outside a bracket the size heuristic is untouched,
	 * so the walk's colour cycling stays as cheap as it was. */
	if (count >= 32 || !s_have_pal || dsp_palette_incomplete())
		s_dirty = 1;

	/* The Atari twin's eager re-quant on a WHOLESALE replacement (see
	 * st_set_palette): the draw-time planar writers stamp the visible
	 * planes with the current remap as the engine blits, so a title/BigPic
	 * committing its own palette must re-cut NOW or everything drawn until
	 * the next present wears the previous scene's colours (the A500
	 * "titles in multiple passes / wrong colours" report). Same total
	 * cost — the present's CLUT-unchanged guard then skips — and the disk
	 * palette cache still serves repeat scenes. */
	if (ecs_majq && ecs_eagerq
	    && s_dirty && s_have_pal && e_quant_valid
	    && ecs_clut_major_change()) {
		dbg_log("ecs: EAGER reband (clut replaced)");
		ecs_reband(0);            /* no flip pending: install now */
		s_dirty = 0;
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
	2,                      /* page-flipped (ecs_present flips s_front) — see
	                         * the AGA note; both this and AGA left it 0 and
	                         * were driven single-buffered. */
	0,                      /* hw_palette: NO — 5 planes hold a quantised slot,
	                         * not the index, so a CLUT move can invalidate
	                         * converted pixels. (Explicit, to reach the field
	                         * below; the value is the historical default.) */
#ifdef FRUA_ECS_PALBLANKET
	0,                      /* A/B arm: the historical blanket row mark. */
#else
	1,                      /* #63 palette_self_invalidates: ecs_set_palette's
	                         * s_dirty -> ecs_reband/ecs_render is what re-renders
	                         * after a CLUT move, and that branch bypasses the row
	                         * scan entirely — so the shim's blanket row mark only
	                         * bought a 200-row rescan of an UNCHANGED chunky
	                         * surface (measured: 636 marks / 644 presents,
	                         * 128,800 compares for 782 conversions). */
#endif
};

const dsp_backend_t *dsp_backend_ecs(void)
{
	return &ecs_backend;
}

#endif /* FRUA_AMIGA */
