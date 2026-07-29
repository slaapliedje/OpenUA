/*
 * mulprof.c — per-call-site attribution for the software 32x32 multiply.
 * Only compiled under -DFRUA_MULPROF; never in a shipping build.
 *
 * PC sampling put __mulsi3 at 21% of the ST/STe play loop but a leaf sample
 * names the callee, not the caller — and the routine has 253 static call sites
 * (207 of them in the lifted boot.c), so "which multiply" was unanswerable.
 *
 * libgcc's __mulsi3 is ALREADY optimal for a 68000: three mulu.w and eleven
 * instructions. There is nothing to win inside it. The only win available is
 * to stop CALLING it — a 32x32 multiply where 16x16 would do — so the useful
 * measurement is a histogram over return addresses.
 *
 * ★ DEFINING __mulsi3 HERE DOES NOT WORK — libgcc's _mulsi3.o is pulled in
 * regardless and the link dies on `multiple definition of __mulsi3`. Use the
 * linker's --wrap instead: every call site is redirected to __wrap___mulsi3,
 * and __real___mulsi3 still reaches libgcc's routine. That also means the
 * arithmetic is untouched — we count, libgcc multiplies — so there is no way
 * for this instrument to change a single result. The Makefile adds the
 * -Wl,--wrap flag whenever FRUA_MULPROF is defined.
 */

#include "dbglog.h"

#ifdef FRUA_MULPROF

/* Direct-indexed, NOT a linear scan: this runs on every 32x32 multiply in the
 * program, millions of times a drive. A 256-way probe would dominate the very
 * thing it measures. Collisions are counted, not resolved — if the collision
 * count is a small fraction of the total the histogram is trustworthy, and if
 * it is not, the table is too small and the numbers say so. */
#define MP_SLOTS        256
#define MP_IDX(r)       (((unsigned long)(r) >> 2) & (MP_SLOTS - 1))

static unsigned long mp_ret[MP_SLOTS];
static unsigned long mp_hit[MP_SLOTS];
/* Occupancy is its OWN flag, not "mp_ret != 0". A return address of 0 is
 * exactly what a broken __builtin_return_address produces, and using zero as
 * the empty sentinel made that failure invisible: every call landed in slot 0,
 * and the dump then skipped slot 0 for looking empty. */
static unsigned char mp_used[MP_SLOTS];
static unsigned long mp_calls;
static unsigned long mp_collide;
static unsigned long mp_zeroret;        /* the tell, if it ever comes back */

extern long __real___mulsi3(long a, long b);
long __wrap___mulsi3(long a, long b);

long __wrap___mulsi3(long a, long b)
{
	unsigned long ret = (unsigned long)__builtin_return_address(0);
	unsigned long i   = MP_IDX(ret);

	mp_calls++;
	if (ret == 0)
		mp_zeroret++;
	if (!mp_used[i]) {
		mp_used[i] = 1;
		mp_ret[i]  = ret;
		mp_hit[i]  = 1;
	} else if (mp_ret[i] == ret)
		mp_hit[i]++;
	else
		mp_collide++;

	return __real___mulsi3(a, b);
}

/* Dumped from normal context. `base` lets the host turn a runtime return
 * address into a link-time one: the .prg is relocated at load, so the raw
 * addresses mean nothing without it. Anchor on this function itself. */
void mul_prof_dump(void)
{
	unsigned long i, shown = 0;

	dbg_log_num("b96mul: total calls   = ", (long)mp_calls);
	dbg_log_num("b96mul: collisions    = ", (long)mp_collide);
	dbg_log_num("b96mul: ZERO-ret calls= ", (long)mp_zeroret);
	dbg_log_num("b96mul: ANCHOR runtime= ", (long)(unsigned long)&mul_prof_dump);

	/* Selection sort by count, top 24. NON-DESTRUCTIVE — walk downwards by
	 * "largest count strictly below the last one taken" rather than zeroing
	 * as we go, so the counters stay cumulative across the several dumps a
	 * drive produces. (Zeroing was the first version, and it silently made
	 * every dump after the first report a different, smaller population.) */
	{
		unsigned long prev = 0xFFFFFFFFUL, previ = 0;

		for (shown = 0; shown < 24; shown++) {
			unsigned long best = 0, bi = MP_SLOTS;

			for (i = 0; i < MP_SLOTS; i++) {
				/* (count, index) ordered lexicographically, so
				 * EQUAL counts are still enumerated rather than
				 * collapsed into one line. */
				if (!mp_used[i])
					continue;
				if (mp_hit[i] > prev ||
				    (mp_hit[i] == prev && i <= previ))
					continue;
				if (bi == MP_SLOTS || mp_hit[i] > best) {
					best = mp_hit[i];
					bi   = i;
				}
			}
			if (bi == MP_SLOTS)
				break;
			dbg_log_num("b96mul: site ret     = ", (long)mp_ret[bi]);
			dbg_log_num("b96mul:   calls      = ", (long)mp_hit[bi]);
			prev  = best;
			previ = bi;
		}
	}
}

#endif /* FRUA_MULPROF */
