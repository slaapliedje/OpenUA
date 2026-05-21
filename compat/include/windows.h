/*
 * Mac Window Manager shim — window records and the window list (ADR-0003).
 *
 * Per ADR-0006 the editor and play UI are reimplemented inside the shim
 * rather than mapped to AES; this is the Window Manager piece. A window is
 * a GrafPort (quickdraw.h) with the window-specific fields appended — the
 * exact Macintosh WindowRecord layout, so the engine's by-offset accesses
 * port unchanged.
 *
 * Here so far: the window record, the window list, the window lifecycle and
 * geometry (NewWindow, DisposeWindow, ShowWindow, HideWindow, SelectWindow,
 * SizeWindow, MoveWindow, FrontWindow), colour windows (NewCWindow),
 * resource-loaded windows (GetNewWindow / GetNewCWindow), and the update
 * mechanism (InvalRect / BeginUpdate / EndUpdate) over rectangular regions.
 * Window drawing (the frame, the title bar) follows with the display HAL —
 * see docs/toolbox-mapping.md.
 */

#ifndef COMPAT_WINDOWS_H
#define COMPAT_WINDOWS_H

#include "quickdraw.h"          /* GrafPort, GrafPtr, Rect, Boolean */

typedef const unsigned char *ConstStr255Param;  /* a Mac Pascal string */

#define userKind        8       /* windowKind: an application window */

/*
 * WindowRecord — a window. The port comes first, so a WindowPtr (which
 * points at the port) and a WindowPeek (the whole record) interconvert by a
 * cast. The exact 156-byte Macintosh layout; trailing numbers are offsets.
 */
typedef struct WindowRecord {
	GrafPort   port;                /* 0   the window's GrafPort       */
	short      windowKind;          /* 108 window class                */
	Boolean    visible;             /* 110 currently shown?            */
	Boolean    hilited;             /* 111 highlighted (frontmost)?    */
	Boolean    goAwayFlag;          /* 112 has a close box?            */
	Boolean    spareFlag;           /* 113 (reserved)                  */
	RgnHandle  strucRgn;            /* 114 structure region            */
	RgnHandle  contRgn;             /* 118 content region              */
	RgnHandle  updateRgn;           /* 122 update region               */
	Handle     windowDefProc;       /* 126 window definition (WDEF)    */
	Handle     dataHandle;          /* 130 WDEF private data           */
	Handle     titleHandle;         /* 134 title string                */
	short      titleWidth;          /* 138 title width in pixels       */
	Handle     controlList;         /* 140 the window's controls       */
	struct WindowRecord *nextWindow;/* 144 next window, front to back  */
	Handle     windowPic;           /* 148 cached content picture      */
	long       refCon;              /* 152 application-defined         */
} WindowRecord;

typedef GrafPtr        WindowPtr;       /* points at the port  */
typedef WindowRecord  *WindowPeek;      /* the whole record    */

/*
 * Create a window. `wStorage` is caller-supplied WindowRecord memory, or
 * NULL to allocate one. `behind` places it in the window list: (WindowPtr)-1
 * frontmost, NULL backmost, otherwise just behind that window.
 */
WindowPtr NewWindow(void *wStorage, const Rect *boundsRect,
                    ConstStr255Param title, Boolean visible, short procID,
                    WindowPtr behind, Boolean goAwayFlag, long refCon);

/* As NewWindow, but the window's port slot is a colour CGrafPort. */
WindowPtr NewCWindow(void *wStorage, const Rect *boundsRect,
                     ConstStr255Param title, Boolean visible, short procID,
                     WindowPtr behind, Boolean goAwayFlag, long refCon);

/* Create a window from the 'WIND' resource with id `windowID`. */
WindowPtr GetNewWindow(short windowID, void *wStorage, WindowPtr behind);

/* As GetNewWindow, but the window's port slot is a colour CGrafPort. */
WindowPtr GetNewCWindow(short windowID, void *wStorage, WindowPtr behind);

void      DisposeWindow(WindowPtr w);   /* unlink and free            */
void      ShowWindow(WindowPtr w);      /* make visible               */
void      HideWindow(WindowPtr w);      /* make invisible             */
void      SelectWindow(WindowPtr w);    /* bring to the front, hilite */
void      SizeWindow(WindowPtr w, short width, short height,
                     Boolean fUpdate);  /* resize the content         */
void      MoveWindow(WindowPtr w, short h, short v, Boolean front);
WindowPtr FrontWindow(void);            /* the frontmost visible window */

/* The update mechanism — mark areas for redraw, then handle the redraw. */
void InvalRect(const Rect *r);          /* add r to the update region   */
void BeginUpdate(WindowPtr w);          /* begin handling an update     */
void EndUpdate(WindowPtr w);            /* finish it                    */

#endif /* COMPAT_WINDOWS_H */
