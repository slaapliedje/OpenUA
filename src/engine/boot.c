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

#include <stddef.h>           /* NULL */

#include "boot.h"
#include "core.h"             /* core_init */
#include "master.h"           /* master_init */
#include "str.h"              /* ua_strcmp, ua_get_string */
#include "fc.h"               /* fc_dump */

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
static short jt398(const char *path, short flags)  { PROBE("jt398"); return 0; }  /* CODE 3 + 0x37e4 */
static void  jt411(short status)                   { PROBE("jt411"); }            /* CODE 3 + 0x3de2 */
static void  jt480(short a, long b)                { PROBE("jt480"); }            /* CODE 3 + 0x03c6 */
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

	/* Phase 4 — secondary init and the first UI handler. */
	jt480(arg1, arg2);
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

/* Local CODE-6 helpers L07dc calls — all stubs for now. */
static void          l5124(void)              { PROBE("l5124"); }                /* CODE 6 + 0x5124 */
static void          l4b40(const char *msg, short a, short b) { (void)msg; PROBE("l4b40"); }
                                                                                 /* CODE 6 + 0x4b40 (alert?) */
static void          l67ca(void)              { PROBE("l67ca"); }                /* CODE 6 + 0x67ca */
static void          l68f8(void)              { PROBE("l68f8"); }                /* CODE 6 + 0x68f8 */
static void          l2cb0(short a, short b)  { PROBE("l2cb0"); }                /* CODE 6 + 0x2cb0 */

/* Cross-segment JT entries L07dc calls — stubs. */
static void          jt942(short a)           { PROBE("jt942"); }                /* CODE 20 + 0x472a */
static void          jt582(void)              { PROBE("jt582"); }                /* CODE 15 + 0x153e */
static void          jt941(void)              { PROBE("jt941"); }                /* CODE 20 + 0x4108 */
static int           jt918(short a)           { PROBE("jt918"); return 0; }      /* CODE 12 + 0x0d90 */
static void          jt937(long a)            { PROBE("jt937"); }                /* CODE 12 + 0x02dc */
static void          jt938(void)              { PROBE("jt938"); }                /* CODE 12 + 0x0562 */
static void          jt217(short a, short b, short c, short d) { PROBE("jt217"); }
                                                                                 /* CODE 7 + 0x57d2 */
static void          jt948(void)              { PROBE("jt948"); }                /* CODE 20 + 0x4a12 */
static int           jt943(void)              { PROBE("jt943"); return 0; }      /* CODE 20 + 0x4738 */

/* A5-world globals L07dc touches — named by offset until consumers tell us
 * the semantic. Sizes from the disassembly's tstb / tstl / moveb / movel.
 * Many of these will move to a shared A5-world header once other segments
 * are lifted and the same globals show up there. */
static unsigned char  g_a5_18485;             /* mode flag: 0 new, !=0 resume */
static unsigned char  g_a5_18828;             /* byte → 16-bit dest          */
static unsigned char  g_a5_18827;             /* byte → counter base         */
static short          g_a5_18878;             /* 16-bit dest                 */
static unsigned char  g_a5_18488;             /* counter (g_a5_18827 - 1)    */
static long           g_a5_27928;             /* save-list head / count      */
static unsigned char  g_a5_27989;             /* selector default            */
static unsigned char  g_a5_27990;             /* selector (state)            */
static void          *g_a5_28006;             /* pointer; +36 reads a byte    */
static long           g_a5_27932;             /* shutdown-drain pointer      */
static unsigned char  g_a5_12291;             /* payload byte 0              */
static unsigned char  g_a5_12292;             /* payload byte 1              */
static unsigned char  g_a5_12293;             /* payload byte 2              */
static unsigned char  g_a5_12294;             /* payload byte 3              */

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
