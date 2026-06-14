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
 * ...), NewPixMap, rectangular regions, the current-port machinery
 * (GetPort / SetPort), the screen port that owns the display back buffer
 * (qd_attach_screen), the rect primitives (EraseRect, PaintRect,
 * FrameRect), the line family (MoveTo, LineTo, GetPen, PenSize, PenMode,
 * PenPat), the ovals (PaintOval, FrameOval), the blit (CopyBits —
 * same-size, srcCopy mode, 8-bit), ClipRect, the colour entries
 * (RGBForeColor / RGBBackColor with a cached palette), and text drawing
 * (TextFont / TextSize / TextFace / TextMode state, DrawChar / DrawString
 * / CharWidth / StringWidth over a sparse 8x8 fallback font in
 * compat/font_8x8.c) — every primitive clips against portRect ∩ visRgn ∩
 * clipRgn, pen-using primitives honour pnSize, the pen pat modes (patCopy
 * / patOr / patXor / patBic) combine fgColor (and at patCopy, bkColor)
 * with the destination pixel, the 8x8 pen pattern gates each pen pixel,
 * RGBForeColor / RGBBackColor resolve to the nearest-distance palette
 * index, and text honours srcCopy / srcOr through the per-glyph bitmap.
 * Real NFNT fonts, scaling, and the rest of the source transfer modes
 * follow — see docs/decompilation.md, the Display subsystem.
 */

#ifndef COMPAT_QUICKDRAW_H
#define COMPAT_QUICKDRAW_H

#include "macmemory.h"          /* Ptr, Handle */

typedef unsigned char Boolean;          /* a Mac Boolean is one byte */

typedef const unsigned char *ConstStr255Param;  /* a Mac Pascal string */

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

/* --- Cursor Manager ---
 *
 * A Mac Cursor is a 16x16 image + mask + hotspot. The shim keeps one
 * "current" cursor and a visibility level; the present path composites it
 * onto the screen at the live mouse position (platform IKBD), picking the
 * nearest black/white CLUT entries. FRUA uses the system arrow (InitCursor)
 * and the watch (GetCursor(4)) — both supplied here as generic shapes.
 */
typedef struct {
	unsigned short data[16];        /* 1 = black                     */
	unsigned short mask[16];        /* 1 = opaque (else see-through) */
	Point          hotSpot;         /* the click point               */
} Cursor;
typedef Cursor  *CursPtr;
typedef Cursor **CursHandle;

void       InitCursor(void);            /* set the arrow, make visible       */
void       SetCursor(const Cursor *c);  /* install a cursor                  */
void       HideCursor(void);            /* nest-hide (level--)               */
void       ShowCursor(void);            /* nest-show (level++, max visible)  */
void       ObscureCursor(void);         /* hide until the mouse next moves   */
CursHandle GetCursor(short cursorID);   /* standard IDs: 4 = watch, else arrow */
const Cursor *qd_sword_cursor(void);    /* the FRUA engine sword (placeholder) */

/*
 * Install a colour pointer from the user's own art (the DOS FRUA cursors —
 * the one thing the DOS release has over the Mac's, which is mono). `idx` is
 * width*height 8-bit palette indices (0xFF = transparent); `pal_rgb` is 16
 * RGB triples (the library palette). Each index is mapped to the nearest
 * live CLUT entry now, so call this AFTER the palette is installed. Once set,
 * it overrides the mono cursor while visible. 16x16 only (cursors are 16x16).
 * Nothing FRUA is compiled in — the bytes come from a runtime-loaded pack.
 */
void qd_install_color_pointer(short w, short h, short hotx, short hoty,
                              const unsigned char *idx,
                              const unsigned char *pal_rgb);

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

/* A CGrafPort is told from a GrafPort by the high two bits of portVersion. */
#define CGRAFPORT_FLAG  0xC000

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

/*
 * Bind QuickDraw to a back buffer — create the screen GrafPort over the
 * given 8-bit paletted pixel storage and make it the current port. Called
 * once at startup with the display HAL's surface, before any drawing. The
 * pixels must live until the program exits.
 */
void qd_attach_screen(void *pixels, short rowBytes, short width, short height);

/*
 * Return the screen GrafPort qd_attach_screen set up, or NULL if no screen
 * is bound yet. The Window Manager paints window frames into this port —
 * the structure (frame + title bar) lives outside any window's own port,
 * on the desktop.
 */
GrafPtr qd_screen_port(void);

/* Direct access to the attached screen back buffer (raw 8-bit pixels),
 * for engine code that paints cells without the GrafPort primitives.
 * Returns 0 if no screen is attached. */
int  qd_screen_pixels(unsigned char **pixels, short *rowBytes,
                      short *width, short *height);

/*
 * Present hook — called by long-running window-tracking loops (DragWindow,
 * TrackGoAway) so the platform layer can flush the back buffer to the
 * visible screen and pump any per-frame state. NULL is a no-op. Set once
 * at startup; main wires it to the display HAL's present() entry.
 */
typedef void (*qd_present_fn)(void);
void          qd_set_present(qd_present_fn fn);
void          qd_present(void);              /* call the registered hook */

/* Present only a dirty rect, when the backend supports it (else falls
 * back to a full present). Lets the dungeon view skip converting the
 * static parts of the screen each frame. */
typedef void (*qd_present_rect_fn)(short x, short y, short w, short h);
void          qd_set_present_rect(qd_present_rect_fn fn);
void          qd_present_rect(short x, short y, short w, short h);

/*
 * Cursor tracking — keep the on-screen cursor following the live IKBD
 * mouse during engine idle/event-wait spins. The engine only repaints
 * (and thus only composites the cursor) on a real qd_present, so between
 * frames the cursor would freeze while the mouse keeps moving. The event
 * pump (GetNextEvent's idle path) calls this every spin; it presents only
 * when the mouse has actually moved since the last refresh, so a still
 * mouse costs nothing and a moving one tracks at the present/VBL rate
 * instead of waiting for the next engine-driven frame.
 */
void          qd_cursor_refresh(void);

/*
 * Initialise the drawing defaults the Mac sets in OpenPort — pnSize (1,1),
 * patCopy mode, solid pen pattern, fgColor 255, bkColor 0. qd_attach_screen
 * and the Window Manager's NewWindow / NewCWindow both call this.
 */
void qd_init_port_defaults(GrafPtr port);

/* --- drawing primitives ---
 *
 * Drawing acts on the current port. For colour ports the foreground and
 * background colours are taken from fgColor / bkColor as literal 8-bit
 * palette indices (the palette manager will refine this when it arrives).
 * Each primitive clips against the port's effective clip — portRect ∩ the
 * visRgn and clipRgn bounding boxes, both rectangular in the shim — so
 * ClipRect (the application clip) and BeginUpdate's narrowed visRgn (the
 * window-update clip) both restrict drawing as on the Mac.
 */
void EraseRect(const Rect *r);  /* fill r with the port's background */
void PaintRect(const Rect *r);  /* fill r with the port's foreground */
void FrameRect(const Rect *r);  /* outline r in the port's foreground */
void PaintOval(const Rect *r);  /* fill r's inscribed oval, fg */
void FrameOval(const Rect *r);  /* outline r's inscribed oval, fg */
void ClipRect(const Rect *r);   /* set the current port's clipRgn to r */

/* --- line drawing and the pen ---
 *
 * The pen state lives in the current port — pnLoc, pnSize, pnMode, pnPat.
 * MoveTo and LineTo move the pen; LineTo also draws from the old pnLoc to
 * (h, v). PenSize honours its setting: each pen step plots a pnSize.h x
 * pnSize.v rect, so LineTo, FrameRect, and FrameOval draw thick strokes
 * when the pen is enlarged. PenMode picks the pat-mode combine (patCopy
 * overwrites, patOr / patXor / patBic apply the bitwise op of fgColor
 * over the existing pixel). PenPat sets the 8x8 one-bit pattern: at each
 * destination pixel the bit at (x & 7, y & 7) gates the pen — set bits
 * lay down the pen-mode op, clear bits paint bkColor for patCopy and
 * leave the pixel untouched for the bitwise modes.
 */
void MoveTo(short h, short v);
void LineTo(short h, short v);
void GetPen(Point *pt);
void PenSize(short h, short v);  /* set the pen rectangle dimensions */
void PenMode(short mode);        /* set the pen transfer mode (patCopy etc) */
void PenPat(const Pattern *pat); /* set the 8x8 pen pattern (copies bytes) */

/* --- pen transfer modes (the pat- family) ---
 *
 * Applied by LineTo / FrameRect / FrameOval where the pen lays down pixels.
 * At 8 bpp the ops act bitwise on the pixel byte: patOr ORs fgColor into the
 * pixel, patXor XORs it (so XORing fgColor twice restores the original —
 * the rubber-band-rectangle idiom), patBic clears the bits of fgColor in
 * the pixel. patCopy overwrites with fgColor and is the OpenPort default.
 */
#define patCopy  8
#define patOr    9
#define patXor   10
#define patBic   11

/* --- CopyBits transfer modes (subset) --- */
#define srcCopy  0      /* dst = src (the default and most common mode) */
#define srcOr    1
#define srcXor   2
#define srcBic   3

/* --- colour ---
 *
 * RGBForeColor / RGBBackColor record the 16-bit-per-channel RGB into the
 * port's rgbFgColor / rgbBkColor and resolve a nearest-match palette index
 * into fgColor / bkColor, against the shim's cached palette. The drawing
 * primitives still use the 8-bit index — so a call to RGBForeColor takes
 * effect immediately on the next paint.
 *
 * qd_set_palette caches the 256-entry CLUT inside the shim *and* forwards
 * to the display HAL — call it instead of dsp->set_palette so the inverse
 * lookup stays in sync with what the hardware is showing. This entry is
 * scaffolding for the lift-in-progress; the engine will install palettes
 * through the Palette Manager once that arrives.
 */
void RGBForeColor(const RGBColor *color);
void RGBBackColor(const RGBColor *color);
void qd_set_palette(const RGBColor *colors, short first, short count);
void qd_dump_palette(unsigned char *out768);

/* --- text drawing ---
 *
 * The text-state setters write into the port's txFont / txFace / txMode /
 * txSize fields; for now the rendering ignores font / face / size and
 * always uses the embedded 8x8 bitmap in compat/font_8x8.c (a sparse
 * scaffolding font with explicit glyphs for the lift demo and a hollow-
 * box fallback for every other code point). Real NFNT fonts arrive with
 * the Resource Manager; the API surface stands while that lands.
 *
 * DrawChar / DrawString render with pnLoc as the baseline (one descender
 * row below the glyph body), advance the pen by the char's width, and
 * honour txMode for the source-mode subset: srcCopy paints fgColor at
 * glyph bits and bkColor elsewhere, srcOr paints fgColor at glyph bits
 * and leaves the destination alone elsewhere (the QuickDraw default for
 * anti-aliased-feel text on a coloured background). Other modes fall
 * through to srcOr.
 *
 * CharWidth / StringWidth report the advance width — fixed at the 8x8
 * font's cell width while the embedded font is the only one available.
 *
 * DrawString and StringWidth take Pascal strings: str[0] is the length,
 * str[1..len] are the bytes.
 */
void  TextFont(short font);
void  TextFace(short face);
void  TextMode(short mode);
void  TextSize(short size);
void  DrawChar(short ch);
void  DrawString(ConstStr255Param str);
short CharWidth(short ch);
short StringWidth(ConstStr255Param str);

/*
 * CopyBits — blit pixels from srcBits to dstBits.
 *
 * The Mac BitMap layout's first three fields (baseAddr, rowBytes, bounds)
 * sit at the same offsets as the PixMap's, so a colour-port caller passes
 * `(BitMap *)*cport->portPixMap` and one entry point serves both depths.
 *
 * First cut: 8-bit PixMap to 8-bit PixMap, srcRect and dstRect the same
 * size (no scaling), srcCopy mode only, no mask region honoured. Clipped
 * to both bitmaps' bounds. Scaling, the other transfer modes, mask
 * regions, and the visRgn / clipRgn intersection follow.
 */
void CopyBits(const BitMap *srcBits, const BitMap *dstBits,
              const Rect *srcRect, const Rect *dstRect,
              short mode, RgnHandle maskRgn);

#endif /* COMPAT_QUICKDRAW_H */
