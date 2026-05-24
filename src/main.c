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
#include <stddef.h>             /* NULL */

#include "engine/boot.h"        /* ua_main */
#include "dbglog.h"
#include "display.h"
#include "files.h"
#include "input.h"
#include "macmemory.h"
#include "quickdraw.h"
#include "resources.h"

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

	Crawcin();                                      /* hold until a keypress */

	plat_input_shutdown();
	dsp->shutdown();
	dbg_log("main: shutdown ok");
	return rc;
}
