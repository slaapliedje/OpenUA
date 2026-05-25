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
#include "dialogs.h"
#include "events.h"
#include "mac_font.h"
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
	qd_set_present(dsp->present);
	plat_input_init(surf->width, surf->height);
	dbg_log("main: shim up");

	load_frua_rsrc();
	load_frua_palette();
	if (mac_font_load(-27001) == 0)
		dbg_log("main: FONT -27001 loaded");
	else
		dbg_log("main: FONT -27001 unavailable (fallback to 8x8)");

	/* Build a 256-entry string table whose entries point into the real
	 * STRS resource. Without the DATA + DREL replay we don't have the
	 * full index → STRS-offset map the THINK C runtime would compute,
	 * so this is a hand-curated stopgap: scan STRS for the constants
	 * the lifted engine code compares against and stash their pointers
	 * at the indices that engine code reads. The remaining slots are
	 * empty strings (ua_get_string falls back to "" on a NULL slot). */
	{
		static char  *strtab[256];
		Handle        h_strs;
		const char   *strs_base = NULL;
		long          strs_size = 0;
		short         i;

		for (i = 0; i < 256; i++)
			strtab[i] = (char *)"";

		h_strs = GetResource(0x53545253L /* 'STRS' */, 0);
		if (h_strs != NULL && *h_strs != NULL) {
			strs_base = (const char *)*h_strs;
			strs_size = GetHandleSize(h_strs);
		}
		if (strs_base != NULL) {
			long off;

			for (off = 0; off + 6 <= strs_size; off++) {
				if (strs_base[off]     == 'H'
				 && strs_base[off + 1] == 'e'
				 && strs_base[off + 2] == 'a'
				 && strs_base[off + 3] == 'r'
				 && strs_base[off + 4] == 't'
				 && strs_base[off + 5] == '\0') {
					strtab[2] = (char *)(strs_base + off);
					dbg_log_num("main: STRS \"Heart\" at off = ", off);
					break;
				}
			}
		}
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

	/* Splash ALRT 200 from the real fork — "A disk error occurred!".
	 * Modal: paints the dialog + OK button + static text, waits for
	 * any keypress / mouse click to dismiss. Most of the text falls
	 * back to the hollow-box glyph until the font is extended. */
	(void)Alert(200, NULL);
	dbg_log("main: ALRT 200 dismissed");

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

				if (hit != NULL && (part == inContent
				                 || part == inDrag)
				 && hit != FrontWindow())
					SelectWindow(hit);
				if (hit != NULL && part == inDrag) {
					Rect drag_bounds;

					SetRect(&drag_bounds, 0, 15,
					        surf->width,
					        (short)(surf->height - 1));
					DragWindow(hit, e.where, &drag_bounds);
				}
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
