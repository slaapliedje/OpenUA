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
 * geometry (NewWindow, CloseWindow, DisposeWindow, ShowWindow, HideWindow,
 * SelectWindow, SizeWindow, MoveWindow, FrontWindow), colour windows
 * (NewCWindow), resource-loaded windows (GetNewWindow / GetNewCWindow), the
 * update mechanism (InvalRect / ValidRect / BeginUpdate / EndUpdate) over
 * rectangular regions, structure / content regions in global screen
 * coordinates, the title stored in a Handle, the desktop-side frame
 * drawing on ShowWindow / SelectWindow (a black 1-pixel outline, a grey
 * title bar with the title centred, a close box when goAwayFlag is set),
 * and the user-action plumbing (FindWindow over the window stack, the
 * close-box geometry, DragWindow with an XOR outline + MoveWindow on
 * release, TrackGoAway against the close box). DragWindow / TrackGoAway
 * spin on Button() / GetMouse() — they take their wakeup from the real
 * mouse once the IKBD-packet driver promotes the stubs.
 */

#ifndef COMPAT_WINDOWS_H
#define COMPAT_WINDOWS_H

#include "quickdraw.h"          /* GrafPort, GrafPtr, Rect, Boolean, ConstStr255Param */

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

void      CloseWindow(WindowPtr w);     /* unlink and dispose regions */
void      DisposeWindow(WindowPtr w);   /* CloseWindow + free record  */
void      ShowWindow(WindowPtr w);      /* make visible               */
void      HideWindow(WindowPtr w);      /* make invisible             */
void      SelectWindow(WindowPtr w);    /* bring to the front, hilite */
void      SizeWindow(WindowPtr w, short width, short height,
                     Boolean fUpdate);  /* resize the content         */
void      MoveWindow(WindowPtr w, short h, short v, Boolean front);
WindowPtr FrontWindow(void);            /* the frontmost visible window */

/* The update mechanism — mark areas for redraw, then handle the redraw. */
void InvalRect(const Rect *r);          /* add r to the update region   */
void ValidRect(const Rect *r);          /* remove r from the update rgn */
void BeginUpdate(WindowPtr w);          /* begin handling an update     */
void EndUpdate(WindowPtr w);            /* finish it                    */

/* --- FindWindow part codes --- */
#define inDesk       0          /* on the desktop, no window hit       */
#define inMenuBar    1          /* (no menu bar yet)                   */
#define inSysWindow  2          /* a system window (deferred)          */
#define inContent    3          /* the window's content area           */
#define inDrag       4          /* the title bar (draggable area)      */
#define inGrow       5          /* the grow box (deferred)             */
#define inGoAway     6          /* the close box                       */
#define inZoomIn     7          /* zoom box, zoomed-in state           */
#define inZoomOut    8          /* zoom box, zoomed-out state          */

/*
 * Identify what (if anything) sits at screen point thePt — sets *whichWindow
 * to the front-to-back hit, NULL if none, and returns the part code above.
 * Returns inDesk and *whichWindow = NULL when no window contains the point.
 */
short FindWindow(Point thePt, WindowPtr *whichWindow);

/*
 * Drag w by tracking the mouse from startPt until Button() goes false: an
 * XOR outline follows the mouse on the screen port, and the window is
 * moved by the final delta on release. The top-left of the content is
 * clamped into boundsRect. With Button() stubbed at 0 the loop never
 * iterates and the window stays put — the IKBD-packet driver wakes it.
 */
void DragWindow(WindowPtr w, Point startPt, const Rect *boundsRect);

/*
 * Track a click in w's close box from startPt: returns 1 if the button
 * was released with the pointer still inside the close box, 0 otherwise.
 * Returns 0 immediately when startPt isn't in the close box or the
 * window doesn't have a goAwayFlag.
 */
Boolean TrackGoAway(WindowPtr w, Point startPt);

#endif /* COMPAT_WINDOWS_H */
