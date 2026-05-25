/*
 * Mac Control Manager shim — skeleton. See controls.h.
 *
 * Controls hang off their owner's WindowRecord.controlList (a Handle the
 * Mac uses interchangeably with the head ControlHandle); each
 * ControlRecord.contrlNext threads the rest. NewControl prepends to the
 * list so the most recently added control is hit-tested first — matches
 * the Mac convention of "frontmost control draws last, gets hit first".
 *
 * Drawing covers the three primary CDEFs (pushButProc, checkBoxProc,
 * radioButProc) directly in this file — straight QuickDraw into the
 * owner's port. TrackControl spins on Button() / GetMouse(), flipping
 * the hilite state when the mouse crosses the control rect boundary;
 * on release inside, returns the part code; on release outside, 0.
 *
 * Scroll bars (procID 16), the full CDEF dispatch, control colour, and
 * the Dialog Manager DITL type-4 → ControlHandle integration are all
 * follow-on work.
 */

#include <stddef.h>             /* NULL */
#include <string.h>             /* memcpy, memset */

#include "controls.h"
#include "events.h"             /* Button, GetMouse */
#include "macmemory.h"
#include "quickdraw.h"
#include "resources.h"          /* GetResource for GetNewControl */

/* The window's controlList field is a Handle; we treat it as the head
 * of the control linked list (ControlHandle). The helper macros keep
 * the cast in one place. */
#define WIN_CONTROL_LIST(wp)        ((ControlHandle)((WindowPeek)(wp))->controlList)
#define SET_WIN_CONTROL_LIST(wp, h) (((WindowPeek)(wp))->controlList = (Handle)(h))

/* Pascal-string helpers (shared with menus.c). */
static void cpstr_copy(unsigned char *dst, const unsigned char *src)
{
	short n;

	if (dst == NULL)
		return;
	if (src == NULL) {
		dst[0] = 0;
		return;
	}
	n = (short)src[0];
	if (n > 255)
		n = 255;
	dst[0] = (unsigned char)n;
	if (n > 0)
		memcpy(dst + 1, src + 1, (size_t)n);
}

/* --- linked-list management ---------------------------------------- */

static void link_control(WindowPtr owner, ControlHandle c)
{
	(*c)->contrlNext = WIN_CONTROL_LIST(owner);
	SET_WIN_CONTROL_LIST(owner, c);
}

static void unlink_control(ControlHandle c)
{
	WindowPtr      owner;
	ControlHandle  cur, prev = NULL;

	if (c == NULL || *c == NULL)
		return;
	owner = (*c)->contrlOwner;
	if (owner == NULL)
		return;

	cur = WIN_CONTROL_LIST(owner);
	while (cur != NULL) {
		if (cur == c) {
			ControlHandle next = (*cur)->contrlNext;

			if (prev == NULL)
				SET_WIN_CONTROL_LIST(owner, next);
			else
				(*prev)->contrlNext = next;
			return;
		}
		prev = cur;
		cur  = (*cur)->contrlNext;
	}
}

/* --- creation ------------------------------------------------------ */

ControlHandle NewControl(WindowPtr owner, const Rect *boundsRect,
                         ConstStr255Param title, Boolean visible,
                         short value, short min, short max, short procID,
                         long refCon)
{
	ControlHandle  c;
	ControlRecord *info;

	if (owner == NULL || boundsRect == NULL)
		return NULL;
	c = (ControlHandle)NewHandleClear((Size)sizeof(ControlRecord));
	if (c == NULL)
		return NULL;
	info = *c;
	info->contrlOwner  = owner;
	info->contrlRect   = *boundsRect;
	info->contrlVis    = (unsigned char)(visible ? 255 : 0);
	info->contrlHilite = 0;
	info->contrlValue  = value;
	info->contrlMin    = min;
	info->contrlMax    = max;
	info->contrlDefProc = (Handle)(long)procID;       /* low word = CDEF id */
	info->contrlData    = NULL;
	info->contrlAction  = 0;
	info->contrlRfCon   = refCon;
	if (title != NULL && title[0] > 0) {
		Handle h = NewHandleClear((Size)title[0] + 1);

		if (h != NULL) {
			cpstr_copy((unsigned char *)*h, title);
			info->contrlTitle = h;
		}
	}
	link_control(owner, c);
	if (visible)
		Draw1Control(c);
	return c;
}

ControlHandle GetNewControl(short controlID, WindowPtr owner)
{
	Handle               h;
	const unsigned char *p;
	long                 sz, off;
	Rect                 bounds;
	short                value, max, min, procID;
	Boolean              visible;
	long                 refCon;
	unsigned char        title[256];

	h = GetResource(0x434E544CL /* 'CNTL' */, controlID);
	if (h == NULL || *h == NULL)
		return NULL;
	sz = GetHandleSize(h);
	if (sz < 22)
		return NULL;
	p = (const unsigned char *)*h;
	bounds.top    = (short)(((unsigned)p[0]  << 8) | p[1]);
	bounds.left   = (short)(((unsigned)p[2]  << 8) | p[3]);
	bounds.bottom = (short)(((unsigned)p[4]  << 8) | p[5]);
	bounds.right  = (short)(((unsigned)p[6]  << 8) | p[7]);
	value         = (short)(((unsigned)p[8]  << 8) | p[9]);
	visible       = (Boolean)p[10];
	max           = (short)(((unsigned)p[12] << 8) | p[13]);
	min           = (short)(((unsigned)p[14] << 8) | p[15]);
	procID        = (short)(((unsigned)p[16] << 8) | p[17]);
	refCon        = ((long)p[18] << 24) | ((long)p[19] << 16)
	              | ((long)p[20] << 8)  | (long)p[21];
	off           = 22;
	title[0]      = (off < sz) ? p[off] : 0;
	if (title[0] > 0 && off + 1 + title[0] <= sz)
		memcpy(title + 1, p + off + 1, (size_t)title[0]);

	return NewControl(owner, &bounds, title, visible, value, min, max,
	                  procID, refCon);
}

void DisposeControl(ControlHandle c)
{
	ControlRecord *info;

	if (c == NULL || *c == NULL)
		return;
	info = *c;
	unlink_control(c);
	if (info->contrlTitle != NULL)
		DisposeHandle(info->contrlTitle);
	DisposeHandle((Handle)c);
}

void KillControls(WindowPtr owner)
{
	ControlHandle c;

	if (owner == NULL)
		return;
	while ((c = WIN_CONTROL_LIST(owner)) != NULL)
		DisposeControl(c);
}

/* --- accessors ----------------------------------------------------- */

void ShowControl(ControlHandle c)
{
	if (c == NULL || *c == NULL)
		return;
	if ((*c)->contrlVis == 255)
		return;
	(*c)->contrlVis = 255;
	Draw1Control(c);
}

void HideControl(ControlHandle c)
{
	if (c == NULL || *c == NULL)
		return;
	(*c)->contrlVis = 0;
	/* The Mac would erase the control rect on hide; the shim leaves
	 * that to the caller's update cycle for now. */
}

void HiliteControl(ControlHandle c, short hiliteState)
{
	if (c == NULL || *c == NULL)
		return;
	(*c)->contrlHilite = (unsigned char)(hiliteState & 0xFF);
	if ((*c)->contrlVis == 255)
		Draw1Control(c);
}

void SetControlValue(ControlHandle c, short value)
{
	if (c == NULL || *c == NULL)
		return;
	if (value < (*c)->contrlMin) value = (*c)->contrlMin;
	if (value > (*c)->contrlMax) value = (*c)->contrlMax;
	if ((*c)->contrlValue == value)
		return;
	(*c)->contrlValue = value;
	if ((*c)->contrlVis == 255)
		Draw1Control(c);
}

short GetControlValue(ControlHandle c)
{
	if (c == NULL || *c == NULL)
		return 0;
	return (*c)->contrlValue;
}

void SetControlMinimum(ControlHandle c, short min)
{
	if (c == NULL || *c == NULL)
		return;
	(*c)->contrlMin = min;
	if ((*c)->contrlValue < min)
		(*c)->contrlValue = min;
}

short GetControlMinimum(ControlHandle c)
{
	return (c == NULL || *c == NULL) ? 0 : (*c)->contrlMin;
}

void SetControlMaximum(ControlHandle c, short max)
{
	if (c == NULL || *c == NULL)
		return;
	(*c)->contrlMax = max;
	if ((*c)->contrlValue > max)
		(*c)->contrlValue = max;
}

short GetControlMaximum(ControlHandle c)
{
	return (c == NULL || *c == NULL) ? 0 : (*c)->contrlMax;
}

void SetControlTitle(ControlHandle c, ConstStr255Param title)
{
	if (c == NULL || *c == NULL)
		return;
	if ((*c)->contrlTitle != NULL) {
		DisposeHandle((*c)->contrlTitle);
		(*c)->contrlTitle = NULL;
	}
	if (title != NULL && title[0] > 0) {
		Handle h = NewHandleClear((Size)title[0] + 1);

		if (h != NULL) {
			cpstr_copy((unsigned char *)*h, title);
			(*c)->contrlTitle = h;
		}
	}
	if ((*c)->contrlVis == 255)
		Draw1Control(c);
}

void GetControlTitle(ControlHandle c, unsigned char *title)
{
	if (title == NULL)
		return;
	title[0] = 0;
	if (c == NULL || *c == NULL || (*c)->contrlTitle == NULL
	 || *(*c)->contrlTitle == NULL)
		return;
	cpstr_copy(title, (const unsigned char *)*(*c)->contrlTitle);
}

void SetControlReference(ControlHandle c, long refCon)
{
	if (c == NULL || *c == NULL)
		return;
	(*c)->contrlRfCon = refCon;
}

long GetControlReference(ControlHandle c)
{
	return (c == NULL || *c == NULL) ? 0 : (*c)->contrlRfCon;
}

/* --- drawing ------------------------------------------------------- */

/* Map control-rect (window-local) to the screen-port global rect for
 * painting. The shim's window content lives in global coords already —
 * win_set_regions stores contRgn->rgnBBox at the global content top-
 * left, so we just add the offset. */
static void rect_to_global(const ControlRecord *info, Rect *out)
{
	WindowPeek w = (WindowPeek)info->contrlOwner;
	short      ox = 0, oy = 0;

	if (w != NULL && w->contRgn != NULL) {
		ox = (*w->contRgn)->rgnBBox.left;
		oy = (*w->contRgn)->rgnBBox.top;
	}
	out->top    = (short)(info->contrlRect.top    + oy);
	out->left   = (short)(info->contrlRect.left   + ox);
	out->bottom = (short)(info->contrlRect.bottom + oy);
	out->right  = (short)(info->contrlRect.right  + ox);
}

static const RGBColor kCtrlBlack = { 0x0000, 0x0000, 0x0000 };
static const RGBColor kCtrlWhite = { 0xFFFF, 0xFFFF, 0xFFFF };
static const RGBColor kCtrlGrey  = { 0x8000, 0x8000, 0x8000 };

/* --- scroll-bar geometry helpers ----------------------------------- */

#define SCROLL_ARROW_SIZE 16    /* Mac default arrow / thumb cap size */

/* A vertical scroll bar is taller than it is wide. */
static int scroll_is_vertical(const Rect *r)
{
	return (r->bottom - r->top) > (r->right - r->left);
}

/* Arrow / page-zone / thumb rects in global coords. The thumb sits at
 * a position proportional to (value - min) / (max - min). When max ==
 * min the scroll bar is "inert" — TestControl returns 0 for the page
 * and thumb zones (the arrows still respond, matching Mac behaviour).
 * Out-of-range fractions are clamped. */
static void scroll_rects(const ControlRecord *info, Rect *up, Rect *down,
                         Rect *thumb, Rect *page_up, Rect *page_down)
{
	Rect  bar;
	int   vertical;
	short ax, ay, bx, by, len, track_min, track_max, thumb_pos;
	long  span;

	rect_to_global(info, &bar);
	vertical = scroll_is_vertical(&bar);

	if (vertical) {
		ax = bar.left; ay = bar.top;
		bx = bar.right;
		by = (short)(bar.top + SCROLL_ARROW_SIZE);
		SetRect(up, ax, ay, bx, by);
		ay = (short)(bar.bottom - SCROLL_ARROW_SIZE);
		SetRect(down, bar.left, ay, bar.right, bar.bottom);

		track_min = (short)(bar.top + SCROLL_ARROW_SIZE);
		track_max = (short)(bar.bottom - SCROLL_ARROW_SIZE
		                              - SCROLL_ARROW_SIZE);
		len       = (short)(track_max - track_min);
	} else {
		ay = bar.top; by = bar.bottom;
		SetRect(up,  bar.left,
		            ay,
		            (short)(bar.left + SCROLL_ARROW_SIZE),
		            by);
		SetRect(down, (short)(bar.right - SCROLL_ARROW_SIZE),
		            ay, bar.right, by);

		track_min = (short)(bar.left + SCROLL_ARROW_SIZE);
		track_max = (short)(bar.right - SCROLL_ARROW_SIZE
		                              - SCROLL_ARROW_SIZE);
		len       = (short)(track_max - track_min);
	}

	span = (long)info->contrlMax - (long)info->contrlMin;
	if (span <= 0 || len <= 0) {
		thumb_pos = track_min;
		if (thumb) {
			if (vertical)
				SetRect(thumb, bar.left, track_min,
				        bar.right, (short)(track_min + SCROLL_ARROW_SIZE));
			else
				SetRect(thumb, track_min, bar.top,
				        (short)(track_min + SCROLL_ARROW_SIZE), bar.bottom);
		}
	} else {
		long frac_num = (long)(info->contrlValue - info->contrlMin) * (long)len;
		long frac_pos = frac_num / span;

		thumb_pos = (short)(track_min + frac_pos);
		if (thumb) {
			if (vertical)
				SetRect(thumb, bar.left, thumb_pos,
				        bar.right, (short)(thumb_pos + SCROLL_ARROW_SIZE));
			else
				SetRect(thumb, thumb_pos, bar.top,
				        (short)(thumb_pos + SCROLL_ARROW_SIZE), bar.bottom);
		}
	}

	if (page_up) {
		if (vertical)
			SetRect(page_up, bar.left,
			        (short)(bar.top + SCROLL_ARROW_SIZE),
			        bar.right, thumb_pos);
		else
			SetRect(page_up,
			        (short)(bar.left + SCROLL_ARROW_SIZE),
			        bar.top, thumb_pos, bar.bottom);
	}
	if (page_down) {
		if (vertical)
			SetRect(page_down, bar.left,
			        (short)(thumb_pos + SCROLL_ARROW_SIZE),
			        bar.right,
			        (short)(bar.bottom - SCROLL_ARROW_SIZE));
		else
			SetRect(page_down,
			        (short)(thumb_pos + SCROLL_ARROW_SIZE),
			        bar.top,
			        (short)(bar.right - SCROLL_ARROW_SIZE),
			        bar.bottom);
	}
}

static void draw_arrow_glyph(Rect r, int vertical, int forward)
{
	short cx, cy;
	short a, b;

	cx = (short)((r.left + r.right) / 2);
	cy = (short)((r.top + r.bottom) / 2);
	a = 4;
	b = 3;

	if (vertical) {
		if (forward) {
			/* down arrow: chevron pointing south */
			MoveTo((short)(cx - a), (short)(cy - b));
			LineTo((short)(cx + a), (short)(cy - b));
			MoveTo((short)(cx - a + 1), (short)(cy - b + 1));
			LineTo((short)(cx + a - 1), (short)(cy - b + 1));
			MoveTo((short)(cx - 1), (short)(cy + b - 1));
			LineTo((short)(cx + 1), (short)(cy + b - 1));
			MoveTo(cx, (short)(cy + b));
			LineTo(cx, (short)(cy + b));
		} else {
			MoveTo((short)(cx - a), (short)(cy + b));
			LineTo((short)(cx + a), (short)(cy + b));
			MoveTo((short)(cx - a + 1), (short)(cy + b - 1));
			LineTo((short)(cx + a - 1), (short)(cy + b - 1));
			MoveTo((short)(cx - 1), (short)(cy - b + 1));
			LineTo((short)(cx + 1), (short)(cy - b + 1));
			MoveTo(cx, (short)(cy - b));
			LineTo(cx, (short)(cy - b));
		}
	} else {
		if (forward) {
			MoveTo((short)(cx - b), (short)(cy - a));
			LineTo((short)(cx - b), (short)(cy + a));
			MoveTo((short)(cx - b + 1), (short)(cy - a + 1));
			LineTo((short)(cx - b + 1), (short)(cy + a - 1));
			MoveTo((short)(cx + b - 1), (short)(cy - 1));
			LineTo((short)(cx + b - 1), (short)(cy + 1));
			MoveTo((short)(cx + b), cy);
			LineTo((short)(cx + b), cy);
		} else {
			MoveTo((short)(cx + b), (short)(cy - a));
			LineTo((short)(cx + b), (short)(cy + a));
			MoveTo((short)(cx + b - 1), (short)(cy - a + 1));
			LineTo((short)(cx + b - 1), (short)(cy + a - 1));
			MoveTo((short)(cx - b + 1), (short)(cy - 1));
			LineTo((short)(cx - b + 1), (short)(cy + 1));
			MoveTo((short)(cx - b), cy);
			LineTo((short)(cx - b), cy);
		}
	}
}

static void draw_scrollbar(const ControlRecord *info)
{
	Rect g, up, down, thumb, pu, pd;
	int  vertical;
	int  inert;
	int  inactive = (info->contrlHilite == inactiveHilite);

	rect_to_global(info, &g);
	vertical = scroll_is_vertical(&g);
	inert    = (info->contrlMax <= info->contrlMin);

	scroll_rects(info, &up, &down, &thumb, &pu, &pd);

	/* Track background: grey for the page zones, white for the thumb. */
	RGBForeColor(inactive ? &kCtrlGrey : &kCtrlGrey);
	PaintRect(&g);

	/* Frame around the whole bar. */
	RGBForeColor(inactive ? &kCtrlGrey : &kCtrlBlack);
	FrameRect(&g);

	/* Arrow caps. */
	RGBForeColor(&kCtrlWhite);
	PaintRect(&up);
	PaintRect(&down);
	RGBForeColor(inactive ? &kCtrlGrey : &kCtrlBlack);
	FrameRect(&up);
	FrameRect(&down);
	draw_arrow_glyph(up,   vertical, 0);
	draw_arrow_glyph(down, vertical, 1);

	/* Thumb — only when the scroll bar has a usable range. */
	if (!inert) {
		RGBForeColor(&kCtrlWhite);
		PaintRect(&thumb);
		RGBForeColor(inactive ? &kCtrlGrey : &kCtrlBlack);
		FrameRect(&thumb);
	}

	/* Disabled-hilite cue: the page zones flash hilite=1 during track —
	 * here we just leave the grey background; track_scrollbar paints the
	 * inverted highlight when the user clicks a zone. */
}

static void draw_pushbutton(const ControlRecord *info)
{
	Rect    g;
	short   tw, tx, ty;

	rect_to_global(info, &g);

	/* Background: white when normal, black when hilited. */
	RGBForeColor(info->contrlHilite == 1 ? &kCtrlBlack : &kCtrlWhite);
	PaintRect(&g);

	/* Frame. Disabled (255) gets a grey frame; otherwise black. */
	RGBForeColor(info->contrlHilite == inactiveHilite ? &kCtrlGrey
	                                                  : &kCtrlBlack);
	FrameRect(&g);

	/* Title centred. Inverted when hilited; greyed when disabled. */
	if (info->contrlTitle != NULL && *info->contrlTitle != NULL) {
		const unsigned char *t = (const unsigned char *)*info->contrlTitle;

		tw = (short)(t[0] * 6);                /* 8x8-fallback advance */
		tx = (short)((g.left + g.right - tw) / 2);
		ty = (short)((g.top + g.bottom) / 2 + 3);
		if (info->contrlHilite == 1)
			RGBForeColor(&kCtrlWhite);
		else if (info->contrlHilite == inactiveHilite)
			RGBForeColor(&kCtrlGrey);
		else
			RGBForeColor(&kCtrlBlack);
		MoveTo(tx, ty);
		DrawString((ConstStr255Param)t);
	}
}

static void draw_checkbox(const ControlRecord *info)
{
	Rect    g, box;
	short   ty;

	rect_to_global(info, &g);

	/* 12x12 box on the left, vertically centred. */
	box.top    = (short)((g.top + g.bottom) / 2 - 6);
	box.left   = (short)(g.left);
	box.bottom = (short)(box.top + 12);
	box.right  = (short)(box.left + 12);

	RGBForeColor(&kCtrlWhite);
	PaintRect(&box);
	RGBForeColor(info->contrlHilite == inactiveHilite ? &kCtrlGrey
	                                                  : &kCtrlBlack);
	FrameRect(&box);

	/* Tick: an X drawn corner to corner when value != 0. */
	if (info->contrlValue != 0) {
		MoveTo((short)(box.left + 2),     (short)(box.top + 2));
		LineTo((short)(box.right - 3),    (short)(box.bottom - 3));
		MoveTo((short)(box.right - 3),    (short)(box.top + 2));
		LineTo((short)(box.left + 2),     (short)(box.bottom - 3));
	}

	/* Title to the right of the box. */
	if (info->contrlTitle != NULL && *info->contrlTitle != NULL) {
		const unsigned char *t = (const unsigned char *)*info->contrlTitle;

		ty = (short)((g.top + g.bottom) / 2 + 3);
		if (info->contrlHilite == inactiveHilite)
			RGBForeColor(&kCtrlGrey);
		else
			RGBForeColor(&kCtrlBlack);
		MoveTo((short)(box.right + 4), ty);
		DrawString((ConstStr255Param)t);
	}

	/* Hilite frame around the whole control when pressed. */
	if (info->contrlHilite == 1) {
		RGBForeColor(&kCtrlBlack);
		FrameRect(&g);
	}
}

static void draw_radiobutton(const ControlRecord *info)
{
	Rect    g, circle, inner;
	short   ty;

	rect_to_global(info, &g);

	circle.top    = (short)((g.top + g.bottom) / 2 - 6);
	circle.left   = (short)(g.left);
	circle.bottom = (short)(circle.top + 12);
	circle.right  = (short)(circle.left + 12);

	RGBForeColor(&kCtrlWhite);
	PaintOval(&circle);
	RGBForeColor(info->contrlHilite == inactiveHilite ? &kCtrlGrey
	                                                  : &kCtrlBlack);
	FrameOval(&circle);

	if (info->contrlValue != 0) {
		inner.top    = (short)(circle.top + 3);
		inner.left   = (short)(circle.left + 3);
		inner.bottom = (short)(circle.bottom - 3);
		inner.right  = (short)(circle.right - 3);
		RGBForeColor(&kCtrlBlack);
		PaintOval(&inner);
	}

	if (info->contrlTitle != NULL && *info->contrlTitle != NULL) {
		const unsigned char *t = (const unsigned char *)*info->contrlTitle;

		ty = (short)((g.top + g.bottom) / 2 + 3);
		if (info->contrlHilite == inactiveHilite)
			RGBForeColor(&kCtrlGrey);
		else
			RGBForeColor(&kCtrlBlack);
		MoveTo((short)(circle.right + 4), ty);
		DrawString((ConstStr255Param)t);
	}

	if (info->contrlHilite == 1) {
		RGBForeColor(&kCtrlBlack);
		FrameRect(&g);
	}
}

void Draw1Control(ControlHandle c)
{
	GrafPtr        saved;
	ControlRecord *info;
	short          procID;

	if (c == NULL || *c == NULL)
		return;
	info = *c;
	if (info->contrlVis != 255 || info->contrlOwner == NULL)
		return;

	GetPort(&saved);
	SetPort(qd_screen_port());

	procID = (short)((long)info->contrlDefProc & 0xFF);
	switch (procID) {
	case pushButProc:    draw_pushbutton(info);   break;
	case checkBoxProc:   draw_checkbox(info);     break;
	case radioButProc:   draw_radiobutton(info);  break;
	case scrollBarProc:  draw_scrollbar(info);    break;
	default:             draw_pushbutton(info);   break;
	}
	SetPort(saved);
}

void DrawControls(WindowPtr w)
{
	ControlHandle c;

	if (w == NULL)
		return;
	for (c = WIN_CONTROL_LIST(w); c != NULL; c = (*c)->contrlNext)
		Draw1Control(c);
}

void UpdateControls(WindowPtr w, RgnHandle updateRgn)
{
	(void)updateRgn;
	DrawControls(w);        /* skeleton: redraw all */
}

/* --- hit-testing --------------------------------------------------- */

short TestControl(ControlHandle c, Point pt)
{
	short procID;

	if (c == NULL || *c == NULL)
		return 0;
	if ((*c)->contrlVis != 255 || (*c)->contrlHilite == inactiveHilite)
		return 0;
	if (!PtInRect(pt, &(*c)->contrlRect))
		return 0;

	procID = (short)((long)(*c)->contrlDefProc & 0xFF);
	switch (procID) {
	case pushButProc:    return inButton;
	case checkBoxProc:   return inCheckBox;
	case radioButProc:   return inCheckBox;
	case scrollBarProc: {
		Rect up, down, thumb, page_up, page_down, bar;
		Point global;
		WindowPeek wp = (WindowPeek)(*c)->contrlOwner;

		/* The caller passed `pt` in window-local coords; the scroll
		 * helpers compute global rects, so convert. */
		global = pt;
		if (wp != NULL && wp->contRgn != NULL) {
			global.h = (short)(pt.h + (*wp->contRgn)->rgnBBox.left);
			global.v = (short)(pt.v + (*wp->contRgn)->rgnBBox.top);
		}
		rect_to_global(*c, &bar);
		if (!PtInRect(global, &bar))
			return 0;
		scroll_rects(*c, &up, &down, &thumb, &page_up, &page_down);
		if (PtInRect(global, &up))         return inUpButton;
		if (PtInRect(global, &down))       return inDownButton;
		if ((*c)->contrlMax > (*c)->contrlMin) {
			if (PtInRect(global, &thumb))      return inThumb;
			if (PtInRect(global, &page_up))    return inPageUp;
			if (PtInRect(global, &page_down))  return inPageDown;
		}
		return 0;
	}
	default:             return inButton;
	}
}

short FindControl(Point pt, WindowPtr w, ControlHandle *theControl)
{
	ControlHandle c;
	short         part;
	Point         local = pt;

	if (theControl != NULL)
		*theControl = NULL;
	if (w == NULL)
		return 0;

	/* Convert global pt to window-local — the control rects are in
	 * window-local coords. */
	{
		WindowPeek wp = (WindowPeek)w;

		if (wp->contRgn != NULL) {
			local.h = (short)(pt.h - (*wp->contRgn)->rgnBBox.left);
			local.v = (short)(pt.v - (*wp->contRgn)->rgnBBox.top);
		}
	}

	for (c = WIN_CONTROL_LIST(w); c != NULL; c = (*c)->contrlNext) {
		part = TestControl(c, local);
		if (part != 0) {
			if (theControl != NULL)
				*theControl = c;
			return part;
		}
	}
	return 0;
}

/* Map a current mouse position (in global coords) to a scroll-bar
 * value, based on the thumb's position inside the track. */
static short scroll_value_from_pt(const ControlRecord *info, Point global)
{
	Rect  bar;
	int   vertical;
	short track_min, track_max, pos;
	long  span, frac_num;
	short value;

	rect_to_global(info, &bar);
	vertical = scroll_is_vertical(&bar);

	if (vertical) {
		track_min = (short)(bar.top + SCROLL_ARROW_SIZE);
		track_max = (short)(bar.bottom - SCROLL_ARROW_SIZE
		                              - SCROLL_ARROW_SIZE);
		pos       = (short)(global.v - track_min);
	} else {
		track_min = (short)(bar.left + SCROLL_ARROW_SIZE);
		track_max = (short)(bar.right - SCROLL_ARROW_SIZE
		                              - SCROLL_ARROW_SIZE);
		pos       = (short)(global.h - track_min);
	}
	if (pos < 0)
		pos = 0;
	if (pos > (track_max - track_min))
		pos = (short)(track_max - track_min);
	span     = (long)info->contrlMax - (long)info->contrlMin;
	frac_num = (long)pos * span;
	value    = (short)(info->contrlMin
	                  + frac_num / ((track_max - track_min) > 0
	                                ? (track_max - track_min) : 1));
	if (value < info->contrlMin) value = info->contrlMin;
	if (value > info->contrlMax) value = info->contrlMax;
	return value;
}

static short track_scrollbar(ControlHandle c, short part)
{
	Point pt, global;
	long  tick_end;

	switch (part) {
	case inUpButton:
	case inDownButton:
	case inPageUp:
	case inPageDown: {
		int   step = 0;

		switch (part) {
		case inUpButton:
			step = -1;
			break;
		case inDownButton:
			step = 1;
			break;
		case inPageUp:
			step = -((*c)->contrlMax - (*c)->contrlMin) / 8;
			if (step == 0)
				step = -1;
			break;
		case inPageDown:
			step = ((*c)->contrlMax - (*c)->contrlMin) / 8;
			if (step == 0)
				step = 1;
			break;
		}
		/* First step fires immediately; subsequent steps repeat every
		 * ~6 ticks (~100 ms) while held — matches the Mac's auto-repeat
		 * cadence well enough for the skeleton. */
		SetControlValue(c, (short)((*c)->contrlValue + step));
		qd_present();
		tick_end = TickCount() + 18;
		while (Button()) {
			if (TickCount() >= tick_end) {
				SetControlValue(c, (short)((*c)->contrlValue + step));
				qd_present();
				tick_end = TickCount() + 6;
			}
		}
		break;
	}
	case inThumb: {
		while (Button()) {
			GetMouse(&pt);
			global = pt;       /* Mac GetMouse returns global coords */
			SetControlValue(c, scroll_value_from_pt(*c, global));
			qd_present();
		}
		break;
	}
	default:
		break;
	}
	return part;
}

short TrackControl(ControlHandle c, Point startPt, void *actionProc)
{
	short part;
	Point pt;
	int   inside;
	int   prev_inside;
	short procID;

	(void)actionProc;
	if (c == NULL || *c == NULL)
		return 0;

	/* Convert startPt (global) to window-local for the rect test. */
	{
		WindowPeek wp = (WindowPeek)(*c)->contrlOwner;
		Point      local = startPt;

		if (wp != NULL && wp->contRgn != NULL) {
			local.h = (short)(startPt.h - (*wp->contRgn)->rgnBBox.left);
			local.v = (short)(startPt.v - (*wp->contRgn)->rgnBBox.top);
		}
		part = TestControl(c, local);
		if (part == 0)
			return 0;
		inside      = 1;
		prev_inside = 1;
	}

	procID = (short)((long)(*c)->contrlDefProc & 0xFF);
	if (procID == scrollBarProc)
		return track_scrollbar(c, part);

	HiliteControl(c, 1);
	qd_present();

	while (Button()) {
		Point      local;
		WindowPeek wp = (WindowPeek)(*c)->contrlOwner;

		GetMouse(&pt);
		local = pt;
		if (wp != NULL && wp->contRgn != NULL) {
			local.h = (short)(pt.h - (*wp->contRgn)->rgnBBox.left);
			local.v = (short)(pt.v - (*wp->contRgn)->rgnBBox.top);
		}
		inside = PtInRect(local, &(*c)->contrlRect);
		if (inside != prev_inside) {
			HiliteControl(c, (short)(inside ? 1 : 0));
			qd_present();
			prev_inside = inside;
		}
	}
	HiliteControl(c, 0);
	qd_present();
	return inside ? part : 0;
}
