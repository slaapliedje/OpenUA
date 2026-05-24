/*
 * FRUA — Atari Falcon030 / TT030 port.
 *
 * The launcher's job: bring the platform HALs up (display, input), bind
 * the QuickDraw shim to the display back buffer, and hand control to
 * ua_main — the lifted FRUA application entry (src/engine/boot.c). The
 * shim's manager inits run inside master_init's toolbox_init() prologue.
 *
 * Once ua_main returns, the shim's input vector and the display mode
 * are restored. The probe-style logging through dbg_log mirrors via
 * Hatari's --conout 2 so the bring-up trail survives a crash.
 */

#include <mint/osbind.h>
#include <stddef.h>             /* NULL    */
#include <string.h>             /* memset  */

#include "engine/boot.h"        /* ua_main */
#include "dbglog.h"
#include "display.h"
#include "files.h"
#include "input.h"
#include "macmemory.h"
#include "events.h"
#include "quickdraw.h"
#include "resources.h"
#include "windows.h"

/*
 * Open frua.rsrc through the File Manager and hand the bytes to the
 * Resource Manager shim. Silent no-op when the file isn't there (the
 * engine runs with an empty archive — GetResource just returns NULL).
 * The buffer must outlive the engine, so it leaks on purpose; we exit
 * shortly after ua_main returns.
 */
static void load_frua_rsrc(void)
{
	short  ref;
	long   size, n;
	void  *buf;
	OSErr  err;

	if (FSOpen((ConstStr255Param)"\011frua.rsrc", 0, &ref) != noErr) {
		dbg_log("main: frua.rsrc not found (running without resources)");
		return;
	}
	err = GetEOF(ref, &size);
	if (err != noErr || size <= 0) {
		(void)FSClose(ref);
		dbg_log("main: frua.rsrc unreadable");
		return;
	}
	buf = NewPtr(size);
	if (buf == NULL) {
		(void)FSClose(ref);
		dbg_log("main: NewPtr failed for frua.rsrc");
		return;
	}
	n = size;
	err = FSRead(ref, &n, buf);
	(void)FSClose(ref);
	if (err != noErr || n != size) {
		dbg_log("main: FSRead frua.rsrc short");
		return;
	}
	if (resource_open(buf) != 0) {
		dbg_log("main: frua.rsrc isn't a FRSC archive");
		return;
	}
	dbg_log_num("main: frua.rsrc loaded, bytes = ", size);
}

/*
 * Install the 256-entry CLUT from clut 129 — FRUA's full palette. The Mac
 * clut format is an 8-byte header (ctSeed, ctFlags, ctSize = count-1)
 * followed by N ColorSpec entries of 8 bytes each (value short, then
 * red / green / blue shorts in big-endian 0..65535). The value field is
 * the palette index, so a sparse CLUT lands in the right slots; missing
 * slots stay black.
 *
 * Resource Manager must already be open (load_frua_rsrc) and the screen
 * port must already be attached (qd_attach_screen) before this runs.
 */
static void load_frua_palette(void)
{
	static RGBColor      pal[256];
	Handle               h;
	const unsigned char *p;
	long                 size;
	short                ct_size, n_entries, i;

	h = GetResource(0x636c7574L /* 'clut' */, 129);
	if (h == NULL || *h == NULL) {
		dbg_log("main: clut 129 missing");
		return;
	}
	size = GetHandleSize(h);
	if (size < 8) {
		dbg_log("main: clut 129 truncated");
		return;
	}
	p = (const unsigned char *)*h;
	ct_size = (short)(((unsigned)p[6] << 8) | p[7]);
	n_entries = (short)(ct_size + 1);
	if (n_entries < 0 || n_entries > 256)
		n_entries = 256;

	memset(pal, 0, sizeof pal);
	for (i = 0; i < n_entries; i++) {
		const unsigned char *e   = p + 8 + (long)i * 8;
		unsigned short       val;

		if (8 + (long)(i + 1) * 8 > size)
			break;                          /* truncated */
		val = (unsigned short)((e[0] << 8) | e[1]);
		if (val > 255)
			continue;
		pal[val].red   = (unsigned short)((e[2] << 8) | e[3]);
		pal[val].green = (unsigned short)((e[4] << 8) | e[5]);
		pal[val].blue  = (unsigned short)((e[6] << 8) | e[7]);
	}
	qd_set_palette(pal, 0, 256);
	dbg_log_num("main: clut 129 installed, entries = ", n_entries);
}

int main(void)
{
	const dsp_backend_t *dsp;
	dsp_surface_t       *surf;
	int                  rc;

	dbg_log("main: entered");

	dsp = dsp_detect();
	dbg_log(dsp->name);
	if (dsp->init(320, 240) != 0) {
		dbg_log("main: display init failed");
		return 1;
	}
	surf = dsp->surface();
	dbg_log("main: display up");

	qd_attach_screen(surf->pixels, surf->pitch, surf->width, surf->height);
	plat_input_init(surf->width, surf->height);
	dbg_log("main: shim up");

	load_frua_rsrc();
	load_frua_palette();

	/* Synthetic string-table stand-in until the THINK C runtime's
	 * DATA + DREL string-pool replay is lifted. Indices 0..4 cover the
	 * indices ua_main's phase-5 string checks reach (2 and 3); index 2
	 * is "Heart", the constant the engine compares against. */
	{
		static char *const strtab[] = {
			(char *)"",
			(char *)"",
			(char *)"Heart",
			(char *)"",
			(char *)"",
		};
		rc = ua_main((short)(sizeof strtab / sizeof strtab[0]),
		             (long)(void *)strtab);
	}
	dbg_log_num("main: ua_main rc = ", (long)rc);

	/* Open WIND 1 / 2 / 3 from the real resource fork and stack them.
	 * All three share the same bounds in the resource, so MoveWindow
	 * offsets each by a different amount to make the layering visible.
	 * vis=0 in every WIND, so ShowWindow has to fire explicitly; we
	 * paint back-to-front (w1 first) so the painted z-order matches
	 * the window-list z-order (w3 frontmost). */
	{
		WindowPtr w1, w2, w3;
		Rect      full;

		SetRect(&full, 0, 0, surf->width, surf->height);
		EraseRect(&full);

		w1 = GetNewCWindow(1, NULL, (WindowPtr)-1L);
		w2 = GetNewCWindow(2, NULL, (WindowPtr)-1L);
		w3 = GetNewCWindow(3, NULL, (WindowPtr)-1L);
		if (w1) MoveWindow(w1,   8,  50, 0);
		if (w2) MoveWindow(w2,  40,  90, 0);
		if (w3) MoveWindow(w3,  72, 130, 0);
		if (w1) ShowWindow(w1);
		if (w2) ShowWindow(w2);
		if (w3) ShowWindow(w3);
		if (w3) SelectWindow(w3);          /* w3 active, w1/w2 inactive */
		dbg_log("main: WIND 1/2/3 stacked");
	}
	dsp->present();

	/* Interactive loop: WaitNextEvent at ~60 Hz. mouseDown promotes the
	 * clicked window via FindWindow + SelectWindow (active stripe goes
	 * with it). keyDown exits. The whole chain — IKBD packet handler →
	 * Event Manager edge synthesis → FindWindow z-order walk →
	 * SelectWindow's repaint via win_draw_frame — runs through real
	 * shim code; the user's clicks reorder the cascade. */
	{
		EventRecord e;
		Boolean     done = 0;

		while (!done) {
			if (!WaitNextEvent(everyEvent, &e, 1, NULL))
				continue;
			switch (e.what) {
			case mouseDown: {
				WindowPtr hit = NULL;
				short     part = FindWindow(e.where, &hit);

				if (hit != NULL && hit != FrontWindow()
				 && (part == inContent || part == inDrag))
					SelectWindow(hit);
				dsp->present();
				break;
			}
			case keyDown:
				done = 1;
				break;
			default:
				break;
			}
		}
	}

	plat_input_shutdown();
	dsp->shutdown();
	dbg_log("main: shutdown ok");
	return rc;
}
