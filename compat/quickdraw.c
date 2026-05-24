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
#include "macmemory.h"          /* NewHandleClear, DisposeHandle */
#include "display.h"            /* dsp_color_t, dsp_detect — palette forward */
#include "font_8x8.h"           /* qd_font_8x8 — fallback bitmap font */

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

	SetPort((GrafPtr)cp);
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

void TextFont(short font) { GrafPtr p; GetPort(&p); if (p) p->txFont = font; }
void TextFace(short face) { GrafPtr p; GetPort(&p); if (p) p->txFace = (Style)face; }
void TextMode(short mode) { GrafPtr p; GetPort(&p); if (p) p->txMode = mode; }
void TextSize(short size) { GrafPtr p; GetPort(&p); if (p) p->txSize = size; }

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
	GrafPtr              port;
	CGrafPtr             cp;
	const unsigned char *glyph;
	Rect                 clip;
	PixMap              *pm;
	short                top, row, col, x, y;
	unsigned char        fg, bk, mode, mask, bits;
	Boolean              draw;

	GetPort(&port);
	if (port == NULL)
		return;
	cp   = (CGrafPtr)port;
	draw = qd_effective_clip(port, &clip);

	if (((unsigned short)cp->portVersion & CGRAFPORT_FLAG) != CGRAFPORT_FLAG
	 || cp->portPixMap == NULL || *cp->portPixMap == NULL)
		draw = 0;
	pm = draw ? *cp->portPixMap : NULL;
	if (pm != NULL && pm->baseAddr == NULL)
		draw = 0;

	glyph = qd_font_8x8[(unsigned char)ch];
	fg    = (unsigned char)cp->fgColor;
	bk    = (unsigned char)cp->bkColor;
	mode  = (unsigned char)port->txMode;

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
				} else if (mode == srcCopy) {
					unsigned char *p = (unsigned char *)pm->baseAddr
					                 + (y - pm->bounds.top) * pm->rowBytes
					                 + (x - pm->bounds.left);
					*p = bk;
				}
			}
		}
	}

	port->pnLoc.h = (short)(port->pnLoc.h + qd_font_8x8_width);
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
	(void)ch;
	return (short)qd_font_8x8_width;
}

short StringWidth(ConstStr255Param str)
{
	if (str == NULL)
		return 0;
	return (short)(str[0] * qd_font_8x8_width);
}

/* The GrafPort must be the exact 108-byte Macintosh layout. */
typedef char qd_assert_grafport_size[sizeof(GrafPort) == 108 ? 1 : -1];
typedef char qd_assert_portrect_off[offsetof(GrafPort, portRect) == 16 ? 1 : -1];

/* The Color QuickDraw structs must match their Macintosh layouts too — and
 * CGrafPort must be GrafPort's size, with portRect at the same offset. */
typedef char qd_assert_cgrafport[sizeof(CGrafPort) == 108 ? 1 : -1];
typedef char qd_assert_cport_rect[offsetof(CGrafPort, portRect) == 16 ? 1 : -1];
typedef char qd_assert_pixmap[sizeof(PixMap) == 50 ? 1 : -1];
typedef char qd_assert_pixpat[sizeof(PixPat) == 28 ? 1 : -1];
typedef char qd_assert_rgbcolor[sizeof(RGBColor) == 6 ? 1 : -1];
typedef char qd_assert_colorspec[sizeof(ColorSpec) == 8 ? 1 : -1];
typedef char qd_assert_region[sizeof(Region) == 10 ? 1 : -1];
