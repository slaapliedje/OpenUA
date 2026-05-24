/*
 * Mac Window Manager shim — see windows.h.
 *
 * Here so far: the window data model, the window list, the lifecycle and
 * geometry, b&w / colour windows, resource-loaded windows (GetNewWindow /
 * GetNewCWindow), the update mechanism (InvalRect / ValidRect /
 * BeginUpdate / EndUpdate) over rectangular regions, the structure /
 * content regions in global screen coordinates, the window's title in a
 * Handle, and the desktop-side frame drawing (a 1-pixel black outline, a
 * filled title bar with the title centred, a close box when goAwayFlag is
 * set) painted into the screen port on ShowWindow / SelectWindow. The
 * local/global coordinate split, drag / find / track-go-away, and the
 * active vs inactive title-bar styling follow.
 */

#include <stddef.h>             /* NULL, offsetof */
#include <string.h>             /* memset         */

#include "windows.h"
#include "macmemory.h"          /* NewPtr, DisposePtr */
#include "resources.h"          /* GetResource        */
#include "events.h"             /* Button, GetMouse for DragWindow / Track */

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

/* Set the window's visRgn to its full content — the rectangular-region
 * stand-in for "the window is entirely visible". */
static void win_reset_visrgn(WindowPeek w)
{
	if (w->port.visRgn != NULL)
		RectRgn(w->port.visRgn, &w->port.portRect);
}

/*
 * The window frame geometry the shim draws around every window. Sized for
 * the 8x8 fallback font (TITLE_BAR_HEIGHT = ascent + a couple px above and
 * below) so the title text sits comfortably. Real WDEFs will take over and
 * each can pick its own; this is the system-7 stand-in.
 */
#define WIN_FRAME_PX         1
#define WIN_TITLE_BAR_HEIGHT 14
#define WIN_CLOSE_BOX_SIZE   11
#define WIN_CLOSE_BOX_PAD    2

/* Close-box rect, in screen coordinates — shared by win_draw_frame and
 * the hit-testing entries (FindWindow / TrackGoAway) so the box never
 * drifts between the painted pixels and the hit area. */
static void win_close_box(WindowPeek w, Rect *box)
{
	short struc_left = (*w->strucRgn)->rgnBBox.left;
	short struc_top  = (*w->strucRgn)->rgnBBox.top;
	short box_top    = (short)(struc_top
	                   + (WIN_TITLE_BAR_HEIGHT - WIN_CLOSE_BOX_SIZE) / 2);
	short box_left   = (short)(struc_left + WIN_CLOSE_BOX_PAD + WIN_FRAME_PX);

	SetRect(box, box_left, box_top,
	        (short)(box_left + WIN_CLOSE_BOX_SIZE),
	        (short)(box_top  + WIN_CLOSE_BOX_SIZE));
}

/*
 * Set strucRgn (the whole window including frame and title bar) and
 * contRgn (the content area only) in global screen coordinates from the
 * content bounds `bounds` the caller passed to NewWindow.
 */
static void win_set_regions(WindowPeek w, const Rect *bounds)
{
	Rect struc;

	if (w->contRgn != NULL)
		RectRgn(w->contRgn, bounds);

	struc = *bounds;
	struc.top    = (short)(struc.top    - WIN_TITLE_BAR_HEIGHT - WIN_FRAME_PX);
	struc.left   = (short)(struc.left   - WIN_FRAME_PX);
	struc.right  = (short)(struc.right  + WIN_FRAME_PX);
	struc.bottom = (short)(struc.bottom + WIN_FRAME_PX);
	if (w->strucRgn != NULL)
		RectRgn(w->strucRgn, &struc);
}

/*
 * Copy the title's Pascal-string bytes into a relocatable block on the
 * heap and stash it in w->titleHandle, replacing any previous title.
 * w->titleWidth is set so the frame drawing can centre the text without
 * re-measuring.
 */
static void win_set_title(WindowPeek w, ConstStr255Param title)
{
	unsigned char len, i;
	Handle        h;

	if (w->titleHandle != NULL) {
		DisposeHandle(w->titleHandle);
		w->titleHandle = NULL;
	}
	w->titleWidth = 0;
	if (title == NULL)
		return;
	len = title[0];
	h   = NewHandle((Size)(len + 1));
	if (h == NULL || *h == NULL)
		return;
	for (i = 0; i <= len; i++)
		((unsigned char *)*h)[i] = title[i];
	w->titleHandle = h;
	w->titleWidth  = StringWidth(title);
}

/*
 * Paint the window's frame, title bar, title text, and (optional) close
 * box into the screen port. The window's port has the *content*; the
 * frame lives outside it on the desktop, so this draws into qd_screen_port
 * with the caller's current port saved and restored around the operation.
 *
 * Per-port state on the screen port (pen size / mode / pattern, fg / bk
 * colour, pen location) is also saved across this call: the engine drives
 * the screen port directly for desktop drawing and would otherwise see it
 * mutated under its feet.
 */
static void win_draw_frame(WindowPeek w)
{
	GrafPtr  saved_port;
	GrafPtr  scr;
	CGrafPtr scp;
	Rect     struc, cont, title_bar;
	short    saved_fg, saved_bk;
	Point    saved_pn_loc, saved_pn_size;
	short    saved_pn_mode;
	Pattern  saved_pn_pat;
	int      i;

	if (w == NULL || !w->visible || w->strucRgn == NULL || w->contRgn == NULL)
		return;
	scr = qd_screen_port();
	if (scr == NULL)
		return;
	scp = (CGrafPtr)scr;

	GetPort(&saved_port);
	saved_fg      = (short)scp->fgColor;
	saved_bk      = (short)scp->bkColor;
	saved_pn_loc  = scr->pnLoc;
	saved_pn_size = scr->pnSize;
	saved_pn_mode = scr->pnMode;
	for (i = 0; i < 8; i++)
		saved_pn_pat.pat[i] = scr->pnPat.pat[i];
	SetPort(scr);

	struc = (*w->strucRgn)->rgnBBox;
	cont  = (*w->contRgn)->rgnBBox;

	/* Title bar: solid mid-grey strip across the top of the structure.
	 * In our 332 palette index 0xDB is (192, 192, 192) — light grey. */
	SetRect(&title_bar, struc.left, struc.top, struc.right, cont.top);
	PenSize(1, 1);
	PenMode(patCopy);
	for (i = 0; i < 8; i++)
		scr->pnPat.pat[i] = 0xFF;
	scp->fgColor = 0xDB;
	PaintRect(&title_bar);

	/* Content background — paper white for the demo; the engine repaints
	 * it on the first update event with whatever the window contains. */
	scp->fgColor = 0xFF;
	PaintRect(&cont);

	/* Frame: 1-pixel black outline around the whole structure plus a
	 * separator under the title bar. */
	scp->fgColor = 0;
	FrameRect(&struc);
	MoveTo(struc.left, (short)(cont.top - 1));
	LineTo((short)(struc.right - 1), (short)(cont.top - 1));

	/* Close box on the left, when goAwayFlag is set. The geometry lives
	 * in win_close_box so hit-testing matches the painted pixels. */
	if (w->goAwayFlag) {
		Rect box;

		win_close_box(w, &box);
		FrameRect(&box);
	}

	/* Title text: centred in the title bar, baseline near its bottom,
	 * rendered in black through the embedded 8x8 fallback font. */
	if (w->titleHandle != NULL && *w->titleHandle != NULL) {
		ConstStr255Param title = (ConstStr255Param)*w->titleHandle;
		short            title_x = (short)(struc.left
		                + ((struc.right - struc.left) - w->titleWidth) / 2);
		short            title_y = (short)(cont.top - 3);

		MoveTo(title_x, title_y);
		DrawString(title);
	}

	scp->fgColor = saved_fg;
	scp->bkColor = saved_bk;
	scr->pnLoc   = saved_pn_loc;
	scr->pnSize  = saved_pn_size;
	scr->pnMode  = saved_pn_mode;
	for (i = 0; i < 8; i++)
		scr->pnPat.pat[i] = saved_pn_pat.pat[i];
	SetPort(saved_port);
}

/*
 * Shared body of NewWindow / NewCWindow: allocate or adopt the storage, set
 * the window up, and link it into the list. `isColor` selects whether the
 * 108-byte port slot is initialised as a GrafPort or a CGrafPort.
 */
static WindowPtr win_new(void *wStorage, const Rect *boundsRect,
                         ConstStr255Param title, Boolean visible,
                         WindowPtr behind, Boolean goAwayFlag,
                         long refCon, Boolean isColor)
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

	/* The window's update region, its port's visible region, and the
	 * structure / content regions in global screen coords. */
	w->updateRgn   = NewRgn();
	w->port.visRgn = NewRgn();
	w->strucRgn    = NewRgn();
	w->contRgn     = NewRgn();
	win_reset_visrgn(w);
	win_set_regions(w, boundsRect);
	win_set_title(w, title);

	/* Per-port drawing defaults — pnSize (1,1), patCopy, solid pen,
	 * fgColor 255, bkColor 0. The Mac's OpenPort equivalent. */
	qd_init_port_defaults(&w->port);

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
	(void)procID;           /* the window definition (WDEF) is deferred */
	return win_new(wStorage, boundsRect, title, visible, behind,
	               goAwayFlag, refCon, 0);
}

WindowPtr NewCWindow(void *wStorage, const Rect *boundsRect,
                     ConstStr255Param title, Boolean visible, short procID,
                     WindowPtr behind, Boolean goAwayFlag, long refCon)
{
	(void)procID;
	return win_new(wStorage, boundsRect, title, visible, behind,
	               goAwayFlag, refCon, 1);
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
	DisposeRgn(w->strucRgn);
	DisposeRgn(w->contRgn);
	if (w->titleHandle != NULL)
		DisposeHandle(w->titleHandle);
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
	WindowPeek w = (WindowPeek)wp;

	if (w == NULL || w->visible)
		return;
	w->visible = 1;
	win_draw_frame(w);
}

void HideWindow(WindowPtr wp)
{
	if (wp != NULL)
		((WindowPeek)wp)->visible = 0;
}

void SelectWindow(WindowPtr wp)
{
	WindowPeek w = (WindowPeek)wp;
	WindowPeek old_front;

	if (w == NULL)
		return;
	old_front = g_window_list;
	if (old_front != NULL && old_front != w)
		old_front->hilited = 0;
	win_unlink(w);
	win_link(w, WIN_FRONT);
	w->hilited = 1;

	/* Repaint the affected frames so the active / inactive distinction
	 * (when it lands) takes effect immediately. Until the styling diverges
	 * this just redraws the same pixels. */
	if (old_front != NULL && old_front != w)
		win_draw_frame(old_front);
	win_draw_frame(w);
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
	Rect       new_cont;

	(void)fUpdate;
	if (w == NULL)
		return;
	w->port.portRect.right  = (short)(w->port.portRect.left + width);
	w->port.portRect.bottom = (short)(w->port.portRect.top + height);
	win_reset_visrgn(w);
	if (w->contRgn != NULL) {
		new_cont = (*w->contRgn)->rgnBBox;
		new_cont.right  = (short)(new_cont.left + width);
		new_cont.bottom = (short)(new_cont.top  + height);
		win_set_regions(w, &new_cont);
	}
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
	Rect       new_cont;

	if (w == NULL)
		return;
	r = &w->port.portRect;
	OffsetRect(r, (short)(h - r->left), (short)(v - r->top));
	win_reset_visrgn(w);
	if (w->contRgn != NULL) {
		new_cont = (*w->contRgn)->rgnBBox;
		new_cont.right  = (short)(new_cont.right  - new_cont.left + h);
		new_cont.bottom = (short)(new_cont.bottom - new_cont.top  + v);
		new_cont.left   = h;
		new_cont.top    = v;
		win_set_regions(w, &new_cont);
	}
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

/* --- user-action plumbing: FindWindow, DragWindow, TrackGoAway --- */

short FindWindow(Point thePt, WindowPtr *whichWindow)
{
	WindowPeek w;

	if (whichWindow != NULL)
		*whichWindow = NULL;

	/* Front to back over visible windows: g_window_list is the list head
	 * (frontmost first), with hidden windows skipped inline. */
	for (w = g_window_list; w != NULL; w = w->nextWindow) {
		if (!w->visible || w->strucRgn == NULL || w->contRgn == NULL)
			continue;
		if (!PtInRect(thePt, &(*w->strucRgn)->rgnBBox))
			continue;
		if (whichWindow != NULL)
			*whichWindow = (WindowPtr)w;

		if (w->goAwayFlag) {
			Rect close_box;

			win_close_box(w, &close_box);
			if (PtInRect(thePt, &close_box))
				return inGoAway;
		}
		if (PtInRect(thePt, &(*w->contRgn)->rgnBBox))
			return inContent;
		/* Inside the structure but outside content / close box — the
		 * draggable area (title bar / frame edges). */
		return inDrag;
	}
	return inDesk;
}

/*
 * Tracking helper for DragWindow: paint or erase the drag outline on the
 * screen port. The outline is strucRgn offset by (dh, dv); patXor + a
 * solid pen means two consecutive calls with the same offset cancel
 * (a draw then an erase) — that's how the previous outline gets removed
 * before the next is laid down without saving any pixels.
 */
static void win_drag_outline(WindowPeek w, short dh, short dv)
{
	Rect r = (*w->strucRgn)->rgnBBox;

	OffsetRect(&r, dh, dv);
	FrameRect(&r);
}

void DragWindow(WindowPtr wp, Point startPt, const Rect *boundsRect)
{
	WindowPeek w = (WindowPeek)wp;
	GrafPtr    scr, saved_port;
	CGrafPtr   scp;
	Point      cur;
	short      saved_fg, saved_pn_mode;
	short      saved_pn_size_h, saved_pn_size_v;
	Pattern    saved_pn_pat;
	short      dh = 0, dv = 0, prev_dh = 0, prev_dv = 0;
	int        i;

	if (w == NULL || boundsRect == NULL || w->strucRgn == NULL
	 || w->contRgn == NULL)
		return;
	scr = qd_screen_port();
	if (scr == NULL)
		return;
	scp = (CGrafPtr)scr;

	GetPort(&saved_port);
	saved_fg        = (short)scp->fgColor;
	saved_pn_mode   = scr->pnMode;
	saved_pn_size_h = scr->pnSize.h;
	saved_pn_size_v = scr->pnSize.v;
	for (i = 0; i < 8; i++)
		saved_pn_pat.pat[i] = scr->pnPat.pat[i];
	SetPort(scr);

	/* XOR outline in fgColor=255 with a solid pattern at 1x1 — that
	 * gives the classic "marching pixels" look against any backdrop. */
	scp->fgColor = 0xFF;
	PenSize(1, 1);
	PenMode(patXor);
	for (i = 0; i < 8; i++)
		scr->pnPat.pat[i] = 0xFF;

	win_drag_outline(w, 0, 0);

	while (Button()) {
		GetMouse(&cur);
		dh = (short)(cur.h - startPt.h);
		dv = (short)(cur.v - startPt.v);
		if (dh != prev_dh || dv != prev_dv) {
			win_drag_outline(w, prev_dh, prev_dv); /* erase old */
			win_drag_outline(w, dh, dv);           /* draw new  */
			prev_dh = dh;
			prev_dv = dv;
		}
	}

	/* Erase the final outline. The button has gone up; the window
	 * paint takes over from MoveWindow below. */
	win_drag_outline(w, prev_dh, prev_dv);

	scp->fgColor   = saved_fg;
	scr->pnMode    = saved_pn_mode;
	scr->pnSize.h  = saved_pn_size_h;
	scr->pnSize.v  = saved_pn_size_v;
	for (i = 0; i < 8; i++)
		scr->pnPat.pat[i] = saved_pn_pat.pat[i];
	SetPort(saved_port);

	if (prev_dh != 0 || prev_dv != 0) {
		Rect  cont = (*w->contRgn)->rgnBBox;
		short new_h = (short)(cont.left + prev_dh);
		short new_v = (short)(cont.top  + prev_dv);

		if (new_h < boundsRect->left)   new_h = boundsRect->left;
		if (new_v < boundsRect->top)    new_v = boundsRect->top;
		if (new_h > boundsRect->right)  new_h = boundsRect->right;
		if (new_v > boundsRect->bottom) new_v = boundsRect->bottom;
		MoveWindow(wp, new_h, new_v, 0);
	}
}

Boolean TrackGoAway(WindowPtr wp, Point startPt)
{
	WindowPeek w = (WindowPeek)wp;
	Rect       close_box;
	Point      cur = startPt;

	if (w == NULL || !w->goAwayFlag || w->strucRgn == NULL)
		return 0;
	win_close_box(w, &close_box);
	if (!PtInRect(startPt, &close_box))
		return 0;

	/* Track: spin while the button is down, sampling the mouse so the
	 * "released inside" check at the end is accurate. Highlighting the
	 * box (invert-while-inside) is a later visual refinement. */
	while (Button())
		GetMouse(&cur);

	return (Boolean)PtInRect(cur, &close_box);
}

/* WindowRecord must be the exact 156-byte Macintosh layout. */
typedef char win_assert_size[sizeof(WindowRecord) == 156 ? 1 : -1];
typedef char win_assert_port[offsetof(WindowRecord, port) == 0 ? 1 : -1];
typedef char win_assert_refcon[offsetof(WindowRecord, refCon) == 152 ? 1 : -1];
