/*
 * Mac Dialog Manager shim — Alert + ALRT/DITL readers. See dialogs.h.
 *
 * ALRT format (12 bytes):
 *   +0  8 bytes Rect      bounds (global screen coords)
 *   +8  short             DITL id
 *   +10 short             alert stage info (ignored for the first cut)
 *
 * DITL format:
 *   +0  short             item-count minus one
 *   +2  N items, each:
 *      +0  4 bytes        handle / proc placeholder (ignored)
 *      +4  8 bytes Rect   local rect (relative to dialog top-left)
 *      +12 byte           item type (low 7 bits; high bit = disabled)
 *      +13 byte           item data length
 *      +14 N bytes        item data (button title, text, etc.)
 *      pad to even byte
 *
 * Item types we care about now:
 *   0x04  standard button   — FrameRect + centred title
 *   0x08  static text       — DrawString at top-left + ascent
 *
 * Pictures, icons, edit-text and user items follow as the engine
 * surface demands them.
 *
 * The drawing path takes the global-coords shortcut the rest of the
 * shim uses: SetPort(screen_port), compute item rects as bounds + local,
 * paint into the screen pixmap. (Per-window offscreen pixmaps would let
 * us paint in local coords; that's a later refinement.)
 *
 * Modal dismissal is simplified — any keypress or mouse click ends the
 * alert and returns 1. Hit-testing each button to return its actual
 * item index waits for the engine to demand it.
 */

#include <stddef.h>             /* NULL */

#include "dialogs.h"
#include "events.h"
#include "macmemory.h"
#include "quickdraw.h"
#include "resources.h"
#include "windows.h"

static unsigned short be16(const unsigned char *p)
{
	return (unsigned short)(((unsigned)p[0] << 8) | p[1]);
}

short Alert(short alertID, void *filterProc)
{
	Handle               h_alrt, h_ditl;
	const unsigned char *a, *d;
	Rect                 bounds;
	short                ditl_id, count, i;
	long                 off, ditl_size;
	WindowPtr            w;
	GrafPtr              saved_port;

	(void)filterProc;
	h_alrt = GetResource(0x414C5254L /* 'ALRT' */, alertID);
	if (h_alrt == NULL || *h_alrt == NULL)
		return -1;
	if (GetHandleSize(h_alrt) < 10)
		return -1;
	a = (const unsigned char *)*h_alrt;
	bounds.top    = (short)be16(a + 0);
	bounds.left   = (short)be16(a + 2);
	bounds.bottom = (short)be16(a + 4);
	bounds.right  = (short)be16(a + 6);
	ditl_id       = (short)be16(a + 8);

	h_ditl = GetResource(0x4449544CL /* 'DITL' */, ditl_id);
	if (h_ditl == NULL || *h_ditl == NULL)
		return -1;
	ditl_size = GetHandleSize(h_ditl);
	if (ditl_size < 2)
		return -1;

	w = NewCWindow(NULL, &bounds, (ConstStr255Param)"",
	               1, 0, (WindowPtr)-1L, 0, 0);
	if (w == NULL)
		return -1;

	/* Draw into the screen port — see comment at top. */
	GetPort(&saved_port);
	SetPort(qd_screen_port());

	d = (const unsigned char *)*h_ditl;
	count = (short)(be16(d + 0) + 1);
	off = 2;
	for (i = 0; i < count && off + 14 <= ditl_size; i++) {
		Rect          grect;
		short         itop, ileft, ibot, irght;
		unsigned char itype, ilen;

		itop  = (short)be16(d + off + 4);
		ileft = (short)be16(d + off + 6);
		ibot  = (short)be16(d + off + 8);
		irght = (short)be16(d + off + 10);
		itype = d[off + 12];
		ilen  = d[off + 13];

		grect.top    = (short)(bounds.top  + itop);
		grect.left   = (short)(bounds.left + ileft);
		grect.bottom = (short)(bounds.top  + ibot);
		grect.right  = (short)(bounds.left + irght);

		switch (itype & 0x7F) {
		case 4: {                                  /* standard button */
			short tw = (short)(ilen * 8);
			short tx = (short)((grect.left + grect.right - tw) / 2);
			short ty = (short)((grect.top + grect.bottom) / 2 + 3);
			RGBColor black = { 0, 0, 0 };

			RGBForeColor(&black);
			FrameRect(&grect);
			MoveTo(tx, ty);
			DrawString((ConstStr255Param)(d + off + 13));
			break;
		}
		case 8: {                                  /* static text */
			RGBColor black = { 0, 0, 0 };

			RGBForeColor(&black);
			MoveTo(grect.left, (short)(grect.top + 7));
			DrawString((ConstStr255Param)(d + off + 13));
			break;
		}
		default:
			break;
		}

		off += 14 + ilen;
		if ((ilen & 1) == 1)
			off++;                              /* word-align */
	}

	qd_present();

	/* Modal dismissal: any keypress or mouse click ends the alert. */
	{
		EventRecord e;

		for (;;) {
			if (!WaitNextEvent(everyEvent, &e, 1, NULL))
				continue;
			if (e.what == keyDown || e.what == mouseDown)
				break;
		}
	}

	SetPort(saved_port);
	DisposeWindow(w);
	return 1;
}
