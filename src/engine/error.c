/*
 * Engine error reporting.
 *
 * The Mac build's error reporter (CODE 5, jump-table entry 1084) draws an
 * on-screen error box. That path depends on the display layer, which does
 * not exist yet — so this first implementation simply formats the message
 * and writes it to the TOS console. It will grow the on-screen box once the
 * platform display HAL is in place.
 */

#include <stdarg.h>
#include <stdio.h>
#include <mint/osbind.h>

#include "error.h"

void ua_error(const char *fmt, ...)
{
	char    msg[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);

	Cconws(msg);
	Cconws("\r\n");
}
