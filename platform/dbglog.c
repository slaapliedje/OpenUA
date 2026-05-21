/*
 * Debug log — see dbglog.h.
 *
 * Writes to the VT-52 console; with Hatari's `--conout 2` the line is
 * mirrored to the host terminal, so the trail is captured even when the
 * program crashes too fast to read on screen.
 */

#include <mint/osbind.h>

#include "dbglog.h"

void dbg_log(const char *msg)
{
	Cconws(msg);
	Cconws("\r\n");
}
