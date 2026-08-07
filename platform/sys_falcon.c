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

/*
 * ST-RAM allocation, portable across every TOS this engine can boot on.
 *
 * ★ Mxalloc DOES NOT EXIST BEFORE TOS 2.01, AND ITS ABSENCE IS NOT SAFE.
 * The Compendium's Sversion table pins the GEMDOS revisions exactly:
 *
 *     0.13  TOS 1.00, 1.02        0.15  TOS 1.04, 1.06
 *     0.17  TOS 1.62              0.19  TOS 2.01, 2.05, 2.06, 3.0x
 *     0.30  TOS 4.0x
 *
 * and Mxalloc (GEMDOS 0x44) is "available from GEMDOS version 0.19". So it is
 * missing on EVERY plain-ST ROM and on TOS 1.62 — the ROM most STes actually
 * shipped with. An unimplemented GEMDOS opcode returns EINVFN (-32), which is
 * NOT NULL, so a `== NULL` check waves it through and the caller then memsets
 * through a pointer of 0xFFFFFFE0. Observed live: frua on an emulated ST with
 * TOS 1.04 took a double bus error inside st_init, immediately after the
 * backend logged its name and before it could log anything else. Controls: the
 * same TOS 1.04 with the same GEMDOS mount and no frua.prg boots to the GEM
 * desktop with a HARD DISK icon and zero bus errors, and the same emulated ST
 * hardware with TOS 2.06 reaches the main menu. The variable is the ROM.
 *
 * Falling back to Malloc loses NOTHING on those machines, and that is provable
 * rather than hopeful: alternative (non-ST) RAM is reached through Maddalt,
 * which is ALSO "available as of GEMDOS version 0.19 only". A system that has
 * no Mxalloc therefore has no alternative RAM either — every byte Malloc can
 * return is already ST-RAM, which is exactly what Mxalloc(size, 0) was asking
 * for. The distinction only starts to matter on the TT and Falcon, and those
 * are 0.19 and 0.30, so they keep taking the Mxalloc path.
 *
 * Gating on the version rather than calling Mxalloc and inspecting the result
 * is deliberate: it means we never issue the unimplemented trap at all.
 *
 * Mfree is GEMDOS 0x49 and predates all of this, so it frees blocks from
 * either allocator — callers keep using Mfree and need no matching helper.
 */
void *plat_stram_alloc(long bytes)
{
	long p;

	if (bytes <= 0)
		return NULL;
	if ((Sversion() & 0xffff) >= 0x1900)
		p = (long)Mxalloc(bytes, 0);         /* 0 = ST-RAM */
	else
		p = (long)Malloc(bytes);
	/* GEMDOS reports failure as a small negative error code (EINVFN is -32);
	 * a real allocation is never in that range. Collapse both to NULL so the
	 * existing `== NULL` checks at every call site become correct. */
	if (p <= 0 && p > -4096L)
		return NULL;
	return (void *)p;
}

/* The MiNT crt0 already sets a large stack (the 256 KB floor the HAL contract
 * asks for), so the Atari backend runs the engine right where it is. */
int plat_run_big_stack(int (*fn)(void))
{
	return fn();
}
