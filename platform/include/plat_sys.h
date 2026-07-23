/*
 * System HAL — the handful of OS primitives the Toolbox shim and the launcher
 * need that are inherently machine-specific: console I/O (the fatal-error path
 * and bring-up demos), the largest free memory block, and the wall clock.
 *
 * Keeping these behind the HAL is the layer rule (CLAUDE.md): only platform/
 * knows the machine. The Falcon/TT backend (platform/sys_falcon.c) routes to
 * GEMDOS; the Amiga backend (platform/amiga/sys_amiga.c) to exec/dos.library.
 */

#ifndef PLATFORM_PLAT_SYS_H
#define PLATFORM_PLAT_SYS_H

/* Write a NUL-terminated string to the debug/console device. */
void plat_console_puts(const char *s);

/* Block until a key is pressed and return it. Bring-up / demo pauses only. */
int plat_console_getc(void);

/* Largest single free RAM block, in bytes (the Mac FreeMem stand-in). */
unsigned long plat_mem_largest_free(void);

/* Broken-down local wall-clock time. The machine-neutral epoch conversion
 * (e.g. GetDateTime's Mac-epoch math) stays in the caller. */
struct plat_datetime {
	int year;      /* full year, e.g. 2026 */
	int month;     /* 1..12                */
	int day;       /* 1..31                */
	int hour;      /* 0..23                */
	int minute;    /* 0..59                */
	int second;    /* 0..59                */
};
void plat_get_datetime(struct plat_datetime *out);

/* Is a hardware blitter available for native planar blits (ADR-0016)?
 * 1 = use the blitter path, 0 = CPU fallback. Amiga: always 1 (core chipset).
 * Atari: STe/Mega ST always have it; the plain ST had an optional socket, so
 * this probes XBIOS Blitmode(-1). Queried once at display init. */
int plat_have_blitter(void);

/* Run `fn` on a stack with at least 256 KB of headroom and return its result.
 * The engine's lifted draw chain runs deep, and the ECS/ST backends re-band
 * (quant_banded, ~2 KB of locals) from the BOTTOM of it. The Atari crt0
 * already grants that headroom, so its backend just calls fn(); an Amiga
 * Shell grants ~4 KB and the `__stack` cookie is inert under -noixemul ncrt0
 * (see installer/asl_amiga.c), so its backend StackSwaps onto an AllocMem'd
 * stack for the program's whole life. main() runs the engine through this. */
int plat_run_big_stack(int (*fn)(void));

#endif /* PLATFORM_PLAT_SYS_H */
