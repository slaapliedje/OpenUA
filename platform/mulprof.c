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

#ifdef FRUA_DIVPROF

/*
 * #125 — the same instrument, aimed at software DIVISION.
 *
 * #124 profiled the ST boot and put __udivsi3 at 10.6% of program cycles, the
 * top item. As with __mulsi3, libgcc's routine is not the problem (lb1sf68's
 * __udivsi3 is a hand-written 68000 shift/subtract loop); the only win is to
 * stop calling it, so the useful measurement is again a histogram over return
 * addresses.
 *
 * ★ WRAP ALL FOUR, NOT JUST __udivsi3. libgcc's __divsi3, __umodsi3 and
 * __modsi3 are implemented ON TOP of __udivsi3 (__modsi3 -> __divsi3 ->
 * __udivsi3), and those inner calls are undefined symbols in their own
 * objects, so --wrap catches them too. Wrapping only __udivsi3 would therefore
 * attribute most of the traffic to an address inside libgcc — true, and
 * useless. Wrapping all four and suppressing the nested record (dp_depth)
 * gives one entry per SOURCE-LEVEL divide, and the per-routine totals below
 * say whether the code is dividing signed, unsigned, or taking remainders.
 *
 * dp_depth is not interrupt-safe: a VBL that divides while a wrapper is in
 * flight has its call dropped from the histogram. dp_nested reports the
 * magnitude of what was suppressed, so that stays visible rather than silent.
 */

#define DP_SLOTS        256
#define DP_IDX(r)       (((unsigned long)(r) >> 2) & (DP_SLOTS - 1))

static unsigned long dp_ret[DP_SLOTS];
static unsigned long dp_hit[DP_SLOTS];
static unsigned char dp_used[DP_SLOTS];
static unsigned long dp_calls;          /* source-level divides recorded  */
static unsigned long dp_collide;
static unsigned long dp_zeroret;
static unsigned long dp_nested;         /* libgcc-internal, not recorded  */
static unsigned long dp_n_udiv, dp_n_div, dp_n_umod, dp_n_mod;
static int           dp_depth;
/* The dump itself divides (dbg_log_num renders decimal), which would grow the
 * very counters it is walking. Freeze first, so the numbers printed are the
 * numbers the drive produced. */
static int           dp_frozen;

extern unsigned long __real___udivsi3(unsigned long a, unsigned long b);
extern long          __real___divsi3(long a, long b);
extern unsigned long __real___umodsi3(unsigned long a, unsigned long b);
extern long          __real___modsi3(long a, long b);

unsigned long __wrap___udivsi3(unsigned long a, unsigned long b);
long          __wrap___divsi3(long a, long b);
unsigned long __wrap___umodsi3(unsigned long a, unsigned long b);
long          __wrap___modsi3(long a, long b);

static void dp_note(unsigned long ret)
{
	unsigned long i = DP_IDX(ret);

	dp_calls++;
	if (ret == 0)
		dp_zeroret++;
	if (!dp_used[i]) {
		dp_used[i] = 1;
		dp_ret[i]  = ret;
		dp_hit[i]  = 1;
	} else if (dp_ret[i] == ret)
		dp_hit[i]++;
	else
		dp_collide++;
}

unsigned long __wrap___udivsi3(unsigned long a, unsigned long b)
{
	if (dp_frozen)
		return __real___udivsi3(a, b);
	if (dp_depth) {
		dp_nested++;
		return __real___udivsi3(a, b);
	}
	dp_n_udiv++;
	dp_note((unsigned long)__builtin_return_address(0));
	return __real___udivsi3(a, b);
}

long __wrap___divsi3(long a, long b)
{
	long r;

	if (dp_frozen)
		return __real___divsi3(a, b);
	if (dp_depth) {
		dp_nested++;
		return __real___divsi3(a, b);
	}
	dp_n_div++;
	dp_note((unsigned long)__builtin_return_address(0));
	dp_depth++;
	r = __real___divsi3(a, b);
	dp_depth--;
	return r;
}

unsigned long __wrap___umodsi3(unsigned long a, unsigned long b)
{
	unsigned long r;

	if (dp_frozen)
		return __real___umodsi3(a, b);
	if (dp_depth) {
		dp_nested++;
		return __real___umodsi3(a, b);
	}
	dp_n_umod++;
	dp_note((unsigned long)__builtin_return_address(0));
	dp_depth++;
	r = __real___umodsi3(a, b);
	dp_depth--;
	return r;
}

long __wrap___modsi3(long a, long b)
{
	long r;

	if (dp_frozen)
		return __real___modsi3(a, b);
	if (dp_depth) {
		dp_nested++;
		return __real___modsi3(a, b);
	}
	dp_n_mod++;
	dp_note((unsigned long)__builtin_return_address(0));
	dp_depth++;
	r = __real___modsi3(a, b);
	dp_depth--;
	return r;
}

/* A second, smaller histogram one level up: WHO asks for the clock. The divide
 * histogram names plat_ticks, but "plat_ticks" is not an answer — 47% of its
 * traffic turned out to be WaitNextEvent's deadline spin, where the cycles are
 * wall clock the boot spends either way. This separates the callers whose cost
 * is recoverable from the ones that are just waiting. */
#define TP_SLOTS        64
#define TP_IDX(r)       (((unsigned long)(r) >> 2) & (TP_SLOTS - 1))

static unsigned long tp_ret[TP_SLOTS];
static unsigned long tp_hit[TP_SLOTS];
static unsigned char tp_used[TP_SLOTS];
static unsigned long tp_collide;

void tick_prof_note(unsigned long ret);
void tick_prof_note(unsigned long ret)
{
	unsigned long i = TP_IDX(ret);

	if (dp_frozen)
		return;
	if (!tp_used[i]) {
		tp_used[i] = 1;
		tp_ret[i]  = ret;
		tp_hit[i]  = 1;
	} else if (tp_ret[i] == ret)
		tp_hit[i]++;
	else
		tp_collide++;
}

void div_prof_dump(void)
{
	unsigned long i, shown;
	unsigned long prev = 0xFFFFFFFFUL, previ = 0;

	dp_frozen = 1;

	dbg_log_num("b125div: total divides = ", (long)dp_calls);
	dbg_log_num("b125div:   __udivsi3   = ", (long)dp_n_udiv);
	dbg_log_num("b125div:   __divsi3    = ", (long)dp_n_div);
	dbg_log_num("b125div:   __umodsi3   = ", (long)dp_n_umod);
	dbg_log_num("b125div:   __modsi3    = ", (long)dp_n_mod);
	dbg_log_num("b125div: libgcc-nested = ", (long)dp_nested);
	dbg_log_num("b125div: collisions    = ", (long)dp_collide);
	dbg_log_num("b125div: ZERO-ret calls= ", (long)dp_zeroret);
	dbg_log_num("b125div: ANCHOR runtime= ", (long)(unsigned long)&div_prof_dump);

	/* Hot path or busy wait? See the note in compat/events.c. */
	{
		extern unsigned long g_wne_calls, g_wne_iters, g_wne_timeouts;
		extern unsigned long g_tick_calls;

		dbg_log_num("b125div: TickCount calls= ", (long)g_tick_calls);
		dbg_log_num("b125div: WaitNextEvent  = ", (long)g_wne_calls);
		dbg_log_num("b125div:   spin iters   = ", (long)g_wne_iters);
		dbg_log_num("b125div:   timed out    = ", (long)g_wne_timeouts);
	}

	/* Same non-destructive selection sort as mul_prof_dump: walk downwards by
	 * "largest count strictly below the last one taken" so the counters stay
	 * cumulative across however many dumps a drive produces. */
	for (shown = 0; shown < 24; shown++) {
		unsigned long best = 0, bi = DP_SLOTS;

		for (i = 0; i < DP_SLOTS; i++) {
			if (!dp_used[i])
				continue;
			if (dp_hit[i] > prev ||
			    (dp_hit[i] == prev && i <= previ))
				continue;
			if (bi == DP_SLOTS || dp_hit[i] > best) {
				best = dp_hit[i];
				bi   = i;
			}
		}
		if (bi == DP_SLOTS)
			break;
		dbg_log_num("b125div: site ret     = ", (long)dp_ret[bi]);
		dbg_log_num("b125div:   calls      = ", (long)dp_hit[bi]);
		prev  = best;
		previ = bi;
	}

	dbg_log_num("b125tick: collisions   = ", (long)tp_collide);
	prev = 0xFFFFFFFFUL; previ = 0;
	for (shown = 0; shown < 16; shown++) {
		unsigned long best = 0, bi = TP_SLOTS;

		for (i = 0; i < TP_SLOTS; i++) {
			if (!tp_used[i])
				continue;
			if (tp_hit[i] > prev ||
			    (tp_hit[i] == prev && i <= previ))
				continue;
			if (bi == TP_SLOTS || tp_hit[i] > best) {
				best = tp_hit[i];
				bi   = i;
			}
		}
		if (bi == TP_SLOTS)
			break;
		dbg_log_num("b125tick: site ret    = ", (long)tp_ret[bi]);
		dbg_log_num("b125tick:   calls     = ", (long)tp_hit[bi]);
		prev  = best;
		previ = bi;
	}

	dp_frozen = 0;
}

#endif /* FRUA_DIVPROF */
