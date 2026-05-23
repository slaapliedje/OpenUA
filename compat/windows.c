/*
 * Mac Window Manager shim — see windows.h.
 *
 * First cut: the window data model, the window list, the lifecycle and
 * geometry, b&w / colour windows, resource-loaded windows (GetNewWindow /
 * GetNewCWindow), and the update mechanism (InvalRect / ValidRect /
 * BeginUpdate / EndUpdate) over rectangular regions. SizeWindow and
 * MoveWindow act on portRect; the Mac local/global coordinate split, the
 * structure/content regions, and the window frame and title-bar drawing
 * follow with the display HAL — an honest minimal start, the error.c
 * pattern.
 */

#include <stddef.h>             /* NULL, offsetof */
#include <string.h>             /* memset         */

#include "windows.h"
#include "macmemory.h"          /* NewPtr, DisposePtr */
#include "resources.h"          /* GetResource        */

/* The window list: head = frontmost; WindowRecord.nextWindow chains back. */
static WindowPeek g_window_list;

/* NewWindow / SelectWindow "bring to the front" sentinel for `behind`. */
#define WIN_FRONT  ((WindowPtr)-1L)

/* A CGrafPort is told from a GrafPort by the high two bits of portVersion. */
#define CGRAFPORT_FLAG  0xC000

/* Detach `w` from the window list if it is currently in it. */
static void win_unlink(WindowPeek w)
{
	WindowPeek *link = &g_window_list;

	while (*link != NULL && *link != w)
		link = &(*link)->nextWindow;
	if (*link == w)
		*link = w->nextWindow;
}

/* Insert `w` into the window list relative to `behind` (see NewWindow). */
static void win_link(WindowPeek w, WindowPtr behind)
{
	if (behind == WIN_FRONT || g_window_list == NULL) {
		w->nextWindow = g_window_list;
		g_window_list = w;
	} else if (behind == NULL) {
		WindowPeek tail = g_window_list;

		while (tail->nextWindow != NULL)
			tail = tail->nextWindow;
		tail->nextWindow = w;
		w->nextWindow = NULL;
	} else {
		WindowPeek b = (WindowPeek)behind;

		w->nextWindow = b->nextWindow;
		b->nextWindow = w;
	}
}

/* Set the window's visRgn to its full content — the rectangular-region
 * stand-in for "the window is entirely visible". */
static void win_reset_visrgn(WindowPeek w)
{
	if (w->port.visRgn != NULL)
		RectRgn(w->port.visRgn, &w->port.portRect);
}

/*
 * Shared body of NewWindow / NewCWindow: allocate or adopt the storage, set
 * the window up, and link it into the list. `isColor` selects whether the
 * 108-byte port slot is initialised as a GrafPort or a CGrafPort.
 */
static WindowPtr win_new(void *wStorage, const Rect *boundsRect,
                         Boolean visible, WindowPtr behind,
                         Boolean goAwayFlag, long refCon, Boolean isColor)
{
	WindowPeek w;

	w = wStorage ? (WindowPeek)wStorage
	             : (WindowPeek)NewPtr((Size)sizeof(WindowRecord));
	if (w == NULL)
		return NULL;
	memset(w, 0, sizeof *w);

	/* portRect (offset 16, shared by GrafPort and CGrafPort) is the content
	 * in the window's own local coordinates. */
	SetRect(&w->port.portRect, 0, 0,
	        (short)(boundsRect->right - boundsRect->left),
	        (short)(boundsRect->bottom - boundsRect->top));
	if (isColor) {
		/* Mark the 108-byte port slot as a CGrafPort and give it a colour
		 * pixel map sized to the content. The PixMap's pixel storage
		 * (baseAddr / rowBytes) is filled in with the display HAL. */
		CGrafPtr     cp = (CGrafPtr)&w->port;
		PixMapHandle pm = NewPixMap();

		cp->portVersion = (short)CGRAFPORT_FLAG;
		cp->portPixMap  = pm;
		if (pm != NULL)
			(*pm)->bounds = w->port.portRect;
	} else {
		w->port.portBits.bounds = *boundsRect;  /* b&w global placement */
	}

	/* The window's update region and its port's visible region — the
	 * substrate for the rectangular-region update mechanism. */
	w->updateRgn   = NewRgn();
	w->port.visRgn = NewRgn();
	win_reset_visrgn(w);

	w->windowKind = userKind;
	w->goAwayFlag = goAwayFlag;
	w->refCon     = refCon;

	win_link(w, behind);
	SetPort((WindowPtr)w);
	if (visible)
		ShowWindow((WindowPtr)w);
	return (WindowPtr)w;
}

WindowPtr NewWindow(void *wStorage, const Rect *boundsRect,
                    ConstStr255Param title, Boolean visible, short procID,
                    WindowPtr behind, Boolean goAwayFlag, long refCon)
{
	(void)title;            /* title storage awaits the Handle-based Memory
	                         * Manager and the title-bar drawing */
	(void)procID;           /* the window definition (WDEF) is deferred */
	return win_new(wStorage, boundsRect, visible, behind, goAwayFlag,
	               refCon, 0);
}

WindowPtr NewCWindow(void *wStorage, const Rect *boundsRect,
                     ConstStr255Param title, Boolean visible, short procID,
                     WindowPtr behind, Boolean goAwayFlag, long refCon)
{
	(void)title;
	(void)procID;
	return win_new(wStorage, boundsRect, visible, behind, goAwayFlag,
	               refCon, 1);
}

/* Big-endian 16-/32-bit fields of a resource body. */
static unsigned short wind_be16(const unsigned char *p)
{
	return (unsigned short)(((unsigned)p[0] << 8) | p[1]);
}

static unsigned long wind_be32(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16)
	     | ((unsigned long)p[2] << 8)  |  (unsigned long)p[3];
}

/*
 * Shared body of GetNewWindow / GetNewCWindow: load the 'WIND' resource
 * `windowID` and parse it — boundsRect (8 bytes), procID (a word), the
 * visible and goAway flags (a 2-byte field each, nonzero means set), refCon
 * (a long), and the title (a Pascal string) — then hand off to NewWindow or
 * NewCWindow. The title pointer is into the resource body, which the create
 * call does not retain.
 */
static WindowPtr get_new_window(short windowID, void *wStorage,
                                WindowPtr behind, Boolean isColor)
{
	Handle               wind = GetResource('WIND', windowID);
	const unsigned char *p;
	Rect                 bounds;
	ConstStr255Param     title;
	Boolean              visible, goAway;
	short                procID;
	long                 refCon;

	if (wind == NULL || *wind == NULL)
		return NULL;
	p = (const unsigned char *)*wind;

	bounds.top    = (short)wind_be16(p + 0);
	bounds.left   = (short)wind_be16(p + 2);
	bounds.bottom = (short)wind_be16(p + 4);
	bounds.right  = (short)wind_be16(p + 6);
	procID  = (short)wind_be16(p + 8);
	visible = (Boolean)(wind_be16(p + 10) != 0);
	goAway  = (Boolean)(wind_be16(p + 12) != 0);
	refCon  = (long)wind_be32(p + 14);
	title   = p + 18;

	if (isColor)
		return NewCWindow(wStorage, &bounds, title, visible, procID,
		                  behind, goAway, refCon);
	return NewWindow(wStorage, &bounds, title, visible, procID,
	                 behind, goAway, refCon);
}

/* Create a window from the 'WIND' resource with id `windowID`. */
WindowPtr GetNewWindow(short windowID, void *wStorage, WindowPtr behind)
{
	return get_new_window(windowID, wStorage, behind, 0);
}

/* As GetNewWindow, but the window's port slot is a colour CGrafPort. */
WindowPtr GetNewCWindow(short windowID, void *wStorage, WindowPtr behind)
{
	return get_new_window(windowID, wStorage, behind, 1);
}

/*
 * CloseWindow — remove the window from the screen and dispose of its
 * regions and any port-owned resources, but leave the WindowRecord itself
 * to the caller. A NewWindow caller who supplied `wStorage` matches with
 * CloseWindow; a caller who let NewWindow allocate one matches with
 * DisposeWindow, which is CloseWindow + DisposePtr.
 */
void CloseWindow(WindowPtr wp)
{
	WindowPeek w = (WindowPeek)wp;
	CGrafPtr   cp;

	if (w == NULL)
		return;
	/* A colour window owns the PixMap NewCWindow gave its port. */
	cp = (CGrafPtr)&w->port;
	if (((unsigned short)cp->portVersion & CGRAFPORT_FLAG) == CGRAFPORT_FLAG)
		DisposePixMap(cp->portPixMap);
	DisposeRgn(w->updateRgn);
	DisposeRgn(w->port.visRgn);
	win_unlink(w);
}

void DisposeWindow(WindowPtr wp)
{
	if (wp == NULL)
		return;
	CloseWindow(wp);
	DisposePtr((Ptr)wp);
}

void ShowWindow(WindowPtr wp)
{
	if (wp != NULL)
		((WindowPeek)wp)->visible = 1;
}

void HideWindow(WindowPtr wp)
{
	if (wp != NULL)
		((WindowPeek)wp)->visible = 0;
}

void SelectWindow(WindowPtr wp)
{
	WindowPeek w = (WindowPeek)wp;

	if (w == NULL)
		return;
	if (g_window_list != NULL)
		g_window_list->hilited = 0;     /* unhighlight the old front */
	win_unlink(w);
	win_link(w, WIN_FRONT);
	w->hilited = 1;
}

/*
 * SizeWindow — resize the window's content to width x height.
 *
 * Adjusts portRect's bottom-right, keeping its top-left, exactly as the Mac
 * does. The structure/content regions and, when `fUpdate` is set, the
 * exposed-area update region follow with the display HAL.
 */
void SizeWindow(WindowPtr wp, short width, short height, Boolean fUpdate)
{
	WindowPeek w = (WindowPeek)wp;

	(void)fUpdate;
	if (w == NULL)
		return;
	w->port.portRect.right  = (short)(w->port.portRect.left + width);
	w->port.portRect.bottom = (short)(w->port.portRect.top + height);
	win_reset_visrgn(w);
}

/*
 * MoveWindow — move the window so its content top-left is at (h, v), and
 * bring it to the front when `front` is set.
 *
 * First cut: the move offsets portRect. The Mac keeps portRect in local
 * coordinates and repositions the window through its regions and the
 * screen-map offset — that local/global split arrives with the display HAL.
 */
void MoveWindow(WindowPtr wp, short h, short v, Boolean front)
{
	WindowPeek w = (WindowPeek)wp;
	Rect      *r;

	if (w == NULL)
		return;
	r = &w->port.portRect;
	OffsetRect(r, (short)(h - r->left), (short)(v - r->top));
	win_reset_visrgn(w);
	if (front)
		SelectWindow(wp);
}

WindowPtr FrontWindow(void)
{
	WindowPeek w = g_window_list;

	while (w != NULL && !w->visible)
		w = w->nextWindow;
	return (WindowPtr)w;
}

/*
 * InvalRect — add `r` to the update region of the window whose port is
 * current, marking that area for redraw. As on the Mac, the current port
 * must be a window's port.
 */
void InvalRect(const Rect *r)
{
	GrafPtr    port;
	WindowPeek w;
	Rect      *bbox;

	GetPort(&port);
	w = (WindowPeek)port;
	if (w == NULL || w->updateRgn == NULL)
		return;
	bbox = &(*w->updateRgn)->rgnBBox;
	if (EmptyRect(bbox))
		*bbox = *r;
	else
		UnionRect(bbox, r, bbox);
}

/*
 * ValidRect — remove `r` from the update region of the window whose port is
 * current, the inverse of InvalRect. As on the Mac, the current port must
 * be a window's port.
 *
 * Region subtraction can leave a non-rectangular result, and the shim's
 * regions are rectangular (see quickdraw.h). So this takes the conservative
 * case: validate only when `r` fully covers the dirty bounding box —
 * including the common one of validating exactly the rectangle the caller
 * just redrew after BeginUpdate. Otherwise leave the update region alone;
 * over-redrawing is safe, under-redrawing is not.
 */
void ValidRect(const Rect *r)
{
	GrafPtr    port;
	WindowPeek w;
	Rect      *bbox;

	GetPort(&port);
	w = (WindowPeek)port;
	if (w == NULL || w->updateRgn == NULL)
		return;
	bbox = &(*w->updateRgn)->rgnBBox;
	if (EmptyRect(bbox))
		return;
	if (r->left <= bbox->left && r->right  >= bbox->right
	 && r->top  <= bbox->top  && r->bottom >= bbox->bottom)
		SetEmptyRgn(w->updateRgn);
}

/*
 * BeginUpdate — begin handling an update event for `w`: narrow the port's
 * visRgn to the area that needs redrawing, and clear the update region.
 * Drawing is not yet clipped to visRgn (that arrives with the display HAL),
 * but the regions are kept correct meanwhile.
 */
void BeginUpdate(WindowPtr wp)
{
	WindowPeek w = (WindowPeek)wp;

	if (w == NULL || w->updateRgn == NULL || w->port.visRgn == NULL)
		return;
	SectRect(&(*w->port.visRgn)->rgnBBox, &(*w->updateRgn)->rgnBBox,
	         &(*w->port.visRgn)->rgnBBox);
	SetEmptyRgn(w->updateRgn);
}

/* EndUpdate — finish an update: restore visRgn to the full content. */
void EndUpdate(WindowPtr wp)
{
	if (wp != NULL)
		win_reset_visrgn((WindowPeek)wp);
}

/* WindowRecord must be the exact 156-byte Macintosh layout. */
typedef char win_assert_size[sizeof(WindowRecord) == 156 ? 1 : -1];
typedef char win_assert_port[offsetof(WindowRecord, port) == 0 ? 1 : -1];
typedef char win_assert_refcon[offsetof(WindowRecord, refCon) == 152 ? 1 : -1];
