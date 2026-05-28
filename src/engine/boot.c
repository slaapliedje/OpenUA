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
#include "events.h"           /* WaitNextEvent (jt1125 event poll)   */
#include "windows.h"          /* InvalRect (L71ac osEvt arm)         */

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
#define g_a5_18395 g_a5_byte(-18395)   /* JT[108]: GrafPort "dirty" flag */
#define g_a5_18393 g_a5_byte(-18393)   /* L3994:   "snapshot is valid" flag */
#define g_a5_18392 g_a5_long(-18392)   /* L3994:   handle of resource to snapshot */
#define g_a5_27468 g_a5_byte(-27468)   /* L6bbe: id-table entry count       */

/* JT[1089]'s pen-state cluster (CODE 5 + 0x024c..0x028x):
 *   -4894 / -4892 — packed (col, style) the engine's color word
 *                  decomposes into; L024c stores the word.
 *   -4898 / -4896 — transformed (x, y) pen position; L0264 stores
 *                  after the jt1135 coord remap. */
#define g_a5_4894  g_a5_word(-4894)
#define g_a5_4892  g_a5_byte(-4892)
#define g_a5_4898  g_a5_word(-4898)
#define g_a5_4896  g_a5_word(-4896)
#define g_a5_4902  g_a5_long(-4902)    /* JT[1083]: LCG PRNG state    */
#define g_a5_21152 g_a5_byte(-21152)   /* JT[878]: inventory-item bucket */
#define g_a5_4676  g_a5_byte(-4676)    /* JT[989]: handler config byte */
#define g_a5_4670  g_a5_byte(-4670)    /* JT[989]: handler config byte */
#define g_a5_2344  g_a5_byte(-2344)    /* JT[1129]: input-source mode */
#define g_a5_4680  g_a5_long(-4680)    /* JT[989]: handler pointer */
#define g_a5_4674  g_a5_long(-4674)    /* JT[989]: handler context */
#define g_a5_9286  g_a5_long(-9286)    /* JT[447]: DLItem pool base seed */
#define g_a5_4886  g_a5_word(-4886)    /* JT[977]/JT[1009]: paint-stack depth (0..4) */

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

/* L6bbe / JT[525] / JT[531] tables — id-key array + 6-byte record
 * array. The Mac walks g_a5_25676 (longs) for a matching key, then
 * indexes the same row in g_a5_27472 (bytes-per-record = 6). */
#define g_a5_25676 g_a5_longs(-25676)
#define g_a5_27472 g_a5_buf  (-27472)

/* JT[1009] / JT[977] paint-stack arrays — three parallel short
 * arrays indexed by g_a5_4886 (depth 0..4). jt1009 pushes a frame,
 * jt977 pops. The depth tracks nested deferred paint regions. */
#define g_a5_4880  g_a5_shorts(-4880)
#define g_a5_4870  g_a5_shorts(-4870)
#define g_a5_4860  g_a5_shorts(-4860)

/* L2d3e selection-nav state. */
#define g_a5_4884  g_a5_word(-4884)    /* current selection index (signed) */
#define g_a5_4882  g_a5_word(-4882)    /* selection step / default key */

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
/* JT[1129] (CODE 4 + 0x4756) — 1-case JT[3] switch on `a`.
 *
 *   if (a == 1) g_a5_2344 = (unsigned char)a;
 *
 * Inline table at 0x4762 has min=max=1, single arm. Sets the
 * input-source mode flag on a specific command code. */
static void  jt1129(short a)
{
	PROBE("jt1129");
	if (a == 1)
		g_a5_2344 = (unsigned char)(a & 0xff);
}

/* JT[1130] / JT[920] / JT[956] (CODE 4 / CODE 12 / CODE 21) —
 * empty bodies (literally `rts`). Placeholder hooks the Mac
 * build left in for symmetry / future use. */
static void  jt1130(void)                          { PROBE("jt1130"); }
static void  jt920(void)                           { PROBE("jt920"); }
static void  jt956(void)                           { PROBE("jt956"); }
                                                   /* first CODE 21 entry */

/* JT[1009] (CODE 5 + 0x0a34) — push paint-stack frame.
 *
 * Increments g_a5_4886 (capped at 4), then writes three parallel
 * shorts indexed by the new depth:
 *
 *   g_a5_4880[depth] = a
 *   g_a5_4870[depth] = b * 4
 *   g_a5_4860[depth] = 15        (default style)
 *
 * Sister of jt977 which decrements the same counter. The stack
 * tracks nested deferred paint regions; the arrays at -4880 /
 * -4870 / -4860 hold per-frame state. */
static void  jt1009(short a, short b)
{
	short depth;

	PROBE("jt1009");
	if (g_a5_4886 < 4)
		g_a5_4886++;
	depth = g_a5_4886;
	if (depth < 0 || depth > 4)
		return;
	g_a5_4880[depth] = a;
	g_a5_4870[depth] = (short)(b << 2);
	g_a5_4860[depth] = 15;
}

/* JT[977] (CODE 5 + 0x0aaa) — pop paint-stack frame.
 *
 *   if (g_a5_4886 > 0) g_a5_4886--;
 *
 * Matched with JT[1009]; both maintain the deferred-paint nesting
 * depth. */
static void  jt977(void)
{
	PROBE("jt977");
	if (g_a5_4886 > 0)
		g_a5_4886--;
}

/* JT[989] (CODE 5 + 0x1b56) — register handler config.
 *
 * Stores four caller args into a 4-field A5 cluster:
 *
 *   g_a5_4680 = handler         (long ptr)
 *   g_a5_4676 = (low byte) flag (byte)
 *   g_a5_4674 = name            (long, STRS ptr)
 *   g_a5_4670 = (low byte) code (byte)
 *
 * Sets up a callback the engine fires later (probably the
 * key-handler or trap-handler installation). */
static void  jt989(void (*handler)(void), short flag,
                   const char *name, short code)
{
	PROBE("jt989");
	g_a5_4680 = (long)(uintptr_t)handler;
	g_a5_4676 = (unsigned char)(flag & 0xff);
	g_a5_4674 = (long)(uintptr_t)name;
	g_a5_4670 = (unsigned char)(code & 0xff);
}
                                                                                  /* CODE 5 + 0x1b56 */
static void  jt361(short a)                        { PROBE("jt361"); }            /* CODE 8 + 0x71ec */
static void  jt919(void)                           { PROBE("jt919"); }            /* CODE 12 + 0x1b12 */
static int   jt931(void)                           { PROBE("jt931"); return 0; }  /* CODE 12 + 0x430c */
static void  jt949(void)                           { PROBE("jt949"); }            /* CODE 20 + 0x77a2 */
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
/* JT[937] (CODE 12 + 0x02dc, 28 sites) — public alias for L02dc
 * (Modify Character roster grid, lifted further down). */
static void          l02dc(long highlight);
static void          jt937(long a)
{
	PROBE("jt937");
	l02dc(a);
}
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
/* Forward — jt1146 / jt1153 are window-paint commit leaves
 * defined further down. JT[108] needs them. */
static void jt1146(void);
static void jt1153(short arg);

/* JT[108] (CODE 6 + 0x38d0, 30 sites) — mark GrafPort dirty +
 * commit deferred paint.
 *
 *   - Skip if already dirty (g_a5_18395 != 0) or in save-mode 2
 *     (g_a5_18394 == 2).
 *   - When the low byte of `a` is non-zero, flush via JT[1146].
 *   - JT[1153](0) commits the pen state.
 *   - Mark dirty for subsequent skips. */
static int  jt108(short a)
{
	PROBE("jt108");
	if (g_a5_18395 != 0)
		return 0;
	if (g_a5_18394 == 2)
		return 0;
	if ((a & 0xff) != 0)
		jt1146();
	jt1153(0);
	g_a5_18395 = 1;
	return 0;
}
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

/* JT[483] (CODE 3 + 0x0058, 36 sites) — duplicate public alias for
 * strlen-as-short. The Mac body literally JSRs to L39ae and
 * returns; routes to l39ae like JT[423]. */
static short jt483(const char *s) __attribute__((unused));
static short jt483(const char *s)
{
	PROBE("jt483");
	return l39ae(s);
}

/* JT[1083] (CODE 5 + 0x1ae) — linear-congruential PRNG, returns
 * a uniform short in [0, n-1] for n > 1, else 0.
 *
 * Mac asm body, in C:
 *
 *      if (n <= 1) return 0;
 *      divisor = (n + 0x3FFFFFFF) / n;
 *      g_a5_4902 = (g_a5_4902 * 0x6d25) + 1;  // 32-bit wrap
 *      masked   = g_a5_4902 & 0x3FFFFFFF;     // 30-bit
 *      return (short)(masked / divisor);
 *
 * The multiplier 0x6d25 (= 27941) is the engine's chosen LCG
 * constant; combined with the 30-bit mask + the per-roll divisor
 * scaling, output lands in [0, n-1]. State lives in g_a5_4902
 * (long, A5 -4902); seeded by boot_a5_seed_defaults to 1 so the
 * sequence is reproducible across boots until the engine plants
 * a real seed (TickCount-style entropy comes later). */
static short jt1083(short n) __attribute__((unused));
static short jt1083(short n)
{
	unsigned long state;
	unsigned long divisor;

	PROBE("jt1083");
	if (n <= 1)
		return 0;

	state    = (unsigned long)g_a5_4902;
	state    = state * 0x6d25UL + 1UL;
	g_a5_4902 = (long)state;
	divisor  = ((unsigned long)0x3FFFFFFFUL + (unsigned long)n)
	           / (unsigned long)n;
	if (divisor == 0)
		return 0;
	return (short)((state & 0x3FFFFFFFUL) / divisor);
}

/* JT[485] (CODE 3 + 0x0388) — thin wrapper over JT[1083]. Same
 * shape — pure routing. */
static short jt485(short n) __attribute__((unused));
static short jt485(short n)
{
	PROBE("jt485");
	return jt1083(n);
}

/* JT[870] (CODE 18 + 0x15f4, 95 sites) — "count d item" dice roll.
 *
 * Sums `count` rolls of `1..item` using jt485 (which wraps
 * jt1083). The Mac inner loop is the AD&D dice notation primitive
 * the engine uses for damage, saves, treasure, etc.
 *
 *      sum = 0
 *      for i in 1..count:
 *          sum += rand(0..item-1) + 1
 *      return sum
 *
 * With jt1083 still PROBE-stubbed to return 0, this routine
 * returns `count` (every roll is the minimum 1). When the engine's
 * RNG is wired up the dice come alive without further changes.
 *
 * First lifted entry from CODE 18 — opens the segment. */
static short jt870(short count, short item) __attribute__((unused));
static short jt870(short count, short item)
{
	short sum = 0;
	short i;
	short n = (short)(count & 0xff);

	PROBE("jt870");
	if (n <= 0)
		return 0;
	for (i = 1; i <= n; i++)
		sum += (short)(jt485(item) + 1);
	return sum;
}

/* JT[404] (CODE 3 + 0x3976, 34 sites) — strcat. Walks to end of
 * `dst`, then copies bytes from `src` (including terminator).
 * Plain C strcat semantics. */
static void jt404(char *dst, const char *src) __attribute__((unused));
static void jt404(char *dst, const char *src)
{
	PROBE("jt404");
	if (dst == NULL || src == NULL)
		return;
	while (*dst != 0)
		dst++;
	while ((*dst++ = *src++) != 0)
		;
}

/* JT[1163] (CODE 4 + 0x0532, 36 sites) — return 0. The Mac body
 * is literally `moveq #0,d0; rts`. Possibly a "feature disabled"
 * stub the engine queries before invoking some optional path. */
static int jt1163(void) __attribute__((unused));
static int jt1163(void)
{
	PROBE("jt1163");
	return 0;
}

/* JT[1170] (CODE 4 + 0x0536) — empty body (linkw / unlk / rts).
 * Probably a placeholder hook that the build left in. */
static void jt1170(void) __attribute__((unused));
static void jt1170(void)
{
	PROBE("jt1170");
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

/* L2856 (CODE 5 + 0x2856) — font-metric extract. Looks up the
 * sized font for (handle, size), copies an 8-byte metric blob
 * into out_8bytes, and returns a pointer past the blob (or NULL
 * on failure). Mac body validates the font library via L37aa
 * (size >= 0 path) then memcpy's via JT[406].
 *
 * Port stub writes a synthetic 8-byte metric blob so callers
 * (JT[1005] mainly) can produce approximate text rects without
 * a real font system. The blob's layout (m68k big-endian):
 *
 *   [0..1] word  height        — total glyph height
 *   [2..3] word  ascent        — baseline distance below top
 *   [4..5] word  x_bearing     — left-bearing offset
 *   [6]    byte  width_index   — char width (shifted by mode in
 *                                JT[1005] to get pixel extent)
 *   [7]    byte  pad
 *
 * Once a real font system lands, swap this for the actual
 * lookup; JT[1005]'s arithmetic stays the same. */
static long l2856(long font_handle, short size, void *out_8bytes)
                                                __attribute__((unused));
static long l2856(long font_handle, short size, void *out_8bytes)
{
	static const unsigned char fake_metrics[8] = {
		0x00, 0x0e,   /* height      = 14 */
		0x00, 0x0c,   /* ascent      = 12 */
		0x00, 0x00,   /* x_bearing   =  0 */
		0x06, 0x00    /* width_index =  6 (→ 48-pixel char box after lslw #3) */
	};

	PROBE("L2856");
	(void)font_handle; (void)size;
	if (out_8bytes == NULL)
		return 0;
	jt406(out_8bytes, fake_metrics, 8);
	/* Real return is (font_ptr + 8); we just need non-NULL so
	 * the caller continues into bounds computation. */
	return (long)(uintptr_t)(fake_metrics + 8);
}

/* JT[1005] (CODE 5 + 0x31fc) — text-style bounding rect. Given
 * (x, y, style, size), look up font metrics and write the
 * (top, left, bottom, right) of the rendered glyph extent.
 *
 * Flow (lifted from L31fc):
 *   1. font_handle = JT[468](style)
 *   2. ptr = L2856(font_handle, size, &fontinfo[8])
 *      — if ptr == NULL the rect stays whatever the caller had,
 *        in our port that's the caller's zero-init.
 *   3. (x, y) = JT[1135](x, y) — scale design coords to screen
 *   4. y -= ascent, x -= x_bearing
 *   5. right_x = x + width_index << (display-mode shift via
 *      JT[1200]: 3 in mode 0, mode-value in modes 1/3)
 *   6. bottom_y = y + height
 *   7. Clip the four edges to (g_a5_3054..3050) (top..bottom)
 *      and (g_a5_3056..3052) (left..right) via JT[397] (max)
 *      and JT[413] (min).
 *   8. If the clipped rect is degenerate (top>=bottom or
 *      left>=right) zero all four outputs. */
static void jt1005(short x, short y, short style, short size,
                   short *out_top, short *out_left,
                   short *out_bottom, short *out_right)
                                                __attribute__((unused));
static void jt1005(short x, short y, short style, short size,
                   short *out_top, short *out_left,
                   short *out_bottom, short *out_right)
{
	long           font_handle;
	unsigned char  fontinfo[8];
	long           ptr;
	short          sx, sy;
	short          height, ascent, x_bearing;
	unsigned char  width_index;
	short          right_x, bottom_y;
	short          mode;

	PROBE("jt1005");
	if (out_top == NULL || out_left == NULL ||
	    out_bottom == NULL || out_right == NULL)
		return;

	font_handle = jt468(style);
	ptr = l2856(font_handle, size, fontinfo);
	if (ptr == 0)
		return;

	/* Scale design coords → screen coords. */
	sx = x; sy = y;
	jt1135(sx, sy, &sx, &sy);

	height      = *(short *)(fontinfo + 0);
	ascent      = *(short *)(fontinfo + 2);
	x_bearing   = *(short *)(fontinfo + 4);
	width_index = fontinfo[6];

	/* (sx, sy) is the post-scale glyph anchor. Mac's first arg
	 * (rec[16]) is the Y coord (Point (v, h) order); jt1135
	 * preserves that mapping. So sx here holds Y, sy holds X. */
	sx -= ascent;
	sy -= x_bearing;
	bottom_y = sx + height;

	mode = (short)jt1200();
	if (mode == 0)
		right_x = (short)(sy + ((short)width_index << 3));
	else
		right_x = (short)(sy + ((short)width_index << mode));

	*out_top    = jt397(sx,       g_a5_3054);
	*out_left   = jt397(sy,       g_a5_3056);
	*out_bottom = jt413(bottom_y, g_a5_3050);
	*out_right  = jt413(right_x,  g_a5_3052);

	/* Validate: degenerate rect → zero all four outs. */
	if (*out_top >= *out_bottom || *out_left >= *out_right) {
		*out_top    = 0;
		*out_left   = 0;
		*out_bottom = 0;
		*out_right  = 0;
	}
}

/* JT[1139] (CODE 4 + 0x785c) — grid-coord hit-test. Given an item
 * origin (rec[16], rec[18]) and a click (mouse_y, mouse_x),
 * returns (row, col) into the two out-shorts. jt378 cmd=2 uses
 * this to map a screen click into a grid cell; callers then
 * bounds-check the row against rec[22] and col against rec[24].
 * PROBE stub returns row=col=0 so jt378's hit-test cleanly
 * accepts (0,0) as a valid cell — fine until real grid rendering
 * arrives. */
static void jt1139(short origin_y, short origin_x,
                   short click_y, short click_x,
                   short *out_row, short *out_col)
                                                __attribute__((unused));
static void jt1139(short origin_y, short origin_x,
                   short click_y, short click_x,
                   short *out_row, short *out_col)
{
	PROBE("jt1139");
	(void)origin_y; (void)origin_x;
	(void)click_y;  (void)click_x;
	if (out_row != NULL) *out_row = 0;
	if (out_col != NULL) *out_col = 0;
}

/* JT[1141] (CODE 4 + 0x78e8) — rect-from-corner-plus-extent. Given
 * a top-left (top, left), a height (h) and a width (w), write the
 * bottom-right corner through the two out-pointers. PROBE stub
 * leaves the outputs untouched; jt382 path-2 fills the rect via
 * a prior JT[1135] and this call writes the other corner, so
 * leaving it at the caller-init zero keeps the hit-test missing. */
static void jt1141(short top, short left, short h, short w,
                   short *out_bottom, short *out_right)
                                                __attribute__((unused));
static void jt1141(short top, short left, short h, short w,
                   short *out_bottom, short *out_right)
{
	PROBE("jt1141");
	(void)top; (void)left; (void)h; (void)w;
	(void)out_bottom; (void)out_right;
}

/* L3994 (CODE 6 + 0x3994) — GrafPort save / paint setup. Reads
 * g_a5_18394 / 18393 / 18395 state flags, conditionally invokes
 * JT[468] / JT[1012] / JT[406] (memcpy) for the backing-store
 * snapshot and JT[1128] / JT[1153] for clip restore. PROBE for
 * now — the snapshot machinery wires once the QuickDraw shim
 * publishes GrafPort state. */
/* JT[1012] / JT[1128] / JT[1066] are paint-system leaves L3994
 * reaches. PROBE for now. */
static long jt1012(long handle, short item) __attribute__((unused));
static long jt1012(long handle, short item)
                                            { PROBE("jt1012"); (void)handle;
                                              (void)item; return 0; }
static void jt1128(void)                    { PROBE("jt1128"); }
static void jt1066(void)                    { PROBE("jt1066"); }

/* L3994 — GrafPort save / paint-state commit. Called by JT[94] /
 * JT[112] / JT[117]; the chain is:
 *
 *   1. If save-mode flag g_a5_18394 != 0, skip everything
 *      (we're in a "no save" mode like style-2 paint).
 *   2. If a snapshot is pending (g_a5_18393 != 0 && a resource
 *      handle is parked in g_a5_18392): would copy 96 bytes from
 *      the caller's stack into the resource via JT[406]. The
 *      caller-stack source is the suspicious part — fp[-96] is a
 *      LOCAL buffer in *this* function, allocated by `linkw fp,
 *      #-96` with no initialiser. Replicating the Mac copy would
 *      write uninitialised bytes to the handle. PROBE the branch
 *      instead until that detail's understood; the dirty-flush
 *      below runs regardless.
 *   3. If the paint state is dirty (g_a5_18395 != 0) — JT[108]
 *      sets this — commit deferred paint via JT[1128] + JT[1153](1)
 *      and clear dirty.
 *   4. If we're inside an encounter (jt1200() != 0; jt1163() also
 *      gates), call JT[1066] for whatever the cleanup is.
 *   5. Clear the snapshot-pending flag g_a5_18393.
 *
 * Used by every text-paint + every JT[94] caller — wiring the
 * dirty flush makes the JT[108] → L3994 → JT[1153] chain actually
 * commit pen state at the right time. */
static void l3994(void)
{
	PROBE("l3994");
	if (g_a5_18394 != 0)
		return;

	if (g_a5_18393 != 0 && g_a5_18392 != 0) {
		PROBE("l3994/snapshot-skipped");
		/* Mac: jt406(jt1012(jt468(0), 0) + 8, &local_96, 96)
		 * Writes 96 bytes from an uninitialised local into the
		 * resource handle. PROBE-deferred. */
	}

	if (g_a5_18395 != 0) {
		jt1128();
		jt1153(1);
		g_a5_18395 = 0;
	}

	if (jt1163() != 0 || jt1200() != 0)
		jt1066();

	g_a5_18393 = 0;
}

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
/* JT[447] (CODE 3 + 0x298a) — DLItem manager state reset.
 *
 *   g_a5_9254 = g_a5_9286;     // pool base ← seeded base
 *   g_a5_9250 = 0;              // count = 0
 *   g_a5_9248 = 1;              // manager active flag
 *   g_a5_9247 = 0;              // selection-locked flag
 *
 * Called at the start of each menu / dialog open to wipe the
 * DLItem state and reseat the pool pointer. Pairs with JT[452]
 * (item installer) and JT[453] (run dialog). */
static void    jt447(void)
{
	PROBE("jt447");
	g_a5_9254 = g_a5_9286;
	g_a5_9250 = 0;
	g_a5_9248 = 1;
	g_a5_9247 = 0;
}
static void    jt449(short a)                       { PROBE("jt449"); (void)a; }
static void    jt451(void)                          { PROBE("jt451"); }

/* JT[376] .. JT[382] (CODE 3) — the 7 DLItem shape-method handlers
 * JT[442] parks in the g_a5_9282 table:
 *
 *   shape 1 → JT[382] (CODE 3 + 0x1a3c)  — likely "button"
 *   shape 2 → JT[381] (CODE 3 + 0x20c4)  — likely "checkbox/radio"
 *   shape 3 → JT[380] (CODE 3 + 0x224c)
 *   shape 4 → JT[379] (CODE 3 + 0x2438)
 *   shape 5 → JT[378] (CODE 3 + 0x25a2)
 *   shape 6 → JT[377] (CODE 3 + 0x27c8)
 *   shape 7 → JT[376] (CODE 3 + 0x2862)
 *
 * Each handler is the method-dispatch for items of its shape — it
 * gets invoked by L2d3e with (rec, cmd, ...) where cmd is one of
 * 1 / 2 (hit test) / 3 / 4 (action) / 5 (selection match) / 19 /
 * 27. Bodies are sizeable (each has its own JT[3] case-table over
 * cmd). PROBE stubs for now — once L2d3e calls them on a click,
 * we'll see which cmd codes are used and can lift on demand. */
/* Per-shape, per-cmd PROBE. Each handler dispatches by cmd so the
 * trace shows which cmd codes L2d3e is actually sending — driving
 * which arms get lifted first. cmd is one of 1..5 (hit/action/sel),
 * plus 16 / 24 (flag-bit set/clear) and the less-frequent 19 / 27.
 * Unknown cmds fall through the default and log the actual value
 * via dbg_log_num so new arms can be added as discovered.
 * No-op (compiled out) when FRUA_ENGINE_PROBE is off. */
#ifdef FRUA_ENGINE_PROBE
#  define SHAPE_CMD_PROBE(jt)                                            \
	switch (cmd) {                                                       \
	case  1: PROBE(jt ":cmd=1");  break;                                 \
	case  2: PROBE(jt ":cmd=2");  break;                                 \
	case  3: PROBE(jt ":cmd=3");  break;                                 \
	case  4: PROBE(jt ":cmd=4");  break;                                 \
	case  5: PROBE(jt ":cmd=5");  break;                                 \
	case 16: PROBE(jt ":cmd=16"); break;                                 \
	case 19: PROBE(jt ":cmd=19"); break;                                 \
	case 24: PROBE(jt ":cmd=24"); break;                                 \
	case 27: PROBE(jt ":cmd=27"); break;                                 \
	default: dbg_log_num("stub: " jt ":cmd=", (long)cmd); break;         \
	}
#else
#  define SHAPE_CMD_PROBE(jt) ((void)0)
#endif

/* Forward — jt1134 (paint flush) lifts further down. L1676's
 * cmd=3 mouse-track loop needs it. */
static long jt1134(void);

/* JT[1132] (CODE 4 + 0x6288) — mouse poll. Writes the current
 * mouse (y, x) to the two out-shorts and returns the button
 * state: 1 if the button is held, 0 if released. The Mac body
 * checks two cached A5 flags (g_a5_-903 / -904) for a buffered
 * button-down, falling through to L6204 (live mouse poll) when
 * the cache is empty.
 *
 * Port stub: no mouse-event source is wired yet, so return 0
 * (button released) with zeroed coords. This makes L1676's
 * cmd=3 mouse-track loop exit cleanly on the first iteration.
 * Replace with a real poll once Hatari's mouse / keyboard input
 * reaches the engine via the input HAL. */
static short jt1132(short *out_y, short *out_x) __attribute__((unused));
static short jt1132(short *out_y, short *out_x)
{
	PROBE("jt1132");
	if (out_y != NULL) *out_y = 0;
	if (out_x != NULL) *out_x = 0;
	return 0;
}

/* L31b8 (CODE 3 + 0x31b8) — thin wrapper over JT[1132]. */
static short l31b8(short *out_y, short *out_x) __attribute__((unused));
static short l31b8(short *out_y, short *out_x)
{
	PROBE("L31b8");
	return jt1132(out_y, out_x);
}

/* L13e8 (CODE 3 + 0x13e8, CODE-local) — keyboard shortcut matcher.
 *
 *   short L13e8(short shortcut, short key);
 *
 * Body:
 *   if (shortcut == 0)   return 0;                        // no shortcut
 *   if (shortcut < 32)   return key == (shortcut+256) ?   // function key
 *                        -1 : 0;
 *   // ASCII (shortcut >= 32):
 *   d0 = shortcut;  JT[1]-dispatch d0:                    // (deferred)
 *     few special chars            → return 0;
 *     few special chars            → return 1;
 *     Return (13) / LF (10) match  → return 1;
 *     default → return tolower(shortcut) == tolower(key) ? -1 : 0;
 *
 * The JT[1] inline dispatch handles a handful of Mac-specific
 * shortcut codes (Cmd-Q etc.). Deferred. The default ASCII compare
 * is lifted faithfully and matches case-insensitive — covers the
 * common printable-key path used by the editor / dialogs. */
static int l13e8(short shortcut, short key) __attribute__((unused));
static int l13e8(short shortcut, short key)
{
	PROBE("L13e8");
	if (shortcut == 0)
		return 0;
	if (shortcut < 32)
		return (key == (short)(shortcut + 256)) ? -1 : 0;
	return (l46b2(shortcut) == l46b2(key)) ? -1 : 0;
}

/* L1676 (CODE 3 + 0x1676) — base DLItem method handler. Every
 * shape handler (jt376..jt382) delegates un-recognized cmds here
 * via its JT[3] default arm. Lifted from L1676's own JT[3] (min=1
 * max=44), arms grouped by behavior:
 *
 *   1            set bit 7 of rec[28] ("dirty / needs redraw")
 *   2            no-op (L1a36 = return 0)
 *   3, 4, 5      complex action / track / select arms — PROBE-
 *                only for now; not exercised in the boot trace
 *   6..15        no-op
 *   16..22       set bit (cmd-16) of rec[28]; if changed, clear
 *                bit 7
 *   23           no-op
 *   24..30       clear bit (cmd-24) of rec[28]; if changed, clear
 *                bit 7
 *   31           no-op
 *   32..44       per-field setters (rec[29], rec[4..7], rec[12..],
 *                etc.) — most take a long arg the current callers
 *                don't push; PROBE-only until a caller exercises
 *                them
 *   default      no-op
 *
 * Arm 43 reads `g_a5_9282[arg-1]` and parks it at rec[0..3] — a
 * runtime "set method to handler N" op. Arm 44 sets rec[0..3]
 * directly. Both PROBE-only for now.
 *
 * Bit 7 of rec[28] is the "dirty" flag L2d3e consults after the
 * action methods run; lifting cmd=1 here is what closes the
 * draw/ack cycle for any item that doesn't override cmd=1. */
static short l1676(unsigned char *rec, short cmd, ...)
                                                __attribute__((unused));
static short l1676(unsigned char *rec, short cmd, ...)
{
	PROBE("L1676");
	if (rec == NULL)
		return 0;

	switch (cmd) {
	case 1:
		rec[28] |= 0x80;
		return 0;
	case 16: case 17: case 18: case 19:
	case 20: case 21: case 22: {
		unsigned char before = rec[28];
		rec[28] |= (unsigned char)(1u << (cmd - 16));
		if (rec[28] != before)
			rec[28] &= 0x7f;
		return 0;
	}
	case 24: case 25: case 26: case 27:
	case 28: case 29: case 30: {
		unsigned char before = rec[28];
		rec[28] &= (unsigned char)~(1u << (cmd - 24));
		if (rec[28] != before)
			rec[28] &= 0x7f;
		return 0;
	}
	case 3: {
		/* Mouse-track loop (L16f4..L17fe).
		 *
		 *   Mac initial sequence:
		 *     method(rec, 19)   ; cmd=19 → set bit 3 (highlight)
		 *     method(rec,  1)   ; cmd=1  → set bit 7 (dirty)
		 *     JT[1134]          ; paint flush
		 *     method(rec,  7)   ; cmd=7  → shape-specific press hook
		 *
		 *   Loop while button held:
		 *     l31b8(&y, &x)     ; current mouse + button state
		 *     cur_hit = (method(rec, 2, y, x) != 0) ? 1 : 0
		 *     if cur_hit != prev_hit:
		 *         prev_hit = cur_hit
		 *         method(rec, prev_hit ? 19 : 27)   ; (un)highlight
		 *         method(rec, 1)                     ; dirty
		 *         JT[1134]                            ; flush
		 *     if prev_hit && !button_held:
		 *         method(rec, 7)                     ; release-on-hit hook
		 *
		 *   Final:
		 *     method(rec, 27)   ; cmd=27 → clear bit 3 (unhighlight)
		 *     return prev_hit   ; 1 if click landed, 0 if dragged off
		 *
		 * With l31b8 currently stubbed to "button released", the loop
		 * runs once and exits. Once Hatari mouse events reach the
		 * engine, this loop tracks live cursor drift and toggles the
		 * highlight as the user drags. */
		short (*method)(void *, short, ...);
		signed char prev_hit = 1;
		signed char cur_hit;
		short        button_held;
		short        mouse_y = 0;
		short        mouse_x = 0;

		PROBE("L1676:cmd=3-track");
		method = *(short (**)(void *, short, ...))rec;
		if (method == NULL)
			return 0;

		method(rec, (short)19);
		method(rec, (short) 1);
		jt1134();
		method(rec, (short) 7);

		do {
			button_held = l31b8(&mouse_y, &mouse_x);
			cur_hit = (method(rec, (short)2, mouse_y, mouse_x) != 0)
			        ? (signed char)1 : (signed char)0;

			if (cur_hit != prev_hit) {
				prev_hit = cur_hit;
				method(rec, (short)(prev_hit ? 19 : 27));
				method(rec, (short)1);
				jt1134();
			}

			if (prev_hit != 0 && button_held == 0)
				method(rec, (short)7);
		} while (button_held != 0);

		method(rec, (short)27);
		return (short)prev_hit;
	}
	case 4: {
		/* L185a — action arm. Fires after cmd=3's mouse-track loop
		 * returns 1 (click confirmed). If the DLItem carries an
		 * action callback at rec[4..7], invoke it with the item's
		 * pool index (computed as (rec - g_a5_-9254) / 32). The
		 * Mac uses Pascal calling: one 16-bit arg, no return. */
		void  (*action)(short);
		long   pool_base = g_a5_9254;
		short  item_index;

		PROBE("L1676:cmd=4-action");
		action = *(void (**)(short))((unsigned char *)rec + 4);
		if (action == NULL)
			return 0;
		if (pool_base == 0)
			return 0;
		item_index = (short)(((long)(uintptr_t)rec - pool_base)
		                     / (long)DLITEM_BYTES);
		action(item_index);
		return 0;
	}
	case 5: {
		/* L1802 — keyboard select arm. Fires from L2d3e Phase 5
		 * (Return-key path) walking DLItems to find one whose
		 * shortcut matches the pressed key.
		 *
		 *   if (rec[28] & 0x03)                    // disabled / special
		 *       return 0;
		 *   if (L13e8(rec[29], key))   return 1;   // primary shortcut
		 *   if (L13e8(rec[30], key))   return 1;   // secondary shortcut
		 *   return 0;
		 *
		 * rec[29] and rec[30] are 8-bit shortcut codes seeded by
		 * cmd=32 / cmd=33 (see further down). L13e8 is a partial
		 * lift — handles the "no shortcut configured" path; the
		 * ASCII / function-key dispatch is deferred. */
		short key;
		va_list ap;
		unsigned char *rec_b = (unsigned char *)rec;

		PROBE("L1676:cmd=5-select");
		if ((rec_b[28] & 0x03) != 0)
			return 0;
		va_start(ap, cmd);
		key = (short)va_arg(ap, int);
		va_end(ap);
		if (l13e8((short)(signed char)rec_b[29], key) != 0)
			return 1;
		if (l13e8((short)(signed char)rec_b[30], key) != 0)
			return 1;
		return 0;
	}
	/* cmd 32..44 — DLItem field setters. Each reads one variadic
	 * arg and stamps it into a specific rec[N] offset. Several
	 * arms (37 / 38 / 39 / 40) also clear bit 7 of rec[28] when
	 * the value actually changed — the engine's "redraw on change"
	 * signal. Field map matches the jt452 stream-installer.
	 *
	 * Variadic arg widths:
	 *   short  → cmd 32, 33 (low byte stored), 36, 37, 38, 41, 42
	 *   2 shorts → cmd 40 (position pair)
	 *   long   → cmd 34, 35, 39, 43, 44
	 *
	 * The Mac uses fp@(14) for the first variadic arg (short or
	 * long); fp@(15) is the low byte when only the byte matters. */
	case 32: {
		va_list ap;
		unsigned char *r = (unsigned char *)rec;
		PROBE("L1676:cmd=32-set29");
		va_start(ap, cmd);
		r[29] = (unsigned char)(va_arg(ap, int) & 0xff);
		va_end(ap);
		return 0;
	}
	case 33: {
		va_list ap;
		unsigned char *r = (unsigned char *)rec;
		PROBE("L1676:cmd=33-set30");
		va_start(ap, cmd);
		r[30] = (unsigned char)(va_arg(ap, int) & 0xff);
		va_end(ap);
		return 0;
	}
	case 34: {
		va_list ap;
		unsigned char *r = (unsigned char *)rec;
		PROBE("L1676:cmd=34-set4");
		va_start(ap, cmd);
		*(long *)(r + 4) = va_arg(ap, long);
		va_end(ap);
		return 0;
	}
	case 35: {
		va_list ap;
		unsigned char *r = (unsigned char *)rec;
		PROBE("L1676:cmd=35-set8");
		va_start(ap, cmd);
		*(long *)(r + 8) = va_arg(ap, long);
		va_end(ap);
		return 0;
	}
	case 36: {
		va_list ap;
		unsigned char *r = (unsigned char *)rec;
		PROBE("L1676:cmd=36-set24");
		va_start(ap, cmd);
		*(short *)(r + 24) = (short)va_arg(ap, int);
		va_end(ap);
		return 0;
	}
	case 37: {
		va_list ap;
		unsigned char *r = (unsigned char *)rec;
		short new_val;
		PROBE("L1676:cmd=37-set26");
		va_start(ap, cmd);
		new_val = (short)va_arg(ap, int);
		va_end(ap);
		if (*(short *)(r + 26) == new_val)
			return 0;
		*(short *)(r + 26) = new_val;
		r[28] &= 0x7f;
		return 0;
	}
	case 38: {
		va_list ap;
		unsigned char *r = (unsigned char *)rec;
		short new_val;
		PROBE("L1676:cmd=38-set31");
		va_start(ap, cmd);
		new_val = (short)va_arg(ap, int);
		va_end(ap);
		if ((short)r[31] == new_val)
			return 0;
		r[31] = (unsigned char)(new_val & 0xff);
		r[28] &= 0x7f;
		return 0;
	}
	case 39: {
		va_list ap;
		unsigned char *r = (unsigned char *)rec;
		long new_val;
		PROBE("L1676:cmd=39-set12");
		va_start(ap, cmd);
		new_val = va_arg(ap, long);
		va_end(ap);
		if (*(long *)(r + 12) == new_val)
			return 0;
		*(long *)(r + 12) = new_val;
		r[28] &= 0x7f;
		return 0;
	}
	case 40: {
		va_list ap;
		unsigned char *r = (unsigned char *)rec;
		short a, b;
		PROBE("L1676:cmd=40-setpos");
		va_start(ap, cmd);
		a = (short)va_arg(ap, int);
		b = (short)va_arg(ap, int);
		va_end(ap);
		if (*(short *)(r + 16) == a && *(short *)(r + 18) == b)
			return 0;
		*(short *)(r + 16) = a;
		*(short *)(r + 18) = b;
		r[28] &= 0x7f;
		return 0;
	}
	case 41: {
		va_list ap;
		unsigned char *r = (unsigned char *)rec;
		PROBE("L1676:cmd=41-set20");
		va_start(ap, cmd);
		*(short *)(r + 20) = (short)va_arg(ap, int);
		va_end(ap);
		return 0;
	}
	case 42: {
		va_list ap;
		unsigned char *r = (unsigned char *)rec;
		PROBE("L1676:cmd=42-set22");
		va_start(ap, cmd);
		*(short *)(r + 22) = (short)va_arg(ap, int);
		va_end(ap);
		return 0;
	}
	case 43: {
		/* cmd=43 — load method ptr from the shape table by index.
		 * Mac: d0 = (short_arg - 1) * 4; rec[0..3] = g_a5_-9282[d0]. */
		va_list ap;
		unsigned char *r = (unsigned char *)rec;
		short idx;
		long *table;
		PROBE("L1676:cmd=43-setmth");
		va_start(ap, cmd);
		idx = (short)va_arg(ap, int);
		va_end(ap);
		table = (long *)g_a5_buf(-9282);
		*(long *)r = table[idx - 1];
		return 0;
	}
	case 44: {
		/* cmd=44 — raw method ptr write (caller-supplied). */
		va_list ap;
		unsigned char *r = (unsigned char *)rec;
		PROBE("L1676:cmd=44-setraw");
		va_start(ap, cmd);
		*(long *)r = va_arg(ap, long);
		va_end(ap);
		return 0;
	}
	default: break;
	}
	return 0;
}

static short jt376(void *rec_v, short cmd, ...) __attribute__((unused));
static short jt376(void *rec_v, short cmd, ...)
{
	unsigned char *rec = (unsigned char *)rec_v;
	va_list ap;
	short a, b;

	PROBE("jt376");
	SHAPE_CMD_PROBE("jt376");

	va_start(ap, cmd);
	a = (short)va_arg(ap, int);
	b = (short)va_arg(ap, int);
	va_end(ap);
	return l1676(rec, cmd, a, b);
}
static short jt377(void *rec_v, short cmd, ...) __attribute__((unused));
static short jt377(void *rec_v, short cmd, ...)
{
	unsigned char *rec = (unsigned char *)rec_v;
	va_list ap;
	short a, b;

	PROBE("jt377");
	SHAPE_CMD_PROBE("jt377");

	va_start(ap, cmd);
	a = (short)va_arg(ap, int);
	b = (short)va_arg(ap, int);
	va_end(ap);
	return l1676(rec, cmd, a, b);
}
/* jt378 — shape 5 method dispatcher. cmd=2 is a grid hit-test
 * (lifted from L25ba): JT[1139] maps the click into a grid cell,
 * the cell is bounds-checked against rec[22] (max row) and rec[24]
 * (max col), and bit 6 of rec[28] optionally gates the result on
 * a per-item callback at rec[4]. JT[1139] is a PROBE stub; until
 * it's filled in, every in-range click maps to cell (0, 0). */
static short jt378(void *rec_v, short cmd, ...) __attribute__((unused));
static short jt378(void *rec_v, short cmd, ...)
{
	unsigned char *rec = (unsigned char *)rec_v;
	va_list ap;
	short y, x;
	short row = 0, col = 0;

	PROBE("jt378");
	SHAPE_CMD_PROBE("jt378");

	if (cmd != 2) {
		short la, lb;
		va_start(ap, cmd);
		la = (short)va_arg(ap, int);
		lb = (short)va_arg(ap, int);
		va_end(ap);
		return l1676(rec, cmd, la, lb);
	}

	if ((rec[28] & 0x03) != 0)
		return 0;

	va_start(ap, cmd);
	y = (short)va_arg(ap, int);
	x = (short)va_arg(ap, int);
	va_end(ap);

	jt1139(*(short *)(rec + 16), *(short *)(rec + 18),
	       y, x, &row, &col);

	if (row < 0 || row >= *(short *)(rec + 22)) return 0;
	if (col < 0 || col >= *(short *)(rec + 24)) return 0;

	/* Bit 6: extra callback gate. Callback at rec[4] is invoked
	 * with (-2, y, x); a zero return means "not really a hit". */
	if ((rec[28] & 0x40) != 0) {
		short (*cb)(short, short, short) =
			*(short (**)(short, short, short))(rec + 4);
		if (cb != NULL && cb((short)-2, y, x) == 0)
			return 0;
	}
	return 1;
}

static short jt379(void *rec_v, short cmd, ...) __attribute__((unused));
static short jt379(void *rec_v, short cmd, ...)
{
	unsigned char *rec = (unsigned char *)rec_v;
	va_list ap;
	short a, b;

	PROBE("jt379");
	SHAPE_CMD_PROBE("jt379");

	va_start(ap, cmd);
	a = (short)va_arg(ap, int);
	b = (short)va_arg(ap, int);
	va_end(ap);
	return l1676(rec, cmd, a, b);
}

/* jt380 — shape 3 method dispatcher. cmd=2 has a primary text-
 * bounds hit-test like jt382's, plus a secondary fallback when
 * the primary misses (lifted from L2278). The fallback measures
 * a rectangle whose width is derived from the label's string
 * length plus rec[22] (extra columns) — used by items that have
 * a clickable region wider than their visible glyph extent. */
static short jt380(void *rec_v, short cmd, ...) __attribute__((unused));
static short jt380(void *rec_v, short cmd, ...)
{
	unsigned char *rec = (unsigned char *)rec_v;
	va_list ap;
	short y, x, n, total_w;
	short style_size;
	short top = 0, left = 0, bottom = 0, right = 0;

	PROBE("jt380");
	SHAPE_CMD_PROBE("jt380");

	if (cmd != 2) {
		short la, lb;
		va_start(ap, cmd);
		la = (short)va_arg(ap, int);
		lb = (short)va_arg(ap, int);
		va_end(ap);
		return l1676(rec, cmd, la, lb);
	}

	if ((rec[28] & 0x03) != 0)
		return 0;

	va_start(ap, cmd);
	y = (short)va_arg(ap, int);
	x = (short)va_arg(ap, int);
	va_end(ap);

	style_size = *(short *)(rec + 26);

	/* Primary: text-style bounds via JT[1005] (only if rec[26]
	 * is non-zero; rec[26]==0 skips straight to the fallback). */
	if (style_size != 0) {
		short style = (short)(style_size >> 10);
		short size  = (short)(style_size & 0x03ff);

		jt1005(*(short *)(rec + 16), *(short *)(rec + 18),
		       style, size,
		       &top, &left, &bottom, &right);
		if (top  <= y && y < bottom &&
		    left <= x && x < right)
			return 1;
	}

	/* Fallback: scaled origin + string-length-derived rect. */
	jt1135(*(short *)(rec + 16), *(short *)(rec + 18),
	       &top, &left);
	if (y < top || x < left)
		return 0;

	n = l39ae(*(const char **)(rec + 12));
	total_w = (short)((n + *(short *)(rec + 22)) * 4 + 8008);
	jt1141(top, left, 8004, total_w, &bottom, &right);

	if (y >= bottom) return 0;
	if (x <  right ) return 1;
	return 0;
}
static short jt381(void *rec_v, short cmd, ...) __attribute__((unused));
static short jt381(void *rec_v, short cmd, ...)
{
	unsigned char *rec = (unsigned char *)rec_v;
	va_list ap;
	short a, b;

	PROBE("jt381");
	SHAPE_CMD_PROBE("jt381");

	va_start(ap, cmd);
	a = (short)va_arg(ap, int);
	b = (short)va_arg(ap, int);
	va_end(ap);
	return l1676(rec, cmd, a, b);
}
/* jt382 — shape 1 (button) method dispatcher. cmd=2 hit-test is
 * the dominant boot-path call (90 of 92 per boot). The hit-test
 * arm is lifted from L1e82..L200a in CODE 3:
 *
 *   1. If rec[28] & 3 is set (item disabled), return 0.
 *   2. style_size = *(short *)(rec+26) picks the bounds path:
 *        - style_size >  0  → text item, JT[1005] writes the rect
 *        - style_size == 0  → default-sized text, JT[1005] with
 *                             style=0, size=14
 *        - style_size == -1 → bitmap; JT[1135] for top-left,
 *                             JT[1141] for bottom-right via the
 *                             string length at rec[12]
 *        - style_size < -1  → custom-positioned; two JT[1135]
 *                             calls, one per corner, using
 *                             rec[24] (row count) for the height
 *   3. Hit if (top..bottom) × (left..right) contains (y, x).
 *
 * JT[1005] / JT[1141] are PROBE stubs (font system not wired);
 * they leave the rect zeroed so the hit-test cleanly misses
 * text/bitmap items until the font path lands. The style_size<-1
 * (custom-position) path uses only the already-lifted JT[1135],
 * so its hit-test is real today — that's the arm that will fire
 * once L2d3e gets coordinates from a real click. */
static short jt382(void *rec_v, short cmd, ...) __attribute__((unused));
static short jt382(void *rec_v, short cmd, ...)
{
	unsigned char *rec = (unsigned char *)rec_v;
	va_list ap;
	short y, x;
	short style_size;
	short top = 0, left = 0, bottom = 0, right = 0;

	PROBE("jt382");
	SHAPE_CMD_PROBE("jt382");

	if (cmd != 2) {
		short la, lb;
		va_start(ap, cmd);
		la = (short)va_arg(ap, int);
		lb = (short)va_arg(ap, int);
		va_end(ap);
		return l1676(rec, cmd, la, lb);
	}

	/* Bit 0/1 of rec[28] are the "disabled" flags. */
	if ((rec[28] & 0x03) != 0)
		return 0;

	va_start(ap, cmd);
	y = (short)va_arg(ap, int);
	x = (short)va_arg(ap, int);
	va_end(ap);

	style_size = *(short *)(rec + 26);

	if (style_size >= 0) {
		short style, size;

		if (style_size == 0) {
			style = 0;
			size  = 14;
		} else {
			style = (short)(style_size >> 10);
			size  = (short)(style_size & 0x03ff);
		}
		jt1005(*(short *)(rec + 16), *(short *)(rec + 18),
		       style, size,
		       &top, &left, &bottom, &right);
	} else if (style_size == -1) {
		short n;

		/* jt1135 writes out1=scaled(v1)=scaled(rec[16]) which
		 * is the Y/top coord (Mac uses Point (v, h) order), and
		 * out2=scaled(rec[18]) which is X/left. Pass &top, &left
		 * in that order so the variables end up named correctly. */
		jt1135(*(short *)(rec + 16), *(short *)(rec + 18),
		       &top, &left);
		n = l39ae(*(const char **)(rec + 12));
		jt1141(top, left, 8004, (short)(n * 4 + 8000),
		       &bottom, &right);
	} else {
		jt1135((short)(*(short *)(rec + 16) - 1),
		       (short)(*(short *)(rec + 18) - 2),
		       &top, &left);
		jt1135((short)(*(short *)(rec + 16) + 5),
		       (short)(*(short *)(rec + 18) +
		               *(short *)(rec + 24) * 4 + 2),
		       &bottom, &right);
	}

	if (top  >  y || y >= bottom) return 0;
	if (left >  x || x >= right ) return 0;
	return 1;
}

/* Forward — g_dlitem_pool lives in the DLItem cluster further
 * down. JT[442] needs its address. */
extern unsigned char g_dlitem_pool[DLITEM_MAX * DLITEM_BYTES];

/* JT[442] (CODE 3 + 0x28d0) — DLInit. Called when opening a new
 * dialog with `max_items` capacity. Allocates the DLItem pool
 * (port uses the static g_dlitem_pool instead), then populates
 * the 7-entry method-handler table at g_a5_9282 so that JT[452]
 * (the shape-code dispatch) can park the right method pointer in
 * each DLItem record.
 *
 *   1. g_a5_9288 = max_items
 *   2. g_a5_9286 = pool base (port: address of g_dlitem_pool)
 *   3. g_a5_9247 = g_a5_9248 = 0
 *   4. g_a5_9282[0..6] = JT[382] / JT[381] / ... / JT[376]
 *   5. g_a5_9246 = 0  (extra clear)
 *
 * The port skips NewPtr — g_dlitem_pool is a heap-equivalent
 * static buffer. Aside from that, the lift is faithful. */
static void jt442(short max_items) __attribute__((unused));
static void jt442(short max_items)
{
	long *table;

	PROBE("jt442");
	g_a5_9288 = max_items;
	g_a5_9286 = (long)(uintptr_t)g_dlitem_pool;
	g_a5_9247 = 0;
	g_a5_9248 = 0;

	table = (long *)g_a5_buf(-9282);
	table[0] = (long)(uintptr_t)jt382;     /* shape 1 */
	table[1] = (long)(uintptr_t)jt381;     /* shape 2 */
	table[2] = (long)(uintptr_t)jt380;     /* shape 3 */
	table[3] = (long)(uintptr_t)jt379;     /* shape 4 */
	table[4] = (long)(uintptr_t)jt378;     /* shape 5 */
	table[5] = (long)(uintptr_t)jt377;     /* shape 6 */
	table[6] = (long)(uintptr_t)jt376;     /* shape 7 */

	/* g_a5_9246 = 0 — extra long clear past the table. */
	*(long *)g_a5_buf(-9246) = 0;
}

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
unsigned char g_dlitem_pool[DLITEM_MAX * DLITEM_BYTES];
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

	/* THINK C's DATA blit pre-loads g_a5_-1316 (idle-active flag)
	 * with 0x05 and g_a5_-126 / g_a5_-130 with stale tick-like
	 * values. The Mac runtime would have drained -1316 via L79ec
	 * during pre-main init; we never call that path, so clear it
	 * here so jt1134's event-pump loop terminates. Also zero the
	 * paired tick slots so the first real L79d4 / L79ec pair
	 * computes a sensible delta. */
	g_a5_byte(-1316) = 0;
	g_a5_long(-126)  = 0;
	g_a5_long(-130)  = 0;

	/* DATA blit also seeds the L731e event cluster with junk
	 * (-820 = 0xbb mode flag, -810 = 0xff / -809 = 0xaa "event
	 * ready" flags, -814 = 0xbbbbbbXX "event handle" pointer).
	 * The Mac fills these from IRQ-driven input; without that
	 * path lifted, jt1118 reads the stale values and reports
	 * "continue polling" forever, which makes L2d3e's Phase 5
	 * fire on every iteration. Zero them so the cluster looks
	 * idle until real events arrive. */
	g_a5_byte(-820) = 0;
	g_a5_byte(-810) = 0;
	g_a5_byte(-809) = 0;
	g_a5_long(-814) = 0;

	/* JT[1083] PRNG seed. A non-zero seed is required — with state
	 * == 0 the LCG locks at state = 1 forever (state * mul + 1
	 * → 1) and all rolls degenerate to 0. Plant 1 here so each
	 * boot starts at a known, reproducible point; later passes can
	 * pull entropy from TickCount or similar. */
	g_a5_4902 = 1;

	/* DLItem pool base. jt447 (dialog reset) copies g_a5_9286 →
	 * g_a5_9254 on every dialog open, so this slot must hold the
	 * pool address before the first dialog. On the Mac, jt442
	 * (DLInit) seeds it from a NewPtr; calling jt442 from boot
	 * regressed ua_main, so seed g_a5_9286 directly here and let
	 * the table population happen in this function. */
	g_a5_9286 = (long)(uintptr_t)g_dlitem_pool;

	/* Populate ONLY the g_a5_9282 shape-handler table without
	 * touching g_a5_9248 / 9247 — the state that calling full
	 * jt442 disrupted. */
	{
		long *table = (long *)g_a5_buf(-9282);
		table[0] = (long)(uintptr_t)jt382;
		table[1] = (long)(uintptr_t)jt381;
		table[2] = (long)(uintptr_t)jt380;
		table[3] = (long)(uintptr_t)jt379;
		table[4] = (long)(uintptr_t)jt378;
		table[5] = (long)(uintptr_t)jt377;
		table[6] = (long)(uintptr_t)jt376;
	}
}

/* JT[452] (CODE 3 + 0x29a0) — DLItem stream installer.
 *
 * The Mac caller pushes a stream of (shape, args, shape, args, ...)
 * terminated by shape 0. Each shape code is a directive interpreted
 * against the "current record" (the most recently allocated DLItem):
 *
 *   0           end of stream
 *   1..7        allocate a new DLItem; method from g_a5_9282[shape-1];
 *               each shape consumes type-specific arg shape:
 *                 1,3,4,6,8 → 2 shorts (rec[16], rec[18]) + 1 long (rec[12])
 *                 2         → 1 short (rec[22])
 *                 5         → 4 shorts (rec[16], [18], [22], [24])
 *                 7         → 1 long (rec[4])
 *   8           allocate; method is the next long (caller-supplied)
 *   9..15       reserved no-op
 *   16..22      set bit (shape - 16) in rec[28]
 *   23          reserved no-op
 *   24..30      clear bit (shape - 24) in rec[28]
 *   31          reserved no-op
 *   32          rec[29] = next short (low byte)
 *   33          rec[30] = next short (low byte)
 *   34          rec[4..7] = next long
 *   35          rec[8..11] = next long
 *   36          rec[24..25] = next short
 *   37          rec[26..27] = next short
 *   38          rec[31] = next short (low byte)
 *   39          rec[12..15] = next long
 *   40          rec[16..17] + rec[18..19] = next two shorts
 *   41          rec[20..21] = next short
 *   42          rec[22..23] = next short
 *
 * The method-pointer table at g_a5_9282 stays unpopulated in the
 * port — the Mac has 7 builtin handlers (button, checkbox, text,
 * etc.) parked there at startup; until we install C equivalents
 * the methods read NULL and the dialog dispatcher (L2d3e) skips
 * them. The RECORD shape is correct though, so coords / labels /
 * action codes are stored right.
 *
 * C-side ABI note: variadic short args promote to int; the Mac
 * reads them as 2-byte shorts inline. The C lift consumes each
 * arg as `int` and casts to short/long as the shape dictates.
 * Long args (4 bytes) get the type explicitly. Callers in lifted
 * C code pass everything as `long` or short→long cast for
 * uniformity. */
/* NB: the lifted L0aae caller passes (i, label, sel, page, phr)
 * with i as a per-item index 0..11 — NOT a Mac shape code. The
 * earlier "full shape-code dispatch" lift interpreted i=0 as
 * end-of-stream and skipped half the items. Reverted to the
 * simpler "one DLItem per call" form that matches L0aae's actual
 * usage. The Mac stream-parser shape dispatch is documented in
 * the asm comment above and stays as a TODO once the L0aae lift
 * is restructured to follow Mac conventions.
 *
 * Crucial change kept from the shape-dispatch attempt: the method
 * pointer at rec[0] comes from the g_a5_9282 handler table that
 * boot_a5_seed_defaults seeds, instead of always being 0. Items
 * built via jt452 now have non-NULL methods, so L2d3e's dispatch
 * actually calls them — the click pipeline lights up end-to-end. */
static void jt452(long shape0, ...)
{
	va_list        ap;
	unsigned char *rec;
	long          *table;
	short          shape_idx;

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

	/* Install method pointer from the handler table. The Mac
	 * indexes by shape-1; our L0aae caller passes 0-based item
	 * indices, so clamp into [1..7] = [jt382..jt376]. Items past
	 * 7 cycle through the 7 handlers. */
	table = (long *)g_a5_buf(-9282);
	shape_idx = (short)(shape0 % 7);   /* 0..6 */
	*(long *)rec = table[shape_idx];

	/* Consume the rest of the stream for ABI parity. Shape-code 0
	 * terminates the Mac stream; the lift walks (long)args until 0. */
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

/* Forward — l4d88 / l6804 lift further down with jt1134's helpers.
 * jt1118 needs l4d88 for the InvalRect flush prelude; l731e needs
 * l6804 for the front-window gate. */
static void l4d88(void);
static signed char l6804(void);

/* L62fa (CODE 4, near L6538) — query "where is the mouse?" state.
 *
 * Writes a state code (1..4 region) to *out and returns non-zero
 * when the cursor is over our window. PROBE stub for now — the
 * real body walks GrafPort + window-z-order data. */
static short l62fa(short *out) __attribute__((unused));
static short l62fa(short *out)
{
	PROBE("L62fa");
	if (out != NULL)
		*out = 0;
	return 0;
}

/* L6538 (CODE 4 + 0x6538) — cursor reset / refresh.
 *
 * Called from L66e8(0). The Mac body:
 *
 *   if (L62fa(&region) && region == 3) {        // cursor in window
 *       if (g_a5_-893) {                        // grabbed-input mode
 *           if (arg != 0)
 *               SetCursor(g_a5_-3514);          // arrow
 *       } else {
 *           if (arg != 0 || g_a5_-894)
 *               SetCursor(g_a5_-892);           // engine cursor
 *       }
 *       g_a5_-894 = 0;
 *   } else {                                    // cursor outside window
 *       if (!g_a5_-893 && !g_a5_-894)
 *           SetCursor(g_a5_-3514);
 *       g_a5_-894 = 1;
 *   }
 *
 * The Falcon HAL doesn't expose a SetCursor equivalent yet, so
 * the actual cursor shape changes are deferred — the lift only
 * maintains the g_a5_-894 "needs-reset" flag the Mac uses as a
 * one-shot guard. SetCursor will land when the cursor sprite
 * lifecycle wires through the display HAL. */
static void l6538(short arg) __attribute__((unused));
static void l6538(short arg)
{
	short region = 0;

	PROBE("L6538");
	if (l62fa(&region) != 0 && region == 3) {
		/* In-window path. Visual SetCursor deferred. */
		if (g_a5_byte(-893) != 0) {
			/* grabbed-input mode — would set arrow if arg != 0 */
			(void)arg;
		} else {
			/* normal — would set engine cursor if arg != 0
			 * or the "needs-reset" flag was sticky. */
			(void)arg;
		}
		g_a5_byte(-894) = 0;
	} else {
		/* Out-of-window path. Would arrow-reset on first crossing. */
		g_a5_byte(-894) = 1;
	}
}

/* L66e8 (CODE 4 + 0x66e8, CODE-local) — event-record post-process
 * gate. Body: L6538(0) with the event-record arg ignored. */
static void l66e8(EventRecord *ev) __attribute__((unused));
static void l66e8(EventRecord *ev)
{
	PROBE("L66e8");
	(void)ev;
	l6538((short)0);
}

/* L731e (CODE 4 + 0x731e) — shared event filter (level-2 skeleton).
 *
 * The pump that jt1118 / jt1125 funnel through. Mac body:
 *
 *   short mask = -1;
 *   if (g_a5_-820)            mask &= ~0x08;          // input-mode adj
 *   if (arg != 2)
 *       if (!_EventAvail(2, &ev))   mask &= ~0x07;
 *   L66e8(&ev);
 *   do {                                              // L73ce / L7362
 *       if (!L6804()) {                               // not frontmost
 *           L66e8(&ev); L725c(mask);                  // pump dispatcher
 *           switch (arg) {                            // inline JT[3]
 *               case 2: ...; case 3: test g_a5_-903;
 *               default: fall through;
 *           }
 *           recheck mode + EventAvail with refreshed mask;
 *       }
 *   } while (L6804() && _EventAvail(mask, &ev));
 *
 * Inner loop deferred (level 2): with our l6804 stub returning
 * "always front" and the engine event queue idle during boot, the
 * outer loop falls through after the pre-EventAvail. Restore the
 * inner-loop / JT[3] arm dispatch once L725c's case handlers and
 * a real front-window check land. */
static void l731e(short arg)
{
	EventRecord ev;
	short       mask = (short)-1;

	PROBE("L731e");

	if (g_a5_byte(-820) != 0)
		mask = (short)(mask & ~0x08);

	if (arg != (short)2) {
		if (!EventAvail((short)2, &ev))
			mask = (short)(mask & ~0x07);
	}

	l66e8(&ev);

	if (l6804() != 0) {
		if (EventAvail(mask, &ev)) {
			/* Real Mac loops back into L7362 to dispatch the
			 * pending event; deferred until the L725c arms
			 * are lifted. */
			PROBE("L731e:event-pending");
		}
	}
}

/* JT[1118] (CODE 4 + 0x6710) — "should we continue polling?" gate.
 *
 *   l4d88();                       // flush deferred InvalRect
 *   l731e(3);                      // pump events (mode 3 = quick poll)
 *   if (g_a5_-810 || g_a5_-809) {
 *       if (g_a5_-814 != 0)        // event handle pending
 *           return 1;              // — yes, keep going
 *       g_a5_-809 = 0;             // stale flags; clear and fall through
 *       g_a5_-810 = 0;
 *   }
 *   return (signed char)g_a5_-820; // 0 (idle) or non-zero (mode override)
 *
 * Called by L31ea (JT[441]) and CODE 6/7/11 dispatchers as the
 * "continue?" check on dialog event loops. Returns a Mac-style
 * Boolean — 0 = stop, non-zero = continue. */
static short jt1118(void)
{
	PROBE("jt1118");
	l4d88();
	l731e((short)3);
	if (g_a5_byte(-810) != 0 || g_a5_byte(-809) != 0) {
		if (g_a5_long(-814) != 0)
			return 1;
		g_a5_byte(-809) = 0;
		g_a5_byte(-810) = 0;
	}
	return (short)(signed char)g_a5_byte(-820);
}
/* JT[1125] (CODE 4 + 0x6230) — engine event poll.
 *
 * The dialog event loop (L2d3e → L3198 → here) calls this every
 * frame to ask "any input?" The Mac body pulls from an internal
 * event buffer (g_a5_904 / 912 / 910 cluster) that L731e fills
 * from IRQ + Toolbox. Returns:
 *
 *   0   no event              (out1 = out2 = 0)
 *   1   basic event (key, click)
 *   2   special event (modified — Cmd-key etc.)
 *
 * For the port: bypass the Mac's IRQ-driven buffer and call
 * WaitNextEvent directly with a 1-tick sleep. The shim's event
 * queue is fed by IKBD / mouse synthesisers in compat/events.c,
 * so a real keyboard/mouse event from the host shows up here.
 *
 * Event mapping:
 *   keyDown / autoKey → out1 = charCode, out2 = modifiers
 *   mouseDown         → out1 = where.h,  out2 = where.v
 *   anything else     → (0, 0), return 0
 *
 * With this in place the engine's dialog loop receives real
 * input — clicks and keys from the host flow into the menu
 * system. Previously the loop spun forever because every poll
 * returned 0. */
static short  jt1125(short kind, long p1, long p2)
{
	EventRecord ev;
	short *out1 = (short *)(uintptr_t)p1;
	short *out2 = (short *)(uintptr_t)p2;

	PROBE("jt1125");
	(void)kind;
	if (out1 == NULL || out2 == NULL)
		return 0;

	if (!WaitNextEvent(everyEvent, &ev, 1, NULL)) {
		*out1 = 0;
		*out2 = 0;
		return 0;
	}

	switch (ev.what) {
	case keyDown:
	case autoKey:
		*out1 = (short)(ev.message & 0xff);
		*out2 = ev.modifiers;
		return (ev.modifiers & cmdKey) ? (short)2 : (short)1;
	case mouseDown:
		*out1 = ev.where.h;
		*out2 = ev.where.v;
		return 1;
	default:
		*out1 = 0;
		*out2 = 0;
		return 0;
	}
}
/* L79d4 (CODE 4 + 0x79d4) — start idle-time accounting.
 *
 * Idempotent: if g_a5_-1316 (idle-active flag) is already set,
 * a nested idle period is already in flight and this is a no-op.
 * Otherwise sets the flag and stamps the current TickCount into
 * g_a5_-126. Paired with L79ec around blocking Toolbox calls so
 * the engine can subtract OS wait time from elapsed game time
 * (the running total lives in g_a5_-130). */
static void l79d4(void) __attribute__((unused));
static void l79d4(void)
{
	PROBE("L79d4");
	if (g_a5_byte(-1316) != 0)
		return;
	g_a5_byte(-1316) = 1;
	g_a5_long(-126)  = TickCount();
}

/* L79ec (CODE 4 + 0x79ec) — end idle-time accounting.
 *
 * If g_a5_-1316 is set, clears it and accumulates the elapsed
 * (TickCount - g_a5_-126) into g_a5_-130. Otherwise no-op.
 * Called from L71ac (osEvt arm) and L7204 (diskEvt arm) inside
 * L725c's event dispatch. */
static void l79ec(void) __attribute__((unused));
static void l79ec(void)
{
	PROBE("L79ec");
	if (g_a5_byte(-1316) == 0)
		return;
	g_a5_byte(-1316) = 0;
	g_a5_long(-130) += TickCount() - g_a5_long(-126);
}

/* L4d88 (CODE 4 + 0x4d88) — flush deferred _InvalRect.
 *
 * Tests g_a5_-936 (pending-invalidate count). When non-zero, the
 * Mac body assembles a rect from cached coords (g_a5_-928 / -926
 * with a font-metric mul gated by g_a5_-2347), routes it through
 * L4fae / L4e12 (rect builders) + L5d8c (visibility test), and
 * traps _InvalRect to mark the area for redraw. Always clears the
 * pending count and resets the buffer pointer (-940 → -1041).
 *
 * Lifted as a PROBE stub: no Quickdraw buffer is wired into the
 * Falcon display HAL yet, so there's nothing to flush. Promote
 * to a real lift when the InvalRect dispatch lands. */
static void l4d88(void)
{
	PROBE("L4d88");
}

/* Forward — l71ac / l7090 / l70e0 / l7204 / l6cba lift further
 * down. l725c routes cases 1/6/7/8/15 (mouseDown/updateEvt/
 * activateEvt/osEvt/diskEvt). case 2 (mouseUp) routes to l6cba. */
static void l71ac(EventRecord *ev);
static void l7090(EventRecord *ev);
static void l70e0(EventRecord *ev);
static void l7204(EventRecord *ev);
static void l6cba(EventRecord *ev);

/* L725c (CODE 4 + 0x725c) — Mac event-pump dispatcher.
 *
 * Calls _WaitNextEvent (preferred — g_a5_-2590 picks the path) or
 * _SystemTask + _GetNextEvent, then routes the resulting event via
 * a JT[3] 15-arm switch into the per-event handlers:
 *
 *   1 keyDown / 2 keyUp  → L690e   (deferred)
 *   3 / 5 mouseDown      → L6cba   (deferred)
 *   6 updateEvt          → L7090   (deferred)
 *   7 activateEvt        → L70e0   (deferred)
 *   8 osEvt              → L71ac   (lifted — suspend/resume arm)
 *  15 diskEvt            → L7204   (deferred)
 *
 * Mini lift: WaitNextEvent the queue, stash event.what into
 * g_a5_-2592, dispatch case 8 → L71ac so suspend/resume actually
 * runs end-to-end. Other arms still PROBE-only (logged but not
 * routed). The MultiFinder branch (g_a5_-2590) is collapsed to
 * always-WaitNextEvent — TT/Falcon don't have classic Finder. */
static void l725c(short mask)
{
	EventRecord ev;

	PROBE("L725c");
	if (!WaitNextEvent(mask, &ev, 1, NULL))
		return;
	g_a5_word(-2592) = ev.what;

	switch (ev.what) {
	case 6:                                    /* updateEvt */
		l7090(&ev);
		break;
	case 7:                                    /* activateEvt */
		l70e0(&ev);
		break;
	case 8:                                    /* osEvt */
		l71ac(&ev);
		break;
	case 15:                                   /* diskEvt / high-level */
		l7204(&ev);
		break;
	case 2:                                    /* mouseUp */
		l6cba(&ev);
		break;
	case 1:                                    /* mouseDown */
	case 3: case 5:                            /* keyDown / autoKey */
		PROBE("L725c:arm-deferred");
		break;
	default:
		break;
	}
}

/* L6804 (CODE 4 + 0x6804) — "are we the front window?" probe.
 *
 *   short L6804(void) { return _FrontWindow() == g_a5_-2578; }
 *
 * Used by jt1134 to keep yielding to the OS until the engine's
 * own window regains focus.
 *
 * Stub: window Z-order isn't tracked yet, so always report front
 * so jt1134's event-pump loop exits after one iteration. */
static signed char l6804(void)
{
	PROBE("L6804");
	return 1;
}

/* L74ae / L747a / L7690 — deeper menu-bar plumbing the jt1145 /
 * jt1151 paths call. PROBE-only stubs for now; their bodies sit
 * below this region in CODE 4 and walk MenuMgr / Window state. */
static void l74ae(void) __attribute__((unused));
static void l74ae(void)
{
	PROBE("L74ae");
}

/* L747a (CODE 4, near L74ae) — schedule paint with bounds.
 * Takes (ptr, long_a, long_b) — caller cleanup is 12 bytes.
 * Stub for now; real body lives below in the menu manager. */
static void l747a(void *p1, long p2, long p3) __attribute__((unused));
static void l747a(void *p1, long p2, long p3)
{
	PROBE("L747a");
	(void)p1; (void)p2; (void)p3;
}

/* L7690 is the same address as JT[1122] — same function, two
 * naming conventions. Defined together with jt1122 further down;
 * forward-declared here so jt1151 can call it. */
static void jt1122(short mode, short slot, short val);
/* Forward — jt5 (unsigned 32-bit divide) lifts further down. */
static unsigned long jt5(unsigned long a, unsigned long b);

/* L0f48 (CODE 5 + 0x0f48, CODE-local) — count non-empty menu slots.
 *
 * Walks a 5-entry table of 14-byte records at g_a5_-4848 and
 * returns how many have a non-zero first long. Used by jt983 to
 * decide whether the menu bar deserves to be shown (> 1 entry
 * present). */
static short l0f48(void) __attribute__((unused));
static short l0f48(void)
{
	short count = 0;
	short i;

	PROBE("L0f48");
	for (i = 0; i < 5; i++) {
		long *entry = (long *)(g_a5_buf(-4848) + (long)i * 14);
		if (*entry != 0)
			count++;
	}
	return count;
}

/* JT[1145] (CODE 4 + 0x7628) — show menu bar.
 *
 *   if (g_a5_-779) { L74ae(); g_a5_-779 = 0; }
 *   if (g_a5_-780 == 0) {
 *       g_a5_-780 = 1;
 *       g_a5_-266 = 2500;                       // some timeout
 *       L747a(&g_a5_-216, 6, 0);                // schedule paint
 *   }
 *
 * Idempotent on the visible flag (-780): only schedules once. The
 * actual visual change happens inside L747a; lifted as a stub
 * here so the state bits move correctly. */
static void jt1145(void) __attribute__((unused));
static void jt1145(void)
{
	PROBE("jt1145");
	if (g_a5_byte(-779) != 0) {
		l74ae();
		g_a5_byte(-779) = 0;
	}
	if (g_a5_byte(-780) == 0) {
		g_a5_byte(-780) = 1;
		g_a5_word(-266) = (short)2500;
		l747a(g_a5_buf(-216), 6L, 0L);
	}
}

/* JT[1151] (CODE 4 + 0x765c) — hide menu bar.
 *
 *   if (g_a5_-780) {
 *       g_a5_-264 = g_a5_-256 = g_a5_-248 = g_a5_-240 = 0;
 *       L74ae();
 *       g_a5_-780 = 0;
 *   } else {
 *       jt1122(0, 0, 0);                        // alternate cleanup (= L7690)
 *   }
 *
 * Symmetric with jt1145 — clears the cached menu-bar geometry
 * (4 longs at -264..-228) if the bar was visible. */
static void jt1151(void) __attribute__((unused));
static void jt1151(void)
{
	PROBE("jt1151");
	if (g_a5_byte(-780) != 0) {
		g_a5_long(-264) = 0;
		g_a5_long(-256) = 0;
		g_a5_long(-248) = 0;
		g_a5_long(-240) = 0;
		l74ae();
		g_a5_byte(-780) = 0;
	} else {
		jt1122((short)0, (short)0, (short)0);
	}
}

/* JT[983] (CODE 5 + 0x0f74) — menu-bar visibility setter.
 *
 *   g_a5_-4776 = (signed char)arg;             // cache state
 *   if (arg == 0)             jt1151();        // hide
 *   else if (l0f48() > 1)     jt1145();        // show, gated on count
 *
 * Now functional end-to-end: L71ac's suspend/resume arm drives
 * jt983 → jt1151 / jt1145, which flip the engine's menu-bar
 * visibility flags. Real visual changes still live inside L74ae /
 * L747a (stubbed below). */
static void jt983(short arg) __attribute__((unused));
static void jt983(short arg)
{
	PROBE("jt983");
	g_a5_byte(-4776) = (signed char)(arg & 0xff);
	if ((arg & 0xff) == 0) {
		jt1151();
	} else {
		if (l0f48() > 1)
			jt1145();
	}
}

/* L24aa (CODE 4 + 0x24aa) = JT[1178] — menu repaint / item refresh.
 *
 * Large (-1036 byte frame, nested loops, mulsw #108). Walks the
 * 108-byte per-page descriptors at g_a5_-2570 and updates a
 * cluster around g_a5_-1310 / -1318. Called from L71ac and CODE
 * 6/7 menu paths. PROBE-only stub for now — the full lift is its
 * own task. */
static void l24aa(void) __attribute__((unused));
static void l24aa(void)
{
	PROBE("L24aa");
}

/* L3e38 (CODE 3 + 0x3e38, CODE-local) — window content repaint.
 *
 * Called inside the BeginUpdate / EndUpdate bracket of L7090.
 * Walks the engine's draw list and re-blits visible content into
 * the window. PROBE-only stub for now — the real paint code lives
 * deeper in CODE 3 and depends on a chain of unlifted helpers. */
static void l3e38(void) __attribute__((unused));
static void l3e38(void)
{
	PROBE("L3e38");
}

/* JT[1064] (CODE 5 + 0x4992) — hit-test? L70e0 calls this after
 * computing scaled coords. Returns short — non-zero means the
 * activate processed something and we should not stamp the
 * modifier slot. PROBE-only stub for now. */
static short jt1064(long msg, long scaled, short flag) __attribute__((unused));
static short jt1064(long msg, long scaled, short flag)
{
	PROBE("jt1064");
	(void)msg; (void)scaled; (void)flag;
	return 0;
}

/* L70e0 (CODE 4 + 0x70e0) — activateEvt arm.
 *
 *   long  msg  = event.message;
 *   short high = (short)(msg >> 16);
 *   if (high != 0) {                          // activate (vs deactivate)
 *       // Pick the window's port-data ptr based on color QD flag:
 *       //   color: deref window+2 (Handle) twice + 6
 *       //   mono : window+8 directly
 *       unsigned char *pd = ...;
 *       Rect *r = (Rect *)(window + 16);      // port rect
 *       short v = (r->bottom - r->top  + -285) / 2
 *                 - *(short *)(pd + 2);
 *       short h = (r->right  - r->left + -100) / 3
 *                 - *(short *)pd;
 *       if (JT[1064](msg, (long)h:v, 0) != 0)
 *           return;                           // handled; skip modifier stamp
 *       g_a5_-808 = (short)(msg & 0xFFFF);    // stamp low word
 *   } else {
 *       g_a5_-808 = (short)(msg & 0xFFFF);    // deactivate: just stamp
 *   }
 *
 * Note: g_a5_-808 is the same slot jt1121 reads-and-clears. The
 * modifier value persists until the engine consumes it.
 *
 * The rect math reproduces the asm verbatim — the magic constants
 * -285 (vertical center offset) and -100 (horizontal center
 * offset divided by 3) come from the original FRUA layout. */
static void l70e0(EventRecord *ev)
{
	long           msg;
	long           window_long;
	unsigned char *port_data;
	Rect          *port_rect;
	short          v, h;
	long           hv_pair;
	short          ret;

	PROBE("L70e0");
	if (ev == NULL)
		return;
	msg = (long)ev->message;
	if ((msg & 0xFFFF0000L) == 0) {
		/* Deactivate — just stamp the modifier slot. */
		g_a5_word(-808) = (short)(msg & 0xFFFFL);
		return;
	}

	/* Activate path. */
	window_long = g_a5_long(-2578);
	if (window_long == 0) {
		g_a5_word(-808) = (short)(msg & 0xFFFFL);
		return;
	}

	if (g_a5_2347 != 0) {
		/* Color QD — entry+2 is a Handle, deref twice + 6 bytes. */
		void **handle = *(void ***)((unsigned char *)(uintptr_t)window_long + 2);
		port_data = (handle != NULL && *handle != NULL)
		            ? ((unsigned char *)*handle + 6)
		            : NULL;
	} else {
		port_data = (unsigned char *)(uintptr_t)window_long + 8;
	}

	port_rect = (Rect *)(uintptr_t)(window_long + 16);
	v = (short)((port_rect->bottom - port_rect->top + -285) / 2);
	h = (short)((long)(port_rect->right - port_rect->left + -100) / 3);
	if (port_data != NULL) {
		v -= *(short *)(port_data + 2);
		h -= *(short *)port_data;
	}

	hv_pair = ((long)h << 16) | (long)(unsigned short)v;
	ret = jt1064(msg, hv_pair, (short)0);
	if (ret != 0)
		return;
	g_a5_word(-808) = (short)(msg & 0xFFFFL);
}

/* L7090 (CODE 4 + 0x7090) — updateEvt arm.
 *
 *   if (event.message == g_a5_-2578) {    // our window
 *       BeginUpdate(window);
 *       L3e38();                          // repaint content
 *       EndUpdate(window);
 *   }
 *
 * Standard Mac update-event pattern. L3e38 is the engine's
 * content-repaint dispatch (still a stub). Reached via L725c
 * case 6 now that the dispatch is wired. */
static void l7090(EventRecord *ev) __attribute__((unused));
static void l7090(EventRecord *ev)
{
	WindowPtr w;

	PROBE("L7090");
	if (ev == NULL)
		return;
	if ((long)ev->message != g_a5_long(-2578))
		return;
	w = (WindowPtr)(uintptr_t)ev->message;
	BeginUpdate(w);
	l3e38();
	EndUpdate(w);
}

/* L71ac (CODE 4 + 0x71ac) — osEvt suspend/resume arm.
 *
 * Reached from L725c's case-8 (osEvt) dispatch. Body:
 *
 *   if (event.message != g_a5_-2578)       // not our window
 *       return;
 *   if (event.modifiers & 0x0001) {        // resume bit set
 *       l79ec();                            // end idle accounting
 *       jt983(1);                           // show menu bar
 *       SetPort(g_a5_-2578);                // restore engine GrafPtr
 *       l24aa();                            // menu/item refresh
 *       InvalRect((Rect *)(g_a5_-2578 + 16));// invalidate window content
 *   } else {
 *       jt983(0);                           // hide menu bar
 *       l79d4();                            // start idle accounting
 *   }
 *
 * The "bit 0 of modifiers low byte" test is unusual — the Mac
 * suspendResumeMessage encodes the resume flag in the message
 * field (byte 5), but THINK C / SSI's engine appears to use the
 * modifiers byte 15 instead. Lifted verbatim from the asm.
 *
 * Dormant in the current boot trace: L725c's arm dispatch isn't
 * wired yet, so this is unreachable until the case-8 path lands.
 * First real exercise of l79d4 / l79ec when that wiring happens. */
static void l71ac(EventRecord *ev) __attribute__((unused));
static void l71ac(EventRecord *ev)
{
	long window_ptr;

	PROBE("L71ac");
	if (ev == NULL)
		return;

	window_ptr = g_a5_long(-2578);
	if ((long)ev->message != window_ptr)
		return;

	if ((ev->modifiers & 0x0001) != 0) {
		/* Resume path. */
		l79ec();
		jt983((short)1);
		if (window_ptr != 0)
			SetPort((GrafPtr)(uintptr_t)window_ptr);
		l24aa();
		if (window_ptr != 0)
			InvalRect((Rect *)(uintptr_t)(window_ptr + 16));
	} else {
		/* Suspend path. */
		jt983((short)0);
		l79d4();
	}
}

/* L04cc / L04de (CODE 4) — screen-dimension constants gated by
 * g_a5_-2347 (color QD flag).
 *   L04cc: 300 mono / 200 color  (vertical screen height)
 *   L04de: 480 mono / 320 color  (horizontal screen width)
 *
 * Used to clamp mouseUp coordinates to the visible screen. */
static short l04cc(void) __attribute__((unused));
static short l04cc(void)
{
	return (g_a5_2347 != 0) ? (short)200 : (short)300;
}
static short l04de(void) __attribute__((unused));
static short l04de(void)
{
	return (g_a5_2347 != 0) ? (short)320 : (short)480;
}

/* L6cba (CODE 4 + 0x6cba) — mouseUp arm.
 *
 * Reached from L725c case 2. Captures the up coordinates clamped
 * to the visible screen and flags g_a5_-903 so the engine input
 * loop can read them.
 *
 * Mac body:
 *   if (FrontWindow() != g_a5_-2578)         return;
 *   Point pt = event.where;
 *   GlobalToLocal(&pt);
 *   if (g_a5_-2346) { pt.v >>= 1; pt.h >>= 1; }   // half-scale mode
 *   g_a5_-908 = clamp(pt.h, 0, L04cc());           // (200 / 300)
 *   g_a5_-906 = clamp(pt.v, 0, L04de());           // (320 / 480)
 *   g_a5_-903 = 1;
 *   g_a5_-901 = 0;
 *
 * GlobalToLocal isn't wired in the Falcon HAL yet (no Mac-style
 * window coordinate translation). Treating ev.where as local
 * is correct for the engine's single-window setup. The clamps
 * fire so g_a5_-908 / -906 land within the engine's expected
 * screen rect even if the raw coords overshoot. */
static void l6cba(EventRecord *ev) __attribute__((unused));
static void l6cba(EventRecord *ev)
{
	short h, v;

	PROBE("L6cba");
	if (ev == NULL)
		return;
	if ((long)(uintptr_t)FrontWindow() != g_a5_long(-2578))
		return;

	v = ev->where.v;
	h = ev->where.h;

	if (g_a5_byte(-2346) != 0) {
		v >>= 1;
		h >>= 1;
	}

	g_a5_word(-908) = jt413(jt397((short)0, h), l04cc());
	g_a5_word(-906) = jt413(jt397((short)0, v), l04de());
	g_a5_byte(-903) = 1;
	g_a5_byte(-901) = 0;
}

/* L7204 (CODE 4 + 0x7204) — diskEvt / high-level event arm.
 *
 * Reached from L725c case 15. Same suspend/resume body as L71ac,
 * but uses the *standard* Mac suspendResumeMessage layout instead
 * of L71ac's modifiers-byte convention:
 *
 *   if ((event.message >> 24) == 1) {        // suspendResumeMessage
 *       if (event.message & 0x01)            // resume bit (low byte bit 0)
 *           resume_path;                     // l79ec, jt983(1), SetPort, l24aa, InvalRect
 *       else
 *           suspend_path;                    // jt983(0), l79d4
 *   }
 *
 * Either L71ac or L7204 will run depending on which event-type
 * carried the suspend/resume info — the engine handles both
 * routes since pre-System 7 Mac queued these slightly differently. */
static void l7204(EventRecord *ev) __attribute__((unused));
static void l7204(EventRecord *ev)
{
	long msg;
	long window_ptr;

	PROBE("L7204");
	if (ev == NULL)
		return;

	msg = (long)ev->message;
	if ((msg >> 24) != 1)
		return;

	window_ptr = g_a5_long(-2578);

	if ((msg & 0x01L) != 0) {
		/* Resume path. */
		l79ec();
		jt983((short)1);
		if (window_ptr != 0)
			SetPort((GrafPtr)(uintptr_t)window_ptr);
		l24aa();
		if (window_ptr != 0)
			InvalRect((Rect *)(uintptr_t)(window_ptr + 16));
	} else {
		/* Suspend path. */
		jt983((short)0);
		l79d4();
	}
}

/* jt1134 (CODE 4 + 0x7980) — yield-to-OS / drain idle paint.
 *
 * Structural lift (level 2). The Mac body is:
 *
 *   L4d88();                              // flush pending InvalRect
 *   do {
 *       do {
 *           L725c(0x8140);                // pump one event
 *       } while (g_a5_-1316 != 0);        // ...until dirty queue drains
 *   } while (!L6804());                   // ...until we're front window
 *   (TickCount() - g_a5_-130) * 6 / 5;    // result discarded in asm
 *
 * 0x8140 is the Toolbox everyEvent mask (sysMask + mouse + key);
 * the per-event arms inside L725c are deferred until the L725c
 * lift lands. The trailing arithmetic is dead in the original
 * (d0 is clobbered before rts) — preserved for fidelity in case
 * a side-effect lives inside JT[4] / JT[7] we haven't found yet.
 *
 * The dirty flag (g_a5_-1316) is DATA-initialized to 0x05; we
 * zero it in boot_a5_seed_defaults so the inner loop exits on
 * the first pass while L725c is still a PROBE-only stub. Once
 * real events flow through L725c → L71ac / L7204 → L79ec, the
 * flag will drain naturally and the loop will iterate as the
 * Mac intended. */
static long jt1134(void)
{
	long elapsed;

	PROBE("jt1134");
	l4d88();
	do {
		do {
			l725c((short)0x8140);
		} while (g_a5_byte(-1316) != 0);
	} while (l6804() == 0);
	elapsed = TickCount() - g_a5_long(-130);
	return (elapsed * 6) / 5;
}
static void   jt1153(short arg)                   { PROBE("jt1153");
                                                    (void)arg; }
/* L050a (CODE 4 + 0x050a, CODE-local) — page byte-count selector.
 *
 *   if (g_a5_-2347 == 0)        return 18002;   // mono FRUA framebuffer
 *   if (g_a5_-1312 != 0)        return 64000;   // 320x200 8-bit
 *   else                        return 32000;   // 320x200 4-bit
 *
 * Returned as `long` since the Mac uses `movel`; jt1146 truncates
 * to short at the jt406 call. */
static long l050a(void) __attribute__((unused));
static long l050a(void)
{
	PROBE("L050a");
	if (g_a5_2347 == 0)
		return 18002L;
	return (g_a5_1312 != 0) ? 64000L : 32000L;
}

/* JT[1146] (CODE 4 + 0x5c82) — page-flip / full-page blit.
 *
 * JT[108] hits this when its arg is non-zero ("commit deferred
 * paint to the visible page"). The Mac body picks two 108-byte
 * "page descriptor" entries from the array at g_a5_-2570 — entry
 * [current] and entry [1 - current], where current = g_a5_-2354 —
 * and memmoves L050a bytes from the off-page to the on-page using
 * jt406.
 *
 *   entry+2 holds the page-data pointer. In color QuickDraw mode
 *   (g_a5_-2347 set) the field is a Handle (void**); the Mac
 *   double-derefs to reach the raw pixel buffer. In mono mode the
 *   field is a direct void*.
 *
 * Both code paths converge on:
 *
 *   jt406(dst = bits[current], src = bits[1 - current], L050a()).
 *
 * Note: the arg order in jt406 is (dst, src, count) — matches
 * BlockMove on Mac, opposite of memcpy. The flip direction is
 * "copy the off-screen back-buffer onto the visible page", which
 * mirrors how the engine renders into the alternate page and
 * commits to the visible one once a frame's worth of paint is
 * queued. */
static void jt1146(void)
{
	unsigned char *base;
	unsigned char *entry_cur;
	unsigned char *entry_oth;
	void  *dst;
	void  *src;
	short  cur;
	short  count;

	PROBE("jt1146");
	l4d88();

	cur       = g_a5_word(-2354);
	base      = g_a5_buf(-2570);
	entry_cur = base + 108 * cur;
	entry_oth = base + 108 * (1 - cur);

	if (g_a5_2347 != 0) {
		/* Color QD: entry+2 is a Handle (void**); deref twice. */
		void **h_cur = *(void ***)(entry_cur + 2);
		void **h_oth = *(void ***)(entry_oth + 2);
		dst = (h_cur != NULL) ? *h_cur : NULL;
		src = (h_oth != NULL) ? *h_oth : NULL;
	} else {
		/* Mono: entry+2 is a direct data pointer. */
		dst = *(void **)(entry_cur + 2);
		src = *(void **)(entry_oth + 2);
	}

	count = (short)l050a();
	if (dst != NULL && src != NULL)
		jt406(dst, src, count);
}

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
	return (int)jt1118();
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

/* JT[1007] / JT[1123] — L2d3e selection-nav primitives. PROBE
 * for now; once lifted, Tab/arrow keystrokes will actually
 * move the dialog's highlighted item. */
static void jt1007(short cur_sel, short key) __attribute__((unused));
static void jt1007(short cur_sel, short key)
{
	PROBE("jt1007");
	(void)cur_sel; (void)key;
}
static void jt1123(short a) __attribute__((unused));
static void jt1123(short a)
{
	PROBE("jt1123");
	(void)a;
}
/* JT[1122] (CODE 4 + 0x7690) — menu-bar slot setter / blinker.
 *
 * Two-mode function gated by g_a5_-806 (enable) and g_a5_-780
 * (menu-bar visible):
 *
 *   if (!g_a5_-806)                    return;       // gated off
 *   if (g_a5_-780) {                                 // menu visible
 *       g_a5_-266 = 2500;                            // refresh timeout
 *       long computed = slot_val ?
 *           jt5(0x233244F7u, slot_val) : 0;          // hash-divide
 *       switch (mode) {                              // pick slot
 *           case 0: g_a5_-264 = computed; break;
 *           case 1: g_a5_-256 = computed; break;
 *           case 2: g_a5_-248 = computed; break;
 *           case 3: g_a5_-240 = computed; break;
 *       }
 *   } else {                                         // menu hidden
 *       if (g_a5_-779) { L74ae(); g_a5_-779 = 0; }   // refresh
 *       if (!slot_val) return;
 *       g_a5_-204 = 2500;                            // alt timeout
 *       g_a5_-208 = slot_val;
 *       g_a5_-210 = -1;
 *       g_a5_-206 = 128;
 *       jt399(&g_a5_-202, 6, 0);                     // zero 6 bytes
 *       L747a(&g_a5_-210, 14, 0);                    // schedule paint
 *       g_a5_-779 = 1;
 *   }
 *
 * Note: jt1080's name "no-hit chime" was a guess — this isn't an
 * audio function. It updates the menu-bar slot LONG and triggers
 * a paint cycle. The "beep" sequence in jt1080 is actually a
 * brief blink of slot 2 (jt1122(2, 1189, 0) for blink-on, then
 * jt1122(2, 0, 0) for blink-off with a 5/1 tick pause between). */
static void jt1122(short mode, short slot_val, short c)
{
	long computed;

	PROBE("jt1122");
	(void)c;                                /* 3rd arg unused in asm */

	if (g_a5_byte(-806) == 0)
		return;

	if (g_a5_byte(-780) != 0) {
		/* Menu-visible path. */
		g_a5_word(-266) = (short)2500;
		computed = (slot_val != 0)
		         ? (long)jt5(0x233244F7UL,
		                     (unsigned long)(unsigned short)slot_val)
		         : 0L;
		switch (mode) {
		case 0: g_a5_long(-264) = computed; break;
		case 1: g_a5_long(-256) = computed; break;
		case 2: g_a5_long(-248) = computed; break;
		case 3: g_a5_long(-240) = computed; break;
		default: break;
		}
		return;
	}

	/* Menu-hidden path. */
	if (g_a5_byte(-779) != 0) {
		l74ae();
		g_a5_byte(-779) = 0;
	}
	if (slot_val == 0)
		return;
	g_a5_word(-204) = (short)2500;
	g_a5_word(-208) = slot_val;
	g_a5_word(-210) = (short)-1;
	g_a5_word(-206) = (short)128;
	jt399(g_a5_buf(-202), (short)6, (short)0);
	l747a(g_a5_buf(-210), 14L, 0L);
	g_a5_byte(-779) = 1;
}

/* JT[1080] (CODE 5 + 0x0156) — "no-hit" feedback chime.
 *
 *   start = jt1134();                    // current idle-adjusted tick
 *   jt1122(2, 1189, 127);                // beep on (freq 1189, vol 127)
 *   while (jt1134() < start + 5)         // hold 5 ticks
 *       ;
 *   jt1122(2, 0, 0);                     // beep off
 *   while (jt1134() < start + 6)         // wait one more tick
 *       ;
 *
 * Fires from L2d3e Phase 3 when an event arrived but no DLItem
 * caught the mouse — the audible "nope" cue. The wait loops also
 * pump the event-queue via jt1134, so the engine stays responsive
 * during the beep. */
static void jt1080(void) __attribute__((unused));
static void jt1080(void)
{
	long start;

	PROBE("jt1080");
	start = jt1134();
	jt1122((short)2, (short)1189, (short)127);
	while (jt1134() < start + 5)
		;
	jt1122((short)2, (short)0, (short)0);
	while (jt1134() < start + 6)
		;
}

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
	short  count = g_a5_9250;
	short  cur_sel, sel_key;
	unsigned char *rec;
	dlitem_method_t method;

	PROBE("L2d3e");

	/* Phase 1 — event read. */
	jt1153(1);
	jt1134();                       /* L2c60 walks items with cmd=1 */
	key = l3198(7, (long)&mouse_y, (long)&mouse_x);

	/* Phase 2 — hit-test: walk DLItems calling method(rec, 2, y, x).
	 * First non-zero return is the item under the mouse. Method
	 * pointers stay NULL until JT[452]'s shape-code dispatch lands,
	 * so each call is guarded. */
	rec = (unsigned char *)g_a5_9254;
	for (i = 0; i < count; i++) {
		method = *(dlitem_method_t *)rec;
		if (method != NULL && method(rec, (short)2,
		                              mouse_y, mouse_x) != 0)
			break;
		rec += DLITEM_BYTES;
	}

	/* Phase 3 — action fire: if we have an event AND a hit, invoke
	 * method(rec, 3, ...) then method(rec, 4, ...). Bit 4 of
	 * rec[28] is the "selected/committed" flag; if it's set after
	 * the action methods run, the hit index becomes our return
	 * value (caller exits the loop). Otherwise clear the high bit
	 * (acknowledge the event) and fall through to selection nav. */
	if (key != 0) {
		if (i < count) {
			method = *(dlitem_method_t *)rec;
			if (method != NULL && method(rec, (short)3,
			                              mouse_y, mouse_x,
			                              key) != 0) {
				method(rec, (short)4, mouse_y, mouse_x, key);
				if ((rec[28] & 0x10) != 0)
					return i;
				rec[28] &= 0x7f;
			}
		} else {
			jt1080();                /* no hit — sleep tick */
		}
	}

	/* Phase 4 — selection navigation. Pick the next move based on
	 * whether we hit something + the engine's auto-scroll state at
	 * g_a5_4884/4882. JT[1007] commits the new selection; JT[1123]
	 * is the "no movement" fallback. */
	if (i < count) {
		short word20 = *(short *)(rec + 20);
		if (word20 != 0)
			sel_key = word20;
		else
			sel_key = (g_a5_4884 >= 0)
			          ? g_a5_4882 : (short)1;
	} else {
		sel_key = (g_a5_4884 >= 0)
		          ? (short)(g_a5_4882 + 1) : (short)2;
	}

	if (sel_key >= 0) {
		cur_sel = (g_a5_4884 >= 0) ? g_a5_4884 : (short)0;
		jt1007(cur_sel, sel_key);
	} else {
		jt1123(0);
	}

	/* Phase 5 — confirmed selection (Return key path). L31ea polls
	 * the post-action flag; L31f0 returns the resolved selection
	 * code. Walk DLItems calling method(rec, 5, code); on match,
	 * fire the kind=19 / 1 / 4(-1) / 27 method sequence and check
	 * the commit bit. */
	if (l31ea() != 0) {
		sel_key = l31f0();
		rec = (unsigned char *)g_a5_9254;
		for (i = 0; i < count; i++) {
			method = *(dlitem_method_t *)rec;
			if (method != NULL && method(rec, (short)5,
			                              sel_key) != 0)
				break;
			rec += DLITEM_BYTES;
		}
		if (i < count) {
			method = *(dlitem_method_t *)rec;
			if (method != NULL) {
				method(rec, (short)19);
				method(rec, (short)1);
				jt1134();
				method(rec, (short)4, (short)-1);
				method(rec, (short)27);
				if ((rec[28] & 0x10) != 0)
					return i;
			}
		} else {
			jt1080();
		}
	}

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

/* L6bbe (CODE 14 + 0x6bbe) — linear search for `key` in the id-key
 * array g_a5_25676; returns the 1-based index of the match (or the
 * count past the last when not found). The companion record array
 * at g_a5_27472 has 6-byte records keyed at the same row index. */
static short l6bbe(long key) __attribute__((unused));
static short l6bbe(long key)
{
	short idx = 0;
	short found = 0;

	PROBE("l6bbe");
	do {
		idx++;
		if (g_a5_25676[idx] == key)
			found = 1;
		if (found || (unsigned char)idx == g_a5_27468)
			break;
	} while (1);
	return idx;
}

/* JT[525] (CODE 14 + 0x6b40, 52 sites) — field 1 of the record
 * matching key. JT[531] (CODE 14 + 0x6b6a, 52 sites) — field 3 of
 * the same record. Both look up via L6bbe then index the 6-byte
 * row in g_a5_27472. */
static unsigned char jt525(long key) __attribute__((unused));
static unsigned char jt525(long key)
{
	short idx;
	PROBE("jt525");
	idx = (l6bbe(key) & 0xff) * 6;
	return g_a5_27472[idx + 1];
}
static unsigned char jt531(long key) __attribute__((unused));
static unsigned char jt531(long key)
{
	short idx;
	PROBE("jt531");
	idx = (l6bbe(key) & 0xff) * 6;
	return g_a5_27472[idx + 3];
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
/* JT[471] (CODE 3 + 0x02e8) — slot free (sister of JT[477] right
 * before it in CODE 3). Recovers the slot index from the entry
 * pointer + bucket geometry, then clears the bitmap bit:
 *
 *   idx       = (entry - bucket->base) / bucket->record_size
 *   byte_idx  = idx / 8
 *   bit_idx   = idx % 8
 *   bucket[8 + byte_idx] &= ~(1 << bit_idx)
 *
 * Bails on negative idx or idx >= bucket->max_count. The `tag`
 * arg is unused — same intended type-check that never landed
 * (see JT[477] for the matching reserve). Used by L15e2's design-
 * delete arm to tear down a record from two lists. */
static void jt471(long entry, short tag, void *bucket)
{
	unsigned char *b = (unsigned char *)bucket;
	short          max_count, record_size, idx, byte_idx, bit_idx;
	long           base_ptr;

	PROBE("jt471");
	(void)tag;
	if (b == NULL)
		return;
	max_count   = *(short *)(b + 0);
	record_size = *(short *)(b + 2);
	base_ptr    = *(long  *)(b + 4);

	if (record_size == 0)
		return;                          /* guard div-by-zero */
	idx = (short)((entry - base_ptr) / record_size);
	if (idx < 0 || idx >= max_count)
		return;

	byte_idx = (short)(idx >> 3);
	bit_idx  = (short)(idx - byte_idx * 8);
	b[8 + byte_idx] &= (unsigned char)~(1 << bit_idx);
}
/* JT[477] (CODE 3 + 0x0214, 65 sites) — slot reserve from a bucket.
 *
 * Bucket layout:
 *   +0  short  max_count        — capacity in slots
 *   +2  short  record_size      — bytes per record
 *   +4  long   base_ptr         — base address of records
 *   +8..  byte bitmap           — 8 slots per byte, bit set = used
 *
 * Walks the bitmap looking for the first byte != 0xff (i.e., has
 * a free bit). Within that byte, scans bits 0..7 for the first
 * unset. If the resulting slot index (byte * 8 + bit) is in-range,
 * marks the bit used and stores `base + idx * record_size` in
 * *out. Otherwise *out = 0 (bucket full).
 *
 * The `tag` arg is unused by the Mac body — probably an intended
 * type-check that never landed. */
static void jt477(void *bucket, short tag, void *out)
{
	unsigned char *b   = (unsigned char *)bucket;
	long          *out_long = (long *)out;
	short          max_count, record_size, byte_idx, bit_idx, idx;
	unsigned char  slot_byte;
	long           base_ptr;

	PROBE("jt477");
	(void)tag;
	if (b == NULL || out_long == NULL)
		return;
	max_count   = *(short *)(b + 0);
	record_size = *(short *)(b + 2);
	base_ptr    = *(long  *)(b + 4);

	/* Find first byte with at least one free bit. */
	byte_idx = 0;
	for (;;) {
		slot_byte = b[8 + byte_idx];
		if (slot_byte != 0xff)
			break;
		if (byte_idx >= (short)(max_count - 1))
			break;
		byte_idx++;
	}

	/* Find first unset bit in this byte. */
	for (bit_idx = 0; bit_idx < 8; bit_idx++) {
		if ((slot_byte & (1 << bit_idx)) == 0)
			break;
	}

	idx = (short)(byte_idx * 8 + bit_idx);
	if (idx >= max_count) {
		*out_long = 0;
		return;
	}

	b[8 + byte_idx] |= (unsigned char)(1 << bit_idx);
	*out_long = base_ptr + (long)idx * record_size;
}
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
/* L77a0 / L1b14 — equip-removal and class-specific cleanup hooks
 * that jt878 dispatches into. CODE 18 leaves, PROBE for now. */
static void l77a0(short item_type, void *entity, void *target, short flag)
                                            { PROBE("l77a0"); (void)item_type;
                                              (void)entity; (void)target;
                                              (void)flag; }
static void l1b14(void *entity, short class)
                                            { PROBE("l1b14"); (void)entity;
                                              (void)class; }

/* JT[878] (CODE 18 + 0x009e, 39 sites) — remove item from
 * inventory.
 *
 * Args: entity, item_type, item_ptr (NULL = find first of type).
 *
 *   1. Resolve target: if item_ptr given, use it. Else walk
 *      entity->[4] (inventory head, .next at +6) looking for
 *      a node whose byte 0 matches item_type. Bail if no match.
 *   2. If target->[5] is set (equip bit), call L77a0 to run the
 *      "remove equipped item" side-effects.
 *   3. Unlink target from the chain: rewire head or predecessor's
 *      .next over it.
 *   4. JT[471] frees the slot in g_a5_21152 (the inventory bucket).
 *   5. Type-specific cleanup: 14 → L1b14(entity, 5);
 *      12 or 38 → L1b14(entity, 0); 68 → L1b14(entity, 1) + (entity, 2).
 *
 * Used by the engine's "drop item / sell / lose" paths — every
 * inventory removal across the game flows through here. */
static void   jt878(long entity_long, short item_type, long item_long)
{
	unsigned char *entity = (unsigned char *)(uintptr_t)entity_long;
	unsigned char *target = (unsigned char *)(uintptr_t)item_long;
	unsigned char  type   = (unsigned char)(item_type & 0xff);

	PROBE("jt878");
	if (entity == NULL)
		return;

	if (target == NULL) {
		target = *(unsigned char * const *)(entity + 4);
		while (target != NULL && target[0] != type)
			target = *(unsigned char * const *)(target + 6);
	}
	if (target == NULL)
		return;

	if (target[5] != 0)
		l77a0(item_type, entity, target, 1);

	/* Unlink from the entity's inventory chain. */
	{
		unsigned char *head =
			*(unsigned char * const *)(entity + 4);
		if (head == target) {
			*(unsigned char **)(entity + 4) =
				*(unsigned char * const *)(target + 6);
		} else {
			unsigned char *prev = head;
			while (prev != NULL &&
			       *(unsigned char * const *)(prev + 6) != target)
				prev = *(unsigned char * const *)(prev + 6);
			if (prev != NULL)
				*(unsigned char **)(prev + 6) =
					*(unsigned char * const *)(target + 6);
		}
	}

	jt471((long)(uintptr_t)target, 10, &g_a5_21152);

	if (type == 14)
		l1b14(entity, 5);
	if (type == 12 || type == 38)
		l1b14(entity, 0);
	if (type == 68) {
		l1b14(entity, 1);
		l1b14(entity, 2);
	}
}
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

/* JT[92] (CODE 6 + 0x4bac, 22 sites) — public alias for L4bac
 * (scroll-advance helper jt42 reaches in its message chain). */
static void jt92(void) __attribute__((unused));
static void jt92(void)
{
	PROBE("jt92");
	l4bac();
}

/* L2f4c (CODE 6 + 0x2f4c) — entity-has-second-stat predicate jt40
 * branches on. PROBE for now; bit-pattern reader for the entity's
 * class table. */
static int l2f4c(const void *entity) __attribute__((unused));
static int l2f4c(const void *entity)
{
	PROBE("l2f4c");
	(void)entity;
	return 0;
}

/* JT[40] (CODE 6 + 0x2fd8, 38 sites) — max of two stat bytes.
 *
 * Reads entity[157 + slot] as the primary stat; when L2f4c says
 * the entity has a multi-class second stat, reads entity[164 + slot]
 * too; returns the larger of the two. The lift is faithful but
 * L2f4c stays PROBE → returns 0 → secondary stat treated as 0,
 * so for now jt40 always returns the primary byte. */
static unsigned char jt40(void *entity, short slot) __attribute__((unused));
static unsigned char jt40(void *entity, short slot)
{
	const unsigned char *e = (const unsigned char *)entity;
	unsigned char stat1, stat2;

	PROBE("jt40");
	if (e == NULL)
		return 0;
	stat1 = e[157 + (slot & 0xff)];
	stat2 = l2f4c(entity) ? e[164 + (slot & 0xff)] : 0;
	return (stat1 > stat2) ? stat1 : stat2;
}

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
