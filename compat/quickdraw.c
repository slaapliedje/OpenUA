/*
 * Mac QuickDraw shim — geometry, the current port, PixMaps, regions, the
 * screen port, and the first drawing primitives (ADR-0003).
 *
 * The Point and rectangle calculations (to the Inside Macintosh semantics),
 * GetPort / SetPort, NewPixMap, the rectangular-region facility, the screen
 * GrafPort that owns the display back buffer, the rect primitives (EraseRect,
 * PaintRect, FrameRect), the line family (MoveTo / LineTo / GetPen), and the
 * ovals (PaintOval, FrameOval) over 8-bit paletted pixels — clipped to
 * portRect, the rest of the clipping machinery follows with window-aware
 * drawing.
 */

#include <stddef.h>             /* offsetof, NULL */

#include "quickdraw.h"
#include "macmemory.h"          /* NewHandleClear, DisposeHandle */

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

	/* Default colours — index 0 for background, 255 for foreground. The
	 * palette manager will set up RGB-keyed colour selection later; for
	 * now drawing reads fgColor / bkColor as literal 8-bit indices. */
	cp->fgColor = 255;
	cp->bkColor = 0;

	SetPort((GrafPtr)cp);
}

/*
 * Shared body of EraseRect / PaintRect — fill `r` (in the current port's
 * local coordinates) with `color`, clipped to portRect. Pixel coordinates
 * are local-minus-bounds.top-left; for the screen port bounds is the same
 * as portRect so the offsets are zero.
 */
static void qd_fill_rect(const Rect *r, unsigned char color)
{
	GrafPtr  port;
	CGrafPtr cp;
	PixMap  *pm;
	Rect     clipped;
	short    row, w;

	GetPort(&port);
	if (port == NULL || r == NULL)
		return;
	/* The first cut handles colour ports only — the b&w portBits path
	 * follows when the engine creates a b&w window. */
	cp = (CGrafPtr)port;
	if (((unsigned short)cp->portVersion & CGRAFPORT_FLAG) != CGRAFPORT_FLAG)
		return;
	if (cp->portPixMap == NULL || *cp->portPixMap == NULL)
		return;
	pm = *cp->portPixMap;
	if (pm->baseAddr == NULL)
		return;

	if (!SectRect(r, &cp->portRect, &clipped))
		return;

	w = (short)(clipped.right - clipped.left);
	for (row = clipped.top; row < clipped.bottom; row++) {
		unsigned char *p = (unsigned char *)pm->baseAddr
		                 + (row - pm->bounds.top) * pm->rowBytes
		                 + (clipped.left - pm->bounds.left);
		short col;

		for (col = 0; col < w; col++)
			p[col] = color;
	}
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
 * discarding writes that fall outside portRect. The per-pixel write that
 * FrameRect and LineTo share.
 */
static void qd_plot(GrafPtr port, short x, short y, unsigned char color)
{
	CGrafPtr cp = (CGrafPtr)port;
	PixMap  *pm;

	if (((unsigned short)cp->portVersion & CGRAFPORT_FLAG) != CGRAFPORT_FLAG)
		return;
	if (x < cp->portRect.left || x >= cp->portRect.right
	 || y < cp->portRect.top  || y >= cp->portRect.bottom)
		return;
	if (cp->portPixMap == NULL || *cp->portPixMap == NULL)
		return;
	pm = *cp->portPixMap;
	if (pm->baseAddr == NULL)
		return;
	((unsigned char *)pm->baseAddr)
		[(y - pm->bounds.top) * pm->rowBytes + (x - pm->bounds.left)] = color;
}

/* Horizontal / vertical line helpers — used by FrameRect's four edges. */
static void qd_hline(GrafPtr port, short x1, short x2, short y,
                     unsigned char color)
{
	if (x1 > x2) { short t = x1; x1 = x2; x2 = t; }
	for (; x1 <= x2; x1++)
		qd_plot(port, x1, y, color);
}

static void qd_vline(GrafPtr port, short x, short y1, short y2,
                     unsigned char color)
{
	if (y1 > y2) { short t = y1; y1 = y2; y2 = t; }
	for (; y1 <= y2; y1++)
		qd_plot(port, x, y1, color);
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
	unsigned char color;
	short l, t, rr, bb;

	GetPort(&port);
	if (port == NULL || r == NULL)
		return;
	if (EmptyRect(r))
		return;
	color = (unsigned char)((CGrafPtr)port)->fgColor;
	l  = r->left;
	t  = r->top;
	rr = (short)(r->right  - 1);
	bb = (short)(r->bottom - 1);

	qd_hline(port, l, rr, t,  color);       /* top    */
	qd_hline(port, l, rr, bb, color);       /* bottom */
	qd_vline(port, l,  t,  bb, color);      /* left   */
	qd_vline(port, rr, t,  bb, color);      /* right  */
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
 * 8-bit pixels, clipped to portRect per-pixel. PenSize / PenPat / PenMode
 * are not honoured yet — see the header.
 */
void LineTo(short h, short v)
{
	GrafPtr       port;
	short         x0, y0, x1, y1;
	short         dx, dy, sx, sy, err, e2;
	unsigned char color;

	GetPort(&port);
	if (port == NULL)
		return;
	x0 = port->pnLoc.h;
	y0 = port->pnLoc.v;
	x1 = h;
	y1 = v;
	color = (unsigned char)((CGrafPtr)port)->fgColor;

	dx = (short)(x1 - x0); if (dx < 0) dx = (short)-dx;
	dy = (short)(y1 - y0); if (dy < 0) dy = (short)-dy;
	sx = (short)(x0 < x1 ?  1 : -1);
	sy = (short)(y0 < y1 ?  1 : -1);
	err = (short)(dx - dy);

	for (;;) {
		qd_plot(port, x0, y0, color);
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
	unsigned char color;
	short         cx, cy, rx, ry, x, y;
	long          rx2, ry2, denom;

	GetPort(&port);
	if (port == NULL || r == NULL || EmptyRect(r))
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
			qd_hline(port, xleft, xright, y, color);
	}
}

void FrameOval(const Rect *r)
{
	GrafPtr       port;
	unsigned char color;
	short         cx, cy, rx, ry, x, y;
	long          rx2, ry2, denom;

	GetPort(&port);
	if (port == NULL || r == NULL || EmptyRect(r))
		return;
	qd_oval_axes(r, &cx, &cy, &rx, &ry);
	if (rx == 0 || ry == 0)
		return;
	color = (unsigned char)((CGrafPtr)port)->fgColor;
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
			qd_plot(port, xleft, y, color);
			if (xright != xleft)
				qd_plot(port, xright, y, color);
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
			qd_plot(port, x, ytop, color);
			if (ybottom != ytop)
				qd_plot(port, x, ybottom, color);
		}
	}
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
