/*
 * Mac Window Manager shim — see windows.h.
 *
 * First cut: the window data model, the front-to-back window list, and the
 * lifecycle. The window frame and title bar (drawing), the colour CGrafPort
 * variant, and region-based update events follow with the display HAL — an
 * honest minimal start, the error.c pattern.
 */

#include <stddef.h>             /* NULL, offsetof */
#include <string.h>             /* memset         */

#include "windows.h"
#include "macmemory.h"          /* NewPtr, DisposePtr */

/* The window list: head = frontmost; WindowRecord.nextWindow chains back. */
static WindowPeek g_window_list;

/* NewWindow / SelectWindow "bring to the front" sentinel for `behind`. */
#define WIN_FRONT  ((WindowPtr)-1L)

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

WindowPtr NewWindow(void *wStorage, const Rect *boundsRect,
                    ConstStr255Param title, Boolean visible, short procID,
                    WindowPtr behind, Boolean goAwayFlag, long refCon)
{
	WindowPeek w;

	(void)title;            /* title storage awaits the Handle-based Memory
	                         * Manager and the title-bar drawing */
	(void)procID;           /* the window definition (WDEF) is deferred */

	w = wStorage ? (WindowPeek)wStorage
	             : (WindowPeek)NewPtr((Size)sizeof(WindowRecord));
	if (w == NULL)
		return NULL;
	memset(w, 0, sizeof *w);

	/* portRect is the content in the window's own local coordinates;
	 * portBits.bounds keeps the global placement. The full local/global
	 * coordinate setup arrives with the drawing layer. */
	SetRect(&w->port.portRect, 0, 0,
	        (short)(boundsRect->right - boundsRect->left),
	        (short)(boundsRect->bottom - boundsRect->top));
	w->port.portBits.bounds = *boundsRect;

	w->windowKind = userKind;
	w->goAwayFlag = goAwayFlag;
	w->refCon     = refCon;

	win_link(w, behind);
	SetPort((WindowPtr)w);
	if (visible)
		ShowWindow((WindowPtr)w);
	return (WindowPtr)w;
}

void DisposeWindow(WindowPtr wp)
{
	WindowPeek w = (WindowPeek)wp;

	if (w == NULL)
		return;
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
