/*
 * Mac QuickDraw shim — core types and rectangle utilities.
 *
 * Part of the compat/ Toolbox shim (ADR-0003): the decompiled engine draws
 * with QuickDraw, and this provides that API. Struct layouts match the
 * Macintosh exactly — both the Mac and the Atari target are big-endian 68k —
 * so the engine's by-offset struct accesses port unchanged.
 *
 * This first piece is the geometry core: Point, Rect, and the pure rect
 * utilities. GrafPort, the current-port machinery, and the drawing calls
 * (MoveTo/LineTo, Pen*, PaintRect, CopyBits, ...) arrive with the display
 * HAL backend — see docs/decompilation.md, the Display subsystem.
 */

#ifndef COMPAT_QUICKDRAW_H
#define COMPAT_QUICKDRAW_H

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

#endif /* COMPAT_QUICKDRAW_H */
