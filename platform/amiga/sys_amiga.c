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
 *   - clock    -> dos.library DateStamp, converted to calendar fields here.
 *
 * Only exec + dos.library are used — both auto-opened by the libnix/newlib
 * startup — so nothing here needs an extra library base. (The obvious
 * utility.library Amiga2Date would need UtilityBase; the by-hand epoch walk
 * below keeps the backend self-contained, matching the Falcon twin's math.)
 */

#include "plat_sys.h"

#ifdef FRUA_AMIGA

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
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

static int amiga_is_leap(int y)
{
	return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

void plat_get_datetime(struct plat_datetime *out)
{
	static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
	struct DateStamp ds;
	long days;
	int  y = 1978, mo;      /* the Amiga epoch is 1978-01-01 */

	if (out == NULL)
		return;
	DateStamp(&ds);         /* ds_Days since 1978-01-01, ds_Minute, ds_Tick */

	days = ds.ds_Days;
	for (;;) {
		long dy = amiga_is_leap(y) ? 366 : 365;
		if (days < dy)
			break;
		days -= dy;
		y++;
	}
	for (mo = 0; mo < 11; mo++) {
		long dm = mdays[mo] + ((mo == 1 && amiga_is_leap(y)) ? 1 : 0);
		if (days < dm)
			break;
		days -= dm;
	}

	out->year   = y;
	out->month  = mo + 1;
	out->day    = (int)days + 1;
	out->hour   = (int)(ds.ds_Minute / 60);
	out->minute = (int)(ds.ds_Minute % 60);
	out->second = (int)(ds.ds_Tick / TICKS_PER_SECOND);
}

#endif /* FRUA_AMIGA */
