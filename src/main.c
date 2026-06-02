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
#include "engine/data_pool_replay.h"
#include "dbglog.h"
#include "display.h"
#include "files.h"
#include "input.h"
#include "plat_sound.h"
#include "macmemory.h"
#include "controls.h"
#include "dialogs.h"
#include "events.h"
#include "mac_font.h"
#include "menus.h"
#include "quickdraw.h"
#include "resources.h"
#include "windows.h"

/*
 * Pop up DLOG `id`, run the modal loop, return the dismissing item
 * (1-based) or 0 if the dialog couldn't be loaded. The OK / Cancel
 * choice flows back to the caller for action.
 */
static short show_dialog(short id)
{
	DialogPtr d = GetNewDialog(id, NULL, (WindowPtr)-1L);
	short     item = 0;

	if (d == NULL)
		return 0;
	ModalDialog(NULL, &item);
	DisposeDialog(d);
	return item;
}

/*
 * Stand-alone Mac Control Manager demo — opens a fresh window, drops
 * a push button + a checkbox + two radio buttons + an OK button into
 * it via NewControl, then runs a small event loop that routes
 * mouseDown to FindControl / TrackControl. The checkbox toggles its
 * value on click; the radios act as a group (clicking one clears the
 * other). The OK push button (refCon == 1) dismisses the demo.
 */
static void show_controls_demo(short surf_w, short surf_h)
{
	Rect          bounds, r;
	WindowPtr     w;
	ControlHandle b_ok, cb, r1, r2, sb;
	EventRecord   ev;
	Boolean       done = 0;

	SetRect(&bounds,
	        (short)((surf_w - 240) / 2),
	        (short)((surf_h - 140) / 2),
	        (short)((surf_w - 240) / 2 + 240),
	        (short)((surf_h - 140) / 2 + 140));
	w = NewCWindow(NULL, &bounds, (ConstStr255Param)"\010Controls",
	               1, 1, (WindowPtr)-1L, 0, 0);
	if (w == NULL)
		return;

	SetRect(&r, 18, 20, 90, 36);
	cb = NewControl(w, &r, (ConstStr255Param)"\005Check", 1, 0, 0, 1,
	                checkBoxProc, 0);

	SetRect(&r, 18, 42, 90, 58);
	r1 = NewControl(w, &r, (ConstStr255Param)"\004One",   1, 1, 0, 1,
	                radioButProc, 1);
	SetRect(&r, 18, 60, 90, 76);
	r2 = NewControl(w, &r, (ConstStr255Param)"\004Two",   1, 0, 0, 1,
	                radioButProc, 2);
	SetRect(&r, 130, 96, 220, 120);
	b_ok = NewControl(w, &r, (ConstStr255Param)"\002OK",  1, 0, 0, 1,
	                  pushButProc, 1);
	(void)b_ok;

	/* Vertical scroll bar on the right side — value 5 of 0..10. */
	SetRect(&r, 210, 20, 226, 90);
	sb = NewControl(w, &r, (ConstStr255Param)"", 1, 5, 0, 10,
	                scrollBarProc, 0);

	DrawControls(w);
	qd_present();
	dbg_log("main: controls demo opened");

	while (!done) {
		if (!WaitNextEvent(everyEvent, &ev, 1, NULL))
			continue;
		if (ev.what == keyDown) {
			done = 1;
			break;
		}
		if (ev.what == mouseDown) {
			ControlHandle hit = NULL;
			short         part = FindControl(ev.where, w, &hit);

			if (hit == NULL) {
				/* outside any control — dismiss */
				done = 1;
				break;
			}
			if (TrackControl(hit, ev.where, NULL) != 0) {
				if (hit == cb) {
					SetControlValue(cb, (short)
					    (GetControlValue(cb) == 0 ? 1 : 0));
				} else if (hit == r1) {
					SetControlValue(r1, 1);
					SetControlValue(r2, 0);
				} else if (hit == r2) {
					SetControlValue(r1, 0);
					SetControlValue(r2, 1);
				} else if (hit == sb) {
					/* TrackControl already updated the value
					 * during drag — log it for the trace. */
					dbg_log_num("main: scroll value = ",
					            (long)GetControlValue(sb));
				} else if (GetControlReference(hit) == 1) {
					/* OK push button — dismiss */
					done = 1;
				}
			}
			(void)part;
		}
	}
	dbg_log_num("main: controls demo cb value = ",
	            (long)GetControlValue(cb));
	dbg_log_num("main: controls demo r1 value = ",
	            (long)GetControlValue(r1));
	DisposeWindow(w);
}

/*
 * Synthetic "enter name" modal — proves the Dialog Manager's editText
 * path end to end without depending on a FRUA-shipped DITL. Builds a
 * three-item DITL in memory (static text label, edit field, OK button),
 * runs ModalDialog so the user can type / Tab / backspace, then logs
 * the entered string via dbg_log.
 */
static void show_name_prompt(short surf_w, short surf_h)
{
	static unsigned char ditl[] = {
		0x00, 0x02,                                     /* item count - 1 = 2 → 3 items */

		/* item 1: OK button — type 0x04, "OK", local (60, 100, 80, 160) */
		0,0,0,0,
		0x00, 60, 0x00, 100, 0x00, 80, 0x00, (unsigned char)160,
		0x04, 2, 'O', 'K',

		/* item 2: static text "Enter your name:", local (10, 10, 26, 200) */
		0,0,0,0,
		0x00, 10, 0x00, 10, 0x00, 26, 0x00, (unsigned char)200,
		0x88, 16,
		'E','n','t','e','r',' ','y','o','u','r',' ','n','a','m','e',':',

		/* item 3: edit text, local (30, 10, 50, 200) */
		0,0,0,0,
		0x00, 30, 0x00, 10, 0x00, 50, 0x00, (unsigned char)200,
		0x10, 0,
	};
	Handle        h_ditl;
	Rect          bounds;
	DialogPtr     d;
	short         item = 0;
	unsigned char name[256];

	if (PtrToHand(ditl, &h_ditl, (Size)sizeof ditl) != noErr)
		return;
	SetRect(&bounds,
	        (short)((surf_w - 220) / 2),
	        (short)((surf_h - 90)  / 2),
	        (short)((surf_w - 220) / 2 + 220),
	        (short)((surf_h - 90)  / 2 + 90));
	d = NewDialog(NULL, &bounds, (ConstStr255Param)"\012Enter name",
	              1, 1, (WindowPtr)-1L, 0, 0, h_ditl);
	if (d == NULL) {
		DisposeHandle(h_ditl);
		return;
	}
	ModalDialog(NULL, &item);
	dialog_get_edit_text(d, 3, name);
	dbg_log_num("main: name length = ", (long)name[0]);
	if (name[0] > 0) {
		/* dbg_log expects a C string; copy + nul-terminate. */
		char buf[260];
		memcpy(buf, name + 1, (size_t)name[0]);
		buf[name[0]] = 0;
		dbg_log(buf);
	}
	DisposeDialog(d);
	DisposeHandle(h_ditl);
}

/*
 * Open frua.rsc through the File Manager and hand the bytes to the
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

	if (FSOpen((ConstStr255Param)"\010frua.rsc", 0, &ref) != noErr) {
		dbg_log("main: frua.rsc not found (running without resources)");
		return;
	}
	err = GetEOF(ref, &size);
	if (err != noErr || size <= 0) {
		(void)FSClose(ref);
		dbg_log("main: frua.rsc unreadable");
		return;
	}
	buf = NewPtr(size);
	if (buf == NULL) {
		(void)FSClose(ref);
		dbg_log("main: NewPtr failed for frua.rsc");
		return;
	}
	n = size;
	err = FSRead(ref, &n, buf);
	(void)FSClose(ref);
	if (err != noErr || n != size) {
		dbg_log("main: FSRead frua.rsc short");
		return;
	}
	if (resource_open(buf) != 0) {
		dbg_log("main: frua.rsc isn't a FRSC archive");
		return;
	}
	dbg_log_num("main: frua.rsc loaded, bytes = ", size);
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
/* Non-static: the engine (jt315) re-installs this on menu redraw, since the
 * dungeon play loop (port_play_demo) overwrites clut 0..15 and doesn't
 * restore them — the menu would otherwise paint with the dungeon palette. */
void load_frua_palette(void)
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
	qd_set_present_rect(dsp->present_rect);   /* NULL-safe: falls back */
	plat_input_init(surf->width, surf->height);
	if (plat_sound_init() == 0)
		dbg_log("main: sound chip locked");
	else
		dbg_log("main: sound init failed (continuing silent)");
	dbg_log("main: shim up");

	load_frua_rsrc();
	data_pool_replay();
	boot_a5_seed_defaults();
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
	dsp->present();
	dbg_log("main: snapshot after ua_main");
	(void)Cnecin();

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

#ifdef FRUA_SPLASH_ALERT
	/* Splash ALRT 200 from the real fork — "A disk error occurred!".
	 * Modal: paints the dialog + OK button + static text, waits for
	 * any keypress / mouse click to dismiss. Most of the text falls
	 * back to the hollow-box glyph until the font is extended.
	 *
	 * Off by default — opt in with `make FRUA_SPLASH_ALERT=1 run`.
	 * The default boot lands on the menu bar so engine bring-up
	 * isn't gated behind dismissing the alert by hand. */
	(void)Alert(200, NULL);
	dbg_log("main: ALRT 200 dismissed");
#endif

	/* Build a small menu bar so the user can drop a menu down and the
	 * event loop has somewhere to send inMenuBar clicks. NewMenu /
	 * AppendMenu / InsertMenu come from the Menu Manager skeleton in
	 * compat/menus.c; DrawMenuBar paints across the top of the screen
	 * port. The File menu's "Quit" item is the loop's exit path
	 * alongside any keyDown. */
	{
		MenuHandle m_file = NewMenu(128, (ConstStr255Param)"\004File");
		MenuHandle m_edit = NewMenu(129, (ConstStr255Param)"\004Edit");

		/* Items with '/' set a Cmd-key equivalent — MenuKey scans
		 * for these on cmdKey-modified keyDowns. File's About
		 * (item 2) opens DLOG 201; Enter name (item 4) demos the
		 * Dialog Manager's edit-text path with a synthetic DITL;
		 * Quit (item 6) opens DLOG 202 and only exits on OK. */
		if (m_file != NULL) {
			AppendMenu(m_file, (ConstStr255Param)
			           "\072New/N;About FRUA;Open/O;Enter name...;Controls...;-;Quit/Q");
			InsertMenu(m_file, 0);
		}
		if (m_edit != NULL) {
			AppendMenu(m_edit, (ConstStr255Param)
			           "\023Undo/Z;Cut/X;Copy/C");
			InsertMenu(m_edit, 0);
		}
		DrawMenuBar();
		dsp->present();
		dbg_log("main: menu bar drawn");
	}

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

				if (part == inMenuBar) {
					long sel = MenuSelect(e.where);

					if (sel != 0) {
						short id   = (short)((sel >> 16) & 0xFFFF);
						short item = (short)(sel & 0xFFFF);

						dbg_log_num("main: menu id  = ", id);
						dbg_log_num("main: menu it  = ", item);
						if (id == 128 && item == 2)
							(void)show_dialog(201);
						if (id == 128 && item == 4)
							show_name_prompt(surf->width,
							                 surf->height);
						if (id == 128 && item == 5)
							show_controls_demo(surf->width,
							                   surf->height);
						/* File menu, "Quit" = item 7. */
						if (id == 128 && item == 7
						 && show_dialog(202) == 2)
							done = 1;
						DrawMenuBar();
					}
					dsp->present();
					break;
				}
				if (hit != NULL && (part == inContent
				                 || part == inDrag)
				 && hit != FrontWindow())
					SelectWindow(hit);
				if (hit != NULL && part == inDrag) {
					Rect drag_bounds;

					SetRect(&drag_bounds, 0,
					        (short)(menubar_height() + 1),
					        surf->width,
					        (short)(surf->height - 1));
					DragWindow(hit, e.where, &drag_bounds);
				}
				dsp->present();
				break;
			}
			case keyDown: {
				/* Cmd-key chords go to the menu bar; bare keys
				 * exit the demo. The Mac packs the keyDown
				 * message as (scan << 8) | ASCII, so the typed
				 * character is the low byte. */
				if (e.modifiers & cmdKey) {
					short ch  = (short)(e.message & 0xFF);
					long  sel = MenuKey(ch);

					if (sel != 0) {
						short id   = (short)((sel >> 16) & 0xFFFF);
						short item = (short)(sel & 0xFFFF);

						dbg_log_num("main: cmd-key menu id = ", id);
						dbg_log_num("main: cmd-key item    = ", item);
						if (id == 128 && item == 2)
							(void)show_dialog(201);
						if (id == 128 && item == 4)
							show_name_prompt(surf->width,
							                 surf->height);
						if (id == 128 && item == 5)
							show_controls_demo(surf->width,
							                   surf->height);
						if (id == 128 && item == 7
						 && show_dialog(202) == 2)
							done = 1;
						DrawMenuBar();
					}
					dsp->present();
				} else {
					done = 1;
				}
				break;
			}
			default:
				break;
			}
		}
	}

	plat_sound_shutdown();
	plat_input_shutdown();
	dsp->shutdown();
	dbg_log("main: shutdown ok");
	return rc;
}
