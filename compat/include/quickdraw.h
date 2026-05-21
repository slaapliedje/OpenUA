/*
 * Mac QuickDraw shim — core types, geometry, and the GrafPort.
 *
 * Part of the compat/ Toolbox shim (ADR-0003): the decompiled engine draws
 * with QuickDraw, and this provides that API. Struct layouts match the
 * Macintosh exactly — both the Mac and the Atari target are big-endian 68k
 * with 16-bit alignment — so the engine's by-offset struct accesses port
 * unchanged.
 *
 * Here so far: the geometry core (Point, Rect, the rect utilities), the
 * GrafPort, and the current-port machinery (GetPort / SetPort). The GrafPort
 * is brought in now because the Window Manager shim is built on it. The
 * drawing calls (MoveTo/LineTo, Pen*, PaintRect, CopyBits, ...) and colour
 * (CGrafPort, PixMap) arrive with the display HAL backend — see
 * docs/decompilation.md, the Display subsystem.
 */

#ifndef COMPAT_QUICKDRAW_H
#define COMPAT_QUICKDRAW_H

#include "macmemory.h"          /* Ptr, Handle */

typedef unsigned char Boolean;          /* a Mac Boolean is one byte */

typedef struct {
	short v;                        /* vertical   (Mac order: v before h) */
	short h;                        /* horizontal                         */
} Point;

typedef struct {
	short top;
	short left;
	short bottom;
	short right;
} Rect;

void    SetPt(Point *pt, short h, short v);
void    SetRect(Rect *r, short left, short top, short right, short bottom);
void    OffsetRect(Rect *r, short dh, short dv);
void    InsetRect(Rect *r, short dh, short dv);
Boolean SectRect(const Rect *src1, const Rect *src2, Rect *dst);
void    UnionRect(const Rect *src1, const Rect *src2, Rect *dst);
Boolean EqualRect(const Rect *r1, const Rect *r2);
Boolean EmptyRect(const Rect *r);
Boolean PtInRect(Point pt, const Rect *r);
void    Pt2Rect(Point pt1, Point pt2, Rect *dst);

/* --- the GrafPort --- */

typedef long          Fixed;            /* 16.16 fixed-point         */
typedef unsigned char Style;            /* a QuickDraw text-style set */
typedef Handle        RgnHandle;        /* a region — opaque for now  */

typedef struct {
	unsigned char pat[8];           /* an 8x8 one-bit pattern */
} Pattern;

typedef struct {
	Ptr   baseAddr;                 /* pixel storage     */
	short rowBytes;                 /* bytes per row     */
	Rect  bounds;                   /* coordinate system */
} BitMap;

/*
 * GrafPort — a QuickDraw drawing environment, the exact 108-byte Macintosh
 * layout (the engine reads these fields by offset; compat/quickdraw.c
 * asserts the size). Trailing-comment numbers are field offsets.
 */
typedef struct GrafPort {
	short     device;               /* 0   output device          */
	BitMap    portBits;             /* 2   the pixel map          */
	Rect      portRect;             /* 16  the port rectangle     */
	RgnHandle visRgn;               /* 24  visible region         */
	RgnHandle clipRgn;              /* 28  clipping region        */
	Pattern   bkPat;                /* 32  background pattern     */
	Pattern   fillPat;              /* 40  fill pattern           */
	Point     pnLoc;                /* 48  pen location           */
	Point     pnSize;               /* 52  pen size               */
	short     pnMode;               /* 56  pen transfer mode      */
	Pattern   pnPat;                /* 58  pen pattern            */
	short     pnVis;                /* 66  pen visibility         */
	short     txFont;               /* 68  text font              */
	Style     txFace;               /* 70  text style             */
	char      _pad71;               /* 71  alignment fill         */
	short     txMode;               /* 72  text transfer mode     */
	short     txSize;               /* 74  text size              */
	Fixed     spExtra;              /* 76  extra inter-space      */
	long      fgColor;              /* 80  foreground colour      */
	long      bkColor;              /* 84  background colour      */
	short     colrBit;              /* 88  colour plane           */
	short     patStretch;           /* 90  pattern stretch        */
	Handle    picSave;              /* 92  picture in progress    */
	Handle    rgnSave;              /* 96  region in progress     */
	Handle    polySave;             /* 100 polygon in progress    */
	Handle    grafProcs;            /* 104 low-level overrides    */
} GrafPort;                             /* = 108 bytes                */

typedef GrafPort *GrafPtr;

/* The current port — QuickDraw draws into whichever port is current. */
void GetPort(GrafPtr *port);
void SetPort(GrafPtr port);

#endif /* COMPAT_QUICKDRAW_H */
