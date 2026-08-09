/*
 * amiga_prof.h — fine play-loop timer for the Amiga display backends.
 *
 * The Atari backend has FRUA_STPROF (the b63play census) timed off the TOS 200Hz
 * counter at $4BA; the Amiga had NO play-loop instrument at all, so the ECS/AGA
 * walk cost could not be quantified the way the ST composite work was. This is
 * the missing timer: it combines the VBL frame counter (g_amiga_vbl_ticks, the
 * same source plat_ticks/TickCount scale) with the raster BEAM LINE for ~64us
 * (one PAL scanline) resolution — far finer than the 50/60Hz VBL alone, which is
 * too coarse for a single viewport present.
 *
 * Unit: PAL rasterlines (~63.56 us). Monotonic. On NTSC the frame stride (313)
 * overshoots the 262-line frame so absolute values are slightly non-linear, but
 * it never runs backwards — deltas within a frame are exact either way, and the
 * census reports a wall-relative share that is unit-independent.
 *
 * Gated on FRUA_AMIGAPROF so the shipping build is untouched (empty in the
 * default build, exactly like FRUA_STPROF on the Atari side).
 */
#ifndef AMIGA_PROF_H
#define AMIGA_PROF_H

#ifdef FRUA_AMIGAPROF

extern volatile unsigned long g_amiga_vbl_ticks;

/* Read a monotonic fine time in rasterline units. Double-reads the frame
 * counter around the beam sample so a VBL landing mid-read cannot pair a new
 * frame count with an old (near-max) beam line and jump the clock backwards. */
static inline long amiga_prof_rl(void)
{
	unsigned long f0, f1, vp;

	do {
		f0 = g_amiga_vbl_ticks;
		vp = *(volatile unsigned long *)0xDFF004UL;   /* VPOSR<<16 | VHPOSR */
		f1 = g_amiga_vbl_ticks;
	} while (f0 != f1);

	/* vertical beam line 0..312 = (VPOSR bit 0) << 8 | (VHPOSR high byte) */
	return (long)(f0 * 313UL
	              + (((vp >> 16) & 1UL) << 8) + ((vp >> 8) & 0xFFUL));
}

#endif /* FRUA_AMIGAPROF */
#endif /* AMIGA_PROF_H */
