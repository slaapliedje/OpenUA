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

/* ST colour hardware: 16 word registers at 0xFF8240. */
#define ST_COLORREGS ((volatile short *)0xFFFF8240UL)

static unsigned char *s_screen_raw;
static unsigned char *s_screen;
static unsigned char *s_chunky;
static unsigned char *s_shadow;         /* chunky as of the last convert   */
static short          s_force_full;     /* LUTs changed: diffing is void   */
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
short  st_band_stpal[ST_NBANDS + 1][ST_NCOL];   /* ST-format, +sentinel  */
short *st_band_ptr;                             /* next band for Timer B */
static short                   s_dirty;
static short                   s_have_pal;
static short                   s_vbl_slot = -1;

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

	for (x = 0; x + 32 <= w; x += 32)
		c2p4st_32(src + x,  lut,
		          (unsigned short *)(dst + (long)(x / 16) * 8));
	if (x < w) {                            /* 16-pixel tail */
		unsigned char pad[32];
		unsigned short d[8];
		unsigned short *out = (unsigned short *)(dst + (long)(x / 16) * 8);
		short p;

		memcpy(pad, src + x, 16);
		memset(pad + 16, 0, 16);        /* pads land only in d[4..7] */
		c2p4st_32(pad, lut, d);
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

	if (s_force_full) {
		st_blit_rows(0, ST_W, 0, ST_H);
		s_force_full = 0;
		return;
	}
	for (y = 0; y < ST_H; y++) {
		if (memcmp(s_chunky + (long)y * ST_W,
		           s_shadow + (long)y * ST_W, ST_W) != 0)
			st_blit_rows(0, ST_W, y, 1);
	}
}

/* Re-band: histogram + per-band reduce, then build the per-band ST-format
 * palettes (STE gun encoding: nibble = (v0 << 3) | (v >> 1)). The sentinel
 * row (see st_band_stpal) is a copy of the last band. */
static void st_reband(void)
{
	short b, i;

	/* ADR-0016 B1 (fixed per-scene palette): ONE global reduce over the whole
	 * frame (nbands=1 histograms all rows), replicated to every band. A flat
	 * colour that spans bands is now the SAME slot+RGB everywhere, so the per-band
	 * palette SEAMS — the visible "banding" (#40), a uniform panel rendered as
	 * brown/green/olive stripes — vanish. This is also approach B's target
	 * palette model: the per-band scheme existed to stop the granite chrome
	 * starving the viewport, but post-B2.1 the viewport is composited as its own
	 * planar region, so the shared surface holds only the (flat, seam-prone) HUD,
	 * for which one 16-colour palette is ample. The raster-split machinery stays
	 * (identical per-band loads) so per-band anchoring can return later if an
	 * art-heavy screen ever needs the extra colours. */
	quant_banded(s_chunky, ST_W, ST_H, s_clut,
	             1, ST_NCOL, ST_BITS, s_band_pal, s_band_remap);
	for (b = 1; b < ST_NBANDS; b++) {
		memcpy(s_band_pal + (long)b * ST_NCOL * 3, s_band_pal,
		       (size_t)(ST_NCOL * 3));
		memcpy(s_band_remap + (long)b * 256, s_band_remap, 256);
	}
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
	s_force_full = 1;               /* every LUT moved: row diffing is void */
}

/* --- backend entry points ------------------------------------------------ */

static int st_init(short want_w, short want_h)
{
	(void)want_w; (void)want_h;

	s_screen_raw = (unsigned char *)Mxalloc(SCREEN_BYTES + 256, 0);   /* ST-RAM */
	s_chunky     = (unsigned char *)Mxalloc((long)ST_W * ST_H, 0);
	s_shadow     = (unsigned char *)Mxalloc((long)ST_W * ST_H, 0);
	if (s_screen_raw == NULL || s_chunky == NULL || s_shadow == NULL) {
		dbg_log("ste: Mxalloc FAILED");
		if (s_screen_raw) { Mfree(s_screen_raw); s_screen_raw = NULL; }
		if (s_chunky)     { Mfree(s_chunky); s_chunky = NULL; }
		if (s_shadow)     { Mfree(s_shadow); s_shadow = NULL; }
		return 1;
	}
	s_screen = (unsigned char *)
	    (((uintptr_t)s_screen_raw + 255) & ~(uintptr_t)255);
	memset(s_screen, 0, SCREEN_BYTES);
	memset(s_chunky, 0, (size_t)ST_W * ST_H);
	memset(s_shadow, 0, (size_t)ST_W * ST_H);
	s_force_full = 1;                        /* first present converts all */

	s_save_rez  = Getrez();
	s_save_phys = Physbase();
	s_save_log  = Logbase();
	Setscreen(s_save_log, s_screen, 0);      /* ST Low; console keeps old log */

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

	dbg_log("ste: ST-low 320x200x4 16-colour, per-band Timer-B palette up");
	return 0;
}

static void st_shutdown(void)
{
	if (s_st_active) {
		planar_viewport_register((unsigned char *(*)(short *))0,
		                         (void (*)(short, short, short, short))0);
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
	if (s_shadow)     { Mfree(s_shadow); s_shadow = NULL; }
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

	planar_blit_stlow(pp, VP_PLANE_STRIDE, s_vp_w, s_vp_h, ST_DEPTH,
	                  s_screen, LINE_BYTES, ST_W, ST_H, s_vp_x, s_vp_y);
}

#ifdef FRUA_STPROF
/* Coarse present-path profile: every 128 full presents, log wall ticks vs
 * ticks spent inside present and the rows actually converted. TickCount is
 * the compat layer's 60Hz tick — a layering reach-down, debug-only. */
extern long TickCount(void);
static long sp_n, sp_rows, sp_in, sp_wall0 = -1, sp_reband, sp_reband_skip;
long g_stprof_rows;                     /* incremented in st_blit_rows */
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
			st_reband();
		}
	}
	st_blit_full();
	st_vp_composite();                       /* overlay the planar viewport */
#ifdef FRUA_STPROF
	sp_in += TickCount() - t0;
	sp_rows = g_stprof_rows;
	if ((++sp_n & 127) == 0) {
		dbg_log_num("stprof: presents = ", sp_n);
		dbg_log_num("stprof: wall ticks = ", TickCount() - sp_wall0);
		dbg_log_num("stprof: in-present ticks = ", sp_in);
		dbg_log_num("stprof: rows converted = ", sp_rows);
		dbg_log_num("stprof: rebands = ", sp_reband);
		dbg_log_num("stprof: reband skips = ", sp_reband_skip);
	}
#endif
}

static void st_present_rect(short x, short y, short w, short h)
{
	short x1;

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

	x1 = (short)((x + w + 15) & ~15);        /* 16-pixel plane groups */
	x  = (short)(x & ~15);
	st_blit_rows(x, (short)(x1 - x), y, h);
	st_vp_composite();                       /* overlay the planar viewport */
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
	1,                      /* single-buffered: present writes the live screen */
};

const dsp_backend_t *dsp_backend_ste(void)
{
	return &ste_backend;
}
