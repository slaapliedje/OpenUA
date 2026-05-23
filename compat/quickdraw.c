/*
 * Mac QuickDraw shim — geometry, the current port, PixMaps, regions, the
 * screen port, and the first drawing primitives (ADR-0003).
 *
 * The Point and rectangle calculations (to the Inside Macintosh semantics),
 * GetPort / SetPort, NewPixMap, the rectangular-region facility, the screen
 * GrafPort that owns the display back buffer, the rect primitives (EraseRect,
 * PaintRect, FrameRect), the line family (MoveTo / LineTo / GetPen), the
 * ovals (PaintOval, FrameOval), CopyBits (same-size, srcCopy), and ClipRect
 * over 8-bit paletted pixels — every primitive funnels through
 * qd_effective_clip (portRect ∩ visRgn ∩ clipRgn), so BeginUpdate's narrowed
 * visRgn and ClipRect's application clip both restrict drawing as on the Mac.
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
	port->pnMode = 0;                        /* patCopy             */
	port->pnVis  = 0;                        /* visible (>=0)       */
	for (i = 0; i < 8; i++)
		port->pnPat.pat[i] = 0xFF;       /* solid               */
	port->fgColor = 255;
	port->bkColor = 0;
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
 * Fill a rectangle of the current port's pixmap with `color`, clipped to
 * `clip`. Used by the fill primitives (PaintRect / EraseRect through
 * qd_fill_rect, PaintOval's scanlines through qd_hline) and by the pen
 * primitives (qd_pen_plot / qd_pen_hline / qd_pen_vline) — sweeping the
 * pen along a line yields a thick band that's exactly a rectangle.
 */
static void qd_pixmap_fill(GrafPtr port, const Rect *clip,
                           const Rect *r, unsigned char color)
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
	for (row = clipped.top; row < clipped.bottom; row++) {
		unsigned char *p = (unsigned char *)pm->baseAddr
		                 + (row - pm->bounds.top) * pm->rowBytes
		                 + (clipped.left - pm->bounds.left);
		short col;

		for (col = 0; col < w; col++)
			p[col] = color;
	}
}

/*
 * Shared body of EraseRect / PaintRect — fill `r` (in the current port's
 * local coordinates) with `color`, clipped to the port's effective clip.
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
	qd_pixmap_fill(port, &clip, r, color);
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

static void qd_pen_plot(GrafPtr port, const Rect *clip,
                        short x, short y, unsigned char color)
{
	Rect pen;
	short sw = port->pnSize.h > 0 ? port->pnSize.h : 1;
	short sh = port->pnSize.v > 0 ? port->pnSize.v : 1;

	SetRect(&pen, x, y, (short)(x + sw), (short)(y + sh));
	qd_pixmap_fill(port, clip, &pen, color);
}

static void qd_pen_hline(GrafPtr port, const Rect *clip,
                         short x1, short x2, short y, unsigned char color)
{
	Rect band;
	short sw = port->pnSize.h > 0 ? port->pnSize.h : 1;
	short sh = port->pnSize.v > 0 ? port->pnSize.v : 1;

	if (x1 > x2) { short t = x1; x1 = x2; x2 = t; }
	SetRect(&band, x1, y, (short)(x2 + sw), (short)(y + sh));
	qd_pixmap_fill(port, clip, &band, color);
}

static void qd_pen_vline(GrafPtr port, const Rect *clip,
                         short x, short y1, short y2, unsigned char color)
{
	Rect band;
	short sw = port->pnSize.h > 0 ? port->pnSize.h : 1;
	short sh = port->pnSize.v > 0 ? port->pnSize.v : 1;

	if (y1 > y2) { short t = y1; y1 = y2; y2 = t; }
	SetRect(&band, x, y1, (short)(x + sw), (short)(y2 + sh));
	qd_pixmap_fill(port, clip, &band, color);
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
	unsigned char color;
	short l, t, rr, bb;

	GetPort(&port);
	if (port == NULL || r == NULL || EmptyRect(r))
		return;
	if (!qd_effective_clip(port, &clip))
		return;
	color = (unsigned char)((CGrafPtr)port)->fgColor;
	l  = r->left;
	t  = r->top;
	rr = (short)(r->right  - 1);
	bb = (short)(r->bottom - 1);

	qd_pen_hline(port, &clip, l, rr, t,  color); /* top    */
	qd_pen_hline(port, &clip, l, rr, bb, color); /* bottom */
	qd_pen_vline(port, &clip, l,  t,  bb, color);/* left   */
	qd_pen_vline(port, &clip, rr, t,  bb, color);/* right  */
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
	GrafPtr       port;
	Rect          clip;
	Boolean       draw;
	short         x0, y0, x1, y1;
	short         dx, dy, sx, sy, err, e2;
	unsigned char color;

	GetPort(&port);
	if (port == NULL)
		return;
	draw = qd_effective_clip(port, &clip);

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
		if (draw)
			qd_pen_plot(port, &clip, x0, y0, color);
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
			qd_pen_plot(port, &clip, xleft, y, color);
			if (xright != xleft)
				qd_pen_plot(port, &clip, xright, y, color);
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
			qd_pen_plot(port, &clip, x, ytop, color);
			if (ybottom != ytop)
				qd_pen_plot(port, &clip, x, ybottom, color);
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
