/*
 * Atari TT030 display backend — the TT shifter's TT-low mode.
 *
 * TT low is 320x480 in 8 word-interleaved bitplanes with a 256-entry
 * palette (4 bits per gun) — the same colour depth the engine's chunky
 * buffer carries, at double the line count. The engine's 320x200 frame is
 * therefore LINE-DOUBLED into a 320x400 image centred in the 480 lines,
 * with 40-line black borders top and bottom (the letterbox): every present
 * converts a chunky row once (the shared c2p32 transpose) and writes the
 * planar line twice.
 *
 * Unlike the Falcon backend (VIDEL, 16bpp + LUT blit) this is a paletted
 * target, so palette animation (the fireplace) needs no re-present — the
 * same free ride the Amiga's copper palette gives, via EsetPalette.
 *
 * Mode/screen handling stays on the XBIOS the TT TOS (and EmuTOS) provide:
 * EgetShift/EsetShift for the shifter mode, Physbase/Setscreen for the
 * base, EsetPalette for the colour RAM. Single displayed buffer (rect
 * updates land directly; a small unsynchronised write risks one frame of
 * shear inside the cell, the same policy as the other backends' bring-up).
 *
 * plat_cursor_* stays with the VIDEL backend's definitions (one binary
 * serves both machines); without the VIDEL VBL flip installed those report
 * inactive, so the Toolbox shim composites the pointer in software and
 * pushes it through present_rect — correct, just not sprite-assisted.
 */

#include <mint/osbind.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "display.h"
#include "dbglog.h"
#include "c2p32.h"
#include "planar.h"
#include "plat_sys.h"          /* plat_stram_alloc: Mxalloc is TOS 2.01+ */

#define TT_W        320
#define TT_H        480
#define ENGINE_H    200
#define TOP_BORDER  ((TT_H - ENGINE_H * 2) / 2)     /* 40 letterbox lines */
#define LINE_BYTES  (TT_W)                          /* 8bpl: 1 byte/px    */
#define SCREEN_BYTES ((long)LINE_BYTES * TT_H)

#define TT_SHIFT_TTLOW  0x0700      /* 0xFF8262 mode field: TT low */

static unsigned char *g_screen_raw;         /* Mxalloc'd block          */
static unsigned char *g_screen;             /* 256-aligned screen base  */
static unsigned char  g_chunky[TT_W * ENGINE_H];
static dsp_surface_t  g_surface;
static short          g_save_shift = -1;
static void          *g_save_phys, *g_save_log;
static unsigned char  s_seeded;             /* #99: see tt_present below */

#ifdef FRUA_PLANAR
/* ★ ADR-0016 B4 on the TT — the draw-time WRITER half.
 *
 * The TT is the SIMPLE case, exactly like AGA: 8 planes = 256 colours and the
 * palette is hardware (dsp_backend_t.hw_palette, #99), so the writers' remap is
 * the IDENTITY and none of the ST's quantiser machinery (bands, re-band, epoch
 * reset, new-ink trigger) exists here. A stamp can never be invalidated, so
 * there is no epoch to reset.
 *
 * Layout is ST-Low's WORD-INTERLEAVE with nplanes = 8, not the Amiga's separate
 * planes — `planar_put_stlow` and friends are already generic in nplanes and a
 * `slot` byte covers 8 planes exactly, so the whole compat/quickdraw.c writer
 * layer works here UNCHANGED. That is why this half of the ADR is a display-file
 * change plus a build flag, and nothing else.
 *
 * The one thing the TT does that no other planar backend does is LINE-DOUBLE:
 * engine row y lands on screen rows TOP_BORDER + 2y and +1. So the stamp buffer
 * is the ENGINE frame (320x200 interleaved) and the doubling stays where it
 * always was, at present time — the writers never see it.
 *
 * Allocated at init rather than declared static: this file is compiled into the
 * ONE Atari 020 binary shared with the Falcon, which never registers a draw
 * target, and ~190 KB of BSS it can never use is not worth carrying. If the
 * allocation fails we simply do not register, dsp_planar_draw_target() returns
 * 0, and every writer keeps its chunky store — the pre-B4 path, intact. */
#define TT_DEPTH    8
#define DT_BYTES    ((long)LINE_BYTES * ENGINE_H)   /* interleaved, 8 planes */
#define COV_BYTES   ((long)TT_W * ENGINE_H)

static unsigned char *t_dt;          /* draw-time plane accumulation buffer  */
static unsigned char *t_dt_cov;      /* w*h: 1 where a writer stamped        */
static unsigned char *t_dt_idx;      /* w*h: the chunky index it stamped     */
static short         *t_dt_rowcov;   /* ENGINE_H: covered pixels in the row  */
static unsigned char  t_dt_ident[256];

static int tt_dt_target(struct dsp_planar_dt *dt)
{
	if (t_dt == NULL)
		return 0;
	dt->planes       = t_dt;
	dt->remap        = t_dt_ident;
	dt->cov          = t_dt_cov;
	dt->idx          = t_dt_idx;
	dt->rowcov       = t_dt_rowcov;
	dt->chunky       = g_chunky;
	dt->chunky_pitch = TT_W;
	dt->line_bytes   = LINE_BYTES;   /* interleaved: a whole engine row */
	dt->plane_bytes  = 0;            /* interleaved layout: unused      */
	dt->w            = TT_W;
	dt->h            = ENGINE_H;
	dt->nplanes      = TT_DEPTH;
	dt->nbands       = 1;            /* identity: one band is enough    */
	return 1;
}
#endif /* FRUA_PLANAR */

/* Convert one 16-pixel-aligned span of a chunky row into TT interleaved
 * planes at `dst` (8 words per 16-pixel group: plane 0..7), then the caller
 * duplicates the line. `w` and the source offset are multiples of 32 except
 * a possible 16-pixel tail. */
static void tt_c2p_span(const unsigned char *src, unsigned char *dst, short w)
{
	short x;

	for (x = 0; x + 32 <= w; x += 32) {
		c2p_u32 c[8], o[8];
		unsigned short *d = (unsigned short *)(dst + (long)(x / 16) * 16);
		short p;

		c2p_load32(src + x, c);
		c2p_transpose32(c, o);
		for (p = 0; p < 8; p++) {
			d[p]     = (unsigned short)(o[p] >> 16);   /* pixels 0-15  */
			d[p + 8] = (unsigned short)(o[p]);         /* pixels 16-31 */
		}
	}
	if (x < w) {                            /* 16-pixel tail */
		c2p_u32 c[8], o[8];
		unsigned char pad[32];
		unsigned short *d = (unsigned short *)(dst + (long)(x / 16) * 16);
		short p;

		memcpy(pad, src + x, 16);
		memset(pad + 16, 0, 16);
		c2p_load32(pad, c);
		c2p_transpose32(c, o);
		for (p = 0; p < 8; p++)
			d[p] = (unsigned short)(o[p] >> 16);
	}
}

static int tt_init(short want_w, short want_h)
{
	(void)want_w; (void)want_h;             /* fixed 320x200 engine frame */

	g_screen_raw = (unsigned char *)plat_stram_alloc(SCREEN_BYTES + 256); /* ST-RAM */
	if (g_screen_raw == NULL) {
		dbg_log("tt: Mxalloc screen FAILED");
		return 1;
	}
	g_screen = (unsigned char *)
	    (((uintptr_t)g_screen_raw + 255) & ~(uintptr_t)255);
	memset(g_screen, 0, SCREEN_BYTES);      /* black letterbox borders */
	memset(g_chunky, 0, sizeof g_chunky);
	s_seeded = 0;                           /* #99: first present converts all */

	g_save_shift = EgetShift();
	g_save_phys  = Physbase();
	g_save_log   = Logbase();

	dbg_log_num("tt: old shift mode = ", (long)g_save_shift);
	/* Physical base = our screen; LOGICAL base stays on the old TOS
	 * screen so the VT52 console (Cconws boot/debug prints) keeps
	 * rendering into the hidden buffer instead of scribbling over the
	 * letterbox. */
	Setscreen(g_save_log, g_screen, -1);
	dbg_log_screen_owned();   /* see the videl backend: keep Cconws off the picture */
	EsetShift(TT_SHIFT_TTLOW);
	dbg_log("tt: TT-low takeover done");

	g_surface.width  = TT_W;
	g_surface.height = ENGINE_H;
	g_surface.pitch  = TT_W;
	g_surface.pixels = g_chunky;

#ifdef FRUA_PLANAR
	/* ADR-0016 B4: bring up the draw-time plane target. Any allocation
	 * failure leaves t_dt NULL and simply does not register — writers keep
	 * their chunky store and the present converts, exactly as before. */
	t_dt        = (unsigned char *)plat_stram_alloc(DT_BYTES);
	t_dt_cov    = (unsigned char *)plat_stram_alloc(COV_BYTES);
	t_dt_idx    = (unsigned char *)plat_stram_alloc(COV_BYTES);
	t_dt_rowcov = (short *)plat_stram_alloc((long)ENGINE_H * sizeof(short));
	if ((long)t_dt <= 0 || (long)t_dt_cov <= 0
	 || (long)t_dt_idx <= 0 || (long)t_dt_rowcov <= 0) {
		dbg_log("tt: draw-time plane alloc FAILED - chunky path");
		t_dt = NULL;
	} else {
		short i;
		for (i = 0; i < 256; i++)
			t_dt_ident[i] = (unsigned char)i;
		memset(t_dt, 0, (size_t)DT_BYTES);
		memset(t_dt_cov, 0, (size_t)COV_BYTES);
		memset(t_dt_idx, 0, (size_t)COV_BYTES);
		memset(t_dt_rowcov, 0, (size_t)ENGINE_H * sizeof(short));
		planar_draw_target_register(tt_dt_target);
		dbg_log("tt: draw-time plane path up (identity remap, 8 planes)");
	}
#endif
	return 0;
}

static void tt_shutdown(void)
{
#ifdef FRUA_PLANAR
	planar_draw_target_register((int (*)(struct dsp_planar_dt *))0);
	t_dt = NULL;                  /* writers fall back to chunky at once */
#endif
	if (g_save_shift >= 0) {
		EsetShift((short)g_save_shift);
		Setscreen(g_save_log, g_save_phys, -1);
		g_save_shift = -1;
	}
	if (g_screen_raw != NULL) {
		Mfree(g_screen_raw);
		g_screen_raw = NULL;
		g_screen     = NULL;
	}
}

static dsp_surface_t *tt_surface(void)
{
	return &g_surface;
}

#ifdef FRUA_PLANAR
#ifdef FRUA_TTPROF
static long s_ttp_skipped;              /* rows the writers had already stamped */
#endif

/* Make row y of t_dt authoritative, and say whether we had to convert.
 *
 * Straight from aga_dt_ready_row (#41), including the SELF-HEALING OWNERSHIP:
 * a row we convert here IS remap[chunky] — trivially so, the remap being the
 * identity — which is exactly the ownership invariant the skip tests. So claim
 * it. A coverage hole (a region drawn by one of the engine-direct blitters that
 * never stamps) converts ONCE and is skipped thereafter; an overwrite breaks
 * idx == chunky and re-converts. Without this the same rows re-convert every
 * present and the skip never fires in play.
 *
 * Returns 1 if a conversion ran, 0 if the writers had already stamped the row. */
static int tt_dt_ready_row(short y)
{
	const unsigned char *crow = g_chunky + (long)y * TT_W;

	if (t_dt_rowcov[y] == TT_W
	    && memcmp(t_dt_idx + (long)y * TT_W, crow, TT_W) == 0)
		return 0;                       /* writer-stamped: t_dt is correct */

	tt_c2p_span(crow, t_dt + (long)y * LINE_BYTES, TT_W);
	memset(t_dt_cov + (long)y * TT_W, 1, TT_W);
	memcpy(t_dt_idx + (long)y * TT_W, crow, TT_W);
	t_dt_rowcov[y] = TT_W;
	return 1;
}
#endif /* FRUA_PLANAR */

/* Convert + line-double the given chunky rows into the letterboxed screen.
 *
 * With the draw-time writers active (ADR-0016 B4) the conversion is skipped for
 * any row the writers already stamped: t_dt holds the planes and the present is
 * two row copies. The LINE DOUBLING stays here either way — it is a property of
 * the TT screen, not of the frame, so the writers never see it. */
static void tt_blit_rows(short x0, short w, short y0, short h)
{
	short y;

	for (y = 0; y < h; y++) {
		const unsigned char *src =
		    g_chunky + (long)(y0 + y) * TT_W + x0;
		unsigned char *dst = g_screen
		    + (long)(TOP_BORDER + (y0 + y) * 2) * LINE_BYTES
		    + (long)(x0 / 16) * 16;

#ifdef FRUA_PLANAR
		/* Whole-width rows only: t_dt is a full-row accumulation and the
		 * coverage test is per row, so a partial-width call (tt_present_rect)
		 * cannot use it. Those are rare and already correct via the c2p. */
		if (t_dt != NULL && x0 == 0 && w == TT_W) {
			short yy = (short)(y0 + y);
#ifdef FRUA_TTPROF
			if (!tt_dt_ready_row(yy)) s_ttp_skipped++;
#else
			(void)tt_dt_ready_row(yy);
#endif
			memcpy(dst, t_dt + (long)yy * LINE_BYTES,
			       (size_t)LINE_BYTES);
			memcpy(dst + LINE_BYTES, dst, (size_t)LINE_BYTES);
			continue;
		}
#endif
		tt_c2p_span(src, dst, w);
		memcpy(dst + LINE_BYTES, dst, (size_t)((w / 16) * 16));
	}
}

/* --- #99: the dirty-row present ------------------------------------------
 *
 * The TT is a bitplane machine, so ADR-0016 applies to it, but it had none of
 * the machinery: this backend converted ALL 200 rows on EVERY present.
 *
 * The cheapest correct win needs no draw-time writers at all. The shim already
 * maintains a per-row dirty set (`qd_touch_rows` / `qd_touch_all` in
 * compat/quickdraw.c, storage in platform/planar.c) and it is already sized for
 * this backend — planar.h's PLANAR_DIRTY_MAX is 512 precisely because "TT-low is
 * 320x480". Nothing consumed it here. So: convert only the rows that changed.
 *
 * WHY THAT IS SAFE HERE, AND CHEAPER THAN ON THE ST. This backend is
 * SINGLE-BUFFERED (the backend struct's last field is 1) — there is exactly one
 * screen and it persists between presents. A row nobody wrote therefore still
 * holds correct planes from the previous present, so a clean row can be skipped
 * ENTIRELY: no transpose and no store. The ST cannot do that, which is why it
 * carries a per-PAGE pending set (s_pend[NPAGES]) — with two pages a row must be
 * rebuilt once per page before it may be considered clean.
 *
 * Two invariants this leans on, both pre-existing:
 *   - `s_seeded` forces the first present to convert everything (nothing is on
 *     screen yet, and the dirty set says nothing about that).
 *   - The shim's default is CONSERVATIVE: anything that cannot name its rows —
 *     above all qd_screen_pixels, which hands out a raw pointer — calls
 *     qd_touch_all(), and planar_dirty_rows() then reports "scan everything".
 *     A writer that narrows without announcing would leave a row stale; that is
 *     the same contract the ST and Amiga backends already run under, and
 *     FRUA_DIRTYCHECK is the instrument that polices it.
 *
 * The dirty set is reset by qd_present() AFTER the backend runs, so this only
 * reads it. qd_present_rect deliberately does NOT reset, and tt_present_rect
 * below stays dirty-agnostic: it writes the screen directly, so its rows are
 * already correct, and if they are also flagged this present re-converts them —
 * wasteful, never wrong. */
/* (s_seeded is declared up with the other statics so tt_init can clear it.) */

#ifdef FRUA_TTPROF
static long s_ttp_presents, s_ttp_rows, s_ttp_full;
#endif

static void tt_present(void)
{
	const unsigned char *drows;
	short y, y0;
#ifdef FRUA_TTPROF
	long conv = 0;
#endif

	if (!s_seeded || planar_dirty_rows(&drows)) {
		tt_blit_rows(0, TT_W, 0, ENGINE_H);
		s_seeded = 1;
#ifdef FRUA_TTPROF
		conv = ENGINE_H;
		s_ttp_full++;
#endif
	} else {
		/* Coalesce dirty runs: one call per contiguous band, which is the
		 * shape the shim actually reports (a text line, a panel, the
		 * viewport) rather than scattered singles. */
		for (y = 0; y < ENGINE_H; ) {
			if (!drows[y]) { y++; continue; }
			y0 = y;
			while (y < ENGINE_H && drows[y])
				y++;
			tt_blit_rows(0, TT_W, y0, (short)(y - y0));
#ifdef FRUA_TTPROF
			conv += (y - y0);
#endif
		}
	}
#ifdef FRUA_TTPROF
	s_ttp_rows += conv;
	if (++s_ttp_presents % 16 == 0) {
		dbg_file_num("ttprof: presents ", s_ttp_presents);
		dbg_file_num("ttprof:   full    ", s_ttp_full);
		dbg_file_num("ttprof:   rows    ", s_ttp_rows);
		dbg_file_num("ttprof:   rows/pr ", s_ttp_rows / s_ttp_presents);
#ifdef FRUA_PLANAR
		/* B4: rows the writers had already stamped, so the present did two
		 * copies instead of a conversion. This is the number the draw-time
		 * writer half exists to move. */
		dbg_file_num("ttprof:   SKIPPED ", s_ttp_skipped);
#endif
		/* WHO forced the full presents. Same six counters the ST backend
		 * dumps (#63) — needs FRUA_STPROF for the QDT() macro to compile to
		 * anything, so build with BOTH to get attribution. */
		{
			extern long g_qdt_hits[8];
			dbg_file_num("ttqdt:  0 grab   ", g_qdt_hits[0]);
			dbg_file_num("ttqdt:  1 fill   ", g_qdt_hits[1]);
			dbg_file_num("ttqdt:  2 blit   ", g_qdt_hits[2]);
			dbg_file_num("ttqdt:  3 palette", g_qdt_hits[3]);
			dbg_file_num("ttqdt:  4 cursor ", g_qdt_hits[4]);
			dbg_file_num("ttqdt:  5 glyph  ", g_qdt_hits[5]);
			dbg_file_num("ttqdt:  6 SKIPPED clean present ", g_qdt_hits[6]);
		}
	}
#endif
}

static void tt_present_rect(short x, short y, short w, short h)
{
	short x1;

	if (x < 0) { w = (short)(w + x); x = 0; }
	if (y < 0) { h = (short)(h + y); y = 0; }
	if (x + w > TT_W)     w = (short)(TT_W - x);
	if (y + h > ENGINE_H) h = (short)(ENGINE_H - y);
	if (w <= 0 || h <= 0)
		return;

	x1 = (short)((x + w + 15) & ~15);       /* 16-pixel plane groups */
	x  = (short)(x & ~15);
	tt_blit_rows(x, (short)(x1 - x), y, h);
}

static void tt_set_palette(const dsp_color_t *colors, short first, short count)
{
	static unsigned short pal[256];
	short i;

	if (first < 0 || count <= 0 || first >= 256)
		return;
	if (first + count > 256)
		count = (short)(256 - first);
	for (i = 0; i < count; i++) {
		/* TT colour word: %0000 RRRR GGGG BBBB, 4 bits per gun. */
		pal[first + i] = (unsigned short)
		    (((colors[i].r & 0xF0) << 4)
		     | (colors[i].g & 0xF0)
		     | ((colors[i].b & 0xF0) >> 4));
	}
	EsetPalette(first, count, (short *)&pal[first]);
}

static const dsp_backend_t tt_backend = {
	"TT shifter (TT low, line-doubled)",
	tt_init,
	tt_shutdown,
	tt_surface,
	tt_present,
	tt_present_rect,
	tt_set_palette,
	1,                      /* single-buffered: present writes the live screen */
	1,                      /* #99 hw_palette: planes hold the index, EsetPalette
	                         * does the rest — a palette change cannot invalidate
	                         * a converted row. See display.h for the proof and
	                         * the measurement. */
};

const dsp_backend_t *dsp_backend_tt(void)
{
	return &tt_backend;
}
