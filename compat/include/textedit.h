/*
 * Mac TextEdit manager shim (ADR-0003, ADR-0006). Skeleton.
 *
 * Per ADR-0006 the editor's text widgets are reimplemented in the shim,
 * drawn into the HAL surface. The Mac engine reaches for TextEdit via
 * the standard TENew / TEKey / TEClick / TEUpdate path; FRUA's editor
 * uses it for character-sheet name fields and the design-editor's
 * multi-line text entries.
 *
 * Here so far: the TERec at the Mac field offsets engine code reads
 * by-offset access for, TENew / TEDispose, TESetText / TEGetText
 * (round-trip through the hText handle), TEKey for caret insertion +
 * backspace + arrow movement, TEClick for caret placement by mouse,
 * TEActivate / TEDeactivate, TEUpdate that paints the visible text
 * with a vertical insertion caret on the active field, TESetSelect
 * for programmatic selection. Word wrap, multi-line scrolling, the
 * justification modes, the word-break / click-loop hooks, the cut /
 * copy / paste scrap, the style-record variant (TextEdit 1.2 styles),
 * and the caret-blink timer are all follow-ons.
 */

#ifndef COMPAT_TEXTEDIT_H
#define COMPAT_TEXTEDIT_H

#include "macmemory.h"          /* Handle, OSErr */
#include "quickdraw.h"          /* Rect, Point, Style, GrafPtr */

/*
 * TERec — the Mac TextEdit record. The shim sizes it to match the
 * classic Mac TextEdit 1.x record layout (~162 bytes); engine code
 * that pokes individual fields by offset ports unchanged. Trailing
 * comment numbers are Mac field offsets.
 */
typedef struct TERec {
	Rect      destRect;             /* 0   destination rectangle      */
	Rect      viewRect;             /* 8   clipping rectangle         */
	Rect      selRect;              /* 16  selection bounding box     */
	short     lineHeight;           /* 24  line height in pixels      */
	short     fontAscent;           /* 26  font ascent in pixels      */
	Point     selPoint;             /* 28  last click point           */
	short     selStart;             /* 32  selection start (char)     */
	short     selEnd;               /* 34  selection end (char)       */
	short     active;               /* 36  non-zero = active          */
	Handle    wordBreak;            /* 38  word-break ProcPtr         */
	Handle    clikLoop;             /* 42  click-loop ProcPtr         */
	long      clickTime;            /* 46  last click time            */
	short     clickLoc;             /* 50  last click char position   */
	long      caretTime;            /* 52  last caret blink time      */
	short     caretState;           /* 56  caret on / off             */
	short     just;                 /* 58  justification              */
	short     teLength;             /* 60  total chars in text        */
	Handle    hText;                /* 62  text storage handle        */
	short     recalBack;            /* 66  internal                   */
	short     recalLines;           /* 68  internal                   */
	short     clikStuff;            /* 70  internal click state       */
	short     crOnly;               /* 72  -1 = CR-only, no wrap      */
	short     txFont;               /* 74  text font                  */
	Style     txFace;               /* 76  text style                 */
	char      _pad77;               /* 77  alignment fill             */
	short     txMode;               /* 78  text transfer mode         */
	short     txSize;               /* 80  text size                  */
	GrafPtr   inPort;               /* 82  drawing port               */
	Handle    highHook;             /* 86  text-highlight ProcPtr     */
	Handle    caretHook;            /* 90  caret-draw ProcPtr         */
	short     nLines;               /* 94  number of lines            */
	short     lineStarts[16];       /* 96  first-char-per-line array  */
} TERec;

typedef TERec  *TEPtr;
typedef TERec **TEHandle;

/* Justification modes (the `just` field). */
#define teJustLeft        0
#define teJustCenter      1
#define teJustRight      -1

/* --- lifecycle --- */
TEHandle TENew(const Rect *destRect, const Rect *viewRect);
void     TEDispose(TEHandle hTE);

/* --- text content --- */
void   TESetText(const void *text, long length, TEHandle hTE);
Handle TEGetText(TEHandle hTE);

/* --- selection --- */
void TESetSelect(long selStart, long selEnd, TEHandle hTE);

/* --- editing --- */
void TEKey(short key, TEHandle hTE);

/* --- mouse + focus --- */
void TEClick(Point pt, Boolean extend, TEHandle hTE);
void TEActivate(TEHandle hTE);
void TEDeactivate(TEHandle hTE);

/* --- drawing --- */
void TEUpdate(const Rect *updateRect, TEHandle hTE);

/* --- idle (caret blink) --- */
void TEIdle(TEHandle hTE);

#endif /* COMPAT_TEXTEDIT_H */
