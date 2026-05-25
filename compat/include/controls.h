/*
 * Mac Control Manager shim (ADR-0003, ADR-0006). Skeleton.
 *
 * Per ADR-0006 the editor and play-UI widgets are reimplemented inside the
 * shim. FRUA's actual control needs are met through DITL items (compat/
 * dialogs.c paints buttons inside the modal Dialog Manager); this module
 * stands the canonical Mac API beside that so engine code that calls
 * NewControl / TrackControl / SetControlValue / DrawControls works
 * unchanged.
 *
 * Here so far: the ControlRecord at the Mac layout, NewControl /
 * GetNewControl (CNTL resource) / DisposeControl / KillControls
 * (drains a window's control list), the visibility / hilite / title /
 * value / min-max accessors, Draw1Control / DrawControls / UpdateControls
 * for the three primary CDEFs (pushButProc, checkBoxProc, radioButProc),
 * TestControl + FindControl hit-testing, and TrackControl with the Mac's
 * "hilite while pressed, hilite tracks the mouse going in / out" contract
 * (the indicator part of a scroll bar will follow). Scroll bars, the
 * full CDEF dispatch, control colour, and the dialog-item integration
 * (DITL type 4 → ControlHandle) are follow-ons.
 */

#ifndef COMPAT_CONTROLS_H
#define COMPAT_CONTROLS_H

#include "macmemory.h"          /* Handle */
#include "quickdraw.h"          /* Rect, Boolean, ConstStr255Param, Point */
#include "windows.h"            /* WindowPtr */

/*
 * ControlRecord — the Mac 44-byte control record. Trailing-comment
 * numbers are Mac field offsets. contrlNext + contrlOwner give the
 * Window Manager's linked-list-per-window the Window Manager's
 * controlList field hangs off; the head of that list is the
 * frontmost control (last drawn → first hit-tested).
 */
/* Forward-declared: contrlNext is a ControlHandle (Inside Macintosh's
 * Control Manager convention — distinct from the Window Manager, which
 * threads its list via master pointers). */
struct ControlRecord;
typedef struct ControlRecord **ControlHandle_t;

typedef struct ControlRecord {
	ControlHandle_t       contrlNext;        /* 0  next in owner's list   */
	WindowPtr             contrlOwner;       /* 4  owning window          */
	Rect                  contrlRect;        /* 8  position (window-local)*/
	unsigned char         contrlVis;         /* 16 visibility (0 / 255)   */
	unsigned char         contrlHilite;      /* 17 hilite (0/1/255)       */
	short                 contrlValue;       /* 18 current value          */
	short                 contrlMin;         /* 20 minimum                */
	short                 contrlMax;         /* 22 maximum                */
	Handle                contrlDefProc;     /* 24 CDEF (unused in shim)  */
	Handle                contrlData;        /* 28 control-specific data  */
	long                  contrlAction;      /* 32 action proc (unused)   */
	long                  contrlRfCon;       /* 36 app-defined refCon     */
	Handle                contrlTitle;       /* 40 Pascal-string title    */
} ControlRecord;

typedef ControlHandle_t ControlHandle;
typedef ControlHandle   ControlRef;

/* CDEF / procID — high nibble holds the variation code; the low byte
 * picks the CDEF resource id (the shim recognises the three primary
 * ones explicitly). */
#define pushButProc      0
#define checkBoxProc     1
#define radioButProc     2
#define useWFont         8       /* OR'd in — use the window's text font */
#define scrollBarProc    16

/* Part codes returned by TestControl / FindControl / TrackControl. */
#define inButton         10
#define inCheckBox       11
#define inUpButton       20
#define inDownButton     21
#define inPageUp         22
#define inPageDown       23
#define inThumb          129

/* Hilite states (also the contrlHilite field). */
#define noConstraint     0
#define inactiveHilite   255

/* --- creation --- */

/*
 * Create a control on `owner` at `boundsRect` with the given Pascal
 * title, initial value, min / max range, procID (CDEF), visibility,
 * and refCon. Returns the ControlHandle, or NULL on allocation failure.
 * The control is linked into `owner`'s control list and drawn if
 * visible.
 */
ControlHandle NewControl(WindowPtr owner, const Rect *boundsRect,
                         ConstStr255Param title, Boolean visible,
                         short value, short min, short max, short procID,
                         long refCon);

/* Load a control from CNTL resource `controlID` attached to `owner`. */
ControlHandle GetNewControl(short controlID, WindowPtr owner);

/* Free a single control (unlinks it from its owner's list). */
void DisposeControl(ControlHandle c);

/* Free every control on `owner` (called by DisposeWindow). */
void KillControls(WindowPtr owner);

/* --- visibility / state --- */

void ShowControl(ControlHandle c);
void HideControl(ControlHandle c);
void HiliteControl(ControlHandle c, short hiliteState);

/* --- value / range --- */

void  SetControlValue(ControlHandle c, short value);
short GetControlValue(ControlHandle c);
void  SetControlMinimum(ControlHandle c, short min);
short GetControlMinimum(ControlHandle c);
void  SetControlMaximum(ControlHandle c, short max);
short GetControlMaximum(ControlHandle c);

/* --- title --- */
void SetControlTitle(ControlHandle c, ConstStr255Param title);
void GetControlTitle(ControlHandle c, unsigned char *title);

/* --- refCon --- */
void SetControlReference(ControlHandle c, long refCon);
long GetControlReference(ControlHandle c);

/* --- drawing --- */
void Draw1Control(ControlHandle c);
void DrawControls(WindowPtr w);
void UpdateControls(WindowPtr w, RgnHandle updateRgn);

/* --- hit-testing --- */

/* Part code for `pt` (window-local) against `c`; 0 if outside. */
short TestControl(ControlHandle c, Point pt);

/*
 * Walk `w`'s control list at point `pt` (window-local); on hit, sets
 * *theControl and returns the part code. Returns 0 with *theControl =
 * NULL when no control contains the point.
 */
short FindControl(Point pt, WindowPtr w, ControlHandle *theControl);

/*
 * Track the mouse inside `c` starting at `startPt` (window-local) until
 * Button() releases. The control hilites while the mouse is inside its
 * rect and unhilites when it leaves; on release the hilite is cleared
 * and the function returns the part code at release (or 0 if outside).
 * `actionProc` is accepted for API parity and currently ignored —
 * scroll-bar continuous-action arrives with the scroll-bar CDEF.
 */
short TrackControl(ControlHandle c, Point startPt, void *actionProc);

#endif /* COMPAT_CONTROLS_H */
