/*
 * Mac QuickDraw shim — geometry, the current port, PixMaps, regions, the
 * screen port, and the first drawing primitives (ADR-0003).
 *
 * The Point and rectangle calculations (to the Inside Macintosh semantics),
 * GetPort / SetPort, NewPixMap, the rectangular-region facility, the screen
 * GrafPort that owns the display back buffer, the rect primitives (EraseRect,
 * PaintRect, FrameRect), the line family (MoveTo / LineTo / GetPen / PenSize
 * / PenMode), the ovals (PaintOval, FrameOval), CopyBits (same-size,
 * srcCopy), and ClipRect over 8-bit paletted pixels — every primitive
 * funnels through qd_effective_clip (portRect ∩ visRgn ∩ clipRgn), so
 * BeginUpdate's narrowed visRgn and ClipRect's application clip both restrict
 * drawing as on the Mac. Pen-using primitives honour pnSize, the pen's
 * pat-mode (patCopy / patOr / patXor / patBic) on the destination pixel,
 * and the pen's 8x8 one-bit pattern (pnPat) — set bits lay down the op,
 * clear bits paint bkColor for patCopy and leave the pixel alone for the
 * bitwise modes. RGBForeColor / RGBBackColor resolve against a cached
 * palette by sum-of-squared-differences and update fgColor / bkColor.
 */

#include <stddef.h>             /* offsetof, NULL */

#include "quickdraw.h"
#include "printing.h"           /* pr_port_capture — printing-port text route */
#include "macmemory.h"          /* NewHandleClear, DisposeHandle */
#include "display.h"            /* dsp_color_t, dsp_detect — palette forward */
#include "planar.h"             /* planar_put_stlow — B4 draw-time plane store */
#include "font_8x8.h"           /* qd_font_8x8 — fallback bitmap font */
#include "mac_font.h"           /* g_mac_font — preferred when loaded */
#include "input.h"              /* plat_mouse_pos — software cursor   */

void SetPt(Point *pt, short h, short v)
{
	pt->h = h;
	pt->v = v;
}

void SetRect(Rect *r, short left, short top, short right, short bottom)
{
	r->left   = left;
	r->top    = top;
	r->right  = right;
	r->bottom = bottom;
}

void OffsetRect(Rect *r, short dh, short dv)
{
	r->left   += dh;
	r->right  += dh;
	r->top    += dv;
	r->bottom += dv;
}

void InsetRect(Rect *r, short dh, short dv)
{
	r->left   += dh;
	r->right  -= dh;
	r->top    += dv;
	r->bottom -= dv;
}

Boolean EmptyRect(const Rect *r)
{
	return (Boolean)(r->top >= r->bottom || r->left >= r->right);
}

/* Intersection. Returns false (and zeroes dst) when the rects miss. */
Boolean SectRect(const Rect *src1, const Rect *src2, Rect *dst)
{
	dst->top    = src1->top    > src2->top    ? src1->top    : src2->top;
	dst->left   = src1->left   > src2->left   ? src1->left   : src2->left;
	dst->bottom = src1->bottom < src2->bottom ? src1->bottom : src2->bottom;
	dst->right  = src1->right  < src2->right  ? src1->right  : src2->right;

	if (EmptyRect(dst)) {
		SetRect(dst, 0, 0, 0, 0);
		return 0;
	}
	return 1;
}

/* The smallest rectangle enclosing both sources. */
void UnionRect(const Rect *src1, const Rect *src2, Rect *dst)
{
	dst->top    = src1->top    < src2->top    ? src1->top    : src2->top;
	dst->left   = src1->left   < src2->left   ? src1->left   : src2->left;
	dst->bottom = src1->bottom > src2->bottom ? src1->bottom : src2->bottom;
	dst->right  = src1->right  > src2->right  ? src1->right  : src2->right;
}

Boolean EqualRect(const Rect *r1, const Rect *r2)
{
	return (Boolean)(r1->top == r2->top && r1->left == r2->left
	                 && r1->bottom == r2->bottom && r1->right == r2->right);
}

/* A point is inside if left <= h < right and top <= v < bottom. */
Boolean PtInRect(Point pt, const Rect *r)
{
	return (Boolean)(pt.h >= r->left && pt.h < r->right
	                 && pt.v >= r->top && pt.v < r->bottom);
}

/* The smallest rectangle spanned by two points. */
void Pt2Rect(Point pt1, Point pt2, Rect *dst)
{
	dst->top    = pt1.v < pt2.v ? pt1.v : pt2.v;
	dst->left   = pt1.h < pt2.h ? pt1.h : pt2.h;
	dst->bottom = pt1.v > pt2.v ? pt1.v : pt2.v;
	dst->right  = pt1.h > pt2.h ? pt1.h : pt2.h;
}

/* --- the current port --- */

static GrafPtr g_thePort;       /* QuickDraw's current drawing port */

void GetPort(GrafPtr *port)
{
	*port = g_thePort;
}

void SetPort(GrafPtr port)
{
	g_thePort = port;
}

/* --- Color QuickDraw: PixMap allocation --- */

/*
 * NewPixMap — allocate and initialise a PixMap.
 *
 * The PixMap and its colour table are relocatable blocks, so each is a real
 * Handle. On the Mac NewPixMap copies the current GDevice's pixel map; with
 * no GDevice yet, the depth defaults to the 8-bit indexed configuration the
 * display HAL targets. The geometry — bounds, rowBytes, baseAddr — is the
 * caller's to fill.
 */
PixMapHandle NewPixMap(void)
{
	PixMapHandle pmh;
	CTabHandle   ct;

	pmh = (PixMapHandle)NewHandleClear((Size)sizeof(PixMap));
	if (pmh == NULL)
		return NULL;

	/* A 256-entry colour table for the 8-bit depth — zeroed for now; the
	 * palette is loaded once the display HAL is up. */
	ct = (CTabHandle)NewHandleClear((Size)(sizeof(ColorTable)
	                                       + 255 * sizeof(ColorSpec)));
	if (ct == NULL) {
		DisposeHandle((Handle)pmh);
		return NULL;
	}
	(*ct)->ctSize = 255;

	(*pmh)->hRes      = 0x00480000L;        /* 72.0 dpi, 16.16 fixed-point */
	(*pmh)->vRes      = 0x00480000L;
	(*pmh)->pixelType = 0;                  /* indexed (chunky)            */
	(*pmh)->pixelSize = 8;
	(*pmh)->cmpCount  = 1;
	(*pmh)->cmpSize   = 8;
	(*pmh)->pmTable   = ct;
	return pmh;
}

/* DisposePixMap — free a NewPixMap PixMap and its colour table. */
void DisposePixMap(PixMapHandle pm)
{
	if (pm == NULL)
		return;
	if (*pm != NULL)
		DisposeHandle((Handle)(*pm)->pmTable);
	DisposeHandle((Handle)pm);
}

/* --- regions (rectangular) ---
 *
 * The shim's regions are rectangular — a region is its bounding box. NewRgn
 * allocates one as a relocatable block; the box lives in rgnBBox.
 */

RgnHandle NewRgn(void)
{
	RgnHandle rgn = (RgnHandle)NewHandle((Size)sizeof(Region));

	if (rgn != NULL) {
		(*rgn)->rgnSize = (short)sizeof(Region);
		SetRect(&(*rgn)->rgnBBox, 0, 0, 0, 0);
	}
	return rgn;
}

void DisposeRgn(RgnHandle rgn)
{
	DisposeHandle((Handle)rgn);
}

void SetEmptyRgn(RgnHandle rgn)
{
	if (rgn != NULL)
		SetRect(&(*rgn)->rgnBBox, 0, 0, 0, 0);
}

void RectRgn(RgnHandle rgn, const Rect *r)
{
	if (rgn != NULL)
		(*rgn)->rgnBBox = *r;
}

Boolean EmptyRgn(RgnHandle rgn)
{
	return (Boolean)(rgn == NULL || EmptyRect(&(*rgn)->rgnBBox));
}

/* --- the screen port and drawing primitives --- */

/*
 * The screen port — a CGrafPort that owns the display back buffer. Static
 * storage: there is one screen, and the shim never disposes it (the regions
 * it owns are released by process exit). Set up by qd_attach_screen and made
 * the current port; the engine SetPorts to its windows when drawing into
 * them, and back to this for desktop drawing.
 */
static CGrafPort g_screen_port;

/*
 * Initialise the per-port drawing defaults that the Mac sets in OpenPort
 * (the shim has no OpenPort; qd_attach_screen and win_new both call this).
 * The pen starts 1x1 in fgColor with patCopy semantics; colours default to
 * index 0 / 255 — refined by the palette manager later. The pen pattern
 * is solid (0xFF) — also storage-only until PenPat is honoured.
 */
void qd_init_port_defaults(GrafPtr port)
{
	int i;

	SetPt(&port->pnSize, 1, 1);
	port->pnMode = patCopy;
	port->pnVis  = 0;                        /* visible (>=0)       */
	for (i = 0; i < 8; i++)
		port->pnPat.pat[i] = 0xFF;       /* solid               */
	port->fgColor = 255;
	port->bkColor = 0;

	/* Mac OpenPort text defaults — system font, 12 pt, plain, srcOr. The
	 * shim's font path ignores font / face / size for now (only the 8x8
	 * fallback exists), but the fields are stored so engine queries see
	 * the expected values. */
	port->txFont = 0;                        /* systemFont          */
	port->txFace = 0;                        /* plain               */
	port->txMode = srcOr;
	port->txSize = 12;
}

/*
 * Has qd_attach_screen been called yet? Until it has, g_screen_port is
 * zeroed and not usable as a port; the Window Manager checks before
 * targeting it for frame drawing.
 */
static Boolean g_screen_attached;

GrafPtr qd_screen_port(void)
{
	return g_screen_attached ? (GrafPtr)&g_screen_port : NULL;
}

/* Present hook — see quickdraw.h. */
static qd_present_fn g_present_hook;

void qd_set_present(qd_present_fn fn)
{
	g_present_hook = fn;
}

/* Software cursor compositor (defined below, after qd_nearest_color):
 * draw the cursor onto the back buffer just before the HAL flushes it,
 * then restore the pixels underneath so the engine's buffer is untouched. */
static void cursor_composite(void);
static void cursor_restore(void);
static void qd_cursor_tick(void);            /* lazily push the VBL cursor sprite */
static void qd_rebake_color_pointer(void);   /* re-resolve cursor to live CLUT */

/* #144 off-screen compose: coalesce the intermediate presents a screen
 * emits while it builds a frame in visible steps. The engine's faithful
 * frame bracket drives this — JT[108]/L38d0 begin the off-page compose
 * (qd_present_suppress(1)), l3994 commits it (qd_present_suppress(0)) —
 * held in lockstep with the g_a5_-18395 compose flag, whose set/clear
 * lifecycle the engine already keeps balanced. While suppressed a
 * qd_present() just records that the frame wants presenting; the commit
 * flushes it ONCE, so the whole frame appears in a single flip instead
 * of flashing through its partial states. Screens outside the bracket
 * never suppress and present eagerly exactly as before. */
static int g_present_suppress;
static int g_present_pending;
static int g_present_hold;              /* #147 atomic-recompose hold (nests) */

/* #152: has anything drawn to the surface since the last full present?
 * Set by every write path — the fill/blit/glyph primitives, direct-writer
 * pointer grabs (qd_screen_pixels), and palette installs (which can change
 * how existing pixels RENDER, arming a backend re-pack). Cleared when a
 * full present actually runs. qd_present skips the backend entirely when
 * the surface is clean AND the backend is single-buffered — measured on
 * the 8 MHz ST mono build, a clean present still cost ~310 ms in the
 * backend's full-screen diff scan, and the engine's idle loops emit them
 * ~1.6/s: HALF the machine burned scanning an unchanged screen. Page-
 * flipped backends (videl) never skip: their consecutive presents seed
 * different pages, so a "clean" second present still does real work. */
static int   g_qd_touched = 1;

/* Present page count — see quickdraw.h (#151). Default 2 = the old
 * unconditional double present, so an unwired build behaves as before.
 * (Declared here, above qd_present, which reads it for the #152 skip;
 * the setter/getter live further down with the other present plumbing.) */
static short g_present_pages = 2;

void qd_present_suppress(int on)
{
	if (on) {
		g_present_suppress = 1;
	} else {
		g_present_suppress = 0;
		if (g_present_pending) {
			g_present_pending = 0;
			qd_present();            /* the single frame commit */
		}
	}
}

/* #147 atomic recompose: a hold that DOMINATES the g_present_suppress
 * boolean and NESTS (ref-counted), so it survives the inner JT[108]/l3994
 * bracket a text paint runs mid-rebuild (l3994 -> jt1128 is a direct
 * qd_present). While any hold is active a qd_present() is DISCARDED (recorded
 * as pending, then dropped on release) — the frame it would have shown is
 * incomplete. The caller flushes the finished frame with its OWN explicit
 * present(s) AFTER releasing the hold.
 *
 * Wrap a multi-step play-frame rebuild (port_draw_play_frame + 3D view +
 * roster/clock/bar), releasing just before the explicit present, so the
 * SINGLE-BUFFERED ST-High mono backend never puts a half-composed frame on
 * screen. port_draw_play_frame fills the whole play area with the grey stone
 * index (clut 21); in mono that luminance is bright = WHITE paper, so an
 * intermediate present flushed after the fill but before the HUD repaint
 * showed a stark WHITE flash of the roster/clock on every re-render (the
 * "walk HUD flicker"). The videl path double-buffers and never showed it.
 *
 * Release DISCARDS rather than flushes so the caller's trailing double
 * present (jt312's two qd_present() calls — the videl two-page #103 guard)
 * still executes IN FULL: collapsing it to one would leave a videl page
 * stale (a black frame around the view on the first movement). Balanced:
 * every hold(1) has a hold(0), released before the explicit present. */
void qd_present_hold(int on)
{
	if (on) {
		g_present_hold++;
	} else if (g_present_hold > 0) {
		if (--g_present_hold == 0)
			g_present_pending = 0;   /* discard the held intermediate */
	}
}

/* DEBUG click marker: qd_dbg_mark stashes a screen point; qd_present overlays a
 * crosshair there so you can see exactly where a click's hit-test landed (vs the
 * cursor sprite, whose hotspot is offset from the hit point). A bring-up aid for
 * the dungeon mouse work.
 *
 * ★ OFF BY DEFAULT (build with -DFRUA_CLICKMARK to get it back). It used to be
 * unconditional, so the shipping binary drew a red crosshair over the play screen
 * at every click — visible in every screenshot taken of this port for months. */
#ifdef FRUA_CLICKMARK
static short g_dbg_mark_x = -1, g_dbg_mark_y = -1;
static void  qd_dbg_draw_mark(void);
#endif

#ifdef FRUA_KBTRACE
long g_kbt_l2d3e, g_kbt_1134, g_kbt_qdpresent, g_kbt_qdsuppressed;
long g_kbt_1067rot, g_kbt_setpal;
#endif

#ifdef FRUA_MONOPROF
/* Present-caller attribution (#152 profiling): call sites stamp g_qdp_src
 * right before qd_present; the non-suppressed path bins the count and
 * resets the tag. Dumped by the sthigh backend's mono_prof_tick window.
 * Tags: 0 other/untagged, 1 jt1128 (l3994 text-commit), 2 jt1146 (jt108
 * flip), 3 port_cycle_present, 4 cursor, 5 l2d3e inline, 6 port_present_full. */
short g_qdp_src;
long  g_qdp_counts[8];
#endif

void qd_present(void)
{
	if (g_present_suppress || g_present_hold) {
#ifdef FRUA_KBTRACE
		g_kbt_qdsuppressed++;
#endif
		g_present_pending = 1;   /* defer to the commit */
		return;
	}
#ifdef FRUA_KBTRACE
	g_kbt_qdpresent++;
#endif
	/* #152: nothing drawn since the last full present -> the frame on
	 * screen is already current; skip the backend's (expensive) no-op
	 * scan. Only on single-buffered backends — see g_qd_touched. */
	if (!g_qd_touched && g_present_pages == 1) {
#ifdef FRUA_MONOPROF
		g_qdp_counts[7]++;               /* clean presents skipped */
#endif
		return;
	}
#ifdef FRUA_MONOPROF
	g_qdp_counts[g_qdp_src & 7]++;
	g_qdp_src = 0;
#endif
	qd_cursor_tick();                /* (re)push the VBL cursor sprite if dirty */
	cursor_composite();              /* no-op when the VBL cursor is active     */
#ifdef FRUA_CLICKMARK
	qd_dbg_draw_mark();              /* overlay the debug click crosshair        */
#endif
	if (g_present_hook != NULL)
		g_present_hook();
	cursor_restore();
	/* #152: frame on screen is current. Cleared LAST — cursor_composite/
	 * cursor_restore above grab the pixel pointer (marking touched) but
	 * are net-neutral writes already reflected in the pack. */
	g_qd_touched = 0;
}

void qd_set_present_pages(short n)
{
	g_present_pages = (n >= 1) ? n : 1;
}

short qd_present_pages(void)
{
	return g_present_pages;
}

/* Dirty-rect present hook — see quickdraw.h. */
static qd_present_rect_fn g_present_rect_hook;

void qd_set_present_rect(qd_present_rect_fn fn)
{
	g_present_rect_hook = fn;
}

void qd_present_rect(short x, short y, short w, short h)
{
	/* #157: rect presents used to BYPASS the #147 atomic-compose hold, so
	 * a multi-second full recompose (the l63c0 command-cycle rebuild at
	 * 8 MHz) flashed its mid-states — the black interior fill and the
	 * jt221 chrome-prelude plates — onto the single-buffered mono screen
	 * for seconds at a time (the "AREA toggle-back black screen"). While
	 * held, drop the rect: the compose's ending full present covers
	 * everything. Step renders and menu paths never run under a hold. */
	if (g_present_hold > 0)
		return;
	if (g_present_rect_hook != NULL)
		g_present_rect_hook(x, y, w, h);
	else if (g_present_hook != NULL)
		g_present_hook();            /* fall back to a full present */
}

void qd_attach_screen(void *pixels, short rowBytes, short width, short height)
{
	CGrafPtr     cp = &g_screen_port;
	PixMapHandle pm;
	Rect         bounds;

	SetRect(&bounds, 0, 0, width, height);

	cp->portVersion = (short)CGRAFPORT_FLAG;
	cp->portRect    = bounds;

	pm = NewPixMap();
	if (pm != NULL) {
		(*pm)->baseAddr = (Ptr)pixels;
		(*pm)->rowBytes = rowBytes;
		(*pm)->bounds   = bounds;
	}
	cp->portPixMap = pm;

	cp->visRgn  = NewRgn();
	cp->clipRgn = NewRgn();
	RectRgn(cp->visRgn,  &bounds);
	RectRgn(cp->clipRgn, &bounds);

	qd_init_port_defaults((GrafPtr)cp);
	g_screen_attached = 1;

	SetPort((GrafPtr)cp);
}

/*
 * Direct access to the attached screen back buffer, for engine code that
 * paints raw 8-bit cells (e.g. the GEO map visualizer) without going
 * through the GrafPort primitives. Returns 0 if no screen is attached.
 */
int qd_screen_pixels(unsigned char **pixels, short *rowBytes,
                     short *width, short *height)
{
	CGrafPtr     cp = &g_screen_port;
	PixMapHandle pm;

	if (!g_screen_attached || cp->portPixMap == NULL)
		return 0;
	pm = cp->portPixMap;
	if (*pm == NULL)
		return 0;
	if (pixels) {
		*pixels = (unsigned char *)(*pm)->baseAddr;
		g_qd_touched = 1;        /* #152: pointer grab = presumed writer */
	}
	if (rowBytes) *rowBytes = (*pm)->rowBytes;
	if (width)    *width    = (short)(cp->portRect.right - cp->portRect.left);
	if (height)   *height   = (short)(cp->portRect.bottom - cp->portRect.top);
	return 1;
}

/*
 * The port's effective clip — portRect intersected with the visRgn and
 * clipRgn bounding boxes (both rectangular in the shim). All drawing goes
 * through this, so ClipRect (the application clip) and BeginUpdate's
 * narrowed visRgn (the window-update clip) restrict drawing together.
 * Returns false (with *out left empty) when the intersection is empty.
 */
static Boolean qd_effective_clip(GrafPtr port, Rect *out)
{
	Rect tmp;

	*out = port->portRect;
	if (port->visRgn != NULL && *port->visRgn != NULL) {
		tmp = *out;
		if (!SectRect(&tmp, &(*port->visRgn)->rgnBBox, out))
			return 0;
	}
	if (port->clipRgn != NULL && *port->clipRgn != NULL) {
		tmp = *out;
		if (!SectRect(&tmp, &(*port->clipRgn)->rgnBBox, out))
			return 0;
	}
	return 1;
}

/*
 * Fill a rectangle of the current port's pixmap, applying `mode` between
 * `fg` and the destination pixel, optionally gated by a one-bit pattern.
 *
 * Used by the fill primitives (PaintRect / EraseRect through qd_fill_rect,
 * which pass pat=NULL — a fill isn't pen-mediated and writes uniformly)
 * and by the pen primitives (qd_pen_plot / qd_pen_hline / qd_pen_vline,
 * which pass &port->pnPat) — sweeping a 1xN or Nx1 pen along a line is a
 * single rect band.
 *
 * pat == NULL: solid path — one mode-switch outside, tight inner loops.
 * pat != NULL: each pixel reads bit (x & 7, y & 7) from pat. Set bits lay
 *              down the pen-mode op; clear bits paint `bk` for patCopy and
 *              leave the pixel untouched for the bitwise modes (so or/xor/
 *              bic strokes act as stencils through the pattern).
 *
 * At 8 bpp the bitwise pat-modes act on the full pixel byte: patOr ORs
 * `fg` in, patXor XORs it (so writing fg twice restores), patBic clears
 * the bits of `fg` from the pixel.
 */
static void qd_pixmap_fill(GrafPtr port, const Rect *clip,
                           const Rect *r, unsigned char fg, unsigned char bk,
                           short mode, const Pattern *pat)
{
	CGrafPtr cp = (CGrafPtr)port;
	PixMap  *pm;
	Rect     clipped;
	short    row, w;

	if (((unsigned short)cp->portVersion & CGRAFPORT_FLAG) != CGRAFPORT_FLAG)
		return;
	if (cp->portPixMap == NULL || *cp->portPixMap == NULL)
		return;
	pm = *cp->portPixMap;
	if (pm->baseAddr == NULL)
		return;
	if (!SectRect(r, clip, &clipped))
		return;

	g_qd_touched = 1;                        /* #152: about to write pixels */
	w = (short)(clipped.right - clipped.left);

	if (pat == NULL) {
		for (row = clipped.top; row < clipped.bottom; row++) {
			unsigned char *p = (unsigned char *)pm->baseAddr
			                 + (row - pm->bounds.top) * pm->rowBytes
			                 + (clipped.left - pm->bounds.left);
			short col;

			switch (mode) {
			case patOr:
				for (col = 0; col < w; col++)
					p[col] = (unsigned char)(p[col] | fg);
				break;
			case patXor:
				for (col = 0; col < w; col++)
					p[col] = (unsigned char)(p[col] ^ fg);
				break;
			case patBic:
				for (col = 0; col < w; col++)
					p[col] = (unsigned char)(p[col] & ~fg);
				break;
			case patCopy:
			default:
				for (col = 0; col < w; col++)
					p[col] = fg;
				break;
			}
		}
		return;
	}

	for (row = clipped.top; row < clipped.bottom; row++) {
		unsigned char *p = (unsigned char *)pm->baseAddr
		                 + (row - pm->bounds.top) * pm->rowBytes
		                 + (clipped.left - pm->bounds.left);
		unsigned char  patrow = pat->pat[row & 7];
		short col;

		for (col = 0; col < w; col++) {
			short x   = (short)(clipped.left + col);
			int   bit = (patrow >> (7 - (x & 7))) & 1;

			if (bit) {
				switch (mode) {
				case patOr:  p[col] = (unsigned char)(p[col] |  fg); break;
				case patXor: p[col] = (unsigned char)(p[col] ^  fg); break;
				case patBic: p[col] = (unsigned char)(p[col] & ~fg); break;
				case patCopy:
				default:     p[col] = fg; break;
				}
			} else if (mode == patCopy) {
				p[col] = bk;
			}
		}
	}
}

/*
 * Shared body of EraseRect / PaintRect — fill `r` (in the current port's
 * local coordinates) with `color`, clipped to the port's effective clip.
 * The fills always patCopy without a pattern: pnPat / pnMode control the
 * pen, not the bucket. (The Mac's fillPat / bkPat aren't honoured yet —
 * FRUA does solid fills here.)
 */
static void qd_fill_rect(const Rect *r, unsigned char color)
{
	GrafPtr port;
	Rect    clip;

	GetPort(&port);
	if (port == NULL || r == NULL)
		return;
	if (!qd_effective_clip(port, &clip))
		return;
	qd_pixmap_fill(port, &clip, r, color, 0, patCopy, NULL);
}

void EraseRect(const Rect *r)
{
	GrafPtr port;

	GetPort(&port);
	if (port != NULL)
		qd_fill_rect(r, (unsigned char)((CGrafPtr)port)->bkColor);
}

void PaintRect(const Rect *r)
{
	GrafPtr port;

	GetPort(&port);
	if (port != NULL)
		qd_fill_rect(r, (unsigned char)((CGrafPtr)port)->fgColor);
}

/*
 * XOR-fill `r` with `value` — the engine's jt1161 bit-8 fill mode (the
 * Mac CODE 4 L19be/L0888 pixel run: pixel = (pixel & mask) ^ value with
 * mask 0xFFFF). The menu-highlight invert paints through this: XORing
 * the same rect twice restores the original pixels, so the pulldown's
 * selection bar moves without ever redrawing the labels.
 */
void qd_xor_rect(const Rect *r, unsigned char value)
{
	GrafPtr port;
	Rect    clip;

	GetPort(&port);
	if (port == NULL || r == NULL)
		return;
	if (!qd_effective_clip(port, &clip))
		return;
	qd_pixmap_fill(port, &clip, r, value, 0, patXor, NULL);
}

/*
 * Plot one pixel at (x, y) into the current port's pixmap, silently
 * discarding writes that fall outside the precomputed clip rect. Callers
 * compute the clip once at entry via qd_effective_clip and pass it in, so
 * the Bresenham / oval inner loops don't recompute it per pixel.
 */
static void qd_plot(GrafPtr port, const Rect *clip,
                    short x, short y, unsigned char color)
{
	CGrafPtr cp = (CGrafPtr)port;
	PixMap  *pm;

	if (x < clip->left || x >= clip->right
	 || y < clip->top  || y >= clip->bottom)
		return;
	if (((unsigned short)cp->portVersion & CGRAFPORT_FLAG) != CGRAFPORT_FLAG)
		return;
	if (cp->portPixMap == NULL || *cp->portPixMap == NULL)
		return;
	pm = *cp->portPixMap;
	if (pm->baseAddr == NULL)
		return;
	((unsigned char *)pm->baseAddr)
		[(y - pm->bounds.top) * pm->rowBytes + (x - pm->bounds.left)] = color;
}

/* Horizontal line helper — used by PaintOval's per-row fill. */
static void qd_hline(GrafPtr port, const Rect *clip,
                     short x1, short x2, short y, unsigned char color)
{
	if (x1 > x2) { short t = x1; x1 = x2; x2 = t; }
	for (; x1 <= x2; x1++)
		qd_plot(port, clip, x1, y, color);
}

/*
 * Pen-aware primitives — sweep the pnSize.h x pnSize.v pen along the
 * shape's path, so a 3x3 pen draws a 3-pixel-thick line / outline. Each
 * pen step plots a (sw, sh) rect with top-left at the pen position. The
 * union of those rects along a horizontal or vertical sweep is itself
 * one rectangle — qd_pen_hline / qd_pen_vline fall through to a single
 * qd_pixmap_fill for that band.
 */

static void qd_pen_plot(GrafPtr port, const Rect *clip, short x, short y)
{
	Rect pen;
	CGrafPtr cp = (CGrafPtr)port;
	short sw = port->pnSize.h > 0 ? port->pnSize.h : 1;
	short sh = port->pnSize.v > 0 ? port->pnSize.v : 1;

	SetRect(&pen, x, y, (short)(x + sw), (short)(y + sh));
	qd_pixmap_fill(port, clip, &pen,
	               (unsigned char)cp->fgColor, (unsigned char)cp->bkColor,
	               port->pnMode, &port->pnPat);
}

static void qd_pen_hline(GrafPtr port, const Rect *clip,
                         short x1, short x2, short y)
{
	Rect band;
	CGrafPtr cp = (CGrafPtr)port;
	short sw = port->pnSize.h > 0 ? port->pnSize.h : 1;
	short sh = port->pnSize.v > 0 ? port->pnSize.v : 1;

	if (x1 > x2) { short t = x1; x1 = x2; x2 = t; }
	SetRect(&band, x1, y, (short)(x2 + sw), (short)(y + sh));
	qd_pixmap_fill(port, clip, &band,
	               (unsigned char)cp->fgColor, (unsigned char)cp->bkColor,
	               port->pnMode, &port->pnPat);
}

static void qd_pen_vline(GrafPtr port, const Rect *clip,
                         short x, short y1, short y2)
{
	Rect band;
	CGrafPtr cp = (CGrafPtr)port;
	short sw = port->pnSize.h > 0 ? port->pnSize.h : 1;
	short sh = port->pnSize.v > 0 ? port->pnSize.v : 1;

	if (y1 > y2) { short t = y1; y1 = y2; y2 = t; }
	SetRect(&band, x, y1, (short)(x + sw), (short)(y2 + sh));
	qd_pixmap_fill(port, clip, &band,
	               (unsigned char)cp->fgColor, (unsigned char)cp->bkColor,
	               port->pnMode, &port->pnPat);
}

/*
 * FrameRect — outline `r` in the port's foreground colour. The Mac excludes
 * the right and bottom edges from filled rects but includes them in the
 * outline (so a 10x10 rect frames at columns left..right-1 and rows
 * top..bottom-1). One-pixel pen for now — PenSize / PenPat / PenMode follow.
 */
void FrameRect(const Rect *r)
{
	GrafPtr port;
	Rect    clip;
	short l, t, rr, bb;

	GetPort(&port);
	if (port == NULL || r == NULL || EmptyRect(r))
		return;
	if (!qd_effective_clip(port, &clip))
		return;
	l  = r->left;
	t  = r->top;
	rr = (short)(r->right  - 1);
	bb = (short)(r->bottom - 1);

	qd_pen_hline(port, &clip, l, rr, t);  /* top    */
	qd_pen_hline(port, &clip, l, rr, bb); /* bottom */
	qd_pen_vline(port, &clip, l,  t,  bb);/* left   */
	qd_pen_vline(port, &clip, rr, t,  bb);/* right  */
}

/* MoveTo — set the pen location in the current port. */
void MoveTo(short h, short v)
{
	GrafPtr port;

	GetPort(&port);
	if (port != NULL)
		SetPt(&port->pnLoc, h, v);
}

/* GetPen — read the pen location of the current port. */
void GetPen(Point *pt)
{
	GrafPtr port;

	GetPort(&port);
	if (port != NULL && pt != NULL)
		*pt = port->pnLoc;
}

/*
 * LineTo — draw a one-pixel line from the current pen to (h, v) in the
 * port's foreground colour, then move the pen to (h, v). Bresenham over
 * 8-bit pixels, per-pixel clipped to the port's effective clip. PenSize
 * / PenPat / PenMode are not honoured yet — see the header.
 *
 * pnLoc is updated to (h, v) even when the line is entirely clipped out:
 * the pen moves whether or not the strokes are visible (the Mac semantics).
 */
void LineTo(short h, short v)
{
	GrafPtr port;
	Rect    clip;
	Boolean draw;
	short   x0, y0, x1, y1;
	short   dx, dy, sx, sy, err, e2;

	GetPort(&port);
	if (port == NULL)
		return;
	draw = qd_effective_clip(port, &clip);

	x0 = port->pnLoc.h;
	y0 = port->pnLoc.v;
	x1 = h;
	y1 = v;

	dx = (short)(x1 - x0); if (dx < 0) dx = (short)-dx;
	dy = (short)(y1 - y0); if (dy < 0) dy = (short)-dy;
	sx = (short)(x0 < x1 ?  1 : -1);
	sy = (short)(y0 < y1 ?  1 : -1);
	err = (short)(dx - dy);

	for (;;) {
		if (draw)
			qd_pen_plot(port, &clip, x0, y0);
		if (x0 == x1 && y0 == y1)
			break;
		e2 = (short)(err * 2);
		if (e2 > -dy) { err = (short)(err - dy); x0 = (short)(x0 + sx); }
		if (e2 <  dx) { err = (short)(err + dx); y0 = (short)(y0 + sy); }
	}

	SetPt(&port->pnLoc, h, v);
}

/*
 * Oval primitives — the oval is inscribed in `r`, touching the four edges.
 *
 * Brute-force scanline: for each row, scan to find the leftmost and
 * rightmost columns inside the implicit ellipse (x-cx)^2 / rx^2 +
 * (y-cy)^2 / ry^2 <= 1, expressed entirely in integers as
 * dx^2 * ry^2 + dy^2 * rx^2 <= rx^2 * ry^2. PaintOval hlines between the
 * extents; FrameOval plots them, and does a second column-scan pass so the
 * curve stays closed where it runs near-horizontal (one pixel per row on
 * each side is sparse where the slope is shallow).
 *
 * The midpoint ellipse algorithm is faster, but O(rx*ry) per oval is fine
 * for the modest UI ovals FRUA draws. Long arithmetic is large enough for
 * semi-axes up to a few hundred pixels — rx^2 * ry^2 fits in a 32-bit long
 * up to about rx=ry=215.
 */

static void qd_oval_axes(const Rect *r, short *cx, short *cy,
                         short *rx, short *ry)
{
	*cx = (short)((r->left + r->right)  / 2);
	*cy = (short)((r->top  + r->bottom) / 2);
	*rx = (short)((r->right  - r->left) / 2);
	*ry = (short)((r->bottom - r->top)  / 2);
}

void PaintOval(const Rect *r)
{
	GrafPtr       port;
	Rect          clip;
	unsigned char color;
	short         cx, cy, rx, ry, x, y;
	long          rx2, ry2, denom;

	GetPort(&port);
	if (port == NULL || r == NULL || EmptyRect(r))
		return;
	if (!qd_effective_clip(port, &clip))
		return;
	qd_oval_axes(r, &cx, &cy, &rx, &ry);
	if (rx == 0 || ry == 0)
		return;
	color = (unsigned char)((CGrafPtr)port)->fgColor;
	rx2   = (long)rx * rx;
	ry2   = (long)ry * ry;
	denom = rx2 * ry2;

	for (y = r->top; y < r->bottom; y++) {
		long  dy  = y - cy;
		long  dyy = dy * dy * rx2;
		short xleft = -1, xright = -1;

		if (dyy > denom)
			continue;
		for (x = r->left; x < r->right; x++) {
			long dx = x - cx;
			if (dx * dx * ry2 + dyy <= denom) {
				if (xleft < 0)
					xleft = x;
				xright = x;
			}
		}
		if (xleft >= 0)
			qd_hline(port, &clip, xleft, xright, y, color);
	}
}

void FrameOval(const Rect *r)
{
	GrafPtr port;
	Rect    clip;
	short   cx, cy, rx, ry, x, y;
	long    rx2, ry2, denom;

	GetPort(&port);
	if (port == NULL || r == NULL || EmptyRect(r))
		return;
	if (!qd_effective_clip(port, &clip))
		return;
	qd_oval_axes(r, &cx, &cy, &rx, &ry);
	if (rx == 0 || ry == 0)
		return;
	rx2   = (long)rx * rx;
	ry2   = (long)ry * ry;
	denom = rx2 * ry2;

	/* Row scan: plot leftmost and rightmost in-oval column at each y. */
	for (y = r->top; y < r->bottom; y++) {
		long  dy  = y - cy;
		long  dyy = dy * dy * rx2;
		short xleft = -1, xright = -1;

		if (dyy > denom)
			continue;
		for (x = r->left; x < r->right; x++) {
			long dx = x - cx;
			if (dx * dx * ry2 + dyy <= denom) {
				if (xleft < 0)
					xleft = x;
				xright = x;
			}
		}
		if (xleft >= 0) {
			qd_pen_plot(port, &clip, xleft, y);
			if (xright != xleft)
				qd_pen_plot(port, &clip, xright, y);
		}
	}

	/* Column scan: plot topmost and bottommost in-oval row at each x —
	 * closes the curve where it runs nearly horizontally. */
	for (x = r->left; x < r->right; x++) {
		long  dx  = x - cx;
		long  dxx = dx * dx * ry2;
		short ytop = -1, ybottom = -1;

		if (dxx > denom)
			continue;
		for (y = r->top; y < r->bottom; y++) {
			long dy = y - cy;
			if (dy * dy * rx2 + dxx <= denom) {
				if (ytop < 0)
					ytop = y;
				ybottom = y;
			}
		}
		if (ytop >= 0) {
			qd_pen_plot(port, &clip, x, ytop);
			if (ybottom != ytop)
				qd_pen_plot(port, &clip, x, ybottom);
		}
	}
}

/* Plot the outline of the oval `ov`, but only points inside the box `q` —
 * FrameRoundRect's quarter-corner worker (FrameOval's two-pass scan with a
 * quadrant clamp). */
static void qd_frame_oval_quadrant(GrafPtr port, const Rect *clip,
                                   const Rect *ov, const Rect *q)
{
	short cx, cy, rx, ry, x, y;
	long  rx2, ry2, denom;

	qd_oval_axes(ov, &cx, &cy, &rx, &ry);
	if (rx == 0 || ry == 0)
		return;
	rx2   = (long)rx * rx;
	ry2   = (long)ry * ry;
	denom = rx2 * ry2;

	for (y = ov->top; y < ov->bottom; y++) {
		long  dy  = y - cy;
		long  dyy = dy * dy * rx2;
		short xleft = -1, xright = -1;

		if (dyy > denom || y < q->top || y >= q->bottom)
			continue;
		for (x = ov->left; x < ov->right; x++) {
			long dx = x - cx;
			if (dx * dx * ry2 + dyy <= denom) {
				if (xleft < 0)
					xleft = x;
				xright = x;
			}
		}
		if (xleft >= q->left && xleft < q->right)
			qd_pen_plot(port, clip, xleft, y);
		if (xright != xleft && xright >= q->left && xright < q->right)
			qd_pen_plot(port, clip, xright, y);
	}
	for (x = ov->left; x < ov->right; x++) {
		long  dx  = x - cx;
		long  dxx = dx * dx * ry2;
		short ytop = -1, ybottom = -1;

		if (dxx > denom || x < q->left || x >= q->right)
			continue;
		for (y = ov->top; y < ov->bottom; y++) {
			long dy = y - cy;
			if (dy * dy * rx2 + dxx <= denom) {
				if (ytop < 0)
					ytop = y;
				ybottom = y;
			}
		}
		if (ytop >= q->top && ytop < q->bottom)
			qd_pen_plot(port, clip, x, ytop);
		if (ybottom != ytop && ybottom >= q->top && ybottom < q->bottom)
			qd_pen_plot(port, clip, x, ybottom);
	}
}

/* FrameRoundRect — a rect outline whose corners are quarter-ovals of size
 * (ovalWidth, ovalHeight). Straight edges run between the corner spans;
 * everything honours the pen size, matching FrameRect/FrameOval. The
 * engine's L6d40 draws the modal default-button ring with this (PenSize
 * 3,3, corners 18,18). Degenerate oval sizes collapse to FrameRect. */
void FrameRoundRect(const Rect *r, short ovalWidth, short ovalHeight)
{
	GrafPtr port;
	Rect    clip, ov, q;
	short   w, h, rx, ry;

	GetPort(&port);
	if (port == NULL || r == NULL || EmptyRect(r))
		return;
	if (!qd_effective_clip(port, &clip))
		return;

	w = (short)(r->right - r->left);
	h = (short)(r->bottom - r->top);
	if (ovalWidth  > w) ovalWidth  = w;
	if (ovalHeight > h) ovalHeight = h;
	if (ovalWidth <= 1 || ovalHeight <= 1) {
		FrameRect(r);
		return;
	}
	rx = (short)(ovalWidth / 2);
	ry = (short)(ovalHeight / 2);

	/* straight edges, shortened by the corner radii */
	qd_pen_hline(port, &clip, (short)(r->left + rx),
	             (short)(r->right - 1 - rx), r->top);
	qd_pen_hline(port, &clip, (short)(r->left + rx),
	             (short)(r->right - 1 - rx), (short)(r->bottom - 1));
	qd_pen_vline(port, &clip, r->left,
	             (short)(r->top + ry), (short)(r->bottom - 1 - ry));
	qd_pen_vline(port, &clip, (short)(r->right - 1),
	             (short)(r->top + ry), (short)(r->bottom - 1 - ry));

	/* four quarter-oval corners */
	ov.left = r->left; ov.top = r->top;
	ov.right = (short)(r->left + ovalWidth);
	ov.bottom = (short)(r->top + ovalHeight);
	q.left = r->left; q.top = r->top;
	q.right = (short)(r->left + rx); q.bottom = (short)(r->top + ry);
	qd_frame_oval_quadrant(port, &clip, &ov, &q);           /* TL */

	ov.left = (short)(r->right - ovalWidth); ov.right = r->right;
	q.left = (short)(r->right - rx); q.right = r->right;
	qd_frame_oval_quadrant(port, &clip, &ov, &q);           /* TR */

	ov.top = (short)(r->bottom - ovalHeight); ov.bottom = r->bottom;
	q.top = (short)(r->bottom - ry); q.bottom = r->bottom;
	qd_frame_oval_quadrant(port, &clip, &ov, &q);           /* BR */

	ov.left = r->left; ov.right = (short)(r->left + ovalWidth);
	q.left = r->left; q.right = (short)(r->left + rx);
	qd_frame_oval_quadrant(port, &clip, &ov, &q);           /* BL */
}

/*
 * CopyBits — blit a rectangular region of 8-bit pixels.
 *
 * First cut: PixMap → PixMap, same-size source and dest rects (no scaling),
 * srcCopy mode only, no mask region. The destination is clipped to
 * dstBits->bounds intersected with the current port's effective clip
 * (the Mac semantics — drawing always honours the current port's clip,
 * even when dstBits is some other pixmap); the source is then clipped to
 * srcBits->bounds, with each clip pass mirrored into the other rect so
 * the pixel-for-pixel mapping survives. Other modes (srcOr / srcXor /
 * srcBic), scaling, and maskRgn honour follow.
 *
 * BitMap and PixMap share the same first three fields (baseAddr, rowBytes,
 * bounds); the cast at the call site is the Mac trick that lets one entry
 * serve both depths. The shim's PixMaps don't set the high rowBytes bit
 * (the PixMap marker) yet — irrelevant while everything is 8 bpp.
 */
void CopyBits(const BitMap *srcBits, const BitMap *dstBits,
              const Rect *srcRect, const Rect *dstRect,
              short mode, RgnHandle maskRgn)
{
	GrafPtr port;
	Rect    src, dst, port_clip, dst_clip;
	short   w, h, y, shift;

	(void)mode;       /* srcCopy assumed */
	(void)maskRgn;    /* no mask honour yet */

	if (srcBits == NULL || dstBits == NULL
	 || srcRect == NULL || dstRect == NULL
	 || srcBits->baseAddr == NULL || dstBits->baseAddr == NULL)
		return;

	src = *srcRect;
	dst = *dstRect;
	if ((src.right - src.left) != (dst.right - dst.left)
	 || (src.bottom - src.top) != (dst.bottom - dst.top))
		return;       /* scaling not supported yet */

	/* The Mac clips drawing to the current port's effective clip — applies
	 * to CopyBits even when dstBits != current port's pixmap. */
	GetPort(&port);
	if (port == NULL || !qd_effective_clip(port, &port_clip))
		return;
	if (!SectRect(&dstBits->bounds, &port_clip, &dst_clip))
		return;

	g_qd_touched = 1;                /* #152: about to write pixels */

	/* Clip dst to dst_clip, mirroring trims into src. */
	if (dst.left < dst_clip.left) {
		shift = (short)(dst_clip.left - dst.left);
		dst.left = (short)(dst.left + shift);
		src.left = (short)(src.left + shift);
	}
	if (dst.top < dst_clip.top) {
		shift = (short)(dst_clip.top - dst.top);
		dst.top = (short)(dst.top + shift);
		src.top = (short)(src.top + shift);
	}
	if (dst.right > dst_clip.right) {
		shift = (short)(dst.right - dst_clip.right);
		dst.right = (short)(dst.right - shift);
		src.right = (short)(src.right - shift);
	}
	if (dst.bottom > dst_clip.bottom) {
		shift = (short)(dst.bottom - dst_clip.bottom);
		dst.bottom = (short)(dst.bottom - shift);
		src.bottom = (short)(src.bottom - shift);
	}

	/* Then clip src to srcBits->bounds, mirroring trims back into dst. */
	if (src.left < srcBits->bounds.left) {
		shift = (short)(srcBits->bounds.left - src.left);
		src.left = (short)(src.left + shift);
		dst.left = (short)(dst.left + shift);
	}
	if (src.top < srcBits->bounds.top) {
		shift = (short)(srcBits->bounds.top - src.top);
		src.top = (short)(src.top + shift);
		dst.top = (short)(dst.top + shift);
	}
	if (src.right > srcBits->bounds.right) {
		shift = (short)(src.right - srcBits->bounds.right);
		src.right = (short)(src.right - shift);
		dst.right = (short)(dst.right - shift);
	}
	if (src.bottom > srcBits->bounds.bottom) {
		shift = (short)(src.bottom - srcBits->bounds.bottom);
		src.bottom = (short)(src.bottom - shift);
		dst.bottom = (short)(dst.bottom - shift);
	}

	w = (short)(dst.right  - dst.left);
	h = (short)(dst.bottom - dst.top);
	if (w <= 0 || h <= 0)
		return;

	for (y = 0; y < h; y++) {
		const unsigned char *sp = (const unsigned char *)srcBits->baseAddr
		                        + ((src.top + y) - srcBits->bounds.top)
		                          * srcBits->rowBytes
		                        + (src.left - srcBits->bounds.left);
		unsigned char       *dp = (unsigned char *)dstBits->baseAddr
		                        + ((dst.top + y) - dstBits->bounds.top)
		                          * dstBits->rowBytes
		                        + (dst.left - dstBits->bounds.left);
		short x;

		for (x = 0; x < w; x++)
			dp[x] = sp[x];
	}
}

/*
 * ClipRect — set the current port's clipRgn to the rectangle `r`. Each
 * drawing primitive intersects with portRect ∩ visRgn ∩ clipRgn, so the
 * caller's `r` immediately restricts subsequent drawing.
 */
void ClipRect(const Rect *r)
{
	GrafPtr port;

	GetPort(&port);
	if (port == NULL || r == NULL || port->clipRgn == NULL)
		return;
	RectRgn(port->clipRgn, r);
}

/*
 * PenSize — set the current port's pen rectangle to (h, v). Pen-using
 * primitives (LineTo, FrameRect, FrameOval) sweep this rect along the
 * shape's path, so a 3x3 pen draws a 3-pixel-thick stroke.
 */
void PenSize(short h, short v)
{
	GrafPtr port;

	GetPort(&port);
	if (port == NULL)
		return;
	SetPt(&port->pnSize, h, v);
}

/*
 * PenMode — set the current port's pen transfer mode. Pen-using primitives
 * combine fgColor with the destination pixel according to the mode; see
 * the pat-mode comment in quickdraw.h. Modes outside the pat- family fall
 * through to patCopy in qd_pixmap_fill — the source modes (srcCopy etc)
 * apply to CopyBits, not the pen, so silently treating them as copy is
 * the right answer here.
 */
void PenMode(short mode)
{
	GrafPtr port;

	GetPort(&port);
	if (port == NULL)
		return;
	port->pnMode = mode;
}

/*
 * PenPat — set the current port's 8x8 one-bit pen pattern. The bytes are
 * copied (the caller's storage doesn't have to outlive the call). With a
 * solid pattern (all 0xFF — the OpenPort default) every pen pixel writes;
 * with a sparser pattern the pen acts as a stencil through pnPat — see
 * the pat dispatch in qd_pixmap_fill for the per-mode behaviour.
 */
void PenPat(const Pattern *pat)
{
	GrafPtr port;
	int     i;

	GetPort(&port);
	if (port == NULL || pat == NULL)
		return;
	for (i = 0; i < 8; i++)
		port->pnPat.pat[i] = pat->pat[i];
}

/* --- colour: the cached CLUT and the RGB entries --- */

/*
 * The shim's cached CLUT, in 8-bit-per-channel form (one slot for each of
 * the 256 indices). qd_set_palette installs entries here *and* forwards to
 * the display HAL; RGBForeColor / RGBBackColor walk this table to find the
 * nearest-distance index without going back to the hardware.
 *
 * The Mac would consult the current GDevice's inverse table for this; the
 * shim has no GDevice yet, so the cache is the source of truth.
 */
static dsp_color_t g_palette[256];

void qd_set_palette(const RGBColor *colors, short first, short count)
{
	const dsp_backend_t *dsp;
	dsp_color_t   tmp[256];
	short         i;

	if (colors == NULL || first < 0 || count <= 0)
		return;
	if (first >= 256 || count > 256 || first + count > 256)
		return;
	for (i = 0; i < count; i++) {
		tmp[i].r = (unsigned char)(colors[i].red   >> 8);
		tmp[i].g = (unsigned char)(colors[i].green >> 8);
		tmp[i].b = (unsigned char)(colors[i].blue  >> 8);
		g_palette[first + i] = tmp[i];
	}
	dsp = dsp_detect();
	if (dsp != NULL && dsp->set_palette != NULL)
		dsp->set_palette(tmp, first, count);
	g_qd_touched = 1;                /* #152: mapping may re-render pixels */
	qd_rebake_color_pointer();      /* keep the colour cursor true to the CLUT */
}

/* Debug accessor: copy the live shim CLUT out as 256 packed RGB triples.
 * Used by the engine's render-viewport dump (FRUA_VIEWPORT_DUMP). */
void qd_dump_palette(unsigned char *out768)
{
	int i;
	for (i = 0; i < 256; i++) {
		out768[i * 3 + 0] = g_palette[i].r;
		out768[i * 3 + 1] = g_palette[i].g;
		out768[i * 3 + 2] = g_palette[i].b;
	}
}

/*
 * Find the cached-CLUT index closest to `color` by sum-of-squared
 * differences in 8-bit-per-channel space. The Mac Color Manager's
 * Color2Index does the same with an inverse table; the shim has 256
 * entries and an O(n) scan is fine for ad-hoc colour resolution.
 */
static unsigned char qd_nearest_color(const RGBColor *color)
{
	unsigned char r = (unsigned char)(color->red   >> 8);
	unsigned char g = (unsigned char)(color->green >> 8);
	unsigned char b = (unsigned char)(color->blue  >> 8);
	unsigned char best = 0;
	long          best_d = 0x7FFFFFFFL;
	short         i;

	for (i = 0; i < 256; i++) {
		long dr = (long)r - g_palette[i].r;
		long dg = (long)g - g_palette[i].g;
		long db = (long)b - g_palette[i].b;
		long d  = dr * dr + dg * dg + db * db;

		if (d < best_d) {
			best_d = d;
			best   = (unsigned char)i;
		}
	}
	return best;
}

/* Draw the debug click crosshair into the 8-bit surface (before c2p): a
 * plus with a small box ("circle") centred on the stashed click point. */
#ifdef FRUA_CLICKMARK
static void qd_dbg_draw_mark(void)
{
	unsigned char *px;
	short          pitch, sw, sh, k, yy, xx;
	RGBColor       red = { 0xFFFF, 0, 0 };
	unsigned char  c;
	short          x = g_dbg_mark_x, y = g_dbg_mark_y;

	if (x < 0 || y < 0)
		return;
	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == NULL)
		return;
	c = qd_nearest_color(&red);                     /* bright red, contrasts */
	for (k = -7; k <= 7; k++) {                     /* the plus, 15 px */
		if (y >= 0 && y < sh && x + k >= 0 && x + k < sw)
			px[(long)y * pitch + (x + k)] = c;
		if (x >= 0 && x < sw && y + k >= 0 && y + k < sh)
			px[(long)(y + k) * pitch + x] = c;
	}
	for (k = -4; k <= 4; k++) {                     /* a 9x9 box outline */
		xx = x + k; yy = y + k;
		if (xx >= 0 && xx < sw) {
			if (y - 4 >= 0) px[(long)(y - 4) * pitch + xx] = c;
			if (y + 4 < sh) px[(long)(y + 4) * pitch + xx] = c;
		}
		if (yy >= 0 && yy < sh) {
			if (x - 4 >= 0) px[(long)yy * pitch + (x - 4)] = c;
			if (x + 4 < sw) px[(long)yy * pitch + (x + 4)] = c;
		}
	}
}

#endif /* FRUA_CLICKMARK */

/* Stash the last click point for the debug crosshair (screen coords: x = horiz,
 * y = vert). (-1, -1) clears it. A no-op unless -DFRUA_CLICKMARK. */
void qd_dbg_mark(short x, short y)
{
#ifdef FRUA_CLICKMARK
	g_dbg_mark_x = x;
	g_dbg_mark_y = y;
#else
	(void)x; (void)y;
#endif
}

/* ===================== Cursor Manager ===================== *
 *
 * One current cursor + a visibility level (Mac semantics: 0 = visible,
 * HideCursor decrements, ShowCursor increments up to 0). The compositor
 * (called from qd_present) draws the cursor at the live IKBD mouse
 * position onto the chunky back buffer, saving the pixels underneath so
 * cursor_restore() can put them back after the HAL flips — leaving the
 * engine's buffer untouched between frames (no trails). FRUA uses the
 * system arrow + watch; both are generic shapes, not FRUA art.
 */
/* Shared initializer: g_cursor below is a mutable copy and must be
 * statically initialized with a constant expression (initializing from the
 * const object is a GCC extension newer compilers fold but GCC 6 — the
 * Amiga toolchain — rejects). */
#define K_ARROW_CURSOR_INIT { \
	{ 0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00, \
	  0x7F80, 0x7FC0, 0x7C00, 0x4C00, 0x0600, 0x0600, 0x0300, 0x0000 }, \
	{ 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00, 0xFF80, \
	  0xFFC0, 0xFFE0, 0xFFE0, 0xFE00, 0xEF00, 0x0F00, 0x0780, 0x0380 }, \
	{ 0, 0 }                        /* hotspot at the tip (v,h) */ \
}
static const Cursor k_arrow_cursor = K_ARROW_CURSOR_INIT;
/* A small "watch" disc (GetCursor(4)); good enough as a busy marker. */
static const Cursor k_watch_cursor = {
	{ 0x0000, 0x0FE0, 0x1FF0, 0x3CF8, 0x3CF8, 0x39B8, 0x3FF8, 0x3E78,
	  0x3C78, 0x3FF8, 0x3CF8, 0x3CF8, 0x1FF0, 0x0FE0, 0x0000, 0x0000 },
	{ 0x0FE0, 0x1FF0, 0x3FF8, 0x7FFC, 0x7FFC, 0x7FFC, 0x7FFC, 0x7FFC,
	  0x7FFC, 0x7FFC, 0x7FFC, 0x7FFC, 0x3FF8, 0x1FF0, 0x0FE0, 0x0000 },
	{ 7, 7 }
};

/* The engine cursor: FRUA points a diagonal sword (tip top-left = hotspot)
 * once the UI comes up. The real game loads its sword into A5 -892 at
 * runtime — it isn't seeded in the DATA fork, so there's nothing to lift;
 * this is a generic placeholder shape (black blade + crossguard, white
 * outline), not FRUA art. SetCursor(qd_sword_cursor()) installs it. */
static const Cursor k_sword_cursor = {
	{ 0x8000, 0x4000, 0x6000, 0x3000, 0x1800, 0x0C00, 0x0600, 0x0300,
	  0x0180, 0x08D0, 0x04D0, 0x03E0, 0x0C30, 0x1810, 0x0000, 0x0000 },
	{ 0x8000, 0xC000, 0xE000, 0x7000, 0x3800, 0x1C00, 0x0E00, 0x0700,
	  0x1390, 0x19F0, 0x0FF0, 0x0FF0, 0x1F70, 0x3C38, 0x3818, 0x0000 },
	{ 0, 0 }                        /* hotspot at the blade tip (v,h) */
};

static Cursor        g_cursor = K_ARROW_CURSOR_INIT;
static short         g_cursor_level;            /* 0 visible, <0 hidden */
static int           g_cursor_obscured;         /* ObscureCursor: 1 = hidden until next mouse move */
static int           g_cursor_init;
static unsigned char g_cursor_save[16 * 16];    /* pixels under the cursor */
static short         g_cursor_save_x, g_cursor_save_y;
static int           g_cursor_saved;

/* The VBL cursor (platform service) needs the cursor shape baked to a 16x16
 * RGB565 sprite and re-pushed whenever it changes; g_cursor_dirty defers that
 * to the next qd_cursor_tick (so it survives any boot-order question — the VBL
 * may install after InitCursor). Visibility changes push immediately. */
static int  g_cursor_dirty = 1;
static void qd_cursor_sync(void);     /* defined below g_color_cursor */

void InitCursor(void)
{
	g_cursor       = k_arrow_cursor;
	g_cursor_level = 0;
	g_cursor_init  = 1;
	g_cursor_dirty = 1;
	if (plat_cursor_active())
		plat_cursor_show(1);
}

void SetCursor(const Cursor *c)
{
	if (c != NULL) {
		g_cursor      = *c;
		g_cursor_init = 1;
		g_cursor_dirty = 1;
	}
}

void HideCursor(void)
{
	g_cursor_level--;
	if (plat_cursor_active())
		plat_cursor_show(g_cursor_level >= 0);
}

void ShowCursor(void)
{
	if (g_cursor_level < 0)
		g_cursor_level++;
	if (plat_cursor_active())
		plat_cursor_show(g_cursor_level >= 0);
}

void ShieldCursor(const Rect *shieldRect, Point offsetPt)
{
	/* Mac: hide the cursor while it intersects shieldRect (offset to
	 * global coords by offsetPt); balanced by a following ShowCursor.
	 * The shim hides unconditionally — a safe superset that keeps the
	 * level pairing exact. FRUA's only callers (the L3e38 page-present
	 * band walk) bracket every band with ShieldCursor/ShowCursor. */
	(void)shieldRect;
	(void)offsetPt;
	HideCursor();
}

void ObscureCursor(void)
{
	/* Mac: the cursor goes invisible until the next mouse movement, then is
	 * auto-restored — it does NOT touch the HideCursor/ShowCursor nesting
	 * level. With the VBL cursor the platform owns the until-move state (so it
	 * restores on any screen); otherwise qd_cursor_refresh clears the flag on
	 * the first move. (The old shim decremented g_cursor_level, which never
	 * came back: the line editor jt1078 obscured the cursor for name entry and
	 * it stayed gone through the sprite screen and roster.) */
	if (plat_cursor_active())
		plat_cursor_obscure();
	else
		g_cursor_obscured = 1;
}

const Cursor *qd_sword_cursor(void)
{
	return &k_sword_cursor;
}

CursHandle GetCursor(short cursorID)
{
	static Cursor  s_cur;           /* GetCursor returns a handle           */
	static Cursor *s_ptr = &s_cur;
	s_cur = (cursorID == 4) ? k_watch_cursor : k_arrow_cursor;
	return &s_ptr;
}

/* Optional colour pointer (DOS FRUA cursor art, loaded at runtime). When
 * loaded, it overrides the mono cursor while visible. img holds CLUT indices
 * (already mapped through the live palette), 0xFF = transparent. */
static struct {
	int           loaded;
	short         hotx, hoty;
	unsigned char raw[16 * 16];      /* source palette indices (0xFF = transp) */
	unsigned char pal[16 * 3];       /* the cursor's own 16-colour RGB palette */
	unsigned char img[16 * 16];      /* raw[] re-mapped to live CLUT indices    */
} g_color_cursor;

/* The full cursor set loaded from frua.cur (the DOS ALWAYS cursor library:
 * 0 sword, 1 shield, 3-10 directional arrows, 20-22 turns, 25 hourglass ...).
 * qd_select_color_cursor makes one of these the active g_color_cursor. */
#define QD_CURSOR_BANK_MAX 32
static struct {
	int           present;
	short         hotx, hoty;
	unsigned char raw[16 * 16];
	unsigned char pal[16 * 3];
} g_cursor_bank[QD_CURSOR_BANK_MAX];
static int g_cursor_bank_sel = -1;       /* active bank index, -1 = none */

/* #153: on the 1-bit backend, colour cursor art is pure waste — the CLUT
 * bake below costs 16 nearest-colour scans (4096 distance computations) on
 * EVERY palette install, and the composited CLUT indexes then render
 * through the luminance ink LUT as a smudgy threshold of the colour art.
 * Instead, derive a REAL 1-bit Mac cursor from the bank entry once at
 * select time: mask = the opaque pixels, data = the DARK ones (the same
 * (2r+5g+b)>>3 < 112 split the ST-high backend renders by). The DOS art's
 * dark outlines + light fills come out as the classic Mac cursor look —
 * black outline, white body — crisp on any background, and the colour
 * pointer stays unloaded so no bake ever runs. (The Mac app ships NO CURS
 * resources — rfork-checked — so there is no faithful 1-bit set to lift;
 * this derivation is the mono cursor.) */
int g_dsp_mono_active;                  /* 1-bit backend active; set only by
                                         * platform/display_sthigh.c — lives
                                         * HERE so machines without that
                                         * backend (Amiga) still link */

static void qd_derive_mono_cursor(const unsigned char *raw,
                                  const unsigned char *pal,
                                  short hotx, short hoty)
{
	Cursor c;
	short  r, i;

	for (r = 0; r < 16; r++) {
		unsigned short data = 0, mask = 0;

		for (i = 0; i < 16; i++) {
			unsigned char  v   = raw[r * 16 + i];
			unsigned short bit = (unsigned short)(0x8000u >> i);
			short          lum;

			if (v == 0xFF)
				continue;                /* transparent */
			mask |= bit;
			v &= 0x0F;
			lum = (short)((2 * pal[v * 3 + 0]
			             + 5 * pal[v * 3 + 1]
			             +     pal[v * 3 + 2]) >> 3);
			if (lum < 112)
				data |= bit;             /* dark -> black ink */
		}
		c.data[r] = data;
		c.mask[r] = mask;
	}
	c.hotSpot.h = hotx;
	c.hotSpot.v = hoty;
	SetCursor(&c);
}

/* (Re)resolve the cursor's RGB palette to the LIVE CLUT and translate the raw
 * source indices into screen indices. The cursor palette is fixed but the CLUT
 * changes per screen (boot wall palette -> UI palette -> dungeon ...), so this
 * must re-run on every qd_set_palette or the baked colours drift (e.g. the grey
 * sword blade renders purple against the wall palette it was first baked on). */
static void qd_rebake_color_pointer(void)
{
	unsigned char map[16];
	short          i;

	if (!g_color_cursor.loaded)
		return;
	for (i = 0; i < 16; i++) {
		RGBColor c;
		c.red   = (unsigned short)(g_color_cursor.pal[i * 3 + 0] * 257);
		c.green = (unsigned short)(g_color_cursor.pal[i * 3 + 1] * 257);
		c.blue  = (unsigned short)(g_color_cursor.pal[i * 3 + 2] * 257);
		map[i]  = qd_nearest_color(&c);
	}
	for (i = 0; i < 16 * 16; i++) {
		unsigned char v = g_color_cursor.raw[i];
		g_color_cursor.img[i] = (v == 0xFF) ? 0xFF : map[v & 0x0F];
	}
}

void qd_install_color_pointer(short w, short h, short hotx, short hoty,
                              const unsigned char *idx,
                              const unsigned char *pal_rgb)
{
	short i, n;

	if (w != 16 || h != 16 || idx == NULL || pal_rgb == NULL)
		return;                         /* 16x16 only for now */
	if (g_dsp_mono_active) {            /* #153: 1-bit derivation, no bake */
		qd_derive_mono_cursor(idx, pal_rgb, hotx, hoty);
		g_cursor_bank_sel = -1;
		return;
	}
	n = (short)(w * h);
	for (i = 0; i < n; i++)
		g_color_cursor.raw[i] = idx[i];
	for (i = 0; i < 16 * 3; i++)
		g_color_cursor.pal[i] = pal_rgb[i];
	g_color_cursor.hotx   = hotx;
	g_color_cursor.hoty   = hoty;
	g_color_cursor.loaded = 1;
	qd_rebake_color_pointer();          /* bake against the current CLUT */
	g_cursor_dirty = 1;                 /* re-push the 16bpp VBL sprite */
	g_cursor_bank_sel = -1;             /* single-install path: forget the bank */
}

/* The full FRUA cursor set (frua.cur) lives in a bank, loaded once at boot;
 * the engine's per-frame cursor pick (jt1007 item -> qd_select_color_cursor)
 * swaps the ACTIVE entry (g_color_cursor above) without re-reading the file.
 * All entries share the pack's single 16-colour palette. */
void qd_load_color_cursor(int idx, short w, short h, short hotx, short hoty,
                          const unsigned char *pix, const unsigned char *pal_rgb)
{
	short i;

	if (idx < 0 || idx >= QD_CURSOR_BANK_MAX)
		return;
	if (w != 16 || h != 16 || pix == NULL || pal_rgb == NULL)
		return;                         /* 16x16 only, matching the sprite */
	for (i = 0; i < 16 * 16; i++)
		g_cursor_bank[idx].raw[i] = pix[i];
	for (i = 0; i < 16 * 3; i++)
		g_cursor_bank[idx].pal[i] = pal_rgb[i];
	g_cursor_bank[idx].hotx    = hotx;
	g_cursor_bank[idx].hoty    = hoty;
	g_cursor_bank[idx].present = 1;
}

/* Make bank entry `idx` the active colour pointer. No-op when it is already
 * active (the engine re-picks every idle tick, so this guard keeps us off the
 * per-frame re-bake path). Silently ignores an absent/out-of-range entry so an
 * unmapped engine cursor just leaves the current shape up. */
void qd_select_color_cursor(int idx)
{
	short i;

	if (idx == g_cursor_bank_sel)
		return;
	if (idx < 0 || idx >= QD_CURSOR_BANK_MAX || !g_cursor_bank[idx].present)
		return;
	if (g_dsp_mono_active) {            /* #153: 1-bit derivation, no bake */
		qd_derive_mono_cursor(g_cursor_bank[idx].raw,
		                      g_cursor_bank[idx].pal,
		                      g_cursor_bank[idx].hotx,
		                      g_cursor_bank[idx].hoty);
		g_cursor_bank_sel = idx;         /* keep the re-pick guard hot */
		return;
	}
	for (i = 0; i < 16 * 16; i++)
		g_color_cursor.raw[i] = g_cursor_bank[idx].raw[i];
	for (i = 0; i < 16 * 3; i++)
		g_color_cursor.pal[i] = g_cursor_bank[idx].pal[i];
	g_color_cursor.hotx   = g_cursor_bank[idx].hotx;
	g_color_cursor.hoty   = g_cursor_bank[idx].hoty;
	g_color_cursor.loaded = 1;
	g_cursor_bank_sel     = idx;
	qd_rebake_color_pointer();          /* bake against the current CLUT */
	g_cursor_dirty = 1;                 /* re-push the 16bpp VBL sprite */
}

/* RGB565 pack (8-bit channels -> one screen word), matching the VIDEL LUT. */
static unsigned short qd_rgb565(unsigned char r, unsigned char g, unsigned char b)
{
	return (unsigned short)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

/* Bake the active cursor (the colour pointer if loaded, else the mono cursor)
 * into a 16x16 RGB565 sprite + 1-bit mask and push it to the VBL cursor, then
 * sync visibility. The colour sprite uses the pointer's own RGB (not the live
 * CLUT), so unlike the 8-bit composite it never needs re-baking on a palette
 * change. No-op when the platform cursor isn't active. */
static void qd_cursor_sync(void)
{
	unsigned short rgb[16 * 16];
	unsigned short mask[16];
	short r, c;

	if (!plat_cursor_active())
		return;

	if (g_color_cursor.loaded) {
		for (r = 0; r < 16; r++) {
			unsigned short m = 0;
			for (c = 0; c < 16; c++) {
				unsigned char v = g_color_cursor.raw[r * 16 + c];
				if (v == 0xFF) {              /* transparent */
					rgb[r * 16 + c] = 0;
					continue;
				}
				v &= 0x0F;
				rgb[r * 16 + c] = qd_rgb565(g_color_cursor.pal[v * 3 + 0],
				                            g_color_cursor.pal[v * 3 + 1],
				                            g_color_cursor.pal[v * 3 + 2]);
				m |= (unsigned short)(0x8000u >> c);
			}
			mask[r] = m;
		}
		plat_cursor_set_sprite(rgb, mask,
		                       g_color_cursor.hotx, g_color_cursor.hoty);
	} else {
		for (r = 0; r < 16; r++) {
			unsigned short d = g_cursor.data[r];
			unsigned short m = g_cursor.mask[r];
			for (c = 0; c < 16; c++) {
				unsigned short bit = (unsigned short)(0x8000u >> c);
				rgb[r * 16 + c] = (m & bit)
				    ? ((d & bit) ? 0x0000 : 0xFFFF)   /* black / white */
				    : 0;
			}
			mask[r] = m;
		}
		plat_cursor_set_sprite(rgb, mask,
		                       g_cursor.hotSpot.h, g_cursor.hotSpot.v);
	}
	plat_cursor_show(g_cursor_init && g_cursor_level >= 0);
}

/* Push the sprite lazily: the first present after any shape change (or after
 * the VBL installs). Cheap when not dirty. Called from qd_present. */
static void qd_cursor_tick(void)
{
	if (g_cursor_dirty && plat_cursor_active()) {
		qd_cursor_sync();
		g_cursor_dirty = 0;
	}
}

/* Per-frame cursor keep-up for a modal spin (l2d3e): when the VBL owns the
 * pointer it already tracks the position every vblank, so a mouse move needs
 * only a pending SHAPE swap pushed (a 16x16 sprite, no c2p). Using a full
 * qd_present per move instead just throttles the modal loop, starving the
 * sword/shield hit-test so the cursor lags and flickers. Fall back to a full
 * present only when the cursor is software-composited into the frame. */
/* #150 (uncommitted, pending a live-mouse trail check): present only the
 * cursor's dirty rect — the union of where it was last drawn
 * (g_cursor_save_x/y, updated by every composite in both present paths, so
 * no trails) and where it lands now — instead of a FULL present (144 KB
 * re-pack on ST High) per mouse-move event. The Mac had a hardware cursor
 * and never paid this; FRUA is mouse-driven, so it fired constantly. */
void qd_cursor_track(void)
{
	short mx, my, hx, hy, nx, ny;
	short ox, oy, x0, y0, x1, y1;
	int   color;
	int   prev_touched;

	if (plat_cursor_active()) {
		qd_cursor_tick();
		return;
	}
	if (g_present_rect_hook == NULL) {       /* backend has no dirty-rect path */
		/* #152: a cursor MOVE changes the composited output even though
		 * the surface bytes didn't — force the present through the
		 * clean-present gate or the pointer freezes on screen. */
		g_qd_touched = 1;
#ifdef FRUA_MONOPROF
		g_qdp_src = 4;
#endif
		qd_present();
		return;
	}

	/* Where the cursor was last drawn on screen. g_cursor_save_x/y is set by
	 * EVERY composite (this path and a full qd_present) and left intact by
	 * cursor_restore, so it always names the last-drawn position — capture it
	 * BEFORE the new composite overwrites it, and union it in so the old
	 * pointer is erased with no trail across the two present paths. */
	ox = g_cursor_save_x;
	oy = g_cursor_save_y;

	color = g_color_cursor.loaded;
	hx = color ? g_color_cursor.hotx : g_cursor.hotSpot.h;
	hy = color ? g_color_cursor.hoty : g_cursor.hotSpot.v;
	plat_mouse_pos(&mx, &my);
	nx = (short)(mx - hx);
	ny = (short)(my - hy);

	prev_touched = g_qd_touched;             /* #152: net-neutral write below */
	qd_cursor_tick();                        /* push a pending shape swap */
	cursor_composite();                      /* draw at new pos, save underneath */

	/* Dirty rect = union(new 16x16, previous 16x16). Packing the previous
	 * cells from the (now cursor-free) buffer erases the old pointer. Over-
	 * covering (e.g. the 0,0 seed before the first draw) is harmless; only
	 * under-covering would trail, and g_cursor_save_x/y never under-names. */
	x0 = nx; y0 = ny; x1 = (short)(nx + 16); y1 = (short)(ny + 16);
	if (ox < x0) x0 = ox;
	if (oy < y0) y0 = oy;
	if ((short)(ox + 16) > x1) x1 = (short)(ox + 16);
	if ((short)(oy + 16) > y1) y1 = (short)(oy + 16);
	g_present_rect_hook(x0, y0, (short)(x1 - x0), (short)(y1 - y0));

	cursor_restore();                        /* lift the sprite back out */
	g_qd_touched = prev_touched;             /* composite+restore = net zero */
}

static void cursor_composite(void)
{
	unsigned char *px;
	short          pitch, sw, sh, x, y, mx, my, ox, oy;
	unsigned char  black, white;
	int            color = g_color_cursor.loaded;
	short          hx = color ? g_color_cursor.hotx : g_cursor.hotSpot.h;
	short          hy = color ? g_color_cursor.hoty : g_cursor.hotSpot.v;
	RGBColor       bk = { 0, 0, 0 };
	RGBColor       wh = { 0xFFFF, 0xFFFF, 0xFFFF };

	g_cursor_saved = 0;
	if (plat_cursor_active())                /* the VBL draws the cursor */
		return;
	if (!g_cursor_init || g_cursor_level < 0 || g_cursor_obscured)
		return;
	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == NULL)
		return;

	plat_mouse_pos(&mx, &my);
	ox = (short)(mx - hx);
	oy = (short)(my - hy);
	black = qd_nearest_color(&bk);
	white = qd_nearest_color(&wh);

	for (y = 0; y < 16; y++) {
		short          dy   = (short)(oy + y);
		unsigned short dbit = color ? 0 : g_cursor.data[y];
		unsigned short mbit = color ? 0 : g_cursor.mask[y];
		unsigned char *d    = (dy >= 0 && dy < sh) ? px + (long)dy * pitch : NULL;
		for (x = 0; x < 16; x++) {
			short          dx  = (short)(ox + x);
			unsigned short bit = (unsigned short)(0x8000u >> x);
			unsigned char *sv  = &g_cursor_save[y * 16 + x];
			if (d == NULL || dx < 0 || dx >= sw) {
				*sv = 0;
				continue;
			}
			*sv = d[dx];                    /* save underneath */
			if (color) {
				unsigned char cv = g_color_cursor.img[y * 16 + x];
				if (cv != 0xFF)             /* opaque colour pixel */
					d[dx] = cv;
			} else if (mbit & bit) {        /* opaque: black or white */
				d[dx] = (dbit & bit) ? black : white;
			}
		}
	}
	g_cursor_save_x = ox;
	g_cursor_save_y = oy;
	g_cursor_saved  = 1;
}

static void cursor_restore(void)
{
	unsigned char *px;
	short          pitch, sw, sh, x, y;

	if (!g_cursor_saved)
		return;
	g_cursor_saved = 0;
	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == NULL)
		return;
	for (y = 0; y < 16; y++) {
		short dy = (short)(g_cursor_save_y + y);
		unsigned char *d;
		if (dy < 0 || dy >= sh)
			continue;
		d = px + (long)dy * pitch;
		for (x = 0; x < 16; x++) {
			short dx = (short)(g_cursor_save_x + x);
			if (dx < 0 || dx >= sw)
				continue;
			d[dx] = g_cursor_save[y * 16 + x];
		}
	}
}

/*
 * qd_cursor_refresh — called from the event pump on every idle spin. The
 * IKBD interrupt keeps plat_mouse_pos current in real time, but the cursor
 * is only drawn during qd_present, so an engine sitting in an event-wait
 * loop (WaitNextEvent / l725c / the input loops) would show a frozen cursor
 * until the next repaint. Presenting unconditionally here would burn a full
 * c2p + Vsync on every spin; instead we present only when the mouse has
 * actually moved since the last refresh. A still mouse is free; a moving
 * one re-presents (and the Vsync inside the present caps it at the refresh
 * rate), so the cursor tracks smoothly without the engine having to redraw.
 */
void qd_cursor_refresh(void)
{
	static short last_x = -1, last_y = -1;
	short        mx, my;

	if (plat_cursor_active())                /* the VBL tracks the cursor */
		return;
	if (!g_cursor_init || g_cursor_level < 0)
		return;
	plat_mouse_pos(&mx, &my);
	if (mx == last_x && my == last_y)
		return;                          /* unmoved — nothing to do */
	last_x = mx;
	last_y = my;
	g_cursor_obscured = 0;                   /* ObscureCursor restores on the first move */
	qd_cursor_track();                       /* #150: dirty-rect move, not a full c2p */
}

/* --- GDevice / Palette Manager minimum (see quickdraw.h) --- */
static void *g_gdevice_sentinel;

GDHandle GetGDevice(void)
{
	return (GDHandle)&g_gdevice_sentinel;
}

void SetGDevice(GDHandle gdh)
{
	(void)gdh;                      /* one screen device — no switch */
}

void PmForeColor(short pmIndex)
{
	GrafPtr  port;
	CGrafPtr cp;

	GetPort(&port);
	if (port == NULL)
		return;
	/* Palette index == CLUT index on the fixed 8bpp HAL; the Mac
	 * also resolves rgbFgColor from the palette entry — skipped,
	 * the index is authoritative here. */
	cp = (CGrafPtr)port;
	cp->fgColor = pmIndex;
}

void RGBForeColor(const RGBColor *color)
{
	GrafPtr  port;
	CGrafPtr cp;

	GetPort(&port);
	if (port == NULL || color == NULL)
		return;
	cp = (CGrafPtr)port;
	cp->rgbFgColor = *color;
	cp->fgColor    = qd_nearest_color(color);
}

void RGBBackColor(const RGBColor *color)
{
	GrafPtr  port;
	CGrafPtr cp;

	GetPort(&port);
	if (port == NULL || color == NULL)
		return;
	cp = (CGrafPtr)port;
	cp->rgbBkColor = *color;
	cp->bkColor    = qd_nearest_color(color);
}

/* --- text drawing --- */

/*
 * GetFNum — Font Manager: the font id for a family name. The port has
 * no font-family registry; hand out stable synthetic ids (128, 129, …)
 * per distinct name instead. The one live caller is the print chain's
 * jt428 (GetFNum("Moebius") for the TextFont of the printing port) —
 * the printing shim renders every face as its monospace print face, so
 * only stability matters, not the value.
 */
void GetFNum(ConstStr255Param name, short *num)
{
	static unsigned char reg[4][32];
	static short         reg_n;
	short                i, j;
	unsigned char        len;

	if (num == NULL)
		return;
	*num = 0;
	if (name == NULL || name[0] == 0 || name[0] > 31)
		return;
	len = name[0];
	for (i = 0; i < reg_n; i++) {
		if (reg[i][0] != len)
			continue;
		for (j = 1; j <= len && reg[i][j] == name[j]; j++)
			;
		if (j > len) {
			*num = (short)(128 + i);
			return;
		}
	}
	if (reg_n < 4) {
		for (j = 0; j <= len; j++)
			reg[reg_n][j] = name[j];
		*num = (short)(128 + reg_n);
		reg_n++;
	}
}

void TextFont(short font) { GrafPtr p; GetPort(&p); if (p) p->txFont = font; }
void TextFace(short face) { GrafPtr p; GetPort(&p); if (p) p->txFace = (Style)face; }
void TextMode(short mode) { GrafPtr p; GetPort(&p); if (p) p->txMode = mode; }
void TextSize(short size) { GrafPtr p; GetPort(&p); if (p) p->txSize = size; }

#ifdef FRUA_PLANAR
/*
 * ADR-0016 B4: stamp one text pixel into the backend's draw-time plane buffer,
 * at the SAME clip-tested (x,y) the chunky store just wrote. The mac-font / 8x8
 * glyph is inherently per-pixel (mac_font_pixel / a shifted mask), so the plane
 * store rides DrawChar's own GrafPort clip for exact coverage parity with the
 * chunky store. `idx` is the chunky palette index (fgColor or bkColor); the
 * pixel's band (y*nbands/h) selects the remap row, so the slot bits are the
 * SAME ones the per-band c2p would produce for that pixel — byte-identical.
 * (planar_glyph_stlow is the clip-free batch form of this store, for callers
 * with a packed single-band glyph.)
 */
static void dc_plane_px(const dsp_planar_dt_t *dt, short x, short y,
                        unsigned char idx)
{
	short band = (short)((long)y * dt->nbands / dt->h);
	planar_put_stlow(dt->planes, dt->line_bytes, dt->nplanes, x, y,
	                 dt->remap[(long)band * 256 + idx]);
}
#endif

/*
 * DrawChar — render one glyph at pnLoc with the baseline at pnLoc.v, then
 * advance the pen by the cell width. The 8x8 fallback font has ascent 7,
 * so glyph row r maps to y = pnLoc.v - (ascent - 1) + r, i.e. the body
 * sits at y = pnLoc.v - 6 .. pnLoc.v and the descender row coincides with
 * the next baseline. Each glyph bit set lays down fgColor; for srcCopy
 * unset bits write bkColor, for srcOr (and any other mode for now) unset
 * bits leave the destination alone.
 */
void DrawChar(short ch)
{
	GrafPtr       port;
	CGrafPtr      cp;
	Rect          clip;
	PixMap       *pm;
	unsigned char fg, bk, mode;
	Boolean       draw;
	short         advance;
#ifdef FRUA_PLANAR
	dsp_planar_dt_t dt;
	int             dt_on = 0;
#endif

	GetPort(&port);
	if (port == NULL)
		return;
	/* Printing: when the current port is the open printing GrafPort
	 * (the Mac Printing Manager model — PrOpenDoc's port, SetPort'd by
	 * the engine's print chain), the character belongs to the page, not
	 * a PixMap. The capture advances the pen itself. */
	if (pr_port_capture(port, ch))
		return;
	cp   = (CGrafPtr)port;
	draw = qd_effective_clip(port, &clip);
	if (draw)
		g_qd_touched = 1;        /* #152: about to write pixels */

	if (((unsigned short)cp->portVersion & CGRAFPORT_FLAG) != CGRAFPORT_FLAG
	 || cp->portPixMap == NULL || *cp->portPixMap == NULL)
		draw = 0;
	pm = draw ? *cp->portPixMap : NULL;
	if (pm != NULL && pm->baseAddr == NULL)
		draw = 0;

	fg   = (unsigned char)cp->fgColor;
	bk   = (unsigned char)cp->bkColor;
	mode = (unsigned char)port->txMode;

#ifdef FRUA_PLANAR
	/* Draw-time plane store, in parallel with the chunky store below, when the
	 * active backend is in draw-time plane mode (0 = default chunky+c2p path). */
	dt_on = draw && dsp_planar_draw_target(&dt);
#endif

	if (g_mac_font_loaded) {
		/* Real Mac bitmap font path — variable-width glyphs, per-row
		 * bit lookup against the strike. */
		short top, row, col, x, y, sw, lsb;

		sw      = mac_font_strike_width(ch);
		advance = mac_font_advance(ch);
		lsb     = mac_font_offset(ch);   /* left-side bearing within the cell */
		if (draw) {
			top = (short)(port->pnLoc.v - (g_mac_font.ascent - 1));
			for (row = 0; row < g_mac_font.height; row++) {
				y = (short)(top + row);
				if (y < clip.top || y >= clip.bottom)
					continue;
				for (col = 0; col < sw; col++) {
					x = (short)(port->pnLoc.h + lsb + col);
					if (x < clip.left || x >= clip.right)
						continue;
					if (mac_font_pixel(ch, col, row)) {
						unsigned char *p = (unsigned char *)pm->baseAddr
						                 + (y - pm->bounds.top) * pm->rowBytes
						                 + (x - pm->bounds.left);
						*p = fg;
#ifdef FRUA_PLANAR
						if (dt_on) dc_plane_px(&dt, x, y, fg);
#endif
					} else if (mode == srcCopy) {
						unsigned char *p = (unsigned char *)pm->baseAddr
						                 + (y - pm->bounds.top) * pm->rowBytes
						                 + (x - pm->bounds.left);
						*p = bk;
#ifdef FRUA_PLANAR
						if (dt_on) dc_plane_px(&dt, x, y, bk);
#endif
					}
				}
			}
		}
	} else {
		/* 8x8 fallback — the embedded font. */
		const unsigned char *glyph = qd_font_8x8[(unsigned char)ch];
		short                top, row, col, x, y;
		unsigned char        mask, bits;

		advance = qd_font_8x8_width;
		if (draw) {
			top = (short)(port->pnLoc.v - (qd_font_8x8_ascent - 1));
			for (row = 0; row < qd_font_8x8_height; row++) {
				y    = (short)(top + row);
				bits = glyph[row];
				if (y < clip.top || y >= clip.bottom)
					continue;
				for (col = 0, mask = 0x80; col < qd_font_8x8_width;
				     col++, mask = (unsigned char)(mask >> 1)) {
					x = (short)(port->pnLoc.h + col);
					if (x < clip.left || x >= clip.right)
						continue;
					if (bits & mask) {
						unsigned char *p = (unsigned char *)pm->baseAddr
						                 + (y - pm->bounds.top) * pm->rowBytes
						                 + (x - pm->bounds.left);
						*p = fg;
#ifdef FRUA_PLANAR
						if (dt_on) dc_plane_px(&dt, x, y, fg);
#endif
					} else if (mode == srcCopy) {
						unsigned char *p = (unsigned char *)pm->baseAddr
						                 + (y - pm->bounds.top) * pm->rowBytes
						                 + (x - pm->bounds.left);
						*p = bk;
#ifdef FRUA_PLANAR
						if (dt_on) dc_plane_px(&dt, x, y, bk);
#endif
					}
				}
			}
		}
	}

	port->pnLoc.h = (short)(port->pnLoc.h + advance);
}

/*
 * DrawString — Pascal string: str[0] is the length, str[1..len] the bytes.
 * Each char goes through DrawChar so glyph clipping / pen advance stay in
 * one place.
 */
void DrawString(ConstStr255Param str)
{
	unsigned char len, i;

	if (str == NULL)
		return;
	/* Ensure the real FRUA bitmap font (FONT -27001) is in before drawing.
	 * A few engine text paints run before main()'s mac_font_load, and the
	 * built-in 8x8 fallback only carries ~20 glyphs (everything else renders
	 * as a hollow box), so lazy-load the full font on first use. */
	if (!g_mac_font_loaded)
		mac_font_load(-27001);
	len = str[0];
	for (i = 1; i <= len; i++)
		DrawChar((short)str[i]);
}

/*
 * CharWidth / StringWidth — report the cell width. Fixed-pitch font, so
 * StringWidth is len * width with no kerning. (Both are pen-non-mutating;
 * the Mac advances the pen only in DrawChar / DrawString.)
 */
short CharWidth(short ch)
{
	if (g_mac_font_loaded)
		return mac_font_advance(ch);
	(void)ch;
	return (short)qd_font_8x8_width;
}

short StringWidth(ConstStr255Param str)
{
	unsigned char len, i;
	short         total = 0;

	if (str == NULL)
		return 0;
	len = str[0];
	if (g_mac_font_loaded) {
		for (i = 1; i <= len; i++)
			total = (short)(total + mac_font_advance((short)str[i]));
		return total;
	}
	return (short)(len * qd_font_8x8_width);
}

/* The GrafPort must be the exact 108-byte Macintosh layout. */
typedef char qd_assert_grafport_size[sizeof(GrafPort) == 108 ? 1 : -1];
typedef char qd_assert_portrect_off[offsetof(GrafPort, portRect) == 16 ? 1 : -1];

/* The Color QuickDraw structs must match their Macintosh layouts too — and
 * CGrafPort must be GrafPort's size, with portRect at the same offset. */
typedef char qd_assert_cgrafport[sizeof(CGrafPort) == 108 ? 1 : -1];
/* ===================================================================== *
 *  ForeColor / BackColor + DrawPicture — the PICT playback path.         *
 *                                                                        *
 *  A focused Mac PICT opcode interpreter. The port has no PICT recorder  *
 *  and no GWorld; DrawPicture rasterises straight into the current       *
 *  CGrafPort's 8-bit PixMap. It handles the v1/v2 headers, Clip,         *
 *  comments/NOPs, and the three image-carrying bitmap opcodes            *
 *  (BitsRect 0x90, PackBitsRect 0x98, DirectBitsRect 0x9A). Enough for   *
 *  the design-editor "Import Picture" path; unknown opcodes stop the     *
 *  interpreter rather than guess a data size.                            *
 * ===================================================================== */

static void qd_classic_rgb(long c, RGBColor *out)
{
	const unsigned short F = 0xFFFF;

	switch (c) {
	case whiteColor:   out->red = F; out->green = F; out->blue = F; break;
	case yellowColor:  out->red = F; out->green = F; out->blue = 0; break;
	case magentaColor: out->red = F; out->green = 0; out->blue = F; break;
	case redColor:     out->red = F; out->green = 0; out->blue = 0; break;
	case cyanColor:    out->red = 0; out->green = F; out->blue = F; break;
	case greenColor:   out->red = 0; out->green = F; out->blue = 0; break;
	case blueColor:    out->red = 0; out->green = 0; out->blue = F; break;
	case blackColor:
	default:           out->red = 0; out->green = 0; out->blue = 0; break;
	}
}

void ForeColor(long color)
{
	RGBColor c;
	qd_classic_rgb(color, &c);
	RGBForeColor(&c);
}

void BackColor(long color)
{
	RGBColor c;
	qd_classic_rgb(color, &c);
	RGBBackColor(&c);
}

/* --- a bounds-checked big-endian cursor over the picture data --- */
typedef struct {
	const unsigned char *p;
	const unsigned char *base;      /* picture start, for v2 word alignment */
	const unsigned char *end;
	int                  bad;
} pict_reader;

static unsigned char pr_byte(pict_reader *r)
{
	if (r->p >= r->end) { r->bad = 1; return 0; }
	return *r->p++;
}

static short pr_word(pict_reader *r)
{
	unsigned hi = pr_byte(r);
	unsigned lo = pr_byte(r);
	return (short)((hi << 8) | lo);
}

static long pr_long(pict_reader *r)
{
	unsigned long hi = (unsigned short)pr_word(r);
	unsigned long lo = (unsigned short)pr_word(r);
	return (long)((hi << 16) | lo);
}

static void pr_skip(pict_reader *r, long n)
{
	if (n < 0 || r->p + n > r->end) { r->bad = 1; r->p = r->end; return; }
	r->p += n;
}

static void pr_rect(pict_reader *r, Rect *rc)
{
	rc->top    = pr_word(r);
	rc->left   = pr_word(r);
	rc->bottom = pr_word(r);
	rc->right  = pr_word(r);
}

static void pr_align(pict_reader *r)            /* v2 opcodes are word-aligned */
{
	if (((r->p - r->base) & 1) != 0)
		(void)pr_byte(r);
}

/* Expand one PackBits row into exactly `dstBytes` bytes (excess in a run is
 * clamped to the row, matching the Mac _UnpackBits trap). */
static void pict_unpack_row(pict_reader *r, unsigned char *dst, short dstBytes)
{
	short n = 0;

	while (n < dstBytes && !r->bad) {
		signed char c = (signed char)pr_byte(r);
		short       i;

		if (c >= 0) {
			short cnt = (short)c + 1;
			for (i = 0; i < cnt && n < dstBytes; i++)
				dst[n++] = pr_byte(r);
		} else if (c != -128) {
			short         cnt = (short)(1 - c);
			unsigned char v   = pr_byte(r);
			for (i = 0; i < cnt && n < dstBytes; i++)
				dst[n++] = v;
		}
		/* c == -128: no-op byte */
	}
}

/* Resolve one source pixel (sx,sy) of the decoded bitmap to an 8-bit
 * destination palette index. */
static unsigned char pict_pixel(const unsigned char *buf, long rowBytes,
                                short sx, short sy, short pixelSize,
                                const unsigned char *ctabMap, int hasCtab,
                                unsigned char fg, unsigned char bg)
{
	const unsigned char *row = buf + (long)sy * rowBytes;

	if (pixelSize == 1 && !hasCtab) {
		int bit = (row[sx >> 3] >> (7 - (sx & 7))) & 1;
		return bit ? fg : bg;
	}
	if (pixelSize <= 8) {
		unsigned idx;
		if (pixelSize == 8)      idx = row[sx];
		else if (pixelSize == 4) idx = (sx & 1) ? (row[sx >> 1] & 0x0F)
		                                        : (row[sx >> 1] >> 4);
		else if (pixelSize == 2) idx = (row[sx >> 2] >> ((3 - (sx & 3)) * 2)) & 3;
		else /* 1 */             idx = (row[sx >> 3] >> (7 - (sx & 7))) & 1;
		return hasCtab ? ctabMap[idx & 0xFF] : (unsigned char)idx;
	}
	/* direct colour */
	{
		RGBColor c;
		if (pixelSize == 16) {
			const unsigned char *pp = row + (long)sx * 2;
			unsigned short px = (unsigned short)((pp[0] << 8) | pp[1]);
			unsigned r5 = (px >> 10) & 0x1F, g5 = (px >> 5) & 0x1F, b5 = px & 0x1F;
			c.red   = (unsigned short)((r5 << 11) | (r5 << 6) | (r5 << 1));
			c.green = (unsigned short)((g5 << 11) | (g5 << 6) | (g5 << 1));
			c.blue  = (unsigned short)((b5 << 11) | (b5 << 6) | (b5 << 1));
		} else {                          /* 32-bit chunky: [x,R,G,B] */
			const unsigned char *pp = row + (long)sx * 4;
			c.red   = (unsigned short)(pp[1] << 8 | pp[1]);
			c.green = (unsigned short)(pp[2] << 8 | pp[2]);
			c.blue  = (unsigned short)(pp[3] << 8 | pp[3]);
		}
		return qd_nearest_color(&c);
	}
}

/* Draw one BitsRect / PackBitsRect / DirectBitsRect opcode.  `pictXform`
 * carries the picFrame -> caller-dstRect mapping so a source pixel lands in
 * the right place on the current port. */
static void pict_do_bits(pict_reader *r, short opcode, int hasRgn,
                         const Rect *picFrame, const Rect *pictDst,
                         const Rect *pictClip, GrafPtr port)
{
	CGrafPtr        cp = (CGrafPtr)port;
	PixMap         *pm;
	Rect            clip, bounds, srcR, dstR, opDst, screen;
	unsigned char   ctabMap[256];
	unsigned char  *buf, *dp;
	unsigned char   fg, bg;
	long            rowBytes, srcRowBytes, dstRow, bufRows;
	short           pixelSize = 1, pwide, high, sy, X, Y;
	int             hasCtab = 0, packed;

	if (opcode == 0x009A)
		(void)pr_long(r);               /* DirectBitsRect: skip baseAddr */

	rowBytes = (unsigned short)pr_word(r);
	if (opcode == 0x009A || (rowBytes & 0x8000)) {
		/* PixMap header */
		rowBytes &= 0x7FFF;
		pr_rect(r, &bounds);
		(void)pr_word(r);               /* pmVersion  */
		(void)pr_word(r);               /* packType   */
		(void)pr_long(r);               /* packSize   */
		(void)pr_long(r); (void)pr_long(r);   /* hRes, vRes */
		(void)pr_word(r);               /* pixelType  */
		pixelSize = pr_word(r);
		(void)pr_word(r);               /* cmpCount   */
		(void)pr_word(r);               /* cmpSize    */
		(void)pr_long(r);               /* planeBytes */
		(void)pr_long(r);               /* pmTable    */
		(void)pr_long(r);               /* pmReserved */
		if (opcode != 0x009A) {         /* indexed: read the colour table */
			short i, ctSize;
			(void)pr_long(r);           /* ctSeed  */
			(void)pr_word(r);           /* ctFlags */
			ctSize = pr_word(r);
			for (i = 0; i <= ctSize && !r->bad; i++) {
				RGBColor c;
				short    v = pr_word(r);
				c.red = pr_word(r); c.green = pr_word(r); c.blue = pr_word(r);
				if (v < 0 || v > 255) v = (short)(i & 0xFF);
				ctabMap[v & 0xFF] = qd_nearest_color(&c);
			}
			hasCtab = 1;
		}
	} else {
		/* old-style 1-bit BitMap */
		pr_rect(r, &bounds);
		pixelSize = 1;
	}

	pr_rect(r, &srcR);
	pr_rect(r, &dstR);                  /* destination within the picFrame */
	(void)pr_word(r);                   /* transfer mode — treated as srcCopy */

	if (hasRgn) {                       /* BitsRgn/PackBitsRgn: skip maskRgn */
		short rs = pr_word(r);
		pr_skip(r, rs - 2);
	}

	/* clamp dstR to the picture's clip (picture coords) */
	opDst = dstR;
	if (pictClip->left   > opDst.left)   opDst.left   = pictClip->left;
	if (pictClip->top    > opDst.top)    opDst.top    = pictClip->top;
	if (pictClip->right  < opDst.right)  opDst.right  = pictClip->right;
	if (pictClip->bottom < opDst.bottom) opDst.bottom = pictClip->bottom;

	srcRowBytes = rowBytes;
	high = (short)(bounds.bottom - bounds.top);
	pwide = (short)(bounds.right - bounds.left);
	/* 0x90/0x91 are unpacked; 0x98/0x99/0x9A are PackBits (rowBytes >= 8) */
	packed = (opcode == 0x0098 || opcode == 0x0099 || opcode == 0x009A) &&
	         (rowBytes >= 8);
	if (r->bad || high <= 0 || pwide <= 0 || rowBytes <= 0)
		return;

	/* decode the whole source bitmap into a scratch buffer */
	bufRows = (long)high * srcRowBytes;
	buf = (unsigned char *)NewPtr(bufRows);
	if (buf == NULL)
		return;
	for (sy = 0; sy < high && !r->bad; sy++) {
		unsigned char *row = buf + (long)sy * srcRowBytes;
		if (!packed) {
			short i;
			for (i = 0; i < srcRowBytes; i++)
				row[i] = pr_byte(r);
		} else {
			long cnt = (rowBytes > 250) ? (unsigned short)pr_word(r)
			                            : (unsigned char)pr_byte(r);
			const unsigned char *rowend;
			(void)cnt;                  /* the count bounds the run stream */
			rowend = r->p;
			pict_unpack_row(r, row, (short)srcRowBytes);
			(void)rowend;
		}
	}

	/* destination pixmap + effective clip */
	if (((unsigned short)cp->portVersion & CGRAFPORT_FLAG) != CGRAFPORT_FLAG ||
	    cp->portPixMap == NULL || *cp->portPixMap == NULL) {
		DisposePtr((Ptr)buf);
		return;
	}
	pm     = *cp->portPixMap;
	dp     = (unsigned char *)pm->baseAddr;
	dstRow = pm->rowBytes;
	if (dp == NULL || !qd_effective_clip(port, &clip)) {
		DisposePtr((Ptr)buf);
		return;
	}
	fg = (unsigned char)cp->fgColor;
	bg = (unsigned char)cp->bkColor;

	/* map opDst (picture coords) -> screen coords through picFrame->pictDst */
	{
		long pfW = picFrame->right - picFrame->left;
		long pfH = picFrame->bottom - picFrame->top;
		long cdW = pictDst->right - pictDst->left;
		long cdH = pictDst->bottom - pictDst->top;
		if (pfW <= 0 || pfH <= 0 || cdW <= 0 || cdH <= 0) {
			DisposePtr((Ptr)buf);
			return;
		}
		screen.left   = (short)(pictDst->left + (opDst.left   - picFrame->left) * cdW / pfW);
		screen.right  = (short)(pictDst->left + (opDst.right  - picFrame->left) * cdW / pfW);
		screen.top    = (short)(pictDst->top  + (opDst.top    - picFrame->top)  * cdH / pfH);
		screen.bottom = (short)(pictDst->top  + (opDst.bottom - picFrame->top)  * cdH / pfH);

		/* intersect with the port clip + pixmap bounds */
		if (clip.left   > screen.left)   screen.left   = clip.left;
		if (clip.top    > screen.top)    screen.top    = clip.top;
		if (clip.right  < screen.right)  screen.right  = clip.right;
		if (clip.bottom < screen.bottom) screen.bottom = clip.bottom;
		if (pm->bounds.left   > screen.left)   screen.left   = pm->bounds.left;
		if (pm->bounds.top    > screen.top)    screen.top    = pm->bounds.top;
		if (pm->bounds.right  < screen.right)  screen.right  = pm->bounds.right;
		if (pm->bounds.bottom < screen.bottom) screen.bottom = pm->bounds.bottom;

		for (Y = screen.top; Y < screen.bottom; Y++) {
			/* inverse-map Y -> source row */
			long fy = picFrame->top + (long)(Y - pictDst->top) * pfH / cdH;
			long sy2 = srcR.top + (fy - dstR.top) * (bounds.bottom - bounds.top)
			           / (dstR.bottom - dstR.top ? dstR.bottom - dstR.top : 1);
			long srow = sy2 - bounds.top;
			unsigned char *dstline;
			if (srow < 0 || srow >= high)
				continue;
			dstline = dp + (long)(Y - pm->bounds.top) * dstRow;
			for (X = screen.left; X < screen.right; X++) {
				long fx = picFrame->left + (long)(X - pictDst->left) * pfW / cdW;
				long sx2 = srcR.left + (fx - dstR.left) * (bounds.right - bounds.left)
				           / (dstR.right - dstR.left ? dstR.right - dstR.left : 1);
				long scol = sx2 - bounds.left;
				if (scol < 0 || scol >= pwide)
					continue;
				dstline[X - pm->bounds.left] =
					pict_pixel(buf, srcRowBytes, (short)scol, (short)srow,
					           pixelSize, ctabMap, hasCtab, fg, bg);
			}
		}
	}
	DisposePtr((Ptr)buf);
}

void DrawPicture(PicHandle myPicture, const Rect *dstRect)
{
	GrafPtr      port;
	pict_reader  R;
	Rect         picFrame, pictDst, pictClip;
	int          v2;

	if (myPicture == NULL || *myPicture == NULL || dstRect == NULL)
		return;
	GetPort(&port);
	if (port == NULL)
		return;

	R.base = (const unsigned char *)*myPicture;
	R.end  = R.base + GetHandleSize((Handle)myPicture);
	R.p    = R.base;
	R.bad  = 0;

	(void)pr_word(&R);                  /* picSize  */
	pr_rect(&R, &picFrame);             /* picFrame */
	pictDst  = *dstRect;
	pictClip = picFrame;                /* default clip = the whole frame */

	v2 = (R.p < R.end && *R.p == 0x00); /* v2 opcodes are word-aligned (0x0011) */

	while (!R.bad && R.p < R.end) {
		short opcode;
		if (v2) {
			pr_align(&R);
			opcode = pr_word(&R);
		} else {
			opcode = (short)pr_byte(&R);
		}

		switch (opcode) {
		case 0x0000:                    /* NOP */
			break;
		case 0x0011:                    /* VersionOp */
			if (v2) (void)pr_word(&R);  /* version word 0x02FF */
			else    (void)pr_byte(&R);  /* version byte 0x01   */
			break;
		case 0x0C00:                    /* HeaderOp (v2) */
			pr_skip(&R, 24);
			break;
		case 0x0001: {                  /* Clip: region */
			short rgnSize = pr_word(&R);
			if (rgnSize >= 10) {
				pr_rect(&R, &pictClip);
				pr_skip(&R, rgnSize - 10);
			} else {
				pr_skip(&R, rgnSize - 2);
			}
			break;
		}
		case 0x001E:                    /* DefHilite */
			break;
		case 0x00A0:                    /* ShortComment */
			(void)pr_word(&R);
			break;
		case 0x00A1: {                  /* LongComment */
			short n;
			(void)pr_word(&R);          /* kind */
			n = pr_word(&R);
			pr_skip(&R, n);
			break;
		}
		case 0x0090:                    /* BitsRect      */
		case 0x0098:                    /* PackBitsRect  */
			pict_do_bits(&R, opcode, 0, &picFrame, &pictDst, &pictClip, port);
			if (v2) pr_align(&R);
			break;
		case 0x0091:                    /* BitsRgn       */
		case 0x0099: {                  /* PackBitsRgn   */
			/* skip the region that precedes the pixel data */
			pict_do_bits(&R, opcode, 1, &picFrame, &pictDst, &pictClip, port);
			if (v2) pr_align(&R);
			break;
		}
		case 0x009A:                    /* DirectBitsRect */
			pict_do_bits(&R, opcode, 0, &picFrame, &pictDst, &pictClip, port);
			if (v2) pr_align(&R);
			break;
		case 0x00FF:                    /* OpEndPic */
			return;
		default:
			/* an opcode outside the handled set — stop rather than
			 * mis-skip an unknown data length. */
			return;
		}
	}
}

typedef char qd_assert_cport_rect[offsetof(CGrafPort, portRect) == 16 ? 1 : -1];
typedef char qd_assert_pixmap[sizeof(PixMap) == 50 ? 1 : -1];
typedef char qd_assert_pixpat[sizeof(PixPat) == 28 ? 1 : -1];
typedef char qd_assert_rgbcolor[sizeof(RGBColor) == 6 ? 1 : -1];
typedef char qd_assert_colorspec[sizeof(ColorSpec) == 8 ? 1 : -1];
typedef char qd_assert_region[sizeof(Region) == 10 ? 1 : -1];
