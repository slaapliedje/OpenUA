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

void dbg_log_num(const char *label, long value)
{
	char          buf[12];
	short         i = 11;
	short         neg = (value < 0);
	unsigned long u = neg ? (unsigned long)-value : (unsigned long)value;

	buf[i--] = '\0';
	do {
		buf[i--] = (char)('0' + (short)(u % 10));
		u /= 10;
	} while (u != 0);
	if (neg)
		buf[i--] = '-';

	Cconws(label);
	Cconws(&buf[i + 1]);
	Cconws("\r\n");
}
