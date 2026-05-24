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

#include "engine/boot.h"        /* ua_main */
#include "dbglog.h"
#include "display.h"
#include "input.h"
#include "quickdraw.h"

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

	rc = ua_main(0, 0L);
	dbg_log_num("main: ua_main rc = ", (long)rc);

	Crawcin();                                      /* hold until a keypress */

	plat_input_shutdown();
	dsp->shutdown();
	dbg_log("main: shutdown ok");
	return rc;
}
