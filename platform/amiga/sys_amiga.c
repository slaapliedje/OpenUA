/*
 * System HAL — Amiga (exec/dos.library) backend (ADR-0012). See plat_sys.h.
 *
 * SCAFFOLD STATUS: the bodies use real exec/dos calls, but are UNVERIFIED on
 * hardware / amiberry — the AGA backends land together once the toolchain run
 * is green (docs/toolchain-amiga.md). The Falcon twin (platform/sys_falcon.c)
 * is the reference for the intended behaviour.
 *
 *   - console  -> dos.library PutStr, to the shell's Output() stream.
 *   - free mem -> exec AvailMem(MEMF_LARGEST).
 *   - clock    -> exec DateStamp + dos.library Amiga2Date (calendar fields).
 */

#include "plat_sys.h"

#ifdef FRUA_AMIGA

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/datetime.h>
#include <proto/exec.h>
#include <proto/dos.h>

void plat_console_puts(const char *s)
{
	if (s != NULL)
		PutStr((CONST_STRPTR)s);
}

int plat_console_getc(void)
{
	/* Bring-up pause only; the AGA target has no interactive console yet. */
	return 0;
}

unsigned long plat_mem_largest_free(void)
{
	return (unsigned long)AvailMem(MEMF_ANY | MEMF_LARGEST);
}

void plat_get_datetime(struct plat_datetime *out)
{
	struct DateStamp ds;
	struct ClockData cd;
	unsigned long    secs;

	if (out == NULL)
		return;
	DateStamp(&ds);                         /* days/min/tick since 1978-01-01 */
	secs = (unsigned long)ds.ds_Days * 86400UL
	     + (unsigned long)ds.ds_Minute * 60UL
	     + (unsigned long)ds.ds_Tick / TICKS_PER_SECOND;
	Amiga2Date(secs, &cd);                  /* -> broken-down calendar fields */
	out->year   = (int)cd.year;
	out->month  = (int)cd.month;
	out->day    = (int)cd.mday;
	out->hour   = (int)cd.hour;
	out->minute = (int)cd.min;
	out->second = (int)cd.sec;
}

#endif /* FRUA_AMIGA */
