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
 */

#include "boot.h"
#include "core.h"             /* core_init */
#include "master.h"           /* master_init */
#include "str.h"              /* ua_strcmp, ua_get_string */
#include "fc.h"               /* fc_dump */

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
static short jt398(const char *path, short flags)  { return 0; }  /* CODE 3 + 0x37e4 */
static void  jt411(short status)                   { }            /* CODE 3 + 0x3de2 */
static void  jt480(short a, long b)                { }            /* CODE 3 + 0x03c6 */
static void  jt445(void)                           { }            /* CODE 3 + 0x294e */
static void  jt415(short a)                        { }            /* CODE 3 + 0x37da */
static void  jt1129(short a)                       { }            /* CODE 4 + 0x4756 */
static void  jt1130(void)                          { }            /* CODE 4 + 0x61f6 */
static void  jt1081(void)                          { }            /* CODE 5 + 0x0062 */
static void  jt1009(short a, short b)              { }            /* CODE 5 + 0x0a34 */
static void  jt977(void)                           { }            /* CODE 5 + 0x0aaa */
static void  jt989(void (*handler)(void), short flag, const char *name, short code) { }
                                                                  /* CODE 5 + 0x1b56 */
static void  jt361(short a)                        { }            /* CODE 8 + 0x71ec */
static void  jt919(void)                           { }            /* CODE 12 + 0x1b12 */
static void  jt920(void)                           { }            /* CODE 12 + 0x1ba8 */
static int   jt931(void)                           { return 0; }  /* CODE 12 + 0x430c */
static void  jt949(void)                           { }            /* CODE 20 + 0x77a2 */
static void  jt956(void)                           { }            /* CODE 21 + 0x326a */
static int   jt315(void)                           { return 0; }  /* CODE 22 + 0x4d8a */

/* Intra-CODE-6 helpers, still to lift. */
static void  l0444(void)        { }     /* CODE 6 + 0x0444 */
static void  l3918(long a)      { }     /* CODE 6 + 0x3918 */
static void  l4d98(void)        { }     /* CODE 6 + 0x4d98 */
static void  l5888(short a)     { }     /* CODE 6 + 0x5888 */
static void  l5ac0(void)        { }     /* CODE 6 + 0x5ac0 */
static void  l5f66(void)        { }     /* CODE 6 + 0x5f66 */
static void  l6ada(short a)     { }     /* CODE 6 + 0x6ada */
static void  l07dc(void)        { }     /* CODE 6 + 0x07dc — the play-loop body */

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
	jt1081();
	jt415(0);
	return 0;
}
