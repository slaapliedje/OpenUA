/*
 * System HAL — Falcon030 / TT030 (GEMDOS) backend. See plat_sys.h.
 *
 * The three primitives map straight onto GEMDOS: Cconws / Cnecin for the
 * console, Malloc(-1) for the largest free block, and Tgetdate / Tgettime for
 * the wall clock. This is the sole place engine/shim reach the console clock
 * and the free-memory figure now that those callers route through the HAL.
 */

#include <stddef.h>             /* NULL */
#include <mint/osbind.h>

#include "plat_sys.h"

void plat_console_puts(const char *s)
{
	if (s != NULL)
		Cconws((char *)s);
}

int plat_console_getc(void)
{
	return (int)Cnecin();
}

/*
 * The Mac Memory Manager's _FreeMem reports total free heap; the Atari
 * stand-in returns the largest free block (GEMDOS Malloc(-1)), which is the
 * more useful figure for sizing a single large allocation.
 */
unsigned long plat_mem_largest_free(void)
{
	return (unsigned long)Malloc(-1L);
}

void plat_get_datetime(struct plat_datetime *out)
{
	unsigned int d = (unsigned int)Tgetdate();   /* year-1980<<9 | mon<<5 | day */
	unsigned int t = (unsigned int)Tgettime();   /* hour<<11 | min<<5 | sec/2   */

	if (out == NULL)
		return;
	out->year   = 1980 + (int)((d >> 9) & 0x7f);
	out->month  = (int)((d >> 5) & 0x0f);        /* 1..12 */
	out->day    = (int)(d & 0x1f);               /* 1..31 */
	out->hour   = (int)((t >> 11) & 0x1f);
	out->minute = (int)((t >> 5) & 0x3f);
	out->second = (int)((t & 0x1f) * 2);
}

/* Native-planar blit acceleration (ADR-0016). XBIOS Blitmode(-1) returns the
 * current config word without changing it; bit 1 = the BLiTTER hardware is
 * present (STe/Mega ST always; the plain ST had an optional socket). The call
 * exists on TOS >= 1.2, which every ST/STe/Falcon TOS we boot satisfies. The
 * Falcon/TT report their blitter too, harmless — those targets stay chunky. */
int plat_have_blitter(void)
{
	return (Blitmode(-1) & 0x0002) != 0;
}
