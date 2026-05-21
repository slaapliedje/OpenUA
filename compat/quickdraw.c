/*
 * Mac QuickDraw shim — geometry core and the current-port machinery
 * (ADR-0003).
 *
 * The Point and rectangle calculations, implemented to the Inside Macintosh
 * semantics, and GetPort / SetPort. These are pure — no drawing target — so
 * they stand alone ahead of the display HAL.
 */

#include <stddef.h>             /* offsetof */

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
