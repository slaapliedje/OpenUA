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
 * GrafPort and the Color QuickDraw types (CGrafPort, PixMap, ColorTable,
 * ...), NewPixMap, rectangular regions, and the current-port machinery
 * (GetPort / SetPort). The drawing calls (MoveTo/LineTo, Pen*, PaintRect,
 * CopyBits, ...) arrive with the display HAL backend — see
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

/*
 * Region — the shim's regions are rectangular: a region is its bounding
 * box. A Macintosh Region can carry an arbitrary shape after rgnBBox; the
 * shim does not, so rgnSize is always 10.
 */
typedef struct Region {
	short rgnSize;                  /* 10 — a rectangular region */
	Rect  rgnBBox;                  /* the bounding box          */
} Region;

typedef Region **RgnHandle;

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

/* --- Color QuickDraw --- */

typedef struct {
	unsigned short red;             /* 0..65535 per channel */
	unsigned short green;
	unsigned short blue;
} RGBColor;                             /* = 6 bytes */

typedef struct {
	short    value;                 /* a colour-table index */
	RGBColor rgb;                   /* the colour           */
} ColorSpec;                            /* = 8 bytes */

typedef struct {
	long      ctSeed;               /* identifies the table  */
	short     ctFlags;
	short     ctSize;               /* entry count, less one */
	ColorSpec ctTable[1];           /* ctSize + 1 entries    */
} ColorTable;

typedef ColorTable **CTabHandle;

/*
 * PixMap — a colour pixel image, the exact 50-byte Macintosh layout.
 * Trailing-comment numbers are field offsets.
 */
typedef struct {
	Ptr        baseAddr;            /* 0  pixel storage         */
	short      rowBytes;            /* 4  bytes per row         */
	Rect       bounds;              /* 6  coordinate system     */
	short      pmVersion;           /* 14 PixMap version        */
	short      packType;            /* 16 packing format        */
	long       packSize;            /* 18 packed size           */
	Fixed      hRes;                /* 22 horizontal resolution */
	Fixed      vRes;                /* 26 vertical resolution   */
	short      pixelType;           /* 30 pixel format          */
	short      pixelSize;           /* 32 bits per pixel        */
	short      cmpCount;            /* 34 components per pixel  */
	short      cmpSize;             /* 36 bits per component    */
	long       planeBytes;          /* 38 offset between planes */
	CTabHandle pmTable;             /* 42 the colour table      */
	long       pmReserved;          /* 46 (reserved)            */
} PixMap;                               /* = 50 bytes */

typedef PixMap **PixMapHandle;

/* PixPat — a colour pattern, the exact 28-byte Macintosh layout. */
typedef struct {
	short        patType;           /* 0  pattern type           */
	PixMapHandle patMap;            /* 2  the pattern's pixels    */
	Handle       patData;           /* 6  pixel data              */
	Handle       patXData;          /* 10 expanded data           */
	short        patXValid;         /* 14 expanded-data validity  */
	Handle       patXMap;           /* 16 expanded map            */
	Pattern      pat1Data;          /* 20 the one-bit pattern     */
} PixPat;                               /* = 28 bytes */

typedef PixPat **PixPatHandle;

/*
 * CGrafPort — the colour drawing environment, the exact 108-byte Macintosh
 * layout. Same size as GrafPort, deliberately: a window's 108-byte port slot
 * holds either, told apart by the high bits of portVersion. The fields it
 * shares with GrafPort sit at the same offsets — portRect, visRgn, clipRgn,
 * and pnLoc onward.
 */
typedef struct CGrafPort {
	short        device;            /* 0   output device        */
	PixMapHandle portPixMap;        /* 2   the pixel map         */
	short        portVersion;       /* 6   colour-port marker    */
	Handle       grafVars;          /* 8   colour port variables */
	short        chExtra;           /* 12  extra char width      */
	short        pnLocHFrac;        /* 14  pen fractional pos.   */
	Rect         portRect;          /* 16  the port rectangle    */
	RgnHandle    visRgn;            /* 24  visible region        */
	RgnHandle    clipRgn;           /* 28  clipping region       */
	PixPatHandle bkPixPat;          /* 32  background pattern    */
	RGBColor     rgbFgColor;        /* 36  foreground colour     */
	RGBColor     rgbBkColor;        /* 42  background colour     */
	Point        pnLoc;             /* 48  pen location          */
	Point        pnSize;            /* 52  pen size              */
	short        pnMode;            /* 56  pen transfer mode     */
	PixPatHandle pnPixPat;          /* 58  pen pattern           */
	PixPatHandle fillPixPat;        /* 62  fill pattern          */
	short        pnVis;             /* 66  pen visibility        */
	short        txFont;            /* 68  text font             */
	Style        txFace;            /* 70  text style            */
	char         _pad71;            /* 71  alignment fill        */
	short        txMode;            /* 72  text transfer mode    */
	short        txSize;            /* 74  text size             */
	Fixed        spExtra;           /* 76  extra inter-space     */
	long         fgColor;           /* 80  (compatibility)       */
	long         bkColor;           /* 84  (compatibility)       */
	short        colrBit;           /* 88  colour plane          */
	short        patStretch;        /* 90  pattern stretch       */
	Handle       picSave;           /* 92  picture in progress   */
	Handle       rgnSave;           /* 96  region in progress    */
	Handle       polySave;          /* 100 polygon in progress   */
	Handle       grafProcs;         /* 104 low-level overrides   */
} CGrafPort;                            /* = 108 bytes */

typedef CGrafPort *CGrafPtr;

/*
 * NewPixMap allocates and initialises a PixMap; DisposePixMap frees it and
 * its colour table. Both the PixMap and the table are relocatable blocks —
 * real Handles.
 */
PixMapHandle NewPixMap(void);
void         DisposePixMap(PixMapHandle pm);

/* --- regions (rectangular) --- */
RgnHandle NewRgn(void);                 /* allocate an empty region   */
void      DisposeRgn(RgnHandle rgn);
void      SetEmptyRgn(RgnHandle rgn);   /* make a region empty        */
void      RectRgn(RgnHandle rgn, const Rect *r);   /* set it to `r`   */
Boolean   EmptyRgn(RgnHandle rgn);      /* is the region empty?       */

/* The current port — QuickDraw draws into whichever port is current. */
void GetPort(GrafPtr *port);
void SetPort(GrafPtr port);

#endif /* COMPAT_QUICKDRAW_H */
