/*
 * Input HAL — Falcon / TT implementation. See input.h.
 *
 * _hz_200 (the 200 Hz system tick at 0x4BA) → 60 Hz Mac ticks; BIOS
 * Bconstat / Bconin (device 2 = console) for the keyboard; Kbshift for
 * modifier state. Mouse routing waits for the IKBD-packet driver, so
 * Button() / GetMouse() return zero / origin for now.
 */

#include <mint/osbind.h>

#include "input.h"

static long read_hz200(void)
{
	return *(volatile long *)0x4BAL;
}

unsigned long plat_ticks(void)
{
	/* 200 Hz → 60 Hz: *60/200 = *3/10. The unsigned-long arithmetic
	 * gives us ~248 days of run-time before the 60 Hz counter wraps,
	 * comfortably outside any plausible session. */
	unsigned long h200 = (unsigned long)Supexec(read_hz200);

	return (h200 * 3UL) / 10UL;
}

int plat_kb_poll(unsigned char *out_scan, unsigned char *out_ascii)
{
	long c;

	if (Bconstat(2) == 0)
		return 0;
	c = Bconin(2);
	if (out_scan)
		*out_scan = (unsigned char)((c >> 16) & 0xFF);
	if (out_ascii)
		*out_ascii = (unsigned char)(c & 0xFF);
	return 1;
}

unsigned char plat_kb_shift(void)
{
	return (unsigned char)Kbshift(-1);
}

void plat_mouse_pos(short *h, short *v)
{
	if (h) *h = 0;
	if (v) *v = 0;
}

int plat_mouse_btn(void)
{
	return 0;
}
