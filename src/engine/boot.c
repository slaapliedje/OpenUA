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

/* Local CODE-6 helpers L07dc calls — L5124 lifted below; the rest are stubs. */
static void          l5124(void);                                                /* CODE 6 + 0x5124 — lifted below */
static void          l4b40(const char *msg, short a, short b) { (void)msg; PROBE("l4b40"); }
                                                                                 /* CODE 6 + 0x4b40 (alert?) */
static void          l67ca(void)              { PROBE("l67ca"); }                /* CODE 6 + 0x67ca */
static void          l68f8(void)              { PROBE("l68f8"); }                /* CODE 6 + 0x68f8 */
static void          l2cb0(short a, short b)  { PROBE("l2cb0"); }                /* CODE 6 + 0x2cb0 */

/* L07dc's play-loop predicate flag, A5-relative offset -4944. Lives here
 * (above the JT entries) so jt942 / jt943 can read and write it directly;
 * L5124 zeroes it as part of the game-start state reset. Other A5 globals
 * stay co-located with their main lift. */
static unsigned char g_a5_4944;

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
static void          jt941(void)              { PROBE("jt941"); }                /* CODE 20 + 0x4108 */
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

/* Local helpers and JT entries jt918 calls. */
static void jt399(short a, short b, void *buf) { (void)buf; PROBE("jt399"); }    /* CODE 3 + 0x39d2 */
static void jt131(short a)         { PROBE("jt131"); }                            /* CODE 6 + 0x35e   */
static int  jt112(short a)         { PROBE("jt112"); return 0; }                  /* CODE 6 + 0x38fe  */
static int  jt108(short a)         { PROBE("jt108"); return 0; }                  /* CODE 6 + 0x38d0  */
static void jt81(void)             { PROBE("jt81"); }                             /* CODE 6 + 0x6a10  */
static void jt76(void)             { PROBE("jt76"); }                             /* CODE 6 + 0x670c  */
static int  jt3(short a)           { PROBE("jt3"); return 0; }                    /* CODE 1 + 0x158   */
static void jt174(void)            { PROBE("jt174"); }                            /* CODE 7 + 0x2062  */
static int  l0aae(void)            { PROBE("l0aae"); return 0; }                  /* CODE 12 + 0x0aae */
static void l02dc(long a)          { PROBE("l02dc"); }                            /* CODE 12 + 0x02dc */

/* Additional A5-world globals jt918 touches (entry setup + buffer). */
static short          g_a5_5798;             /* mode word — set to 135           */
static short          g_a5_5796;             /* counter — cleared                */
static unsigned char  g_a5_19169;            /* flag — set to 1                  */
static unsigned char  g_a5_22727[4];         /* 4-byte buffer JT[399] fills      */

static int jt918(short a)
{
	(void)a;
	PROBE("jt918 (skeleton)");

	/* Entry side effects — CODE 12 + 0x0d90..0x0dce. */
	g_a5_5798   = 135;
	g_a5_27989  = g_a5_27990;            /* save selector */
	g_a5_27990  = 0;
	g_a5_19169  = 1;
	jt399(1, 4, g_a5_22727);
	g_a5_5796   = 0;
	jt131(6);

	/* TODO: main loop at CODE 12 + 0x0dd4 → L125e. Until the body is
	 * lifted, behave as the prior stub — return 0 so L07dc takes the
	 * cleanup path. (void)-ing the loop helpers below ensures the
	 * stub-trace probe still reports them, even though the skeleton
	 * doesn't yet call them.) */
	(void)jt112;
	(void)jt108;
	(void)jt81;
	(void)jt76;
	(void)jt3;
	(void)jt174;
	(void)l0aae;
	(void)l02dc;
	return 0;
}

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
static unsigned char  g_a5_18474;            /* cleared            */
static unsigned char  g_a5_18473;            /* cleared            */
/* g_a5_4944 lives near the top of the file with the jt942/jt943 lift. */
static unsigned char  g_a5_22218;            /* set to 90          */
static unsigned char *g_a5_13038;            /* 2000-byte buffer ptr */
static unsigned char  g_a5_12288;            /* 3-byte cluster ...  */
static unsigned char  g_a5_12287;
static unsigned char  g_a5_12286;            /* set to 4           */
static unsigned char  g_a5_22226;            /* set to 1           */
static unsigned char  g_a5_27981;            /* set to 1           */
static short          g_a5_27984;            /* cleared (word)     */
static long           g_a5_27940;            /* cleared (long)     */
static unsigned char  g_a5_12290;            /* cleared            */
static short          g_a5_24142;            /* set to 1 (word)    */
static unsigned char  g_a5_23190;            /* cleared            */
static unsigned char  g_a5_22284;
static unsigned char  g_a5_22283;
static unsigned char  g_a5_22282;
static unsigned char  g_a5_22281;
static unsigned char  g_a5_27982;
static unsigned char  g_a5_22279;
static unsigned char  g_a5_24304;
static unsigned char  g_a5_24283;
static unsigned char  g_a5_24262;            /* set to 0xFF        */
static unsigned char  g_a5_24261;            /* set to 0xFF        */
static unsigned char  g_a5_27946;
static unsigned char  g_a5_22330;            /* set to 0xE8 (-24)  */
static unsigned char  g_a5_22331;            /* set to 0xDB (-37)  */
static unsigned char  g_a5_22273;
static unsigned char  g_a5_22626;            /* set to 1           */
static unsigned char  g_a5_24256;            /* set to 0xFF        */
static unsigned char  g_a5_24140;            /* set to 1           */
static unsigned char  g_a5_23187;
static unsigned char  g_a5_22269;
static unsigned char  g_a5_22633;
static unsigned char  g_a5_22635;
static unsigned char  g_a5_22268;
static unsigned char  g_a5_22225;
static unsigned char  g_a5_24148;
static unsigned char  g_a5_27987;
static unsigned char  g_a5_27916;
static unsigned char  g_a5_22275;

/* JT[399] is the engine's "fill / zero buffer" service in this context.
 * mode=0 + size=N + pointer zeroes N bytes (matched against L5124's three
 * call sites and jt918's load-into-buffer call site). The shim still ships
 * it as a PROBE-instrumented stub; once lifted, both consumers benefit. */

static void l5124(void)
{
	PROBE("l5124");

	if (g_a5_28006 != NULL) {
		unsigned char *handle = (unsigned char *)g_a5_28006;

		jt399(0, 1024, handle + 1);          /* zero 1024 bytes */
		handle[39] = 3;
		handle[34] = 1;
	}

	g_a5_18474 = 0;
	g_a5_18473 = 0;
	g_a5_4944  = 0;
	g_a5_22218 = 90;

	if (g_a5_13038 != NULL)
		jt399(0, 2000, g_a5_13038);          /* zero 2000 bytes */

	{
		unsigned char *six = &g_a5_12288;    /* the three vars are
		                                        adjacent in the A5 world */
		jt399(0, 6, six);
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
	g_a5_24304 = 0;
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
