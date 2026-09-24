/*
 * System services HAL — Unix (AMIX, Atari System V).
 *
 * A Unix process has what the other ports bring up by hand: a stack the
 * kernel grows, memory from malloc, the time of day. The Atari-only levers
 * (the blitter, the Mega STe's CPU boost, ST-RAM for DMA) do not exist.
 */
#ifdef FRUA_UNIX

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "plat_sys.h"

void plat_console_puts(const char *s)
{
	if (s != NULL)
		write(2, s, strlen(s));
}

int plat_console_getc(void)
{
	unsigned char c;

	return read(0, &c, 1) == 1 ? c : 0;
}

/* The Mac's FreeMem/MaxMem: the engine sizes caches from it. A Unix
 * process can grow well past what it asks for here; report a generous,
 * fixed figure rather than probing the heap. */
unsigned long plat_mem_largest_free(void)
{
	return 8UL * 1024 * 1024;
}

void plat_get_datetime(struct plat_datetime *out)
{
	time_t now;
	struct tm *tm;		/* ints only: the layout is shared safely */

	if (out == NULL)
		return;
	now = time(NULL);
	tm = localtime(&now);
	out->year   = tm->tm_year + 1900;
	out->month  = tm->tm_mon + 1;
	out->day    = tm->tm_mday;
	out->hour   = tm->tm_hour;
	out->minute = tm->tm_min;
	out->second = tm->tm_sec;
}

int plat_have_blitter(void)
{
	return 0;
}

void *plat_stram_alloc(long bytes)
{
	return malloc((size_t)bytes);	/* no DMA-reachable memory to prefer */
}

int plat_cpu_boost(void)
{
	return 0;			/* nothing to boost */
}

void plat_cpu_boost_restore(void)
{
}

/* The kernel grows a process's stack on demand: no swap needed. */
int plat_run_big_stack(int (*fn)(void))
{
	return fn();
}

#endif /* FRUA_UNIX */
