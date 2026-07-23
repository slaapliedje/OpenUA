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
#include <exec/tasks.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

/* --- stack floor -----------------------------------------------------------
 *
 * A Shell-launched program inherits ~4 KB of stack, and the `__stack` cookie
 * this file used to set is INERT with this toolchain's -noixemul ncrt0 (it
 * references no such symbol — proven in installer/asl_amiga.c, which hit the
 * same wall). The engine overflows 4 KB two independent ways: the art-load
 * recursion (l17e2 -> jt987 -> jt104 bind -> jt460, a 210-byte FileSpec per
 * frame), and the ECS backend's re-band — quant_banded's ~2 KB of locals
 * fired from the bottom of the lifted draw chain, which wedged the adventure
 * screen on both 68000 and 68020 until this trampoline landed.
 *
 * So: run the engine's whole life on a 256 KB AllocMem'd stack via StackSwap
 * (the uainst pattern). StackSwap moves sp out from under the compiler, so
 * the trampoline keeps a frame pointer and touches only statics between the
 * two swaps. 256 KB matches the Falcon/ST crt0 headroom. */
#define FRUA_AMIGA_STACK (256UL * 1024)

static struct StackSwapStruct s_sss;
static int (*s_stk_fn)(void);
static int   s_stk_result;

static void __attribute__((noinline, optimize("no-omit-frame-pointer")))
stack_trampoline(void)
{
	StackSwap(&s_sss);
	s_stk_result = s_stk_fn();              /* runs on the big stack */
	StackSwap(&s_sss);
}

__attribute__((optimize("no-omit-frame-pointer")))
int plat_run_big_stack(int (*fn)(void))
{
	UBYTE *mem = AllocMem(FRUA_AMIGA_STACK, MEMF_ANY);

	if (mem == NULL)
		return fn();            /* no RAM: fall back, may be tight */
	s_stk_fn          = fn;
	s_sss.stk_Lower   = (APTR)mem;
	s_sss.stk_Upper   = (ULONG)mem + FRUA_AMIGA_STACK;
	s_sss.stk_Pointer = (APTR)(mem + FRUA_AMIGA_STACK);
	stack_trampoline();
	FreeMem(mem, FRUA_AMIGA_STACK);
	return s_stk_result;
}

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

/* Native-planar blit acceleration (ADR-0016). The Amiga blitter is core
 * chipset — always present on OCS/ECS/AGA — so this is unconditional. Phase 4
 * drives it via OwnBlitter/BltBitMap under the planar_blit interface. */
int plat_have_blitter(void)
{
	return 1;
}

#endif /* FRUA_AMIGA */
