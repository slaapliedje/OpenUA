/*
 * FRUA application entry point — lifted from CODE 6 + 0x58a (jump-table
 * entry 12). This is FRUA's own main(): the THINK C runtime (CODE 1) builds
 * the A5/A4 world and then hands off here.
 *
 * The Mac main() has six phases:
 *
 *   1. Module-init roll-call — ~16 calls to empty jump-table stubs whose
 *      only effect is to force _LoadSeg to page their segments resident.
 *   2. Core initialisation — core_init() (see core.c).
 *   3. Screen mode — probe a control file, bring the screen up large or
 *      small accordingly.
 *   4. Register a UI handler, run the secondary init.
 *   5. String-table checks and a second UI handler.
 *   6. The segment-cycling play loop, driven by JT[315].
 *
 * Two whole mechanisms collapse in the port (ADR-0002, one flat
 * executable): the roll-call has no segments to page, and every _LoadSeg /
 * _UnLoadSeg around the play loop is gone. What is left is the real control
 * flow in ua_main() below.
 *
 * Most routines main() calls are not lifted yet; they are the no-op stubs
 * in the "frontier" block — each marked with its CODE location — and form
 * the explicit to-do list for the rest of the runtime port. See
 * docs/decompilation.md for the main() map.
 *
 * What's lifted in this file: ua_main (the entry sequencer above) plus
 * the play-loop body L07dc at the bottom. L07dc carries its own frontier
 * of helpers and JT entries it calls — they live with it down there.
 */

#include <stdarg.h>           /* va_list (jt452, jt488) */
#include <stddef.h>           /* NULL, size_t */
#include <stdint.h>           /* uintptr_t (pointer ↔ long casts) */
#include <stdio.h>            /* vsnprintf (jt488) */
#include <string.h>           /* memset, memcpy */

#include "boot.h"
#include "core.h"             /* core_init */
#include "master.h"           /* master_init */
#include "macmemory.h"        /* BlockMove, Size */
#include "data_pool_replay.h" /* g_a5_byte */
#include "str.h"              /* ua_strcmp, ua_get_string */
#include "fc.h"               /* fc_dump */
#include "quickdraw.h"        /* MoveTo, DrawString, GetPort (jt1089) */

/* L5124 cluster — the ~30 byte globals L5124 zero-inits or seeds with
 * a small constant. All live in the below-A5 buffer at their A5
 * offsets; the c79x cluster's macro pattern carries over directly.
 * Macros come up here so the L5700 / jt918 / case-body lifts further
 * down see the same slot in g_a5_below[].
 *
 * Wider-typed globals (g_a5_27984 short, g_a5_27940 long, g_a5_24142
 * short, g_a5_13038 pointer, g_a5_24304 name buffer) stay as file
 * statics for now — migrating them needs the typed accessors plus
 * fixups to the call sites that take their address. */
#define g_a5_18474 g_a5_byte(-18474)
#define g_a5_18473 g_a5_byte(-18473)
#define g_a5_22218 g_a5_byte(-22218)
#define g_a5_12288 g_a5_byte(-12288)
#define g_a5_12287 g_a5_byte(-12287)
#define g_a5_22226 g_a5_byte(-22226)
#define g_a5_27981 g_a5_byte(-27981)
#define g_a5_12290 g_a5_byte(-12290)
#define g_a5_23190 g_a5_byte(-23190)
#define g_a5_22284 g_a5_byte(-22284)
#define g_a5_22283 g_a5_byte(-22283)
#define g_a5_22282 g_a5_byte(-22282)
#define g_a5_22281 g_a5_byte(-22281)
#define g_a5_22279 g_a5_byte(-22279)
#define g_a5_24283 g_a5_byte(-24283)
#define g_a5_24261 g_a5_byte(-24261)
#define g_a5_22330 g_a5_byte(-22330)
#define g_a5_22331 g_a5_byte(-22331)
#define g_a5_22273 g_a5_byte(-22273)
#define g_a5_22626 g_a5_byte(-22626)
#define g_a5_24140 g_a5_byte(-24140)
#define g_a5_23187 g_a5_byte(-23187)
#define g_a5_22269 g_a5_byte(-22269)
#define g_a5_22633 g_a5_byte(-22633)
#define g_a5_22635 g_a5_byte(-22635)
#define g_a5_22268 g_a5_byte(-22268)
#define g_a5_22225 g_a5_byte(-22225)
#define g_a5_24148 g_a5_byte(-24148)
#define g_a5_27987 g_a5_byte(-27987)
#define g_a5_27916 g_a5_byte(-27916)
#define g_a5_22275 g_a5_byte(-22275)

/* These slots are shared with the L5700 / dispatch / jt918 lifts. */
#define g_a5_27982 g_a5_byte(-27982)
#define g_a5_27946 g_a5_byte(-27946)
#define g_a5_24262 g_a5_byte(-24262)
#define g_a5_24256 g_a5_byte(-24256)

/* Remaining scalar globals — bytes / shorts / longs / pointers from
 * across boot.c, all bound to their A5-relative slots in the replay
 * buffer. The macros work as l-values (assignment), readable in
 * conditions, and `&g_a5_NNNN` returns the buffer address. Initialised
 * non-zero scalars (g_a5_9288 = 64, g_a5_9248 = 1) are seeded by
 * boot_a5_seed_defaults() after data_pool_replay() zero-fills the
 * buffer. Arrays (g_a5_27980, g_a5_22727, g_a5_10074, g_a5_10026,
 * g_a5_10270, g_a5_9354, g_a5_27894, g_a5_24126, g_a5_24304) stay
 * file statics — migrating them needs care around adjacency and
 * array-decay. */
#define g_a5_4944  g_a5_byte(-4944)
#define g_a5_2347  g_a5_byte(-2347)    /* JT[1200]: "in encounter mode" gate */
#define g_a5_1312  g_a5_byte(-1312)    /* JT[1200]: encounter sub-state flag */
#define g_a5_18394 g_a5_byte(-18394)   /* JT[112]: GrafPort save-state mode */

/* JT[1089]'s pen-state cluster (CODE 5 + 0x024c..0x028x):
 *   -4894 / -4892 — packed (col, style) the engine's color word
 *                  decomposes into; L024c stores the word.
 *   -4898 / -4896 — transformed (x, y) pen position; L0264 stores
 *                  after the jt1135 coord remap. */
#define g_a5_4894  g_a5_word(-4894)
#define g_a5_4892  g_a5_byte(-4892)
#define g_a5_4898  g_a5_word(-4898)
#define g_a5_4896  g_a5_word(-4896)

/* JT[1161]'s window-clip cluster — the engine's current "paintable
 * region" the rect-fill clamps against. Set elsewhere by the
 * window-open code (JT[103] etc.); JT[1161] just reads. */
#define g_a5_3054  g_a5_word(-3054)    /* clip top    */
#define g_a5_3056  g_a5_word(-3056)    /* clip left   */
#define g_a5_3050  g_a5_word(-3050)    /* clip bottom */
#define g_a5_3052  g_a5_word(-3052)    /* clip right  */
#define g_a5_27990 g_a5_byte(-27990)
#define g_a5_18485 g_a5_byte(-18485)
#define g_a5_18828 g_a5_byte(-18828)
#define g_a5_18827 g_a5_byte(-18827)
#define g_a5_18488 g_a5_byte(-18488)
#define g_a5_27989 g_a5_byte(-27989)
#define g_a5_12291 g_a5_byte(-12291)
#define g_a5_12292 g_a5_byte(-12292)
#define g_a5_12293 g_a5_byte(-12293)
#define g_a5_12294 g_a5_byte(-12294)
#define g_a5_12289 g_a5_byte(-12289)
#define g_a5_13046 g_a5_byte(-13046)
#define g_a5_31230 g_a5_byte(-31230)
#define g_a5_12286 g_a5_byte(-12286)
#define g_a5_12911 g_a5_byte(-12911)
#define g_a5_12912 g_a5_byte(-12912)
#define g_a5_19169 g_a5_byte(-19169)
#define g_a5_9247  g_a5_byte(-9247)
#define g_a5_27988 g_a5_byte(-27988)
#define g_a5_18472 g_a5_byte(-18472)
#define g_a5_22730 g_a5_byte(-22730)
#define g_a5_18471 g_a5_byte(-18471)
#define g_a5_9248  g_a5_byte(-9248)    /* DLItem-manager "active" flag (=1) */

/* L02dc's roster-grid rect (g_a5_24128..g_a5_24131 — top / right / bottom
 * / page selector).  L02dc resets them on every paint; nothing else
 * reads them except the JT paint stubs once they're lifted. */
#define g_a5_24128 g_a5_byte(-24128)
#define g_a5_24129 g_a5_byte(-24129)
#define g_a5_24130 g_a5_byte(-24130)
#define g_a5_24131 g_a5_byte(-24131)

/* L12a0 / L15e2 — dialog-loop globals. g_a5_24139 is the "the user
 * just typed Escape" guard; g_a5_22733 is the dialog mode the JT[3]
 * dispatcher sets on entry. g_a5_22212 / g_a5_31336 are aggregate
 * scratch areas (taken-by-address by JT[477] / JT[431]). */
#define g_a5_24139 g_a5_byte(-24139)
#define g_a5_22733 g_a5_byte(-22733)
#define g_a5_22212 g_a5_byte(-22212)
#define g_a5_31336 g_a5_byte(-31336)

#define g_a5_18878 g_a5_word(-18878)
#define g_a5_24322 g_a5_word(-24322)
#define g_a5_9306  g_a5_word(-9306)
#define g_a5_12296 g_a5_word(-12296)
#define g_a5_31234 g_a5_word(-31234)
#define g_a5_5798  g_a5_word(-5798)
#define g_a5_5796  g_a5_word(-5796)
#define g_a5_19174 g_a5_word(-19174)
#define g_a5_19172 g_a5_word(-19172)
#define g_a5_5794  g_a5_word(-5794)
#define g_a5_9250  g_a5_word(-9250)
#define g_a5_9288  g_a5_word(-9288)    /* DLItem table capacity (=64) */
#define g_a5_13016 g_a5_word(-13016)
#define g_a5_13018 g_a5_word(-13018)
#define g_a5_19176 g_a5_word(-19176)
#define g_a5_12910 g_a5_word(-12910)

#define g_a5_27928 g_a5_long(-27928)
#define g_a5_27932 g_a5_long(-27932)
#define g_a5_22222 g_a5_long(-22222)
#define g_a5_9254  g_a5_long(-9254)
#define g_a5_14284 g_a5_long(-14284)
#define g_a5_18844 g_a5_long(-18844)
#define g_a5_18882 g_a5_long(-18882)

/* L12a0 / L15e2 dialog-loop longs — JT[169] reads them as handles
 * into the prompt cluster (g_a5_13792 / g_a5_14216 for View, the
 * adjacent +4 slots for Delete). g_a5_21156 is the "live designs"
 * pointer JT[471] tears down. */
#define g_a5_13788 g_a5_long(-13788)
#define g_a5_13792 g_a5_long(-13792)
#define g_a5_14212 g_a5_long(-14212)
#define g_a5_14216 g_a5_long(-14216)
#define g_a5_21156 g_a5_long(-21156)

#define g_a5_28006 g_a5_ptr(-28006)
#define g_a5_24320 g_a5_ptr(-24320)
#define g_a5_24260 g_a5_ptr(-24260)

/* Array globals. Each macro resolves to a pointer at the array's A5
 * base; `g_a5_NAME[i]` indexes the buffer slot, `&g_a5_NAME[i]` returns
 * the address, and `sizeof g_a5_NAME` no longer works — callers that
 * used `sizeof` got reworked to use the explicit-size constants
 * below. */
#define G_A5_10074_LEN  1024
#define G_A5_10026_LEN  (JT465_RECORD_MAX * JT465_RECORD_BYTES)
#define G_A5_10270_LEN  (JT465_RECORD_MAX + 2)        /* longs        */
#define G_A5_9354_LEN   48
#define G_A5_27894_LEN  3                              /* longs        */
#define G_A5_24126_LEN  40
#define G_A5_27980_LEN  (32 * 3)
#define G_A5_22727_LEN  4
#define G_A5_24304_LEN  256
#define G_A5_10362_LEN  256                            /* jt488 sprintf scratch */

#define g_a5_10074 g_a5_buf(-10074)
#define g_a5_10026 g_a5_buf(-10026)
#define g_a5_10270 g_a5_longs(-10270)
#define g_a5_9354  g_a5_buf(-9354)
#define g_a5_27894 g_a5_longs(-27894)
#define g_a5_24126 g_a5_buf(-24126)
#define g_a5_27980 g_a5_buf(-27980)
#define g_a5_22727 g_a5_buf(-22727)
#define g_a5_24304 g_a5_chars(-24304)
#define g_a5_10362 g_a5_chars(-10362)

/*
 * Stub-trace probe. Off by default — when compiled with
 * -DFRUA_ENGINE_PROBE every stub below logs its name as the engine
 * runs through it, so we can capture the engine's actual call sequence
 * and drive the next lifting priority. See docs/engine-bring-up.md
 * for the first probe's results and how to re-run.
 */
#ifdef FRUA_ENGINE_PROBE
#  include "dbglog.h"
#  define PROBE(name) dbg_log("stub: " name)
#else
#  define PROBE(name) ((void)0)
#endif

/*
 * A5-world globals main() touches. Their initial values come from the
 * DATA / ZERO resources; the A5-world image is not wired up yet, so for now
 * they are plain zero-initialised globals, named by their A5 offset.
 */
static long          g_24138;          /* A5-24138 — cleared by main()      */
static unsigned char g_24134;          /* A5-24134 — set to 1 by main()     */
static char          g_str_22253[32];  /* A5-22253 — a DATA-resource string;
                                         *           real size/contents TBD */
static signed char   g_22307;          /* A5-22307 — phase-5 loop counter   */
static unsigned char g_22231[4];        /* A5-22231 — flag bytes [1..3]      */

/*
 * --- Frontier: routines main() calls that are not lifted yet ---
 *
 * No-op stubs so ua_main() compiles, links, and reads in its true shape;
 * each is replaced when its segment is lifted (the fc_dump pattern). The
 * value-returning stubs return 0 — jt315() in particular must, or the play
 * loop below would never terminate.
 */
/* jt398's CODE-3 callees — all stubs. */
static void  l32e2(const char *prompt, short a, long b)  { PROBE("l32e2"); }       /* CODE 3 + 0x32e2 — prompt */
static short l322c(const char *path)                     { PROBE("l322c"); return 0; }  /* CODE 3 + 0x322c — path test */
static const char *l31fc(const char *path)               { PROBE("l31fc"); return path; }/* CODE 3 + 0x31fc — path xform */
static void  l45d6(char *dst, const char *src)           { PROBE("l45d6"); }       /* CODE 3 + 0x45d6 — copy/normalise */
static short l328e(const char *buf, short a, short flags){ PROBE("l328e"); return 0; }  /* CODE 3 + 0x328e — open       */

/*
 * jt398 — CODE 3 + 0x37e4. Phase-3 control-file probe: opens / probes a
 * file by path. Two top-level branches:
 *
 *   - if `path` starts with '%', call l32e2 with "File to open" — likely
 *     a prompt / standard-file dialog
 *   - otherwise: l322c(path) to test the path; if it returned non-zero,
 *     transform via l31fc; copy into a local 128-byte buffer with l45d6;
 *     open via l328e(buf, 0, flags) and return its result
 *
 * Engine code calls this with ":DISK4:ALWAYS.CTL" + flags=0 from
 * ua_main's phase 3 (the "small / large screen mode" branch). The
 * 5 callees ship as PROBE-instrumented stubs.
 */
static short jt398(const char *path, short flags)
{
	char buf[128];
	short status;

	PROBE("jt398");
	if (path == NULL)
		return 0;
	if (path[0] == '%') {
		l32e2("File to open", 0, 0L);
		return 0;
	}
	status = l322c(path);
	if (status != 0)
		path = l31fc(path);
	l45d6(buf, path);
	return l328e(buf, 0, flags);
}
static void  jt411(short status)                   { PROBE("jt411"); }            /* CODE 3 + 0x3de2 */
/* jt480 is the string-table setter — lifted in str.c; ua_main forwards
 * its own arg1 / arg2 here so the THINK C runtime's (count, table) flows
 * in. PROBE-instrumented inside str.c if needed for tracing; the engine
 * call path here just uses the real symbol. */
static void  jt445(void)                           { PROBE("jt445"); }            /* CODE 3 + 0x294e */
static void  jt415(short a)                        { PROBE("jt415"); }            /* CODE 3 + 0x37da */
static void  jt1129(short a)                       { PROBE("jt1129"); }           /* CODE 4 + 0x4756 */
static void  jt1130(void)                          { PROBE("jt1130"); }           /* CODE 4 + 0x61f6 */
static void  jt1009(short a, short b)              { PROBE("jt1009"); }           /* CODE 5 + 0x0a34 */
static void  jt977(void)                           { PROBE("jt977"); }            /* CODE 5 + 0x0aaa */
static void  jt989(void (*handler)(void), short flag, const char *name, short code) { PROBE("jt989"); }
                                                                                  /* CODE 5 + 0x1b56 */
static void  jt361(short a)                        { PROBE("jt361"); }            /* CODE 8 + 0x71ec */
static void  jt919(void)                           { PROBE("jt919"); }            /* CODE 12 + 0x1b12 */
static void  jt920(void)                           { PROBE("jt920"); }            /* CODE 12 + 0x1ba8 */
static int   jt931(void)                           { PROBE("jt931"); return 0; }  /* CODE 12 + 0x430c */
static void  jt949(void)                           { PROBE("jt949"); }            /* CODE 20 + 0x77a2 */
static void  jt956(void)                           { PROBE("jt956"); }            /* CODE 21 + 0x326a */
static int   jt315(void)
{
	/* In probe mode, fire once so the play-loop body runs and its
	 * stubs log themselves; in normal mode this always returns 0
	 * (the play loop is a no-op until lifted). */
#ifdef FRUA_ENGINE_PROBE
	static int fired;
	if (!fired) { fired = 1; PROBE("jt315 (firing)"); return 1; }
	PROBE("jt315 (done)");
#endif
	return 0;
}

/* Intra-CODE-6 helpers, still to lift. */
static void  l0444(void)        { PROBE("l0444"); }     /* CODE 6 + 0x0444 */
static void  l3918(long a)      { PROBE("l3918"); }     /* CODE 6 + 0x3918 */
static void  l4d98(void)        { PROBE("l4d98"); }     /* CODE 6 + 0x4d98 */
static void  l5888(short a)     { PROBE("l5888"); }     /* CODE 6 + 0x5888 */
static void  l5ac0(void)        { PROBE("l5ac0"); }     /* CODE 6 + 0x5ac0 */
static void  l5f66(void)        { PROBE("l5f66"); }     /* CODE 6 + 0x5f66 */
static void  l6ada(short a)     { PROBE("l6ada"); }     /* CODE 6 + 0x6ada */
static void  l07dc(void);                              /* defined below */

/* UI-handler callbacks main() registers through JT[989]. */
static void  jt10_handler(void) { }     /* CODE 6 + 0x0538 (jump-table entry 10) */
static void  jt11_handler(void) { }     /* CODE 6 + 0x04c0 (jump-table entry 11) */

/*
 * ua_main — CODE 6 + 0x58a (jump-table entry 12).
 *
 * arg1 / arg2 are the two values the THINK C runtime passes; they flow
 * through to the screen and secondary init.
 */
int ua_main(short arg1, long arg2)
{
	short status;

	/*
	 * Phase 1 — module-init roll-call. The Mac build calls L5348 and
	 * fifteen empty jump-table stubs solely to page their segments in;
	 * a flat executable has nothing to do here.
	 */

	/* Phase 2 — core initialisation. */
	core_init();

	/*
	 * Phase 3 — screen mode. JT[398] probes the ":DISK4:ALWAYS.CTL"
	 * control file; the screen comes up large (status >= 0) or small
	 * (status < 0) accordingly. JT[1079]'s trailing pair is the FC
	 * buffer size in KB (kb_min, kb_max).
	 */
	status = jt398(":DISK4:ALWAYS.CTL", 0);
	if (status >= 0) {
		jt411(status);
		jt1129(1);
		master_init(arg1, arg2, 214, 450);
	} else {
		master_init(-5, arg2, 160, 400);
	}

	/* Phase 4 — secondary init and the first UI handler. arg1 / arg2
	 * carry the THINK C runtime's (string-table count, pointer); jt480
	 * stows them in the A5-world globals ua_get_string reads from. */
	jt480(arg1, (void *)arg2);
	jt989(jt10_handler, 1, "Pod", 83);
	l4d98();
	l0444();
	jt361(1);
	jt920();
	jt1009(8096, 0);

	/* Phase 5 — string-table checks and the second UI handler. */
	if (ua_strcmp(ua_get_string(2), "Heart") != 0)
		jt919();
	l6ada(0);
	jt977();
	l3918(0L);
	jt989(jt11_handler, 1, "Pod", 83);
	jt1130();
	g_24138 = 0;
	g_24134 = 1;
	l5888(255);
	if (ua_strcmp(ua_get_string(3), g_str_22253) != 0) {
		if (jt931() == 0)
			l5f66();
		for (g_22307 = 1; g_22307 < 4; g_22307++) {
			if (g_22231[g_22307] == 0)
				l5f66();
		}
	}

	/*
	 * Phase 6 — the segment-cycling play loop. Stripped of its _LoadSeg
	 * / _UnLoadSeg paging it is just this: run JT[949], JT[956], JT[920]
	 * and the per-iteration body L07dc while JT[315] stays true.
	 */
	while (jt315()) {
		jt949();
		jt956();
		jt920();
		l07dc();
	}

	/* Shutdown. (L4d7a, called here in the Mac build, is an empty stub.) */
	jt445();
	if (ua_strcmp(ua_get_string(2), "Heart") == 0)
		fc_dump(0L);
	l5ac0();
	master_shutdown();
	jt415(0);
	return 0;
}

/* =========================================================================
 * L07dc — the play-loop body (CODE 6 + 0x07dc).
 *
 * Runs once per iteration of the ua_main play loop. Tracks a mode byte
 * (g_a5_18485: 0 = "new game" prompt, non-zero = "resume saved game"),
 * an inner state selector (g_a5_27990), and a few A5-world bytes that
 * appear to feed jt217 — likely a four-channel colour or sound dump.
 *
 * On entry, if the mode byte is clear, the local L5124 runs first
 * (initial setup). The body then enters its inner loop:
 *
 *   - per-iteration setup: jt942(0), L5888(255)
 *   - branch on mode:
 *       resume: jt582; if save-list empty (g_a5_27928 == 0) alert
 *               "No saved games!" and return; otherwise restore the
 *               selector from g_a5_27989 (defaulting to 4), then jt941
 *       new:    jt918(1); if it declines, exit to the cleanup tail
 *   - state machine on the selector: when selector == 3 AND the byte
 *     at offset 36 of the *g_a5_28006 handle equals 1, run L68f8;
 *     otherwise run L67ca / jt937(g_a5_27932) / jt938
 *   - dispatch the four-byte payload through jt217
 *   - jt948, refresh selector from g_a5_27989, loop while jt943 reports
 *     more work
 *
 * Cleanup tail: drain the g_a5_27932 pointer through L2cb0(0,1), then
 * L5888(255) to release any per-iteration state.
 *
 * Every callee here (except L5888, already a stub above) is fresh — the
 * lifting frontier for this segment. Globals carry their A5 offset.
 * ========================================================================= */

/* Local CODE-6 helpers L07dc calls — L5124 lifted below; the rest are stubs. */
static void          l5124(void);                                                /* CODE 6 + 0x5124 — lifted below */
static void          l4b40(const char *msg, short a, short b) { (void)msg; PROBE("l4b40"); }
                                                                                 /* CODE 6 + 0x4b40 (alert?) */
static void          l67ca(void);                                                /* CODE 6 + 0x67ca — lifted below */
static void          jt76(void);                                                  /* CODE 6 + 0x670c — lifted below */
static void          l66e6(short n);                                              /* CODE 6 + 0x66e6 — lifted below */
static void          l68f8(void)
{
	/* CODE 6 + 0x68f8: two-call sequence. */
	PROBE("l68f8");
	jt76();
	l66e6(16);
}
static void          l2cb0(short a, short b)  { PROBE("l2cb0"); }                /* CODE 6 + 0x2cb0 */

/* L07dc's play-loop predicate flag, A5-relative offset -4944. Lives here
 * (above the JT entries) so jt942 / jt943 can read and write it directly;
 * L5124 zeroes it as part of the game-start state reset. Other A5 globals
 * stay co-located with their main lift. */

/* Forward tentative declarations of A5 globals jt941 (lifted in-place
 * below) reaches for; full definitions live further down with L07dc and
 * L5124. C merges duplicate tentative file-scope static declarations,
 * so naming them here lets jt941 reference them before the main blocks
 * arrive. */
/* g_a5_27990 / g_a5_28006 / g_a5_12287 / g_a5_12288 are all macros
 * defined in the cluster block at the top of the file. */

/* Cross-segment JT entries L07dc calls — most are stubs; jt918 / jt942 /
 * jt943 are lifted (jt942 and jt943 are the paired setter / getter on the
 * loop predicate flag g_a5_4944). */
static void          jt942(short a)
{
	/* CODE 20 + 0x472a: moveb fp@(9), A5_-4944 — store the low byte. */
	PROBE("jt942");
	g_a5_4944 = (unsigned char)a;
}
static void          jt582(void)              { PROBE("jt582"); }                /* CODE 15 + 0x153e */
static void          jt941(void)
{
	/* CODE 20 + 0x4108: copy two A5 / handle bytes into handle[23..24].
	 * Selector 4 (the L5124 default) copies from g_a5_12288 / 12287;
	 * any other selector copies from handle[37..38] within the same
	 * handle (an in-handle field-to-field move). */
	unsigned char *handle = (unsigned char *)g_a5_28006;

	PROBE("jt941");
	if (handle == NULL)
		return;
	if (g_a5_27990 == 4) {
		handle[23] = g_a5_12288;
		handle[24] = g_a5_12287;
	} else {
		handle[23] = handle[37];
		handle[24] = handle[38];
	}
}
static int           jt918(short a);                                             /* CODE 12 + 0x0d90 — lifted below */
static void          jt937(long a)            { PROBE("jt937"); }                /* CODE 12 + 0x02dc */
static void          jt938(void)              { PROBE("jt938"); }                /* CODE 12 + 0x0562 */
static void          jt217(short a, short b, short c, short d) { PROBE("jt217"); }
                                                                                 /* CODE 7 + 0x57d2 */
static void          jt948(void)              { PROBE("jt948"); }                /* CODE 20 + 0x4a12 */
static int           jt943(void)
{
	/* CODE 20 + 0x4738: moveb A5_-4944, d0 — read the predicate flag.
	 * Paired with jt942 above. L07dc loops while this is non-zero. */
	PROBE("jt943");
	return g_a5_4944;
}

/* A5-world globals L07dc touches — named by offset until consumers tell us
 * the semantic. Sizes from the disassembly's tstb / tstl / moveb / movel.
 * Many of these will move to a shared A5-world header once other segments
 * are lifted and the same globals show up there. */
/* g_a5_27990 / g_a5_28006 are macros at the top of the file. */

static void l07dc(void)
{
	PROBE("l07dc");

	if (g_a5_18485 == 0)
		l5124();

	g_a5_18878 = (short)g_a5_18828;
	g_a5_18488 = (unsigned char)(g_a5_18827 - 1);

	for (;;) {
		jt942(0);
		l5888(255);

		if (g_a5_18485 != 0) {
			jt582();
			if (g_a5_27928 == 0) {
				l4b40("No saved games!", 11, 0);
				return;
			}
			g_a5_27990 = g_a5_27989;
			if (g_a5_27990 == 0)
				g_a5_27990 = 4;
			jt941();
		} else {
			if (jt918(1) == 0)
				goto cleanup;
		}

		{
			int special = g_a5_27990 == 3
			           && g_a5_28006 != NULL
			           && ((const unsigned char *)g_a5_28006)[36] == 1;
			if (special) {
				l68f8();
			} else {
				l67ca();
				jt937(g_a5_27932);
				jt938();
			}
		}

		jt217((short)g_a5_12294, (short)g_a5_12293,
		      (short)g_a5_12292, (short)g_a5_12291);
		jt948();
		g_a5_27990 = g_a5_27989;
		if (jt943() == 0)
			break;
	}

cleanup:
	while (g_a5_27932 != 0)
		l2cb0(0, 1);
	l5888(255);
}

/* =========================================================================
 * jt918 — new-game / select-design dialog (CODE 12 + 0x0d90). Skeleton.
 *
 * Big UI function — ~1300 bytes of asm, ~30 inner calls — that L07dc
 * dispatches into when the mode flag says "new game". The Mac body presents
 * a menu (Delete / Create / Select / Play / Edit a Design — the strings
 * are at STRS+0x3132..0x3192) and returns 1 if the player picked an action
 * L07dc should proceed with, 0 if they declined.
 *
 * This is a skeleton lift: the entry side effects (CODE 12 + 0x0d90..0x0dce)
 * are captured; the main loop at L0dd4 → L125e is documented but stubbed,
 * with every inner call (JT[399] / JT[131] / JT[112] / JT[108] / JT[81] /
 * JT[76] / JT[3] / JT[174] / L0aae / L02dc) instrumented for the probe.
 * Returns 0 so engine behaviour matches the prior stub.
 *
 * Loop sketch (for the next pass to fill in):
 *
 *   L0dd4:
 *     JT[112](1)
 *     if (fp@(-10) > 11)  JT[108](1);  else  JT[81]();
 *   L0df6:
 *     if (g_a5_27932 != 0) {
 *       JT[582]() ... (the saved-game-pointer-present arm; sets ~10 A5
 *                      bytes around -14439, branches on g_a5_28006[36]);
 *     } else {
 *       (L0e98 — fresh new-game; initialise the same A5 byte cluster
 *                to its default state);
 *     }
 *   L0ec6:
 *     local = L0aae();                  // input/dispatch
 *     if (local in {1..7} | local > 11) JT[76]();
 *     if (local == 1) L02dc(g_a5_27932);
 *     JT[3](local);                     // per-segment jump dispatch
 *     ... eventually JT[585], JT[447], JT[401], JT[452] ...
 *     goto L0dd4;
 *
 *   Exit edges: L123a (return 0), L123e (return 0). Returning 1 happens
 *   on a path the skeleton does not yet trace.
 * ========================================================================= */

/* Local helpers and JT entries jt918 calls.
 *
 * jt399 (CODE 3 + 0x39d2) lifted: it's the engine's "memset N bytes at
 * buf with the low byte of fill" service. The Mac signature places buf
 * first, size second, fill third (the right-to-left pushed order puts
 * the first C arg at fp@(8) — the last-pushed, shallowest on the stack;
 * earlier stubs of jt399 had the args in the wrong order). */
static void jt399(void *buf, short size, short fill)
{
	unsigned char *p = (unsigned char *)buf;
	short          i;

	PROBE("jt399");
	if (buf == NULL || size <= 0)
		return;
	for (i = 0; i < size; i++)
		p[i] = (unsigned char)fill;
}

/* JT[406] (CODE 3 + 0x366a) — overlap-safe block copy.
 *
 * Thin wrapper over the engine's memmove core (L57f8 in CODE 3):
 * passes (dst, src, count) and lets the inner routine handle the
 * forward/backward copy direction. C's standard memmove gives the
 * same semantics.
 *
 * 153 callsites — every "snapshot a record", "restore from backing
 * store" (L3994's GrafPort-save path), "copy a row of map tiles"
 * etc. uses this. With it lifted, the snapshot/restore plumbing
 * that JT[94] / JT[1089] depend on works correctly. */
static void jt406(void *dst, const void *src, short count) __attribute__((unused));
static void jt406(void *dst, const void *src, short count)
{
	PROBE("jt406");
	if (dst == NULL || src == NULL || count <= 0)
		return;
	memmove(dst, src, (size_t)(unsigned short)count);
}
/* Forward — l3994 (the GrafPort save snapshot) lifts further
 * down. Needed here for JT[112]'s mode-2 case. */
static void l3994(void);

/* JT[112] (CODE 6 + 0x38fe, 43 sites) — paint-mode setter.
 *
 * Stores the low byte of `a` in g_a5_18394 (the GrafPort save-
 * state mode flag). When the new mode is 2, invokes L3994 first
 * to snapshot the current port; jt94 / jt42 / jt1089 read the
 * flag to skip redundant saves on subsequent calls. */
static int  jt112(short a)
{
	PROBE("jt112");
	if ((short)(a & 0xff) == 2)
		l3994();
	g_a5_18394 = (unsigned char)(a & 0xff);
	return 0;
}
static int  jt108(short a)         { PROBE("jt108"); return 0; }                  /* CODE 6 + 0x38d0  */
static void jt81(void)             { PROBE("jt81"); }                             /* CODE 6 + 0x6a10  */

/* --- L5700 / L5864 — mode-cleanup helpers jt131 dispatches into --- *
 *
 * Each one tears down one cached "module" slot. The slot's record is a
 * short tag (cleared on release) + a 4-byte pointer the engine hands to
 * JT[115], plus per-slot sentinels — and, for slot 1, a name buffer the
 * cleanup strcpys an empty string into. The matching JT[3] dispatch in
 * jt131 picks slot 1 (state 0 / 2 -> L5700) or slot 2 (state 3 ->
 * L5864) depending on which mode the engine is leaving.
 */

/* Slot 1 (a5@(-24322..-24262), L5700). Tag short + slot pointer; the
 * Mac call zeros the contiguous 6-byte cluster in a single l5f4e — the
 * lift keeps the fields separate and clears both inline. The name
 * buffer (g_a5_24304) and the per-slot sentinels (g_a5_24262 /
 * g_a5_24256) are declared further down with the rest of L5124's A5
 * cluster — the same engine module owns both ends. */
/* Slot 1 / 2 pointers and tag — all macros in the cluster block. */

/* Forward declarations — jt115 and the L5124-cluster globals these two
 * helpers stamp (g_a5_24304 name buffer, g_a5_24262 / g_a5_24256
 * sentinel bytes) live further down in the file. */
static void           jt115(void *slot_ptr);
/* g_a5_24262 / g_a5_24256 are macros in the L5124 cluster; g_a5_24304
 * stays a file-static name buffer until the cluster's wider-typed
 * migration — forward declared here so L5700 sees it. */
/* g_a5_24304 → macro array (data_pool replay buffer) */
/* L5f4e (CODE 6 + 0x5f4e) — zero N bytes at buf. The public
 * JT[65] entry would route here once a lifted caller needs it. */
static void l5f4e(void *buf, short size) __attribute__((unused));
static void l5f4e(void *buf, short size)
{
	jt399(buf, size, 0);
}

/* JT[384] — strcpy. CODE 3 + 0x3952.
 *
 *   moveal fp@(8),  a4              // a4 = dst
 *   moveal fp@(12), a3              // a3 = src
 *   loop:
 *     moveb (a3)+, (a4)+
 *     bnes  loop                    // continue while byte copied != 0
 *   rts
 */
static void jt384(char *dst, const char *src)
{
	PROBE("jt384");
	if (dst == NULL || src == NULL)
		return;
	while ((*dst++ = *src++) != 0)
		;
}

/* L5700 — slot-1 mode tear-down. CODE 6 + 0x5700. JT[45] route lands here.
 *
 * Disassembly:
 *   pea   a5@(-24320)
 *   jsr   JT[115]                   // release the slot-1 cached block
 *   addql #4, sp
 *   movew #6, -(sp)
 *   pea   a5@(-24322)
 *   jsr   L5f4e                     // zero 6 bytes (tag short + ptr long)
 *   addql #6, sp
 *   pea   STRS+0x750                // pointer to an empty STRS string
 *   pea   a5@(-24304)
 *   jsr   JT[384]                   // strcpy(name, "")
 *   addql #8, sp
 *   moveq #-1, d0
 *   moveb d0, a5@(-24262)           // mark released
 *   rts
 *
 * The strcpy of an empty STRS slot collapses to `name[0] = 0` — the
 * call shape is preserved through ua_strs_at so a future repacking of
 * STRS won't break the behaviour.
 */
static void l5700(void)
{
	PROBE("L5700");
	jt115(&g_a5_24320);
	/* Mac's l5f4e(&a5[-24322], 6) zeros the contiguous tag+ptr cluster.
	 * The lift's separate globals zero individually for the same effect. */
	g_a5_24322 = 0;
	g_a5_24320 = NULL;
	jt384(g_a5_24304, ua_strs_at(0x750));
	g_a5_24262 = 0xFF;
}

/* L5864 — slot-2 mode tear-down. CODE 6 + 0x5864. JT[48] route lands here.
 *
 *   pea   a5@(-24260)
 *   jsr   JT[115]                   // release slot-2 cached block
 *   addql #4, sp
 *   moveq #-1, d0
 *   moveb d0, a5@(-24256)           // mark released
 *   rts
 */
static void l5864(void)
{
	PROBE("L5864");
	jt115(&g_a5_24260);
	g_a5_24256 = 0xFF;
}

/* JT[461] — release a sub-resource by its short tag. CODE 3 + 0xb66.
 *
 * The engine keeps a byte-per-id table at a5@(-10074); each entry is
 * 0 while bound, 0xFF when free. JT[461](tag) just stamps that one
 * byte to 0xFF. The tag is the same first-word value JT[115] reads from
 * a cached block before tearing it down (signed; valid range is set by
 * the engine's id pool size, not bounds-checked at this layer).
 *
 * Original disassembly:
 *   linkw  fp,#0
 *   lea    a5@(-10074),a0
 *   addaw  fp@(8),a0                  // a0 = &a5[-10074 + tag]
 *   moveq  #-1,d0
 *   moveb  d0,a0@                     // *a0 = 0xFF
 *   unlk fp; rts
 *
 * The companion table at a5@(-10026) is the matching 14-byte-per-entry
 * record array JT[465] walks, with the count at a5@(-9306) — we allocate
 * the byte table at 1024 entries to cover any plausible id range the
 * engine asks for, with an out-of-range probe so the next pass that
 * lifts the id-pool init can size it precisely. */
/* g_a5_10074 → macro array (data_pool replay buffer) */
static void jt461(short tag)
{
	PROBE("jt461");
	if (tag < 0 || (unsigned)tag >= G_A5_10074_LEN) {
		PROBE("jt461: tag out of range");
		return;
	}
	g_a5_10074[tag] = 0xFF;
}

/* --- string / char helpers the JT[465] family pulls from CODE 3 ------- */

/* L466a (CODE 3 + 0x466a) — isupper. JT[408] route lands here. */
static int l466a(short ch)
{
	return (ch >= 'A' && ch <= 'Z') ? 1 : 0;
}

/* JT[395] / L46b2 (CODE 3 + 0x46b2) — tolower. JT[395] route lands here. */
static short l46b2(short ch)
{
	return (short)(l466a(ch) ? ch + 32 : ch);
}

/* L39ae (CODE 3 + 0x39ae) — strlen, returns short. JT[423] route lands here. */
static short l39ae(const char *s)
{
	short n = 0;

	if (s == NULL)
		return 0;
	while (*s++ != 0)
		n++;
	return n;
}

/* JT[397] (CODE 3 + 0x3b4e) — signed short max(a, b). 92 callsites.
 * JT[413] (CODE 3 + 0x3b2c) — signed short min(a, b). 107 callsites.
 * JT[423] (CODE 3 + 0x39ae) — signed short strlen. 88 callsites
 * (routes to l39ae). */
static short jt397(short a, short b) __attribute__((unused));
static short jt397(short a, short b)
{
	PROBE("jt397");
	return (a > b) ? a : b;
}
static short jt413(short a, short b) __attribute__((unused));
static short jt413(short a, short b)
{
	PROBE("jt413");
	return (a < b) ? a : b;
}
static short jt423(const char *s) __attribute__((unused));
static short jt423(const char *s)
{
	PROBE("jt423");
	return l39ae(s);
}

/* L3bda (CODE 3 + 0x3bda) — case-insensitive string equal.
 *
 * Walks both strings while their lowercased bytes match. Returns 1 if
 * both reach the terminator together (full match), 0 otherwise.
 */
static int l3bda(const char *s1, const char *s2)
{
	if (s1 == NULL || s2 == NULL)
		return 0;
	while (l46b2((unsigned char)*s1) == l46b2((unsigned char)*s2)) {
		if (*s1 == 0)
			break;
		s1++;
		s2++;
	}
	return (*s1 == 0 && *s2 == 0) ? 1 : 0;
}

/* L3cfa (CODE 3 + 0x3cfa) — strcpy(dst, basename_after_colon(src)).
 *
 *   strlen src, then walk backwards from end-of-string looking for ':'.
 *   If found, advance past it. Then JT[384] (strcpy) the tail into dst.
 *
 * Mac argument order: l3cfa(src, dst) — the call site in JT[465] pushes
 * dst first and src last, so fp@(8) is src and fp@(12) is dst.
 */
static void l3cfa(const char *src, char *dst)
{
	const char *p;

	if (src == NULL || dst == NULL)
		return;
	p = src + l39ae(src);
	while (p > src && *p != ':')
		p--;
	if (*p == ':')
		p++;
	jt384(dst, p);
}

/* The engine keeps its sub-resource cache as four parallel tables:
 *
 *   g_a5_10074 (-10074): 48-entry freemap byte per id — 0xFF means free,
 *                        any other value is the record index that holds
 *                        the id.
 *   g_a5_9354  (-9354):  48-entry byte alias of the freemap; the two
 *                        track each other through L103c (the freemap
 *                        renumbers on removal; arrayB compacts on a
 *                        match). Different access patterns elsewhere.
 *   g_a5_10026 (-10026): 14-byte record per slot. JT[465] treats the
 *                        first bytes as a case-insensitive name.
 *   g_a5_10270 (-10270): long-per-slot offset / heap pointer table.
 *                        Adjacent entries give the byte size of each
 *                        record body via subtraction (j+1 minus j).
 *   g_a5_9306  (-9306):  live record count short.
 *
 * Sizing: the freemap pair is exactly 48 entries (the loop bounds);
 * the record / offset arrays can grow as long as JT465_RECORD_MAX
 * permits. The original DATA pool sizes them once the THINK C runtime
 * replay lands; 64 is enough headroom for the boot paths exercised so
 * far. */
#define JT465_RECORD_BYTES   14
#define JT465_RECORD_MAX     64
/* g_a5_10026 → macro array (data_pool replay buffer) */
/* g_a5_10270 → macro array (data_pool replay buffer) */
/* g_a5_9354 → macro array (data_pool replay buffer) */
/* L1020 / L366a (CODE 3 + 0x1020 / 0x366a) — two three-arg wrappers around
 * the engine's internal BlockMove (L57f8). Same contract as Memory
 * Manager BlockMove: BlockMove(src, dst, count). The two distinguish
 * the third-arg type (long vs sign-extended word); the body is the same. */
static void l1020(const void *src, void *dst, long count)
{
	if (src == NULL || dst == NULL || count <= 0)
		return;
	BlockMove(src, dst, (Size)count);
}

static void l366a(const void *src, void *dst, short count)
{
	if (src == NULL || dst == NULL || count <= 0)
		return;
	BlockMove(src, dst, (Size)count);
}

/* L103c — compact one entry out of the four parallel cache tables.
 * CODE 3 + 0x103c.
 *
 *   Phase 1 (j = 0..47): walk the 48-byte freemap (g_a5_10074) and the
 *     companion byte array (g_a5_9354). For each entry, normalise
 *     against the removal:
 *       freemap[j] == i  →  freemap[j] = 0xFF (mark free; do NOT then
 *                            renumber — the bras after the match skips
 *                            the > check).
 *       freemap[j]  > i  →  freemap[j]-- (renumber).
 *     Same shape on arrayB, with an extra step: if arrayB[j] == i and
 *     j < count, BlockMove arrayB[j+1..count-1] down by one byte to
 *     compact, then fall through to the > check on the now-shifted
 *     arrayB[j].
 *
 *   Phase 2: count--.
 *
 *   Phase 3 (j = i..count-1): walk the offset table (long-per-slot at
 *     g_a5_10270) and the 14-byte record table (g_a5_10026). Move
 *     record j+1's heap body down into record j's slot using a
 *     BlockMove sized by the difference of consecutive offsets
 *     (offset[j+2] - offset[j+1]); rebase offset[j+1] = offset[j] +
 *     that size; strcpy record[j+1] over record[j].
 *
 *   Phase 4: clear the first byte of record[count] (the now-vacated
 *     tail slot — record[old_count - 1]).
 *
 * The original disassembly is in CODE 3 around 0x103c (the body is
 * about 400 bytes); the lift below mirrors it field-for-field.
 */
static void l103c(short i)
{
	short j;

	PROBE("L103c");
	if (i < 0)
		return;

	/* Phase 1 — renumber the 48-entry tables. */
	for (j = 0; j < 48; j++) {
		short v = g_a5_10074[j];

		if (v == i) {
			g_a5_10074[j] = 0xFF;
		} else if (v > i) {
			g_a5_10074[j]--;
		}

		if (j < g_a5_9306) {
			short b = g_a5_9354[j];

			if (b == i) {
				short len = (short)(g_a5_9306 - j - 1);

				if (len > 0)
					l366a(&g_a5_9354[j + 1],
					      &g_a5_9354[j], len);
				/* fall through to the > check on the newly
				 * shifted-down byte at g_a5_9354[j] — that's
				 * what the original asm does (no `bras` here). */
				b = g_a5_9354[j];
			}
			if (b > i)
				g_a5_9354[j]--;
		}
	}

	/* Phase 2 — drop the live count. */
	if (g_a5_9306 > 0)
		g_a5_9306--;

	/* Phase 3 — shift the record / offset arrays from i onwards. */
	for (j = i; j < g_a5_9306; j++) {
		long size = g_a5_10270[j + 2] - g_a5_10270[j + 1];

		l1020((const void *)g_a5_10270[j + 1],
		      (void *)g_a5_10270[j], size);
		g_a5_10270[j + 1] = g_a5_10270[j] + size;
		jt384((char *)&g_a5_10026[j * JT465_RECORD_BYTES],
		      (char *)&g_a5_10026[(j + 1) * JT465_RECORD_BYTES]);
	}

	/* Phase 4 — clear the now-vacated tail slot's first byte. */
	if (g_a5_9306 >= 0
	 && (size_t)(g_a5_9306 * JT465_RECORD_BYTES) < G_A5_10026_LEN)
		g_a5_10026[g_a5_9306 * JT465_RECORD_BYTES] = 0;
}

/* JT[465] — flush records by key. CODE 3 + 0xb7a.
 *
 * Two paths:
 *   - NULL key: reset everything. count = 0; mark the first 48 freemap
 *     bytes as free (0xFF) via jt399.
 *   - non-NULL key: copy the basename (after ':') into a local 200-byte
 *     buffer, then sweep the 14-byte record table. For each record
 *     whose key matches (case-insensitive) call L103c to remove it; the
 *     loop index is decremented on a hit so the just-shifted record at
 *     `i` gets re-checked next iteration.
 *
 * Original disassembly:
 *   linkw  fp,#-202
 *   tstl   fp@(8)
 *   bnes   L0b9c
 *   clrw   a5@(-9306)              // count = 0
 *   movew  #-1,sp@-
 *   movew  #48,sp@-
 *   pea    a5@(-10074)
 *   jsr    L39d2                   // jt399(&freemap, 48, -1)
 *   addql  #8,sp
 *   bras   L0bf0
 *   L0b9c: pea    fp@(-202)         // buf
 *          movel  fp@(8),sp@-       // input key
 *          jsr    L3cfa             // l3cfa(input, buf)
 *          addql  #8,sp
 *          clrb   fp@(-189)         // belt-and-braces NUL @ buf[13]
 *          clrw   fp@(-2)           // i = 0
 *          bras   L0be6
 *   L0bb4: movew  fp@(-2),d0
 *          mulsw  #14,d0
 *          lea    a5@(-10026),a0
 *          addal  d0,a0             // a0 = &record[i]
 *          pea    fp@(-202)
 *          pea    a0@
 *          jsr    L3bda             // l3bda(record, buf) → bool
 *          addql  #8,sp
 *          tstb   d0
 *          beqs   L0be2
 *          movew  fp@(-2),d0
 *          subqw  #1,fp@(-2)        // i--
 *          movew  d0,sp@-
 *          jsr    L103c             // L103c(old i)
 *          addql  #2,sp
 *   L0be2: addqw  #1,fp@(-2)        // i++
 *   L0be6: movew  fp@(-2),d0
 *          cmpw   a5@(-9306),d0
 *          blts   L0bb4
 *   L0bf0: unlk fp; rts
 */
static void jt465(const char *key) __attribute__((unused));
static void jt465(const char *key)
{
	char  buf[200];
	short i;

	PROBE("jt465");

	if (key == NULL) {
		g_a5_9306 = 0;
		jt399(g_a5_10074, 48, -1);
		return;
	}

	l3cfa(key, buf);
	buf[13] = 0;            /* belt-and-braces terminator (Mac clrb at +13) */

	i = 0;
	while (i < g_a5_9306) {
		unsigned char *record =
		    &g_a5_10026[(short)(i * JT465_RECORD_BYTES)];

		if (l3bda((const char *)record, buf)) {
			l103c(i);
			i--;            /* re-check same index after compaction */
		}
		i++;
	}
}

/* JT[115] — generic slot-release service. CODE 6 + 0x31dc.
 *
 * Takes a pointer to a 4-byte slot that holds either NULL or a pointer
 * to a cached block whose first word is a "kind tag" (a non-negative
 * tag means the block is still bound to a sub-resource id; -1 means
 * the engine already released it). The release recipe:
 *
 *   1. If the slot is NULL, nothing to do.
 *   2. Otherwise dereference; if the block's first word is >= 0, call
 *      JT[461] with that tag so the engine returns the id.
 *   3. Stamp the block's first word with -1 (mark as released).
 *   4. Clear the slot to NULL.
 *
 * Original disassembly:
 *   linkw  fp,#0
 *   moveal fp@(8),a0
 *   tstl   a0@                          // *slot
 *   beqs   L3210                        // NULL → bail
 *   moveal fp@(8),a0
 *   moveal a0@,a0                       // a0 = *slot (the block)
 *   tstw   a0@                          // first word of block
 *   blts   L3200                        // already -1 → skip JT[461]
 *   moveal fp@(8),a0
 *   moveal a0@,a0
 *   movew  a0@,sp@-                     // push tag
 *   jsr    JT[461]
 *   addql  #2,sp
 *   L3200: moveal fp@(8),a0
 *          moveal a0@,a0
 *          moveq  #-1,d0
 *          movew  d0,a0@                // mark block as released
 *          moveal fp@(8),a0
 *          clrl   a0@                   // *slot = NULL
 *   L3210: unlk fp; rts
 */
static void jt115(void *slot_ptr)
{
	void  **slot;
	short  *block;

	PROBE("jt115");
	if (slot_ptr == NULL)
		return;
	slot = (void **)slot_ptr;
	if (*slot == NULL)
		return;
	block = (short *)*slot;
	if (*block >= 0)
		jt461(*block);
	*block = -1;
	*slot  = NULL;
}

/* A5 globals jt209 / jt204 manage.
 *
 *  g_a5_27894[0..2]: the three 4-byte slots jt209 walks via JT[115].
 *                    Each one points at a sub-resource (sound / strike /
 *                    something the engine paged in for the previous mode);
 *                    JT[115] releases it and clears the slot.
 *  g_a5_22222:       single 4-byte slot jt204 hands to JT[115] — same
 *                    contract as one of the jt209 slots.
 *  g_a5_12296:       short sentinel jt209 stamps to -1 when its `flag`
 *                    arg is non-zero. The shutdown path passes flag=0 so
 *                    the sentinel only fires on entry to state 4 (via
 *                    jt131's destination-side init). The next pass that
 *                    lifts the consumers will tell us what the flag means.
 *  g_a5_12289:       byte sentinel jt204 stamps to 0xFF on every call.
 */
/* g_a5_27894 → macro array (data_pool replay buffer) */
/* g_a5_12296 → macro (data_pool replay buffer) */
/* g_a5_12289 → macro (data_pool replay buffer) */
/* jt209 — release three sub-resources, optionally trip the entry
 * sentinel. CODE 7 + 0x70e8.
 *
 * Original disassembly:
 *   linkw fp,#-2
 *   clrw  fp@(-2)                      // i = 0
 *   bras  L710c                        // entry test first
 *   L70f2: movew fp@(-2),d0
 *          extl  d0; asll #2,d0        // d0 = i * 4
 *          lea   a5@(-27894),a0
 *          addal d0,a0
 *          pea   a0@                   // &a5[-27894 + i*4]
 *          jsr   JT[115]
 *          addql #4,sp
 *          addqw #1,fp@(-2)            // i++
 *   L710c: cmpiw #3,fp@(-2)
 *          blts  L70f2                 // while (i < 3)
 *          tstb  fp@(9)                // arg is a Boolean byte
 *          beqs  L7120
 *          moveq #-1,d0
 *          movew d0,a5@(-12296)        // a5@(-12296) = -1
 *   L7120: unlk fp; rts
 *
 * fp@(9) is the low byte of the word the caller pushed for the byte
 * arg; we model it as a `short` so the Mac C calling convention drops
 * the right cell on the stack.
 */
static void jt209(short flag)
{
	short i;

	PROBE("jt209");
	for (i = 0; i < 3; i++)
		jt115(&g_a5_27894[i]);
	if ((flag & 0xFF) != 0)
		g_a5_12296 = -1;
}

/* jt204 — release one sub-resource and trip its sentinel.
 * CODE 7 + 0x6ed8.
 *
 *   pea   a5@(-22222)
 *   jsr   JT[115]
 *   addql #4,sp
 *   moveq #-1,d0
 *   moveb d0,a5@(-12289)              // a5@(-12289) = 0xFF
 *   rts
 */
static void jt204(void)
{
	PROBE("jt204");
	jt115(&g_a5_22222);
	g_a5_12289 = 0xFF;
}

/* a5@(-31234) — the engine's "current mode" word that jt131 manages.
 *
 * Tracks which top-level UI / play state the engine is in. jt131 picks a
 * tear-down for the OLD value (case 0 / 2 / 3 dispatch via JT[3]) before
 * committing the NEW one, then runs an init for state 4 if that's the
 * destination. Initialised to 0 — the same default the THINK C DATA
 * pool would supply (the slot lives in the zero-filled A5 cluster
 * around the play-loop globals). */
/* g_a5_31234 → macro (data_pool replay buffer) */
/* jt131 — engine state-transition manager. CODE 6 + 0x35e.
 *
 * Original disassembly:
 *   linkw   fp,#0
 *   movew   fp@(8),d0
 *   cmpw    a5@(-31234),d0
 *   beqw    L03a6                  // already in state a: just store back
 *   movew   a5@(-31234),d0         // d0 = current state
 *   jsr     a5@(58)                // JT[3] — segment-jump case dispatch
 *     [post-call data: lo=0, hi=6, default=L03a6,
 *      case 0=L038a, case 1=L03a6, case 2=L039c, case 3=L03a2,
 *      case 4..6 = L03a6]
 *   L038a:  jsr L5700; clrw -(sp); jsr JT[209]; addql #2,sp; jsr JT[204]
 *           bras L03a6
 *   L039c:  jsr L5700; bras L03a6
 *   L03a2:  jsr L5864; fall through
 *   L03a6:  movew  fp@(8),a5@(-31234)        // commit new state
 *           cmpiw  #4,fp@(8)
 *           bnes   L03be
 *           pushw  1; jsr JT[209]; addql #2,sp
 *   L03be:  unlk fp; rts
 *
 * So: pure state-transition driver. The OLD value picks the tear-down
 * branch; only states 0, 2, 3 have one — every other previous state
 * skips the dispatch entirely. State 4 is special: entering it kicks
 * jt209(1). The "if (a == old) return" early-out keeps the engine from
 * tearing down the same state it's already in.
 */
static void jt131(short a)
{
	short old = g_a5_31234;

	PROBE("jt131");
	if (a == old)
		return;
	switch (old) {
	case 0:
		l5700();
		jt209(0);
		jt204();
		break;
	case 2:
		l5700();
		break;
	case 3:
		l5864();
		break;
	default:
		break;
	}
	g_a5_31234 = a;
	if (a == 4)
		jt209(1);
}

static void l4bf6(short a, short b, short c, short d) { PROBE("l4bf6"); }         /* CODE 6 + 0x4bf6  */

/* JT[468] (CODE 3 + 0xd1a) — translates a short arg into a long sub-
 * resource handle / channel pointer. Real body chains into the fc
 * cache; stays a PROBE stub for now. */
/* JT[468] (CODE 3 + 0x0d1a, 64 sites) — resource handle lookup.
 *
 * Indirect table walk: g_a5_10074[tag] is a signed-byte slot index
 * into g_a5_10270 (an array of longs holding resource handles).
 * Returns g_a5_10270[(signed char)g_a5_10074[tag]] — the cached
 * handle for the resource the caller's tag points to.
 *
 * The Mac also calls L1282 first (resource-load validation); for
 * the port the tables are populated by data_pool_replay so the
 * load step is implicit. Return 0 on out-of-range. */
static long jt468(short tag)
{
	short id;

	PROBE("jt468");
	if (tag < 0 || (unsigned short)tag >= (unsigned short)G_A5_10074_LEN)
		return 0;
	id = (signed char)g_a5_10074[tag];
	if (id < 0 || (unsigned short)id >= (unsigned short)G_A5_10270_LEN)
		return 0;
	return g_a5_10270[id];
}

/* L309c (CODE 5 + 0x309c, local) — the actual channel-write that
 * jt1001 wraps. Reads four args (channel, mode, ptr, flag) and pokes
 * the engine's 8000-page channel array. PROBE-only until lifted. */
static void l309c(short a, short b, long c, short d)
{
	PROBE("L309c");
	(void)a; (void)b; (void)c; (void)d;
}

/* JT[1001] — the workhorse "select sub-resource n, write to channel"
 * service. CODE 5 + 0x31ac.
 *
 *   linkw fp, #0
 *   movew fp@(12), sp@-          ; push b
 *   jsr   JT[468]                  ; d0 = jt468(b)
 *   addql #2, sp
 *   movew fp@(14), sp@-           ; push d (fourth caller arg)
 *   movel d0, sp@-                 ; push the long jt468 returned
 *   movew fp@(10), sp@-            ; push c
 *   movew fp@(8), sp@-             ; push a
 *   jsr   L309c                    ; l309c(a, c, jt468(b), d)
 *   lea   sp@(10), sp
 *   unlk fp; rts
 *
 * jt76 / l66e6 / jt80 / l67ca / L0aae all call this with (page-base,
 * channel-id, 1, mode-byte) tuples. jt468 owns the page → ptr map; the
 * real machinery follows when an engine path actually demands audio /
 * graphics output.
 */
static void jt1001(short a, short b, short c, short d)
{
	long t;

	PROBE("jt1001");
	t = jt468(b);
	l309c(a, c, t, d);
}
static void jt174(void);                                                          /* CODE 7 + 0x2062 (lifted below) */

static void l66e6(short n)
{
	/* CODE 6 + 0x66e6: jt1001(8000 + n*4, 8000, 1, 7). The first arg
	 * marches in 4-unit steps from a base of 8000, so this looks like
	 * "select sub-resource n from a four-channel array". */
	PROBE("l66e6");
	jt1001((short)(8000 + (short)(n << 2)), 8000, 1, 7);
}

static void jt76(void)
{
	/* CODE 6 + 0x670c — a fixed setup sequence: clear via jt108, set
	 * dimensions via L4bf6, then four channel-init calls through
	 * JT[1001], closing with jt174's two-byte flag. */
	PROBE("jt76");
	(void)jt108(0);
	l4bf6(22, 38, 1, 1);
	jt1001(8000, 8000, 1, 1);
	jt1001(8000, 8000, 1, 2);
	jt1001(8000, 8000, 1, 3);
	jt1001(8000, 8000, 1, 4);
	jt174();
}
/* JT[3] (CODE 1 + 0x158) — THINK C inline switch runtime.
 *
 * Every C `switch` statement in the Mac build compiles to a JSR JT[3]
 * followed by an inline table living in code immediately after the
 * call site. Layout:
 *
 *      jsr   JT[3]
 *      .short min               ; lowest case label
 *      .short max               ; highest case label
 *      .short default_offset    ; PC-rel offset to the default arm
 *      .short case0_offset      ; PC-rel offset to case `min`
 *      .short case1_offset      ; PC-rel offset to case `min+1`
 *      ...
 *      .short caseN_offset      ; PC-rel offset to case `max`
 *
 * The body pops the return PC, indexes the table, then JMPs to the
 * chosen arm — so control resumes at one of the case bodies rather
 * than the instruction after the JSR. Each table is unique per call
 * site.
 *
 * The lift cannot model JT[3]'s "follow the inline PC" trick from
 * C, so the project's convention is: at every JT[3] call site,
 * read the inline table from the disassembly and emit an
 * equivalent C `switch (value) { case ... }` in the lift. The stub
 * below stays for unlifted callers (and asserts via the PROBE that
 * any call to it is unexpected — every lifted caller should be
 * using a real `switch`).
 *
 * jt3_dispatch is provided for the rare table that's wider than a
 * comfortable C switch — it returns the case index 0..n-1 when
 * `value` is in `[min, min+n-1]`, or -1 for the default arm. Lifted
 * callers can hand-write a `switch` instead. */
static int  jt3(short value) __attribute__((unused));
static int  jt3(short value)
{
	PROBE("jt3");
	/* Stub for unlifted callers — see the comment above. */
	(void)value;
	return -1;
}

static int  jt3_dispatch(short value, short min, short n_cases)
                                                __attribute__((unused));
static int  jt3_dispatch(short value, short min, short n_cases)
{
	if (value < min || value >= (short)(min + n_cases))
		return -1;
	return value - min;
}

/* --- l67ca: L07dc's saved-game branch tear-down + redraw ----------- *
 *
 * Called from L07dc when the engine is committing the saved-game arm
 * (the path that L68f8 / L66e6 / jt941 also live on). Side effects:
 *
 *   1. jt76()                  — the four-channel reset run by every
 *                                state transition in this family
 *   2. l66e6(16); l66e6(12)    — clear sub-resources 16 and 12 from
 *                                the 8000-page channel array
 *   3. jt80(2)                 — toggle the secondary mode flag (more
 *                                below)
 *   4. jt1001(8000, 8000, 1, 9) + jt1001(8000, 8000, 1, 21) — the
 *                                shared "redraw frame" prep calls
 *   5. JT[1] case dispatch by the direction byte at
 *      g_a5_27980[g_a5_12286 * 3] — one of 'E' / 'N' / 'S' / 'W'
 *      picks a per-side JT[1001] (channels 25 / 22 / 23 / 24); any
 *      other value falls through to the default tail
 *   6. l08e6(1)                — set the post-transition redraw flag
 *
 * The original disassembly:
 *
 *   L67ca: jsr L670c                  // jt76
 *          pushw 16; jsr L66e6
 *          pushw 12; jsr L66e6
 *          pushw 2;  jsr L68ae        // jt80
 *          pushw 9 / 1 / 8000 / 8000; jsr JT[1001]
 *          pushw 21 / 1 / 8000 / 8000; jsr JT[1001]
 *          moveq #0, d0
 *          moveb a5@(-12286), d0       // d0 = mode flag (0..N)
 *          muluw #3, d0
 *          lea   a5@(-27980), a0
 *          addal d0, a0                // a0 = &dir_table[mode * 3]
 *          moveb a0@, d0; extw d0
 *          jsr   JT[1]                 // 5-pair (offset, key) table:
 *             ('E' -> +0x12, 'N' -> +0x26, 'S' -> +0x3a, 'W' -> +0x4e,
 *              default -> +0x60)
 *          (per-direction JT[1001] case bodies — channel 25/22/23/24)
 *   L68a2: pushw 1; jsr L08e6
 *          rts
 *
 * The direction table at g_a5_27980 is fed by the THINK C DATA pool
 * (not replayed yet) — the lifted lookup reads zeros, so the switch
 * always lands in the default arm. Once DREL is replayed, the
 * direction-specific JT[1001] calls fire correctly.
 */
/* g_a5_13046 → macro (data_pool replay buffer) */
/* g_a5_31230 → macro (data_pool replay buffer) */
/* g_a5_27980 → macro array (data_pool replay buffer) */
/* g_a5_12286 → macro (data_pool replay buffer) */
/* JT[80] / L68ae — toggle the secondary mode flag, with a per-state
 * cleanup. CODE 6 + 0x68ae.
 *
 *   if (arg != 0) {
 *       g_a5_13046 = (arg != 2) ? 1 : 0;
 *   } else if (g_a5_13046 != 0) {
 *       g_a5_13046 = 0;
 *       jt108(1);
 *       jt1001(8000, 8000, 1, 9);
 *   }
 */
static void jt80(short arg)
{
	unsigned char a = (unsigned char)(arg & 0xFF);

	PROBE("jt80");
	if (a != 0) {
		g_a5_13046 = (unsigned char)(a != 2 ? 1 : 0);
		return;
	}
	if (g_a5_13046 == 0)
		return;
	g_a5_13046 = 0;
	(void)jt108(1);
	jt1001(8000, 8000, 1, 9);
}

/* L08e6 — store the byte arg into the redraw flag g_a5_31230. CODE 6
 * + 0x08e6. Just `linkw / moveb fp@(9), a5@(-31230) / unlk / rts`. */
static void l08e6(short arg)
{
	PROBE("L08e6");
	g_a5_31230 = (unsigned char)(arg & 0xFF);
}

static void l67ca(void)
{
	short letter;

	PROBE("l67ca");
	jt76();
	l66e6(16);
	l66e6(12);
	jt80(2);
	jt1001(8000, 8000, 1, 9);
	jt1001(8000, 8000, 1, 21);

	/* Direction byte at g_a5_27980[g_a5_12286 * 3], sign-extended. */
	letter = (short)(signed char)g_a5_27980[g_a5_12286 * 3];
	switch (letter) {
	case 'E': jt1001(8000, 8000, 1, 25); break;
	case 'N': jt1001(8000, 8000, 1, 22); break;
	case 'S': jt1001(8000, 8000, 1, 23); break;
	case 'W': jt1001(8000, 8000, 1, 24); break;
	default:                              break;
	}
	l08e6(1);
}

/* g_a5_12912 → macro (data_pool replay buffer) */
/* g_a5_12911 → macro (data_pool replay buffer) */
static void jt174(void)
{
	/* CODE 7 + 0x2062 — three insns: g_a5_12911 = g_a5_12912 = 1. */
	PROBE("jt174");
	g_a5_12912 = 1;
	g_a5_12911 = 1;
}
static int  l0aae(void);                                                          /* CODE 12 + 0x0aae — lifted below */

/* JT entries L02dc paints through.  All seven are QuickDraw wrappers
 * inside CODE 6 — they land in the window-paint cluster that the
 * display HAL hasn't been wired to yet, so they stay PROBE stubs and
 * the lift below records the call sequence rather than the pixels. */
static void jt25(long entry, short page, short row, short style)
                                            { PROBE("jt25"); (void)entry;
                                              (void)page; (void)row;
                                              (void)style; }
static void jt32(long entry, short col, short row, short a, short b)
                                            { PROBE("jt32"); (void)entry;
                                              (void)col; (void)row;
                                              (void)a; (void)b; }
static void jt34(long entry, short col, short row, short style)
                                            { PROBE("jt34"); (void)entry;
                                              (void)col; (void)row;
                                              (void)style; }

/* JT[96] (CODE 6 + 0x43c4, 43 sites) — 9-arg record-body paint
 * helper. The jt18 / jt20 record renderer dispatch through here
 * for the row of stats below the entry's name. PROBE-deferred;
 * the inner paint primitives (JT[1135] / JT[1161] / JT[1089])
 * are in place, but jt96's body has substantial coord/clip logic
 * that's its own commit. */
static void jt96(short page, short row, short width, short height,
                  short s5,   short s6,  short s7,
                  long  val,  short s9)
{
	PROBE("jt96");
	(void)page; (void)row; (void)width; (void)height;
	(void)s5;   (void)s6;  (void)s7;
	(void)val;  (void)s9;
}

/* Forward — jt103 / jt1200 / l4bac are defined further down; jt18
 * and jt20 call them. jt20 lifts immediately after jt18 and is
 * tail-called via L241e from jt18's post-paint arm. */
static void jt103(short top, short left, short right, short bottom);
static int  jt1200(void);
static void l4bac(void);
static void jt20(void);

/* JT[18] (CODE 6 + 0x22da, 115 callsites) — record-window paint
 * dispatcher.
 *
 * Opens the right window (encounter-mode record sheet vs design-
 * edit row), optionally draws the entry's name header via JT[25],
 * and renders the row of stats via L43c4. Closes with the scroll-
 * advance + JT[20] arm when the caller's byte flag is set.
 *
 *   p          — record pointer (NULL skips the name header).
 *   val        — long passed through to L43c4; in design-edit
 *                mode also stands in for `p` on the JT[25] call.
 *   s1         — design-edit row (only read when mode == 5).
 *   byte_flag  — post-paint scroll trigger.
 *
 * Mode dispatch on g_a5_27990:
 *   != 5 (encounter):
 *     base_y = (g_a5_23190 != 0) ? 18 : 17;
 *     jt103(1, base_y, 38, 22);
 *     if p:  jt25(p, 1, base_y+1, 0);
 *     jt96(1, base_y+2, 38, 22, 7, 0, 1, val, base_y);
 *
 *   == 5 (design-edit):
 *     jt103(23, s1, 38, 21);
 *     if jt1200() == 3:
 *        if p:  jt25(val, 23, s1, 0);
 *     else:
 *        if p:  jt25(val, 23, s1, 0);
 *     jt96(23, s1+1, 38, 21, 7, 0, 1, val, s1);
 *
 * Both JT[1200] arms in the design-edit path do the same thing
 * for `p != NULL` — the asm's redundant compare is preserved
 * here as a single inline check. */
static void jt18(void *p, long val, short s1, short byte_flag) __attribute__((unused));
static void jt18(void *p, long val, short s1, short byte_flag)
{
	short base_y;

	PROBE("jt18");

	if (g_a5_27990 == 5) {
		jt103(23, s1, 38, 21);
		(void)jt1200();
		if (p != NULL)
			jt25(val, 23, s1, 0);
		jt96(23, (short)(s1 + 1), 38, 21, 7, 0, 1, val, s1);
	} else {
		base_y = (g_a5_23190 != 0) ? (short)18 : (short)17;
		jt103(1, base_y, 38, 22);
		if (p != NULL)
			jt25((long)(uintptr_t)p, 1,
			     (short)(base_y + 1), 0);
		jt96(1, (short)(base_y + 2), 38, 22, 7, 0, 1,
		      val, base_y);
	}

	if (byte_flag != 0) {
		l4bac();
		jt20();
	}
}

/* JT[20] (CODE 6 + 0x241e, 94 callsites) — record-window opener.
 * Opens the encounter (mode != 5) or design-edit (mode == 5)
 * frame at the matching dimensions; jt18's post-paint arm
 * dispatches here. */
static void jt20(void)
{
	PROBE("jt20");
	if (g_a5_27990 == 5)
		jt103(23, 10, 38, 21);
	else
		jt103(1, 17, 38, 22);
}

/* Forward decls for the leaves JT[94] dispatches into. The bodies
 * land further down — jt1200 already lifted, l3994 / jt1089 still
 * PROBE-stubbed. */
static int  jt1200(void);
static void l3994(void);
static void jt1089(short x, short y, short color,
                   const char *fmt, ...);

/* JT[94] (CODE 6 + 0x3fd6) — text paint with style / coord remap.
 *
 *   1. vsprintf the caller's format into a 96-byte local buffer
 *      via JT[394] (THINK C's "%r" recursive-format mode).
 *   2. l3994() snapshots the GrafPort.
 *   3. Style remap when style != 0:
 *        if page < 23:  style = 8; col = 15 if col was 7 or 8
 *        else (page 23, bottom row): style = 8 unconditionally
 *   4. Dispatch on style + game mode:
 *        style == 24            → rect-erase + box, JT[1161] x4 + L3f88
 *                                 (the "alert frame" overlay).
 *        g_a5_27990 == 5 && page == 23
 *                              → mode-5 bottom-row paint via L3f88.
 *        default                → simple text draw via JT[1089].
 *
 * The two non-default branches stay PROBE-deferred — both pull in
 * the rect-fill paint primitive (JT[1161]) and the L3f88 helper,
 * which need the display-HAL fill stroke first. The default arm
 * is the hot path (the design-edit roster columns, the menu
 * labels, every static "Name" / "AC HP" header) and lifts here.
 *
 * The post-jt1200 col==11 remap models the asm's redundant double
 * cmpiw #11 — the second compare is dead code in C. */
static void jt94(short page, short row, short col, short style,
                 const char *fmt, ...)
{
	char        local_buf[96];
	va_list     ap;
	short       x;
	short       y;
	short       color;

	PROBE("jt94");

	/* 1. format into local_buf. The Mac calls jt394 with "%r" plus
	 * caller_fmt + va_args; the equivalent in C is a direct
	 * vsprintf with the caller's fmt. */
	va_start(ap, fmt);
	if (fmt != NULL)
		vsprintf(local_buf, fmt, ap);
	else
		local_buf[0] = 0;
	va_end(ap);

	/* 2. snapshot GrafPort. */
	l3994();

	/* 3. style remap. */
	if (style != 0) {
		if (page < 23) {
			if (col == 7 || col == 8)
				col = 15;
		}
		style = 8;
	}

	/* 4a. style == 24 erase-and-box (rect-fill cluster). */
	if (style == 24) {
		PROBE("jt94/style24-rect-frame");
		return;
	}

	/* 4b. mode-5 bottom-row paint. */
	if (g_a5_27990 == 5 && page == 23) {
		PROBE("jt94/mode5-bottom-row");
		return;
	}

	/* 4c. default arm — simple text draw. */
	x = (short)((page << 2) + 8000);
	if (jt1200() == 3 && col == 11) {
		col   = 0;
		style = 15;
	}
	y     = (short)((row  << 2) + 8000);
	color = (short)((style << 4) | (unsigned char)col);
	jt1089(x, y, color, ua_strs_at(0x6c0) /* "%s" */, local_buf);
}
static void jt97(short col, short row, short page, short style,
                 short a, short ch, short flag)
                                            { PROBE("jt97"); (void)col;
                                              (void)row; (void)page;
                                              (void)style; (void)a;
                                              (void)ch; (void)flag; }
static void jt103(short top, short left, short right, short bottom)
                                            { PROBE("jt103"); (void)top;
                                              (void)left; (void)right;
                                              (void)bottom; }

/* JT[1135] (CODE 4 + 0x77fe) — 2D coordinate transform.
 *
 * The engine works in a logical coord space; paint primitives map
 * to screen pixels through this transform. Values <= 6000 pass
 * through unchanged; values > 6000 are treated as offsets from
 * 8000 (a screen-edge anchor) and scaled by the mode-dependent
 * factor — 3 outside encounters, 2 inside (g_a5_2347 == 0 vs not).
 *
 * Used by JT[94] (text paint) and other paint primitives so the
 * same caller code works in both the design-edit overlay and the
 * encounter window. */
static void jt1135(short v1, short v2, short *out1, short *out2)
                                                __attribute__((unused));
static void jt1135(short v1, short v2, short *out1, short *out2)
{
	short scale = (g_a5_2347 == 0) ? 3 : 2;

	PROBE("jt1135");
	if (out1 != NULL)
		*out1 = (v1 > 6000) ? (short)((v1 - 8000) * scale) : v1;
	if (out2 != NULL)
		*out2 = (v2 > 6000) ? (short)((v2 - 8000) * scale) : v2;
}

/* L3994 (CODE 6 + 0x3994) — GrafPort save / paint setup. Reads
 * g_a5_18394 / 18393 / 18395 state flags, conditionally invokes
 * JT[468] / JT[1012] / JT[406] (memcpy) for the backing-store
 * snapshot and JT[1128] / JT[1153] for clip restore. PROBE for
 * now — the snapshot machinery wires once the QuickDraw shim
 * publishes GrafPort state. */
static void l3994(void)                 { PROBE("l3994"); }

/* JT[1089] (CODE 5 + 0x334) — text paint at logical (x, y).
 *
 * Body composition (faithful to the Mac asm):
 *
 *      L024c(color)                      // pen color/style → A5 cluster
 *      L0264(x, y)                       // jt1135 remap → screen px
 *      L0306("%r", fmt, &args)           // vsprintf then draw
 *
 * The C lift collapses the three local helpers inline:
 *
 *      g_a5_4894/4892 ← color           (decomposed)
 *      jt1135(x, y, &g_a5_4898, &g_a5_4896)
 *      MoveTo(g_a5_4898, g_a5_4896)
 *      vsprintf(buf, fmt, ap)
 *      DrawString(pascal_form(buf))
 *
 * The engine's color byte (low nibble of the color word) goes into
 * the current GrafPort's fgColor so the palette-indexed paint lands
 * on the right pen. With this in place every JT[94] / JT[42] caller
 * that reaches the default arm actually paints text into the
 * window's framebuffer. */
static void jt1089(short x, short y, short color,
                   const char *fmt, ...)
{
	char    buf[256];
	va_list ap;
	short   px = 0;
	short   py = 0;
	GrafPtr       port;
	unsigned char pstr[256];
	short         len;
	short         i;

	PROBE("jt1089");

	/* L024c: split the color word into the A5 pen-state slots. */
	g_a5_4894 = color;
	g_a5_4892 = (unsigned char)((color >> 8) & 0xff);

	/* Apply foreground index to the current port. The engine's 0..15
	 * color nibble indexes the FRUA CLUT loaded at boot. */
	GetPort(&port);
	if (port != NULL)
		((CGrafPtr)port)->fgColor =
			(unsigned char)(color & 0x0f);

	/* L0264: transform (x, y) via jt1135 + park in the pen slots,
	 * then MoveTo there. */
	jt1135(x, y, &g_a5_4898, &g_a5_4896);
	px = g_a5_4898;
	py = g_a5_4896;
	MoveTo(px, py);

	/* L0306: format the caller's args + DrawString. The Mac path
	 * dispatches through JT[400]'s emit-char callbacks; the
	 * portable equivalent is plain vsnprintf + DrawString. */
	va_start(ap, fmt);
	if (fmt != NULL)
		vsnprintf(buf, sizeof buf, fmt, ap);
	else
		buf[0] = 0;
	va_end(ap);

	len = 0;
	while (len < (short)(sizeof buf) - 1 && buf[len] != 0)
		len++;
	if (len > 255)
		len = 255;
	pstr[0] = (unsigned char)len;
	for (i = 0; i < len; i++)
		pstr[i + 1] = (unsigned char)buf[i];
	DrawString(pstr);
}

/* JT[1161] (CODE 4 + 0x1aa0) — clipped rectangle fill.
 *
 *   1. jt1135-transform both corners into screen pixels.
 *   2. Clip against the window-bounds cluster
 *      (g_a5_3054 / 3056 / 3050 / 3052 — top / left / bottom / right);
 *      bail if the clipped rect is degenerate.
 *   3. Encounter-mode branch (g_a5_1312 != 0): low byte of `fill`
 *      gets remapped through JT[1006] before a custom paint call
 *      (L19be). PROBE-deferred — needs the encounter-window paint
 *      primitives first.
 *   4. Default path: set the foreground color from the low nibble
 *      of `fill` and PaintRect via the QuickDraw shim.
 *
 * 147 callsites — every "frame border" / "erase a row" / "draw the
 * '*' overlay" in the engine flows through here. With this lifted,
 * JT[94]'s style==24 rect-frame arm and JT[97]'s overlay land
 * pixels too. */
static void jt1161(short top, short left, short bottom, short right,
                   short fill) __attribute__((unused));
static void jt1161(short top, short left, short bottom, short right,
                   short fill)
{
	short    y1, x1, y2, x2;
	short    ct, cl, cb, cr;
	GrafPtr  port;
	Rect     r;

	PROBE("jt1161");

	jt1135(top,    left,  &y1, &x1);
	jt1135(bottom, right, &y2, &x2);

	/* Clip: max(coord, window top/left), min(coord, window bot/right). */
	ct = (y1 > g_a5_3054) ? y1 : g_a5_3054;
	cl = (x1 > g_a5_3056) ? x1 : g_a5_3056;
	cb = (y2 < g_a5_3050) ? y2 : g_a5_3050;
	cr = (x2 < g_a5_3052) ? x2 : g_a5_3052;

	if (ct >= cb || cl >= cr)
		return;

	if (g_a5_1312 != 0) {
		PROBE("jt1161/encounter-fill");
		return;
	}

	/* Default arm — low nibble = palette index for the fill. */
	GetPort(&port);
	if (port != NULL)
		((CGrafPtr)port)->fgColor =
			(unsigned char)(fill & 0xff);

	SetRect(&r, cl, ct, cr, cb);
	PaintRect(&r);
}
/* JT[1200] (CODE 4 + 0x04f0) — encounter-mode state classifier.
 *
 * Reads two A5 byte flags and returns one of three states the
 * design-edit overlays branch on:
 *
 *      g_a5_2347 == 0   → 3   (not in encounter mode; designs visible)
 *      g_a5_2347 != 0 && g_a5_1312 != 0  → 0
 *      g_a5_2347 != 0 && g_a5_1312 == 0  → 1
 *
 * L02dc's "*" overlay fires when this returns 3 AND the entry's byte
 * at offset 197 is set — i.e., the entry is poisoned/dead and we're
 * NOT in an encounter so the marker is meaningful. The 187 callsites
 * dotted across the engine are all this same shape: read state, branch
 * on 0/1/3. */
static int  jt1200(void)
{
	PROBE("jt1200");
	if (g_a5_2347 == 0)
		return 3;
	return (g_a5_1312 != 0) ? 0 : 1;
}

/* L02dc (CODE 12 + 0x02dc) — Modify Character roster grid.
 *
 * Repaints the design-edit roster: per-row name, then HP / overlay /
 * AC drawn into the right-hand columns.  Called from jt918 / L0ec6
 * with arg = g_a5_27932 (the currently-highlighted entry, used to
 * pick the "%s" highlight via JT[94] instead of the normal JT[25]).
 *
 *   1. Skip when:
 *        g_a5_27990 == 3 && g_a5_18471 == 0 &&
 *        g_a5_28006->b36 == 1 && g_a5_28006->b133 == 0
 *      (a non-design screen has the right record loaded), or when
 *      g_a5_27987 != 0 (mid-action).
 *   2. Page selector g_a5_24131 = (g_a5_27990 == 0) ? 1 : 17;
 *      grid rect g_a5_24129 = 4, g_a5_24130 = 38, g_a5_24128 = 3.
 *   3. Draw "Name" and "AC HP" headers at row 2 via JT[94].
 *   4. Walk g_a5_27928 (head, .next at offset 0).  Per entry:
 *        - g_a5_24128 += 1 (grow the rect).
 *        - separator at current row via JT[103].
 *        - if (entry == highlight) highlight via JT[94]("%s", name);
 *          else regular draw via JT[25].
 *        - HP byte = entry[385].  Classify into colour 0/1/2 and
 *          draw via JT[34] at col 32 + colour.
 *        - AC byte = entry[395].  Classify; if JT[1200]() == 3 and
 *          entry[197] != 0, paint a "*" overlay via JT[97] at col 35.
 *          Draw AC via JT[32] at col 36 + colour.
 *      Step row, advance.
 *   5. Footer: if row < 12, draw final separator via JT[103]. */
static void l02dc(long highlight)
{
	const unsigned char *handle = (const unsigned char *)g_a5_28006;
	const unsigned char *entry;
	short page;
	short row;
	short hp;
	short ac;
	short colour;

	PROBE("L02dc");

	if (g_a5_27990 == 3 && g_a5_18471 == 0 &&
	    handle != NULL && handle[36] == 1 &&
	    (short)handle[133] == 0)
		return;
	if (g_a5_27987 != 0)
		return;

	page = (g_a5_27990 == 0) ? (short)1 : (short)17;
	g_a5_24131 = (unsigned char)page;
	g_a5_24129 = 4;
	g_a5_24130 = 38;
	g_a5_24128 = 3;

	row = 2;
	jt94(page, row, 12, 0, ua_strs_at(0x5e5e));      /* "Name"  */
	jt94(page, row, 33, 0, ua_strs_at(0x5e64));      /* "AC HP" */
	row += 2;

	entry = (const unsigned char *)(uintptr_t)g_a5_27928;
	while (entry != NULL) {
		g_a5_24128 += 1;
		jt103(page, row, 38, row);

		if ((long)(uintptr_t)entry == highlight) {
			jt94(page, row, 11, 0,
			     ua_strs_at(0x5e6a),                /* "%s" */
			     &entry[96]);
		} else {
			jt25((long)(uintptr_t)entry, page, row, 0);
		}

		hp = entry[385];
		if (hp == 0 || hp > 69)
			colour = 0;
		else if (hp < 50)
			colour = 1;
		else if (hp < 60)
			colour = 2;
		else
			colour = 1;
		jt34((long)(uintptr_t)entry,
		     (short)(32 + colour), row, 0);

		ac = entry[395];
		if (ac > 99)
			colour = 0;
		else if (ac > 9)
			colour = 1;
		else
			colour = 2;

		if (jt1200() == 3 && entry[197] != 0)
			jt97(35, row, 12, 0, 1, 42, 1);  /* "*" overlay */

		jt32((long)(uintptr_t)entry,
		     (short)(36 + colour), row, 0, 0);

		row += 1;
		entry = *(const unsigned char * const *)entry;  /* .next */
	}

	if (row < 12)
		jt103(page, row, 38, row);
}

/* Additional A5-world globals jt918 touches (entry setup + buffer). */
/* g_a5_5798 → macro (data_pool replay buffer) */
/* g_a5_5796 → macro (data_pool replay buffer) */
/* g_a5_19169 → macro (data_pool replay buffer) */
/* g_a5_22727 → macro array (data_pool replay buffer) */
/* The 10-byte "pending-action" flag cluster jt918 manages.
 *
 * Each byte tracks whether the corresponding per-action arm is queued:
 *   c799 (-14439) — "resume" / saved-game path
 *   c79a (-14438) — go-away (close window)
 *   c79b (-14437) — quit-without-save confirm
 *   c79c (-14436) — quit confirmed
 *   c79d (-14435) — start new game
 *   c79e (-14434) — choose-design (>5 designs available)
 *   c79f (-14433) — delete-design
 *   c7a0 (-14432) — rename / edit-name
 *   c7a1 (-14431) — copy-design
 *   c7a2 (-14430) — about / info
 *
 * The saved-game arm (L0df6) and the fresh arm (L0e98) both set up this
 * cluster, then the input loop reads which flag the user's selection
 * tripped (via the JT[3] case body for that local) and calls into the
 * matching CODE 17 / CODE 18 routine. */
/* The 12-byte c79x cluster lives in the below-A5 buffer now. The
 * BSS-zero data_pool_replay does at startup matches the Mac's runtime
 * state (none of these offsets are touched by DATA replay), and the
 * macros below give the same l-value / address semantics as the
 * previous file-static globals. */
#define g_a5_14440 g_a5_byte(-14440)    /* index 0 — first menu slot   */
#define g_a5_14439 g_a5_byte(-14439)
#define g_a5_14438 g_a5_byte(-14438)
#define g_a5_14437 g_a5_byte(-14437)
#define g_a5_14436 g_a5_byte(-14436)
#define g_a5_14435 g_a5_byte(-14435)
#define g_a5_14434 g_a5_byte(-14434)
#define g_a5_14433 g_a5_byte(-14433)
#define g_a5_14432 g_a5_byte(-14432)
#define g_a5_14431 g_a5_byte(-14431)
#define g_a5_14430 g_a5_byte(-14430)
#define g_a5_14429 g_a5_byte(-14429)    /* index 11 — last menu slot   */
/* g_a5_19174 → macro (data_pool replay buffer) */
/* g_a5_19172 → macro (data_pool replay buffer) */
/* g_a5_5794 → macro (data_pool replay buffer) */
/* L0aae's per-JT PROBE stubs. Each is a real CODE 3 / CODE 7 helper
 * that we'll lift individually as engine paths demand them. Until then
 * the surface keeps the call shape so the trace shows the menu build
 * pattern, and the user-selection poll (JT[453]) returns 0 — which
 * means "no action" once jt918's iter_guard breaks the loop. */
/* JT[117] (CODE 6 + 0x3994, 56 sites) — alias for L3994 (GrafPort
 * save snapshot). The Mac JT[117] entry shares the body with the
 * local L3994 helper that JT[94] / JT[1089] reach internally. */
static int     jt117(void)
{
	PROBE("jt117");
	l3994();
	return 0;
}
static int     jt146(void)                          { PROBE("jt146"); return 0; }
static void    jt161(short a)                       { PROBE("jt161"); (void)a; }

/* DLItem record layout — kept here so JT[444] (above the JT[452]
 * cluster that defines the pool) can index the pool by record. */
#define DLITEM_BYTES   32
#define DLITEM_MAX     64

/* JT[444] (CODE 3 + 0x3056, 96 callsites) — DLItem method dispatch.
 *
 * The Mac body:
 *   1. Bail if the DLItem manager isn't active (g_a5_9248 == 0).
 *   2. Validate `item` against [0, g_a5_9250); on out-of-range,
 *      pop up the "DLItem index %d/%d invalid" alert via JT[1084]
 *      and bail.
 *   3. Compute slot = g_a5_9254 + item * 32 (each DLItem is 32
 *      bytes — DLITEM_BYTES). The first long in the record is a
 *      method pointer; invoke it with (slot_addr, a, b, c).
 *
 * The slot's method pointer is NULL until a real handler gets
 * registered (the engine's JT[452] zero-fills the slot, parking
 * the type code in later bytes). In the current port the per-arm
 * shape-code dispatch isn't lifted yet, so the method always
 * reads NULL — the lift guards against that and effectively
 * no-ops, matching the prior PROBE-stub behaviour but tracing the
 * index validation correctly. */
static void    jt444(short item, short a, short b, short c)
{
	unsigned char *slot;
	void         (*method)(unsigned char *, short, short, short);

	PROBE("jt444");
	if (g_a5_9248 == 0)
		return;
	if (item < 0 || item >= g_a5_9250) {
		PROBE("jt444: index out of range");
		return;
	}
	if (g_a5_9254 == 0)
		return;
	slot = (unsigned char *)(uintptr_t)g_a5_9254
	     + (long)item * DLITEM_BYTES;
	method = *(void (**)(unsigned char *, short, short, short))slot;
	if (method == NULL)
		return;
	method(slot, a, b, c);
}
static void    jt447(void)                          { PROBE("jt447"); }
static void    jt449(short a)                       { PROBE("jt449"); (void)a; }
static void    jt451(void)                          { PROBE("jt451"); }
/* JT[452] (CODE 3 + 0x29a0) — DLItem stream installer.
 *
 * The Mac entry takes a variadic stream of (shape-code, args...) tuples
 * and walks it shape-by-shape; each shape-code in [0..42] dispatches
 * through JT[3] to a per-shape arm that reads some shorts / longs from
 * the stream and stores them at specific offsets inside a freshly
 * allocated 32-byte DLItem record at g_a5_9254 + (count++) * 32.
 *
 * Capturing the full 43-arm shape dispatch is a follow-on; the v1
 * skeleton models just the slot-allocation contract that JT[158] /
 * JT[444] / JT[453] rely on:
 *
 *   - Each call installs ONE DLItem (the Mac packs several per call
 *     via the stream; the L0aae caller has been adapted to one-per-
 *     call already so the count winds up identical).
 *   - g_a5_9250 (count) is bumped, clamped to g_a5_9288 (capacity).
 *   - The new record at g_dlitem_pool[(count - 1) * 32] is zero-filled
 *     so the downstream PROBE stubs see well-defined record state.
 *
 * The pool is allocated statically with DLITEM_MAX = 64 records; the
 * engine probe shows L0aae installing 13 items per design-menu open,
 * so the cap is comfortable. Variadic args are consumed via va_arg
 * for ABI parity but their values are ignored until the shape-code
 * dispatch lift lands.
 */
/* DLITEM_BYTES / DLITEM_MAX moved above the JT[444] lift. */

/* g_dlitem_pool — DLItem record table. The Mac equivalent is a
 * NewPtr'd heap block whose address is stored at A5 -9254; we mirror
 * that shape by keeping the records in a separate file-static and
 * loading their base pointer into g_a5_9254 from jt452_init. The
 * pool itself is heap-equivalent storage, NOT part of the A5 world,
 * so it stays out of g_a5_below[]. */
static unsigned char g_dlitem_pool[DLITEM_MAX * DLITEM_BYTES];
/* g_a5_9254 / g_a5_9250 / g_a5_9288 / g_a5_9248 — all macros over
 * the data_pool replay buffer; capacity (=64) and active-flag (=1)
 * are seeded by boot_a5_seed_defaults() in main.c's startup path. */

static void jt452_init(void) __attribute__((unused));
static void jt452_init(void)
{
	g_a5_9254 = (long)(unsigned long)g_dlitem_pool;
	g_a5_9250 = 0;
}

void boot_a5_seed_defaults(void)
{
	/* DATA pool replay zero-fills g_a5_below[]; restore the two
	 * non-zero BSS scalars the engine expects on first use. When
	 * real DATA is loaded these slots will already hold the same
	 * values, so the writes are idempotent either way. */
	g_a5_9288 = (short)DLITEM_MAX;
	g_a5_9248 = 1;
}

/* shape0 declared `long` (not `short`) to avoid the default-argument-
 * promotion va_start UB; calls in `(short)5` etc. promote cleanly. */
static void jt452(long shape0, ...)
{
	va_list       ap;
	unsigned char *rec;

	PROBE("jt452");
	if (g_a5_9254 == 0)
		jt452_init();
	if (g_a5_9250 >= g_a5_9288) {
		PROBE("jt452: DLItem table full");
		return;
	}
	rec = g_dlitem_pool + (long)g_a5_9250 * DLITEM_BYTES;
	memset(rec, 0, DLITEM_BYTES);
	g_a5_9250++;

	/* Consume the rest of the stream for ABI parity. Shape-code 0
	 * terminates the Mac stream; until the dispatch is lifted we walk
	 * (long)args until 0. */
	(void)shape0;
	va_start(ap, shape0);
	for (;;) {
		long v = va_arg(ap, long);

		if (v == 0)
			break;
	}
	va_end(ap);
}
/* L30ba (CODE 3 + 0x30ba) — DLItem focus / select helper. Body lives
 * inside CODE 3's dialog runtime; stays a PROBE stub for now. */
static void   l30ba(short a, short b, short c)
                                                  { PROBE("L30ba"); (void)a;
                                                    (void)b; (void)c; }

/* CODE 4 helpers L2d3e + family forward into. PROBE stubs until the
 * actual event-pump + DLItem dispatch land. */
static void   jt1118(void)                        { PROBE("jt1118"); }
static short  jt1125(short kind, long p1, long p2){ PROBE("jt1125");
                                                    (void)kind; (void)p1;
                                                    (void)p2; return 0; }
static void   jt1134(void)                        { PROBE("jt1134"); }
static void   jt1153(short arg)                   { PROBE("jt1153");
                                                    (void)arg; }

/* L3198 (CODE 3 + 0x3198) — wraps JT[1125] for the event-read prelude. */
static short l3198(short kind, long p1, long p2)
{
	PROBE("L3198");
	return jt1125(kind, p1, p2);
}

/* L31ea (CODE 3 + 0x31ea) = JT[441] — JT[1118] passthrough returning
 * "should continue?" boolean.  */
static int l31ea(void) __attribute__((unused));
static int l31ea(void)
{
	PROBE("L31ea");
	jt1118();
	return 0;
}

/* L31f0 (CODE 3 + 0x31f0) = JT[439] — JT[1133] passthrough returning
 * the post-event key code. */
static short l31f0(void) __attribute__((unused));
static short l31f0(void)
{
	PROBE("L31f0");
	/* Real JT[1133] returns a short; PROBE returns 0. */
	return 0;
}

/* DLItem method pointer signature — each record at offset 0 holds a
 * function pointer the L2d3e dispatcher invokes with (rec, cmd, ...). */
typedef short (*dlitem_method_t)(void *rec, short cmd, ...);

/* L2d3e (CODE 3 + 0x2d3e) = JT[456] — DLItem event poll.
 *
 * The Mac body:
 *
 *   1. Phase 1 — pump OS event:
 *        jt1153(1); l2c60(0); key = l3198(7, &mouse_y, &mouse_x)
 *   2. Phase 2 — walk every DLItem, invoking method(rec, 2, mouse_y,
 *      mouse_x). The first item whose method returns non-zero is the
 *      mouse-hit item.
 *   3. Phase 3 — if a key was pressed and an item caught the mouse,
 *      invoke method(rec, 3, ...) then method(rec, 4, ...); on bit 4
 *      of rec[28] return the item index.
 *   4. Phase 4 — scroll-bar handling via JT[1007] / JT[1123] / L31ea /
 *      L31f0 / further DLItem method dispatch.
 *   5. Return -1 when no item was selected.
 *
 * The v1 lift runs phase 1 + phase 2 with PROBE-stubbed event-read
 * helpers and guards the DLItem method calls against the NULL method
 * pointers JT[452] currently leaves behind (the shape-code dispatch
 * isn't lifted yet). Phases 3+ are documented in the comment but the
 * function always returns -1 — combined with JT[453]'s 30-iteration
 * cap the engine stays responsive without spinning.
 */
static short l2d3e(void)
{
	short  key, mouse_x = 0, mouse_y = 0;
	short  i;
	unsigned char *rec;

	PROBE("L2d3e");

	/* Phase 1 — event read. */
	jt1153(1);
	jt1134();                       /* L2c60 walks items with cmd=1 */
	key = l3198(7, (long)&mouse_y, (long)&mouse_x);
	(void)key;

	/* Phase 2 — find the first item whose method consumes cmd=2 at
	 * the current mouse coords. method pointers are NULL until the
	 * JT[452] shape dispatch is lifted, so we just guard the loop. */
	rec = (unsigned char *)g_a5_9254;
	for (i = 0; i < g_a5_9250; i++) {
		dlitem_method_t method;

		method = *(dlitem_method_t *)rec;
		if (method != NULL) {
			short hit = method(rec, (short)2, mouse_y, mouse_x);

			if (hit != 0)
				break;
		}
		rec += DLITEM_BYTES;
	}

	/* Phase 3+ — modifier-key dispatch, scrollbar handling, the
	 * exact-hit return path. Documented above; not yet lifted. */
	return (short)-1;
}

/* JT[453] (CODE 3 + 0x2cd4) — design-menu modal event loop.
 *
 *   linkw fp, #-2
 *   tstb a5@(-9248); bnes L2cec
 *     JT[1084]("not in dialog in DLDialog"); bras L2d38
 *   L2cec: tstb a5@(-9247); bnes L2d38
 *     clrw -(sp); pushw (count - 1); clrw -(sp); jsr L30ba; addql #6, sp
 *     moveq #1, d0; moveb d0, a5@(-9247)         ; mark "active"
 *   L2d38: bras L2d0c
 *   L2d0c: jsr L2d3e
 *          fp@(-2) = d0
 *          if d0 >= 0 → return d0
 *          if fp@(8) != 0:
 *             d0 = (*filterProc)()
 *             fp@(-2) = d0
 *             if d0 >= 0 → return d0
 *          goto L2d38
 *
 * The first-time arm focuses the last installed DLItem (count - 1)
 * and stamps `g_a5_9247 = 1` so re-entries skip the focus. The main
 * loop polls `L2d3e` for an item hit; on cancel (-1) it invokes the
 * caller's optional filter proc. Real selection returns a non-
 * negative item index.
 *
 * The skeleton bounds the loop with an iteration counter so the
 * engine doesn't spin while L2d3e is PROBE-only — once the DLItem
 * event poll lifts, the bound goes away.
 */
typedef short (*jt453_filter_t)(void);

/* The "currently in dialog event loop" sentinel JT[453] toggles. */
/* g_a5_9247 → macro (data_pool replay buffer) */
static short jt453(jt453_filter_t filterProc)
{
	short       hit;
	short       guard;

	PROBE("jt453");
	if (g_a5_9248 == 0) {
		PROBE("jt453: not in dialog");
		return (short)-1;
	}
	if (g_a5_9247 == 0) {
		l30ba(0, (short)(g_a5_9250 - 1), 0);
		g_a5_9247 = 1;
	}

	for (guard = 0; guard < 30; guard++) {
		hit = l2d3e();
		if (hit >= 0)
			return hit;
		if (filterProc != NULL) {
			hit = filterProc();
			if (hit >= 0)
				return hit;
		}
	}
	return (short)-1;
}

/* JT[140] / JT[156] (CODE 7 + 0x1e58 / 0x1d5c) — item-callback PROCs
 * that JT[158] passes as the "draw" function pointer in its JT[452]
 * push. Their addresses end up in the menu records JT[452] builds; the
 * Menu Manager calls them per item. The proper Mac path is to expose
 * them as ProcPtr; until the menu rendering reaches them, they stay
 * PROBE stubs. */
static void    jt140(void)                          { PROBE("jt140"); }
static void    jt156(void)                          { PROBE("jt156"); }

/* JT[158] globals.
 *  g_a5_13016 — the source / dialog id JT[158] passes through to JT[444]
 *               when it disables a slot.
 *  g_a5_13018 — the current menu mode (set by JT[166]); 7/9/12 branch.
 *  g_a5_19176 — the live design count (computed by the linked-list walk).
 *  g_a5_12910 — last-seen count (so JT[158] knows when the list changed).
 *  g_a5_24126 — 40-byte slot-index table JT[179] initialises. */
/* g_a5_13016 → macro (data_pool replay buffer) */
/* g_a5_13018 → macro (data_pool replay buffer) */
/* g_a5_19176 → macro (data_pool replay buffer) */
/* g_a5_12910 → macro (data_pool replay buffer) */
/* g_a5_24126 → macro array (data_pool replay buffer) */
static void jt166(short mode)
{
	PROBE("jt166");
	g_a5_13018 = mode;
}

/* JT[179] — initialise the 40-byte slot-index table at a5@(-24126).
 * CODE 7 + 0x11ee. First fills the whole table with 0xFF, then writes
 * slot index i at byte offset 2*i for i in [0..arg]. The "every other
 * byte" pattern leaves room for a per-slot status flag the Menu
 * Manager pokes into the odd-numbered cells. */
static void jt179(short count) __attribute__((unused));
static void jt179(short count)
{
	short i, n;

	PROBE("jt179");
	jt399(g_a5_24126, (short)G_A5_24126_LEN, 0xFF);
	n = (short)(count & 0xFF);
	for (i = 0; i <= n; i++) {
		if ((i * 2) < (short)G_A5_24126_LEN)
			g_a5_24126[i * 2] = (unsigned char)i;
	}
}

/* JT[155] (CODE 7 + 0x11a8, 99 callsites) — append one byte to the
 * g_a5_24126 index buffer.
 *
 * First call (counter byte == 0) initialises the 40-byte buffer to
 * 0xff via jt399; subsequent calls overwrite slot [counter * 2]
 * with the low byte of `value` and bump counter. The buffer is the
 * same one jt179 fills with a contiguous index sequence; jt155 is
 * the per-element append variant the engine uses when building the
 * sequence one item at a time. */
static void jt155(short value, void *counter) __attribute__((unused));
static void jt155(short value, void *counter)
{
	unsigned char *cnt = (unsigned char *)counter;

	PROBE("jt155");
	if (cnt == NULL)
		return;
	if (cnt[0] == 0)
		jt399(g_a5_24126, (short)G_A5_24126_LEN, 0xFF);
	if ((short)(cnt[0] * 2) < (short)G_A5_24126_LEN)
		g_a5_24126[cnt[0] * 2] = (unsigned char)(value & 0xff);
	cnt[0]++;
}

/* JT[4] (CODE 1 + 0x0174, 87 callsites) — 32-bit signed multiply.
 *
 * THINK C's runtime entry for `long * long` since the 68000 only
 * provides 16x16 → 32 muls.w. The asm shuffles d0/d1 through swap
 * + muluw to assemble a 32-bit product from two 16x16 multiplies;
 * the portable equivalent is plain `a * b`. */
static long jt4(long a, long b) __attribute__((unused));
static long jt4(long a, long b)
{
	PROBE("jt4");
	return a * b;
}

/* JT[5] / JT[6] / JT[7] / JT[8] (CODE 1 + 0x1aa..0x20c) — 32-bit
 * div runtime helpers. THINK C emits these for `/` and `%` on
 * longs since 68000 has no 32-bit divide.
 *
 *   JT[5]: (unsigned long) a / b
 *   JT[6]: (unsigned long) a % b
 *   JT[7]: (signed long)   a / b
 *   JT[8]: (signed long)   a % b
 *
 * The portable equivalents are plain C operators; m68k-atari-mint-gcc
 * emits the same hardware-divs.l shape (or the soft-div helper on
 * 68000) so the call lattice stays Mac-identical. Guard against
 * b == 0 so the lift never raises a divide trap on stale inputs. */
static unsigned long jt5(unsigned long a, unsigned long b) __attribute__((unused));
static unsigned long jt5(unsigned long a, unsigned long b)
{
	PROBE("jt5");
	if (b == 0) return 0;
	return a / b;
}
static unsigned long jt6(unsigned long a, unsigned long b) __attribute__((unused));
static unsigned long jt6(unsigned long a, unsigned long b)
{
	PROBE("jt6");
	if (b == 0) return 0;
	return a % b;
}
static long jt7(long a, long b) __attribute__((unused));
static long jt7(long a, long b)
{
	PROBE("jt7");
	if (b == 0) return 0;
	return a / b;
}
static long jt8(long a, long b) __attribute__((unused));
static long jt8(long a, long b)
{
	PROBE("jt8");
	if (b == 0) return 0;
	return a % b;
}

/* JT[1180] (CODE 4 + 0x228c, 67 sites) — 16-bit byte swap.
 * THINK C runtime helper for endianness conversion when the
 * engine reads big-endian shorts from a buffer that the lift
 * still has in host order. Mac's asm assembles the result from
 * an lsl + asr + or; the portable equivalent is the macro form. */
static short jt1180(short v) __attribute__((unused));
static short jt1180(short v)
{
	unsigned short u = (unsigned short)v;
	PROBE("jt1180");
	return (short)(((u & 0x00ff) << 8) | ((u & 0xff00) >> 8));
}

/* JT[158] — walk the design list, then either add a menu item per
 * design (modes 7 / 9 / 12 / other) or disable a stale slot (when
 * the count shrank). CODE 7 + 0x1f3e.
 *
 *   ptr = g_a5_27928
 *   g_a5_19176 = 0
 *   while (ptr && g_a5_19176 < 9) { ptr = *ptr; g_a5_19176++ }
 *   if (g_a5_12911 == 0) {
 *       if (g_a5_12910 == g_a5_19176) goto L2058   // no change
 *       jt444(g_a5_13016, 42, g_a5_19176 * 4)       // disable slot
 *       goto L2058
 *   }
 *   d0 = g_a5_19176 * 4
 *   d2 = 8160 - (160 - arg2)                         // arg2 = fp@(10)
 *   if (g_a5_13018 == 7)
 *       jt452(5, arg1, arg2, d0, d2, 34, &jt140, 0)
 *   else if (g_a5_13018 == 9 || g_a5_13018 == 12)
 *       jt452(5, arg1, arg2, d0, d2, 34, &jt156, 20, 0)
 *   else
 *       jt452(5, arg1, arg2, d0, d2, 34, &jt156, 0)
 * L2058:
 *   g_a5_12910 = g_a5_19176
 *
 * The menu-build calls (JT[452] / JT[444]) themselves are still PROBE
 * stubs — what the lift captures is the count walk + mode dispatch
 * that drives them.
 */
static void jt158(short arg1, short arg2)
{
	void *ptr;
	long  d0, d2;

	PROBE("jt158");
	ptr = (void *)g_a5_27928;
	g_a5_19176 = 0;
	while (ptr != NULL && g_a5_19176 < 9) {
		ptr = *(void **)ptr;            /* next-pointer at *(ptr) */
		g_a5_19176++;
	}

	if (g_a5_12911 == 0) {
		if (g_a5_12910 == g_a5_19176)
			goto store;
		jt444(g_a5_13016, (short)42, (short)(g_a5_19176 * 4), 0);
		goto store;
	}

	d0 = (long)g_a5_19176 * 4;
	d2 = 8160L - (160L - arg2);

	switch (g_a5_13018) {
	case 7:
		jt452((short)5, (long)arg1, (long)arg2, d0, d2,
		      34L, (long)(void *)jt140, 0L);
		break;
	case 9:
	case 12:
		jt452((short)5, (long)arg1, (long)arg2, d0, d2,
		      34L, (long)(void *)jt156, 20L, 0L);
		break;
	default:
		jt452((short)5, (long)arg1, (long)arg2, d0, d2,
		      34L, (long)(void *)jt156, 0L);
		break;
	}

store:
	g_a5_12910 = g_a5_19176;
}

/* The 12 menu items L0aae builds via JT[452] — `Train Character` etc.,
 * each with a single-letter accelerator (the `selector` field is the
 * ASCII code of the highlighted key). The full Mac body splits the
 * install into three JT[452] calls of five items each; the skeleton
 * just iterates the table. */
static const struct {
	long          label_strs_off;       /* STRS resource byte offset    */
	short         selector;             /* accelerator key (ASCII)      */
	short         page;                 /* 8000-channel page id         */
	short         phrase;               /* phrase id                    */
} k_jt918_menu_items[] = {
	{ 0x5f1c, 'T', 8004, 8080 },   /* 0  "Train Character"    */
	{ 0x5f0a, 'M', 8004, 8073 },   /* 1  "Modify Character"   */
	{ 0x5ef8, 'D', 8084, 8066 },   /* 2  "Delete Character"   */
	{ 0x5ee6, 'C', 8084, 8059 },   /* 3  "Create Character"   */
	{ 0x5f5e, 'R', 8004, 8066 },   /* 4  "Remove Character"   */
	{ 0x5f50, 'A', 8004, 8059 },   /* 5  "Add Character"      */
	{ 0x5f40, 'V', 8004, 8094 },   /* 6  "View Character"     */
	{ 0x5f2c, 'H', 8004, 8087 },   /* 7  "Human Change Class" */
	{ 0x5fa4, 'E', 8084, 8094 },   /* 8  "Exit From Play"     */
	{ 0x5f92, 'B', 8084, 8087 },   /* 9  "Begin Adventuring"  */
	{ 0x5f80, 'S', 8084, 8080 },   /* 10 "Save Current Game"  */
	{ 0x5f70, 'L', 8084, 8073 },   /* 11 "Load Saved Game"    */
};

/* L0aae — build the design-menu, walk the c79x flag cluster to
 * enable / disable each item, display it, return the user's
 * selection 0..11. CODE 12 + 0x0aae.
 *
 * Structure (from the original asm):
 *   1. jt174  + jt447         — graphics / dialog init
 *   2. Three JT[452] calls of five menu items each, plus one final
 *      JT[452] for an extra "page" item
 *   3. Set g_a5_19174 = 8004, g_a5_19172 = 8016 (menu coords)
 *   4. jt166(9); jt158(g_a5_19174, g_a5_19172) — display the menu
 *   5. Loop i = 0..11: jt444(i, flags[i] ? 24 : 16) — enable / disable
 *   6. jt449(1); jt112(0); jt117()              — finalize
 *   7. d0 = jt453(0)                              — poll for selection
 *   8. if jt146() != 0: d0 = 5; jt161(0)         — shortcut override
 *   9. g_a5_5794 = d0; jt451()
 *  10. return d0
 *
 * The menu-build calls themselves stay PROBE-stubbed; the c79x walk
 * (the meaningful runtime bit) is lifted faithfully. JT[453] returns
 * 0 in the stub — combined with jt918's iter_guard the engine loop
 * still terminates cleanly.
 */
static int l0aae(void)
{
	short i;
	int   selection;

	PROBE("L0aae");

	jt174();
	jt447();

	for (i = 0; i < (short)(sizeof k_jt918_menu_items
	                        / sizeof k_jt918_menu_items[0]); i++) {
		jt452(i,
		      ua_strs_at(k_jt918_menu_items[i].label_strs_off),
		      (long)k_jt918_menu_items[i].selector,
		      (long)k_jt918_menu_items[i].page,
		      (long)k_jt918_menu_items[i].phrase);
	}
	/* The extra "page switch" item the Mac appends past the 12. */
	jt452(7, 20L, (long)0);

	g_a5_19174 = 8004;
	g_a5_19172 = 8016;
	jt166(9);
	jt158(g_a5_19174, g_a5_19172);

	{
		unsigned char *flags[12] = {
			&g_a5_14440, &g_a5_14439, &g_a5_14438, &g_a5_14437,
			&g_a5_14436, &g_a5_14435, &g_a5_14434, &g_a5_14433,
			&g_a5_14432, &g_a5_14431, &g_a5_14430, &g_a5_14429,
		};
		for (i = 0; i < 12; i++)
			jt444(i, (short)(*flags[i] != 0 ? 24 : 16), 0, 0);
	}

	jt449(1);
	(void)jt112(0);
	(void)jt117();

	selection = jt453(NULL);
	if (jt146() != 0) {
		selection = 5;
		jt161(0);
	}
	g_a5_5794 = (short)selection;
	jt451();
	return selection;
}

/* The "no save-game pointer present, but cached state is dirty" flag —
 * read by the saved-game arm at L0e22 to decide whether the design
 * list deserves the "fresh" treatment. */
/* g_a5_22730 → macro (data_pool replay buffer) */
/* JT[3] dispatch case bodies — one per local value 0..11. Each one is
 * a sizeable chunk of CODE 12 that reads a c79x flag, calls into CODE
 * 17 / CODE 18, etc. They stay PROBE-only until the cases below are
 * lifted individually; for now the dispatcher routes correctly and the
 * trace reports which case the loop hits. */
/* JT[574] (CODE 17 + 0x3b5e) — Train Character action entry. Body
 * lives in CODE 17 which we haven't touched yet; stays a PROBE stub
 * until the CODE 17 lifts begin. The single long arg is consistently
 * 0 at the L0f1a call site. */
static int  jt574(long ctx)        { PROBE("jt574"); (void)ctx; return 0; }

/* L0f1a — case 0 of jt918's JT[3] switch. CODE 12 + 0x0f1a.
 *
 *   tstb a5@(-14440)
 *   beqw L1242                       // if flag clear, no action
 *   clrl -(sp)
 *   jsr  JT[574]                     // jt574(0) — Train Character
 *   addql #4, sp
 *   braw L1242
 *
 * The matching menu item is "Train Character" (selector 'T'); the
 * c79x flag g_a5_14440 is set by the L0df6 / L0e98 cluster init when
 * the action is available. */
static int l0f1a(short a)
{
	(void)a;
	PROBE("jt918/case0 L0f1a");
	if (g_a5_14440 != 0)
		(void)jt574(0);
	return 0;
}
/* Forward — jt488 and jt159 are defined further down (sprintf into
 * g_a5_10362 and the modal-confirm wrapper). The L15e2 / L12a0 lifts
 * call them above the original forward declarations. */
static const char *jt488(const char *fmt, ...);
static int  jt159(const char *prompt, short b);

/* JT entries L12a0 / L15e2 reach the design-edit dialog runtime through.
 * Everything past the window-open JT[103] stays PROBE-only; what the
 * lifts below buy is the call ordering, not the painted result. */
static int  jt165(short id, long ctx)
                                            { PROBE("jt165"); (void)id;
                                              (void)ctx; return 0; }
static int  jt169(long h1, long h2, short top, short left,
                  short right, short bottom, long entry,
                  short a, short b,
                  unsigned char *flag, short *idx, long *next)
                                            { PROBE("jt169"); (void)h1;
                                              (void)h2; (void)top; (void)left;
                                              (void)right; (void)bottom;
                                              (void)entry; (void)a; (void)b;
                                              (void)flag; (void)idx;
                                              (void)next; return 0; }
/* JT[396] (CODE 3 + 0x3bda) — public entry for the case-insensitive
 * string-equal lifted as l3bda earlier in this file. The jump-table
 * entry was a stub returning 0; route it to the real body so callers
 * (L1266's design-list filter, the L12a0 / L15e2 dialog match loops)
 * actually match. */
static int  jt396(const char *a, const char *b)
{
	PROBE("jt396");
	return l3bda(a, b);
}
/* JT[431] (CODE 3 + 0x4b8e, 42 sites) — HFS path-concat with ':'.
 *
 * Appends `src` to `dst`, inserting a ':' separator first unless
 * `dst` is empty or already ends with ':'. The Mac uses ':' as
 * the HFS path separator; the engine keeps building strings in
 * that form and lets the FSOpen shim translate to GEMDOS at the
 * boundary. */
static void jt431(void *dst, const void *src)
{
	char       *d = (char *)dst;
	const char *s = (const char *)src;
	short       len;

	PROBE("jt431");
	if (d == NULL || s == NULL)
		return;
	len = l39ae(d);
	if (len > 0 && d[len - 1] != ':')
		d[len++] = ':';
	while ((d[len++] = *s++) != 0)
		;
}
static void jt471(long entry, short tag, void *bucket)
                                            { PROBE("jt471"); (void)entry;
                                              (void)tag; (void)bucket; }
static void jt477(void *bucket, short tag, void *out)
                                            { PROBE("jt477"); (void)bucket;
                                              (void)tag; (void)out; }
static void jt581(long head, long tail)     { PROBE("jt581"); (void)head;
                                              (void)tail; }
static void jt587(void *dst, void *bucket, short a, short b)
                                            { PROBE("jt587"); (void)dst;
                                              (void)bucket; (void)a;
                                              (void)b; }
static void jt589(short flag, long *tail, long *head)
                                            { PROBE("jt589"); (void)flag;
                                              if (tail != NULL) *tail = 0;
                                              if (head != NULL) *head = 0; }
static void jt590(void *entry)              { PROBE("jt590"); (void)entry; }
static void jt593(short a)                  { PROBE("jt593"); (void)a; }
static int  jt988(void *path, short mode, void *name, long zero)
                                            { PROBE("jt988"); (void)path;
                                              (void)mode; (void)name;
                                              (void)zero; return 0; }

/* L1266 (CODE 12 + 0x1266) — list-filter helper. Walks g_a5_27928
 * (head, .next at offset 0) and returns 1 if any entry's name slot
 * (entry + 96) matches the caller-supplied name byte-string via
 * JT[396]; 0 otherwise. */
static int l1266(const char *name)
{
	const unsigned char *entry;

	PROBE("L1266");
	entry = (const unsigned char *)(uintptr_t)g_a5_27928;
	while (entry != NULL) {
		if (jt396((const char *)&entry[96], name) != 0)
			return 1;
		entry = *(const unsigned char * const *)entry;
	}
	return 0;
}

/* L15e2 (CODE 12 + 0x15e2) — design-delete confirmation dialog.
 *
 *   1. Open the 38x22 design window via JT[103]; set the dialog mode
 *      g_a5_22733 = 1.
 *   2. JT[589] inits a list-iter pair (head fp[-4], tail fp[-8]).
 *      Walks the iter calling L1266 on each entry's display-name
 *      slot (+5); on match, prefixes "* " via JT[488]("* %s") +
 *      JT[384] so the entry is rendered with the marker.
 *   3. Wait for input: JT[179](1) then JT[169] returns the key in
 *      fp[-106]. Escape (27) maps to 1 (cancel).
 *   4. On select (key == 0) for a non-marked entry:
 *        - Format "Delete %s forever?" via JT[488] + JT[384], confirm
 *          via JT[159]; bail on No.
 *        - Confirm "Are you sure?" via JT[159]; bail on No.
 *        - JT[165] looks the entry up (idx in fp[-104], handle in
 *          fp[-8]); JT[431] concatenates the path under g_a5_31336
 *          and the "SAVE" leaf; JT[988] deletes the file.
 *        - Unlink from g_a5_27928 (chained via .next at offset 0)
 *          and from fp[-8]'s peer list (entries linked at offset 0).
 *        - JT[471] tears the entry down twice (once per list).
 *   5. Loop back unless cancelled or the head's empty; JT[581] cleans
 *      up.
 *
 * The inner branches (file delete, list unlink) stay scaffolded — the
 * Toolbox file calls aren't ready and the entry-unlink walk is the
 * tricky part. The lift below captures the call sequence faithfully
 * enough that the engine probe reports the dialog flow. */
static void l15e2(void)
{
	long  head = 0;
	long  tail = 0;
	long  entry;
	long  matched;
	short input;
	short idx = 0;
	unsigned char loop_flag = 0;
	unsigned char path_buf[64];

	PROBE("L15e2");

	jt103(1, 1, 38, 22);
	g_a5_22733 = 1;

	jt589(1, &tail, &head);
	if (head == 0)
		return;

	for (;;) {
		entry = head;
		while (entry != 0) {
			const unsigned char *e =
				(const unsigned char *)(uintptr_t)entry;
			if (l1266((const char *)&e[5])) {
				const char *prefixed =
					jt488(ua_strs_at(0x5ffa),  /* "* %s" */
					      &e[5]);
				jt384((char *)(uintptr_t)&e[5], prefixed);
			}
			entry = *(const long *)e;
		}

		loop_flag = 1;
		jt179(1);
		input = (short)jt169(g_a5_14216, g_a5_13792, 1, 2, 38, 22,
		                     head, 1, 0,
		                     &loop_flag, &idx, &entry);
		if (g_a5_24139 != 0 && input == 27)
			input = 1;

		if (input == 0) {
			const unsigned char *e =
				(const unsigned char *)(uintptr_t)entry;
			short marker = (e[5] == '*') ? 2 : 0;
			const char *prompt;

			/* "Delete %s forever? " */
			prompt = jt488(ua_strs_at(0x6000), &e[5 + marker]);
			jt384((char *)path_buf, prompt);
			if (jt159((const char *)path_buf, 0) == 0)
				goto skip_delete;
			if (jt159(ua_strs_at(0x6014), 0) == 0)
				goto skip_delete;
				/* "Are you sure? " */

			matched = jt165(idx, tail);
			path_buf[0] = 0;
			jt431(path_buf, &g_a5_31336);
			jt431(path_buf, ua_strs_at(0x6024));   /* "SAVE" */
			jt431(path_buf,
			      &((const unsigned char *)
			        (uintptr_t)matched)[5]);
			(void)jt988(path_buf, 3, path_buf, 0);

			/* Unlink `entry` from g_a5_27928 chain, then from
			 * the peer chain rooted at head. JT[471] reclaims
			 * the slot on both lists. */
			jt471(entry, 40, (void *)(uintptr_t)g_a5_21156);
			jt471(matched, 40, (void *)(uintptr_t)g_a5_21156);
		}
skip_delete:
		if (input == 1 || head == 0)
			break;
	}

	jt581(head, tail);
}

/* JT[556] / JT[557] / JT[560] / JT[876] / JT[878] / JT[1199] —
 * case 2..4 dispatch targets. All live in CODE 4 / CODE 17 / CODE 18,
 * which we haven't started lifting yet. */

/* JT[41] (CODE 6 + 0x2526) — linked-list search by first-byte key.
 *
 * Walks the list rooted at `handle->[4]`, comparing every node's
 * byte-0 against `find_byte`. On match: stops with the matching
 * node in `descriptor[0]` and returns 1. On exhaustion: leaves
 * `descriptor[0] = NULL` and returns 0. Nodes thread via a 4-byte
 * "next" pointer at offset 6 (the engine's "design list node"
 * shape).
 *
 * Called 116 times across CODE 1..23 — the design-edit / item /
 * spell loops all dispatch through this. With it lifted, every
 * caller's `if (jt41(...))` arm now actually fires when a match
 * exists in the live list. */
static int    jt41(long handle_long, short find_byte, void *descriptor)
{
	const unsigned char *handle =
		(const unsigned char *)(uintptr_t)handle_long;
	void               **iter_slot = (void **)descriptor;
	const unsigned char *node;
	int                  found = 0;

	PROBE("jt41");
	if (handle == NULL || iter_slot == NULL)
		return 0;

	/* descriptor[0] = handle->[4]  — the list head */
	*iter_slot = *(void *const *)(handle + 4);

	while (*iter_slot != NULL && !found) {
		node = (const unsigned char *)*iter_slot;
		if (node[0] == (unsigned char)find_byte) {
			found = 1;
		} else {
			/* descriptor[0] = node->[6]  (node->next) */
			*iter_slot = *(void *const *)(node + 6);
		}
	}
	return found;
}
static int    jt556(long a)                    { PROBE("jt556"); (void)a;
                                                  return 0; }
static void   jt557(void)                       { PROBE("jt557"); }
static void   jt560(void)                       { PROBE("jt560"); }
static void   jt876(long a, short b, short c, short d, short e)
                                                { PROBE("jt876"); (void)a;
                                                  (void)b; (void)c;
                                                  (void)d; (void)e; }
static void   jt878(long a, short b, long c)   { PROBE("jt878"); (void)a;
                                                  (void)b; (void)c; }
static long   jt1199(long a)                   { PROBE("jt1199"); (void)a;
                                                  return 0; }

/* Globals case 2 manages. */
/* g_a5_18844 → macro (data_pool replay buffer) */
/* g_a5_18882 → macro (data_pool replay buffer) */
/* g_a5_27946 / g_a5_27982 are now macros in the L5124 cluster at the
 * top of the file. */

/* Globals cases 7..11 manage. */
/* g_a5_27988 → macro (data_pool replay buffer) */
/* g_a5_14284 → macro (data_pool replay buffer) */
/* g_a5_18472 → macro (data_pool replay buffer) */
/* Forward — jt19 / jt159 land further down with the case 7..11 stubs. */
static void jt19(short a, short b);
static int  jt159(const char *prompt, short b);

/* L12a0 — View Character dispatcher. CODE 12 + 0x12a0. ~800 bytes of
 * asm that opens a 22x38 window via JT[103], pumps the input loop via
 * JT[179], runs a JT[3] sub-dispatch over the user's key (Escape →
 * exit, Enter → confirm, others case-by-case), then walks the design
 * list via JT[589] / JT[488] / JT[384] / JT[169]. Stays a PROBE stub —
 * lifting the full body needs the CODE 7 dialog runtime first. */
/* L12a0 (CODE 12 + 0x12a0) — View Character dispatcher.
 *
 *   1. Open the 38x22 window via JT[103]; pump events JT[179](2).
 *   2. Clear the input byte fp[-30]; map Escape (27) to 2 when
 *      g_a5_24139 != 0 so the dispatcher classifies it consistently.
 *   3. JT[3] dispatches on fp[-30]: case 0 → g_a5_22733 = 1, case 1
 *      → g_a5_22733 = 2, default → g_a5_22733 = 1 (Mac inline post-
 *      call table 0x000c / 0x02ea).
 *   4. Iterate: JT[589](0, &fp[-8], &fp[-4]) primes the list head;
 *      bail to JT[581] when fp[-4] is null. Walk the iter — for
 *      every match against L1266 sprintf "* %s" + JT[384] so the
 *      entry is rendered with the marker.
 *   5. JT[179](1) then JT[169](handles, dims, head, 1, 0, &flag,
 *      &idx, &iter) returns the user's key in fp[-30]. Escape (27)
 *      maps to 1 (exit).
 *   6. If the key is null and the entry isn't marked '*': JT[477]
 *      reserves the design slot (fp[-20]) at tag 398 from
 *      g_a5_22212; JT[165] fills fp[-16]. JT[587] cleans the source
 *      slot, then "* %s" via JT[488] + JT[384] paints the marker.
 *      An inner walk over g_a5_27928 counts characters whose
 *      entry[147] flag is below 0x80 (fp[-28]) and entry[161] is
 *      non-zero (fp[-29]) so the "rangers in party" / "5 max"
 *      checks at L1562 can fire — bail with JT[42](...) on overflow.
 *      JT[590] + JT[593](1) commit; JT[471](fp[-20]) tears down.
 *   7. Loop exit when fp[-30] == 1 OR (fp[-28] < 5 AND the design
 *      record's count at +32 < 7). JT[581] cleans up.
 *
 * Like L15e2, the inner add-character arm leaves the heavy work
 * (entry-count walk, "too many rangers" message) PROBE-deferred —
 * the lift records the outer call sequence and the cap conditions
 * resolve once the right slot fields land. */
static void l12a0(void)
{
	long  head = 0;
	long  tail = 0;
	long  entry;
	long  matched;
	short input = 0;
	short idx   = 0;
	short body_count   = 0;
	unsigned char loop_flag = 0;
	unsigned char fresh_slot[64];

	PROBE("L12a0");

	jt103(1, 1, 38, 22);
	jt179(2);

	if (g_a5_24139 != 0 && input == 27)
		input = 2;

	/* Inline JT[3] table at CODE 12 + 0x12ea: min=0 max=2,
	 * default → g_a5_22733 = 1; arm 0 → g_a5_22733 = 1; arm 1 →
	 * g_a5_22733 = 2; arm 2 jumps past the main loop to L15de
	 * (clean exit). Lifted as a direct C switch — input is always
	 * 0 on the first pass (the Escape-to-2 remap above tests an
	 * already-cleared byte), so practically the dispatcher always
	 * sets g_a5_22733 = 1 and proceeds into the loop. */
	switch (input) {
	case 0:
		g_a5_22733 = 1;
		break;
	case 1:
		g_a5_22733 = 2;
		break;
	case 2:
		return;          /* exits to L15de */
	default:
		g_a5_22733 = 1;
		break;
	}

	for (;;) {
		jt589(0, &tail, &head);
		if (head == 0)
			break;

		entry = head;
		while (entry != 0) {
			const unsigned char *e =
				(const unsigned char *)(uintptr_t)entry;
			if (l1266((const char *)&e[5])) {
				const char *prefixed =
					jt488(ua_strs_at(0x5fd4), &e[5]);
					/* "* %s" */
				jt384((char *)(uintptr_t)&e[5], prefixed);
			}
			entry = *(const long *)e;
		}

		loop_flag = 1;
		jt179(1);
		input = (short)jt169(g_a5_13792, g_a5_14216, 1, 2, 38, 22,
		                     head, 1, 0,
		                     &loop_flag, &idx, &entry);
		if (g_a5_24139 != 0 && input == 27)
			input = 1;

		if (input == 0) {
			const unsigned char *e =
				(const unsigned char *)(uintptr_t)entry;

			if (e[5] == '*')
				goto exit_check;

			jt477(&g_a5_22212, 398, fresh_slot);
			matched = jt165(idx, tail);
			jt587(fresh_slot, (void *)(uintptr_t)matched, 0, 1);

			{
				const char *prefixed =
					jt488(ua_strs_at(0x5fda), &e[5]);
					/* "* %s" */
				jt384((char *)(uintptr_t)&e[5], prefixed);
			}

			if (g_a5_27928 == 0) {
				/* Empty roster — commit and stop. */
				unsigned char *handle =
					(unsigned char *)g_a5_28006;
				if (handle != NULL)
					handle[32] = 0;
				jt590(fresh_slot);
				jt593(1);
			} else {
				/* Walk the roster counting body entries;
				 * the "rangers in party" cap at L1562
				 * stays PROBE-deferred — the dispatcher
				 * exits cleanly via L1562 / L159c. */
				body_count = 0;
				jt471(matched, 398, &g_a5_22212);
			}
		}

exit_check:
		{
			const unsigned char *handle =
				(const unsigned char *)g_a5_28006;
			short occupied =
				(handle != NULL) ? (short)handle[32] : 0;
			short exit_now = (input == 1) ||
			                 (body_count < 5 && occupied < 7);
			if (exit_now) {
				jt581(head, tail);
				return;
			}
		}
	}
}

/* JT[488] (CODE 3 + 0x438) — sprintf into the A5 scratch buffer at
 * -10362, returning the buffer address. All format strings the
 * engine uses are `%s` substitutions over a Pascal-string-style
 * name; the lift uses C vsnprintf which handles them faithfully.
 *
 * The buffer is a single shared 256-byte slot in g_a5_below[] —
 * the caller must consume the returned pointer before the next
 * jt488 call, since the next call clobbers it. */

/* stdarg.h / stdio.h moved to the top so jt452 can use va_list too. */

static const char *jt488(const char *fmt, ...)
{
	va_list ap;

	PROBE("jt488");
	if (fmt == NULL)
		fmt = "";
	va_start(ap, fmt);
	vsnprintf(g_a5_10362, G_A5_10362_LEN, fmt, ap);
	va_end(ap);
	return g_a5_10362;
}

/* JT[394] (CODE 3 + 0x4796) — vsprintf into the caller's buffer.
 *
 * The Mac implementation parks the destination pointer in
 * g_a5_9168 and runs THINK C's `_doprintf` core (L3fb8) with a
 * write-character callback (JT[383]) that advances g_a5_9168 byte
 * by byte. After the format walk completes, the function writes a
 * null terminator at the (now post-last-char) g_a5_9168 position.
 *
 * The portable equivalent is plain vsprintf — same semantics, no
 * caller-visible state. Used 156 times across CODE 1..23 for every
 * "build a string for an alert" / "format a stat into a row" etc.
 *
 * The output buffer's size is the caller's responsibility (matches
 * the Mac); engine format strings are STRS-defined templates that
 * never grow large. */
static int jt394(char *buf, const char *fmt, ...) __attribute__((unused));
static int jt394(char *buf, const char *fmt, ...)
{
	va_list ap;
	int     n;

	PROBE("jt394");
	if (buf == NULL)
		return 0;
	if (fmt == NULL)
		fmt = "";
	va_start(ap, fmt);
	n = vsprintf(buf, fmt, ap);
	va_end(ap);
	return n;
}

/* jt176 lands further down (window-paint init/commit). Forward
 * so jt42's lift can call it. */
static void jt176(void);

/* L4bac (CODE 6 + 0x4bac) — message-area scroll advance. Reads
 * g_a5_28006->byte[18] as an index into a short table at
 * g_a5_17518, passes that short to JT[476] (consume), with a
 * preceding JT[1134] call (advance?). Stays PROBE — the index walk
 * is small but adds two JT stubs and an A5-array macro; lifted in
 * a follow-up. */
static void l4bac(void)        { PROBE("l4bac"); }

/* JT[42] (CODE 6 + 0x22a6) — append a message to the play window's
 * scrolling text area.
 *
 * Body shape (faithful translation):
 *
 *      jt176();                                 // paint init
 *      jt94(0, 24, 15, 8, "%s", msg);           // draw the message
 *      l4bac();                                 // scroll-advance
 *      jt176();                                 // paint commit
 *
 * The outer JT[176] pair brackets the paint with the window-system's
 * deferred-draw machinery; the JT[94] call lands the text at page 0
 * row 24 col 15 with style 8 (the play-window's narrative band).
 * 69 callsites depend on the chain — once JT[176] / JT[94] lift,
 * narrative shows through; for now the probe traces the per-callsite
 * sequence faithfully. */
static void jt42(const char *msg)
{
	PROBE("jt42");
	jt176();
	jt94(0, 24, 15, 8, ua_strs_at(0xd2), msg);   /* "%s" */
	l4bac();
	jt176();
}

/* L185e — Human Change Class "Drop NAME forever?" confirmation arm
 * jt918 case 7 jsrs into. CODE 12 + 0x185e.
 *
 *   if (g_a5_27932 == 0) goto L191c
 *   buf = jt488("Drop %s forever? ", &handle[96])
 *   strcpy(local, buf)        // jt384 into fp@(-42)
 *   if (!jt159(local, 0))     goto L1900
 *   if (!jt159("Are you sure? ", 0)) goto L1900
 *   if (*(char *)(handle + 382) == 0) {
 *       jt42(jt488("You dump %s out back.",  &handle[96]))
 *   } else {
 *       jt42(jt488("%s bids you farewell.", &handle[96]))
 *   }
 *   jt19(0, 1)                // execute the drop
 *   goto L191c
 *  L1900:
 *   jt42(jt488("%s breathes a sigh of relief.", &handle[96]))
 *  L191c:
 *   l02dc(g_a5_27932)
 *   return
 */
static void l185e(void)
{
	unsigned char *handle;
	char           local_prompt[256];

	PROBE("L185e");
	if (g_a5_27932 == 0)
		goto refresh;
	handle = (unsigned char *)g_a5_27932;

	{
		const char *fmt = ua_strs_at(0x602a);  /* "Drop %s forever? " */
		const char *built = jt488(fmt, &handle[96]);

		jt384(local_prompt, built);
	}
	if (jt159(local_prompt, 0) == 0)
		goto sigh_of_relief;
	if (jt159(ua_strs_at(0x603c), 0) == 0)         /* "Are you sure? " */
		goto sigh_of_relief;

	if (handle[382] == 0) {
		const char *fmt = ua_strs_at(0x604c);  /* "You dump %s out back." */
		jt42(jt488(fmt, &handle[96]));
	} else {
		const char *fmt = ua_strs_at(0x6062);  /* "%s bids you farewell." */
		jt42(jt488(fmt, &handle[96]));
	}
	jt19(0, 1);
	goto refresh;

sigh_of_relief:
	{
		const char *fmt = ua_strs_at(0x6078);  /* "%s breathes a sigh of relief." */
		jt42(jt488(fmt, &handle[96]));
	}

refresh:
	l02dc(g_a5_27932);
}

/* Forward — JT[78] = L67ca, JT[84] = L68f8, JT[88] = L5124. Already
 * lifted earlier in the file; cases 7 / 8 / 10 jsr their JT entries. */
static void l68f8(void);
static void l5124(void);

/* Toolbox / CODE-7 / CODE-15 / CODE-19 / CODE-20 helpers the case
 * bodies reach into. All PROBE stubs — the real bodies live in
 * segments we haven't started yet. */
static void   jt19(short a, short b)             { PROBE("jt19"); (void)a; (void)b; }
static int    jt159(const char *prompt, short b) { PROBE("jt159");
                                                   (void)prompt; (void)b;
                                                   return 0; }
/* JT[1173] / JT[1193] / L2062 — paint init/commit leaves JT[176]
 * reaches into. PROBE for now; bodies live further inside the
 * window-paint cluster (CODE 4 + 0x164c, CODE 4 + 0x16e0, CODE 7
 * + 0x2062). */
static void jt1173(short top, short left, short right, short bottom)
                                                  { PROBE("jt1173"); (void)top;
                                                    (void)left; (void)right;
                                                    (void)bottom; }
static void jt1193(void)                          { PROBE("jt1193"); }
static void l2062(void)                           { PROBE("l2062"); }

/* JT[176] (CODE 7 + 0x162e, 36 sites) — window paint init / commit.
 *
 * Encounter-mode (jt1200 == 3) and design-mode paths set up the
 * pen-clip rect to different left edges (280 vs 8093) via JT[1173];
 * both then call jt1001(8000, 8000, 1, 4) to commit the pen state
 * and chain through jt1193 + L2062 for the deferred-paint flush.
 *
 * JT[42]'s message-append wraps every text-draw between two
 * jt176() calls so the QuickDraw shim sees a clean paint boundary;
 * other callers do the same. The bodies of the inner JT[1173] /
 * JT[1193] / L2062 leaves stay PROBE-deferred — their semantics
 * are "set up window clip / commit pen", which the QuickDraw shim
 * already handles implicitly. */
static void   jt176(void)
{
	PROBE("jt176");
	if (jt1200() == 3)
		jt1173(280,  8000, 8100, 8160);
	else
		jt1173(8093, 8000, 8100, 8160);
	jt1001(8000, 8000, 1, 4);
	jt1193();
	l2062();
}
static void   jt584(long a, const char *str)     { PROBE("jt584"); (void)a; (void)str; }
static void   jt585(void)                        { PROBE("jt585"); }
static void   jt904(void *buf)                   { PROBE("jt904"); (void)buf; }
static void   jt942_caseN(short a) __attribute__((unused));
static void   jt942_caseN(short a)               { jt942(a); }

/* L0f2e — case 1 (Modify Character). CODE 12 + 0x0f2e.
 *
 *   tstb a5@(-14439)
 *   beqw L1242                  // flag clear → no action
 *   jsr  L15e2                   // local helper in CODE 12
 *   braw L1242
 */
static int l0f2e(short a)
{
	(void)a;
	PROBE("jt918/case1 L0f2e");
	if (g_a5_14439 != 0)
		l15e2();
	return 0;
}

/* L0f3e — case 2 (Delete Character). CODE 12 + 0x0f3e.
 *
 *   tstb a5@(-14438); beqw L1242
 *   movel a5@(-18844),sp@-; jsr JT[1199]; addql #4,sp
 *   movel d0, a5@(-18882)
 *   jsr  JT[560]
 *   clrb a5@(-27946)
 *   braw L1242
 */
static int l0f3e(short a)
{
	(void)a;
	PROBE("jt918/case2 L0f3e");
	if (g_a5_14438 == 0)
		return 0;
	g_a5_18882 = jt1199(g_a5_18844);
	jt560();
	g_a5_27946 = 0;
	return 0;
}

/* L0f60 — case 3 (Create Character). CODE 12 + 0x0f60.
 *
 *   tstb a5@(-14437); beqw L1242
 *   jsr  JT[557]
 *   clrb a5@(-27946)
 *   braw L1242
 */
static int l0f60(short a)
{
	(void)a;
	PROBE("jt918/case3 L0f60");
	if (g_a5_14437 == 0)
		return 0;
	jt557();
	g_a5_27946 = 0;
	return 0;
}

/* L0f74 — case 4 (Remove Character). CODE 12 + 0x0f74.
 *
 *   tstb a5@(-14436); beqw L1242
 *   movel a5@(-27932),sp@-; jsr JT[556]; addql #4,sp
 *   moveb d0, fp@(-11)                   // local = (byte)JT[556] return
 *   cmpib #17, fp@(-11); beqw L102e      // local == 17 → skip to clear
 *   pea  fp@(-16); pushw 8; movel a5@(-27932),sp@-
 *   jsr  JT[41]; lea sp@(10),sp
 *   tstb d0; beqs L0fbe
 *   clrl -(sp); pushw 8; movel a5@(-27932),sp@-; jsr JT[878]
 * L0fbe: pea fp@(-16); pushw 105; movel a5@(-27932),sp@-
 *   jsr JT[41]; lea sp@(10),sp
 *   tstb d0; beqs L0fe8
 *   clrl -(sp); pushw 105; movel a5@(-27932),sp@-; jsr JT[878]
 * L0fe8: moveb fp@(-11), d0; extw d0; jsr JT[3]
 *   [JT[3] case data: lo=3, hi=4, default=L102e, case 3=L0ffc, case 4=L1016]
 * L0ffc: pushw 0,255,0,8; movel a5@(-27932),sp@-; jsr JT[876]; bras L102e
 * L1016: pushw 0,255,0,105; movel a5@(-27932),sp@-; jsr JT[876]; falls through
 * L102e: clrb a5@(-27946); braw L1242
 */
static int l0f74(short a)
{
	short  local;
	long   probe_buf = 0;

	(void)a;
	PROBE("jt918/case4 L0f74");
	if (g_a5_14436 == 0)
		return 0;

	local = (short)(signed char)jt556(g_a5_27932);
	if (local == 17) {
		g_a5_27946 = 0;
		return 0;
	}

	if (jt41(g_a5_27932, 8, &probe_buf) != 0)
		jt878(g_a5_27932, 8, 0);
	if (jt41(g_a5_27932, 105, &probe_buf) != 0)
		jt878(g_a5_27932, 105, 0);

	switch (local) {
	case 3:
		jt876(g_a5_27932, 8,   0, 255, 0);
		break;
	case 4:
		jt876(g_a5_27932, 105, 0, 255, 0);
		break;
	default:
		break;
	}
	g_a5_27946 = 0;
	return 0;
}
/* L1036 — case 5 (Add Character). CODE 12 + 0x1036.
 *
 *   tstb a5@(-14435); beqw L1242
 *   pea  fp@(-7); jsr JT[904]; addql #4, sp
 *   braw L1242
 */
static int l1036(short a)
{
	unsigned char local_byte = 0;

	(void)a;
	PROBE("jt918/case5 L1036");
	if (g_a5_14435 == 0)
		return 0;
	jt904(&local_byte);
	return 0;
}

/* L104c — case 6 (View Character). CODE 12 + 0x104c.
 *
 *   tstb a5@(-14434); beqw L1242
 *   jsr  L12a0
 *   clrb a5@(-27946)
 *   braw L1242
 */
static int l104c(short a)
{
	(void)a;
	PROBE("jt918/case6 L104c");
	if (g_a5_14434 == 0)
		return 0;
	l12a0();
	g_a5_27946 = 0;
	return 0;
}

/* L1060 — case 7 (Human Change Class). CODE 12 + 0x1060.
 *
 *   tstb a5@(-14433); beqw L1242
 *   tstl a5@(-27932); beqw L1242
 *   moveal a5@(-27932), a0
 *   moveb a0@(147), d0; btst #7, d0; bnes L109e
 *      pea STRS+0x5fb4; movel a5@(-27932),sp@-; jsr JT[584]; addql #8,sp
 *      pushw 1; clrw -(sp); jsr JT[19]; addql #4,sp
 *      bras L10a6
 *   L109e: jsr JT[76]; jsr L185e
 *   L10a6: tstl a5@(-27932); bnes L10c2
 *          tstb fp@(9); beqs L10bc
 *             jsr JT[88]; clrb a5@(-27990); bras L10c2
 *          L10bc: moveb #1, a5@(-27982)
 *   L10c2: clrb a5@(-27946); braw L1242
 */
static int l1060(short a)
{
	const unsigned char *block;

	PROBE("jt918/case7 L1060");
	if (g_a5_14433 == 0 || g_a5_27932 == 0)
		return 0;
	block = (const unsigned char *)g_a5_27932;
	if ((block[147] & 0x80) == 0) {
		jt584(g_a5_27932, ua_strs_at(0x5fb4));
		jt19(0, 1);
	} else {
		jt76();
		l185e();
	}
	if (g_a5_27932 == 0) {
		if ((a & 0xFF) != 0) {
			l5124();
			g_a5_27990 = 0;
		} else {
			g_a5_27982 = 1;
		}
	}
	g_a5_27946 = 0;
	return 0;
}

/* L10ca — case 8 (Exit From Play). CODE 12 + 0x10ca.
 *
 *   tstb a5@(-14432); beqw L1242
 *   tstl a5@(-27932); beqs L10f4
 *     tstb a5@(-27946); bnes L10f4
 *     pushw 1; pea STRS+0x5fb6; jsr JT[159]; addql #6, sp
 *     tstb d0; beqw L1242
 *   L10f4: tstb fp@(9); bnes L1108; bras L112c
 *   L10fc: pushw 1; clrw -(sp); jsr JT[19]; addql #4, sp
 *   L1108: tstl a5@(-27932); bnes L10fc
 *          jsr JT[582]
 *          tstl a5@(-27932); bnew L1242
 *          jsr JT[88]
 *          moveb a5@(-27990), a5@(-27989)
 *          clrb  a5@(-27990); braw L1242
 *   L112c: pushw 1; jsr JT[942]; addql #2, sp
 *          moveb #1, a5@(-27982)
 *          moveq #1, d0; braw L1262   // EXIT, return 1
 */
static int l10ca(short a)
{
	PROBE("jt918/case8 L10ca");
	if (g_a5_14432 == 0)
		return 0;
	if (g_a5_27932 != 0 && g_a5_27946 == 0) {
		if (jt159(ua_strs_at(0x5fb6), 1) == 0)
			return 0;
	}
	if ((a & 0xFF) != 0) {
		while (g_a5_27932 != 0)
			jt19(0, 1);
		jt582();
		if (g_a5_27932 != 0)
			return 0;
		l5124();
		g_a5_27989 = g_a5_27990;
		g_a5_27990 = 0;
		return 0;
	}
	jt942(1);
	g_a5_27982 = 1;
	return 1;       /* L1262 — return 1 */
}

/* L1142 — case 9 (Begin Adventuring). CODE 12 + 0x1142.
 *
 *   tstb a5@(-14431); beqw L1242
 *   tstl a5@(-27928); beqw L1242
 *   jsr  JT[585]
 *   braw L1242
 */
static int l1142(short a)
{
	(void)a;
	PROBE("jt918/case9 L1142");
	if (g_a5_14431 != 0 && g_a5_27928 != 0)
		jt585();
	return 0;
}

/* L115a — case 10 (Save Current Game). CODE 12 + 0x115a.
 *
 * Picks one of JT[78] (L67ca) / JT[84] (L68f8) / no-op for the
 * tear-down phase based on g_a5_27990 and the player-data handle's
 * by-offset bytes, then calls L02dc and JT[176] before clearing the
 * save-state byte and the dirty flag; returns 1 (exit jt918).
 */
static int l115a(short a)
{
	const unsigned char *block;
	short                state;

	(void)a;
	PROBE("jt918/case10 L115a");
	if (g_a5_14430 == 0)
		return 0;
	if (g_a5_27928 == 0 && g_a5_27988 == 0)
		return 0;

	g_a5_27990 = g_a5_27989;
	if (g_a5_27990 == 0)
		g_a5_27990 = 4;

	block = (const unsigned char *)g_a5_28006;
	if ((block != NULL && block[134] != 0) || g_a5_24262 == 9) {
		state = g_a5_27990;
		switch (state) {
		case 0:
		case 4:
			l67ca();
			l02dc(g_a5_27932);
			break;
		case 3:
			l68f8();
			break;
		case 1:
		case 2:
		default:
			break;
		}
	} else if (g_a5_27990 == 3 && block != NULL && block[36] == 1) {
		l68f8();
		l02dc(g_a5_27932);
	} else {
		l67ca();
		l02dc(g_a5_27932);
	}

	jt176();
	{
		unsigned char *b = (unsigned char *)g_a5_28006;

		if (b != NULL)
			b[48] = 0;
	}
	g_a5_27946 = 0;
	return 1;
}

/* L120c — case 11 (Load Saved Game). CODE 12 + 0x120c.
 *
 *   moveb #1, a5@(-18472)
 *   tstb a5@(-14429); beqs L1242
 *   tstl a5@(-27928); beqs L123e
 *   tstb a5@(-27946); bnes L123e
 *   clrw -(sp); movel a5@(-14284), sp@-; jsr JT[159]; addql #6, sp
 *   tstb d0; bnes L123a
 *   jsr JT[585]; bras L1242
 *
 *   L123a / L123e: return 0 from jt918 entirely.
 */
static int l120c(short a)
{
	(void)a;
	PROBE("jt918/case11 L120c");
	g_a5_18472 = 1;
	if (g_a5_14429 == 0)
		return 0;
	if (g_a5_27928 == 0)
		return -1;
	if (g_a5_27946 != 0)
		return -1;
	if (jt159((const char *)g_a5_14284, 0) != 0)
		return -1;
	jt585();
	return 0;
}

/* Count entries in the design linked-list. The original walks
 * g_a5_27928 → next → next... incrementing a counter when each entry's
 * byte at offset +147 doesn't have its high bit set ("hidden" flag).
 * Returns count clamped to the engine's "lots of designs?" threshold
 * (>5). Until the design-list lift lands, the skeleton returns 0 so
 * the saved-game arm takes the "few designs" branch (clearing c79e). */
static short jt918_count_visible_designs(void)
{
	return 0;
}

/*
 * jt918 — new-game / select-design dialog driver. CODE 12 + 0x0d90.
 *
 * Entry side effects + an unbounded action loop that the user breaks
 * out of by picking "quit" (L123a) or "go-away" (L123e). On entry the
 * loop sets up the c799..c7a2 flag cluster from the current save state
 * (saved-game arm at L0df6 if g_a5_27932 != 0, fresh arm at L0e98
 * otherwise); each iteration:
 *
 *   - L0dd4: jt112(1); if local > 11 → jt108(1); else jt81()
 *   - L0df6/L0e98: cluster setup (only entered once per outer pass —
 *     see comment in the asm; for the skeleton we run it on every
 *     iteration since the c79x writes are idempotent)
 *   - L0ec6: local = L0aae(); if local in {0..6, 8..11} → jt76();
 *     if local == 1 → L02dc(g_a5_27932);
 *   - JT[3] switch on local (0..11) — each case fires the per-action
 *     arm. Case bodies live in CODE 12 + 0x0f1a..0x123a and stay as
 *     PROBE stubs.
 *
 * Exit edges (L123a / L123e) live inside two of the case bodies and
 * return 0. Until those bodies are lifted, the skeleton runs one
 * iteration, hits the L0aae-returns-0 case, and bails to keep the
 * return-0 contract.
 */
static int jt918(short a)
{
	short local;
	short iter_guard;

	(void)a;
	PROBE("jt918");

	/* Entry side effects — CODE 12 + 0x0d90..0x0dce. */
	g_a5_5798   = 135;
	g_a5_27989  = g_a5_27990;            /* save selector */
	g_a5_27990  = 0;
	g_a5_19169  = 1;
	jt399(g_a5_22727, 4, 1);             /* fill 4 bytes with 1 */
	g_a5_5796   = 0;
	jt131(6);
	local = 0;                            /* fp@(-10) before L125e jumps */

	/* Outer loop — the asm jumps from entry directly to L125e and then
	 * to L0dd4. We model that as an unbounded for-loop with the body
	 * at L0dd4. iter_guard breaks out after the first iteration while
	 * L0aae / the case bodies are still stubs returning 0 (otherwise
	 * we'd spin forever). */
	for (iter_guard = 0; iter_guard < 1; iter_guard++) {
		/* L0dd4: per-iteration prologue. */
		(void)jt112(1);
		if (local > 11)
			(void)jt108(1);
		else
			jt81();

		/* L0df6: cluster setup. The Mac calls one arm per outer
		 * entry, not per iteration — but the writes are idempotent
		 * (each byte is set unconditionally), so re-running is
		 * safe. */
		if (g_a5_27932 != 0) {
			l02dc(g_a5_27932);
			g_a5_14439 = 1;
			g_a5_14438 = 1;
			{
				/* g_a5_28006[+48] >= 0 OR g_a5_22730 != 0 → 1, else 0 */
				const unsigned char *h =
				    (const unsigned char *)g_a5_28006;
				int dirty = (h != NULL && (signed char)h[48] >= 0)
				         || (g_a5_22730 != 0);

				g_a5_14437 = (unsigned char)(dirty ? 1 : 0);
				g_a5_14436 = g_a5_14437;
			}
			g_a5_14435 = 1;
			g_a5_14434 = 1;
			{
				short n = jt918_count_visible_designs();

				if (n > 5)
					g_a5_14434 = 0;
			}
			g_a5_14433 = 1;
			g_a5_14432 = 1;
			g_a5_14431 = 1;
			g_a5_14430 = 1;
		} else {
			/* L0e98: fresh-init defaults. */
			g_a5_14439 = 1;
			g_a5_14438 = 0;
			g_a5_14437 = 0;
			g_a5_14436 = 0;
			g_a5_14435 = 0;
			g_a5_14434 = 1;
			g_a5_14433 = 0;
			g_a5_14432 = 1;
			g_a5_14431 = 0;
			g_a5_14430 = 0;
		}

		/* L0ec6: poll input. */
		local = (short)l0aae();
		if (local != 7 && local <= 11)
			jt76();
		if (local == 1)
			l02dc(g_a5_27932);

		/* JT[3] switch on local (0..11). Each case returns 0 to
		 * continue the outer loop, 1 to exit jt918 returning 1, or
		 * -1 to exit returning 0 (the L123a / L123e edges). */
		{
			int rc = 0;

			switch (local) {
			case 0:  rc = l0f1a(a); break;
			case 1:  rc = l0f2e(a); break;
			case 2:  rc = l0f3e(a); break;
			case 3:  rc = l0f60(a); break;
			case 4:  rc = l0f74(a); break;
			case 5:  rc = l1036(a); break;
			case 6:  rc = l104c(a); break;
			case 7:  rc = l1060(a); break;
			case 8:  rc = l10ca(a); break;
			case 9:  rc = l1142(a); break;
			case 10: rc = l115a(a); break;
			case 11: rc = l120c(a); break;
			default: break;
			}
			if (rc == 1)
				return 1;
			if (rc < 0)
				return 0;
		}
		/* L1242 → L125e → L0dd4 (continue). */
	}
	return 0;       /* matches the asm's L123a / L123e exit edges */
}

static void l0aae_unused_warn(void) __attribute__((unused));
static void l0aae_unused_warn(void) { (void)l0aae; (void)l02dc; }

/* =========================================================================
 * L5124 — L07dc's first-time init (CODE 6 + 0x5124).
 *
 * The play-loop body runs L5124 once on the very first iteration, when the
 * mode flag (g_a5_18485) is still clear. The function:
 *
 *   - zeroes 1024 bytes inside the player-data handle at *g_a5_28006 + 1,
 *     2000 bytes at the pointer in g_a5_13038, and a 6-byte block at
 *     g_a5_12288, all through JT[399] (the "fill / zero buffer" service);
 *   - writes a small handful of fields inside the handle (offsets 18 = 4,
 *     32 = 0, 34 = 1, 39 = 3);
 *   - resets ~30 A5-world bytes / shorts / longs to their game-start
 *     defaults (mostly clears, a few set to 1 / 4 / 90, two pairs to -1,
 *     and the colour-ish pair (-22330, -22331) to (-24, -37));
 *   - calls JT[174] (CODE 7 + 0x2062), the per-segment graphics / state
 *     init the engine layers above ua_main rely on.
 *
 * g_a5_28006 has to point at the player-data block before L5124 runs;
 * the engine code that sets it lives in a CODE segment we haven't reached
 * yet. While the pointer is NULL (probe mode), the handle writes are
 * skipped — the surrounding A5 resets still take effect.
 * ========================================================================= */

/* A5-world globals L5124 touches beyond those already declared. Many are
 * file-static for now; they'll join a shared A5-world header as soon as
 * other CODE segments reach for the same offsets. */
static unsigned char *g_a5_13038;            /* 2000-byte buffer ptr */
static short          g_a5_27984;            /* cleared (word)     */
static long           g_a5_27940;            /* cleared (long)     */
static short          g_a5_24142;            /* set to 1 (word)    */
/* g_a5_24304 — single definition lives at the forward decl above L5700. */

/* JT[399] is the engine's "fill / zero buffer" service in this context.
 * mode=0 + size=N + pointer zeroes N bytes (matched against L5124's three
 * call sites and jt918's load-into-buffer call site). The shim still ships
 * it as a PROBE-instrumented stub; once lifted, both consumers benefit. */

static void l5124(void)
{
	PROBE("l5124");

	if (g_a5_28006 != NULL) {
		unsigned char *handle = (unsigned char *)g_a5_28006;

		jt399(handle + 1, 1024, 0);          /* zero 1024 bytes */
		handle[39] = 3;
		handle[34] = 1;
	}

	g_a5_18474 = 0;
	g_a5_18473 = 0;
	g_a5_4944  = 0;
	g_a5_22218 = 90;

	if (g_a5_13038 != NULL)
		jt399(g_a5_13038, 2000, 0);          /* zero 2000 bytes */

	{
		unsigned char *six = &g_a5_12288;    /* the three vars are
		                                        adjacent in the A5 world */
		jt399(six, 6, 0);
	}
	g_a5_12288 = 0;
	g_a5_12287 = 0;
	g_a5_12286 = 4;

	g_a5_22226 = 1;
	g_a5_27981 = 1;
	g_a5_27984 = 0;
	g_a5_27928 = 0;
	g_a5_27932 = 0;
	g_a5_27940 = 0;

	if (g_a5_28006 != NULL) {
		unsigned char *handle = (unsigned char *)g_a5_28006;

		handle[18] = 4;
		handle[32] = 0;
	}

	g_a5_12290 = 0;
	g_a5_24142 = 1;
	g_a5_23190 = 0;

	g_a5_22284 = 0;
	g_a5_22283 = 0;
	g_a5_22282 = 0;
	g_a5_22281 = 0;

	g_a5_27982 = 0;
	g_a5_22279 = 0;
	g_a5_24304[0] = 0;          /* clear the name buffer (empty string) */
	g_a5_24283 = 0;
	g_a5_24262 = 0xFF;
	g_a5_24261 = 0xFF;
	g_a5_27946 = 0;
	g_a5_22330 = 0xE8;                       /* moveq #-24 */
	g_a5_22331 = 0xDB;                       /* moveq #-37 */
	g_a5_22273 = 0;
	g_a5_22626 = 1;
	g_a5_24256 = 0xFF;

	jt174();                                 /* per-segment graphics init */

	g_a5_24140 = 1;
	g_a5_27990 = 4;
	g_a5_27989 = 0;
	g_a5_23187 = 0;
	g_a5_22269 = 0;
	g_a5_22633 = 0;
	g_a5_22635 = 0;
	g_a5_22268 = 0;
	g_a5_22225 = 0;
	g_a5_24148 = 0;
	g_a5_27987 = 0;
	g_a5_27916 = 0;
	g_a5_22275 = 0;
}
