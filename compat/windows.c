/*
 * Mac Window Manager shim — see windows.h.
 *
 * First cut: the window data model, the window list, the lifecycle, b&w /
 * colour windows, and resource-loaded windows (GetNewCWindow). The window
 * frame and title bar (drawing) and region-based update events follow with
 * the display HAL — an honest minimal start, the error.c pattern.
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
 * GetNewCWindow — create a colour window from a 'WIND' resource.
 *
 * The 'WIND' resource is the boundsRect (8 bytes), the procID (a word), the
 * visible and goAway flags (a 2-byte field each — nonzero means set), the
 * refCon (a long), and the title (a Pascal string); NewCWindow does the
 * rest. The title pointer is into the resource body, which NewCWindow does
 * not retain.
 */
WindowPtr GetNewCWindow(short windowID, void *wStorage, WindowPtr behind)
{
	Handle               wind = GetResource('WIND', windowID);
	const unsigned char *p;
	Rect                 bounds;

	if (wind == NULL || *wind == NULL)
		return NULL;
	p = (const unsigned char *)*wind;

	bounds.top    = (short)wind_be16(p + 0);
	bounds.left   = (short)wind_be16(p + 2);
	bounds.bottom = (short)wind_be16(p + 4);
	bounds.right  = (short)wind_be16(p + 6);

	return NewCWindow(wStorage, &bounds, p + 18,
	                  (Boolean)(wind_be16(p + 10) != 0),  /* visible */
	                  (short)wind_be16(p + 8),            /* procID  */
	                  behind,
	                  (Boolean)(wind_be16(p + 12) != 0),  /* goAway  */
	                  (long)wind_be32(p + 14));           /* refCon  */
}

void DisposeWindow(WindowPtr wp)
{
	WindowPeek w = (WindowPeek)wp;
	CGrafPtr   cp;

	if (w == NULL)
		return;
	/* A colour window owns the PixMap NewCWindow gave its port. */
	cp = (CGrafPtr)&w->port;
	if (((unsigned short)cp->portVersion & CGRAFPORT_FLAG) == CGRAFPORT_FLAG)
		DisposePixMap(cp->portPixMap);
	win_unlink(w);
	DisposePtr((Ptr)w);
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

WindowPtr FrontWindow(void)
{
	WindowPeek w = g_window_list;

	while (w != NULL && !w->visible)
		w = w->nextWindow;
	return (WindowPtr)w;
}

/* WindowRecord must be the exact 156-byte Macintosh layout. */
typedef char win_assert_size[sizeof(WindowRecord) == 156 ? 1 : -1];
typedef char win_assert_port[offsetof(WindowRecord, port) == 0 ? 1 : -1];
typedef char win_assert_refcon[offsetof(WindowRecord, refCon) == 152 ? 1 : -1];
