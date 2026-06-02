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
#include "files.h"            /* FSOpen / FSRead (jt398 file-open chain) */
#include "quickdraw.h"        /* MoveTo, DrawString, GetPort (jt1089) */
#include "events.h"           /* WaitNextEvent (jt1125 event poll)   */
#include "windows.h"          /* InvalRect (L71ac osEvt arm)         */
#include "menus.h"            /* MenuKey (L6dd0 keyDown arm)         */
#include "input.h"            /* plat_kb_poll (port_play_demo)        */
#include "resources.h"        /* GetResource (clut 129 for colour art) */

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

/* L322c (CODE 3 + 0x322c) — path-type classifier. Returns 0 for a
 * normal HFS path; non-zero only for the '#vol:...' search-path
 * syntax (skip optional '-', scan an alnum volume token, require a
 * trailing ':'). The Mac then resolves the named search volume via
 * L39f0; the port has no multi-volume search path, so a '#' path
 * just returns 1 — jt398 reacts by stripping to the bare filename
 * (l31fc), which is the sensible port behaviour. Our boot paths use
 * ':DISKn:FILE' (no '#'), so this returns 0 and the full path flows
 * through to FSOpen (which strips the HFS prefix itself). */
static short l322c(const char *path)
{
	const char *p;

	PROBE("l322c");
	if (path == NULL || path[0] != '#')
		return 0;
	p = path + 1;
	if (*p == '-')
		p++;
	while (*p != 0 && ((*p >= '0' && *p <= '9')
	                || (*p >= 'A' && *p <= 'Z')
	                || (*p >= 'a' && *p <= 'z')))
		p++;
	if (*p == ':' && p > path + 1) {
		PROBE("l322c:search-path");
		return 1;
	}
	return 0;
}

/* L31fc (CODE 3 + 0x31fc) — strip a path to the bare filename after
 * the last ':'. Walk to the NUL, then back to just past the last
 * ':' (or the start if none). */
static const char *l31fc(const char *path)
{
	const char *p;

	PROBE("l31fc");
	if (path == NULL)
		return path;
	p = path;
	while (*p != 0)
		p++;
	while (p > path && p[-1] != ':')
		p--;
	return p;
}

/* L45d6 (CODE 3 + 0x45d6) — C string -> Pascal string. dst[0] =
 * length, dst[1..] = bytes. */
static void  l45d6(char *dst, const char *src)
{
	unsigned char  len = 0;
	char          *out;

	PROBE("l45d6");
	if (dst == NULL || src == NULL)
		return;
	out = dst + 1;
	while (*src != 0 && len < 255) {
		*out++ = *src++;
		len++;
	}
	dst[0] = (char)len;
}

/* L328e (CODE 3 + 0x328e) — open a file. The Mac builds a PBOpen
 * param block and calls JT[1041] (PBOpenSync); the port routes
 * straight to the compat FSOpen, which translates the HFS path to
 * GEMDOS and opens it. `buf` is the Pascal string l45d6 built.
 * Returns the file refnum on success, -2 on a permission error
 * (Mac permErr -54), -1 on any other failure — matching the Mac's
 * -1 / -2 / refnum return contract. */
static short l328e(const char *buf, short vRefNum, short flags)
{
	short  ref = 0;
	OSErr  err;

	PROBE("l328e");
	(void)flags;                  /* Mac perm = flags+1; FSOpen self-selects */
	if (buf == NULL)
		return -1;
	err = FSOpen((ConstStr255Param)buf, vRefNum, &ref);
	if (err != noErr)
		return (err == (OSErr)-54) ? (short)-2 : (short)-1;
	return ref;
}

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
	status = l328e(buf, 0, flags);
#ifdef FRUA_ENGINE_PROBE
	dbg_log_num("jt398: open refnum = ", status);
#endif
	return status;
}
/* JT[411] (CODE 3 + 0x3de2) — close a file. Mac body builds a
 * 50-byte PBClose param block (ioRefNum = arg) and calls JT[1042]
 * (PBClose via the File Manager); returns 0 on success, -1 on
 * error. The port routes to the compat FSClose. Paired with jt398
 * (open): ua_main opens :DISK4:ALWAYS.CTL then closes it here, and
 * jt127 closes each design file after reading it.
 *
 * (Earlier mislabelled an "error status reporter" — the param-block
 * + JT[1042] shape is PBClose, confirmed against jt127's open/read/
 * close sequence.) */
static short jt411(short refNum)                   /* CODE 3 + 0x3de2 */
{
	PROBE("jt411");
	return (FSClose(refNum) == noErr) ? (short)0 : (short)-1;
}
/* jt480 is the string-table setter — lifted in str.c; ua_main forwards
 * its own arg1 / arg2 here so the THINK C runtime's (count, table) flows
 * in. PROBE-instrumented inside str.c if needed for tracing; the engine
 * call path here just uses the real symbol. */
/* JT[445] (CODE 3 + 0x294e) — Mac body is empty (just rts).
 * Genuinely a no-op in the original; PROBE label for tracing
 * which paths reach it. */
static void  jt445(void)                           { PROBE("jt445"); }            /* CODE 3 + 0x294e */
/* JT[415] (CODE 3 + 0x37da) — wraps _ExitToShell. The Mac call
 * tears down the application and returns to Finder. No Atari
 * equivalent in the compat shim yet; treat as a no-op so boot
 * code doesn't accidentally exit. Firing this in production
 * should call exit() / Pterm0() once the shutdown path lifts. */
static void  jt415(short a)                        { PROBE("jt415"); (void)a; }    /* CODE 3 + 0x37da */
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
/* jt127 (design-data loader) + jt132 (file-group setter) lift
 * further down / just below; jt361 calls them. */
static void jt127(const char *prefix, short num, short *out, void *buffer);

/* JT[387] (CODE 3 + 0x36bc) — allocate a heap block. Mac:
 * JT[1028](size, 0) == _NewPtr. The port routes to the compat
 * NewPtr. Returns the block address (0 on failure). */
static long  jt387(short size)
{
	PROBE("jt387");
	return (long)(uintptr_t)NewPtr((Size)(unsigned short)size);
}

/* JT[211] (CODE 7 + 0x57bc) — allocate the 3746-byte design-state
 * buffer, park it in g_a5_-12300, and clear its first short. The
 * Mac init path (CODE 6 L4cc0) calls this; the port also calls it
 * lazily from jt361 when the buffer hasn't been allocated yet. */
static void  jt211(void)
{
	PROBE("jt211");
	g_a5_long(-12300) = jt387((short)3746);
	if (g_a5_long(-12300) != 0)
		*(short *)(uintptr_t)g_a5_long(-12300) = 0;
}

/* L6e50 (CODE 8 + 0x6e50) — g_a5_-10374 = clamp(arg, 0, 40). The
 * Mac uses JT[413] (min) + JT[397] (max); inlined here as a plain
 * clamp to avoid forward-declaring those (identical result). */
static void  l6e50(short arg)
{
	short v = arg;

	PROBE("L6e50");
	if (v > 40) v = 40;
	if (v < 0)  v = 0;
	g_a5_byte(-10374) = (unsigned char)v;
}

/* L7222 / JT[369] (CODE 8 + 0x7222) — design-header post-load step:
 *   L6e50(g_a5_-18828);            // active level/page, clamped 0..40
 *   *(short *)g_a5_-12300 = 0;      // reset the design-state cursor
 * g_a5_-18828 is a byte from the just-loaded GAME header. */
static void  l7222(void)
{
	PROBE("L7222");
	l6e50((short)(signed char)g_a5_byte(-18828));
	if (g_a5_long(-12300) != 0)
		*(short *)(uintptr_t)g_a5_long(-12300) = 0;
}

/* JT[132] (CODE 6 + 0x0092) — set the current file-group id.
 *   g_a5_-31236 = (byte)id;
 * The group id (51 / 0x33 for GAME) tags subsequent file-cache
 * registrations; jt361 sets it before loading the GAME header. */
static void  jt132(short id)
{
	PROBE("jt132");
	g_a5_byte(-31236) = (unsigned char)(id & 0xff);
}

/* ============================================================
 * GEO content-load subsystem (CODE 5/6/7)
 *
 * jt198 loads a design's GEOnnn.DAT (the level/wilderness map +
 * its string table) and parses the IFF-style container into the
 * live design-state buffer. The chunk layout, validated against
 * the tutorial's GEO040.DAT, is:
 *
 *   'FORM' <size>                       (no formType — FRUA quirk)
 *     'AMOD' <size = filesize-16>
 *       'HDR ' <0x122>   -> design-state[0..289]
 *       'MAP ' <0xd80>   -> design-state[290..]
 *       'ENCR' <0x7d0>   -> g_a5_-13038 (NCR buffer)
 *       'STRG' <0x1c00>  -> g_a5_-13034 (string table)
 *
 * The 4-byte chunk sizes are big-endian on disk (read directly on
 * the big-endian 68k); a handful of HDR fields at +272 are stored
 * byte-swapped and flipped back via jt1180. See docs/decompilation.md.
 * ============================================================ */

/* forward decls — these are defined further down in this file */
static void  jt399(void *buf, short size, short fill);
static void  jt406(void *dst, const void *src, short count);
static short jt1180(short v);
static void  jt131(short a);
static void  party_step(short cmd);          /* play-loop movement (defined late) */
static const char *jt488(const char *fmt, ...);
static short jt393(const char *a, const char *b);
static int   jt394(char *buf, const char *fmt, ...);
static int   jt1200(void);
static void  l30cc(short n);

/* JT[421] (CODE 3 + 0x36d6) — NewPtr(size). JT[1028](size,0) ==
 * _NewPtr; returns the block (0 on failure). Unlike jt387 this
 * takes a long size (the GEO buffer is 37888 bytes). */
static long  jt421(long size)
{
	PROBE("jt421");
	return (long)(uintptr_t)NewPtr((Size)size);
}

/* JT[1002] (CODE 5 + 0x2822) — allocate the GEO read buffer into
 * g_a5_-4582. Mac aborts via L036a("Out of memory in DBInit") on
 * failure; that alert chain is deferred (we just leave the slot
 * null and let the caller's jt69 path report it). */
static void  jt1002(long size)
{
	PROBE("jt1002");
	g_a5_long(-4582) = jt421(size);
}

/* L4ab6 (CODE 7 + 0x4ab6) — point the string-table cursors at a
 * (possibly new) base. Cached so repeated loads of the same table
 * skip the re-seed. */
static void  l4ab6(void *base)
{
	PROBE("L4ab6");
	if ((void *)(uintptr_t)g_a5_long(-12322) != base) {
		g_a5_byte(-12324) = 0;
		g_a5_long(-12322) = (long)(uintptr_t)base;
		g_a5_long(-12304) = (long)(uintptr_t)base;
		g_a5_long(-12314) = (long)(uintptr_t)((char *)base + 6);
		g_a5_long(-12318) = (long)(uintptr_t)((char *)base + 406);
	}
}

/* L4db4 (CODE 7 + 0x4db4) — set up the string-table region: the
 * 6-byte header (count@0, -1@2, 0@4) followed by a 400-byte index
 * at +6, then a zero-filled body. `total` is the region size
 * (7168). Returns 1. */
static char  l4db4(void *base, short total)
{
	short i;
	char *p, *q;

	PROBE("L4db4");
	g_a5_long(-12304) = (long)(uintptr_t)base;
	g_a5_long(-12314) = (long)(uintptr_t)((char *)base + 6);
	g_a5_long(-12318) = (long)(uintptr_t)((char *)base + 406);
	jt399((void *)(uintptr_t)g_a5_long(-12318), (short)(total - 406), (short)0);
	p = (char *)(uintptr_t)g_a5_long(-12304);
	*(short *)p = 0;
	*(short *)(p + 2) = -1;
	*(short *)(p + 4) = 0;
	q = (char *)(uintptr_t)g_a5_long(-12314);
	for (i = 0; i < 400; i++)
		q[i] = 0;
	*(short *)(uintptr_t)g_a5_long(-12304) = (short)(total - 406);
	return 1;
}

/* L4e3a (CODE 7 + 0x4e3a) — re-point the cursors at `base` (L4ab6)
 * then byte-swap the 3-word string-table header into native order. */
static void  l4e3a(void *base)
{
	short *p;

	PROBE("L4e3a");
	l4ab6(base);
	p = (short *)(uintptr_t)g_a5_long(-12304);
	p[0] = jt1180(p[0]);
	p[1] = jt1180(p[1]);   /* +2 */
	p[2] = jt1180(p[2]);   /* +4 */
}

/* JT[231] (CODE 7 + 0x0004) — allocate the per-design NCR (2000 B,
 * g_a5_-13038) and string-table (7168 B, g_a5_-13034) buffers and
 * initialise them. Part of the design subsystem bring-up (L4cc0). */
static void  jt231(void)
{
	PROBE("jt231");
	g_a5_long(-13038) = jt387((short)2000);
	g_a5_long(-13034) = jt387((short)7168);
	jt399((void *)(uintptr_t)g_a5_long(-13038), (short)2000, (short)0);
	l4db4((void *)(uintptr_t)g_a5_long(-13034), (short)7168);
}

/* JT[1004] (CODE 5 + 0x2850) — return the GEO read buffer pointer. */
static long  jt1004(void)
{
	PROBE("jt1004");
	return g_a5_long(-4582);
}

/* JT[69] (CODE 6 + 0x5f66) — content-load error reporter. The Mac
 * body runs the disk-error alert + cleanup chain (jt461 / jt1081 /
 * jt415); deferred to a PROBE stub. A failed GEO parse lands here. */
static void  jt69(void)
{
	PROBE("jt69");
}

/* L7470 (CODE 7 + 0x7470) — IFF chunk walker. `*walker` is the
 * read cursor; advances it chunk by chunk looking for `fourcc`,
 * descending into 'FORM' containers. FRUA FORMs carry no formType,
 * so a FORM whose first inner id is the AMOD wrapper is stepped
 * into transparently. Returns the matched chunk's data size (and
 * leaves *walker at its data), or -1 if not found. */
static long  l7470(unsigned char **walker, long fourcc)
{
	long remaining, chunkID, chunkSize, formType;
	signed char pad;

	PROBE("L7470");
	if (walker == 0 || *walker == 0)
		return -1;
	remaining = 0x7fffffffL;
	for (;;) {
		chunkID = *(long *)(*walker);
		if (chunkID == 0)
			return -1;
		*walker += 4;
		chunkSize = *(long *)(*walker);
		if (chunkSize == 0)
			return -1;
		*walker += 4;
		pad = (signed char)(chunkSize & 1);
		chunkSize += pad;
		if (chunkID == fourcc)
			return chunkSize - pad;
		if (chunkID == 0x464F524DL /* 'FORM' */) {
			formType = *(long *)(*walker);
			if (formType == 0)
				return -1;
			*walker += 4;
			if (formType == 0x414D4F44L /* 'AMOD' */) {
				if (fourcc == 0x414D4F44L) {
					*walker -= 4;   /* re-read AMOD as a chunk */
				} else {
					chunkSize -= 4;
					if (chunkSize >= 0)
						*walker += 4;
					remaining = chunkSize - 8;
				}
			} else {
				remaining -= (chunkSize + 8);
				chunkSize -= 4;
				if (chunkSize >= 0)
					*walker += chunkSize;
			}
		} else {
			remaining -= (chunkSize + 8);
			*walker += chunkSize;
		}
		if (remaining <= 0)
			return -1;
	}
}

/* L7406 (CODE 7 + 0x7406) — find `fourcc` via L7470, verify it is
 * exactly `size` bytes, BlockMove it to `dst`, and step *walker
 * past it (plus any odd-byte pad). Returns 1 on success, 0 if the
 * chunk is missing or the wrong size. */
static char  l7406(unsigned char **walker, void *dst, long fourcc, long size)
{
	long pad;

	PROBE("L7406");
	if (walker == 0 || *walker == 0)
		return 0;
	pad = size & 1;
	if (l7470(walker, fourcc) != size)
		return 0;
	jt406(dst, *walker, (short)(size & 0xffff));
	*walker += size;
	if (pad)
		*walker += 1;
	return 1;
}

/* L7226 (CODE 7 + 0x7226) — parse a GEO container into the
 * design-state buffer (g_a5_-12300). Returns 1 on success, 0 on a
 * malformed/oversize file. A buffer whose first long isn't an IFF
 * id ('FORM'/'CAT '/'LIST') is treated as raw legacy data (-> 1).*/
static short l7226(unsigned char *buf)
{
	unsigned char *cur = buf;
	long firstID;
	char *ds;
	short i;
	short dims;

	PROBE("L7226");
	if (buf == 0)
		return 0;
	jt399((void *)(uintptr_t)g_a5_long(-12300), (short)3746, (short)0);
	firstID = *(long *)cur;
	if (firstID != 0x464F524DL /* FORM */ &&
	    firstID != 0x43415420L /* 'CAT ' */ &&
	    firstID != 0x4C495354L /* LIST */)
		return 1;
	if (l7470(&cur, 0x414D4F44L /* AMOD */) != 12946)
		return 0;
	ds = (char *)(uintptr_t)g_a5_long(-12300);
	if (!l7406(&cur, ds, 0x48445220L /* 'HDR ' */, 0x122L))
		return 0;
	if (*(unsigned short *)ds < 100 || *(unsigned short *)ds > 106)
		return 0;
	for (i = 0; i < 8; i++)
		((short *)(ds + 272))[i] = jt1180(((short *)(ds + 272))[i]);
	dims = (short)((unsigned char)ds[2] * (unsigned char)ds[3]);
	if (dims <= 0 || dims > 576)
		return 0;
	if (!l7406(&cur, ds + 290, 0x4D415020L /* 'MAP ' */, 0xd80L))
		return 0;
	if (!l7406(&cur, (void *)(uintptr_t)g_a5_long(-13038),
	           0x454E4352L /* 'ENCR' */, 0x7d0L))
		return 0;
	if (!l7406(&cur, (void *)(uintptr_t)g_a5_long(-13034),
	           0x53545247L /* 'STRG' */, 0x1c00L))
		return 0;
	/* scan the string table: if it's all-NUL, re-seed the region */
	{
		unsigned char *p = (unsigned char *)(uintptr_t)g_a5_long(-13034);
		short cnt = 0;
		while (cnt < 7168 && *p == 0) {
			p++;
			cnt++;
		}
		if (cnt >= 7168)
			l4db4((void *)(uintptr_t)g_a5_long(-13034), (short)7168);
		l4e3a((void *)(uintptr_t)g_a5_long(-13034));
	}
	return 1;
}

/* L720a (CODE 7 + 0x720a) — thin wrapper: 0 if the GEO parse
 * succeeded, -1 (error) otherwise. */
static short l720a(unsigned char *buf)
{
	PROBE("L720a");
	return l7226(buf) ? 0 : -1;
}

/* JT[198] (CODE 7 + 0x7124) — load GEOnnn.DAT for the given level
 * number. Reads the file into the GEO buffer (jt1004), checks the
 * byte count against the fixed 12962-byte GEO size, parses it into
 * design state (L720a -> L7226), then records the level number in
 * the design header (g_a5_-28006[19]) and resets the load flags. */
static void  jt198(short geo_num)
{
	unsigned char *buf;
	short out = 0;
	short expected;
	char *hdr;
	char *ds;

	PROBE("jt198");
	jt132((short)51);
	jt131((short)6);
	buf = (unsigned char *)(uintptr_t)jt1004();
	jt127("GEO", geo_num, &out, buf);
	expected = 12962;
	if (expected & 1)
		expected++;
	if (out != expected)
		jt69();
	else if (l720a(buf) < 0)
		jt69();
	jt131((short)0);
	hdr = (char *)(uintptr_t)g_a5_long(-28006);
	if (hdr)
		hdr[19] = (char)(geo_num & 0xff);
	ds = (char *)(uintptr_t)g_a5_long(-12300);
	if (ds && !(ds[7] & 1))
		g_a5_byte(-12290) = 0;
	g_a5_word(-12296) = -1;
}

/* L0b88 (CODE 20 + 0x0b88) — set "look/edit" play mode: clear the
 * player record's in-play flag (offset 34), set offset 36, and put
 * the play-state byte g_a5_-27990 = 3. */
static void  l0b88(void)
{
	unsigned char *p = (unsigned char *)g_a5_28006;

	PROBE("L0b88");
	if (p != NULL) {
		p[34] = 0;
		p[36] = 1;
	}
	g_a5_27990 = 3;
}

/* L0ba2 / JT[952] (CODE 20 + 0x0ba2) — set "in-play" mode: player
 * record offset 34 = 1, offset 36 = 0, play-state g_a5_-27990 = 4. */
static void  l0ba2(void)
{
	unsigned char *p = (unsigned char *)g_a5_28006;

	PROBE("L0ba2");
	if (p != NULL) {
		p[34] = 1;
		p[36] = 0;
	}
	g_a5_27990 = 4;
}

/* L0bbc (CODE 20 + 0x0bbc) — ENTER A LEVEL. The bridge from the
 * lifted GEO loader into the adventure runtime: set the file group,
 * load the current level's GEO map (g_a5_-18878) via jt198, then
 * place the party. On a fresh entry the party start (x,y,facing) is
 * read from the map's start tile at design-state + g_a5_-18488*4
 * (bytes 15/14/16&7); on a resume it is restored from the player
 * record. The live party globals are g_a5_-12288 (x) / -12287 (y) /
 * -12286 (facing); for levels <= 4 they are also copied into the
 * player record (offsets 37/38 map, 67/68 saved, 23/24). Finishes by
 * selecting play vs look mode (L0ba2 / L0b88) by level number. */
static void  l0bbc(void)
{
	unsigned char *p = (unsigned char *)g_a5_28006;

	PROBE("L0bbc");
	jt132((short)51);

	if (p != NULL && p[134] != 0) {
		/* resume: restore the party position from the record */
		if (g_a5_18485 == 0) {
			g_a5_12287 = p[68];
			g_a5_12288 = p[67];
			g_a5_12286 = p[17];
		}
	} else {
		/* fresh entry */
		if (g_a5_18485 == 0) {
			unsigned char *ds;

			jt198((short)g_a5_18878);          /* load the level map */
			ds = (unsigned char *)(uintptr_t)g_a5_long(-12300);
			if (ds != NULL) {
				unsigned char *st =
					ds + (((long)(unsigned char)g_a5_18488) << 2);
				g_a5_12288 = st[15];               /* party X      */
				g_a5_12287 = st[14];               /* party Y      */
				g_a5_12286 = (unsigned char)(st[16] & 7); /* facing */
			}
		}
		if (g_a5_18878 <= 4 && p != NULL) {
			p[37] = g_a5_12288; p[38] = g_a5_12287;
			p[67] = g_a5_12288; p[68] = g_a5_12287;
			p[23] = g_a5_12288; p[24] = g_a5_12287;
		}
	}

	if (g_a5_18878 <= 4)
		l0b88();
	else
		l0ba2();
}

/* L3154 (CODE 6 + 0x3154) — allocate the STRG load scratch buffer:
 * g_a5_-21152 = arg, g_a5_-21150 = 10, g_a5_-21148 = NewPtr(arg*10).
 * Called from L4cc0 with arg=400 (a 4000-byte buffer). */
static void  l3154(short arg)
{
	PROBE("L3154");
	g_a5_word(-21152) = arg;
	g_a5_word(-21150) = 10;
	g_a5_long(-21148) = jt387((short)(arg * 10));
}

/* L4cc0 (CODE 6 + 0x4cc0) — design subsystem bring-up: allocate
 * every per-design buffer at design open.  Lifted as a STRUCTURAL
 * SKELETON — the GEO content path's buffers are allocated for real
 * (GEO read buffer, design header, NCR/STRG, design state); the
 * unrelated combat/sprite tables (g_a5_-27944/-27920/-22306/-25318
 * and the L30cc/L311c/L3144/L3154/L531e/L59ca/L30f4/L317c/L31a4
 * sub-allocators) are DEFERRED until their consumers are lifted. */
static void  l4cc0(void)
{
	PROBE("L4cc0");
	jt1002((long)0x9400);                       /* GEO buffer (37888 B) -> -4582 */
	/* jt442(40) — DEFERRED */
	g_a5_long(-28006) = jt387((short)1024);     /* design header (1024 B) */
	if (g_a5_long(-28006) != 0)
		g_a5_long(-28006) -= 1;             /* THINK C 1-based: store ptr-1 */
	jt231();                                    /* NCR + string-table buffers */
	/* jt387(2064)->-27944; jt387(4590)->-27920 — DEFERRED */
	l30cc((short)8);                            /* record staging buffer -> -22208 */
	/* L311c(640); L3144(200) — DEFERRED */
	l3154((short)400);                          /* STRG scratch buffer -> -21148 */
	jt211();                                    /* design-state buffer -> -12300 */
	/* L59ca(); L531e(2738)->-22306; L531e(1260)->-25318;
	 * L30f4(60); L317c(70); L31a4(68) — DEFERRED */
}

/* JT[363] (CODE 8 + 0x5f04) — load STRGnnn.DAT (a phrase table) for
 * level `num`, returning its byte size (the on-disk count word is
 * little-endian; size == (count+1)*14, 14 bytes per phrase record).
 *
 * The most-recently-loaded table is cached at g_a5_-10370 keyed by
 * its 6-byte "STR@<n>" tag (built with jt488 / compared with jt393):
 * a repeat request for the same table returns the cached size with
 * no file I/O. On a miss the table loads fresh into the scratch
 * buffer (g_a5_-21148, allocated by L3154), the data sits 6 bytes
 * past the tag, and the load is size-checked against (count+1)*14.
 *
 * `out` (optional) receives the cached table pointer on success. */
static short jt363(long *out, short num)
{
	char *cached;
	char *buf;
	short got = 0;

	PROBE("jt363");
	if (out != 0 && out != (long *)&g_a5_long(-10370))
		*out = 0;

	cached = (char *)(uintptr_t)g_a5_long(-10370);
	if (cached != 0) {
		const char *key = jt488("%3s@%1d", "STR", (int)num);
		if (jt393(cached - 6, key) == 0) {
			if (out != 0)
				*out = g_a5_long(-10370);
			return (short)(((unsigned char)cached[0] + 1) * 14);
		}
	}

	buf = (char *)(uintptr_t)g_a5_long(-21148);
	jt394(buf, "%3s@%1d", "STR", (int)num);
	buf += 6;
	g_a5_long(-10370) = (long)(uintptr_t)buf;
	jt132((short)51);
	jt127("STRG", num, &got, buf);
	if (buf != 0) {
		short count = (short)((((unsigned char)buf[1]) << 8) |
		                      (unsigned char)buf[0]);
		short expected = (short)((count + 1) * 14);
		if (expected != got)
			return 0;
		if (out != 0)
			*out = g_a5_long(-10370);
		return got;
	}
	return 0;
}

/* JT[1171] (CODE 4 + 0x108e) — record decompressor. Expands a
 * compressed MONST record (src) of `rawlen` uncompressed bytes
 * into dst. Deferred to a PROBE stub: the only MONST files we can
 * test (HEIRS.DSN) are stored at the full 450-byte size, so the
 * decompress arm isn't exercised. Lift when a compressed module
 * needs it. */
static void  jt1171(void *src, void *dst, short rawlen)
{
	PROBE("jt1171");
	(void)src; (void)dst; (void)rawlen;
}

/* L6028 (CODE 10 + 0x6028) — load MONSTnnn.DAT for monster slot
 * `num` into the design header (g_a5_-28006 + 101). Mirrors the GEO
 * loader's shape: file group 50, byte-count gate (1..450). Records
 * are stored either at the full 450 bytes (used as-is) or RLE-
 * packed at < 450 (relocated to the buffer tail and expanded back
 * via jt1171, where dst[0..1] is the big-endian uncompressed
 * length). On success the slot is hung off the active monster
 * context (g_a5_-11718) and tagged with `num`. Returns the record
 * pointer, or 0 on a load error. */
static long  l6028(short num)
{
	short out = 0;
	char *dest;
	char ok = 0;

	PROBE("L6028");
	jt132((short)50);
	jt131((short)6);
	dest = (char *)(uintptr_t)g_a5_long(-28006) + 101;
	jt127("MONST", num, &out, dest);
	if (out == 0 || out > 450) {
		jt69();
	} else {
		if (out < 450) {
			/* compressed: relocate the packed bytes to the
			 * buffer tail, then expand back over `dest`. */
			short rawlen = (short)(((unsigned char)dest[0] << 8) |
			                       (unsigned char)dest[1]);
			char *tail = dest - out + 490;
			jt406(tail, dest + 2, out);
			jt1171(tail, dest, rawlen);
		}
		if (g_a5_long(-11718) != 0) {
			char *ctx = (char *)(uintptr_t)g_a5_long(-11718);
			*(long *)(ctx + 10) = (long)(uintptr_t)dest;
			((char *)(uintptr_t)(*(long *)(ctx + 10)))[397] =
				(char)(num & 0xff);
		}
		ok = 1;
	}
	return ok ? (long)(uintptr_t)dest : 0;
}

/* JT[370] (CODE 8 + 0x6ed2) — default-stat / attribute lookup,
 * keyed by a class/type byte (arg) with a JT[3] switch (1..14) and
 * two flag bits (bit7 if mode>0, bit6 if arg<6). Deferred to a
 * PROBE stub returning 0 — jt263 only feeds the result into the
 * high byte of a geo packing word, harmless while the monster
 * editor flow is itself deferred. */
static short jt370(short arg, short mode)
{
	PROBE("jt370");
	(void)arg; (void)mode;
	return 0;
}

/* L30cc (CODE 6 + 0x30cc) — allocate the record staging buffer:
 * g_a5_-22212 = n, g_a5_-22210 = 398, g_a5_-22208 = NewPtr(n*398).
 * Called from L4cc0 with n=8 (a 3184-byte buffer); JT[325] stages
 * records here before writing them. */
static void  l30cc(short n)
{
	PROBE("L30cc");
	g_a5_word(-22212) = n;
	g_a5_word(-22210) = 398;
	g_a5_long(-22208) = jt387((short)(n * 398));
}

/* JT[134] (CODE 6 + 0x0138) — set the file-group sub-id byte
 * (g_a5_-31235). Paired with jt132's group id. */
static void  jt134(short b)
{
	PROBE("jt134");
	g_a5_byte(-31235) = (unsigned char)(b & 0xff);
}

/* JT[113] (CODE 6 + 0x338c) — set the record file format byte
 * (g_a5_-18396): the caller's value when the platform probe
 * (jt1200) reports 3, else the default 52. */
static void  jt113(short b)
{
	PROBE("jt113");
	if (jt1200() == 3)
		g_a5_byte(-18396) = (unsigned char)(b & 0xff);
	else
		g_a5_byte(-18396) = 52;
}

/* JT[1014] (CODE 5 + 0x36a4) — register a named file in a group's
 * cache, resolving it against the shared .GLB libraries. Deferred
 * to a PROBE stub: the JT[325] prologue's data path uses the
 * pre-allocated staging buffer (g_a5_-22208) directly and does not
 * depend on this registration's side effects. */
static void  jt1014(short group, const char *name, short size)
{
	PROBE("jt1014");
	(void)group; (void)name; (void)size;
}

/* JT[325] (CODE 9 + 0x22d8) — the design-record database engine:
 * stage / load / store a fixed-size record (monsters, items, map
 * tiles, ...) keyed by a record type (1/21/33/51/52) and a command
 * (0..7). Called 8x across CODE 2/10/11.
 *
 * Lifted PARTIALLY — the prologue + input command class only:
 *   - file-group setup (jt131/jt134/jt113/jt1014);
 *   - point the staging cursor at the record buffer (g_a5_-11660 =
 *     g_a5_-22208, the L30cc block) and grab the field buffer
 *     (g_a5_-11656 = jt1004);
 *   - cmd 2/5/6/7: BlockMove the caller's source into the staging
 *     buffer and stamp the record tag (rec[20..22] = 20,50,15);
 *   - cmd 0/1: clear the 450-byte record;
 *   - write the control-block header (type, slot, arg).
 *
 * DEFERRED — the ~3000-byte per-type field-serialization tail
 * (asm 0x242c..0x30c2): the cmd-3 record fetch and the L258e type
 * dispatch that encodes/decodes individual fields across record
 * types. The function therefore stages the raw record but does not
 * yet transform fields; it returns a provisional 0 status (the
 * real status word fp@(-14) is accumulated in the deferred tail).
 *
 * Args mirror the Mac stack: a8 (stored to the control block),
 * rec (caller record ptr), ctrl (control block), type, src (input
 * buffer), cmd, count. */
static short jt325(short a8, long *rec, void *ctrl, short type,
                   void *src, short cmd, short count)
{
	unsigned char *cb = (unsigned char *)ctrl;
	unsigned char *stage;
	short status = 0;            /* fp@(-14) — provisional, see tail */
	short flag20;
	short slot10;

	PROBE("jt325");
	jt131((short)4);
	jt134((short)0);
	jt113((short)51);
	jt1014((short)51, "script", (short)24);
	g_a5_long(-11656) = jt1004();
	g_a5_long(-11660) = g_a5_long(-22208);
	stage = (unsigned char *)(uintptr_t)g_a5_long(-11660);

	if (cmd == 2 || cmd == 5 || cmd == 6 || cmd == 7) {
		stage[20] = 20;
		stage[21] = 50;
		stage[22] = 15;
		jt406(stage, src, count);     /* BlockMove src -> staging */
		stage[2510] = 0;
	} else if (cmd == 0 || cmd == 1) {
		jt399(stage, (short)450, (short)0);
		stage[2510] = 1;
	}

	flag20 = (type < 51) ? 0 : -1;
	*(short *)cb = type;
	if (cmd == 3 || cmd == 4) {
		slot10 = *(short *)(cb + 2);
	} else {
		slot10 = (type < 51) ? 2 : 1;
		if ((type == 51 || type == 52) && *(short *)(cb + 6) != 0) {
			slot10 = *(short *)(cb + 6);
			*(short *)(cb + 6) = 0;
		}
		*(short *)(cb + 4) = 0;
		*(short *)(cb + 8) = a8;
	}

	/* DEFERRED: the field-serialization tail (asm 0x242c..0x30c2,
	 * ~1023 lines through ~40 entries). A trace shows this is not a
	 * data-only codec but the interactive RECORD EDITOR — every one
	 * of the 8 commands flows into one of:
	 *
	 *   L1ae2 (CODE 9 + 0x1ae2, 566 lines) — the field codec. It
	 *     does not just copy fields; it reads each record's field-
	 *     layout *script* and edits fields through the cmd-arg
	 *     stream parser JT[452] (called 6x) plus JT[1012] (GLIB
	 *     glyphs), JT[468], JT[423]. L0052 is its per-field
	 *     descriptor accessor (a JT[3] type switch 50..53 =
	 *     byte/word/.../long over the staging buffer g_a5_-11660).
	 *
	 *   L2626+ — the editor UI: JT[1089] formats the field/"Page
	 *     %2d" strings, JT[155] runs the driver, JT[452] the menus.
	 *
	 * The return status word (fp@(-14)) is itself written in ~10
	 * places across the editor body, so there is no faithful slice
	 * that completes the read/write contract without lifting the
	 * editor. This is a multi-session subsystem arc; the prologue
	 * above stages the raw record into g_a5_-22208, which is the
	 * data-meaningful step the store-direction callers rely on.
	 * Record types seen in the tail: 1, 21, 33, 51, 52. */
	(void)flag20; (void)slot10; (void)rec;

	jt134((short)1);             /* file op end */
	return status;
}

/* JT[263] (CODE 10 + 0x5acc) — monster/NPC setup state machine.
 *
 * Lifted as a STRUCTURAL SKELETON (ADR-0002 level 2). The state
 * dispatch (a JT[1] value-switch on `state`), the per-state field
 * setup, the MONST-load arm (state 8 -> L6028), and the trailing
 * flag-packing switch (a JT[3] on the geo sub-state) are lifted
 * faithfully. The middle NPC-editor + record-serialize block (the
 * `setup_done == 0` path, asm 0x5c1c..0x5e9c) is DEFERRED: it runs
 * the "Re-create NPC?" / "change class?" dialogs and the JT[325]
 * record serializer through ~10 editor entries not yet lifted.
 * States 1 and 8 set setup_done and skip that block, so the lifted
 * paths are complete on their own.
 *
 * `ctx`  (the arg) is the live monster context:
 *   ctx[0]  word  state echo      ctx[2]  byte flag
 *   ctx[3]  byte  record id       ctx[4]  byte packed
 *   ctx[6]  long  -> geo block    ctx[10] long -> monster record
 * `geo`:
 *   geo[10] word  sub-state       geo[12] word packed value
 * `result` (long*) receives packed display flags. Returns the new
 * geo sub-state. */
static short jt263(short state, long *result, void *ctxp)
{
	unsigned char *ctx;
	unsigned char *geo;
	char setup_done = 0;
	char mode = 3;                /* fp@(-21) — serialize mode */

	PROBE("jt263");
	if (ctxp == 0)
		return 0;
	ctx = (unsigned char *)ctxp;
	g_a5_long(-11718) = (long)(uintptr_t)ctx;
	geo = *(unsigned char **)(ctx + 6);

	switch (state) {              /* JT[1] value-switch on the state */
	case 1:                       /* L5b1c — fresh init */
		*(long *)(ctx + 10) = 0;
		setup_done = 1;
		*(short *)(ctx + 0) = state;
		ctx[2] = 1;
		*(short *)(geo + 10) = 8;
		*(short *)(geo + 12) =
			(short)((jt370(3, 0) & 0xff) << 8);
		break;
	case 5:                       /* L5b64 */
		if (*(short *)(geo + 10) == 6)
			state = 6;
		mode = 2;
		break;
	case 8:                       /* L5b80 — load the MONST record */
		if (*result & 0x8000L) {
			short id = (short)(*result & 0xff);
			if (l6028(id) != 0) {
				ctx[3] = (unsigned char)id;
				ctx[4] = (unsigned char)((*result >> 8) & 0x7f);
				if (ctx[2] != 0) {
					state = 1;
					ctx[2] = 0;
				}
				mode = 2;
				break;
			}
		}
		/* load skipped/failed (L5bf8) */
		if (ctx[2] != 0) {
			setup_done = 1;
			*(short *)(geo + 10) = *(short *)(ctx + 0);
		}
		break;
	case 10:                      /* L5c14 — fall straight through */
	case 11:
	default:
		break;
	}

	/* L5c14 */
	if (!setup_done) {
		/* DEFERRED — NPC editor + JT[325] record serialize
		 * (asm 0x5c1c..0x5e9c). Drives the "Re-create NPC?" /
		 * "change class?" dialogs (jt159/jt574/jt556/jt76/
		 * jt557/jt575/jt150/l65be), the design-record field
		 * walks (two JT[471] list loops + the @430 clear +
		 * jt591), and finally JT[325](state, result, geo,
		 * cmd 57|54, rec, mode, 450) to pack the 450-byte
		 * record, with L611c(ctx[3]) on a success result.
		 * Lift when the monster/NPC editor subsystem lands. */
		(void)mode;
	}

	/* L5e9e — pack display flags into *result by the geo sub-state */
	*result &= 0xffff0000L;
	switch (*(short *)(geo + 10)) {          /* JT[3], min=1 max=11 */
	case 1:                                  /* L5ee4 */
		if (ctx[2] == 0) {
			ctx[2] = 1;
			*(short *)(geo + 10) = 8;
			*(short *)(geo + 12) =
				(short)(((jt370(3, 0) & 0xff) << 8) | ctx[3]);
			/* fall through to the case-8 packing */
		} else {
			break;
		}
		/* FALLTHROUGH */
	case 8:                                  /* L5f2a */
		((unsigned char *)result)[4] = (ctx[2] == 0) ? 1 : 0;
		*result |= (long)*(short *)(geo + 12);   /* sign-extend (moveaw) */
		break;
	case 6:                                  /* L5f54 */
		*(short *)(geo + 10) = 5;
		/* FALLTHROUGH */
	case 5: {                                /* L5f5e */
		short g12 = *(short *)(geo + 12);
		short slot;

		*result |= (long)(g12 & 0xff);   /* swap/clrw/swap = byte zero-extend */
		g12 = (short)(g12 >> 8);          /* asrw #8 — arithmetic */
		*(short *)(geo + 12) = g12;
		slot = (short)(g12 - 48);
		if (slot < 32) {
			*result |= ((long)(short)(slot / 4) << 11) | 0x200L;
		} else if ((slot -= 32) < 32) {
			*result |= ((long)(short)(slot / 4) << 11) | 0x300L;
		} else if ((slot -= 32) < 8) {
			*result |= ((long)slot << 11) | 0x400L;
		}
		break;
	}
	case 10:                                 /* L5ed0 */
	case 11:
		*result |= (long)*(short *)(geo + 12);   /* sign-extend (moveaw) */
		break;
	default:                                 /* L6012 */
		*(short *)(geo + 10) = 1;
		break;
	}

	return *(short *)(geo + 10);
}

/* JT[361] (CODE 8 + 0x71ec) — load the design GAME header.
 *
 *   jt132(51);                                   // file group = GAME
 *   jt127("GAME", 1, &n, &g_a5_-18876);          // load GAME001.DAT
 *   if (a) L7222();                               // post-load setup
 *
 * Reads the design's GAME001.DAT (the design index/header — its
 * first bytes are the design name string, e.g. "tutorial design")
 * into g_a5_-18876, the buffer jt315 later paints as "Current Game
 * Design:". Called from ua_main phase 4 as jt361(1).
 *
 * The a!=0 post-step L7222 (jt369) is DEFERRED: it derefs the
 * g_a5_-12300 pointer (clrw **g_a5_-12300) and calls L6e50 (CODE 8
 * design-state init), neither of which is set up in the port yet —
 * running it now would NULL-deref. Guarded behind a non-NULL check
 * so the GAME load itself is safe; restore the post-step when the
 * design-state cluster (g_a5_-12300 / L6e50) lifts. */
static void  jt361(short a)
{
	short n = 0;

	PROBE("jt361");
	jt132((short)51);
	jt127("GAME", (short)1, &n, g_a5_buf(-18876));
#ifdef FRUA_ENGINE_PROBE
	{
		char tag[20];
		int i;
		dbg_log_num("jt361: GAME bytes = ", n);
		tag[0] = ' ';
		for (i = 0; i < 16; i++) {
			unsigned char c = g_a5_buf(-18876)[i];
			tag[i + 1] = (c >= 32 && c < 127) ? (char)c : '.';
		}
		tag[17] = 0;
		dbg_log(tag);   /* expect " tutorial design" */
	}
#endif
	if (a != 0) {
		/* L7222 post-step: clamp the header's level byte into
		 * g_a5_-10374 and reset the design-state cursor.
		 *
		 * The design subsystem buffers (GEO read buffer, design
		 * header, NCR/STRG, design state) are allocated by the
		 * CODE 6 init path L4cc0; run it once before the post-
		 * load step so every slot points at real storage. */
		static int ds_inited;

		if (!ds_inited) {
			l4cc0();
			ds_inited = 1;
		}
		l7222();

#ifdef FRUA_ENGINE_PROBE
		/* TEST: exercise the content loaders now that the design
		 * buffers exist. The default TUTORIAL.DSN staging only
		 * ships GEO040 / STRG003; stage HEIRS.DSN
		 *   make gamedata DSN=HEIRS.DSN
		 * for the multi-GEO + MONST coverage. A missing file
		 * just returns 0 and lands in jt69 (no crash). GEO040
		 * expects ds[0]=106, dims=264; STRG003 = 574; STRG001 =
		 * 3668; MONST101 = 450 (loads raw, no decompress). */
		{
			static const short geos[] = { 1, 2, 40 };
			char *ds = (char *)(uintptr_t)g_a5_long(-12300);
			int i;

			for (i = 0; i < 3; i++) {
				jt198(geos[i]);
				dbg_log_num("jt198 geo = ", geos[i]);
				if (ds) {
					dbg_log_num("  ds[0] = ",
					            *(unsigned short *)ds);
					dbg_log_num("  dims  = ",
					            (unsigned char)ds[2] *
					            (unsigned char)ds[3]);
				}
			}
			dbg_log_num("jt363(3) = ",
			            jt363((long *)0, (short)3));
			dbg_log_num("jt363(3) cached = ",
			            jt363((long *)0, (short)3));
			dbg_log_num("jt363(1) = ",
			            jt363((long *)0, (short)1));
			dbg_log_num("L6028(101) ok = ",
			            l6028((short)101) != 0);
		}
		/* TEST: drive the lifted jt263 state arms against a
		 * synthetic context. State 1 inits (geo sub-state 8,
		 * returns 8); state 8 loads MONST101 via L6028 and
		 * records the id in ctx[3]. The deferred editor block
		 * is skipped (both states set setup_done). */
		{
			static unsigned char tgeo[64];
			static unsigned char tctx[32];
			long res = 0;

			*(unsigned char **)(tctx + 6) = tgeo;
			dbg_log_num("jt263(1) -> ",
			            jt263((short)1, &res, tctx));
			res = 0x8000L | 101;
			tctx[2] = 0;
			dbg_log_num("jt263(8) -> ",
			            jt263((short)8, &res, tctx));
			dbg_log_num("  ctx[3] = ", tctx[3]);
		}
		/* TEST: L0bbc level-entry — the bridge into the adventure
		 * runtime. Loads level 1's GEO map via jt198 and places
		 * the party from the start tile / record. */
		{
			unsigned char *pl = (unsigned char *)g_a5_28006;
			if (pl != NULL)
				pl[134] = 0;          /* fresh-entry path */
			g_a5_18485 = 0;               /* not the editor */
			g_a5_18878 = 1;               /* level 1 */
			g_a5_18488 = 0;
			l0bbc();
			dbg_log_num("L0bbc party x = ", g_a5_12288);
			dbg_log_num("L0bbc party y = ", g_a5_12287);
			dbg_log_num("L0bbc facing  = ", g_a5_12286);
			dbg_log_num("L0bbc mode    = ", g_a5_27990);

			/* TEST: the play-loop movement core — drive a fixed
			 * command sequence and log the party state each step,
			 * so the render-input-move cycle is verifiable without
			 * live input. (cmd: 0 turnL 1 turnR 2 fwd 3 back) */
			{
				static const short seq[6] = { 2, 2, 1, 2, 0, 2 };
				int s;
				g_a5_12286 = (unsigned char)(g_a5_12286 & 6);
				for (s = 0; s < 6; s++) {
					party_step(seq[s]);
					dbg_log_num("  step cmd*1000+x = ",
					            (long)seq[s] * 1000 + g_a5_12288);
					dbg_log_num("       y*100+face = ",
					            (long)g_a5_12287 * 100 + g_a5_12286);
				}
			}
		}
		/* TEST: jt325 cmd-2 record stage. Copy a known 450-byte
		 * source into the staging buffer and confirm the bytes
		 * + record tag (rec[20..22]=20,50,15, rec[2510]=0)
		 * landed in g_a5_-22208. */
		{
			static unsigned char tsrc[450];
			static unsigned char tcb[16];
			unsigned char *stg;
			long res = 0;
			int i;

			for (i = 0; i < 450; i++)
				tsrc[i] = (unsigned char)(i & 0xff);
			jt325((short)0, &res, tcb, (short)57,
			      tsrc, (short)2, (short)450);
#ifdef FRUA_MAP_DEMO
			/* Visualize the last-loaded GEO map (geo 40 above) as
			 * a colored tile grid and hold it on screen for a
			 * screenshot. Blocks here, before the menu paint. */
			/* 3D view + wall-loader byte-depth log. */
			port_play_demo();
			/* hold the snapshot: re-present forever so the
			 * engine's menu paint can't overwrite it (Crawcin
			 * doesn't block under --fast-forward). */
			for (;;)
				qd_present();
#endif
			stg = (unsigned char *)(uintptr_t)g_a5_long(-11660);
			if (stg) {
				/* src[i]=i&0xff was copied over the tag, so
				 * stage[0/200/449] == 0/200/193; rec[2510]
				 * is the post-copy marker (0 = cmd 2/5/6/7). */
				dbg_log_num("jt325: stage[0] = ", stg[0]);
				dbg_log_num("jt325: stage[200] = ", stg[200]);
				dbg_log_num("jt325: stage[449] = ", stg[449]);
				dbg_log_num("jt325: rec2510 = ", stg[2510]);
			}
		}
#endif
	}
}
static void  jt919(void)                           { PROBE("jt919"); }            /* CODE 12 + 0x1b12 */
static int   jt931(void)                           { PROBE("jt931"); return 0; }  /* CODE 12 + 0x430c */
/* JT[949] (CODE 20 + 0x77a2) — Mac body is just `rts`. Genuinely
 * a no-op placeholder hook. */
static void  jt949(void)                           { PROBE("jt949"); }            /* CODE 20 + 0x77a2 */
/* jt315 (CODE 22 + 0x4d8a) — the main menu. Defined after the dialog /
 * DLItem machinery it drives (jt447 / jt452 / l2c60 / jt94 / jt453); the
 * forward declaration lets ua_main's play loop call it. */
static int   jt315(void);

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

void port_play_demo(void);              /* interactive 3D-view demo (FRUA_3D_DEMO) */

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
#ifdef FRUA_3D_DEMO
	/* Interactive dungeon-walk demo. jt361(1) has run l4cc0 (design
	 * buffers) and l7222, so the play-loop core can load DEMO_LEVEL,
	 * place the party at a corridor vantage, and render the first-person
	 * 3D view (jt312 -> the active renderer). WASD/turn keys walk; never
	 * returns. Opt in with `make EXTRA_CFLAGS=-DFRUA_3D_DEMO ...`; off by
	 * default so the normal boot lands on the engine UI. */
	port_play_demo();
#endif
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
static int           jt574(long ctx);                                            /* CODE 17 + 0x3b5e — char-gen entry, lifted below */
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
	/* TEST SCAFFOLD — forward decl; defined alongside
	 * boot_a5_seed_defaults further down. Remove when
	 * JT[557]/JT[585] land. */
	{
		extern void port_test_seed_design(void);

		PROBE("l07dc");

		if (g_a5_18485 == 0)
			l5124();
		/* l5124 cleared g_a5_-27932; re-point to the
		 * synthesized record so jt904's driver gets real
		 * flags. */
		port_test_seed_design();
	}

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

/* L309c (CODE 5 + 0x309c) = JT[999] — render scaled bitmap into
 * channel.
 *
 * Despite earlier "channel-write" guess, this is a 200+ line
 * scaled-blit dispatcher. Reads four args (target_x, target_y,
 * bitmap_handle, mode), then:
 *
 *   jt1135(arg_x, arg_y, &scaled_x, &scaled_y);   // coord remap
 *   long bytes_needed = L2856(...);                // font/bitmap metric
 *   if (bytes_needed != 0) {
 *       L4d88();                                    // flush invalrect
 *       arg_x -= scaled_x; arg_y -= scaled_y;       // adjust origin
 *       int half = (fp@(-1) & 0x0F) == 9;          // half-pixel mode
 *       if (half) ...                              // half-pixel branch
 *       ... 150+ more lines of pixel-walk, mask,
 *       ... clip-region intersection, color-table
 *       ... lookups, _BlockMove into the page descriptor's
 *       ... bits ptr (from g_a5_-2570[N].entry+2).
 *   }
 *
 * Stays a PROBE stub — the full body needs the engine's font
 * cache + a Falcon-side pixel destination. With jt1001 stubbing
 * the 4 calls in boot to "do nothing," the pixel rendering is
 * deferred to the display HAL phase. The "channel" framing was
 * a misnomer — it's a pixmap blit, not audio. */
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

/* Forward — jt1200 / l2856 / l4d88 / jt1135 / l731e lift further
 * down. L148a / jt995 / jt1132 need them. */
static int  jt1200(void);
static long l2856(long font_handle, short size, void *out_8bytes);
static void l4d88(void);
static void jt1135(short v1, short v2, short *out1, short *out2);
static void l731e(short arg);

/* JT[1198] (CODE 4 + 0x52e) — Mac body returns 1 always.
 * Likely "rows per glyph step" — placeholder hook from the Mac
 * build that the engine reads as a constant. */
static short jt1198(void) __attribute__((unused));
static short jt1198(void)
{
	PROBE("jt1198");
	return 1;
}

/* L21d0 (CODE 5 + 0x21d0, CODE-local) — clip-mask helper used by
 * jt995 to compute a partial-pixel byte mask.
 *
 *   short m = jt1200();
 *   short masked = (signed char)g_a5_-4650[m] & x;
 *   return masked << (3 - m);
 *
 * The 8-byte table at g_a5_-4650..-4643 contains per-mode bit
 * masks. mode 3 (deep) reads from a different slot than modes
 * 0/1. */
static short l21d0(short x) __attribute__((unused));
static short l21d0(short x)
{
	short m, masked;

	PROBE("L21d0");
	m = (short)jt1200();
	masked = (short)(((signed char)g_a5_byte(-4650 + m)) & x);
	return (short)(masked << (3 - m));
}

/* JT[1177] / JT[1183] / JT[1184] / JT[1188] / JT[1189] / JT[1191] —
 * the row-blit primitive family jt995 dispatches into.
 *
 *   jt1177(left, top)                            // begin column
 *   jt1183/jt1188(data, w, h, mask, lmask, rmask) → mode 0/1 vs 3
 *   jt1184/jt1181(data, w, h, mask, lmask, rmask) → mode 0/1 vs 3
 *   jt1189/jt1191(data, src2, w, h, mask, lmask, rmask) → 7 args
 *
 * Each writes a row of pixels into the engine's page-descriptor
 * back buffer (g_a5_-2570[page].entry+2 → pixel ptr). Stubs for
 * now — the Falcon HAL doesn't expose row-blit primitives here;
 * the engine's actual rendering happens via the QuickDraw shim
 * path that jt382 cmd=1 still hits inline. */
static void jt1177(short left, short top) __attribute__((unused));
static void jt1177(short left, short top)
{
	PROBE("jt1177");
	(void)left; (void)top;
}
static char jt1183(long data, short w, short h, short mask,
                   short lmask, short rmask) __attribute__((unused));
static char jt1183(long data, short w, short h, short mask,
                   short lmask, short rmask)
{
	PROBE("jt1183");
	(void)data; (void)w; (void)h; (void)mask; (void)lmask; (void)rmask;
	return 0;
}
static char jt1188(long data, short w, short h, short mask,
                   short lmask, short rmask) __attribute__((unused));
static char jt1188(long data, short w, short h, short mask,
                   short lmask, short rmask)
{
	PROBE("jt1188");
	(void)data; (void)w; (void)h; (void)mask; (void)lmask; (void)rmask;
	return 0;
}
static void jt1181(long data, short w, short h, short mask,
                   short lmask, short rmask) __attribute__((unused));
static void jt1181(long data, short w, short h, short mask,
                   short lmask, short rmask)
{
	PROBE("jt1181");
	(void)data; (void)w; (void)h; (void)mask; (void)lmask; (void)rmask;
}
static void jt1184(long data, short w, short h, short mask,
                   short lmask, short rmask) __attribute__((unused));
static void jt1184(long data, short w, short h, short mask,
                   short lmask, short rmask)
{
	PROBE("jt1184");
	(void)data; (void)w; (void)h; (void)mask; (void)lmask; (void)rmask;
}
static void jt1189(long data, long src2, short w, short h,
                   short mask, short lmask, short rmask) __attribute__((unused));
static void jt1189(long data, long src2, short w, short h,
                   short mask, short lmask, short rmask)
{
	PROBE("jt1189");
	(void)data; (void)src2; (void)w; (void)h;
	(void)mask; (void)lmask; (void)rmask;
}
static void jt1191(long data, long src2, short w, short h,
                   short mask, short lmask, short rmask) __attribute__((unused));
static void jt1191(long data, long src2, short w, short h,
                   short mask, short lmask, short rmask)
{
	PROBE("jt1191");
	(void)data; (void)src2; (void)w; (void)h;
	(void)mask; (void)lmask; (void)rmask;
}

/* JT[995] (CODE 5 + 0x21fc) — clipped scaled-bitmap blit.
 *
 * Structural skeleton (level 2). The Mac body is ~300 lines of
 * resource lookup → coord remap → clip rect math → row-walk
 * with a 6-arm row-blit dispatch. The CFG mirrors the asm but
 * the final blit primitives (jt1183/1184/1188/1189/1191/1181)
 * are PROBE stubs — the engine bitmap resources and a Falcon-
 * side row-blit aren't plumbed.
 *
 * Body shape:
 *
 *   handle    = jt468(style)
 *   info_ptr  = L2856(handle, size, &font_info)
 *   if (info_ptr == 0)       return 0;
 *   L4d88();                                     // flush InvalRect
 *   jt1135(top, left, &top, &left);              // engine→pixel
 *   top  -= font_info[3];                        // y bearing
 *   left -= font_info[2];                        // x bearing
 *   bpp_w = font_info[6];                        // byte width
 *
 *   // Reject when fully outside clip rect (-3050..-3056)
 *   if (top  < g_a5_-3054)             goto exit;
 *   if (top + font_info[4] > -3050)    goto exit;
 *   if (left < g_a5_-3056)             goto exit;
 *   if ((bpp_w << jt1200()) + left > -3052)  goto exit;
 *
 *   row_count = ((font_info[7] & 0x80) != 0) ? 1 : jt1198();
 *
 *   // Compute row stride (bytes per row in bitmap data)
 *   bytes_per_row = font_info[4] * bpp_w;
 *
 *   // Top / bottom clip:
 *   top_clip   = max(0, g_a5_-3054 - top);
 *   bottom_cap = min(font_info[4], g_a5_-3050 - top);
 *
 *   // Left clip: compute byte offset + boundary mask
 *   if (left < g_a5_-3056) {
 *       short over   = g_a5_-3056 - left;
 *       short bytes  = (over >> jt1200()) & ~1;
 *       lmask        = g_a5_-4646[L21d0(over)];
 *       left_clip    = bytes;
 *       left        += bytes << jt1200();
 *   } else { left_clip = 0; lmask = -1; }
 *
 *   // Right clip: same shape against -3052
 *   if ((bpp_w << jt1200()) + left > -3052) {
 *       short over   = (bpp_w << jt1200()) + left - g_a5_-3052;
 *       short bytes  = (over >> jt1200()) & ~1;
 *       right_clip   = bytes;
 *       rmask        = g_a5_-4614[L21d0(over)];
 *   } else { right_clip = 0; rmask = -1; }
 *
 *   info_ptr += top_clip * bytes_per_row + left_clip;
 *   bpp_w    -= left_clip + right_clip;
 *
 *   if (bottom_cap <= 0 || bpp_w <= 0)  return 0;
 *
 *   // Compute drawing mask from highlighted bit (font_info[7] bit 7) +
 *   // left coord parity
 *   mask = (jt1200() == 3) ? L21d0(left ^ 8) : L21d0(left);
 *
 *   // Dispatch — two paths based on `mode` arg
 *   composite = 0;
 *   if (mode != 0 && mode != 2) {
 *       jt1177(left, top);                       // begin column
 *       for (i = 0; i < jt1198(); i++) {
 *           jt1170();
 *           composite |= (jt1200() == 3 ? jt1188 : jt1183)(
 *               info_ptr, bpp_w, bottom_cap, mask, lmask, rmask);
 *       }
 *   } else {
 *       jt1177(left, top);
 *       long src2 = info_ptr + ((font_info[3] & 1) ? bytes_per_row : 0);
 *       for (i = 0; i < row_count; i++) {
 *           jt1170();
 *           if (mode != 0)
 *               (jt1200() == 3 ? jt1189 : jt1191)(
 *                   info_ptr, src2, bpp_w, bottom_cap, mask, lmask, rmask);
 *           else
 *               (jt1200() == 3 ? jt1184 : jt1181)(
 *                   info_ptr, bpp_w, bottom_cap, mask, lmask, rmask);
 *           info_ptr += bytes_per_row;
 *       }
 *   }
 *   return composite;
 *
 * The skeleton C lift below preserves the major CFG (resource
 * setup, clip math, mode-dispatched row loop) so when the row-
 * blit primitives lift to real bodies, the engine's bitmap path
 * will render through this function unchanged.
 *
 * Note: the bitmap is read from `info_ptr` which is what L2856
 * returns — a pointer into the engine's font / sprite resource
 * with the metric blob written into `font_info`. Without that
 * resource populated by data_pool, `info_ptr` may be NULL or
 * point at garbage, so the function exits early at the L2856
 * gate. */
static short jt995(short top, short left, short style, short size_high,
                   short mode) __attribute__((unused));
static short jt995(short top, short left, short style, short size_high,
                   short mode)
{
	unsigned char  font_info[8];                  /* fp@(-8..-1)        */
	long           info_ptr;                       /* fp@(-12)           */
	long           info_ptr_2;                     /* fp@(-16)           */
	short          bpp_w;                          /* fp@(-18) byte cnt  */
	short          bytes_per_row;                  /* fp@(-20) row stride*/
	short          top_clip;                       /* fp@(-22)           */
	short          left_clip;                      /* fp@(-24)           */
	short          right_clip;                     /* fp@(-26)           */
	short          row_count;                      /* fp@(-32)           */
	short          lmask;                          /* fp@(-36)           */
	short          rmask;                          /* fp@(-38)           */
	short          mask;                           /* fp@(-34)           */
	unsigned char  composite = 0;                  /* fp@(-39)           */
	short          i;
	long           handle;
	short          v_scaled = 0, h_scaled = 0;

	PROBE("jt995");

	handle    = jt468(style);
	info_ptr  = (long)l2856(handle, size_high, font_info);
	if (info_ptr == 0)
		return 0;

	l4d88();
	jt1135(top, left, &v_scaled, &h_scaled);
	top  = v_scaled;
	left = h_scaled;
	top  -= (short)(signed char)font_info[3];
	left -= (short)(signed char)font_info[2];
	bpp_w = (short)font_info[6];

	/* Reject fully-outside-clip-rect cases. */
	if (top  <  g_a5_word(-3054))                                    return 0;
	if (top  + (short)font_info[4] > g_a5_word(-3050))               return 0;
	if (left <  g_a5_word(-3056))                                    return 0;
	if ((short)(bpp_w << jt1200()) + left > g_a5_word(-3052))        return 0;

	row_count = ((font_info[7] & 0x80) != 0) ? (short)1 : jt1198();

	bytes_per_row = (short)((short)font_info[4] * bpp_w);

	/* Top / bottom clip. */
	if (top < g_a5_word(-3054)) {
		top_clip = (short)(g_a5_word(-3054) - top);
	} else {
		top_clip = 0;
	}
	if (top + (short)font_info[4] > g_a5_word(-3050)) {
		font_info[4] = (unsigned char)(g_a5_word(-3050) - top);
	}

	/* Left clip. */
	if (left < g_a5_word(-3056)) {
		short over  = (short)(g_a5_word(-3056) - left);
		short m     = jt1200();
		short bytes = (short)((over >> m) & ~1);
		short pcnt  = l21d0(over);
		left_clip   = bytes;
		lmask       = *(short *)(g_a5_buf(-4646) + pcnt * 2);
		left       += (short)(bytes << jt1200());
	} else {
		left_clip = 0;
		lmask     = (short)-1;
	}

	/* Right clip. */
	if ((short)(bpp_w << jt1200()) + left > g_a5_word(-3052)) {
		short over  = (short)((bpp_w << jt1200()) + left
		                      - g_a5_word(-3052));
		short m     = jt1200();
		short bytes = (short)((over >> m) & ~1);
		short pcnt  = l21d0(over);
		right_clip  = bytes;
		rmask       = *(short *)(g_a5_buf(-4614) + pcnt * 2);
	} else {
		right_clip = 0;
		rmask      = (short)-1;
	}

	left     += top_clip;
	info_ptr += (long)top_clip * (long)bpp_w + (long)left_clip;
	bpp_w    -= (short)(left_clip + right_clip);

	if ((short)font_info[4] <= 0 || bpp_w <= 0)
		return 0;

	/* Drawing mask. */
	mask = (jt1200() == 3) ? l21d0((short)(left ^ 8)) : l21d0(left);

	/* Mode dispatch. */
	if (mode != 0 && mode != 2) {
		jt1177(left, top);
		for (i = 0; i < jt1198(); i++) {
			jt1170();
			if (jt1200() == 3)
				composite |= (unsigned char)jt1188(info_ptr, bpp_w,
				                                   (short)font_info[4],
				                                   mask, lmask, rmask);
			else
				composite |= (unsigned char)jt1183(info_ptr, bpp_w,
				                                   (short)font_info[4],
				                                   mask, lmask, rmask);
		}
	} else {
		jt1177(left, top);
		info_ptr_2 = info_ptr;
		if ((font_info[3] & 0x01) != 0)
			info_ptr += bytes_per_row;

		for (i = 0; i < row_count; i++) {
			jt1170();
			if (mode != 0) {
				if (jt1200() == 3)
					jt1189(info_ptr, info_ptr_2, bpp_w,
					       (short)font_info[4], mask, lmask, rmask);
				else
					jt1191(info_ptr, info_ptr_2, bpp_w,
					       (short)font_info[4], mask, lmask, rmask);
			} else {
				if (jt1200() == 3)
					jt1184(info_ptr, bpp_w, (short)font_info[4],
					       mask, lmask, rmask);
				else
					jt1181(info_ptr, bpp_w, (short)font_info[4],
					       mask, lmask, rmask);
			}
			info_ptr += bytes_per_row;
		}
	}
	return (short)composite;
}

/* L148a (CODE 3 + 0x148a) — text/bitmap paint dispatcher.
 *
 *   if (jt1200() == 3)                             // mode 3 (deep)
 *       jt995(top, left, style, size_h, 2);
 *   else                                            // mode 0 / 1
 *       jt1001(top, left, style, size_h);
 *
 * jt382 cmd=1 funnels through here; both branches end up doing
 * a scaled-bitmap blit (jt995 directly, jt1001 via jt468 +
 * l309c). For the port the actual rendering both arms do is
 * dormant (PROBE stubs), so L148a fires without producing
 * pixels — jt382 cmd=1 still has its own DrawString call for
 * the text label as a port addition. */
static void l148a(short top, short left, short style, short size_high) __attribute__((unused));
static void l148a(short top, short left, short style, short size_high)
{
	PROBE("L148a");
	if (jt1200() == 3) {
		(void)jt995(top, left, style, size_high, (short)2);
	} else {
		jt1001(top, left, style, size_high);
	}
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
static void jt94(short page, short row, short col, short style,
                 const char *fmt, ...);   /* defined below */
/* jt25 (CODE 6 + 0x4b0e) — paint a roster entry's NAME. The Mac body
 * draws name + class/level/status via the record-paint funnel; the port
 * surfaces the name (record offset +96, a C-string) through the lifted
 * jt94 text path so the roster grid (l02dc) shows real entries, not just
 * the highlighted one. */
static void jt25(long entry, short page, short row, short style)
{
	const unsigned char *e = (const unsigned char *)(uintptr_t)entry;
	PROBE("jt25");
	if (e != NULL)
		jt94(page, row, (short)11, style, "%s", (const char *)&e[96]);
}
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

/* L37aa (CODE 5 + 0x37aa) — GLIB library item lookup.
 *
 * The engine's bitmap/glyph resources are "GLIB" (Graphics
 * LIBrary) files loaded through the file cache. Layout:
 *
 *   +0   long   magic 'GLIB' (0x474C4942)
 *   +8   word   item_count
 *   +16  long[] item offset table (item_count entries), each
 *               offset relative to the library base
 *
 * L37aa(base, idx) verifies the magic, bounds-checks idx, reads
 * the idx-th offset, and returns base + offset (a pointer to the
 * item's data). The Mac copies the 16-byte header + 4-byte offset
 * via JT[406] (BlockMove) into locals then reads them as native
 * big-endian; on the big-endian m68k target we read the bytes
 * directly with explicit big-endian assembly (avoids any odd-
 * address word/long fault on the 68000).
 *
 * Mac error paths (bad magic / out-of-range) call L036a to raise
 * an alert; the port returns 0 instead — callers (L2856) treat 0
 * as "not found" and bail cleanly, no modal alert spam. */
static long l37aa(long base_long, short idx) __attribute__((unused));
static long l37aa(long base_long, short idx)
{
	const unsigned char *base = (const unsigned char *)(uintptr_t)base_long;
	unsigned short       count;
	const unsigned char *ent;
	long                 offset;

	PROBE("L37aa");
	if (base == NULL)
		return 0;
	/* 'GLIB' magic at +0. */
	if (base[0] != 'G' || base[1] != 'L'
	 || base[2] != 'I' || base[3] != 'B')
		return 0;
	count = (unsigned short)(((unsigned)base[8] << 8) | base[9]);
	if (idx < 0 || (unsigned short)idx >= count)
		return 0;
	ent = base + 16 + (long)idx * 4;
	offset = ((long)ent[0] << 24) | ((long)ent[1] << 16)
	       | ((long)ent[2] << 8)  | (long)ent[3];
	return base_long + offset;
}

/* L2856 (CODE 5 + 0x2856) — font / glyph metric extract.
 *
 *   entry = (size >= 0) ? L37aa(handle, size) : handle;
 *   BlockMove(entry, out_8bytes, 8);          // copy metric header
 *   return entry + 8;                          // -> bitmap data
 *
 * Each GLIB item begins with an 8-byte metric header followed by
 * the bitmap. The header layout (big-endian, as jt995 / jt1005
 * read it):
 *
 *   +0  word  height      — glyph rows
 *   +2  word  y_bearing   — subtracted from the pen top
 *   +4  word  x_bearing   — subtracted from the pen left
 *   +6  byte  bpp_w       — bytes per bitmap row
 *   +7  byte  flags       — bit7 = single-row; low nibble (<=1) valid
 *
 * Port deviation: when the lookup fails (no GLIB loaded, bad
 * magic, idx out of range) L37aa returns 0; we return 0 here too
 * so jt995 / jt1005 take their "no glyph" early-exit cleanly. The
 * literal Mac would return entry+8 == 8 (a bogus low pointer) and
 * fault downstream — the port's 0-return is the safe equivalent
 * of the Mac's L036a alert-and-abort.
 *
 * Replaces the prior synthetic-metrics stub: that fabricated a
 * 14px font so JT[1005] could compute approximate text rects
 * without real data. With this real lookup, rects only compute
 * when an actual GLIB is loaded — correct, but it means JT[1005]
 * produces no hit-test rect in the data-less boot path (benign;
 * the menu is keyboard-driven via L1676 cmd=5, which doesn't use
 * the rect). */
static long l2856(long font_handle, short size, void *out_8bytes)
                                                __attribute__((unused));
static long l2856(long font_handle, short size, void *out_8bytes)
{
	long entry;

	PROBE("L2856");
	if (out_8bytes == NULL)
		return 0;
	entry = (size >= 0) ? l37aa(font_handle, size) : font_handle;
	if (entry == 0)
		return 0;
	jt406(out_8bytes, (const void *)(uintptr_t)entry, 8);   /* dst, src, n */
	return entry + 8;
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

/* L6204 (CODE 4 + 0x6204) — live mouse poll. Faithful lift:
 *
 *   Point pt;
 *   GetMouse(&pt);                                // _GetMouse trap
 *   if (g_a5_-2346)  pt.v >>= 1;  pt.h >>= 1;     // half-scale mode
 *   *out_y = pt.v;
 *   *out_x = pt.h;
 *
 * GetMouse routes through the compat shim → plat_mouse_pos in
 * the Input HAL, which reads the IKBD-driven g_mouse_x / g_mouse_y. */
static void l6204(short *out_y, short *out_x) __attribute__((unused));
static void l6204(short *out_y, short *out_x)
{
	Point pt;

	PROBE("L6204");
	GetMouse(&pt);
	if (g_a5_byte(-2346) != 0) {
		pt.v = (short)(pt.v >> 1);
		pt.h = (short)(pt.h >> 1);
	}
	if (out_y != NULL) *out_y = pt.v;
	if (out_x != NULL) *out_x = pt.h;
}

/* JT[1132] (CODE 4 + 0x6288) — mouse poll. Faithful lift:
 *
 *   L4d88();                                       // flush InvalRect
 *   L731e(2);                                      // pump events (mouseDown peek)
 *   if (g_a5_-903 || g_a5_-904) {                  // buffered click?
 *       *out_y = g_a5_-908;
 *       *out_x = g_a5_-906;
 *       g_a5_-903 = 0;                              // clear edge
 *       return 1;                                   // button held
 *   }
 *   L6204(out_y, out_x);                            // live poll
 *   return 0;
 *
 * The buffered path (-903 / -904) fires when L6cba (mouseUp arm)
 * or L6b26 (inContent click body) previously captured coords
 * into g_a5_-908 / -906. With real IKBD input plumbed through
 * L725c → those arms, the engine's cmd=3 mouse-track loop sees
 * live clicks. */
static short jt1132(short *out_y, short *out_x) __attribute__((unused));
static short jt1132(short *out_y, short *out_x)
{
	PROBE("jt1132");
	l4d88();
	l731e((short)2);

	if (g_a5_byte(-903) != 0 || g_a5_byte(-904) != 0) {
		if (out_y != NULL) *out_y = g_a5_word(-908);
		if (out_x != NULL) *out_x = g_a5_word(-906);
		g_a5_byte(-903) = 0;
		return 1;
	}

	l6204(out_y, out_x);
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
		/* Mac: rec[28] |= 0x80 ("painted") + return. The actual
		 * paint lives in each shape handler's cmd=1; L1676's role
		 * here is just the flag. */
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

	if (cmd == 1) {
		/* Mac L1a5e (CODE 3 + 0x1a5e) — paint button label.
		 *
		 *   rec[28] |= 0x80                          ; mark painted
		 *   if (bit 1 of rec[28])    return 0;       ; disabled
		 *   highlighted = (rec[28] & 0x09) ? 1 : 0;  ; bits 0 or 3
		 *   short style_size = rec[26..27];
		 *   if (style_size >= 0) {
		 *       short style = (style_size > 0) ? style_size >> 10 : 0;
		 *       short size  = (style_size != 0) ? style_size & 0x3FF : 14;
		 *       L148a(rec[16], rec[18], style, size + highlighted);
		 *   } else if (style_size == -1) {
		 *       // No-op; sentinel for "no paint"
		 *   } else {
		 *       // Icon path (style_size < -1) — deferred.
		 *   }
		 *
		 * Mac's L148a → jt995 chain is the text-paint funnel
		 * (font lookup → coord scale → clip → DrawString). For the
		 * port we collapse to a direct jt1135 + DrawString on
		 * rec[12..15] (label C-string ptr) — the same shortcut
		 * the L1676 cmd=1 catch-all uses. Once L148a / jt995 lift
		 * faithfully we'll route through them instead. */
		short ss = *(short *)(rec + 26);
		short highlighted;

		rec[28] |= 0x80;

		if ((rec[28] & 0x02) != 0)
			return 0;                       /* disabled */

		highlighted = ((rec[28] & 0x09) != 0) ? 1 : 0;

		if (ss < 0) {
			/* style_size == -1: no-paint sentinel.
			 * style_size < -1: icon path, deferred. */
			(void)highlighted;
			return 0;
		}

		/* style_size >= 0 → text path */
		{
			short style = (ss > 0) ? (short)(ss >> 10) : (short)0;
			short size  = (ss != 0) ? (short)(ss & 0x3FF) : (short)14;
			const char *label = *(const char **)(rec + 12);
			short y_pix = 0, x_pix = 0;

			/* Faithful: route through the Mac paint dispatcher.
			 * In our port jt995 / jt1001 are PROBE stubs so this
			 * doesn't produce visible pixels — see below for the
			 * port-side text rendering. */
			l148a(*(short *)(rec + 16), *(short *)(rec + 18),
			      style, (short)(size + highlighted));

			/* Port addition: render the label text. The Mac paint
			 * chain handles button bitmaps + decoration; the
			 * label-as-text rendering we surface here is what the
			 * QuickDraw shim renders to screen until the Mac
			 * bitmap path lifts. */
			if (label == NULL || (long)label <= 0x1000L)
				return 0;

			jt1135(*(short *)(rec + 16), *(short *)(rec + 18),
			       &y_pix, &x_pix);

			{
				unsigned char pbuf[256];
				short len = 0;
				while (label[len] != 0 && len < 255)
					len++;
#ifdef FRUA_ENGINE_PROBE
				/* DEBUG: log paint coords + label */
				dbg_log_num("jt382:paint y_eng=",
				    *(short *)(rec + 16));
				dbg_log_num("jt382:paint x_eng=",
				    *(short *)(rec + 18));
				dbg_log_num("jt382:paint y_pix=", y_pix);
				dbg_log_num("jt382:paint x_pix=", x_pix);
				dbg_log_num("jt382:paint len=", len);
				if (len > 0 && len < 64) {
					char tmp[80];
					int  k;
					tmp[0] = ' ';
					tmp[1] = '[';
					for (k = 0; k < len; k++)
						tmp[k + 2] = label[k];
					tmp[len + 2] = ']';
					tmp[len + 3] = 0;
					dbg_log(tmp);
				}
#endif
				if (len > 0) {
					/* The Mac paint chain (L148a/jt995) sets the pen
					 * colour; our collapsed path doesn't, so the label
					 * would inherit a stale fgColor. Draw the body in the
					 * UI's light grey (clut 7) and the accelerator letter
					 * in white (clut 15) — the highlighted hotkey letter
					 * in data/frua_mac_menu.png. The accelerator code is
					 * rec[29] (set by jt452 cmd 32); we highlight its first
					 * case-insensitive match in the label. */
					const unsigned char BODY = 7, HOT = 15;
					unsigned char hk = rec[29];
					short hi = -1, k;
					CGrafPtr cport;
					GrafPtr  bport;

					GetPort(&bport);
					cport = (CGrafPtr)bport;

					if (hk != 0) {
						unsigned char hu = (unsigned char)
						    ((hk >= 'a' && hk <= 'z') ? hk - 32 : hk);
						for (k = 0; k < len; k++) {
							unsigned char c = (unsigned char)label[k];
							if (c >= 'a' && c <= 'z') c -= 32;
							if (c == hu) { hi = k; break; }
						}
					}

					MoveTo(x_pix, y_pix);
					if (hi < 0) {            /* no accelerator: all body */
						if (cport) cport->fgColor = BODY;
						pbuf[0] = (unsigned char)len;
						memcpy(pbuf + 1, label, (size_t)len);
						DrawString(pbuf);
					} else {
						if (hi > 0) {    /* prefix (body) */
							if (cport) cport->fgColor = BODY;
							pbuf[0] = (unsigned char)hi;
							memcpy(pbuf + 1, label, (size_t)hi);
							DrawString(pbuf);
						}
						if (cport) cport->fgColor = HOT;  /* hotkey */
						pbuf[0] = 1;
						pbuf[1] = (unsigned char)label[hi];
						DrawString(pbuf);
						if (hi + 1 < len) {  /* suffix (body) */
							short n = (short)(len - hi - 1);
							if (cport) cport->fgColor = BODY;
							pbuf[0] = (unsigned char)n;
							memcpy(pbuf + 1, label + hi + 1, (size_t)n);
							DrawString(pbuf);
						}
					}
				}
			}
		}
		return 0;
	}

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

/* TEST SCAFFOLD — forward decl; defined just below. */
void port_test_seed_design(void);
/* Forward — jt127 (design-data loader) lifts further down; the
 * probe self-test below calls it. */
static void jt127(const char *prefix, short num, short *out, void *buffer);

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

	/* Dungeon-view direction-step tables (the -27862 view-state struct:
	 * drow at -27862+dir, dcol at -27853+dir, dir 0..7). These read zero
	 * in our build — they fall in the BSS region below the 12694-byte
	 * DATA image (-12694..-1; CODE 0's below-A5 size is 31336, so -27862
	 * is genuinely BSS), below the lowest DREL reloc, and no CODE segment
	 * ever writes them, so the frustum walker (jt199 / jt210 / l5e52)
	 * can't step without them. Seed the cardinal/ordinal steps over the
	 * column-major map (cell = col*height + row): drow == dir_dy and
	 * dcol == dir_dx — the same movement deltas render_3d_view walks the
	 * map with, so these are validated, not guessed. */
	{
		static const signed char drow[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };
		static const signed char dcol[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };
		short k;
		for (k = 0; k < 8; k++) {
			g_a5_byte(-27862 + k) = (unsigned char)drow[k];
			g_a5_byte(-27853 + k) = (unsigned char)dcol[k];
		}
	}

	/* Dungeon-view slot-layout globals g_a5_-12240..-12198 (22 words), the
	 * per-slot screen deltas jt199/l5b42 read. CAPTURED LIVE from real FRUA
	 * under a mon-enabled BasiliskII (CurrentA5-confirmed = 0x01F74AC0; see
	 * docs/mac-emulator.md). The static DATA image held different, off-screen
	 * values (175/516/...) — a launch-time init overwrites them with these
	 * small deltas, and l5b42's deep transform maps them on-screen (e.g.
	 * side xdelta -12202=4 -> ((8016+4*4)-8012)<<2+8 = x 88). This is what
	 * unblocks the faithful jt199 pixel render. */
	{
		static const short layout[22] = {
			5, 4, 6, 4, 2, 7, 2, 0,  9, 5, 4, 3, 3, 3, 1, 1,
			1, 0, 0, 4, 0, 0
		};
		short k;
		for (k = 0; k < 22; k++)
			g_a5_word(-12240 + k * 2) = layout[k];
	}

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

	/* ===== TEST SCAFFOLD — REVERT WHEN JT[557] / JT[585] LAND =====
	 * Initial seed; l5124 clears g_a5_-27932 on every first-time
	 * play-loop entry so port_test_seed_design() in l07dc re-points
	 * it after that clear. See port_test_seed_design() for details. */
	port_test_seed_design();
	/* ===== end TEST SCAFFOLD ===== */

#ifdef FRUA_ENGINE_PROBE
	/* L2856 / L37aa self-test — build a known GLIB in a static
	 * buffer, look up item 1, and log whether the returned bitmap
	 * pointer + extracted 8-byte metric header match expectations.
	 * Probe-gated; compiled out of production. Verifies the GLIB
	 * lookup independently of the live jt468 / style / size flow. */
	{
		static unsigned char glib[64];
		unsigned char        info[8];
		long                 ret;
		short                i;

		for (i = 0; i < 64; i++)
			glib[i] = 0;
		/* header */
		glib[0]='G'; glib[1]='L'; glib[2]='I'; glib[3]='B';
		glib[8]=0;   glib[9]=2;                 /* item_count = 2 */
		/* offset table @16: item0 -> 24, item1 -> 40 (base-rel) */
		glib[16]=0; glib[17]=0; glib[18]=0; glib[19]=24;
		glib[20]=0; glib[21]=0; glib[22]=0; glib[23]=40;
		/* item1 @40: 8-byte metric header + 1 bitmap byte */
		glib[40]=0x00; glib[41]=0x08;           /* height    = 8   */
		glib[42]=0x00; glib[43]=0x02;           /* y_bearing = 2   */
		glib[44]=0x00; glib[45]=0x01;           /* x_bearing = 1   */
		glib[46]=0x03;                           /* bpp_w     = 3   */
		glib[47]=0x80;                           /* flags: bit7 set */
		glib[48]=0xAA;                           /* bitmap[0]       */

		ret = l2856((long)(uintptr_t)glib, (short)1, info);
		dbg_log_num("L2856 self-test: ret-base = ",
		            ret ? (ret - (long)(uintptr_t)glib) : -1);
		dbg_log_num("  height    = ", (info[0]<<8)|info[1]);
		dbg_log_num("  y_bearing = ", (info[2]<<8)|info[3]);
		dbg_log_num("  x_bearing = ", (info[4]<<8)|info[5]);
		dbg_log_num("  bpp_w     = ", info[6]);
		dbg_log_num("  flags     = ", info[7]);
		dbg_log_num("  bitmap[0] = ", *(unsigned char *)(uintptr_t)ret);
		/* Expected: ret-base=48, height=8, ybear=2, xbear=1,
		 * bpp_w=3, flags=128, bitmap[0]=0xAA(170). */
	}

	/* jt127 self-test — load the tutorial's first map. Drives the
	 * design-data open/read path (jt127 -> jt398 -> jt401) against
	 * the staged TUTORIAL.DSN. Expects GEO040.DAT (12962 bytes,
	 * IFF "FORM" magic). Only meaningful when run with
	 * GEMDOS_DIR=data/work/gamedata; with the repo-root mount the
	 * file is absent and the read returns 0. */
	{
		static unsigned char geobuf[13000];
		short out = -1;

		geobuf[0] = geobuf[1] = geobuf[2] = geobuf[3] = 0;
		jt127("GEO", (short)40, &out, geobuf);
		dbg_log_num("jt127 self-test: bytes read = ", out);
		{
			char tag[6];
			tag[0]=' '; tag[1]=geobuf[0]; tag[2]=geobuf[1];
			tag[3]=geobuf[2]; tag[4]=geobuf[3]; tag[5]=0;
			dbg_log(tag);   /* expect " FORM" from GEO040.DAT */
		}
	}
#endif
}

/* port_render_geo_map — visualize the loaded GEO map (design-state
 * g_a5_-12300) as a grid of 8-bit cells. The 'MAP ' chunk at ds+290
 * is 6 bytes per tile on a 24x24 max grid; ds[2]/ds[3] are the used
 * width/height. Each cell is painted straight into the display back
 * buffer (open tile vs wall tile), with a 1px black gap forming a
 * grid, then presented. A bring-up aid, not the real tile renderer
 * (that needs the deferred GLIB blit). */
/* paint one pixel with clipping against the surface */
static void map_px(unsigned char *base, short pitch, short sw, short sh,
                   short x, short y, unsigned char c)
{
	if (x >= 0 && x < sw && y >= 0 && y < sh)
		base[(long)y * pitch + x] = c;
}

/* Decode a tile edge byte to a CLUT index. The edge codes split on
 * bit 7: a set bit 7 (0xe0..0xff cluster) is a SOLID WALL — shaded
 * as a grey ramp by the texture low-nibble (224..239); a clear bit 7
 * (0x01..0x5a cluster) is a DOOR / passage — a bright colour by type
 * (240..243). 0 is no edge. */
static unsigned char edge_color(unsigned char code)
{
	if (code == 0)
		return 0;
	if (code & 0x80)
		return (unsigned char)(224 + (code & 0x0f));
	return (unsigned char)(240 + (code & 3));
}

/* install the map demo CLUT: 224..239 wall grey ramp, 240..243 door
 * colours, 248..251 floor shades, 255 black. Shared by both views. */
static void map_demo_palette(void)
{
	RGBColor c[32];
	short i;

	for (i = 0; i < 32; i++)
		c[i].red = c[i].green = c[i].blue = 0;
	for (i = 0; i < 16; i++) {          /* 224..239 wall grey ramp */
		unsigned short v = (unsigned short)(0x4000 + i * 0xC00);
		c[i].red = c[i].green = c[i].blue = v;
	}
	c[16].red = 0x1000; c[16].green = 0xffff; c[16].blue = 0x2000; /* 240 green */
	c[17].red = 0xffff; c[17].green = 0xe000; c[17].blue = 0x1000; /* 241 yellow */
	c[18].red = 0x1000; c[18].green = 0xe000; c[18].blue = 0xffff; /* 242 cyan */
	c[19].red = 0xffff; c[19].green = 0x6000; c[19].blue = 0x1000; /* 243 orange */
	c[24].red = 0x1000; c[24].green = 0x1000; c[24].blue = 0x2800; /* 248 floor */
	c[25].red = 0x2000; c[25].green = 0x2000; c[25].blue = 0x4000; /* 249 */
	c[26].red = 0x3000; c[26].green = 0x3000; c[26].blue = 0x5800; /* 250 */
	c[27].red = 0x4000; c[27].green = 0x4000; c[27].blue = 0x7000; /* 251 */
	/* 255 (c[31]) stays black */
	qd_set_palette(c, 224, 32);
}

void port_render_geo_map(void)
{
	unsigned char *px;
	short pitch, sw, sh;
	const unsigned char *ds, *map;
	short w, h, x, y, sx, sy;
	const short cell = 10, ox = 6, oy = 6;

	/* tile layout (6 bytes): [0]=N wall, [1]=S, [2]=E, [3]=W,
	 * [4]=0 (reserved), [5]=floor flag (0..3). Each edge byte is
	 * decoded (edge_color) into a wall-texture grey or a door
	 * colour. */
	ds = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	if (ds == 0)
		return;
	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == 0)
		return;

	w = (unsigned char)ds[2];
	h = (unsigned char)ds[3];
	map = ds + 290;
#ifdef FRUA_ENGINE_PROBE
	dbg_log_num("geo map: w = ", w);
	dbg_log_num("geo map: h = ", h);
#endif

	map_demo_palette();

	for (y = 0; y < sh; y++)
		memset(px + (long)y * pitch, 255, (size_t)sw);

	if (w <= 0 || h <= 0 || (long)w * h > 576)
		return;

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			/* row-major, packed at the map's own width (w*h<=576
			 * tiles, 6 bytes each, fit the 3456-byte MAP chunk) */
			const unsigned char *t = map + ((long)y * w + x) * 6;
			unsigned char floor = (unsigned char)(248 + (t[5] & 3));
			unsigned char eN = edge_color(t[0]);
			unsigned char eS = edge_color(t[1]);
			unsigned char eE = edge_color(t[2]);
			unsigned char eW = edge_color(t[3]);
			short bx = (short)(ox + x * cell);
			short by = (short)(oy + y * cell);

			/* floor interior */
			for (sy = 1; sy < cell; sy++)
				for (sx = 1; sx < cell; sx++)
					map_px(px, pitch, sw, sh,
					       bx + sx, by + sy, floor);
			/* decoded edges */
			for (sx = 0; sx <= cell; sx++) {
				if (eN) map_px(px, pitch, sw, sh,
				               bx + sx, by, eN);
				if (eS) map_px(px, pitch, sw, sh,
				               bx + sx, by + cell, eS);
			}
			for (sy = 0; sy <= cell; sy++) {
				if (eW) map_px(px, pitch, sw, sh,
				               bx, by + sy, eW);
				if (eE) map_px(px, pitch, sw, sh,
				               bx + cell, by + sy, eE);
			}
		}
	}
	qd_present();
}

/* blit_glyph_1bpp — from-scratch GLIB glyph rasterizer (the option-B
 * blit from docs/decompilation.md). Reads a 1-bit-per-pixel glyph —
 * `metric[0..1]` = height (rows), `metric[6]` = bpp_w (bytes per
 * row), MSB-first within each byte — and paints set bits as `pen`
 * straight into the 8-bit back buffer at (x0,y0). This diverges from
 * the faithful jt995 path (which composites into a bit-packed page);
 * it draws chunky pixels at the shim's depth instead. */
static void blit_glyph_1bpp(unsigned char *px, short pitch, short sw, short sh,
                            const unsigned char *metric, const unsigned char *bmp,
                            short x0, short y0, unsigned char pen, short scale,
                            short plane)
{
	short height = (short)(((unsigned short)metric[0] << 8) | metric[1]);
	short bpp_w  = metric[6];
	short row, byte, bit, sx, sy;
	const unsigned char *src = bmp + (long)plane * height * bpp_w;

	if (scale < 1)
		scale = 1;
	for (row = 0; row < height; row++) {
		for (byte = 0; byte < bpp_w; byte++) {
			unsigned char bits = src[row * bpp_w + byte];
			for (bit = 0; bit < 8; bit++) {
				short col = (short)(byte * 8 + bit);
				if (!(bits & (0x80 >> bit)))
					continue;
				for (sy = 0; sy < scale; sy++)
					for (sx = 0; sx < scale; sx++)
						map_px(px, pitch, sw, sh,
						       (short)(x0 + col * scale + sx),
						       (short)(y0 + row * scale + sy),
						       pen);
			}
		}
	}
}

/* port_render_topview — load the real TOPVIEW.TLB tile library
 * (Disk1, a GLIB of 16x16 1bpp top-down map tiles) and rasterize
 * every glyph into a grid via blit_glyph_1bpp + the validated
 * L37aa/L2856 extraction. Confirms the GLIB blit path renders real
 * game tile art onto the Falcon. */
void port_render_topview(void)
{
	static unsigned char buf[2048];
	unsigned char metric[8];
	unsigned char *px;
	short pitch, sw, sh, i, slot = 0;
	long base, count;
	short refnum = 0;
	RGBColor c[2];

	if (FSOpen((ConstStr255Param)"\013TOPVIEW.TLB", 0, &refnum) != noErr)
		return;
	count = (long)sizeof buf;
	(void)FSRead(refnum, &count, buf);    /* reads to EOF (eofErr ok) */
	(void)FSClose(refnum);

	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == 0)
		return;
	base = (long)(uintptr_t)buf;
	if (l37aa(base, 0) == 0)              /* validate 'GLIB' magic */
		return;

	c[0].red = c[0].green = c[0].blue = 0;          /* 0 black   */
	c[1].red = c[1].green = c[1].blue = 0xffff;     /* 1 white   */
	qd_set_palette(c, 0, 2);
	for (i = 0; i < sh; i++)
		memset(px + (long)i * pitch, 0, (size_t)sw);

	/* item 0 is the directory; glyphs are items 1.. */
	for (i = 1; ; i++) {
		long bmp = l2856(base, i, metric);
		short ox, oy;

		if (bmp == 0)
			break;                       /* past the last item */
		ox = (short)((slot % 8) * 38 + 8);
		oy = (short)((slot / 8) * 38 + 8);
		blit_glyph_1bpp(px, pitch, sw, sh, metric,
		                (const unsigned char *)(uintptr_t)bmp,
		                ox, oy, 1, 2, 0);       /* 2x scale, plane 0 */
		slot++;
	}
#ifdef FRUA_ENGINE_PROBE
	dbg_log_num("topview tiles drawn = ", slot);
#endif
	qd_present();
}

/* port_render_geo_tiles — draw the loaded GEO map with REAL tiles.
 * Each TOPVIEW.TLB glyph 1..16 is the automap cell for one wall
 * combination: tile (1 + mask), where mask = N|E<<1|S<<2|W<<3 over
 * the cell's four edge bytes (a wall present on that side). So for
 * each map cell we compute its wall mask and rasterize the matching
 * 16x16 tile — the GEO map rendered as the game's own top-down
 * automap, not coloured cells. (Tile semantics confirmed straight
 * from the glyph bitmaps: tile 2 = N bar, 3 = E bar, ... 16 = all
 * four.) The screen is a 20x15-cell viewport; larger maps clip. */
void port_render_geo_tiles(void)
{
	static unsigned char tv[2048];
	unsigned char metric[8];
	unsigned char *px;
	short pitch, sw, sh, w, h, x, y, refnum = 0;
	const unsigned char *ds, *map;
	long tvbase, count;
	RGBColor c3[3];

	if (FSOpen((ConstStr255Param)"\013TOPVIEW.TLB", 0, &refnum) != noErr)
		return;
	count = (long)sizeof tv;
	(void)FSRead(refnum, &count, tv);
	(void)FSClose(refnum);
	tvbase = (long)(uintptr_t)tv;
	if (l37aa(tvbase, 0) == 0)
		return;

	jt198((short)40);                          /* load a real map */
	ds = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	if (ds == 0)
		return;
	w = (unsigned char)ds[2];
	h = (unsigned char)ds[3];
	map = ds + 290;
	if (w <= 0 || h <= 0 || (long)w * h > 576)
		return;

	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == 0)
		return;
	c3[0].red = c3[0].green = c3[0].blue = 0;          /* 0 black  */
	c3[1].red = c3[1].green = c3[1].blue = 0xffff;     /* 1 white walls */
	c3[2].red = 0xffff; c3[2].green = 0xd000; c3[2].blue = 0; /* 2 door amber */
	qd_set_palette(c3, 0, 3);
	for (y = 0; y < sh; y++)
		memset(px + (long)y * pitch, 0, (size_t)sw);

	/* Layout per JT[202] (the runtime wall query, CODE 7 + 0x5e52):
	 * the map is COLUMN-MAJOR with stride = height (ds[3]) — tile
	 * (col,row) = MAP + (col*h + row)*6 — and the edge byte's high
	 * nibble selects the wall art: 0xe_ = a standard wall texture,
	 * 0x0_/0x3_ = a special edge from GEO.GLB's 43-entry table
	 * (doors / archways / secret etc.). We draw standard walls
	 * (bit 7 set) as the wall-combination tile and overlay each
	 * special edge with the directional TOPVIEW door tile (N=17,
	 * S=19, E=20, W=18 — plane 1 = the arrow).
	 *
	 * (JT[202] returns the edge's LOW nibble; against the editor's
	 * movement-type list — Free/Blocked/Open/Open-secret/Locked.. —
	 * that nibble is the per-edge movement attribute the runtime
	 * uses for collision. The automap's exact door-glyph rule is
	 * the editor's, unlifted; the high-nibble art split is the
	 * visual heuristic here.) */
	for (y = 0; y < h; y++) {            /* y = row */
		for (x = 0; x < w; x++) {        /* x = column */
			const unsigned char *t = map + ((long)x * h + y) * 6;
			short px0 = (short)(x * 16), py0 = (short)(y * 16);
			short mask = (short)(((t[0] & 0x80) ? 1 : 0)
			                   | ((t[1] & 0x80) ? 2 : 0)
			                   | ((t[2] & 0x80) ? 4 : 0)
			                   | ((t[3] & 0x80) ? 8 : 0));
			long bmp = l2856(tvbase, (short)(1 + mask), metric);
			short e, door_tile[4];

			if (bmp != 0)
				blit_glyph_1bpp(px, pitch, sw, sh, metric,
				                (const unsigned char *)(uintptr_t)bmp,
				                px0, py0, 1, 1, 0);

			/* edge order [N,E,S,W] = t[0..3] per JT[202] */
			door_tile[0] = 17;  /* N */
			door_tile[1] = 20;  /* E */
			door_tile[2] = 19;  /* S */
			door_tile[3] = 18;  /* W */
			for (e = 0; e < 4; e++) {
				long db;
				if (t[e] == 0 || (t[e] & 0x80))
					continue;          /* no edge, or standard wall */
				db = l2856(tvbase, door_tile[e], metric);
				if (db == 0)
					continue;
				blit_glyph_1bpp(px, pitch, sw, sh, metric,
				                (const unsigned char *)(uintptr_t)db,
				                px0, py0, 2, 1, 1);  /* door colour, plane 1 */
			}
		}
	}
	qd_present();
}

/* --- the play loop core: walk the party around the loaded map --- */

/* 8-direction deltas (facing 0=N, 2=E, 4=S, 6=W; odd = diagonal). */
static const signed char dir_dx[8] = {  0,  1, 1, 1, 0, -1, -1, -1 };
static const signed char dir_dy[8] = { -1, -1, 0, 1, 1,  1,  0, -1 };

/* party_passable — may the party leave cell (x,y) heading `f`? Per
 * JT[202] the blocking edge is t[(f&6)>>1]; passable iff that edge's
 * movement nibble is 0 (Free movement). */
static int party_passable(short x, short y, short f)
{
	const unsigned char *ds =
		(const unsigned char *)(uintptr_t)g_a5_long(-12300);
	const unsigned char *t;
	short hh;

	if (ds == NULL)
		return 0;
	hh = (unsigned char)ds[3];
	t = ds + 290 + ((long)x * hh + y) * 6;
	return (short)(t[(f & 6) >> 1] & 0x0f) == 0;
}

/* party_step — apply a movement command to the live party globals
 * (g_a5_-12288 x / -12287 y / -12286 facing): 0 turn left, 1 turn
 * right, 2 forward, 3 back. Turns rotate +-2 mod 8 (the engine's
 * cardinal step); moves advance one cell if the facing edge is
 * passable and the destination stays on the map. */
static void party_step(short cmd)
{
	const unsigned char *ds =
		(const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short f = (short)(g_a5_12286 & 7);
	short w, h, mf, nx, ny;

	if (ds == NULL)
		return;
	w = (unsigned char)ds[2];
	h = (unsigned char)ds[3];

	switch (cmd) {
	case 0: g_a5_12286 = (unsigned char)((f + 6) & 7); return;
	case 1: g_a5_12286 = (unsigned char)((f + 2) & 7); return;
	case 2: mf = f;                    break;
	case 3: mf = (short)((f + 4) & 7); break;
	default: return;
	}
	if (!party_passable((short)g_a5_12288, (short)g_a5_12287, mf))
		return;
	nx = (short)((short)g_a5_12288 + dir_dx[mf]);
	ny = (short)((short)g_a5_12287 + dir_dy[mf]);
	if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
		g_a5_12288 = (unsigned char)nx;
		g_a5_12287 = (unsigned char)ny;
	}
}

/* draw_party — a marker at the party cell with a nub pointing in the
 * facing direction (CLUT index 3). */
static void draw_party(unsigned char *px, short pitch, short sw, short sh,
                       short x, short y, short f)
{
	short cx = (short)(x * 16 + 8), cy = (short)(y * 16 + 8), i, j;

	for (j = -3; j <= 3; j++)
		for (i = -3; i <= 3; i++)
			map_px(px, pitch, sw, sh, (short)(cx + i), (short)(cy + j), 3);
	for (i = 1; i <= 6; i++)
		map_px(px, pitch, sw, sh,
		       (short)(cx + dir_dx[f & 7] * i),
		       (short)(cy + dir_dy[f & 7] * i), 3);
}

/* cell_edge — the edge byte of cell (x,y) in direction `f` (0 if open;
 * off-map returns a standard wall code so boundaries are textured). The
 * dungeon view reads this to decide where walls appear. */
static unsigned char cell_edge(short x, short y, short f)
{
	const unsigned char *ds =
		(const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short w, h;

	if (ds == NULL)
		return 0xe1;
	w = (unsigned char)ds[2];
	h = (unsigned char)ds[3];
	if (x < 0 || x >= w || y < 0 || y >= h)
		return 0xe1;
	return ds[290 + ((long)x * h + y) * 6 + ((f & 6) >> 1)];
}

/* DUNGCOM 1bpp wall-tile table — still loaded by the demo/setup (the
 * byte-depth probe + level loader); the colour 3D view no longer reads it. */
#define WALL_NTILES 27
static unsigned char        g_wall_metric[WALL_NTILES][8];
static const unsigned char *g_wall_bmp[WALL_NTILES];
static short                g_wall_n;

/* Colour wall sets — up to CW_SLOTS loaded at once so the three wall
 * groups a level can use (Wall1-3) are all on screen. Each slot holds a
 * copy of its set's plain-wall piece (item 8, 8bpp chunky, <=56x56) and a
 * 32-colour palette band in the clut at g_cw_base[slot]; the tile bytes
 * are 32..71 (a per-set band that points at clut 32), remapped per slot to
 * its band. A per-slot/per-depth darken table (g_cw_remap) shades within
 * the band. The 8X8DC .CTL pieces are 8bpp (1 byte/pixel, stride = width
 * = 8*bpp_w), unlike the .TLB's 1/2bpp. */
#define CW_NPIECE 48          /* pieces per set                            */
#define CW_SLOTS 3            /* one per wall group (Wall1-3)              */
#define CW_FACETS 5           /* facets per group: wall/window/door/...    */
#define CW_BAND  32           /* clut entries per wall band                */
/* Facet (position-in-group, 0-4) -> near-face piece, from jt200's tile
 * arithmetic. Facet 0 = plain wall; 1 = window, 2 = door, 3 = brazier,
 * 4 = fireplace — the last four are decoration overlays (their magenta
 * key is transparent, showing the wall behind). */
static const short g_cw_facet_piece[CW_FACETS] = { 8, 18, 27, 36, 45 };
static const short g_cw_base[CW_SLOTS] = { 32, 64, 96 };  /* clut band bases */
static unsigned char g_cw_sbody[CW_SLOTS][CW_FACETS][56 * 56]; /* facet pixels */
static short g_cw_fh[CW_SLOTS][CW_FACETS], g_cw_fw[CW_SLOTS][CW_FACETS];
static short g_cw_fxo[CW_SLOTS][CW_FACETS], g_cw_fyo[CW_SLOTS][CW_FACETS]; /* -bear */
static short g_cw_sid[CW_SLOTS];                     /* loaded id (0 = empty)*/
static unsigned char g_cw_sr[CW_SLOTS][CW_BAND];     /* band palette R/G/B   */
static unsigned char g_cw_sg[CW_SLOTS][CW_BAND];
static unsigned char g_cw_sb[CW_SLOTS][CW_BAND];
static unsigned char g_cw_strans[CW_SLOTS][CW_BAND]; /* 1 = magenta key (clear)*/
static unsigned char g_cw_remap[CW_SLOTS][4][CW_BAND]; /* depth->darker off  */
static short g_cw_grp[CW_SLOTS] = { -1, -1, -1 };    /* cached Wall1-3 ids   */
#define CW_CELL 56            /* wall-cell size the facet bearings sit in  */

/* Full piece store for the FAITHFUL renderer: all 48 pieces of one active
 * set, kept resident, blitted 1:1 at jt199/l5b42 screen positions (no
 * scaling) — the real Mac slot-assembly. The pieces' bytes are band values
 * (32..71) and the active set's palette band is loaded at clut 32 (by
 * load_color_wallset), so a byte IS its clut index. g_cwf_px gates the
 * colour blit in jt200_layer; NULL = the 1bpp DUNGCOM path. */
static unsigned char        g_cwf_buf[327680];       /* resident CTL file   */
static const unsigned char *g_cwf_body[CW_NPIECE];   /* piece pixel data    */
static short g_cwf_h[CW_NPIECE], g_cwf_w[CW_NPIECE];
static short g_cwf_xb[CW_NPIECE], g_cwf_yb[CW_NPIECE]; /* signed bearings    */
static short g_cwf_n;                                /* pieces loaded (0=no)*/
static short g_cwf_grp = -1;                         /* cached (file<<8|set) */
static unsigned char *g_cwf_px;                      /* blit surface (NULL off) */
static short g_cwf_pitch, g_cwf_sw, g_cwf_sh;
static short g_cwf_blits __attribute__((unused));    /* debug: blit count   */

/* Active environment selector — CW_SET picks the initial set at build
 * time (1=marble 2=forest 4=coral 6=lava 7=brick in 8X8DC); the walk demo
 * cycles g_cw_set / g_cw_file live (keys 't' / 'y') for regression. The
 * two files are the Gold Box dungeon wall libraries: 8X8DC (8 sets) and
 * 8X8DB (10 sets). g_cw_setmax is filled from the top GLIB count on load. */
#ifndef CW_SET
#define CW_SET 1
#endif
static const char *const g_cw_files[2] = { "\0118X8DC.CTL", "\0118X8DB.CTL" };
static short g_cw_file   = 0;        /* index into g_cw_files            */
static short g_cw_set    = CW_SET;   /* 1-based environment set          */
static short g_cw_setmax = 7;        /* valid sets in the current file   */
static short g_cw_auto = 1;          /* 1 = pick the set from the level's
                                      * Wall1; 't'/'y' set 0 to browse */
static short g_view_force_full = 0;  /* set on a live switch -> full clear+present next frame */

/* Backdrop (BACK.CTL) — the floor/ceiling/sky drawn behind the walls.
 * 20 backdrops, each an 88x88 8bpp image whose indices live at clut
 * [BACK_PAL_BASE..] (the image's own 32-colour palette is item 0); image
 * index 0 is the dark ceiling. The walls' transparent regions reveal it;
 * here render_3d_view paints it as the viewport background under the
 * (opaque) wall trapezoids. 'b' cycles backdrops in the walk demo. */
#define BACK_W 88
#define BACK_H 88
#define BACK_PAL_BASE 145
static unsigned char g_back_img[BACK_W * BACK_H];
static short g_back_w = 0, g_back_h = 0;   /* loaded dims (0 = none)       */
static short g_back_set = 1;               /* 1-based backdrop index       */
static short g_back_max = 19;
static short g_back_auto = 1;              /* 1 = pick per-cell from the map;
                                            * 'b' sets 0 to browse manually */

/* CW_CLEAR — cw_shade's "transparent pixel" sentinel (clut 255 is unused by
 * the dungeon view: walls 32..127, backdrop 145..176). Callers drawing an
 * overlay skip it to leave whatever's behind. */
#define CW_CLEAR 255

/* cw_shade — sample slot `slot`, facet `facet` at (col,row) and return the
 * depth-shaded clut index, or CW_CLEAR if the pixel is transparent (the
 * global key 255 or the per-set magenta key). The stored byte is a per-set
 * band value (32..); subtract 32 for the band offset, darken it for
 * `depth` via the slot's remap, add the slot's clut base. */
static unsigned char cw_shade(short slot, short facet, short depth,
                              short col, short row)
{
	short w, h, off;
	unsigned char raw;

	if (slot < 0 || slot >= CW_SLOTS || g_cw_sid[slot] == 0
	 || facet < 0 || facet >= CW_FACETS)
		return CW_CLEAR;
	w = g_cw_fw[slot][facet];
	h = g_cw_fh[slot][facet];
	if (w < 1 || h < 1)
		return CW_CLEAR;
	if (col < 0) col = 0; else if (col >= w) col = (short)(w - 1);
	if (row < 0) row = 0; else if (row >= h) row = (short)(h - 1);
	raw = g_cw_sbody[slot][facet][(long)row * w + col];
	if (raw == 255)                          /* global transparency key */
		return CW_CLEAR;
	off = (short)(raw - 32);
	if (off < 0) off = 0; else if (off >= CW_BAND) off = (short)(CW_BAND - 1);
	if (g_cw_strans[slot][off])              /* magenta key -> transparent */
		return CW_CLEAR;
	return (unsigned char)(g_cw_base[slot] + g_cw_remap[slot][depth & 3][off]);
}

/* fill_wall_trap_c — perspective-correct trapezoid for one side wall:
 * the plain wall (facet 0) tiled in a 32x32 window (offset `voff` down) of
 * slot `slot`, depth-shaded. If `facet` > 0 the cell's decoration is
 * overlaid: the trapezoid is one cell's side face, so its perspective depth
 * axis maps to the cell's horizontal (0..CW_CELL) and the decoration is
 * placed at its bearing, transparent pixels showing the wall. (Mapped onto
 * the slanted face, so foreshortened — the faithful oblique pieces aren't
 * used by this trapezoid renderer.) */
static void fill_wall_trap_c(unsigned char *px, short pitch, short sw, short sh,
                             short xNear, short xFar, short hNear, short hFar,
                             short vcy, short slot, short facet,
                             short voff, short depth)
{
	short lo = xNear < xFar ? xNear : xFar;
	short hi = xNear < xFar ? xFar : xNear;
	short denom = (short)(xFar - xNear);
	long invN = hNear > 0 ? 16384L / hNear : 0;
	long invF = hFar  > 0 ? 16384L / hFar  : 0;
	long dinv = invF - invN;
	short xo = facet > 0 ? g_cw_fxo[slot][facet] : 0;
	short yo = facet > 0 ? g_cw_fyo[slot][facet] : 0;
	short ow = facet > 0 ? g_cw_fw[slot][facet] : 0;
	short oh = facet > 0 ? g_cw_fh[slot][facet] : 0;
	short x, y;

	if (denom == 0) denom = 1;
	if (dinv == 0)  dinv = 1;
	for (x = lo; x <= hi; x++) {
		long  frac = ((long)(x - xNear) * 256) / denom;
		short ht, y0, y1, span, u, cu, v;
		long  inv;

		if (frac < 0) frac = -frac;
		ht = (short)(hNear + (((long)(hFar - hNear) * frac) >> 8));
		if (ht < 1) ht = 1;
		y0 = (short)(vcy - ht);
		y1 = (short)(vcy + ht);
		span = (short)(2 * ht);
		if (span < 1) span = 1;
		inv = 16384L / ht;
		u  = (short)(((inv - invN) * 32) / dinv);        /* 0 near .. 32 far */
		cu = (short)(((inv - invN) * CW_CELL) / dinv);   /* cell horiz 0..56 */
		for (y = y0; y <= y1; y++) {
			unsigned char c;
			v = (short)(((long)(y - y0) * 32) / span);
			c = cw_shade(slot, 0, depth, (short)(u & 31),
			             (short)(voff + (v & 31)));   /* facet 0 = plain wall */
			if (facet > 0) {
				short cv = (short)(((long)(y - y0) * CW_CELL) / span);
				short du = (short)(cu - xo), dv = (short)(cv - yo);
				if (du >= 0 && du < ow && dv >= 0 && dv < oh) {
					unsigned char o = cw_shade(slot, facet, depth, du, dv);
					if (o != CW_CLEAR) c = o;
				}
			}
			if (c != CW_CLEAR)
				map_px(px, pitch, sw, sh, x, y, c);
		}
	}
}


/* defined further down — used by cw_finalize / load_wall_groups */
static int load_backdrop(short n);
static int wallset_for_id(short id, short *file, short *set);

/* cw_load_slot — load wall-set (`file`,`set`) into slot `slot`: copy its
 * plain-wall piece (item 8) and its 32-colour palette band into the slot's
 * storage. The .CTL pieces are 8bpp chunky (the .TLB holds only 1/2bpp);
 * the per-set palette (sub-GLIB item 0) is RGB triples — set 1 marble grey,
 * set 2 forest green, etc. — and the tile bytes index it (band value 32..,
 * 0..20 light/dark ramp, 21..30 brown/fire, 31.. the magenta key). Returns
 * 1 on success. The big file buffer is shared+reused (we copy out). */
static int cw_load_slot(short slot, short file, short set)
{
	static unsigned char buf[327680];        /* holds 8X8DB.CTL (~296KB) */
	short refnum = 0, k;
	long count, base, sub, p0, p1;
	unsigned char metric[8];
	short h, w;

	g_cw_sid[slot] = 0;
	if (FSOpen((ConstStr255Param)g_cw_files[file & 1], 0, &refnum) != noErr)
		return 0;
	count = (long)sizeof buf;
	(void)FSRead(refnum, &count, buf);
	(void)FSClose(refnum);
	base = (long)(uintptr_t)buf;
	if (l37aa(base, 0) == 0)                  /* validate 'GLIB' magic */
		return 0;
	if (slot == 0) {                          /* track set count for 't' cycling */
		g_cw_setmax = (short)((((unsigned)buf[8] << 8) | buf[9])) - 1;
		if (g_cw_setmax < 1) g_cw_setmax = 1;
	}
	sub = l37aa(base, set);
	if (sub == 0)
		return 0;

	/* Copy each facet's near-face piece (item g_cw_facet_piece[facet]) out
	 * of the file buffer (it is reused next call), with its bearing — the
	 * signed (xbear,ybear) the piece sits at within the CW_CELL wall cell;
	 * we store the negated bearing as the draw offset. */
	{
		short fct;
		for (fct = 0; fct < CW_FACETS; fct++) {
			long b = l2856(sub, g_cw_facet_piece[fct], metric);
			short xo = 0, yo = 0;
			h = w = 0;
			if (b != 0) {
				const unsigned char *s =
					(const unsigned char *)(uintptr_t)b;
				long n;
				h = metric[1];
				w = (short)(8 * metric[6]);
				if (w > 56) w = 56;
				if (h > 56) h = 56;
				n = (long)h * w;
				for (count = 0; count < n; count++)
					g_cw_sbody[slot][fct][count] = s[count];
				/* metric ybear/xbear are signed words; the piece draws
				 * at (-xbear,-ybear) within the cell. */
				xo = (short)-(short)((metric[4] << 8) | metric[5]);
				yo = (short)-(short)((metric[2] << 8) | metric[3]);
			}
			g_cw_fh[slot][fct] = h;
			g_cw_fw[slot][fct] = w;
			g_cw_fxo[slot][fct] = xo;
			g_cw_fyo[slot][fct] = yo;
		}
	}

	/* item 0 = the set's RGB-triple palette -> the slot's band colours;
	 * flag the magenta key (255,103,255) entries as transparent. */
	p0 = l37aa(sub, 0);
	p1 = l37aa(sub, 1);
	{
		const unsigned char *pp = (p0 && p1 > p0)
			? (const unsigned char *)(uintptr_t)(p0 + 8) : NULL;
		short pe = pp ? (short)((p1 - p0 - 8) / 3) : 0;
		for (k = 0; k < CW_BAND; k++) {
			unsigned char r = (k < pe) ? pp[k * 3 + 0] : 0;
			unsigned char g = (k < pe) ? pp[k * 3 + 1] : 0;
			unsigned char b = (k < pe) ? pp[k * 3 + 2] : 0;
			g_cw_sr[slot][k] = r;
			g_cw_sg[slot][k] = g;
			g_cw_sb[slot][k] = b;
			g_cw_strans[slot][k] =
				(r == 255 && g == 103 && b == 255) ? 1 : 0;
		}
	}
	g_cw_sid[slot] = (short)(set ? set : 1);
	return 1;
}

/* cw_finalize — after the slots are loaded, push every loaded slot's band
 * into the clut, set the ceiling/floor fallback colours, build the per-slot
 * per-depth darken remap (nearest darker colour within the slot's own
 * band), and re-lay the backdrop band (which this clut write clobbers). */
static void cw_finalize(void)
{
	static const short fct[4] = { 256, 178, 128, 92 };
	static RGBColor pal[256];
	short i, slot, d, k, j;

	for (i = 0; i < 256; i++)
		pal[i].red = pal[i].green = pal[i].blue = 0;
	pal[4].red = 0x1000; pal[4].green = 0x0c00; pal[4].blue = 0x0800; /* ceiling */
	pal[5].red = 0x7000; pal[5].green = 0x4800; pal[5].blue = 0x2400; /* floor   */
	for (slot = 0; slot < CW_SLOTS; slot++) {
		short base = g_cw_base[slot];
		if (g_cw_sid[slot] == 0)
			continue;
		for (k = 0; k < CW_BAND; k++) {
			unsigned char r = g_cw_sr[slot][k];
			unsigned char g = g_cw_sg[slot][k];
			unsigned char b = g_cw_sb[slot][k];
			pal[base + k].red   = (unsigned short)((r << 8) | r);
			pal[base + k].green = (unsigned short)((g << 8) | g);
			pal[base + k].blue  = (unsigned short)((b << 8) | b);
		}
	}
	qd_set_palette(pal, 0, 256);

	for (slot = 0; slot < CW_SLOTS; slot++)
		for (d = 0; d < 4; d++)
			for (k = 0; k < CW_BAND; k++) {
				short tr = (short)((g_cw_sr[slot][k] * fct[d]) >> 8);
				short tg = (short)((g_cw_sg[slot][k] * fct[d]) >> 8);
				short tb = (short)((g_cw_sb[slot][k] * fct[d]) >> 8);
				long  bestd = 0x7fffffffL;
				short best = k;
				for (j = 0; j < CW_BAND; j++) {
					long dr = g_cw_sr[slot][j] - tr;
					long dg = g_cw_sg[slot][j] - tg;
					long db = g_cw_sb[slot][j] - tb;
					long dd2 = dr * dr + dg * dg + db * db;
					if (dd2 < bestd) { bestd = dd2; best = j; }
				}
				g_cw_remap[slot][d][k] = (unsigned char)best;
			}

	load_backdrop(g_back_set);
}

/* load_color_wallset — manual/initial path: load the current (g_cw_file,
 * `set`) into slot 0 and leave slots 1-2 empty, so every face uses it. */
static int load_color_wallset(short set)
{
	cw_load_slot(0, g_cw_file, set);
	g_cw_sid[1] = g_cw_sid[2] = 0;
	g_cw_grp[0] = g_cw_grp[1] = g_cw_grp[2] = -1;  /* invalidate auto cache */
	cw_finalize();
	return g_cw_sid[0] ? 1 : 0;
}

/* load_wall_groups — auto path: load the level's three wall groups (Wall1-3
 * = ds[4..6]) into slots 0-2 so each map face can use its own set. */
static void load_wall_groups(const unsigned char *ds)
{
	short i, file, set;
	for (i = 0; i < CW_SLOTS; i++) {
		short id = (short)(unsigned char)ds[4 + i];
		g_cw_grp[i] = id;
		if (wallset_for_id(id, &file, &set))
			cw_load_slot(i, file, set);
		else
			g_cw_sid[i] = 0;
	}
	cw_finalize();
}

/* load_backdrop — load BACK.CTL backdrop `n` (1-based): the 88x88 8bpp
 * floor/ceiling/sky image (sub-GLIB item 1) into g_back_img, and its own
 * 32-colour palette (item 0) into the screen clut at BACK_PAL_BASE — the
 * band the image's indices point into. The magenta transparency-key
 * entries are folded to a dark tone so the horizon band reads as shadow
 * rather than bright pink. Leaves clut 0..144 (EGA + clut129 + the wall
 * band at 32) untouched so it composes with the active wall set. */
static int load_backdrop(short n)
{
	static unsigned char buf[163840];     /* BACK.CTL is ~150KB */
	static RGBColor bpal[32];
	short refnum = 0, k, pe;
	long count, base, sub, p0, p1, img;
	unsigned char metric[8];
	short w, h;

	g_back_w = g_back_h = 0;
	if (FSOpen((ConstStr255Param)"\010BACK.CTL", 0, &refnum) != noErr)
		return 0;
	count = (long)sizeof buf;
	(void)FSRead(refnum, &count, buf);
	(void)FSClose(refnum);
	base = (long)(uintptr_t)buf;
	if (l37aa(base, 0) == 0)
		return 0;
	g_back_max = (short)((((unsigned)buf[8] << 8) | buf[9])) - 1;
	if (g_back_max < 1) g_back_max = 1;
	if (n < 1) n = 1;
	if (n > g_back_max) n = g_back_max;
	sub = l37aa(base, n);
	if (sub == 0)
		return 0;

	/* item 1 = the 88x88 image; copy it out (buf gets reused next call). */
	img = l2856(sub, 1, metric);
	if (img == 0)
		return 0;
	h = metric[1];
	w = (short)(metric[6] * 8);
	if (w > BACK_W) w = BACK_W;
	if (h > BACK_H) h = BACK_H;
	{
		const unsigned char *s = (const unsigned char *)(uintptr_t)img;
		long nbytes = (long)w * h;
		for (count = 0; count < nbytes; count++)
			g_back_img[count] = s[count];
	}
	g_back_w = w;
	g_back_h = h;

	/* item 0 = the backdrop's RGB-triple palette -> clut[BACK_PAL_BASE..]. */
	p0 = l37aa(sub, 0);
	p1 = l37aa(sub, 1);
	if (p0 == 0 || p1 <= p0)
		return 1;                         /* image loaded; palette as-is */
	{
		const unsigned char *pp = (const unsigned char *)(uintptr_t)(p0 + 8);
		pe = (short)((p1 - p0 - 8) / 3);
		if (pe > 32) pe = 32;
		for (k = 0; k < pe; k++) {
			unsigned char r = pp[k * 3 + 0];
			unsigned char g = pp[k * 3 + 1];
			unsigned char b = pp[k * 3 + 2];
			/* The bright horizon/transparency markers (magenta key and
			 * the cyan transition) read as speckle noise here — fold them
			 * to a dark shadow tone so the horizon band stays neutral. */
			if ((r == 255 && g == 103 && b == 255) ||
			    (r == 127 && g == 255 && b == 255)) {
				r = 0x18; g = 0x14; b = 0x10;
			}
			bpal[k].red   = (unsigned short)((r << 8) | r);
			bpal[k].green = (unsigned short)((g << 8) | g);
			bpal[k].blue  = (unsigned short)((b << 8) | b);
		}
		qd_set_palette(bpal, BACK_PAL_BASE, pe);
	}
	return 1;
}

/* ua_backdrop_to_back — map a FRUA level-header backdrop id (the values
 * stored in ds[8..11]) to a BACK.CTL backdrop index (1-based). The day
 * backdrops (UA 1..13) index BACK.CTL directly; the night variants
 * (UA 32..37) are the six images stored after them, at 14..19; anything
 * else (e.g. 255 'none' or the 240 overland marker) falls back to 1. */
static short ua_backdrop_to_back(short ua)
{
	short id;
	if (ua >= 1 && ua <= 19)        id = ua;
	else if (ua >= 32 && ua <= 37)  id = (short)(ua - 18);
	else                            id = 1;
	if (id > g_back_max) id = g_back_max;
	if (id < 1)          id = 1;
	return id;
}

/* cell_backdrop_id — the BACK.CTL backdrop for the party's current cell.
 * Each map cell's 6th byte is its BackdropZone: the low two bits pick one
 * of the level's four backdrops (ds[8..11] = Backdrop1..4), the high bits
 * are the monster/event zone. So the floor/ceiling can change as the
 * party walks between zones. */
static short cell_backdrop_id(const unsigned char *ds)
{
	short x = (short)g_a5_12288, y = (short)g_a5_12287;
	short w = (unsigned char)ds[2], h = (unsigned char)ds[3];
	long  cell;
	unsigned char zone;

	if (x < 0 || y < 0 || x >= w || y >= h)
		return g_back_set;
	cell = (long)x * h + y;
	zone = ds[290 + cell * 6 + 5];
	return ua_backdrop_to_back((short)(unsigned char)ds[8 + (zone & 3)]);
}

/* wallset_for_id — map a FRUA wall-set id (level header Wall1-3, ds[4..6])
 * to a wall library + set. The Mac engine builds the art name "8x8d%c%d"
 * (CODE 7 L6eea): the letter is 'b' for id < 10, 'c' for id >= 10, and the
 * number is the set within that library. The port's gamedata combined the
 * Mac's separate 8x8db1..9 / 8x8dc1..7 files into 8X8DB.CTL / 8X8DC.CTL as
 * sub-GLIB sets, so id 1..9 -> 8X8DB set id, id 10..16 -> 8X8DC set id-9.
 * Returns 1 and fills file/set for a real wall id, 0 for none/overland. */
static int wallset_for_id(short id, short *file, short *set)
{
	if (id < 1 || id == 255)            /* 255 = overland / no wall set */
		return 0;
	if (id < 10) { *file = 1; *set = id; }       /* 8X8DB */
	else         { *file = 0; *set = (short)(id - 9); }  /* 8X8DC */
	return 1;
}

/* wall_slot_for_edge — given a map edge byte, return the wall-set slot to
 * draw that face with, or -1 for no wall. The low nibble (1-15) is the wall
 * slot: 1-5 use Wall1, 6-10 Wall2, 11-15 Wall3 (UAF OffsetWallSlotIndex),
 * i.e. group (w-1)/5 -> slot. In manual ('t'/'y') mode every face uses
 * slot 0; an unloaded group also falls back to slot 0. */
static short wall_slot_for_edge(short e)
{
	short w = (short)(e & 0x0F);
	short slot;

	if (w == 0)
		return -1;
	slot = g_cw_auto ? (short)((w - 1) / 5) : 0;
	if (slot < 0) slot = 0; else if (slot >= CW_SLOTS) slot = (short)(CW_SLOTS - 1);
	if (g_cw_sid[slot] == 0)
		slot = 0;
	return slot;
}

/* facet_for_edge — the decoration facet (0-4) for a map edge byte: the low
 * nibble's position within its group. 0 (plain) in manual browse mode. */
static short facet_for_edge(short e)
{
	short w = (short)(e & 0x0F);
	return (g_cw_auto && w) ? (short)(((w - 1) % CW_FACETS)) : 0;
}

/* render_3d_view — a textured first-person corridor view from the
 * party's (x,y,facing). Walks depth slices 0..3; at each depth draws
 * the left/right side walls as perspective-correct textured
 * trapezoids and, where the way is blocked, the textured front face
 * (then stops), over a ceiling/floor split. Each wall's texture is
 * selected per-edge (pick_wall) from the wall-tile table, depth-
 * shaded via the per-depth fg/bg colour pair. Viewport ~220x150
 * centred at (118,83). */
static void render_3d_view(unsigned char *px, short pitch, short sw, short sh)
	__attribute__((unused));
static void render_3d_view(unsigned char *px, short pitch, short sw, short sh)
{
	static const short hw[5] = { 110, 68, 42, 26, 16 };
	static const short hh[5] = {  74, 46, 28, 17, 11 };
	const short vcx = 118, vcy = 83;
	short f  = (short)(g_a5_12286 & 7);
	short lf = (short)((f + 6) & 7);
	short rf = (short)((f + 2) & 7);
	short x, y, d;

	{
		short y0 = (short)(vcy - hh[0]), y1 = (short)(vcy + hh[0]);
		short x0 = (short)(vcx - hw[0]), x1 = (short)(vcx + hw[0]);
		short vh = (short)(y1 - y0 + 1), vw = (short)(x1 - x0 + 1);
		for (y = y0; y <= y1; y++) {
			short by = g_back_h ? (short)(((long)(y - y0) * g_back_h) / vh) : 0;
			for (x = x0; x <= x1; x++) {
				short c;
				if (g_back_w) {
					short bx = (short)(((long)(x - x0) * g_back_w) / vw);
					unsigned char v = g_back_img[(long)by * g_back_w + bx];
					/* image idx 0 = dark ceiling; else the backdrop band. */
					c = v ? (short)v : 4;
				} else {
					c = (y < vcy) ? 4 : 5;  /* fallback solid split */
				}
				map_px(px, pitch, sw, sh, x, y, (unsigned char)c);
			}
		}
	}

	/* Each wall face's texture comes from its own set: the edge byte's low
	 * nibble (1-15) picks one of the level's three wall groups (Wall1-3 ->
	 * slots 0-2) via wall_slot_for_edge; VOFF skips piece 8's top edge band. */
	const short VOFF = 8;

	for (d = 0; d < 4; d++) {
		short cx = (short)((short)g_a5_12288 + dir_dx[f] * d);
		short cy = (short)((short)g_a5_12287 + dir_dy[f] * d);
		short fd = (short)(d + 1 < 4 ? d + 1 : 3);
		short sl, sr2, sff, el, er;

		el = cell_edge(cx, cy, lf);
		sl = wall_slot_for_edge(el);
		if (sl >= 0)
			fill_wall_trap_c(px, pitch, sw, sh,
			                 (short)(vcx - hw[d]), (short)(vcx - hw[d + 1]),
			                 hh[d], hh[d + 1], vcy, sl, facet_for_edge(el), VOFF, d);
		er = cell_edge(cx, cy, rf);
		sr2 = wall_slot_for_edge(er);
		if (sr2 >= 0)
			fill_wall_trap_c(px, pitch, sw, sh,
			                 (short)(vcx + hw[d]), (short)(vcx + hw[d + 1]),
			                 hh[d], hh[d + 1], vcy, sr2, facet_for_edge(er), VOFF, d);
		{
			short fe = cell_edge(cx, cy, f);
			sff = wall_slot_for_edge(fe);
			if (sff < 0)
				continue;
			{
			/* Front face: the full plain wall (facet 0) scaled to the
			 * cell, with the cell's facet decoration (window/door/...)
			 * overlaid at its bearing, transparent pixels showing the
			 * wall. Facets only in the map-driven mode. */
			short fac = facet_for_edge(fe);
			short xl = (short)(vcx - hw[d + 1]), xr = (short)(vcx + hw[d + 1]);
			short yt = (short)(vcy - hh[d + 1]), yb = (short)(vcy + hh[d + 1]);
			short fw = (short)(xr - xl), fh = (short)(yb - yt);
			short xo = g_cw_fxo[sff][fac], yo = g_cw_fyo[sff][fac];
			short ow = g_cw_fw[sff][fac], oh = g_cw_fh[sff][fac];

			if (fw < 1) fw = 1;
			if (fh < 1) fh = 1;
			for (x = xl; x <= xr; x++) {
				short cu = (short)(((long)(x - xl) * CW_CELL) / fw);
				for (y = yt; y <= yb; y++) {
					short cv = (short)(((long)(y - yt) * CW_CELL) / fh);
					unsigned char c = cw_shade(sff, 0, fd, cu, cv);
					if (fac > 0) {
						short du = (short)(cu - xo), dv = (short)(cv - yo);
						if (du >= 0 && du < ow && dv >= 0 && dv < oh) {
							unsigned char o = cw_shade(sff, fac, fd, du, dv);
							if (o != CW_CLEAR) c = o;
						}
					}
					if (c != CW_CLEAR)
						map_px(px, pitch, sw, sh, x, y, c);
				}
			}
			}
			break;
		}
	}
}

/* draw_front_face — draw wall-set slot `slot`'s plain wall (facet 0) scaled
 * into the rect [xl,xr]x[yt,yb], then overlay its facet decoration at the
 * facet's bearing (transparent pixels show the wall). Shared by the corridor
 * and frustum renderers. `fd` is the depth-shade level. */
static void draw_front_face(unsigned char *px, short pitch, short sw, short sh,
                            short xl, short xr, short yt, short yb,
                            short slot, short fac, short fd)
{
	short fw = (short)(xr - xl), fh = (short)(yb - yt);
	short xo = g_cw_fxo[slot][fac], yo = g_cw_fyo[slot][fac];
	short ow = g_cw_fw[slot][fac], oh = g_cw_fh[slot][fac];
	short x, y;

	if (fw < 1) fw = 1;
	if (fh < 1) fh = 1;
	for (x = xl; x <= xr; x++) {
		short cu = (short)(((long)(x - xl) * CW_CELL) / fw);
		for (y = yt; y <= yb; y++) {
			short cv = (short)(((long)(y - yt) * CW_CELL) / fh);
			unsigned char c = cw_shade(slot, 0, fd, cu, cv);
			if (fac > 0) {
				short du = (short)(cu - xo), dv = (short)(cv - yo);
				if (du >= 0 && du < ow && dv >= 0 && dv < oh) {
					unsigned char o = cw_shade(slot, fac, fd, du, dv);
					if (o != CW_CLEAR) c = o;
				}
			}
			if (c != CW_CLEAR)
				map_px(px, pitch, sw, sh, x, y, c);
		}
	}
}

/* render_3d_raycast — the faithful-frustum view: the wider 3-cell-wide
 * field jt199 walks (left/center/right columns), versus render_3d_view's
 * single corridor. Drawn back-to-front (far depth first) so nearer cells
 * overdraw farther ones (occlusion), using the same perspective ramp and
 * colour wall/facet system. The lateral column step is 2*hw[d+1], matching
 * the front-face width, so adjacent columns' front faces tile exactly (no
 * chevrons). This is jt199's visibility logic over our texture renderer —
 * the Mac's own l5b42 pixel coords aren't reconstructible (see docs/TODO). */
static void render_3d_raycast(unsigned char *px, short pitch, short sw, short sh)
{
	static const short hw[5] = { 110, 68, 42, 26, 16 };
	static const short hh[5] = {  74, 46, 28, 17, 11 };
	const short vcx = 118, vcy = 83, VOFF = 8;
	short f  = (short)(g_a5_12286 & 7);
	short lf = (short)((f + 6) & 7);
	short rf = (short)((f + 2) & 7);
	short L, d;

	/* backdrop (floor/ceiling/sky) over the whole viewport */
	{
		short y0 = (short)(vcy - hh[0]), y1 = (short)(vcy + hh[0]);
		short x0 = (short)(vcx - hw[0]), x1 = (short)(vcx + hw[0]);
		short vh = (short)(y1 - y0 + 1), vw = (short)(x1 - x0 + 1);
		short x, y;
		for (y = y0; y <= y1; y++) {
			short by = g_back_h ? (short)(((long)(y - y0) * g_back_h) / vh) : 0;
			for (x = x0; x <= x1; x++) {
				short c;
				if (g_back_w) {
					short bx = (short)(((long)(x - x0) * g_back_w) / vw);
					unsigned char v = g_back_img[(long)by * g_back_w + bx];
					c = v ? (short)v : 4;
				} else {
					c = (y < vcy) ? 4 : 5;
				}
				map_px(px, pitch, sw, sh, x, y, (unsigned char)c);
			}
		}
	}

	/* frustum: far depth first, three lateral columns; near overdraws far.
	 * A side column (L=±1) is only visible THROUGH an opening — draw it only
	 * where the centre corridor's wall toward it is open at that depth (the
	 * occlusion jt199's walk does); otherwise the corridor wall hides it.
	 * The centre column then paints last (below), covering closed walls. */
	for (d = 3; d >= 0; d--) {
		short fd = (short)(d + 1 < 4 ? d + 1 : 3);
		short colw = (short)(2 * hw[d + 1]);   /* lateral column step */
		short ccx = (short)((short)g_a5_12288 + dir_dx[f] * d);
		short ccy = (short)((short)g_a5_12287 + dir_dy[f] * d);
		for (L = -1; L <= 1; L++) {
			short cx  = (short)((short)g_a5_12288 + dir_dx[f] * d + dir_dx[rf] * L);
			short cy  = (short)((short)g_a5_12287 + dir_dy[f] * d + dir_dy[rf] * L);
			short cxs = (short)(vcx + L * colw);
			short el, er, fe, sl, sr2, sff;

			/* gate side columns on a clear line of sight through the wall */
			if (L < 0 && (cell_edge(ccx, ccy, lf) & 0x0F))
				continue;                 /* left wall closed -> can't see left */
			if (L > 0 && (cell_edge(ccx, ccy, rf) & 0x0F))
				continue;                 /* right wall closed -> can't see right */

			el = cell_edge(cx, cy, lf);
			sl = wall_slot_for_edge(el);
			if (sl >= 0)
				fill_wall_trap_c(px, pitch, sw, sh,
				                 (short)(cxs - hw[d]), (short)(cxs - hw[d + 1]),
				                 hh[d], hh[d + 1], vcy, sl, facet_for_edge(el),
				                 VOFF, d);
			er = cell_edge(cx, cy, rf);
			sr2 = wall_slot_for_edge(er);
			if (sr2 >= 0)
				fill_wall_trap_c(px, pitch, sw, sh,
				                 (short)(cxs + hw[d]), (short)(cxs + hw[d + 1]),
				                 hh[d], hh[d + 1], vcy, sr2, facet_for_edge(er),
				                 VOFF, d);
			fe = cell_edge(cx, cy, f);
			sff = wall_slot_for_edge(fe);
			if (sff >= 0)
				draw_front_face(px, pitch, sw, sh,
				                (short)(cxs - hw[d + 1]), (short)(cxs + hw[d + 1]),
				                (short)(vcy - hh[d + 1]), (short)(vcy + hh[d + 1]),
				                sff, facet_for_edge(fe), fd);
		}
	}
}

/* draw_map_tiles — render the currently-loaded design-state map as
 * TOPVIEW automap tiles (shared with port_render_geo_tiles, edge
 * order [N,E,S,W] per JT[202]). `tvbase` is a loaded TOPVIEW GLIB. */
static void draw_map_tiles(long tvbase, unsigned char *px,
                           short pitch, short sw, short sh)
{
	unsigned char metric[8];
	const unsigned char *ds =
		(const unsigned char *)(uintptr_t)g_a5_long(-12300);
	const unsigned char *map;
	short w, h, x, y;
	static const short door_tile[4] = { 17, 20, 19, 18 };  /* N,E,S,W */

	if (ds == NULL)
		return;
	w = (unsigned char)ds[2];
	h = (unsigned char)ds[3];
	map = ds + 290;
	if (w <= 0 || h <= 0 || (long)w * h > 576)
		return;

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			const unsigned char *t = map + ((long)x * h + y) * 6;
			short px0 = (short)(x * 16), py0 = (short)(y * 16);
			short mask = (short)(((t[0] & 0x80) ? 1 : 0)
			                   | ((t[1] & 0x80) ? 2 : 0)
			                   | ((t[2] & 0x80) ? 4 : 0)
			                   | ((t[3] & 0x80) ? 8 : 0));
			long bmp = l2856(tvbase, (short)(1 + mask), metric);
			short e;

			if (bmp != 0)
				blit_glyph_1bpp(px, pitch, sw, sh, metric,
				                (const unsigned char *)(uintptr_t)bmp,
				                px0, py0, 1, 1, 0);
			for (e = 0; e < 4; e++) {
				long db;
				if (t[e] == 0 || (t[e] & 0x80))
					continue;
				db = l2856(tvbase, door_tile[e], metric);
				if (db != 0)
					blit_glyph_1bpp(px, pitch, sw, sh, metric,
					                (const unsigned char *)(uintptr_t)db,
					                px0, py0, 2, 1, 1);
			}
		}
	}
}

/* ===== jt995 option A — the bit-packed-page blit foundation =====
 *
 * FRUA's faithful bitmap path (jt995 -> the JT[1181] family) composites
 * 1bpp source into a bit-packed page, then the page is converted to the
 * screen. bp_blit_or is the lift of JT[1181] (CODE 4 + 0xcf2), the OR /
 * set primitive: each 16-bit source word is dropped into the high half
 * of a 32-bit window and shifted right by a sub-word pen offset (0..15),
 * then OR'd word-by-word into the destination long (a3 advances one
 * word per source word, so a shifted word straddles two dest words and
 * the overlap accumulates). lmask/rmask trim the first/last words.
 *
 * The page is 1bpp bit-packed (stride bytes/row); bp_present expands it
 * to the 8-bit shim buffer (set bit -> fg pen, clear -> bg). jt995's
 * clip math (both axes) and the jt1135 coord remap are lifted in bp_blit
 * below; the column cursor (L05dc), the collision-test primitive
 * (JT[1188]), and wiring this under the 3D view follow. */
#define BP_STRIDE 64                       /* 512 px/row, bit-packed */
#define BP_ROWS   480                      /* covers the shim surface height */

static void bp_blit_or(unsigned char *page, short dx, short dy,
                       const unsigned char *src, short src_stride,
                       short nwords, short h, unsigned short lmask,
                       unsigned short rmask)
{
	short shift = (short)(dx & 15);
	long  byteoff = (long)(dx >> 4) * 2;
	short r, w;

	for (r = 0; r < h; r++) {
		long                 row = (long)(dy + r) * BP_STRIDE;
		const unsigned char *s = src + (long)r * src_stride;

		for (w = 0; w < nwords; w++) {
			unsigned long sword = ((unsigned long)s[w * 2] << 8)
			                    | s[w * 2 + 1];
			long          off = byteoff + (long)w * 2;
			unsigned long win;
			int           k;

			if (w == 0)
				sword &= lmask;
			if (w == nwords - 1)
				sword &= rmask;
			win = (sword << 16) >> shift;          /* 32-bit window */
			/* OR the 4-byte window into the dest, clamping each byte
			 * to the row: a word straddling the page edge writes only
			 * its on-page bytes (jt995's clip guarantees on-page; the
			 * clamp is the safe equivalent for sub-word edge spans). */
			for (k = 0; k < 4; k++) {
				long o = off + k;
				if (o < 0 || o >= BP_STRIDE)
					continue;
				page[row + o] |= (unsigned char)(win >> (24 - k * 8));
			}
		}
	}
}

/* bp_blit_andnot — lift of JT[1184] (CODE 4 + 0xd7c), the AND-NOT /
 * clear primitive: like bp_blit_or but the source word is inverted
 * (notw), windowed, inverted again (notl), and AND'd into the dest
 * (*dest &= ~win) — the mask/clear pass that pairs with bp_blit_or to
 * composite a 1bpp sprite (clear the footprint, then OR the data). */
static void bp_blit_andnot(unsigned char *page, short dx, short dy,
                           const unsigned char *src, short src_stride,
                           short nwords, short h, unsigned short lmask,
                           unsigned short rmask)
{
	short shift = (short)(dx & 15);
	long  byteoff = (long)(dx >> 4) * 2;
	short r, w;

	for (r = 0; r < h; r++) {
		long                 row = (long)(dy + r) * BP_STRIDE;
		const unsigned char *s = src + (long)r * src_stride;

		for (w = 0; w < nwords; w++) {
			unsigned short inv = (unsigned short)(~(((unsigned short)s[w * 2] << 8)
			                                       | s[w * 2 + 1]));
			long           off = byteoff + (long)w * 2;
			unsigned long  win;
			int            k;

			if (w == 0)
				inv &= lmask;
			if (w == nwords - 1)
				inv &= rmask;
			win = ((unsigned long)inv << 16) >> shift;
			/* clear (AND-NOT) the 4-byte window, clamped to the row. */
			for (k = 0; k < 4; k++) {
				long o = off + k;
				if (o < 0 || o >= BP_STRIDE)
					continue;
				page[row + o] &= (unsigned char)~(win >> (24 - k * 8));
			}
		}
	}
}

/* bp_present — expand the 1bpp bit-packed page to the 8-bit shim
 * buffer: each set bit -> fg, clear -> bg. */
static void bp_present(const unsigned char *page, unsigned char *px,
                       short pitch, short sw, short sh,
                       unsigned char fg, unsigned char bg)
{
	short x, y;

	if (sh > BP_ROWS)
		sh = BP_ROWS;
	for (y = 0; y < sh; y++) {
		const unsigned char *row = page + (long)y * BP_STRIDE;
		unsigned char       *o   = px + (long)y * pitch;
		for (x = 0; x < sw && (x >> 3) < BP_STRIDE; x++)
			o[x] = ((row[x >> 3] >> (7 - (x & 7))) & 1) ? fg : bg;
	}
}

/* bp_blit — the 1:1 clipped tile blit, i.e. the leaf L2d4e (CODE 5 +
 * 0x2d4e) that the dungeon blit JT[999]/L309c delegates to (and the
 * structural twin of jt995's per-plane blit). Takes already-remapped
 * dest coords (x, y): the VIDEL coord remap (jt1135) and the glyph
 * bearing adjust belong to the L309c entry above the leaf, not here.
 * Decodes the glyph metric (height @0 word, bytes/row @6), clips the
 * bitmap to the page in BOTH axes, then dispatches by mode: 0 = draw
 * (OR / set), 1 = clear (AND-NOT).
 *
 * Horizontal clip mirrors L2d4e / jt995's setup (0x230c..0x23fe):
 * intersect the blit's pixel span with the clip rect, reduce to the
 * visible source words, and trim the partial edge words with lmask/
 * rmask. The Mac reads those masks from the edge-mask tables at
 * g_a5_-4646 / g_a5_-4614 and clips against the clip-rect globals
 * g_a5_-3050..-3056; here the clip rect is the page itself ([0,pw) x
 * [0,BP_ROWS)) and the masks are computed directly. The collision
 * (mode 1/3) / 2-source (mode 2) variants remain ahead. */
static void bp_blit(unsigned char *page, short x, short y,
                    const unsigned char *metric, const unsigned char *src,
                    short mode)
{
	short h      = (short)(((unsigned short)metric[0] << 8) | metric[1]);
	short bpp_w  = (short)metric[6];
	short nwords = (short)((bpp_w + 1) / 2);
	short pw     = BP_STRIDE * 8;            /* page width in pixels */
	short sx = x, sy = y;
	short r0 = 0;
	short Wpx, sp0, sp1, w0, w1, nw, ddx, rb;
	unsigned short lmask, rmask;

	if (h <= 0 || nwords <= 0)
		return;

	/* vertical clip */
	if (sy < 0)
		r0 = (short)(-sy);
	if (sy + h > BP_ROWS)
		h = (short)(BP_ROWS - sy);
	if (r0 >= h)
		return;                          /* fully off-page vertically */

	/* horizontal clip: visible source-pixel span [sp0, sp1) against
	 * the page, then the source words it covers + edge masks. */
	Wpx = (short)(nwords * 16);
	sp0 = (sx < 0) ? (short)(-sx) : 0;
	sp1 = (sx + Wpx > pw) ? (short)(pw - sx) : Wpx;
	if (sp0 >= sp1)
		return;                          /* fully off-page horizontally */
	w0  = (short)(sp0 >> 4);                 /* first source word touched */
	w1  = (short)((sp1 - 1) >> 4);           /* last source word touched  */
	nw  = (short)(w1 - w0 + 1);
	ddx = (short)(sx + w0 * 16);             /* dest x of word w0 (may be <0) */
	lmask = (unsigned short)(0xffffu >> (sp0 & 15));     /* drop leading bits */
	rb    = (short)(sp1 & 15);
	rmask = rb ? (unsigned short)(0xffffu << (16 - rb))  /* keep up to sp1   */
	           : (unsigned short)0xffff;

	{
		const unsigned char *s = src + (long)r0 * bpp_w + (long)w0 * 2;
		short rows = (short)(h - r0);
		short dy   = (short)(sy + r0);

		if (mode == 1)
			bp_blit_andnot(page, ddx, dy, s, bpp_w, nw, rows,
			               lmask, rmask);
		else
			bp_blit_or(page, ddx, dy, s, bpp_w, nw, rows,
			           lmask, rmask);
	}
}

/* jt1004 (JT[1004], CODE 5 + 0x2850) — return the current wall-tile
 * library handle (g_a5_-4582), the DUNGCOM GLIB the dungeon view blits
 * its pre-rendered slot tiles from. (The Mac body is a one-line
 * `movel g_a5_-4582,d0; rts`.) */
static long jt1004_handle(void) __attribute__((unused));
static long jt1004_handle(void)
{
	return g_a5_long(-4582);
}

/* l309c — JT[999] (CODE 5 + 0x309c), the dungeon's bitmap blit entry
 * (mono path). Remaps the dest coords (jt1135, the VIDEL anchor remap),
 * fetches tile `idx` from `handle` (l2856 -> 8-byte glyph metric + the
 * 1bpp bits), shifts by the glyph bearing (ybear @2, xbear @4), and
 * draws 1:1 through the leaf bp_blit (= L2d4e -> L2970 for 1bpp).
 *
 * Faithful to L309c's prologue (0x30a0..0x30ea): jt1135 remap, l2856
 * fetch, early-out on no glyph, then (top,left) -= (ybear,xbear). The
 * multi-part-sprite arm (metric[7]&15 == 9, which recurses over 6-byte
 * sub-part descriptors via JT[406]) and L2d4e's deep / composite arms
 * remain ahead — opaque 1bpp wall tiles take the mono OR path. Threads
 * an explicit page instead of the Mac page descriptor (g_a5_-2570);
 * distinct from the older l309c() channel stub that jt1001 still uses. */
static void l309c_tile(unsigned char *page, short top, short left,
                       long handle, short idx) __attribute__((unused));
static void l309c_tile(unsigned char *page, short top, short left,
                       long handle, short idx)
{
	unsigned char metric[8];
	long  info;
	short sy = top, sx = left;
	short ybear, xbear;

	jt1135(top, left, &sy, &sx);
	info = l2856(handle, idx, metric);
	if (info == 0)
		return;
	ybear = (short)(((unsigned short)metric[2] << 8) | metric[3]);
	xbear = (short)(((unsigned short)metric[4] << 8) | metric[5]);
	bp_blit(page, (short)(sx - xbear), (short)(sy - ybear),
	        metric, (const unsigned char *)(uintptr_t)info, 0);
}

/* l5baa (CODE 7 + 0x5baa) — is map cell (row, col) inside the loaded
 * design's GEO map? The map is column-major with stride = height: the
 * row index spans [0, ds[3]) and the column index spans [0, ds[2])
 * (ds = design state g_a5_-12300; ds[2] = width, ds[3] = height). */
static int l5baa(short row, short col) __attribute__((unused));
static int l5baa(short row, short col)
{
	const unsigned char *ds = (const unsigned char *)(uintptr_t)g_a5_long(-12300);

	if (ds == NULL)
		return 0;
	if (row < 0 || row > (short)ds[3] - 1)
		return 0;
	if (col < 0 || col > (short)ds[2] - 1)
		return 0;
	return 1;
}

/* jt210 / l5bfa (JT[210], CODE 7 + 0x5bfa) — read the wall-art code of
 * map cell (row, col)'s edge facing direction `dir`. The cell readers
 * the frustum walker (JT[199]) uses to probe walls along each view ray.
 *
 * Faithful: an odd `dir` first steps one cell along the direction-delta
 * tables (g_a5_-27862 = drow, g_a5_-27853 = dcol, indexed by the raw
 * dir, signed bytes) and rotates dir by 2; then the cell is bounds-
 * checked (l5baa -> 0 = no wall when off-map), the edge index is
 * (dir & 6) >> 1, and the cell's edge byte at map+ (col*height + row)*6
 * + edge is read — its HIGH nibble is the wall-art code (the low nibble
 * is the movement type). The map lives at design_state + 290. */
static short jt210(short row, short col, short dir) __attribute__((unused));
static short jt210(short row, short col, short dir)
{
	const unsigned char *ds, *cell;
	short edge;

	if (dir & 1) {
		dir--;
		row = (short)(row + (signed char)g_a5_byte(-27862 + dir));
		col = (short)(col + (signed char)g_a5_byte(-27853 + dir));
		dir = (short)((dir + 2) & 6);
	}
	if (!l5baa(row, col))
		return 0;                        /* off-map: no wall */
	edge = (short)((dir & 6) >> 1);
	ds   = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	cell = ds + 290 + ((long)col * ds[3] + row) * 6 + edge;
	return (short)((*cell >> 4) & 15);
}

/* cw_blit_piece — blit one full-set piece `idx` (1-based) at screen
 * (top,left), 1:1, into the faithful blit surface. The piece sits at
 * (top - ybear, left - xbear) in the cell (the glyph bearing convention);
 * bytes are clut indices (32..71 via the active set's clut-32 band),
 * skipping the 255 / magenta transparency key. No scaling, no shade. */
static void cw_blit_piece(short top, short left, short idx)
{
	const unsigned char *b;
	short w, h, x, y, ox, oy;

	if (g_cwf_px == NULL || idx < 1 || idx >= CW_NPIECE)
		return;
	b = g_cwf_body[idx];
	if (b == NULL)
		return;
	w = g_cwf_w[idx];
	h = g_cwf_h[idx];
	/* Mac Rect convention: top = Y (vertical), left = X (horizontal). */
	ox = (short)(left - g_cwf_xb[idx]);
	oy = (short)(top  - g_cwf_yb[idx]);
#ifdef FRUA_ENGINE_PROBE
	if (g_cwf_blits < 12) {
		dbg_log_num("blit idx=", (long)idx);
		dbg_log_num("   top=", (long)top);
		dbg_log_num("  left=", (long)left);
	}
	g_cwf_blits++;
#endif
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			unsigned char v = b[(long)y * w + x];
			short off;
			if (v == 255)
				continue;                 /* global transparency key */
			off = (short)(v - 32);
			if (off >= 0 && off < CW_BAND && g_cw_strans[0][off])
				continue;                 /* per-set magenta key */
			map_px(g_cwf_px, g_cwf_pitch, g_cwf_sw, g_cwf_sh,
			       (short)(ox + x), (short)(oy + y), v);
		}
	}
}

/* load_cw_full — load all 48 pieces of wall-set (`file`,`set`) into the
 * resident store (bodies + dims + signed bearings). Cached on (file,set);
 * call load_color_wallset(set) first so the palette band is at clut 32. */
static int load_cw_full(short file, short set)
{
	short refnum = 0, i;
	long count, base, sub;
	short key = (short)((file << 8) | (set & 0xff));

	if (g_cwf_n > 0 && g_cwf_grp == key)
		return 1;                                 /* already loaded */
	g_cwf_n = 0;
	if (FSOpen((ConstStr255Param)g_cw_files[file & 1], 0, &refnum) != noErr)
		return 0;
	count = (long)sizeof g_cwf_buf;
	(void)FSRead(refnum, &count, g_cwf_buf);
	(void)FSClose(refnum);
	base = (long)(uintptr_t)g_cwf_buf;
	if (l37aa(base, 0) == 0)
		return 0;
	sub = l37aa(base, set);
	if (sub == 0)
		return 0;
	for (i = 0; i < CW_NPIECE; i++) {
		unsigned char metric[8];
		long b = l2856(sub, i, metric);
		if (b == 0) { g_cwf_body[i] = NULL; g_cwf_h[i] = g_cwf_w[i] = 0; continue; }
		g_cwf_body[i] = (const unsigned char *)(uintptr_t)b;
		g_cwf_h[i]  = metric[1];
		g_cwf_w[i]  = (short)(8 * metric[6]);
		g_cwf_xb[i] = (short)((metric[4] << 8) | metric[5]);   /* signed */
		g_cwf_yb[i] = (short)((metric[2] << 8) | metric[3]);
	}
	g_cwf_n = CW_NPIECE;
	g_cwf_grp = key;
	return 1;
}

/* jt200_layer — draw one wall-tile layer for jt200. Group 2 is the
 * DUNGCOM wall set (handle JT[1004] = g_a5_-4582), drawn 1:1 via the
 * faithful l309c_tile path. The other groups (0/1/3/4) blit through
 * JT[114] (CODE 6 + 0x3804) with per-group handles from the table
 * g_a5_-27894[group*4]; those tile libraries aren't loaded yet, so
 * that arm is a documented TODO. */
static void jt200_layer(unsigned char *page, short top, short left,
                        short group, short idx)
{
	if (g_cwf_px != NULL) {           /* faithful colour path: blit piece idx */
		cw_blit_piece(top, left, idx);
		return;
	}
	if (group == 2) {
		l309c_tile(page, top, left, jt1004_handle(), idx);
	}
	/* else: jt114(top, left, idx, 0, g_a5_-27894[group*4]) — needs the
	 * per-group wall-set tile-lib handle table populated. TODO. */
}

/* jt200 (JT[200], CODE 7 + 0x59d4) — draw one dungeon wall slot.
 *
 * Faithful lift. A wall code carries the wall-set group folded in:
 * peel off fives to recover (group, position-in-group). The wall set
 * must be enabled in the design state (a per-group flag byte at
 * design_state[group + 4]). Then 1 or 2 pre-rendered tile layers are
 * drawn (a near face always; a far face first when position > 1),
 * the tile index for each computed from (code, sub). `left` (the
 * horizontal anchor) steps per non-edge slot — by 16 in the deep
 * (jt1200()==3, VIDEL-doubled) display mode, else by 4.
 *
 * Coords: top = Y, left = X, matching l309c_tile / L2d4e. Threads an
 * explicit page (the port's drawing surface). */
static void jt200(unsigned char *page, short top, short left,
                  short code, short sub) __attribute__((unused));
static void jt200(unsigned char *page, short top, short left,
                  short code, short sub)
{
	const unsigned char *ds = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short group = 0;

	/* recover (group, position) from the folded wall code. */
	while ((code & 0xff) > 5) {
		code -= 5;
		group++;
	}
	if (ds == NULL || ds[group + 4] == 0)
		return;                          /* wall set disabled */

	sub++;
	if ((sub & 0xff) == 10) {
		sub = 0;
		if ((code & 0xff) > 2)
			code = 1;
	}
	if ((sub & 0xff) < 8) {                  /* step the horizontal anchor */
		if (jt1200() == 3) {
			if (left < 8000)
				left = (short)(left + 16);
		} else {
			left = (short)(left + 4);
		}
	}

	code--;
	if ((code & 0xff) <= 1) {
		sub = (short)((code & 0xff) * 10 + sub + 1);
	} else {
		jt200_layer(page, top, left, group, (short)(sub + 1)); /* far face */
		sub = (short)((code & 0xff) * 9 + sub + 2);
	}
	jt200_layer(page, top, left, group, sub);                      /* near face */
}

/* l5e52 (CODE 7 + 0x5e52) — read a cell edge's MOVEMENT-TYPE code (the
 * LOW nibble), clamping the cell to the map rather than failing: the
 * frustum walker (JT[199]) uses it to test whether a view ray can see
 * past a cell (open) or is blocked. Coords off the map wrap to the
 * opposite edge (row > H-1 -> 0, row < 0 -> H-1; same for col vs W).
 * Same map layout as jt210 (column-major, stride = height, map at
 * design_state + 290) but the low nibble and a clamp instead of a
 * bounds-fail; edge index = (dir & 6) >> 1. */
static short l5e52(short row, short col, short dir) __attribute__((unused));
static short l5e52(short row, short col, short dir)
{
	const unsigned char *ds = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	const unsigned char *cell;
	short edge;

	if (ds == NULL)
		return 0;
	if (row > (short)ds[3] - 1)
		row = 0;
	else if (row < 0)
		row = (short)(ds[3] - 1);
	if (col > (short)ds[2] - 1)
		col = 0;
	else if (col < 0)
		col = (short)(ds[2] - 1);
	edge = (short)((dir & 6) >> 1);
	cell = ds + 290 + ((long)col * ds[3] + row) * 6 + edge;
	return (short)(*cell & 15);              /* movement-type low nibble */
}

/* l5b42 (CODE 7 + 0x5b42) — place and draw one wall slot for the
 * frustum walker: offset the party's screen anchor (y, x) by the
 * slot's per-depth screen deltas (ydelta, xdelta, signed bytes from
 * the slot-layout globals), apply the deep-mode (jt1200()==3) 8000-
 * anchor remap, then hand off to jt200 with the wall code + sub/layer.
 * (The Mac writes to the engine page descriptor; we thread the page.) */
/* COORD-PIPELINE FINDINGS (2026-05-31, faithful-path investigation):
 * the runtime layout globals dumped from the loaded design are
 *   -12206..-12198 = {40,100,175,250,325}   -12216..-12208 = {175,550,875,1200,1600}
 *   -12222 = 516   -12220 = 18   -12202 = 175   (-12238/-12236 etc = 260/15)
 * l5b42 reads ydelta/xdelta as the LOW BYTE (asm: moveb fp@(13)/fp@(15) +
 * extw + lslw#2 — byte-truncation is FAITHFUL, verified). So the side-wall
 * x-delta g_a5_-12202 = 175 -> signed byte -81; x = 8016 + (-81<<2) = 7692;
 * deep remap ((7692-8012)<<2)+8 = -1272 -> off-screen. -12202 is READ-ONLY
 * across all 23 CODE segments (only jt199 reads it; nothing writes it), so
 * 175 is its permanent DATA value. Front-wall globals (low byte 4) land
 * on-screen but not centred. CONCLUSION: the 8000-anchor / deep-mode
 * transform model needs re-derivation before the faithful slot coords are
 * usable; the layout values themselves are a structured perspective table.
 * (render_3d_color currently hand-places the colour pieces instead.)
 *
 * ENTRY COORDS VERIFIED (CODE 22 jt312 @ 0x24c4..0x24e6): jt199 is called
 * with (Y=8012, X=8016, row=g_a5_-12288, col=g_a5_-12287,
 * facing=g_a5_-12286&7) — EXACTLY as lifted, so the anchor is not the bug.
 * NEXT THREAD: jt312 sets the view up entirely in 8000-anchored space
 * (clip JT[1173](8007,8000,8067,8160); bg JT[1001](16,8000,1,9)) and the
 * screen map is jt1135's (v-8000)*scale. But l5b42 uses a DIFFERENT map,
 * ((v-8012)<<2)+8, gated on jt1200()==3. Two different transforms for one
 * space -> re-derive which is correct (trace JT[1173]/the GrafPort) and
 * whether l5b42's deep branch should fire at all.
 *
 * TRANSFORM TRACED (CODE 4): JT[1173]'s clip transform L77fe is byte-for-
 * byte jt1135: out = (v>6000) ? (v-8000)*scale : v, scale=(g_a5_-2347==0)
 * ?3:2. So the canonical 8000-space->screen map is (v-8000)*3 (deep); the
 * clip rect lands x:[21,201] y:[0,480]. l5b42's ((v-8012)<<2)+8 is NOT
 * that map -> inconsistent. AND the layout table (-12240..-12196) is pure
 * static DATA: NOTHING writes it (jt954/JT[953] is party MOVEMENT, not a
 * view-init; full-segment grep finds no store/lea into -12240..-12202).
 * LIKELY LIFT BUG: the two near-face l5b42 calls are identical and add the
 * per-side stepping `soff` to the value added to fp@(8); since soff is
 * what separates the left/right walls, fp@(8) must be the HORIZONTAL (X)
 * coord -> the jt199/l5b42 lift has X and Y SWAPPED (param1=8012 is X, not
 * Y), which transposes jt200's top/left. Even so the magnitudes overshoot
 * the clip ~1.5-2x, so a scale/anchor factor is still wrong. RESOLUTION
 * needs runtime instrumentation: log l5b42's actual output coords for a
 * known frame and fit them to the clip viewport, with X/Y unswapped. */
static void l5b42(unsigned char *page, short y, short x, short ydelta,
                  short xdelta, short code, short sub) __attribute__((unused));
static void l5b42(unsigned char *page, short y, short x, short ydelta,
                  short xdelta, short code, short sub)
{
	y = (short)(y + ((short)(signed char)ydelta << 2));
	x = (short)(x + ((short)(signed char)xdelta << 2));
	if (jt1200() == 3) {
		y = (short)(((short)(y - 8012) << 2) + 8);
		x = (short)(((short)(x - 8012) << 2) + 8);
	}
#ifdef FRUA_COORD_TRACE
	/* Runtime coord trace: raw deltas (low byte) and the final screen
	 * (y,x). Fit these against the clip viewport x:[21,201] to re-derive
	 * the transform. Canonical map is (v-8000)*3; clip from L77fe. */
	dbg_log_num("l5b42 yd=", (long)(signed char)ydelta);
	dbg_log_num("      xd=", (long)(signed char)xdelta);
	dbg_log_num("   scr y=", (long)y);
	dbg_log_num("   scr x=", (long)x);
	dbg_log_num("  cd/sub=", (long)code * 100 + sub);
#endif
	jt200(page, y, x, code, sub);
}

/* Direction-step deltas: signed-byte drow/dcol tables indexed by dir. */
#define JT199_DROW(dir) ((short)(signed char)g_a5_byte(-27862 + (dir)))
#define JT199_DCOL(dir) ((short)(signed char)g_a5_byte(-27853 + (dir)))

/* jt199_side — one SIDE-wall pass of the frustum walker (JT[199]'s
 * pass 1 / pass 2). Walk the ray `dir` for depths 0..3 from (r, c):
 * read the cell's front face (looking `facing`); a wall there draws the
 * previous slot's wall as a receding side face (g_a5_-12222 + soff +
 * yadj / g_a5_-12202) and, for depths < 3, the current wall as a front
 * face (g_a5_-12240 + soff / g_a5_-12220); an open cell instead probes
 * the side neighbour and, if walled, draws the carried-over side face.
 * soff steps by `soffstep` per depth (the receding screen offset). */
static void jt199_side(unsigned char *page, short Y, short X, short r,
                       short c, short dir, short facing, short soffstep,
                       short yadj)
{
	short depth, soff = 0, prev = 0, w;

	for (depth = 0; depth < 4; depth++) {
		w = l5e52(r, c, facing);
		if (w != 0) {
			if (prev > 0)
				l5b42(page, Y, X,
				      (short)(g_a5_word(-12222) + soff + yadj),
				      g_a5_word(-12202), prev, 9);
			prev = w;
			if (depth < 3)
				l5b42(page, Y, X,
				      (short)(g_a5_word(-12240) + soff),
				      g_a5_word(-12220), w, 0);
		} else {
			if (prev > 0) {
				short sr = (short)(r - JT199_DROW(dir));
				short sc = (short)(c - JT199_DCOL(dir));
				if (l5e52(sr, sc, dir) & 0xff)
					l5b42(page, Y, X,
					      (short)(g_a5_word(-12222) + soff + yadj),
					      g_a5_word(-12202), prev, 9);
			}
			prev = 0;
		}
		soff = (short)(soff + soffstep);
		r = (short)(r + JT199_DROW(dir));
		c = (short)(c + JT199_DCOL(dir));
	}
}

/* jt199_front — one FRONT-wall pass (JT[199]'s pass 3 / pass 4). Walk
 * `dir` for depths 0..2: a walled cell (read along `dir`) draws a front
 * face at (gy + soff [+ yadj for depth>0] / gx) with layer `sub`. */
static void jt199_front(unsigned char *page, short Y, short X, short r,
                        short c, short dir, short soffstep, short gy,
                        short gx, short yadj, short sub)
{
	short depth, soff = 0, w;

	for (depth = 0; depth < 3; depth++) {
		w = l5e52(r, c, dir);
		if (w != 0)
			l5b42(page, Y, X,
			      (short)(g_a5_word(gy) + soff + (depth ? yadj : 0)),
			      g_a5_word(gx), w, sub);
		soff = (short)(soff + soffstep);
		r = (short)(r + JT199_DROW(dir));
		c = (short)(c + JT199_DCOL(dir));
	}
}

/* jt199_band — a mid/far-band scan (JT[3] cases 1 and 0). Walk `advdir`
 * for `ndepth` depths from (r, c): each step draws a FACING-face tile
 * (gyA + soff / gxA, layer subA) when the facing wall is set and
 * depth < aMaxDepth, and a SIDE-face tile (gyB + soff / gxB, layer subB)
 * read along `bdir` when depth > 0 and that wall is set. soff steps by
 * `soffstep` per depth. (Cases 1/0 of jt199's selector loop; the near
 * band — case 2 — uses jt199_side/jt199_front instead.) */
static void jt199_band(unsigned char *page, short Y, short X, short r,
                       short c, short facing, short bdir, short advdir,
                       short soff0, short soffstep, short ndepth,
                       short gyA, short gxA, short subA, short aMaxDepth,
                       short gyB, short gxB, short subB)
{
	short depth, soff = soff0, w, w2;

	for (depth = 0; depth < ndepth; depth++) {
		w = l5e52(r, c, facing);
		if (w != 0 && depth < aMaxDepth)
			l5b42(page, Y, X, (short)(g_a5_word(gyA) + soff),
			      g_a5_word(gxA), w, subA);
		w2 = l5e52(r, c, bdir);
		if (depth > 0 && w2 != 0)
			l5b42(page, Y, X, (short)(g_a5_word(gyB) + soff),
			      g_a5_word(gxB), w2, subB);
		soff = (short)(soff + soffstep);
		r = (short)(r + JT199_DROW(advdir));
		c = (short)(c + JT199_DCOL(advdir));
	}
}

/* jt199 (JT[199], CODE 7 + 0x6234) — the dungeon first-person frustum
 * walker. Its JT[3] view-layout selector is NOT constant: a `moveq #2`
 * seeds it, then an outer loop (L6e4a) iterates it 2 -> 1 -> 0, running
 * three depth BANDS as the view origin recedes one cell per pass (start
 * 2 cells forward, then 1, then the party cell). Each band reads walls
 * (l5e52) and draws their pre-rendered slot tiles (l5b42 -> jt200) with a
 * band-specific `sub` (depth) layer: case 2 = sub 0/9 side + 1/2 front;
 * case 1 = sub 3/4/5; case 0 = sub 6/7/8. Verified against a live capture
 * (docs/TODO.md): jt200's idx math + this sub ramp reproduce the real
 * (code,sub)->idx slots. The screen positions come from the runtime
 * layout globals g_a5_-12204..-12240.
 *
 * The cosmetic setup the Mac does first — L6148 (per-frame wall-handle
 * cache), JT[124] (dispose), JT[993] (view-background fill), JT[1173]
 * (clip rect) — is omitted here; this lift is the geometry + tile
 * selection that turns a loaded map into a first-person view. Threads
 * an explicit page (the port's drawing surface). */
static void jt199(unsigned char *page, short Y, short X, short row,
                  short col, short facing) __attribute__((unused));
static void jt199(unsigned char *page, short Y, short X, short row,
                  short col, short facing)
{
	short left  = (short)((facing + 6) & 7);
	short right = (short)((facing + 2) & 7);
	short back  = (short)((facing + 4) & 7);
	short Lr = JT199_DROW(left),  Lc = JT199_DCOL(left);
	short Rr = JT199_DROW(right), Rc = JT199_DCOL(right);
	short orow = (short)(row + 2 * JT199_DROW(facing));  /* origin: 2 fwd */
	short ocol = (short)(col + 2 * JT199_DCOL(facing));
	short sel;

	for (sel = 2; sel >= 0; sel--) {
		if (sel == 2) {                                  /* near band */
			jt199_side(page, Y, X, orow, ocol, left,  facing, -2, +1);
			jt199_side(page, Y, X, orow, ocol, right, facing, +2, -1);
			jt199_front(page, Y, X, orow, ocol, left,  -2, -12238, -12218, -1, 1);
			jt199_front(page, Y, X, orow, ocol, right, +2, -12236, -12216, +1, 2);
		} else if (sel == 1) {                           /* mid band */
			jt199_band(page, Y, X, (short)(orow + 2 * Lr), (short)(ocol + 2 * Lc),
			           facing, left,  right, -6, +3, 3,
			           -12234, -12214, 3, 99, -12232, -12212, 4);
			jt199_band(page, Y, X, (short)(orow + 2 * Rr), (short)(ocol + 2 * Rc),
			           facing, right, left,   6, -3, 3,
			           -12234, -12214, 3,  2, -12230, -12210, 5);
		} else {                                         /* far band */
			jt199_band(page, Y, X, (short)(orow + Lr), (short)(ocol + Lc),
			           facing, left,  right, -7, +7, 2,
			           -12228, -12208, 6, 99, -12226, -12206, 7);
			jt199_band(page, Y, X, (short)(orow + Rr), (short)(ocol + Rc),
			           facing, right, left,   7, -7, 2,
			           -12228, -12208, 6,  1, -12224, -12204, 8);
		}
		orow = (short)(orow + JT199_DROW(back));         /* recede one cell */
		ocol = (short)(ocol + JT199_DCOL(back));
	}
}

/* render_3d_faithful — the 1:1 Mac slot-assembly view: jt199 walks the
 * frustum and l5b42 places each visible wall slot at the real screen coords
 * (from the captured layout globals); jt200_layer blits the pre-sized
 * colour piece there via cw_blit_piece. Single wall set for now (the
 * level's Wall1); per-group Wall1-3 piece stores are a follow-up. */
static void render_3d_faithful(unsigned char *px, short pitch, short sw, short sh)
	__attribute__((unused));
static void render_3d_faithful(unsigned char *px, short pitch, short sw, short sh)
{
	static unsigned char page[BP_STRIDE * BP_ROWS];   /* unused in colour mode */
	static const short hw0 = 110, hh0 = 74;
	const short vcx = 118, vcy = 83;
	const unsigned char *ds = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short f = (short)(g_a5_12286 & 7);
	short file, set, x, y;

	if (ds == NULL)
		return;

	/* active wall set = the level's Wall1; (re)load its clut-32 palette
	 * band + the full 48-piece store when it changes. */
	if (wallset_for_id((short)(unsigned char)ds[4], &file, &set)) {
		short key = (short)((file << 8) | (set & 0xff));
		if (g_cwf_grp != key) {
			g_cw_file = file;
			g_cw_set  = set;
			load_color_wallset(set);          /* palette band @ clut 32 + strans */
			load_cw_full(file, set);          /* 48 pieces resident */
			load_backdrop(g_back_set);        /* re-lay backdrop band (clut 145) */
		}
	}

	/* backdrop (floor/ceiling/sky) under the walls */
	{
		short y0 = (short)(vcy - hh0), y1 = (short)(vcy + hh0);
		short x0 = (short)(vcx - hw0), x1 = (short)(vcx + hw0);
		short vh = (short)(y1 - y0 + 1), vw = (short)(x1 - x0 + 1);
		for (y = y0; y <= y1; y++) {
			short by = g_back_h ? (short)(((long)(y - y0) * g_back_h) / vh) : 0;
			for (x = x0; x <= x1; x++) {
				short c;
				if (g_back_w) {
					short bx = (short)(((long)(x - x0) * g_back_w) / vw);
					unsigned char v = g_back_img[(long)by * g_back_w + bx];
					c = v ? (short)v : 4;
				} else {
					c = (y < vcy) ? 4 : 5;
				}
				map_px(px, pitch, sw, sh, x, y, (unsigned char)c);
			}
		}
	}

	/* run the faithful walk, drawing colour pieces to px */
	g_cwf_px = px; g_cwf_pitch = pitch; g_cwf_sw = sw; g_cwf_sh = sh;
#ifdef FRUA_ENGINE_PROBE
	g_cwf_blits = 0;
	dbg_log_num("faithful: cwf_n=", (long)g_cwf_n);
	dbg_log_num("  ds[4..6]=", (long)ds[4] * 10000 + ds[5] * 100 + ds[6]);
#endif
	/* l5e52 indexes cell = col*h + row, but the map (cell_edge) is x*h+y,
	 * so pass row=partyY, col=partyX (swapped) to read the right cells. */
	jt199(page, (short)8012, (short)8016,
	      (short)g_a5_12287, (short)g_a5_12288, f);
	g_cwf_px = NULL;
#ifdef FRUA_ENGINE_PROBE
	dbg_log_num("faithful: blits=", (long)g_cwf_blits);
#endif
}

/* l04d6 (CODE 22 + 0x04d6) — return a map cell's floor/ceiling
 * decoration byte (cell byte 4, at design_state + cell*6 + 294). */
static short l04d6(short cell) __attribute__((unused));
static short l04d6(short cell)
{
	const unsigned char *ds = (const unsigned char *)(uintptr_t)g_a5_long(-12300);

	if (ds == NULL)
		return 0;
	return (short)ds[(long)cell * 6 + 294];
}

/* dungeon_view_setup — one-time bring-up of what the dungeon view needs:
 * load DUNGCOM.TLB's stone set into the wall-tile table and program the
 * view CLUT (automap 1..3, ceiling 4 / floor 5, and the 8-step brown
 * stone depth ramp at 8..15). Idempotent; returns non-zero once the
 * wall set is loaded. Called lazily by jt312 so the engine's dungeon
 * render entry is self-sufficient (the Mac build's CODE 21 view-init
 * loads these; we stand in for it here until that path is lifted). */
static int g_dungeon_view_ready = 0;
static int dungeon_view_setup(void)
{
	static unsigned char dc[20480];
	short refnum = 0, i;
	long count;
	RGBColor c4[16];

	if (g_dungeon_view_ready)
		return g_wall_n > 0;
	g_dungeon_view_ready = 1;

	g_wall_n = 0;
	if (FSOpen((ConstStr255Param)"\013DUNGCOM.TLB", 0, &refnum) == noErr) {
		long dcbase, nested;
		count = (long)sizeof dc;
		(void)FSRead(refnum, &count, dc);
		(void)FSClose(refnum);
		dcbase = (long)(uintptr_t)dc;
		if (l37aa(dcbase, 0) != 0 && (nested = l37aa(dcbase, 1)) != 0) {
			for (i = 0; i < WALL_NTILES; i++) {
				long lb = l2856(nested, (short)(i + 1), g_wall_metric[i]);
				g_wall_bmp[i] = (lb != 0)
				              ? (const unsigned char *)(uintptr_t)lb : NULL;
			}
			g_wall_n = WALL_NTILES;
		}
	}

	for (i = 0; i < 16; i++)
		c4[i].red = c4[i].green = c4[i].blue = 0;
	c4[1].red  = c4[1].green = c4[1].blue = 0xffff;            /* automap walls */
	c4[2].red  = 0xffff; c4[2].green = 0xd000; c4[2].blue = 0; /* automap door  */
	c4[3].red  = 0xffff; c4[3].green = 0x2000; c4[3].blue = 0xffff; /* party    */
	c4[4].red  = 0x0c00; c4[4].green = 0x0900; c4[4].blue = 0x0600; /* ceiling  */
	c4[5].red  = 0x7000; c4[5].green = 0x5400; c4[5].blue = 0x3200; /* floor    */
	{
		static const unsigned short ramp[8] = {
			0xe800, 0xc200, 0x9e00, 0x7c00, 0x5c00, 0x4000, 0x2800, 0x1400
		};
		for (i = 0; i < 8; i++) {
			unsigned short b = ramp[i];
			c4[8 + i].red   = b;
			c4[8 + i].green = (unsigned short)(((long)b * 184) >> 8);
			c4[8 + i].blue  = (unsigned short)(((long)b * 108) >> 8);
		}
	}
	qd_set_palette(c4, 0, 16);
	/* Initial wall set into slot 0 (cw_finalize also lays the backdrop
	 * band); jt312 then auto-loads the level's Wall1-3 into the slots. */
	load_color_wallset(g_cw_set);
	return (g_wall_n > 0) || (g_cw_sid[0] != 0);
}

/* jt312 (JT[312], CODE 22 + 0x23ee) — the dungeon-view render, the
 * play-loop site that draws the first-person view. In the Mac build
 * this runs the page/palette setup, the view clip + background fill, a
 * backdrop sprite, then jt199 (the frustum walker) and a present.
 *
 * Here jt312 is the engine's live dungeon render entry: in the deep
 * display mode (jt1200()==3) it ensures the colour wall set + clut 129
 * are up (dungeon_view_setup), stamps the cell's floor/ceiling decoration
 * byte, then draws render_3d_view — the perspective-correct trapezoid
 * corridor sampling the 8bpp 8X8DC.CTL wall texture, reading the live
 * party globals g_a5_-12288/-12287/-12286 — straight to the shim screen
 * and presents. (The faithful jt199 -> l5b42 -> jt200 slot path is parked:
 * the Mac coordinate pipeline needs runtime view state we don't
 * reconstruct — see the dungeon-render-architecture note.) The `page`
 * arg (the Mac engine's bit-packed surface) is unused now.
 *
 * Deferred (the Mac does these around the walls): JT[1001] background
 * fill, JT[118] backdrop, the non-deep JT[221] view + wilderness
 * branch, and the L2806/L265e position + compass overlays. */
static void jt312(unsigned char *page)
{
	const unsigned char *ds = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	static int s_view_first = 1;
	unsigned char *px;
	short pitch, sw, sh, y, cell;

	(void)page;
	if (ds == NULL || jt1200() != 3)        /* deep dungeon view only */
		return;
	if (!dungeon_view_setup())
		return;
	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == 0)
		return;

	cell = (short)((long)(short)g_a5_12288 * ds[3] + (short)g_a5_12287);
	g_a5_byte(-12284) = (unsigned char)(l04d6(cell) & 127);

	/* Load the level's three wall groups (Wall1-3 = ds[4..6]) into the
	 * three slots so each map face draws with its own set (unless pinned
	 * with 't'/'y'); reload only when the group ids change. */
	if (g_cw_auto
	 && (ds[4] != g_cw_grp[0] || ds[5] != g_cw_grp[1] || ds[6] != g_cw_grp[2])) {
		load_wall_groups(ds);
		g_view_force_full = 1;
#ifdef FRUA_ENGINE_PROBE
		dbg_log_num("auto Wall1 ", g_cw_grp[0]);
		dbg_log_num("auto Wall2 ", g_cw_grp[1]);
		dbg_log_num("auto Wall3 ", g_cw_grp[2]);
#endif
	}

	/* Pick the floor/ceiling backdrop for the current cell's zone (unless
	 * the demo pinned one with 'b'); reload only when it changes. */
	if (g_back_auto) {
		short id = cell_backdrop_id(ds);
		if (id != g_back_set) {
			g_back_set = id;
			load_backdrop(id);
			g_view_force_full = 1;
#ifdef FRUA_ENGINE_PROBE
			dbg_log_num("cell backdrop -> ", id);
#endif
		}
	}

	/* Clear the whole surface + full-present once, to flush the black
	 * surround; thereafter only the viewport changes, so we present just
	 * that rect (the c2p of the static 320x400 screen was the perf wall). */
	if (s_view_first || g_view_force_full) {
		for (y = 0; y < sh; y++)
			memset(px + (long)y * pitch, 0, (size_t)sw);
	}
	/* render_3d_raycast: jt199's wider 3-column frustum (shows side passages)
	 * over the colour wall/facet system — parity with the old single-corridor
	 * render_3d_view on straight corridors. FRUA_CORRIDOR selects the latter. */
#if defined(FRUA_FAITHFUL)
	render_3d_faithful(px, pitch, sw, sh);   /* 1:1 jt199 slot-assembly */
#elif defined(FRUA_CORRIDOR)
	render_3d_view(px, pitch, sw, sh);
#else
	render_3d_raycast(px, pitch, sw, sh);
#endif
	if (s_view_first || g_view_force_full) {
		qd_present();           /* flush whole screen under the new palette */
		s_view_first = 0;
		g_view_force_full = 0;
	} else {
		/* present just the render_3d_view viewport (x 8..228, y 9..157). */
		qd_present_rect((short)8, (short)9, (short)221, (short)149);
	}
}

/* port_view_demo — drive jt199, the first-person frustum walker, over
 * the real loaded design's GEO map. Seeds the DUNGCOM wall-set handle,
 * logs the runtime view state (the slot-layout DATA globals, the map
 * dims, the display mode) so the geometry can be confirmed, then renders
 * the view from the map centre. */
void port_view_demo(void)
{
	static unsigned char dc[20480];
	static unsigned char page[BP_STRIDE * BP_ROWS];
	const unsigned char *ds;
	unsigned char *px;
	short pitch, sw, sh, refnum = 0, i;
	long count, dcbase, nested;
	RGBColor c2[2];

	if (FSOpen((ConstStr255Param)"\013DUNGCOM.TLB", 0, &refnum) != noErr)
		return;
	count = (long)sizeof dc;
	(void)FSRead(refnum, &count, dc);
	(void)FSClose(refnum);
	dcbase = (long)(uintptr_t)dc;
	if (l37aa(dcbase, 0) == 0 || (nested = l37aa(dcbase, 1)) == 0)
		return;
	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == 0)
		return;
	g_a5_long(-4582) = (long)(uintptr_t)nested;

	ds = (const unsigned char *)(uintptr_t)g_a5_long(-12300);

	/* Engage the deep display mode the dungeon view uses (g_a5_2347 = 0
	 * -> jt1200()==3) and place the party near the column-0 wall run
	 * (row 5, col 3, facing 6 = west). boot_a5_seed_defaults() seeds the
	 * direction-step tables, but the -27862 BSS region gets cleared by a
	 * later boot allocation, so re-seed them here right before the view
	 * render. The live slot-layout globals come from DATA. */
	g_a5_2347 = 0;
	{
		static const signed char drow[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
		static const signed char dcol[8] = {  0,  1, 1, 1, 0, -1, -1, -1 };
		short k;
		for (k = 0; k < 8; k++) {
			g_a5_byte(-27862 + k) = (unsigned char)drow[k];
			g_a5_byte(-27853 + k) = (unsigned char)dcol[k];
		}
	}
	g_a5_byte(-12288) = 5;          /* party row */
	g_a5_byte(-12287) = 3;          /* party col */
	g_a5_byte(-12286) = 6;          /* facing west */

	for (i = 0; i < (BP_STRIDE * BP_ROWS); i++)
		page[i] = 0;
	if (ds)
		jt312(page);

	c2[0].red = c2[0].green = c2[0].blue = 0;
	c2[1].red = c2[1].green = c2[1].blue = 0xffff;
	qd_set_palette(c2, 0, 2);
	bp_present(page, px, pitch, sw, sh, 1, 0);
	qd_present();
	for (;;)
		qd_present();
}

/* port_blit_demo — exercise the bit-packed blit foundation: load a real
 * 32x32 1bpp DUNGCOM tile, OR-blit it into the page at a run of sub-word
 * x offsets (shift 0..15) via bp_blit_or, then convert the page to the
 * 8-bit screen. Proves the shift-blit composites correctly at every
 * sub-pixel offset. */
void port_blit_demo(void)
{
	static unsigned char dc[20480];
	static unsigned char page[BP_STRIDE * BP_ROWS];
	unsigned char metric[8];
	const unsigned char *src;
	unsigned char *px;
	short pitch, sw, sh, refnum = 0, i;
	long count, dcbase, nested, lb;
	RGBColor c2[2];

	if (FSOpen((ConstStr255Param)"\013DUNGCOM.TLB", 0, &refnum) != noErr)
		return;
	count = (long)sizeof dc;
	(void)FSRead(refnum, &count, dc);
	(void)FSClose(refnum);
	dcbase = (long)(uintptr_t)dc;
	if (l37aa(dcbase, 0) == 0 || (nested = l37aa(dcbase, 1)) == 0)
		return;
	lb = l2856(nested, 1, metric);           /* leaf 1: 32x32 1bpp tile */
	if (lb == 0)
		return;
	src = (const unsigned char *)(uintptr_t)lb;

	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == 0)
		return;

	{
		short y;

		for (i = 0; i < (BP_STRIDE * BP_ROWS); i++)
			page[i] = 0;

		/* row of full tiles (mode 0 = OR) at sub-word x offsets. */
		for (i = 0; i < 8; i++)
			bp_blit(page, (short)(64 + i * 34), (short)40, metric, src, 0);

		/* edge clips: a tile clipped above the top (y = -12), one
		 * clipped past the left edge (x = -20, only its right 12 px
		 * show), and a corner-clipped tile (x = -16, y = -16). All
		 * must render only their visible part with no wrap / overflow. */
		bp_blit(page, (short)20,  (short)-12, metric, src, 0);
		bp_blit(page, (short)-20, (short)40,  metric, src, 0);
		bp_blit(page, (short)-16, (short)-16, metric, src, 0);

		/* bottom: a solid white band, then bp_blit mode 1 (AND-NOT)
		 * carves the tile pattern out of it — including one carve
		 * clipped past the left edge to exercise the mask path. */
		for (y = 130; y < 130 + 32; y++)
			for (i = 0; i < BP_STRIDE; i++)
				page[(long)y * BP_STRIDE + i] = 0xff;
		bp_blit(page, (short)-20, (short)130, metric, src, 1);
		for (i = 0; i < 8; i++)
			bp_blit(page, (short)(64 + i * 34), (short)130, metric, src, 1);

		/* l309c (JT[999]) end-to-end: fetch tile 1 from the nested
		 * handle and draw it via the faithful remap+bearing+leaf path
		 * (proves the dungeon blit entry, not just the raw leaf). */
		l309c_tile(page, (short)210, (short)140, nested, (short)1);
		l309c_tile(page, (short)210, (short)180, nested, (short)1);
	}

	c2[0].red = c2[0].green = c2[0].blue = 0;          /* bg black */
	c2[1].red = c2[1].green = c2[1].blue = 0xffff;     /* fg white */
	qd_set_palette(c2, 0, 2);
	bp_present(page, px, pitch, sw, sh, 1, 0);
	qd_present();
	for (;;)
		qd_present();
}

/* port_wall_demo — drive jt200 (JT[200]), the per-slot wall-tile
 * selector, against the real DUNGCOM wall set. Seeds the engine state
 * jt200 reads (the DUNGCOM handle g_a5_-4582, a design state with the
 * wall-set groups enabled, a non-deep display mode so coords stay
 * plain), then calls jt200 for rows of slots with different wall codes
 * — proving the faithful (code -> group/position -> tile-index -> 1:1
 * blit) selection draws real pre-rendered wall art. The L5bfa raycaster
 * that feeds jt200 real map cells is the next piece. */
void port_wall_demo(void)
{
	static unsigned char dc[20480];
	static unsigned char page[BP_STRIDE * BP_ROWS];
	static unsigned char ds[16];
	unsigned char *px;
	short pitch, sw, sh, refnum = 0, i;
	long count, dcbase, nested;
	RGBColor c2[2];

	if (FSOpen((ConstStr255Param)"\013DUNGCOM.TLB", 0, &refnum) != noErr)
		return;
	count = (long)sizeof dc;
	(void)FSRead(refnum, &count, dc);
	(void)FSClose(refnum);
	dcbase = (long)(uintptr_t)dc;
	if (l37aa(dcbase, 0) == 0 || (nested = l37aa(dcbase, 1)) == 0)
		return;
	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == 0)
		return;

	/* seed what jt200 reads: the DUNGCOM wall-set handle, a design
	 * state with every wall-set group enabled, and a non-deep display
	 * mode so coords stay plain (jt1200() -> 0/1, no 8000 anchor). */
	g_a5_long(-4582) = (long)(uintptr_t)nested;
	for (i = 0; i < (short)sizeof ds; i++)
		ds[i] = 1;
	g_a5_long(-12300) = (long)(uintptr_t)ds;
	g_a5_2347 = 1;

	for (i = 0; i < (BP_STRIDE * BP_ROWS); i++)
		page[i] = 0;

	/* two rows of single-layer group-2 slots; stepping `sub` walks
	 * consecutive pre-rendered tiles. code 11 -> group 2 position 1
	 * (tile range ~2..8), code 12 -> group 2 position 2 (~12..18). */
	for (i = 0; i < 7; i++)
		jt200(page, (short)40,  (short)(20 + i * 40), (short)11, (short)i);
	for (i = 0; i < 7; i++)
		jt200(page, (short)150, (short)(20 + i * 40), (short)12, (short)i);

	c2[0].red = c2[0].green = c2[0].blue = 0;
	c2[1].red = c2[1].green = c2[1].blue = 0xffff;
	qd_set_palette(c2, 0, 2);
	bp_present(page, px, pitch, sw, sh, 1, 0);
	qd_present();
	for (;;)
		qd_present();
}

/* port_play_demo — the play loop core as an interactive dungeon walk
 * on the automap. Loads the TOPVIEW tiles, enters level 1 (L0bbc
 * loads the map + places the party), then cycles: draw the map +
 * party marker, read a key, move. Keys: w forward / s back / a turn
 * left / d turn right / m toggle automap / t next wall set / y toggle
 * wall library (8X8DC<->8X8DB) / b browse backdrops / q quit. Normally
 * the wall set is auto-picked from the level's Wall1 and the floor/ceiling
 * backdrop per map cell (its zone selects one of the level's four); t/y
 * pin a manual wall set and b a manual backdrop, for browsing. They
 * live-swap the 3D art — a quick visual regression check over every
 * environment without a rebuild. This is the runtime's render-input-
 * move-render loop. */
void port_play_demo(void)
{
	static unsigned char tv[2048];
	static unsigned char dc[20480];
	unsigned char *px;
	short pitch, sw, sh, refnum = 0, i, show_map = 0;
	long tvbase, count;
	unsigned char *pl;
	RGBColor c4[16];

	if (FSOpen((ConstStr255Param)"\013TOPVIEW.TLB", 0, &refnum) != noErr)
		return;
	count = (long)sizeof tv;
	(void)FSRead(refnum, &count, tv);
	(void)FSClose(refnum);
	tvbase = (long)(uintptr_t)tv;
	if (l37aa(tvbase, 0) == 0)
		return;

	/* Load the dungeon wall set (DUNGCOM.TLB) into the wall-tile
	 * table: it is a GLIB-of-GLIBs, item 1 a nested 'TILE' library of
	 * 32x32 1bpp wall tiles. Grab the first WALL_NTILES so the view
	 * can pick a texture per wall edge. */
	g_wall_n = 0;
	refnum = 0;
	if (FSOpen((ConstStr255Param)"\013DUNGCOM.TLB", 0, &refnum) == noErr) {
		long dcbase, nested;
		count = (long)sizeof dc;
		(void)FSRead(refnum, &count, dc);
		(void)FSClose(refnum);
		dcbase = (long)(uintptr_t)dc;
		if (l37aa(dcbase, 0) != 0 && (nested = l37aa(dcbase, 1)) != 0) {
			for (i = 0; i < WALL_NTILES; i++) {
				/* leaf 0 is the directory; tiles start at leaf 1 */
				long lb = l2856(nested, (short)(i + 1), g_wall_metric[i]);
				g_wall_bmp[i] = (lb != 0)
				              ? (const unsigned char *)(uintptr_t)lb : NULL;
#ifdef FRUA_ENGINE_PROBE
				/* Bit-depth check (per the bytes-read-vs-expected test):
				 * the item body the loader gets vs the 1bpp/8bpp sizes
				 * implied by the metric (h x bpp_w). Body == 1bpp size
				 * means the file stores 1bpp; it is not a truncated 8bpp
				 * texture. */
				if (i < 4 && lb != 0) {
					long  a1 = l37aa(nested, (short)(i + 1));
					long  a2 = l37aa(nested, (short)(i + 2));
					short hh = (short)(((unsigned short)g_wall_metric[i][0] << 8)
					                 | g_wall_metric[i][1]);
					short bw = (short)g_wall_metric[i][6];
					dbg_log_num("wall tile h = ", (long)hh);
					dbg_log_num("  bpp_w     = ", (long)bw);
					dbg_log_num("  body read = ", (a2 > a1) ? (a2 - a1 - 8) : -1);
					dbg_log_num("  1bpp want = ", (long)hh * bw);
					dbg_log_num("  8bpp want = ", (long)hh * bw * 8);
				}
#endif
			}
			g_wall_n = WALL_NTILES;
		}
	}
	/* NOTE: do NOT pre-mark g_dungeon_view_ready here — jt312's
	 * dungeon_view_setup must run on first call so it also loads the
	 * colour wall set (8X8DC) into the piece store + clut 16..19. */

	pl = (unsigned char *)g_a5_28006;
	if (pl != NULL)
		pl[134] = 0;
	g_a5_18485 = 0;
#ifndef DEMO_LEVEL
#define DEMO_LEVEL 1
#endif
	g_a5_18878 = DEMO_LEVEL;          /* GEOnnn to load (1=overland start) */
	g_a5_18488 = 0;
	l0bbc();                          /* load the level + place the party */
	/* the level's HDR start (g_a5_-18488 unknown) lands in open space;
	 * for the demo drop the party into a known E-W corridor facing E
	 * so the 3D view shows side walls receding in perspective. */
	g_a5_12288 = 2;                   /* x */
	g_a5_12287 = 13;                  /* y */
	g_a5_12286 = 2;                   /* facing E */
	/* Auto-pick the best vantage: the (cell, facing) whose view ray
	 * stays open ahead the longest while flanked by side walls — i.e.
	 * looking down a corridor, so render_3d_view shows the side-wall
	 * trapezoids receding in perspective rather than a wall in the face. */
	{
		const unsigned char *ds = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
		if (ds != NULL) {
			short mw = ds[2], mh = ds[3];
			short sx, sy, sf, bx = 2, by = 13, bf = 2, bscore = -1;
			for (sf = 0; sf < 8; sf += 2) {
				short lf = (short)((sf + 6) & 7), rf = (short)((sf + 2) & 7);
				for (sy = 0; sy < mh; sy++)
					for (sx = 0; sx < mw; sx++) {
						short cx = sx, cy = sy, score = 0, d;
						/* count consecutive depths that are a true
						 * corridor: walls on BOTH sides AND open ahead
						 * (so it recedes). A straight tunnel like the
						 * Mac reference scores high; a single wall in
						 * the face or an open plaza scores 0. Search as
						 * deep as the view draws so the deepest run wins. */
						for (d = 0; d < 8; d++) {
							if (!cell_edge(cx, cy, lf) || !cell_edge(cx, cy, rf))
								break;        /* not flanked -> not a corridor */
							if (cell_edge(cx, cy, sf))
								break;        /* blocked ahead -> stop */
							score++;
							cx = (short)(cx + dir_dx[sf]);
							cy = (short)(cy + dir_dy[sf]);
						}
						if (score > bscore) {
							bscore = score; bx = sx; by = sy; bf = sf;
						}
					}
			}
			g_a5_12288 = bx;
			g_a5_12287 = by;
			g_a5_12286 = bf;
#if defined(DEMO_X) && defined(DEMO_Y) && defined(DEMO_F)
			g_a5_12288 = DEMO_X;   /* fixed start, to face a known cell */
			g_a5_12287 = DEMO_Y;
			g_a5_12286 = DEMO_F;
#endif
#ifdef FRUA_ENGINE_PROBE
			dbg_log_num("vantage x=", (long)bx);
			dbg_log_num("vantage y=", (long)by);
			dbg_log_num("vantage f=", (long)bf);
			dbg_log_num("vantage score=", (long)bscore);
#endif
		}
	}

	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == 0)
		return;
	/* shared CLUT: 0..3 automap, 4 ceiling, 5 floor, 8..11 side-wall
	 * shades (near->far), 12..15 front-wall shades. */
	for (i = 0; i < 16; i++)
		c4[i].red = c4[i].green = c4[i].blue = 0;
	c4[1].red  = c4[1].green = c4[1].blue = 0xffff;            /* automap walls */
	c4[2].red  = 0xffff; c4[2].green = 0xd000; c4[2].blue = 0; /* automap door  */
	c4[3].red  = 0xffff; c4[3].green = 0x2000; c4[3].blue = 0xffff; /* party    */
	c4[4].red  = 0x0c00; c4[4].green = 0x0900; c4[4].blue = 0x0600; /* ceiling (dark warm) */
	c4[5].red  = 0x7000; c4[5].green = 0x5400; c4[5].blue = 0x3200; /* floor (cobble brown) */
	/* Brown stone depth ramp at clut 8..15: eight warm-stone shades from
	 * bright near (8) falling geometrically to near-black far (15), so
	 * the corridor recedes into darkness at the vanishing point. The
	 * slot compositor indexes this by depth (stone face 8+d, mortar two
	 * steps darker). RGB ~ (1.0, 0.72, 0.42) of brightness for warm tone. */
	{
		static const unsigned short ramp[8] = {
			0xe800, 0xc200, 0x9e00, 0x7c00, 0x5c00, 0x4000, 0x2800, 0x1400
		};
		for (i = 0; i < 8; i++) {
			unsigned short b = ramp[i];
			c4[8 + i].red   = b;
			c4[8 + i].green = (unsigned short)(((long)b * 184) >> 8);
			c4[8 + i].blue  = (unsigned short)(((long)b * 108) >> 8);
		}
	}
	qd_set_palette(c4, 0, 16);
	/* Engage the deep dungeon-view display mode (jt1200() == 3) so
	 * jt312 renders the first-person view rather than early-returning. */
	g_a5_2347 = 0;

#if defined(FRUA_COORD_TRACE) || defined(FRUA_PERF_TEST)
	{
		extern long TickCount(void);
		static unsigned char jpage[BP_STRIDE * BP_ROWS];
		long t0, t1; short k;
		(void)jpage; (void)t0; (void)t1; (void)k;
		dungeon_view_setup();           /* load walls + clut before timing */
		dbg_log_num("cw slot0 id=", (long)g_cw_sid[0]);
#ifdef FRUA_COORD_TRACE
		/* Drive the faithful frustum walker once so l5b42 logs every
		 * slot's screen coord for this vantage. */
		dbg_log_num("=== jt199 coord trace (clip x[21,201]) facing=",
		            (long)(g_a5_12286 & 7));
		jt199(jpage, (short)8012, (short)8016, (short)g_a5_12288,
		      (short)g_a5_12287, (short)(g_a5_12286 & 7));
#endif
#ifdef FRUA_PERF_TEST
		/* Frame-time benchmark: how many 60Hz ticks for 60 render
		 * frames, render-only vs render+present. */
		t0 = TickCount();
		for (k = 0; k < 10; k++)
			render_3d_view(px, pitch, sw, sh);
		t1 = TickCount();
		dbg_log_num("PERF render-only x10 ticks=", t1 - t0);
		t0 = TickCount();
		for (k = 0; k < 10; k++)
			qd_present();
		t1 = TickCount();
		dbg_log_num("PERF present-FULL x10 ticks=", t1 - t0);
		t0 = TickCount();
		for (k = 0; k < 10; k++)
			qd_present_rect((short)8, (short)9, (short)221, (short)149);
		t1 = TickCount();
		dbg_log_num("PERF present-RECT x10 ticks=", t1 - t0);
#endif
	}
#endif

	for (;;) {
		unsigned char scan = 0, ascii = 0;
		short y;

		if (show_map) {
			for (y = 0; y < sh; y++)
				memset(px + (long)y * pitch, 0, (size_t)sw);
			draw_map_tiles(tvbase, px, pitch, sw, sh);
			draw_party(px, pitch, sw, sh, (short)g_a5_12288,
			           (short)g_a5_12287, (short)g_a5_12286);
			qd_present();
		} else {
			/* Render through the engine's real dungeon-view entry
			 * (jt312), not a demo-only call — jt312 reads the live
			 * party globals, draws the slot-assembly corridor, clears,
			 * and presents. This is the play-loop render path. */
			jt312((unsigned char *)0);
		}

		while (!plat_kb_poll(&scan, &ascii))
			;
		if (ascii == 'q' || ascii == 'Q' || ascii == 27)
			break;
		switch (ascii) {
		case 'a': case 'A': party_step(0); break;
		case 'd': case 'D': party_step(1); break;
		case 'w': case 'W': party_step(2); break;
		case 's': case 'S': party_step(3); break;
		case 'm': case 'M': show_map = (short)!show_map; break;
		case 't': case 'T':     /* browse: next set (pins auto off) */
			g_cw_auto = 0;
			g_cw_set = (short)(g_cw_set >= g_cw_setmax ? 1 : g_cw_set + 1);
			load_color_wallset(g_cw_set);   /* cw_finalize re-lays the backdrop */
			g_view_force_full = 1;
#ifdef FRUA_ENGINE_PROBE
			dbg_log_num("wall set -> ", g_cw_set);
#endif
			break;
		case 'y': case 'Y':     /* browse: toggle 8X8DC <-> 8X8DB (pins auto off) */
			g_cw_auto = 0;
			g_cw_file ^= 1;
			g_cw_set = 1;
			load_color_wallset(g_cw_set);
			g_view_force_full = 1;
#ifdef FRUA_ENGINE_PROBE
			dbg_log_num("wall file -> ", g_cw_file);
#endif
			break;
		case 'b': case 'B':     /* browse backdrops manually (pins auto off) */
			g_back_auto = 0;
			g_back_set = (short)(g_back_set >= g_back_max ? 1 : g_back_set + 1);
			load_backdrop(g_back_set);
			g_view_force_full = 1;
#ifdef FRUA_ENGINE_PROBE
			dbg_log_num("backdrop -> ", g_back_set);
#endif
			break;
		default: break;
		}
	}
}

/* port_render_geo_contact — load every GEOnnn (1..40) in turn and
 * draw each as a small thumbnail in a grid, to confirm the 24x24 /
 * 6-byte tile layout holds across a whole design. A map is "present"
 * if, after jt198, the design-state header word lands in 100..106
 * (jt198 leaves design state untouched on a missing file, so the
 * sentinel we write first stays). Each thumbnail paints the map's
 * ds[2]xds[3] cells at 2px each: white where the tile has any wall
 * edge, floor-flag shade otherwise. */
static short geo_hdr_word(const unsigned char *ds)
{
	return (short)(((unsigned short)ds[0] << 8) | ds[1]);
}

/* ctile_blit — decode and blit one of FRUA's COLOUR tiles (the mode-1
 * "C"-file art: CBODY paperdolls, COMSPR combat sprites, CPIC creatures)
 * straight to the 8-bit screen.
 *
 * Geometry recovered from L2d4e's parameter math: the glyph metric's
 * bpp_w byte is the 1bpp byte-width, so the pixel width is bpp_w*8 and
 * the row stride is 2*bpp_w bytes — i.e. (2*bpp_w*8)/(bpp_w*8) = 2 bits
 * per pixel. So these are 2bpp (4-colour) tiles: each source byte packs
 * four pixels (2 bits each, MSB-first), value 0..3. CBODY/COMSPR are
 * 32x32, CPIC 64-wide.
 *
 * The four 2bpp levels are a stencil (0 = body, 1 = highlight, 2 =
 * accent, 3 = background): the actual colours are a per-sprite SUB-
 * PALETTE. FRUA stores a default in each C-file's item-1 header and
 * recolours levels per character from the character record (CODE 15
 * reads colour fields @188/@189). `subpal[4]` maps each level to a clut
 * 129 index; a level whose entry is 0xff is transparent. */
static void ctile_blit(unsigned char *px, short pitch, short sw, short sh,
                       short x, short y, const unsigned char *metric,
                       const unsigned char *body, short scale,
                       const unsigned char *subpal)
{
	short h      = (short)(((unsigned short)metric[0] << 8) | metric[1]);
	short bppw   = (short)metric[6];
	short stride = (short)(2 * bppw);        /* bytes per row */
	short w      = (short)(8 * bppw);        /* pixels per row (2bpp) */
	short row, col, dx, dy;

	if (h <= 0 || bppw <= 0)
		return;
	for (row = 0; row < h; row++) {
		const unsigned char *r = body + (long)row * stride;
		for (col = 0; col < w; col++) {
			unsigned char v     = r[col >> 2];
			unsigned char level = (unsigned char)((v >> (2 * (3 - (col & 3)))) & 3);
			unsigned char idx   = subpal[level];

			if (idx == 0xff)                 /* transparent level */
				continue;
			for (dy = 0; dy < scale; dy++)
				for (dx = 0; dx < scale; dx++) {
					short sx = (short)(x + col * scale + dx);
					short sy = (short)(y + row * scale + dy);
					if (sx >= 0 && sx < sw && sy >= 0 && sy < sh)
						px[(long)sy * pitch + sx] = idx;
				}
		}
	}
}

/* clut_nearest — index of the clut-129 entry closest to an 8-bit RGB. */
static unsigned char clut_nearest(const RGBColor *pal, short r, short g, short b)
{
	long best = 0x7fffffffL;
	unsigned char bi = 0;
	short i;

	for (i = 0; i < 256; i++) {
		short dr = (short)((pal[i].red   >> 8) - r);
		short dg = (short)((pal[i].green >> 8) - g);
		short db = (short)((pal[i].blue  >> 8) - b);
		long  d  = (long)dr * dr + (long)dg * dg + (long)db * db;
		if (d < best) { best = d; bi = (unsigned char)i; }
	}
	return bi;
}

/* sprite_row — load a colour "C"-file tile library and blit a run of its
 * tiles across one screen row, using the file's OWN sub-palette (item 1,
 * four RGB triples) matched to clut 129. `start` is the GLIB tile index
 * of the appearance to draw (FRUA picks it per character from the record
 * field @189); `buf`/`pal` are caller-owned. Returns the tile width. */
static short sprite_row(unsigned char *buf, long buflen,
                        const char *pname, short start, short n,
                        unsigned char *px, short pitch, short sw, short sh,
                        short ytop, short scale, const RGBColor *pal)
{
	unsigned char metric[8], subpal[4];
	short refnum = 0, i, xx = 4, tilew = 0;
	long count, base, lb, pl;

	if (FSOpen((ConstStr255Param)pname, 0, &refnum) != noErr)
		return 0;
	count = buflen;
	(void)FSRead(refnum, &count, buf);
	(void)FSClose(refnum);
	base = (long)(uintptr_t)buf;
	if (l37aa(base, 0) == 0)
		return 0;

	/* item 1 is the file's default sub-palette: four RGB triples for the
	 * 2bpp levels (the rest filled with the magenta "unused" marker).
	 * Match each to a clut-129 index; level 3 is the transparent bg. */
	subpal[0] = 0; subpal[1] = 15; subpal[2] = 2; subpal[3] = 0xff;
	pl = l2856(base, 1, metric);
	if (pl != 0) {
		const unsigned char *p = (const unsigned char *)(uintptr_t)pl;
		for (i = 0; i < 3; i++)
			subpal[i] = clut_nearest(pal, p[i * 3], p[i * 3 + 1], p[i * 3 + 2]);
		subpal[3] = 0xff;
	}
	for (i = 0; i < n; i++) {
		short h, bppw, w;

		lb = l2856(base, (short)(start + i), metric);
		if (lb == 0)
			continue;
		h    = (short)(((unsigned short)metric[0] << 8) | metric[1]);
		bppw = (short)metric[6];
		if (h <= 0 || bppw <= 0 || (metric[7] & 0x0f) != 1)
			continue;                    /* not a mode-1 colour tile */
		w = (short)(8 * bppw);           /* 2bpp pixel width */
		if (xx + w * scale > sw)
			break;
		ctile_blit(px, pitch, sw, sh, xx, ytop, metric,
		           (const unsigned char *)(uintptr_t)lb, scale, subpal);
		xx = (short)(xx + w * scale + 2);
		tilew = w;
	}
	return tilew;
}

/* port_sprite_demo — render FRUA's COLOUR art (mode-1 4bpp/8bpp tiles
 * from the "C" files) through clut 129: rows of character paperdolls
 * (CBODY), combat sprites (COMSPR), and creature pictures (CPIC). Proves
 * the colour-sprite path the B&W dungeon walls don't exercise. */
void port_sprite_demo(void)
{
	static unsigned char buf[120000];
	unsigned char *px;
	short pitch, sw, sh, y, i;
	RGBColor pal[256];
	Handle ch;

	static unsigned char rec[256];           /* synthetic character record */
	const unsigned char *cr;
	short app;

	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == 0)
		return;

	/* install clut 129 (the 256-colour game palette) as the screen CLUT */
	ch = GetResource(0x636C7574L /* 'clut' */, 129);
	if (ch != NULL && *ch != NULL) {
		const unsigned char *cd = (const unsigned char *)*ch;
		for (i = 0; i < 256; i++) {
			pal[i].red   = (unsigned short)((cd[8 + i * 8 + 2] << 8) | cd[8 + i * 8 + 3]);
			pal[i].green = (unsigned short)((cd[8 + i * 8 + 4] << 8) | cd[8 + i * 8 + 5]);
			pal[i].blue  = (unsigned short)((cd[8 + i * 8 + 6] << 8) | cd[8 + i * 8 + 7]);
		}
	}
	/* reserve index 254 as a neutral panel background (clut greys vary) */
	pal[254].red = pal[254].green = pal[254].blue = 0x5800;
	qd_set_palette(pal, 0, 256);

	/* Seed a synthetic character record and point the engine's current-
	 * character pointer (g_a5_-27932) at it, so we read the appearance the
	 * way the game does — record field @189 (the icon FRUA's character
	 * creation stores; CODE 15's CBODYS load feeds it to the tile pick).
	 * The COLOURS come from each C-file's own item-1 sub-palette (derived
	 * in sprite_row), matched to clut 129 — i.e. FRUA's real art colours,
	 * not hand-picked. */
	for (i = 0; i < 256; i++)
		rec[i] = 0;
	rec[189] = 6;                            /* an appearance/icon index */
	g_a5_long(-27932) = (long)(uintptr_t)rec;
	cr  = (const unsigned char *)(uintptr_t)g_a5_long(-27932);
	app = (short)cr[189];                    /* read it back as the engine does */

	for (y = 0; y < sh; y++)
		memset(px + (long)y * pitch, 254, (size_t)sw);   /* panel bg */

	/* Each C-file rendered with ITS OWN item-1 sub-palette (FRUA's real
	 * colours). CBODY starts at the record's appearance index. */
	(void)sprite_row(buf, (long)sizeof buf, "\011CBODY.TLB",  (short)(2 + app), 10,
	                 px, pitch, sw, sh, (short)8,   (short)2, pal);
	(void)sprite_row(buf, (long)sizeof buf, "\012COMSPR.TLB", 2, 10,
	                 px, pitch, sw, sh, (short)78,  (short)2, pal);
	(void)sprite_row(buf, (long)sizeof buf, "\010CPIC.TLB",   2, 10,
	                 px, pitch, sw, sh, (short)148, (short)2, pal);

	qd_present();
	for (;;)
		qd_present();
}

void port_render_geo_contact(void)
{
	unsigned char *px;
	short pitch, sw, sh;
	unsigned char *ds;
	short n, slot = 0;
	const short cols = 10, tw = 31, th = 42;   /* 1px/tile thumbnails */
	RGBColor demo[8];

	ds = (unsigned char *)(uintptr_t)g_a5_long(-12300);
	if (ds == 0)
		return;
	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == 0)
		return;

	demo[0].red = 0x1000; demo[0].green = 0x1000; demo[0].blue = 0x2800;
	demo[1].red = 0x2000; demo[1].green = 0x2000; demo[1].blue = 0x4000;
	demo[2].red = 0x3000; demo[2].green = 0x3000; demo[2].blue = 0x5800;
	demo[3].red = 0x4000; demo[3].green = 0x4000; demo[3].blue = 0x7000;
	demo[4].red = demo[4].green = demo[4].blue = 0;
	demo[5].red = demo[5].green = demo[5].blue = 0xffff;   /* 253 wall */
	demo[6].red = demo[6].green = demo[6].blue = 0;
	demo[7].red = demo[7].green = demo[7].blue = 0;
	qd_set_palette(demo, 248, 8);

	for (n = 0; n < sh; n++)
		memset(px + (long)n * pitch, 255, (size_t)sw);

	for (n = 1; n <= 40; n++) {
		const unsigned char *map;
		short hdr, w, h, x, y, ox, oy;

		ds[0] = 0; ds[1] = 0;          /* sentinel — overwritten only on load */
		jt198(n);
		hdr = geo_hdr_word(ds);
		if (hdr < 100 || hdr > 106)
			continue;                  /* GEOnnn not present */
		w = (unsigned char)ds[2];
		h = (unsigned char)ds[3];
		if (w <= 0 || h <= 0 || (long)w * h > 576)
			continue;
		map = ds + 290;
		ox = (short)((slot % cols) * tw + 2);
		oy = (short)((slot / cols) * th + 2);

		for (y = 0; y < h; y++) {
			for (x = 0; x < w; x++) {
				/* row-major, packed at the map's own width */
				const unsigned char *t = map + ((long)y * w + x) * 6;
				unsigned char c = (t[0] | t[1] | t[2] | t[3])
				                  ? (unsigned char)253
				                  : (unsigned char)(248 + (t[5] & 3));
				map_px(px, pitch, sw, sh, ox + x, oy + y, c);
			}
		}
		slot++;
	}
#ifdef FRUA_ENGINE_PROBE
	dbg_log_num("geo contact: maps rendered = ", slot);
#endif
	qd_present();
}

/* ===== TEST SCAFFOLD — REVERT WHEN JT[557] / JT[585] LAND =====
 *
 * Synthesize a minimal character record + multi-word source prompt
 * so jt904's roster chain (jt904 -> L1276 -> jt182 -> L206e ->
 * L2184 -> L1a0c -> L1bfe -> L1aea) has real data to walk. Without
 * it, every roster paint short-circuits at the NULL deref guards.
 *
 * The record fields are picked to flip jt904's driver flags:
 *
 *   rec[8..11] = non-zero handle    -> jt155(0) fires
 *   rec[198]   = 1 (low 7 bits set) -> cond1, jt155(1) fires
 *   rec[76]    = 1 short            -> cond2, jt155(2) + jt155(4) fire
 *   rec[89]    = 3                  -> L4e56/L4ec6 take check_mode
 *   rec[94]    = 0                  -> L4e56/L4ec6 don't bail
 *   rec[128]   = 1                  -> L4ec6 returns true, jt155(6) fires
 *
 * g_a5_-13804 points at a writable source prompt so L2184's inner
 * walk can mutate it (Mac behaviour — overwrites boundaries with
 * NUL).
 *
 * Called from boot_a5_seed_defaults() once at boot AND from l07dc
 * right after l5124() — because l5124 faithfully clears
 * g_a5_-27932 = 0 (the Mac-correct fresh-game init) which would
 * wipe the boot-time seed before jt918 / jt904 see it.
 *
 * Remove both call sites + this function once the design-load
 * chain lifts upstream and sets g_a5_-27932 from real data. */
void port_test_seed_design(void)
{
	static unsigned char k_test_record[512];
	static char          k_test_prompt[] =
	    "exit Add Modify Delete View";
	long handle_placeholder = 0xDEADBEEFL;

	memset(k_test_record, 0, sizeof k_test_record);
	*(long  *)(k_test_record +   8) = handle_placeholder;
	*(short *)(k_test_record +  76) = (short)1;
	k_test_record[ 89] = 3;
	k_test_record[ 94] = 0;
	k_test_record[128] = 1;
	k_test_record[147] = 0;          /* skip special-check arm */
	k_test_record[198] = 1;

	g_a5_long(-27932) = (long)(uintptr_t)k_test_record;
	g_a5_long(-13804) = (long)(uintptr_t)k_test_prompt;

	/* Seed a test PARTY so the Training Hall roster grid (l02dc) shows
	 * real entries. The roster is a linked list (next ptr at record +0)
	 * walked from g_a5_-27928; each record carries the name at +96, HP at
	 * +385, AC at +395 (the fields l02dc / jt25 / jt32 / jt34 read). Until
	 * character creation (CODE 17) lands, this stands in for a created
	 * party so "a party exists" and the roster is populated. */
	{
		static unsigned char k_party[3][512];
		static const char   *k_names[3] = { "Bramble", "Korin Vale", "Sable" };
		static const unsigned char k_hp[3] = { 18, 24, 11 };
		static const unsigned char k_ac[3] = { 5, 7, 4 };
		int p, c;

		for (p = 0; p < 3; p++) {
			memset(k_party[p], 0, sizeof k_party[p]);
			for (c = 0; k_names[p][c] != 0 && c < 15; c++)
				k_party[p][96 + c] = (unsigned char)k_names[p][c];
			k_party[p][96 + c] = 0;
			k_party[p][385] = k_hp[p];           /* HP  */
			k_party[p][395] = k_ac[p];           /* AC  */
			*(long *)(k_party[p]) =              /* next ptr (+0) */
			    (p < 2) ? (long)(uintptr_t)k_party[p + 1] : 0L;
		}
		g_a5_long(-27928) = (long)(uintptr_t)k_party[0];   /* roster head */
	}

	/* Enable the case-0 Training Hall action (Train Character) so its
	 * handler l0f1a -> jt574 fires, reaching the char-creation screen. */
	g_a5_byte(-14440) = 1;

	/* Seed the current design name so jt127 builds the real
	 * "<design>:<file>" path. mac_path_to_c strips the design
	 * prefix, so the exact value only matters for path fidelity /
	 * future subfolder support — the flat staging resolves the
	 * bare filename either way. */
	{
		static const char dn[] = "tutorial.dsn";
		char *dst = (char *)&g_a5_31336;
		int   i;

		for (i = 0; dn[i] != 0; i++)
			dst[i] = dn[i];
		dst[i] = 0;
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
/* Faithful Mac stream parser. The first arg (shape0) IS the first
 * cmd code; subsequent args are read as the cmd dictates. Stream
 * ends at cmd == 0. On the C side every arg occupies 4 bytes
 * (vararg promotion + long), so we always va_arg(ap, long) and
 * cast to short for cmds that read 2 bytes on Mac. The Mac
 * stream-pointer arithmetic (fp@(-8) += 2 per short, += 4 per
 * long) collapses to a single va_arg per logical Mac arg here. */
static void jt452(long shape0, ...)
{
	va_list        ap;
	unsigned char *rec  = NULL;
	long          *table;
	long           cmd  = shape0;

	PROBE("jt452");
	if (g_a5_9254 == 0)
		jt452_init();
	table = (long *)g_a5_buf(-9282);

	va_start(ap, shape0);

	while (cmd != 0) {
		switch ((short)cmd) {

		/* ---- 1..8: allocate new DLItem ---- */
		case 1: case 2: case 3: case 4:
		case 5: case 6: case 7: case 8: {
			if (g_a5_9250 >= g_a5_9288) {
				PROBE("jt452: DLItem table full");
				goto exit;
			}
			rec = g_dlitem_pool + (long)g_a5_9250 * DLITEM_BYTES;
			memset(rec, 0, DLITEM_BYTES);
			g_a5_9250++;

			/* Method ptr: shape 8 = caller-supplied raw long;
			 * shapes 1..7 = g_a5_-9282 handler table. */
			if (cmd == 8) {
				*(long *)rec = va_arg(ap, long);
			} else {
				short idx = (short)(cmd - 1);

				if (idx < 0 || idx >= 7)
					idx = 0;
				*(long *)rec = table[idx];
			}

			/* Shape-specific arg consumption:
			 *   2 → 1 short (rec[22])
			 *   7 → 1 long  (rec[4])
			 *   default → 2 shorts (rec[16], rec[18]) +
			 *             5 → 2 more shorts (rec[22], rec[24])
			 *           else → 1 long (rec[12])
			 */
			if (cmd == 2) {
				*(short *)(rec + 22) =
				    (short)va_arg(ap, long);
			} else if (cmd == 7) {
				*(long *)(rec + 4) = va_arg(ap, long);
			} else {
				*(short *)(rec + 16) =
				    (short)va_arg(ap, long);
				*(short *)(rec + 18) =
				    (short)va_arg(ap, long);
				if (cmd == 5) {
					*(short *)(rec + 22) =
					    (short)va_arg(ap, long);
					*(short *)(rec + 24) =
					    (short)va_arg(ap, long);
				} else {
					*(long *)(rec + 12) =
					    va_arg(ap, long);
				}
			}
			break;
		}

		/* ---- 16..22: set bit (cmd-16) of rec[28] ---- */
		case 16: case 17: case 18: case 19:
		case 20: case 21: case 22:
			if (rec != NULL)
				rec[28] |= (unsigned char)
				           (1u << (cmd - 16));
			break;

		/* ---- 24..30: clear bit (cmd-24) of rec[28] ---- */
		case 24: case 25: case 26: case 27:
		case 28: case 29: case 30:
			if (rec != NULL)
				rec[28] &= (unsigned char)
				           ~(1u << (cmd - 24));
			break;

		/* ---- 32..42: per-field setters ---- */
		case 32:
			if (rec) rec[29] = (unsigned char)
			                   (va_arg(ap, long) & 0xff);
			break;
		case 33:
			if (rec) rec[30] = (unsigned char)
			                   (va_arg(ap, long) & 0xff);
			break;
		case 34:
			if (rec) *(long *)(rec +  4) = va_arg(ap, long);
			break;
		case 35:
			if (rec) *(long *)(rec +  8) = va_arg(ap, long);
			break;
		case 36:
			if (rec) *(short *)(rec + 24) =
			            (short)va_arg(ap, long);
			break;
		case 37:
			if (rec) *(short *)(rec + 26) =
			            (short)va_arg(ap, long);
			break;
		case 38:
			if (rec) rec[31] = (unsigned char)
			                   (va_arg(ap, long) & 0xff);
			break;
		case 39:
			if (rec) *(long *)(rec + 12) = va_arg(ap, long);
			break;
		case 40:
			if (rec) {
				*(short *)(rec + 16) =
				    (short)va_arg(ap, long);
				*(short *)(rec + 18) =
				    (short)va_arg(ap, long);
			}
			break;
		case 41:
			if (rec) *(short *)(rec + 20) =
			            (short)va_arg(ap, long);
			break;
		case 42:
			if (rec) *(short *)(rec + 22) =
			            (short)va_arg(ap, long);
			break;

		default:
			break;            /* 9..15 / 23 / 31 reserved no-ops */
		}

		cmd = va_arg(ap, long);
	}

exit:
	va_end(ap);
}
/* L30ba (CODE 3 + 0x30ba) — DLItem focus / select helper. Body lives
 * inside CODE 3's dialog runtime; stays a PROBE stub for now. */
static void   l30ba(short a, short b, short c)
                                                  { PROBE("L30ba"); (void)a;
                                                    (void)b; (void)c; }

/* L2c60 (CODE 3 + 0x2c60) — DLItem paint walker.
 *
 *   if (g_a5_-9247 == 0) {                   // first time
 *       L30ba(0, count - 1, 0);              // mark all dirty
 *       g_a5_-9247 = 1;
 *   }
 *   for (i = 0; i < count; i++) {
 *       if (force_paint != 0 || (rec[28] & 0x80))    // dirty?
 *           method(rec, 1);                  // paint
 *       rec += 32;
 *   }
 *   jt1134();                                 // yield to OS
 *
 * L2d3e Phase 1 calls L2c60(0) to paint dirty items each frame.
 * Method-dispatch goes through the shape handlers (jt376..jt382)
 * which currently delegate cmd=1 to L1676's "set dirty bit"
 * default — so no actual paint reaches the Display HAL until
 * shape-handler cmd=1 bodies are lifted. See ADR-0010 follow-up
 * for the discovery that the play loop (gated by jt315) is the
 * piece that drives this path in normal builds. */
static void l2c60(short force_paint) __attribute__((unused));
static void l2c60(short force_paint)
{
	short i, count;
	unsigned char *rec;
	typedef short (*method_t)(void *, short, ...);
	method_t method;

	PROBE("L2c60");
	if (g_a5_9247 == 0) {
		l30ba((short)0, (short)(g_a5_9250 - 1), (short)0);
		g_a5_9247 = 1;
	}

	count = g_a5_9250;
	rec = (unsigned char *)(uintptr_t)g_a5_9254;
	for (i = 0; i < count; i++) {
		short force = (short)(signed char)(force_paint & 0xff);
		unsigned char painted = rec[28];
		/* Mac semantic (L2c8e in CODE 3): paint items where bit 7
		 * is CLEAR (= "not yet painted"). cmd=1 SETS bit 7 (= "now
		 * painted, skip until something clears it"). jt444 cmd=16
		 * etc. clear bit 7 on state changes so the item gets
		 * repainted. */
		if (force != 0 || (painted & 0x80) == 0) {
			method = *(method_t *)rec;
			if (method != NULL)
				(void)method(rec, (short)1);
		}
		rec += DLITEM_BYTES;
	}
	jt1134();
}

/* CODE 4 helpers L2d3e + family forward into. PROBE stubs until the
 * actual event-pump + DLItem dispatch land. */

/* Forward — l4d88 / l6804 lift further down with jt1134's helpers.
 * jt1118 needs l4d88 for the InvalRect flush prelude; l731e needs
 * l6804 for the front-window gate. */
static void l4d88(void);
static signed char l6804(void);
static void l725c(short mask);

/* L62fa (CODE 4 + 0x62fa) — query "where is the mouse?" state.
 *
 *   Point pt;
 *   GetMouse(&pt);                          // local coords
 *   LocalToGlobal(&pt);                     // → screen coords
 *   short part = FindWindow(pt, &which);    // 0..8 hit-test region
 *   *out_part = part;
 *   return (which == g_a5_-2578) ? -1 : 0;  // our window?
 *
 * Used by L6538 to gate cursor-shape changes ("only swap to the
 * engine cursor when the pointer is in our window's content area";
 * FindWindow part 3 = inContent). LocalToGlobal is a no-op in the
 * Falcon HAL's single-window setup (window origin = screen origin),
 * so the conversion is skipped. */
static short l62fa(short *out_part) __attribute__((unused));
static short l62fa(short *out_part)
{
	Point      pt;
	WindowPtr  which = NULL;
	short      part;

	PROBE("L62fa");
	GetMouse(&pt);
	/* LocalToGlobal skipped — single-window port (local == global). */
	part = FindWindow(pt, &which);
	if (out_part != NULL)
		*out_part = part;
	return ((long)(uintptr_t)which == g_a5_long(-2578))
	       ? (short)-1 : (short)0;
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

	/* L73ce / L7362 inner loop — pump events through L725c while
	 * any match the mask. For arg=3 (jt1118's mode) Mac exits after
	 * a single pump. For arg=2 (jt1132's mode) exits when -903 is
	 * set. Default loops with refreshed mask. */
	while (l6804() == 0 || EventAvail(mask, &ev)) {
		l66e8(&ev);
		l725c(mask);
		if (arg == (short)3)
			return;
		if (arg == (short)2 && g_a5_byte(-903) != 0)
			return;
		/* Refresh mask (Mac L739a path). */
		mask = (short)-1;
		if (g_a5_byte(-820) != 0)
			mask = (short)(mask & ~0x08);
		if (arg == (short)2)
			mask = (short)(mask | 0x06);
		else if (!EventAvail((short)2, &ev))
			mask = (short)(mask & ~0x07);
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
		/* Mac engine has TWO event paths: jt1125 (used by L2d3e
		 * Phase 1) and L725c->L6dd0 (used by L731e pump). On the
		 * Mac they coexist because jt1125 reads from a separate
		 * internal buffer the IRQ fills, while L725c reads the
		 * Toolbox queue. Our port has a single OS event queue, so
		 * jt1125's WaitNextEvent consumes the event before L725c
		 * can route it to L6dd0. Stamp the engine's "key pending"
		 * state here so L2d3e Phase 5 (l31ea -> jt1118 -> jt1133)
		 * still sees the key and the cmd=5 shortcut walk fires. */
		g_a5_word(-818) = (short)(ev.message & 0xff);
		g_a5_byte(-820) = 1;
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

/* L4fae / L4e12 / L5d8c — InvalRect dispatch helpers.
 *
 *   L4fae: color-QD branch — paints pending text via PaintRect /
 *          DrawString and fills a 4-short rect. 200+ lines:
 *          SetPort + TextSize/TextMode + StringWidth + character-
 *          class table lookups (at g_a5_-3016) for word-break
 *          and highlighting. The "rect builder" framing is
 *          misleading — it actually renders.
 *   L4e12: mono branch — identical structure to L4fae but with
 *          a different port-data offset path.
 *   L5d8c: visibility test. Returns -1 (true) iff the current
 *          "back-page" index (g_a5_-2352) equals the "front-page"
 *          index (g_a5_-2354) — i.e. "are we invalidating a
 *          page that's actually on-screen?"
 *
 * L5d8c is lifted faithfully (trivial). L4fae / L4e12 remain
 * stubs that zero the rect — the full lift requires the engine's
 * font-metrics + character-class infrastructure (g_a5_-3016
 * table) and Mac Toolbox DrawString. With the boot trace not
 * driving any deferred-paint (g_a5_-936 stays 0 throughout),
 * the rect content doesn't matter yet. */
static void l4fae(short *rect_out) __attribute__((unused));
static void l4fae(short *rect_out)
{
	PROBE("L4fae");
	if (rect_out != NULL) {
		rect_out[0] = 0; rect_out[1] = 0;
		rect_out[2] = 0; rect_out[3] = 0;
	}
}
static void l4e12(short *rect_out) __attribute__((unused));
static void l4e12(short *rect_out)
{
	PROBE("L4e12");
	if (rect_out != NULL) {
		rect_out[0] = 0; rect_out[1] = 0;
		rect_out[2] = 0; rect_out[3] = 0;
	}
}
static signed char l5d8c(void) __attribute__((unused));
static signed char l5d8c(void)
{
	PROBE("L5d8c");
	return (g_a5_word(-2352) == g_a5_word(-2354))
	       ? (signed char)-1 : (signed char)0;
}

/* L4d88 (CODE 4 + 0x4d88) — flush deferred _InvalRect.
 *
 * Body:
 *
 *   if (g_a5_-936 (short) > 0) {              // pending count > 0
 *       g_a5_-1042 = g_a5_-935;               // copy mode byte
 *       if (g_a5_-926 < 0) {                  // signed signal
 *           short metric = (g_a5_-2347 != 0) ? 8 : 12;
 *           rect.top = g_a5_-928 - metric * g_a5_-936;
 *       } else {
 *           rect.top = g_a5_-926;
 *       }
 *       if (g_a5_-2347 != 0)  L4fae(&rect);   // color-QD build
 *       else                  L4e12(&rect);   // mono build
 *       if (L5d8c()) {                        // visible
 *           if (g_a5_-2346)                   // half-scale mode
 *               rect.top <<= 1; rect.left <<= 1;
 *               rect.bottom <<= 1; rect.right <<= 1;
 *           InvalRect(&rect);
 *       }
 *   }
 *   g_a5_-936 = 0;                            // clear pending count
 *   g_a5_-940 = &g_a5_-1041;                  // reset buffer ptr
 *
 * Always clears the pending count and resets the buffer pointer
 * (-940 → -1041) so subsequent invalidations get fresh coords. */
static void l4d88(void)
{
	short rect[4];                 /* fp@(-8..-2): top, left, bottom, right */
	short metric;

	PROBE("L4d88");
	if (g_a5_word(-936) > 0) {
		g_a5_byte(-1042) = (unsigned char)g_a5_byte(-935);

		if (g_a5_word(-926) < 0) {
			metric = (g_a5_2347 != 0) ? (short)8 : (short)12;
			rect[0] = (short)(g_a5_word(-928)
			                  - metric * g_a5_word(-936));
		} else {
			rect[0] = g_a5_word(-926);
		}

		if (g_a5_2347 != 0)
			l4fae(rect);
		else
			l4e12(rect);

		if (l5d8c() != 0) {
			if (g_a5_byte(-2346) != 0) {
				rect[0] <<= 1;
				rect[1] <<= 1;
				rect[2] <<= 1;
				rect[3] <<= 1;
			}
			InvalRect((Rect *)rect);
		}
	}
	g_a5_word(-936) = 0;
	g_a5_long(-940) = (long)(uintptr_t)g_a5_buf(-1041);
}

/* Forward — l71ac / l7090 / l70e0 / l7204 / l6cba / l6dd0 / l690e
 * lift further down. l725c routes every EventRecord type now;
 * each arm is at least a level-1 skeleton with full dispatch
 * still deferred for L690e's content-click / drag-title paths. */
static void l71ac(EventRecord *ev);
static void l7090(EventRecord *ev);
static void l70e0(EventRecord *ev);
static void l7204(EventRecord *ev);
static void l6cba(EventRecord *ev);
static void l6dd0(EventRecord *ev);
static void l690e(EventRecord *ev);

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
	case 3: case 5:                            /* keyDown / autoKey */
		l6dd0(&ev);
		break;
	case 1:                                    /* mouseDown */
		l690e(&ev);
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

/* jt1044 (CODE 5 + 0x5716), jt1050 (CODE 5 + 0x59ee) — Window
 * Manager helpers L747a / L74ae call. PROBE stubs; both involve
 * window allocation / disposal which the Falcon HAL doesn't
 * provide yet. */
static short jt1044(char a, void *b, short c) __attribute__((unused));
static short jt1044(char a, void *b, short c)
{
	PROBE("jt1044");
	(void)a; (void)b; (void)c;
	return 0;
}
static short jt1050(short a, short b) __attribute__((unused));
static short jt1050(short a, short b)
{
	PROBE("jt1050");
	(void)a; (void)b;
	return 0;
}

/* L74ae (CODE 4 + 0x74ae) — refresh after menu-bar window close.
 *
 *   if (g_a5_-180 > 0) {
 *       jt1050(-4, 0);                       // dispose window?
 *       if (g_a5_-779)
 *           MBarHeight = 0;                  // hide system menu bar
 *   }
 *
 * The MBarHeight write (Mac low-mem global at 0x280) is skipped
 * — Atari has no equivalent global and writing to absolute 0x280
 * would corrupt random low memory. The Falcon HAL handles menu
 * bar visibility separately. */
static void l74ae(void) __attribute__((unused));
static void l74ae(void)
{
	PROBE("L74ae");
	if (g_a5_word(-180) <= 0)
		return;
	jt1050((short)-4, (short)0);
	/* MBarHeight (Mac 0x280) write skipped — no Atari equivalent. */
}

/* L747a (CODE 4 + 0x747a) — schedule menu-bar paint job.
 *
 * Sets up the paint descriptor cluster around g_a5_-196 then
 * calls jt1044(1, &g_a5_-196, 0) to enqueue it:
 *
 *   g_a5_-184 = c;                           // 3rd arg
 *   g_a5_-172 = -4;                          // constant
 *   g_a5_-164 = ptr;                         // 1st arg
 *   g_a5_-160 = b;                           // 2nd arg
 *   g_a5_-152 = 0;
 *   jt1044(1, &g_a5_-196, 0);
 *
 * Called by jt1145 (l747a(&g_a5_-216, 6, 0)) and jt1122
 * (l747a(&g_a5_-210, 14, 0)). */
static void l747a(void *p1, long p2, long p3) __attribute__((unused));
static void l747a(void *p1, long p2, long p3)
{
	PROBE("L747a");
	g_a5_long(-184) = p3;
	g_a5_word(-172) = (short)-4;
	g_a5_long(-164) = (long)(uintptr_t)p1;
	g_a5_long(-160) = p2;
	g_a5_word(-152) = 0;
	(void)jt1044((char)1, g_a5_buf(-196), (short)0);
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

/* L24aa (CODE 4 + 0x24aa) = JT[1178] — palette restore on
 * suspend/resume.
 *
 * Despite earlier "menu repaint" guess, this is actually the
 * Palette Manager state restore. Frame is 1036 bytes (a 1024-byte
 * palette table + scratch); body:
 *
 *   if (!color_QD) goto mono_path;             // g_a5_-2347 == 0
 *   if (!color_setup_done || depth != 8)       // g_a5_-1312 / -1318
 *       goto fast_path;
 *
 *   // Full color path — depth == 8, palette enabled
 *   Handle ph = g_a5_-1310;                    // palette Handle
 *   bset #7, (*ph)[4];                          // dirty flag
 *   bclr #6, (*ph)[4];
 *   long stash[256];                            // local table
 *   // Loop 1..15: snapshot system palette via _PMForeColor
 *   for (i = 1; i < 16; i++) {
 *       long entry = (*ph + 10) + i*8;          // PaletteEntry
 *       _PMForeColor(entry);
 *       stash[i] = saved_color;
 *   }
 *   // Loop 16..255: identity map
 *   for (i = 16; i < 256; i++)
 *       stash[i] = i;
 *
 *   ... 600+ more lines installing the palette into the GrafPort
 *   ... pmTable structure and triggering _PaintRect on the
 *   ... visible content rect.
 *
 * Lifting requires a Palette Manager equivalent in the compat
 * shim (_PMForeColor / pmTable / PaletteHandle) which the
 * current shim doesn't model. The Falcon HAL handles palette
 * via VIDEL color regs directly — when that's wired, this
 * function should map to a single "reapply engine palette" call.
 *
 * Stays a PROBE stub until the Palette Manager shim or direct
 * VIDEL bridge lands. Dormant in boot trace (only L71ac /
 * L7204 osEvt resume paths reach it). */
static void l24aa(void) __attribute__((unused));
static void l24aa(void)
{
	PROBE("L24aa");
}

/* L3d8c / L7de0 / L448c / L4350 / L1084 — L3e38's helper chain.
 * All PROBE-only stubs; the full repaint walks page descriptors
 * and the clip region. */
static void l3d8c(void *entry, void *win) __attribute__((unused));
static void l3d8c(void *entry, void *win)
{
	PROBE("L3d8c");
	(void)entry; (void)win;
}
static signed char l7de0(void) __attribute__((unused));
static signed char l7de0(void)
{
	PROBE("L7de0");
	return 0;
}
/* L448c (CODE 4 + 0x448c) — probe current screen pixel depth.
 *
 * Mac body:
 *   if (g_a5_-1314 != 0) return;                 // already cached
 *   CGrafPtr p = GetCWMgrPort();                  // trap 0xAA2A
 *   short bpp = (*(PixMapHandle *)(p->portPixMap))->pixelSize;
 *   g_a5_-1318 = bpp;
 *   g_a5_-1315 = (bpp == 1) ? 0xFF : 0;            // mono flag
 *
 * Falcon HAL is locked at 8bpp (platform/display_videl.c puts the
 * VIDEL in a 256-colour mode), so the probe is deterministic for
 * the port: depth = 8, mono = 0. Hard-coding skips the missing
 * Toolbox GetCWMgrPort + PixMapHandle deref chain.
 *
 * The TT backend (when it lands) is also 8bpp on the same code
 * path; the mono branch fires only if a 1bpp HAL ever ships. */
static void l448c(void) __attribute__((unused));
static void l448c(void)
{
	PROBE("L448c");
	if (g_a5_byte(-1314) != 0)
		return;
	g_a5_word(-1318) = (short)8;       /* Falcon VIDEL chunky 8bpp */
	g_a5_byte(-1315) = (signed char)0; /* not mono */
}
static void l4350(short flag) __attribute__((unused));
static void l4350(short flag)
{
	PROBE("L4350");
	(void)flag;
}
static void jt1084(void *buf, short val) __attribute__((unused));
static void jt1084(void *buf, short val)
{
	PROBE("jt1084");
	(void)buf; (void)val;
}

/* L3e38 (CODE 4 + 0x3e38) = JT[1162] — window content repaint
 * dispatcher. Called inside the BeginUpdate / EndUpdate bracket
 * of L7090 (updateEvt arm).
 *
 * Body (level-1 skeleton — leaves are PROBE stubs; the
 * 200+-line full dispatch is its own task):
 *
 *   short cur_page = g_a5_-2354;
 *   // Normalize page index to [0, 1].
 *   if (cur_page < 0 || cur_page > 1) {
 *       jt1084(&g_a5_-2610, cur_page);        // error / log
 *       g_a5_-2354 = 1;
 *   }
 *
 *   // Idle + front → "fast path" exit:
 *   if (g_a5_-1316 == 0 && L6804()) {
 *       // L3e8e branch — many sub-paths around L7de0 / ValidRect
 *       // / L448c / L4350 / page-descriptor walks.  Deferred.
 *       PROBE("L3e38:idle-frontwindow-deferred");
 *       return;
 *   }
 *
 *   // Dirty or background → blit current page:
 *   unsigned char *entry = g_a5_-2570 + 108 * cur_page;
 *   L3d8c(entry, (void *)g_a5_-2578);
 *
 * The L3e8e branch is a substantial sub-dispatch (handles
 * "page-swap-in-progress" + clip-region updates + color-QD
 * specific blits). Left as deferred because it'd more than
 * double this function's size and isn't reachable in the boot
 * trace anyway (updateEvt not queued). */
static void l3e38(void)
{
	short cur_page;

	PROBE("L3e38");
	cur_page = g_a5_word(-2354);

	if (cur_page < 0 || cur_page > 1) {
		jt1084(g_a5_buf(-2610), cur_page);
		g_a5_word(-2354) = 1;
		cur_page = 1;
	}

	if (g_a5_byte(-1316) == 0 && l6804() != 0) {
		/* L3e8e branch — idle + frontmost.
		 *
		 * 1. If L7de0 says a page-swap is pending, just call
		 *    ValidRect on the content rect (no fresh paint
		 *    needed) and exit.
		 * 2. Otherwise, if -1314 is clear (no page-busy),
		 *    probe depth via L448c; on depth change AND
		 *    color-QD AND -1312 set, call L4350 with the
		 *    "depth == 8" flag for the swap; then L24aa to
		 *    restore the palette.
		 * 3. Read the window's visRgn bbox (window+24 → handle
		 *    → +2) into local rect; clamp to >= 0 and
		 *    <= 2 * L04cc / L04de in half-scale or L04cc /
		 *    L04de in full-scale.
		 * 4. If the rect ends up empty, exit (L4342).
		 * 5. Otherwise perform the actual page-descriptor
		 *    walk + blit (400+ lines, deferred).
		 */
		PROBE("L3e38:idle-frontwindow");
		if (l7de0() != 0) {
			void *win_ptr = (void *)(uintptr_t)g_a5_long(-2578);
			if (win_ptr != NULL)
				ValidRect((Rect *)((unsigned char *)win_ptr + 16));
			return;
		}
		if (g_a5_byte(-1314) == 0) {
			short saved_depth = g_a5_word(-1318);
			l448c();
			if (saved_depth != g_a5_word(-1318)
			    && g_a5_2347 != 0
			    && g_a5_byte(-1312) != 0) {
				l4350((short)((g_a5_word(-1318) == 8)
				              ? -1 : 0));
			}
			l24aa();
		}
		/* Visible-rect copy + clamp + blit deferred — needs
		 * window region deref + page-descriptor walk. */
		PROBE("L3e38:idle-blit-deferred");
		return;
	}

	{
		unsigned char *entry = g_a5_buf(-2570) + 108 * cur_page;
		void          *win   = (void *)(uintptr_t)g_a5_long(-2578);
		l3d8c(entry, win);
	}
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

/* Forward — l0004 lifts further down (used by L690e + L6dd0). */
static void l0004(long menu_selection);

/* L6b26 (CODE 4 + 0x6b26) — inContent click body.
 *
 *   if (FrontWindow() != which) { SelectWindow(which); return; }
 *   if (which != g_a5_-2578)    return;             // not our window
 *   Point pt = ev.where;
 *   GlobalToLocal(&pt);
 *   if (g_a5_-2346) { pt.v >>= 1; pt.h >>= 1; }     // half-scale
 *   g_a5_-912 = pt.h;
 *   g_a5_-910 = pt.v;
 *   g_a5_-901 = 1;                                  // click captured
 *   g_a5_-904 = 1;
 *   g_a5_-902 = (ev.modifiers & 0x02) ? 1 : 0;      // shift held
 *
 * Falcon HAL has no GlobalToLocal yet — ev.where is treated as
 * local (single window at origin). */
static void l6b26(EventRecord *ev, WindowPtr which) __attribute__((unused));
static void l6b26(EventRecord *ev, WindowPtr which)
{
	short h, v;

	PROBE("L6b26");
	if (ev == NULL)
		return;
	if ((long)(uintptr_t)FrontWindow() != (long)(uintptr_t)which) {
		SelectWindow(which);
		return;
	}
	if ((long)(uintptr_t)which != g_a5_long(-2578))
		return;

	h = ev->where.h;
	v = ev->where.v;
	if (g_a5_byte(-2346) != 0) {
		v >>= 1;
		h >>= 1;
	}
	g_a5_word(-912) = h;
	g_a5_word(-910) = v;
	g_a5_byte(-901) = 1;
	g_a5_byte(-904) = 1;
	g_a5_byte(-902) = (ev->modifiers & 0x02) ? 1 : 0;
}

/* L690e (CODE 4 + 0x690e) — mouseDown arm.
 *
 * L725c case-1 dispatch. Cases:
 *
 *   0   inDesk      — ignore
 *   1   inMenuBar   — MenuSelect → L0004 → HiliteMenu(0)
 *   2   inSysWindow — Mac DA system (no-op on Atari)
 *   3   inContent   — L6b26 capture click
 *   4   inDrag      — drag title bar (deferred)
 *   5   inGrow      — falls through to L6b26 for our window
 *   6   inGoAway    — TrackGoAway + CloseWindow
 *   7/8 zoom in/out — TrackBox + SizeWindow (deferred)
 *
 * Cases 4 (inDrag) and 7/8 (zoom) involve substantial Mac-style
 * window-management math (drag region, zoom rect computation)
 * that doesn't map well to the Falcon HAL's single-window setup.
 * Tagged with arm-specific PROBE markers so the trace shows what
 * fires; the actual drag/zoom is a HAL job once we have it. */
static void l690e(EventRecord *ev) __attribute__((unused));
static void l690e(EventRecord *ev)
{
	WindowPtr which = NULL;
	short     part;
	long      menu_sel;

	PROBE("L690e");
	if (ev == NULL)
		return;

	part = FindWindow(ev->where, &which);

	switch (part) {
	case 0:                                    /* inDesk */
		break;
	case 1:                                    /* inMenuBar */
		menu_sel = MenuSelect(ev->where);
		if ((menu_sel & 0xFFFF0000L) != 0) {
			l0004(menu_sel);
			HiliteMenu((short)0);
		}
		break;
	case 2:                                    /* inSysWindow (DA) */
		PROBE("L690e:sysclick-deferred");
		/* Real Mac: SystemClick(event, which) — no Atari DA system */
		(void)which;
		break;
	case 3:                                    /* inContent */
		l6b26(ev, which);
		break;
	case 4:                                    /* inDrag */
		PROBE("L690e:drag-deferred");
		break;
	case 5:                                    /* inGrow */
		/* Mac falls through to inContent for our window; for
		 * other windows it just selects. Falcon doesn't have
		 * resizeable windows, so this is the right shape. */
		if ((long)(uintptr_t)which == g_a5_long(-2578)) {
			l6b26(ev, which);
		} else if ((long)(uintptr_t)FrontWindow()
		           != (long)(uintptr_t)which) {
			SelectWindow(which);
		}
		break;
	case 6:                                    /* inGoAway */
		if (TrackGoAway(which, ev->where))
			CloseWindow(which);
		break;
	case 7: case 8:                            /* inZoomIn/Out */
		PROBE("L690e:zoom-deferred");
		break;
	default:
		break;
	}
}

/* JT[391] (CODE 3 + 0x3702) — is_letter(ch).
 *
 *   return is_lower(ch) || is_upper(ch);    // L4648 || L466a
 *
 * The Mac body factored out 'a'..'z' (L4648) and 'A'..'Z' (L466a)
 * as separate helpers; we inline both range checks. Used by L6dd0
 * to gate the "Cmd+letter → key code (toupper(ch) + 255)" path. */
static signed char jt391(short ch) __attribute__((unused));
static signed char jt391(short ch)
{
	PROBE("jt391");
	return ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))
	       ? (signed char)1 : (signed char)0;
}

/* JT[422] (CODE 3 + 0x468c) — toupper(ch).
 *
 *   return is_lower(ch) ? ch - 32 : ch;     // L4648-gated
 *
 * Companion to jt391. L6dd0's Cmd+letter path computes
 * `jt422(ch) + 255`, mapping both 'a' (97) and 'A' (65) to FRUA
 * key code 320 (= 65 + 255). */
static short jt422(short ch) __attribute__((unused));
static short jt422(short ch)
{
	PROBE("jt422");
	return (ch >= 'a' && ch <= 'z') ? (short)(ch - 32) : ch;
}

/* L0004 (CODE 4 + 0x0004) — segment entry / menu dispatch. Called
 * from L6dd0 with the long result of MenuKey when a Cmd-key
 * combo hits a menu item. PROBE-only stub — the segment-entry
 * dispatcher is complex (linkw fp,#-268, multi-arm JT[3]). */
static void l0004(long menu_selection) __attribute__((unused));
static void l0004(long menu_selection)
{
	PROBE("L0004");
	(void)menu_selection;
}

/* L6dd0 (CODE 4 + 0x6dd0) — keyDown / autoKey arm.
 *
 * L725c routes case 3 (keyDown) and case 5 (autoKey) here.
 *
 * Mac body (partial lift):
 *   if (event.modifiers & 0x01) {              // Cmd held
 *       long sel = MenuKey((char)(event.message_byte_5));
 *       if ((sel >> 16) != 0) {                // menu match
 *           L0004(sel);                        // dispatch
 *           return;
 *       }
 *       // No menu match: check printable / control class
 *       char ch = (char)(event.message & 0xff);
 *       if (jt391(ch))
 *           g_a5_-818 = jt422(ch) + 255;
 *       else
 *           g_a5_-818 = ch & 0x1f;             // ctrl-letter
 *       g_a5_-820 = 1;
 *   } else {
 *       g_a5_-820 = 1;
 *       short lo = event.message & 0xFFFF;
 *       JT[2] dispatch on lo (25-entry table mapping Mac scan
 *       codes to FRUA logical keys 256..286);
 *       // default arm:
 *       JT[2] dispatch on (lo & 0xFF) for arrow keys (258/260/
 *       262/264) and Tab (9);
 *       // default default: g_a5_-818 = lo & 0x7F;
 *   }
 *
 * Partial lift: the JT[2] tables decode 25+ Mac-specific scan
 * codes mapping to FRUA key codes 256..286 (function keys F1..)
 * and a smaller default table for arrow keys. Lifted as the
 * common case: low byte stored as ASCII into g_a5_-818, special
 * scan codes still need the per-entry table decoded from the
 * inline JT[2] payload. */
static void l6dd0(EventRecord *ev) __attribute__((unused));
static void l6dd0(EventRecord *ev)
{
	long  msg;
	short ch;
	short lo;
	long  sel;

	PROBE("L6dd0");
	if (ev == NULL)
		return;

	msg = (long)ev->message;
	lo  = (short)(msg & 0xFFFFL);
	ch  = (short)(msg & 0xFFL);                /* low byte = char */

	if ((ev->modifiers & 0x01) != 0) {
		/* Cmd-key path. Mac uses byte 5 (low byte of message,
		 * the char) as the MenuKey input. */
		sel = MenuKey(ch);
		if ((sel & 0xFFFF0000L) != 0) {
			l0004(sel);
			return;
		}
		if (jt391(ch))
			g_a5_word(-818) = (short)(jt422(ch) + 255);
		else
			g_a5_word(-818) = (short)(ch & 0x1f);
		g_a5_byte(-820) = 1;
		return;
	}

	/* Non-Cmd path. */
	g_a5_byte(-820) = 1;

	/* Primary JT[2] dispatch on the full low word (scan-code in
	 * high byte | char in low byte). 25 mappings decoded from the
	 * inline table at CODE 4 + 0x6e84 — Mac scan codes for
	 * numeric keypad digits 1..0 (with chars '1'..'9','0') map to
	 * FRUA function-key codes 256..265, and 15 more scan codes
	 * with char 0x10 map to extended codes 272..286.
	 *
	 * If no primary match: secondary JT[2] dispatch on the low
	 * byte alone (table at CODE 4 + 0x7026) handles Ctrl-C → CR,
	 * Tab passthrough, arrow keys (0x1c..0x1f → 262/258/264/260),
	 * and the default (char & 0x7F). */
	switch (lo) {
	/* Numeric keypad 1..0 — Mac F-key mappings */
	case 0x5331: g_a5_word(-818) = 261; return;
	case 0x5432: g_a5_word(-818) = 260; return;
	case 0x5533: g_a5_word(-818) = 259; return;
	case 0x5634: g_a5_word(-818) = 262; return;
	case 0x5735: g_a5_word(-818) = 256; return;
	case 0x5836: g_a5_word(-818) = 258; return;
	case 0x5937: g_a5_word(-818) = 263; return;
	case 0x5b38: g_a5_word(-818) = 264; return;
	case 0x5c39: g_a5_word(-818) = 257; return;
	case 0x5230: g_a5_word(-818) = 265; return;
	/* Function keys F1..F15 — scan-code | 0x10 (char unused) */
	case 0x7a10: g_a5_word(-818) = 272; return;
	case 0x7810: g_a5_word(-818) = 273; return;
	case 0x6310: g_a5_word(-818) = 274; return;
	case 0x7610: g_a5_word(-818) = 275; return;
	case 0x6010: g_a5_word(-818) = 276; return;
	case 0x6110: g_a5_word(-818) = 277; return;
	case 0x6210: g_a5_word(-818) = 278; return;
	case 0x6410: g_a5_word(-818) = 279; return;
	case 0x6510: g_a5_word(-818) = 280; return;
	case 0x6d10: g_a5_word(-818) = 281; return;
	case 0x6710: g_a5_word(-818) = 282; return;
	case 0x6f10: g_a5_word(-818) = 283; return;
	case 0x6910: g_a5_word(-818) = 284; return;
	case 0x6b10: g_a5_word(-818) = 285; return;
	case 0x7110: g_a5_word(-818) = 286; return;
	default:     break;
	}

	/* Secondary table — low byte only. */
	switch (lo & 0xFF) {
	case 0x03: g_a5_word(-818) = 13;  return;  /* Ctrl-C → CR */
	case 0x09: g_a5_word(-818) = 9;   return;  /* Tab passthrough */
	case 0x1c: g_a5_word(-818) = 262; return;  /* right arrow */
	case 0x1d: g_a5_word(-818) = 258; return;  /* left arrow */
	case 0x1e: g_a5_word(-818) = 264; return;  /* up arrow */
	case 0x1f: g_a5_word(-818) = 260; return;  /* down arrow */
	default:                                       break;
	}

	/* Default: low byte as ASCII (bit 7 cleared). */
	g_a5_word(-818) = (short)(lo & 0x7F);
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
	/* Port concession: on Mac the framebuffer is the screen, so the
	 * "yield to OS" point is implicitly visible. The Falcon HAL
	 * double-buffers (engine paints into the QuickDraw shim buffer;
	 * qd_present blits to VIDEL). Trigger that blit here so the
	 * engine's L2c60 walk (which ends in jt1134) becomes visible
	 * during the jt453 / l2d3e modal loop — before this, the menu
	 * only showed up when ua_main returned and main.c flushed. */
	qd_present();
	elapsed = TickCount() - g_a5_long(-130);
	return (elapsed * 6) / 5;
}
/* JT[1153] (CODE 4 + 0x5d34) — select back-page for rendering.
 *
 *   L4d88();                                     // flush pending InvalRect
 *   short back = arg ? g_a5_-2354                // arg=1: visible page
 *                    : (1 - g_a5_-2354);         // arg=0: off-page (flip)
 *   g_a5_-2352 = back;
 *   page = &g_a5_-2570[back * 108];              // 108-byte descriptor
 *   if (g_a5_-2347)                              // color QD
 *       g_a5_-3076 = ((PixMap *)*(*(PixMapHandle *)(page+2)))->baseAddr;
 *   else                                         // mono GrafPort
 *       g_a5_-3076 = *(long *)(page+2);          // direct ptr field
 *
 * Sets g_a5_-3076 as the "current target framebuffer" pointer the
 * blit primitives (L2c60 paint walker, jt995 row-blit etc.) use as
 * their canvas base. arg=1 keeps the back-buffer index pointed at
 * the visible page — used by direct screen-effect work (menu
 * blink, scroll) so the change is immediately on-screen. arg=0 is
 * the normal flip: render into the off-page, then jt1146 commits.
 *
 * Port-adapted chase: the g_a5_-2570 page descriptors ARE 108-byte
 * CGrafPort structs (the descriptor stride 108 == sizeof CGrafPort,
 * and the chased field at descriptor+2 == CGrafPort.portPixMap).
 * The Mac builds them via NewGWorld during the offscreen-page init
 * at CODE 4 + 0x4ad2, which calls Toolbox traps (0xaa95 etc.) the
 * port doesn't run — so those slots stay uninitialized and the
 * literal chase bus-errors at $fff1fd28.
 *
 * Instead chase the QuickDraw shim's *real* screen CGrafPort, whose
 * portPixMap is a live PixMapHandle the display HAL set up via
 * qd_attach_screen. The deref shape is identical to the Mac's
 * (PixMapHandle -> PixMap -> baseAddr at offset 0); only the source
 * port differs. Both page indices map to the same shim back-buffer
 * — the port doesn't double-buffer at the page-descriptor level;
 * the Falcon HAL's present() performs the actual flip. So
 * g_a5_-3076 lands a valid pixel base regardless of `back`.
 *
 * The mono branch (g_a5_-2347 == 0, descriptor+2 a direct ptr) is
 * unreachable on Falcon VIDEL (always 8bpp color, g_a5_-2347 set);
 * we take the color/PixMapHandle path unconditionally. */
static void jt1153(short arg)
{
	short        back;
	GrafPtr      port;

	PROBE("jt1153");
	l4d88();

	back = (arg != 0)
	     ? g_a5_word(-2354)
	     : (short)(1 - g_a5_word(-2354));
	g_a5_word(-2352) = back;

	/* Faithful PixMapHandle chase against the shim's screen port. */
	port = qd_screen_port();
	if (port != NULL) {
		PixMapHandle pm = ((CGrafPtr)port)->portPixMap;

		if (pm != NULL && *pm != NULL)
			g_a5_long(-3076) =
			    (long)(uintptr_t)(*pm)->baseAddr;
	}
}
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
 * CGrafPort page descriptors from the array at g_a5_-2570 — entry
 * [current] and entry [1 - current], where current = g_a5_-2354 —
 * and memmoves L050a bytes from the off-page's pixel buffer to the
 * on-page's via jt406:
 *
 *   color (g_a5_-2347):
 *     dst = (*(PixMapHandle)(entry_cur + 2))->baseAddr
 *     src = (*(PixMapHandle)(entry_oth + 2))->baseAddr
 *   mono:
 *     dst = *(void **)(entry_cur + 2)        // direct ptr field
 *     src = *(void **)(entry_oth + 2)
 *   jt406(dst, src, L050a());                // BlockMove(dst, src, n)
 *
 * jt406's arg order is (dst, src, count) — BlockMove, opposite of
 * memcpy. The flip copies the off-screen back-buffer onto the
 * visible page.
 *
 * Port adaptation: same situation as jt1153's chase — the
 * g_a5_-2570 descriptors are CGrafPort structs the Mac builds via
 * NewGWorld (offscreen-page init the port never runs), so they're
 * uninitialized here. And critically, the port DOESN'T double-
 * buffer at the page-descriptor level: there is one shim back-
 * buffer, and the Falcon HAL's present() (qd_present) does the
 * real flip via c2p to VIDEL. So both descriptors would resolve
 * to the same shim buffer — the inter-page memmove collapses to a
 * pointless self-copy (src == dst).
 *
 * The faithful-but-adapted lift: skip the no-op self-copy and
 * commit the frame through the HAL present instead. That's exactly
 * what "copy off-page to visible page" means in our single-buffer
 * architecture. The earlier lift's literal descriptor deref both
 * bus-errored (uninitialized) AND stopped one deref short (it
 * parked the PixMap master pointer in dst instead of baseAddr).
 *
 * The previous lift had an unused L050a (page byte count) — kept
 * documented; the present() doesn't need it (the HAL knows the
 * surface geometry). */
static void jt1146(void)
{
	PROBE("jt1146");
	l4d88();

	/* Single-buffer port: off-page == on-page == shim back-buffer,
	 * so the Mac's inter-page memmove is a self-copy. Commit the
	 * frame via the HAL present instead (the real "flip"). */
	qd_present();
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

/* JT[1133] (CODE 4 + 0x6742) — keyboard input read.
 *
 * Two modes — macro replay or live poll:
 *
 *   if (g_a5_-814 != NULL) {                   // macro pending
 *       if (g_a5_-809) {                       // start-of-macro flag
 *           if (*g_a5_-814 == 1) {             // header marker?
 *               g_a5_-814++;                   // skip header
 *               g_a5_-810 = 1;                 // arm replay
 *           } else {
 *               g_a5_-809 = 0;                 // bad header — abort
 *               return 27;                     // ESC
 *           }
 *       }
 *       if (g_a5_-810) {                       // actively replaying
 *           short ch = (signed char)*g_a5_-814;
 *           g_a5_-814++;
 *           if (*g_a5_-814 == 0)
 *               g_a5_-814 = NULL;              // end of buffer
 *           g_a5_-820 = 0;
 *           g_a5_-810 = 0;
 *           if (ch < 32) ch += 256;            // map ctrl chars to F-key range
 *           return ch;
 *       }
 *   }
 *   while (!jt1118())                          // live poll
 *       ;
 *   g_a5_-820 = 0;
 *   return g_a5_-818;                          // resolved key code
 *
 * The macro path replays a recorded byte stream pointed to by
 * g_a5_-814 (set by a "begin macro" hook we haven't lifted). The
 * +256 fold on ctrl chars matches L6dd0's F-key range (256..286),
 * so a macro byte of 0x05 plays as the same key code keypad-5
 * would produce.
 *
 * The live path spins on jt1118 (the "continue polling?" gate) and
 * reads the cached key code L6dd0 / similar arms wrote into
 * g_a5_-818. g_a5_-820 is the "key pending" flag — cleared on
 * consume so the next call blocks until a new key arrives.
 *
 * Widely called: CODE 3, 5, 6, 11, 12 menu input loops + L31f0
 * (the JT[439] passthrough). */
static short jt1133(void) __attribute__((unused));
static short jt1133(void)
{
	unsigned char *p;
	short          ch;

	PROBE("jt1133");

	if (g_a5_long(-814) != 0) {
		if (g_a5_byte(-809) != 0) {
			p = (unsigned char *)(uintptr_t)g_a5_long(-814);
			if (*p == 1) {
				g_a5_long(-814) = (long)(uintptr_t)(p + 1);
				g_a5_byte(-810) = 1;
			} else {
				g_a5_byte(-809) = 0;
				return (short)27;
			}
		}
		if (g_a5_byte(-810) != 0) {
			p = (unsigned char *)(uintptr_t)g_a5_long(-814);
			ch = (short)(signed char)*p;
			g_a5_long(-814) = (long)(uintptr_t)(p + 1);
			p = (unsigned char *)(uintptr_t)g_a5_long(-814);
			if (*p == 0)
				g_a5_long(-814) = 0;
			g_a5_byte(-820) = 0;
			g_a5_byte(-810) = 0;
			if (ch < (short)32)
				ch = (short)(ch + 256);
			return ch;
		}
	}

	while (jt1118() == 0)
		;
	g_a5_byte(-820) = 0;
	return g_a5_word(-818);
}

/* L31f0 (CODE 3 + 0x31f0) = JT[439] — JT[1133] tail call. */
static short l31f0(void) __attribute__((unused));
static short l31f0(void)
{
	PROBE("L31f0");
	return jt1133();
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

/* JT[1131] (CODE 4 + 0x7760) — semitone -> menu-slot dispatcher.
 *
 *   short tmp   = g_a5_shorts(-804)[midi % 12];   // 12-entry octave row
 *   short shift = midi / 12 - 5;                  // octave 5 is base
 *   if (shift < 0)      tmp = (short)(tmp << -shift);
 *   else if (shift > 0) tmp = (short)(tmp >>  shift);
 *   jt1122(snd_id, tmp, dur);                     // routes to L7690
 *
 * The table at g_a5_-804 is a 12-entry short[] (one octave). The
 * shift retunes to the requested octave: right-shift for higher
 * MIDI = smaller value, so the entries are *periods* (jt5/JT[1122]
 * hash divisors), not frequencies — consistent with jt1122's
 * "slot_val ? jt5(0x233244F7u, slot_val) : 0" hash.
 *
 * Six CODE 5 call sites (0x135a / 0x13a4 / 0x1478 / 0x14ac /
 * 0x14c8 / 0x1524) all pass (char_byte, midi=0, dur=-1) — the
 * "tag this menu slot with the character's hash" path used by the
 * party-roster screens. With midi=0, shift = -5, so tmp is the
 * table[0] period left-shifted by 5 before being hashed.
 *
 * dur is passed through and ignored by jt1122 (the Mac kept the
 * prototype symmetric with sibling sound helpers).
 */
static void jt1131(short snd_id, short midi, short dur) __attribute__((unused));
static void jt1131(short snd_id, short midi, short dur)
{
	short tmp;
	short shift;

	PROBE("jt1131");

	tmp   = g_a5_shorts(-804)[midi % 12];
	shift = (short)(midi / 12 - 5);
	if (shift < 0)
		tmp = (short)(tmp << (-shift));
	else if (shift > 0)
		tmp = (short)(tmp >> shift);

	jt1122(snd_id, tmp, dur);
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
	l2c60((short)0);                /* walk dirty items with cmd=1 */
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
	short hit;

	PROBE("jt453");
	if (g_a5_9248 == 0) {
		PROBE("jt453: not in dialog");
		return (short)-1;
	}
	if (g_a5_9247 == 0) {
		l30ba(0, (short)(g_a5_9250 - 1), 0);
		g_a5_9247 = 1;
	}

	/* Mac: spin on l2d3e until an item is selected (>= 0). The
	 * inner l2d3e call pumps the event queue every iteration via
	 * jt1118 / L731e / L725c so a host keystroke or click does
	 * land in g_a5_-818 / -908 within a few iterations. The earlier
	 * 30-iter guard was a defense against L2d3e returning -1 forever
	 * (PROBE-only DLItem methods); with the L1676 cmd=5 path lifted
	 * and the IKBD chain live, l2d3e resolves real input. */
	for (;;) {
		hit = l2d3e();
		if (hit >= 0)
			return hit;
		if (filterProc != NULL) {
			hit = filterProc();
			if (hit >= 0)
				return hit;
		}
	}
}

/* jt313 (CODE 22 + 0x4d3c) — the main menu's item event-filter PROC,
 * stored in the shape-7 DLItem (rec+4) jt315 builds. The Mac body polls
 * the IKBD (jt1118) and maps the arrow keys 338/339 to scroll (jt50/jt51);
 * a return of 0 means "not consumed". Minimal for now — the menu items are
 * keyboard/click-selectable through l2d3e directly. */
static short   jt313(void *rec, short cmd, ...)
{
	PROBE("jt313");
	(void)rec; (void)cmd;
	return 0;
}

/* The ten main-menu commands, two columns (left x=8004, right x=8084),
 * straight from jt315's jt452 build (CODE 22 + 0x4dde..0x4fe8): label,
 * x, y (8000-anchored), and the selector hotkey letter. The two disabled
 * spacer rows at y=8087 are omitted. */
/* A menu command (or a label-less spacer plate). The reusable menu runner
 * (menu_run) builds a DLItem per entry with a label, draws a bevelled plate
 * per entry, and dispatches on the selected index. Every menu in the game
 * shares this format, so new menus are just a menu_item_t[] + an optional
 * decorate callback (title plate + banner).
 *
 * `recessed` = 1 draws the plate sunken (disabled commands / spacers /
 * title) vs raised (active). Wire to the real enable state (jt158 /
 * rec[28]) once the sub-menus are lifted; hard-coded to boot-state now. */
typedef struct {
	const char *label;        /* NULL = spacer: plate only, no DLItem */
	short       x, y, hotkey;
	int         recessed;
} menu_item_t;

static const menu_item_t g_mainmenu[] = {
	{ "Play the Game",     8004, 8059, 'P', 0 },
	{ "Select a Design",   8004, 8066, 'S', 0 },
	{ "Create New Design", 8004, 8073, 'C', 0 },
	{ "Delete the Design", 8004, 8080, 'D', 1 },
	{ "Unlock Editor",     8004, 8094, 'U', 1 },
	{ "Game Settings",     8084, 8059, 'G', 0 },
	{ "Edit Modules",      8084, 8066, 'E', 0 },
	{ "Art Gallery",       8084, 8073, 'A', 0 },
	{ "Monster Editor",    8084, 8080, 'M', 0 },
	{ "Quit From Game",    8084, 8094, 'Q', 0 },
	{ NULL,                8004, 8087,  0,  1 },   /* spacer (left)  */
	{ NULL,                8084, 8087,  0,  1 },   /* spacer (right) */
};

/* Main-menu UI assets, assembled from two GLIBs:
 *
 *   MENU.CTL item 0 — the 256-entry base UI palette (11 = cyan headings,
 *                     15 = white, 7 = light grey text).
 *   FRAME.CTL item 0 — a 16-colour "band" of warm greys installed over
 *                     clut 16..31 (the UI stone ramp: 16 = light 167,
 *                     23 = 91,83,79 the plate face, 31 = near-black). The
 *                     FRAME tiles + plates index into this band.
 *   GEN.CTL item 1   — a 320x90 PackBits-8bpp dark stone texture; the menu
 *                     backdrop. The Mac's jt81 (CODE 6 + 0x6a10) loads the
 *                     "gen" asset and blits it in sections via JT[1001] ->
 *                     l309c to fill the screen, so the field is the GEN
 *                     stone, NOT a frame molding strip. (An earlier cut
 *                     tiled FRAME.CTL item 4 — a divider molding with a
 *                     baked-in 3D bevel highlight — which left a stray
 *                     light line across the gaps; GEN is the real field.)
 *
 * Backdrop pixels index clut 16..31 too, so they render through the same
 * FRAME band — a warm dark stone, matching data/frua_mac_menu.png. */
static unsigned char g_menu_file[16384];
static unsigned char g_frame_file[40960];    /* FRAME.CTL, kept resident    */
static unsigned char g_gen_file[28672];      /* GEN.CTL, kept resident      */
static long          g_frame_base, g_gen_base;  /* GLIB bases for ui_glib_blit */
static unsigned char g_bg[320 * 90];         /* decoded GEN backdrop image  */
static short         g_bg_w, g_bg_h, g_bg_ybear;
static int           g_menu_state;           /* 0 untried, 1 ok, -1 failed */
static RGBColor      g_menu_pal[256];
static short         g_menu_pe;

/* Decode a PackBits-RLE run into dst (cap bytes). Returns bytes written. */
static long unpackbits(const unsigned char *src, long srclen,
                       unsigned char *dst, long cap)
{
	long o = 0, si = 0;
	while (si < srclen && o < cap) {
		signed char c = (signed char)src[si++];
		if (c >= 0) {                         /* c+1 literals */
			short n = (short)(c + 1);
			while (n-- > 0 && si < srclen && o < cap)
				dst[o++] = src[si++];
		} else if (c != -128) {               /* repeat next byte 1-c times */
			short n = (short)(1 - c);
			unsigned char v = src[si++];
			while (n-- > 0 && o < cap)
				dst[o++] = v;
		}
	}
	return o;
}

static void load_menu_ui(void)
{
	if (g_menu_state == 0) {
		short refnum = 0;
		long  flen, base, p0;

		g_menu_state = -1;

		/* --- MENU.CTL item 0: the 256-entry base palette --- */
		if (FSOpen((ConstStr255Param)"\010MENU.CTL", 0, &refnum) == noErr) {
			long p1;
			flen = (long)sizeof g_menu_file;
			(void)FSRead(refnum, &flen, g_menu_file);
			(void)FSClose(refnum);
			base = (long)(uintptr_t)g_menu_file;
			if ((p0 = l37aa(base, 0)) != 0
			 && (p1 = l37aa(base, 1)) != 0 && p1 > p0) {
				const unsigned char *pp =
				    (const unsigned char *)(uintptr_t)(p0 + 8);
				short pe = (short)((p1 - p0 - 8) / 3), k;
				if (pe > 256) pe = 256;
				for (k = 0; k < pe; k++) {
					g_menu_pal[k].red   =
					    (unsigned short)((pp[k*3+0] << 8) | pp[k*3+0]);
					g_menu_pal[k].green =
					    (unsigned short)((pp[k*3+1] << 8) | pp[k*3+1]);
					g_menu_pal[k].blue  =
					    (unsigned short)((pp[k*3+2] << 8) | pp[k*3+2]);
				}
				g_menu_pe = pe;
			}
		}

		/* --- FRAME.CTL item 0: the warm-grey band over clut 16..31 --- */
		refnum = 0;
		if (g_menu_pe > 0
		 && FSOpen((ConstStr255Param)"\011FRAME.CTL", 0, &refnum) == noErr) {
			long pb;
			flen = (long)sizeof g_frame_file;
			(void)FSRead(refnum, &flen, g_frame_file);
			(void)FSClose(refnum);
			base = (long)(uintptr_t)g_frame_file;
			g_frame_base = base;                  /* keep FRAME.CTL resident */
			pb = l37aa(base, 0);                  /* 16-colour band */
			if (pb != 0) {
				const unsigned char *bd =
				    (const unsigned char *)(uintptr_t)(pb + 8);
				short k;
				for (k = 0; k < 16; k++) {
					g_menu_pal[16 + k].red   =
					    (unsigned short)((bd[k*3+0] << 8) | bd[k*3+0]);
					g_menu_pal[16 + k].green =
					    (unsigned short)((bd[k*3+1] << 8) | bd[k*3+1]);
					g_menu_pal[16 + k].blue  =
					    (unsigned short)((bd[k*3+2] << 8) | bd[k*3+2]);
				}
			}
		}

		/* --- GEN.CTL item 1: the stone backdrop image (jt81's "gen") --- */
		refnum = 0;
		if (g_menu_pe > 0
		 && FSOpen((ConstStr255Param)"\007GEN.CTL", 0, &refnum) == noErr) {
			long p1;
			flen = (long)sizeof g_gen_file;
			(void)FSRead(refnum, &flen, g_gen_file);
			(void)FSClose(refnum);
			base = (long)(uintptr_t)g_gen_file;
			g_gen_base = base;                    /* keep GEN.CTL resident   */
			p1 = l37aa(base, 1);                  /* 320x90 PackBits image */
			if (p1 != 0) {
				const unsigned char *it =
				    (const unsigned char *)(uintptr_t)p1;
				short h = (short)(((unsigned)it[0] << 8) | it[1]);
				short w = (short)(8 * it[6]);
				long  srclen = flen - (p1 - base) - 8;
				long  got = unpackbits(it + 8, srclen, g_bg,
				                       (long)sizeof g_bg);
				if (w > 0 && h > 0 && got >= (long)w * h) {
					g_bg_w = w;
					g_bg_h = h;
					g_bg_ybear =
					    (short)(((unsigned short)it[2] << 8) | it[3]);
					g_menu_state = 1;
				}
			}
		}
	}
	if (g_menu_state == 1)               /* install the UI palette */
		qd_set_palette(g_menu_pal, (short)0, g_menu_pe);
}

/* ui_glib_blit — the faithful l309c (CODE 5 + 0x309c) for the chunky UI
 * surface. Draws sub-image `idx` of GLIB `handle` at engine coords
 * (top,left):
 *
 *   1. jt1135 remaps (top,left) to screen pixels (idempotent on pixel-
 *      range values, so the composite recursion below is safe);
 *   2. l2856 fetches the 8-byte metric + the bits pointer;
 *   3. the (ybear,xbear) bearing shifts the draw origin;
 *   4. composite arm (metric[7] & 0x0F == 9): the body is a list of
 *      6-byte {sub_idx, count, dy, dx} records — recurse for each piece at
 *      its offset (this is the frame placement data);
 *   5. else: blit the leaf 8bpp image — PackBits (flag nibble 2) decoded
 *      first, raw (0/5) copied directly. `transparent` controls index 0:
 *      skip it (molding over the field) vs copy it (the opaque field).
 *
 * Targets qd_screen_pixels (the chunky 8bpp surface) rather than the Mac
 * page descriptor; the l2d4e leaf's planar/scaled/clip-global arms are
 * collapsed to a direct chunky copy clipped to the surface. Reusable for
 * every UI GLIB image (frame molding, Art Gallery, portraits, …). */
static unsigned char g_glib_dec[320 * 96];   /* PackBits decode scratch */

/* `flip` vertically mirrors the leaf (source row h-1-row) — not a Mac blit
 * feature, but it lets the backdrop tiler mirror alternate copies so the
 * 90-row repeat boundaries match (seamless), since GEN.CTL ships only the
 * one section and the screen is taller. */
static void ui_glib_blit(long handle, short idx, short top, short left,
                         int transparent, int flip) __attribute__((unused));
static void ui_glib_blit(long handle, short idx, short top, short left,
                         int transparent, int flip)
{
	unsigned char metric[8];
	long          info;
	short         y = top, x = left, ybear, xbear, w, h;
	unsigned char flags;
	unsigned char *px;
	short          pitch, sw, sh, row, col;
	const unsigned char *src;

	if (handle == 0)
		return;
	jt1135(top, left, &y, &x);
	info = l2856(handle, idx, metric);
	if (info == 0)
		return;
	ybear = (short)(((unsigned short)metric[2] << 8) | metric[3]);
	xbear = (short)(((unsigned short)metric[4] << 8) | metric[5]);
	y = (short)(y - ybear);
	x = (short)(x - xbear);

	if ((metric[7] & 0x0f) == 9) {           /* composite sub-part list */
		const unsigned char *rec = (const unsigned char *)(uintptr_t)info;
		short count = 1, i;
		for (i = 0; i < count; i++) {
			short sub = rec[0];
			short cnt = rec[1];
			short dy  = (short)(((unsigned short)rec[2] << 8) | rec[3]);
			short dx  = (short)(((unsigned short)rec[4] << 8) | rec[5]);
			rec += 6;
			ui_glib_blit(handle, sub, (short)(y + dy), (short)(x + dx),
			             transparent, 0);
			count = cnt;
		}
		return;
	}

	h     = (short)(((unsigned short)metric[0] << 8) | metric[1]);
	w     = (short)(metric[6] * 8);
	flags = metric[7];
	if (h <= 0 || w <= 0)
		return;
	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == NULL)
		return;

	if ((flags & 0x0f) == 2) {               /* PackBits 8bpp */
		long cap = (long)w * h;
		if (cap > (long)sizeof g_glib_dec)
			return;
		(void)unpackbits((const unsigned char *)(uintptr_t)info,
		                 (long)sizeof g_glib_dec, g_glib_dec, cap);
		src = g_glib_dec;
	} else {                                 /* raw 8bpp chunky (0 / 5) */
		src = (const unsigned char *)(uintptr_t)info;
	}

	for (row = 0; row < h; row++) {
		short dy = (short)(y + row);
		short srow = flip ? (short)(h - 1 - row) : row;
		const unsigned char *s = src + (long)srow * w;
		unsigned char *d;
		if (dy < 0 || dy >= sh)
			continue;
		d = px + (long)dy * pitch;
		for (col = 0; col < w; col++) {
			short dx = (short)(x + col);
			unsigned char v = s[col];
			if (transparent && v == 0)   /* index 0 = transparent */
				continue;
			if (dx < 0 || dx >= sw)
				continue;
			d[dx] = v;
		}
	}
}

/* --- main-menu chrome: dark stone surround + raised lighter plates ---
 *
 * data/frua_mac_menu.png is a dark, textured stone *background* with the
 * title block and each command sitting on a raised, smoother, lighter
 * stone *plate* (bg lum ~70, plates ~95-100). The faithful art is in
 * FRAME.TLB — a mask + edge + corner compositing system whose blit
 * (l309c / jt1001) is still a PROBE stub, so reproducing it means
 * reversing both its pixel encoding and the Mac's piece-placement logic
 * (a multi-step RE). Until then the port renders the same look directly:
 * a darkened stone backdrop with flat lighter bevelled plates.
 *
 * Palette: stone shades run clut 16 (light, lum 196) .. 31 (dark, 57);
 * plate fill is the flat mid grey clut 8 (103); bevel uses clut 7 (light)
 * + clut 0 (dark). */
#define MENU_PLATE_FILL   23    /* clut 23 = 91,83,79  warm plate face   */
#define MENU_BEVEL_LIGHT  16    /* clut 16 = 167,167,167  band highlight */
#define MENU_BEVEL_DARK   31    /* clut 31 = 27,27,39     band shadow    */

/* Tile the GEN.CTL stone field across the screen as the backdrop.
 *
 * GEN item 1 (decoded into g_bg) is one bearing-placed section of the Mac's
 * "gen" backdrop; rows 0..1 are a bright panel-top highlight + transition
 * (mean clut ~17 / ~21 vs the ~27 dark interior) meant for where the
 * section sits at its bearing. Tiling the whole section repeats those at
 * every boundary as stray light bars, so we tile only the dark interior
 * (rows 2..h-1). A straight repeat there is essentially seamless: the
 * row(h-1)->row2 wrap differs by ~3, the same as adjacent interior rows
 * (~2), and unlike mirroring it doesn't reverse the diagonal grain (which
 * read as a chevron at the fold). The lifted ui_glib_blit draws whole
 * images; this is a tuned interior tile, since gen's bright edge isn't part
 * of the tileable field. */
static void fill_backdrop(unsigned char *px, short pitch,
                          short x0, short y0, short x1, short y1)
{
	short x, y;
	short T = (short)(g_bg_h - 2);       /* interior height (skip rows 0,1) */

	if (g_menu_state != 1 || T <= 0)
		return;
	for (y = y0; y <= y1; y++) {
		const unsigned char *row = g_bg + (long)(2 + y % T) * g_bg_w;
		unsigned char *d = px + (long)y * pitch;
		for (x = x0; x <= x1; x++)
			d[x] = (x < g_bg_w) ? row[x] : row[g_bg_w - 1];
	}
}

/* A bevelled plate over the backdrop: flat warm-grey face + a 1px bevel.
 * `recessed` flips the bevel — raised = highlight top/left, shadow
 * bottom/right (active buttons); recessed = highlight bottom/right, shadow
 * top/left (the title block + disabled/empty boxes). */
static void draw_plate(unsigned char *px, short pitch, short sw, short sh,
                       short x0, short y0, short x1, short y1, int recessed)
{
	unsigned char tl = recessed ? MENU_BEVEL_DARK  : MENU_BEVEL_LIGHT;
	unsigned char br = recessed ? MENU_BEVEL_LIGHT : MENU_BEVEL_DARK;
	short x, y;

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > sw - 1) x1 = (short)(sw - 1);
	if (y1 > sh - 1) y1 = (short)(sh - 1);
	if (x1 <= x0 || y1 <= y0)
		return;

	for (y = y0; y <= y1; y++)
		memset(px + (long)y * pitch + x0, MENU_PLATE_FILL,
		       (size_t)(x1 - x0 + 1));
	for (x = x0; x <= x1; x++) {
		px[(long)y0 * pitch + x] = tl;
		px[(long)y1 * pitch + x] = br;
	}
	for (y = y0; y <= y1; y++) {
		px[(long)y * pitch + x0] = tl;
		px[(long)y * pitch + x1] = br;
	}
}

/* Draw a bevelled plate behind every entry of a menu spec (commands and
 * label-less spacers alike). Geometry comes from each entry's engine
 * coords via jt1135 — the same transform jt382 uses for the label — so the
 * plates track the labels. Active = raised, recessed = sunken (per the
 * spec). Called before l2c60 so the labels land on top. */
static void menu_draw_plates(const menu_item_t *items, short n)
{
	unsigned char *px;
	short pitch, sw, sh, i, py = 0, pxx = 0;

	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == NULL)
		return;

	/* Plate box per row. Rows are 14px apart (engine y step 7, scale 2).
	 * The 8px label sits at py-6..py+1, so a py-7..py+2 box (10px) centres
	 * it with a 1px margin top and bottom, and leaves a ~4px gap between
	 * rows — matching the spaced, centred buttons in the reference. */
	for (i = 0; i < n; i++) {
		jt1135(items[i].y, items[i].x, &py, &pxx);
		draw_plate(px, pitch, sw, sh,
		           (short)(pxx - 5), (short)(py - 7),
		           (short)(pxx - 5 + 150), (short)(py + 2),
		           items[i].recessed);
	}
}

/* menu_run — the reusable DLItem menu runner shared by every menu screen.
 *
 * Restores the menu display state (jt1135 scale 2 + the UI palette), paints
 * the shared chrome (stone backdrop + a bevelled plate per spec entry),
 * builds the DLItem group from the spec (jt447 + jt452), paints the labels
 * (l2c60), presents (jt117), then blocks in the dialog event loop (jt453)
 * and returns the selected index (jt452 order over the labelled entries).
 *
 *   items / n  — the menu spec; entries with a label get a DLItem + plate,
 *                label-less entries get a recessed spacer plate only.
 *   proc       — the shape-7 action PROC for this menu (may be NULL).
 *   decorate   — menu-specific chrome (may be NULL): phase 0 is called over
 *                the backdrop (e.g. a title plate), phase 1 after l2c60 (the
 *                banner text). One callback, two phases, so a menu's extra
 *                art and text stay together.
 *
 * New menus are a menu_item_t[] + a proc + an optional decorate — no copy
 * of the build/paint/loop boilerplate. */
static short menu_run(const menu_item_t *items, short n, void *proc,
                      void (*decorate)(unsigned char *, short, short, short, int))
{
	short i;

	g_a5_2347 = 1;                       /* non-encounter: jt1135 scale 2 */
	load_menu_ui();

	/* backdrop + menu-specific chrome (phase 0), then prime the present */
	{
		unsigned char *px; short pitch, sw, sh, yy;
		if (qd_screen_pixels(&px, &pitch, &sw, &sh) && px) {
			if (g_menu_state == 1) {
				fill_backdrop(px, pitch, 0, 0,
				              (short)(sw - 1), (short)(sh - 1));
				if (decorate) decorate(px, pitch, sw, sh, 0);
			} else {
				for (yy = 0; yy < sh; yy++)
					memset(px + (long)yy * pitch, 0x08, (size_t)sw);
			}
			qd_present();
		}
	}

	/* build the DLItem group from the spec */
	jt131((short)6);
	jt112((short)1);
	jt81();
	jt447();
	for (i = 0; i < n; i++)
		if (items[i].label != NULL)
			jt452((long)1, (long)items[i].y, (long)items[i].x,
			      (long)(uintptr_t)items[i].label,
			      (long)32, (long)items[i].hotkey,
			      (long)36, (long)18, (long)20, (long)21, (long)0);
	if (proc != NULL)
		jt452((long)7, (long)(uintptr_t)proc, (long)20, (long)0);

	/* plates under the labels, then paint the labels */
	menu_draw_plates(items, n);
	l2c60((short)1);

	/* menu-specific banner (phase 1) on top of the labels */
	if (decorate) {
		unsigned char *px; short pitch, sw, sh;
		if (qd_screen_pixels(&px, &pitch, &sw, &sh))
			decorate(px, pitch, sw, sh, 1);
	}

	jt112((short)0);
	jt117();
	qd_present();

	return jt453((jt453_filter_t)0);
}

/* The main menu's decorate callback: the recessed title plate (phase 0) +
 * the five-line banner (phase 1). Faithful to CODE 22 + 0x506e..0x50ee:
 * "Unlimited Adventures" (row 3, col 11 cyan), the version/build line
 * (row 4, col 7 grey), "Current Game Design:" (row 9, col 11) + the design
 * name + module title (col 7). The Mac sources the title/version from A5
 * -13948/-13944, which our DATA replay leaves holding a GEO template, so
 * those two lines are drawn literally; the design/title come from their A5
 * slots (populated by the play path). */
static void jt315_decorate(unsigned char *px, short pitch, short sw, short sh,
                           int phase)
{
	if (phase == 0) {
		draw_plate(px, pitch, sw, sh, 6, 6, 313, 96, 1);  /* title plate */
		return;
	}
	{
		const char *design = (const char *)g_a5_buf(-31336);
		const char *title  = (const char *)g_a5_buf(-18876);
		jt94((short)8, (short)3, (short)11, (short)0, "Unlimited Adventures");
		/* Port version + build date. The Mac drew one A5 string here; we
		 * draw two literals (version left, date a few cols right) so the
		 * gap is tight, and use __DATE__ so the build date tracks the
		 * actual build. Edit "Version 0.1" to bump the port version. */
		jt94((short)4,  (short)4, (short)7, (short)0, "Version 0.1");
		jt94((short)18, (short)4, (short)7, (short)0, "%s", __DATE__);
		jt94((short)4, (short)9, (short)11, (short)0, "Current Game Design:");
		if (design[0]) jt94((short)25, (short)9, (short)7, (short)0, "%s", design);
		if (title[0])  jt94((short)4, (short)10, (short)7, (short)0, "%s", title);
	}
}

/* --- the menu-stack pattern: a sub-menu is a C function that runs its own
 * menu_run loop and returns to the caller's dispatch. menu_todo() is the
 * Phase 1 placeholder sub-screen: it proves the pattern end-to-end (a main
 * command opens a sub-screen with its own chrome, and "Exit" backs out)
 * while each command's faithful content is lifted per Phase 2. */
static const char *g_submenu_title;

static void submenu_decorate(unsigned char *px, short pitch, short sw, short sh,
                             int phase)
{
	if (phase == 0) {
		draw_plate(px, pitch, sw, sh, 6, 6, 313, 40, 1);  /* header plate */
		return;
	}
	jt94((short)8, (short)3, (short)11, (short)0, "%s", g_submenu_title);
	jt94((short)8, (short)6, (short)7,  (short)0, "Not yet implemented");
}

static void menu_todo(const char *title)
{
	static const menu_item_t items[] = {
		{ "Exit", 8056, 8080, 'E', 0 },
	};
	g_submenu_title = title;
	/* one item -> any selection (the Exit button / its hotkey) returns. */
	(void)menu_run(items, 1, (void *)(uintptr_t)jt313, submenu_decorate);
}

/* jt315 (CODE 22 + 0x4d8a) — the main menu screen + event loop, on the
 * shared menu_run. Returns 1 to keep ua_main's play loop running, 0 on
 * "Quit From Game". The dispatch now routes every command to a handler
 * (the menu-stack pattern); the design / settings / editor sub-menus open
 * the menu_todo placeholder until their faithful content is lifted
 * (Phase 2 — JT entries noted, see docs/menu-wiring-plan.md). */
static int   jt315(void)
{
	PROBE("jt315");

	for (;;) {
		short hit = menu_run(g_mainmenu,
		    (short)(sizeof g_mainmenu / sizeof g_mainmenu[0]),
		    (void *)(uintptr_t)jt313, jt315_decorate);
		switch (hit) {
		case 0:                          /* Play the Game -> Training Hall */
			return 1;
		case 1: menu_todo("Select a Design");   break;  /* CODE 8 JT[361/369] */
		case 2: menu_todo("Create New Design"); break;  /* CODE 8 */
		case 3: menu_todo("Delete the Design"); break;  /* CODE 8 */
		case 4: break;                   /* Unlock Editor — disabled */
		case 5: menu_todo("Game Settings");     break;  /* JT[247] CODE 2 */
		case 6: menu_todo("Edit Modules");      break;  /* CODE 12 editor */
		case 7: menu_todo("Art Gallery");       break;  /* JT[1080] CODE 5 */
		case 8: menu_todo("Monster Editor");    break;  /* CODE 12 editor */
		case 9:                          /* Quit From Game */
			return 0;
		default:
			break;
		}
	}
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
		/* Mac stream format:
		 *   cmd 1                   ; install shape 1 (text button via jt382)
		 *   short top  = phrase     ; rec[16]
		 *   short left = page       ; rec[18]
		 *   long  str  = label      ; rec[12]
		 *   cmd 32, short selector  ; rec[29] = accelerator
		 *   cmd 20                  ; set bit 4 of rec[28] (enabled)
		 *   cmd 21                  ; set bit 5 of rec[28] (visible)
		 *   cmd 0                   ; end-of-stream
		 *
		 * Per the prior simplified parser's documented field map:
		 * phr -> rec[16] (top), page -> rec[18] (left). */
		jt452(1L,
		      (long)k_jt918_menu_items[i].phrase,
		      (long)k_jt918_menu_items[i].page,
		      (long)(uintptr_t)ua_strs_at(
		          k_jt918_menu_items[i].label_strs_off),
		      32L, (long)k_jt918_menu_items[i].selector,
		      20L, 21L, 0L);
	}
	/* The extra "page switch" item the Mac appends past the 12.
	 * Shape 7 = callback DLItem; arg is the callback pointer
	 * (passed as 20 here just as a non-NULL marker). End with 0. */
	jt452(7L, 20L, 0L);

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

		/* Shared chrome: a bevelled plate behind each command, drawn (like
		 * the main menu) before the labels. recessed = disabled (flag 0),
		 * so greyed commands render sunken — matching jt315's look. */
		{
			menu_item_t ti[12];
			for (i = 0; i < 12; i++) {
				ti[i].label    = "";
				ti[i].x        = k_jt918_menu_items[i].page;
				ti[i].y        = k_jt918_menu_items[i].phrase;
				ti[i].hotkey   = k_jt918_menu_items[i].selector;
				ti[i].recessed = (*flags[i] == 0);
			}
			menu_draw_plates(ti, 12);
		}
	}

	l2c60(1);                            /* real DLItem paint walker (jt449 is a stub) */
	(void)jt112(0);
	(void)jt117();
	qd_present();                        /* c2p the QD port to VIDEL + flip */

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
/* L35f8 (CODE 17 + 0x35f8) — draw the four character-creation PICK headers
 * (race / alignment / gender / class) via the jt1089 text path, at their
 * 8000-anchored screen coords. g_a5_-7016 is the text colour the screen
 * init (L3666) sets per display mode. */
static void l35f8(void)
{
	short col = g_a5_word(-7016);

	PROBE("L35f8");
	jt76();
	jt1089((short)8006, (short)8006, col, "PICK RACE");
	jt1089((short)8040, (short)8006, col, "PICK ALIGNMENT");
	jt1089((short)8076, (short)8006, col, "PICK GENDER");
	jt1089((short)8012, (short)8068, col, "PICK CLASS");
}

/* L3666 (CODE 17 + 0x3666) — character-creation screen init + header draw.
 * Sets the window dims for the display mode, paints the PICK headers, and
 * seeds the wizard state (step g_a5_-7018). The FULL Mac body then rolls
 * ability scores (the L34f0 loop over the race/class tables at g_a5_-30450)
 * and runs the pick state machine — that is the large multi-session
 * remainder of the CODE 17 lift and stays TODO. */
static void l3666(void)
{
	PROBE("L3666");
	g_a5_byte(-7038) = 0;
	if (jt1200() == 3) {                 /* deep mode */
		g_a5_word(-7000) = 15;
		g_a5_word(-7016) = 15;
	} else {
		g_a5_word(-7000) = 135;
		g_a5_word(-7016) = 140;
	}
	l35f8();
	jt117();
	g_a5_word(-7026) = 6;
	g_a5_word(-7024) = 1;
	g_a5_word(-7022) = 1;
	g_a5_word(-7020) = 1;
	g_a5_word(-7018) = 3;                 /* wizard step */
	/* TODO: ability-score roll (L34f0) + the per-step pick state machine
	 * (jt568 dispatcher, race/class/gender/alignment selection). */
}

/* Char-gen option lists under the PICK headers. PORT ADDITION: the Mac
 * draws these through the jt568 selection state machine (CODE 17) which
 * isn't lifted yet; this lists the choices so the screen reads as the real
 * char-gen, under each header's column (RACE top-left, ALIGNMENT top-mid,
 * GENDER top-right — matching l35f8's header coords). CLASS is race-
 * dependent (appears after the race pick) and is deferred with the
 * selection machine. Options in light grey (col 7) under the white (15)
 * headers. */
#define CG_NRACES   6
#define CG_NGENDERS 2
#define CG_NCLASSES 6
static const char *const cg_races[CG_NRACES] = {
	"Human", "Elf", "Half-Elf", "Dwarf", "Gnome", "Halfling",
};
static const char *const cg_genders[CG_NGENDERS] = { "Male", "Female" };
static const char *const cg_classes[CG_NCLASSES] = {
	"Cleric", "Fighter", "Magic-User", "Thief", "Paladin", "Ranger",
};

/* Which single classes each race may be (bit i = cg_classes[i] allowed) —
 * the AD&D-1e race/class table FRUA follows. The faithful source is the
 * game's own table at g_a5_-30450 (read by jt568); this hard-codes the
 * single-class allowances (multi-class combos are the deferred remainder)
 * so the class list is correctly gated by the picked race. */
static const unsigned char cg_race_classes[CG_NRACES] = {
	0x3F,   /* Human:    Cleric Fighter Mage Thief Paladin Ranger */
	0x2F,   /* Elf:      Cleric Fighter Mage Thief Ranger          */
	0x2F,   /* Half-Elf: Cleric Fighter Mage Thief Ranger          */
	0x0B,   /* Dwarf:    Cleric Fighter Thief                      */
	0x0B,   /* Gnome:    Cleric Fighter Thief                      */
	0x0A,   /* Halfling: Fighter Thief                             */
};

/* Build the allowed-class index list for `race`; returns the count. */
static short cg_allowed_classes(short race, short *out)
{
	short i, n = 0;
	unsigned char mask = cg_race_classes[race];
	for (i = 0; i < CG_NCLASSES; i++)
		if (mask & (1u << i))
			out[n++] = i;
	return n;
}

/* The six ability scores, in record order. */
static const char *const cg_stat_names[6] = {
	"STR", "INT", "WIS", "DEX", "CON", "CHA",
};

/* Per-race ability adjustments (the AD&D-1e mods FRUA applies; the game's
 * own values live in the table at g_a5_-30450). */
static const signed char cg_race_adj[CG_NRACES][6] = {
	/* STR INT WIS DEX CON CHA */
	{   0,  0,  0,  0,  0,  0 },   /* Human    */
	{   0,  0,  0, +1, -1,  0 },   /* Elf      */
	{   0,  0,  0,  0,  0,  0 },   /* Half-Elf */
	{   0,  0,  0,  0, +1, -1 },   /* Dwarf    */
	{   0,  0,  0,  0,  0,  0 },   /* Gnome    */
	{  -1,  0,  0, +1,  0,  0 },   /* Halfling */
};

extern short ua_rand(short n);   /* CODE 5 / JT[1083] LCG, 0..n-1 */

/* Roll the six ability scores: 3d6 each (the FRUA LCG), + the race
 * adjustment, clamped to 3..18. Class-minimum re-rolls (Fighter STR>=9,
 * Mage INT>=9, Paladin's full set, …) are the deferred refinement. */
static void cg_roll_stats(short race, short *stats)
{
	short i;
	for (i = 0; i < 6; i++) {
		short v = (short)((ua_rand(6) + 1) + (ua_rand(6) + 1)
		                + (ua_rand(6) + 1));
		v = (short)(v + cg_race_adj[race][i]);
		if (v < 3)  v = 3;
		if (v > 18) v = 18;
		stats[i] = v;
	}
}

/* Draw the char-gen pick lists. `step` is the active pick (0 race, 1
 * gender, 2 class); each region shows its current choice in cyan once it's
 * been reached, the rest light grey. The class list shows only the picked
 * race's allowed classes (gated). */
static void cg_draw(short race, short gender, short ksel, short step,
                    const short *allowed, short nallowed, const short *stats)
{
	static const char *const aligns[] = {
		"Lawful Good", "Lawful Neut", "Lawful Evil",
		"Neutral Good", "Neutral", "Neutral Evil",
		"Chaotic Good", "Chaotic Neut", "Chaotic Evil",
	};
	short k;

	for (k = 0; k < CG_NRACES; k++)
		jt1089((short)8006, (short)(8010 + 3 * k),
		       (short)(k == race ? 11 : 7), "%s", cg_races[k]);
	for (k = 0; k < (short)(sizeof aligns / sizeof aligns[0]); k++)
		jt1089((short)8040, (short)(8010 + 3 * k), (short)7, "%s", aligns[k]);
	for (k = 0; k < CG_NGENDERS; k++)
		jt1089((short)8076, (short)(8010 + 3 * k),
		       (short)(k == gender && step >= 1 ? 11 : 7), "%s",
		       cg_genders[k]);
	if (step >= 2)                       /* class list (race-gated) */
		for (k = 0; k < nallowed; k++)
			jt1089((short)8006, (short)(8035 + 3 * k),
			       (short)(k == ksel ? 11 : 7), "%s",
			       cg_classes[allowed[k]]);
	if (step >= 3) {                     /* rolled ability scores */
		for (k = 0; k < 6; k++)
			jt1089((short)8076, (short)(8024 + 3 * k), (short)7,
			       "%s %d", cg_stat_names[k], stats[k]);
		jt1089((short)8006, (short)8058, (short)7,
		       "R = re-roll   Return = keep");
	}
}

/* JT[574] (CODE 17 + 0x3b5e) — the character create/train entry (l0f1a /
 * case 0). Shows the char-creation screen (L3666 -> the PICK race/class/
 * gender/alignment headers + the option lists) on the shared stone chrome,
 * then holds until a key. The selection + stat-roll state machine is the
 * deferred remainder (docs/menu-wiring-plan.md / TODO). */
static int  jt574(long ctx)
{
	unsigned char scan = 0, ascii = 0;
	unsigned char *px; short pitch, sw, sh, yy;

	PROBE("jt574");
	(void)ctx;

	/* The char-creation screen's label coords (8006/8040/8076…) assume the
	 * deep display scale (jt1135 ×3); the Training Hall left g_a5_-2347 = 1
	 * (×2), which packs the PICK headers on top of each other. Use the deep
	 * scale here. */
	g_a5_2347 = 0;
	load_menu_ui();                      /* shared UI palette + backdrop */

	if (qd_screen_pixels(&px, &pitch, &sw, &sh) && px) {
		if (g_menu_state == 1)
			fill_backdrop(px, pitch, 0, 0,
			              (short)(sw - 1), (short)(sh - 1));
		else
			for (yy = 0; yy < sh; yy++)
				memset(px + (long)yy * pitch, 0x08, (size_t)sw);
		qd_present();
	}
	l3666();                             /* seed wizard state + PICK headers */

	/* Interactive pick flow (PORT interaction, pending the faithful jt568
	 * mouse state machine): a keyboard cursor advances through the picks —
	 * race -> gender -> class. Up/Down (scan 0x48/0x50) move the highlight
	 * in the active list, Return advances to the next pick, Esc backs up
	 * (and cancels out of race). The class list is gated to the picked
	 * race's allowed classes (cg_allowed_classes). The chosen race is
	 * stored in g_a5_-7027; the stat roll (L34f0), alignment pick, naming,
	 * and the record build + add-to-roster are the deferred remainder. */
	{
		short race = (short)(signed char)g_a5_byte(-7027);
		short gender = 0, ksel = 0, step = 0;
		short allowed[CG_NCLASSES];
		short nallowed;
		short stats[6];

		if (race < 0 || race >= CG_NRACES)
			race = 0;
		nallowed = cg_allowed_classes(race, allowed);
		cg_roll_stats(race, stats);

		while (plat_kb_poll(&scan, &ascii))    /* drain the triggering key */
			;
		for (;;) {
			if (qd_screen_pixels(&px, &pitch, &sw, &sh) && px) {
				if (g_menu_state == 1)
					fill_backdrop(px, pitch, 0, 0,
					              (short)(sw - 1), (short)(sh - 1));
				else
					for (yy = 0; yy < sh; yy++)
						memset(px + (long)yy * pitch, 0x08, (size_t)sw);
			}
			l35f8();                       /* PICK headers */
			cg_draw(race, gender, ksel, step, allowed, nallowed, stats);
			qd_present();

			while (!plat_kb_poll(&scan, &ascii))
				;
			if (ascii == 13 || ascii == 3) {       /* Return -> advance */
				if (step == 0)                 /* race picked: gate classes */
					nallowed = cg_allowed_classes(race, allowed);
				if (step == 2)                 /* class picked: roll stats */
					cg_roll_stats(race, stats);
				if (++step > 3)
					break;                 /* race+gender+class+stats done */
			} else if (ascii == 27) {              /* Esc -> back up / cancel */
				if (--step < 0) {
					g_a5_byte(-7027) = (unsigned char)race;
					return 0;
				}
			} else if (step == 3) {                /* stat-roll step */
				if (ascii == 'r' || ascii == 'R' || ascii == ' ')
					cg_roll_stats(race, stats);
			} else {                               /* list-pick step */
				short *cur = (step == 0) ? &race
				           : (step == 1) ? &gender : &ksel;
				short  n   = (step == 0) ? CG_NRACES
				           : (step == 1) ? CG_NGENDERS : nallowed;
				if (scan == 0x48)              /* Up */
					*cur = (short)((*cur + n - 1) % n);
				else if (scan == 0x50)         /* Down */
					*cur = (short)((*cur + 1) % n);
			}
		}
		g_a5_byte(-7027) = (unsigned char)race;    /* store the chosen race */
		/* race / gender / allowed[ksel] / stats[] are now chosen; the
		 * record build that persists them into the roster is the next
		 * char-gen slice (alignment pick + naming come with it). */
	}
	return 0;                            /* back to the Training Hall */
}

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
/* JT[557] — Create Character (Training Hall case 3, l0f60). Opens the
 * char-gen screen (jt574 shows PICK race/align/gender + the option lists);
 * the fresh-record build + selection state machine is the deferred CODE 17
 * remainder. */
static void   jt557(void)                       { PROBE("jt557"); (void)jt574(0); }
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

/* JT[401] (CODE 3 + 0x3d4c) — read from an open file. Mac builds a
 * PBRead param block + JT[1043]; the port routes to the compat
 * FSRead. Returns bytes read (partial reads on eofErr are kept),
 * or -1 on a hard error. */
static short jt401(short refNum, void *buffer, short count)
                                                __attribute__((unused));
static short jt401(short refNum, void *buffer, short count)
{
	long  n = (long)(unsigned short)count;
	OSErr err;

	PROBE("jt401");
	if (buffer == NULL)
		return -1;
	err = FSRead(refNum, &n, buffer);
	if (err != noErr && err != (OSErr)-39)     /* -39 = eofErr (partial ok) */
		return -1;
	return (short)n;
}

/* JT[127] (CODE 6 + 0x0146) — load a design data file, with
 * shared-library fallback.
 *
 *   JT[394](path, "%s%03d.dat", prefix, num);     // "GEO040.dat"
 *   designpath = "";
 *   JT[431](designpath, g_a5_-31336);             // current design name
 *   JT[431](designpath, path);                     // "<design>:GEO040.dat"
 *   refnum = JT[398](designpath, 0);               // open per-design file
 *   if (refnum >= 0) {
 *       *out = JT[401](refnum, buffer, 32766);     // read
 *       JT[411](refnum);                            // close
 *       return;                                     // success
 *   }
 *   // else fall back to the shared "<prefix>.glb" library, pulling
 *   // item `num` through the file cache (JT[987]/JT[467]/JT[468]).
 *
 * The per-design open uses the <design>:<file> HFS path; the compat
 * FSOpen strips it to the bare filename, which resolves in the flat
 * staged gamedata folder. A design overrides individual maps /
 * sprites by shipping its own GEOnnn.DAT etc.; an absent per-design
 * file falls through to the shared GLIB library.
 *
 * Signature: (prefix, num, *out, buffer). *out receives the byte
 * count read; the engine's load routines read it back.
 *
 * The .glb fallback is DEFERRED — it needs the FC group-load path
 * (JT[987] group register + JT[467] fc_read + JT[468] cached handle
 * + GLIB item extract). Until then a non-overridden file reads
 * nothing (the TUTORIAL.DSN tutorial ships GEO040.DAT, GAME001.DAT,
 * STRG003.DAT, so its core files take the per-design path). */
static void jt127(const char *prefix, short num, short *out, void *buffer)
                                                __attribute__((unused));
static void jt127(const char *prefix, short num, short *out, void *buffer)
{
	char  path[402];
	char  dpath[202];
	short refnum;
	short n;

	PROBE("jt127");

	jt394(path, "%s%03d.dat", prefix, (int)(unsigned char)num);
	dpath[0] = 0;
	jt431(dpath, &g_a5_31336);     /* prepend the current design name */
	jt431(dpath, path);            /* "<design>:<prefix><num>.dat"     */

	refnum = jt398(dpath, 0);
	if (refnum >= 0) {
		n = jt401(refnum, buffer, (short)32766);
		if (out != NULL)
			*out = n;
		jt411(refnum);
		return;
	}

	/* .glb shared-library fallback — deferred (see header). */
	PROBE("jt127:glb-fallback-deferred");
	if (out != NULL)
		*out = 0;
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
/* Forward — jt182 / jt176 / jt394 / jt179 lifted further down. */
static short jt182(const char *p1, long p2, short arg3, short arg4);
static void  jt176(void);

/* New PROBE-stub helpers jt585 calls. */
static signed char l005a(void)                   { PROBE("L005a"); return 1; }
static signed char l00e0(const char *fn, void *cb)
                                                 { PROBE("L00e0"); (void)fn; (void)cb; return 0; }
static void  jt419(char *path, const char *ext, short flags)
                                                 { PROBE("jt419"); (void)path; (void)ext; (void)flags; }
static short jt580(void)                         { PROBE("jt580"); return 0; }
static void  l1c92(void)                         { PROBE("L1c92"); }
static void  l1cd2(void)                         { PROBE("L1cd2"); }

#define g_a5_18486 g_a5_byte(-18486)   /* "save-flag" gate for player[49] clear */
#define g_a5_18877 g_a5_byte(-18877)   /* per-player byte copied into player[19] */
#define g_a5_13904 g_a5_long(-13904)   /* JT[182] source prompt for save picker */
#define g_a5_13776 g_a5_long(-13776)   /* JT[182] target buffer for save picker */
#define g_a5_22733 g_a5_byte(-22733)   /* save-mode flag */

/* JT[585] (CODE 15 + 0x1a24, 104 lines) — save / load slot picker.
 *
 * Despite the "Save Saved Game" naming in jt918's case-11 path
 * (l120c), this function handles BOTH save and load via the
 * g_a5_-27990 mode flag. The Mac asm:
 *
 *   ; Seed the player handle with current state bytes.
 *   player = *g_a5_-28006;
 *   player[67] = g_a5_-12288;
 *   player[68] = g_a5_-12287;
 *   player[17] = g_a5_-12286;
 *   player[19] = g_a5_-18877;
 *   if (g_a5_-18486 == 0) player[49] = 0;
 *
 *   loop:
 *     JT[179](9);                                 ; reset 9-slot picker
 *     short mode = (g_a5_-27990 == 2) ? -1 : 0;   ; -1 = "load" mode
 *     short slot = JT[182](g_a5_-13904,
 *                          g_a5_-13776, 0, mode);
 *     ; Keep looping if slot < 10 AND not (special-flag + ESC)
 *     if (slot < 10 && !(g_a5_-24139 && slot == 27))
 *         goto loop;
 *
 *   if (slot > 9) goto exit;                       ; cancelled
 *
 *   byte slot_char = 'A' + slot;
 *   if (!L005a()) goto exit;                       ; "insert save disk"
 *
 *   JT[176]();                                     ; paint frame
 *   JT[94](0, 24, 0, 7, "Saving...Please Wait");
 *
 *   char fn[44];
 *   JT[394](fn, "%s%c", "SavGam", slot_char);      ; "SavGamA".."SavGamJ"
 *   JT[419](fn, "csv", 1);                          ; append extension
 *
 *   g_a5_-22733 = 1;
 *   if (!L00e0(fn, JT[580])) {                     ; write file via callback
 *       JT[176]();
 *       goto exit;
 *   }
 *
 *   if (g_a5_-22218 != slot_char) {                ; slot index changed?
 *       L1c92();
 *       g_a5_-22218 = slot_char;
 *       L1cd2();
 *   }
 *   g_a5_-27946 = 1;                                ; "save complete" flag
 *   JT[176]();
 *
 * The L00e0(fn, JT[580]) callback writes the actual file bytes —
 * JT[580] is the per-byte write hook the file I/O loop calls.
 *
 * Level-2 lift: structure preserved, sub-helpers PROBE-only. The
 * player handle deref (g_a5_-28006) is NULL-guarded since our boot
 * path has no player data yet — without the design-load chain, no
 * one populates g_a5_-28006. iter_guard caps the slot-picker loop
 * while JT[182] is structurally complete but driven through the
 * test scaffold's empty roster.
 *
 * Caveat: NOT actually wired into the boot dispatch path — jt918
 * case 11 (l120c) gates on g_a5_-14429 which our fresh-init leaves
 * 0, so jt585 doesn't fire from a real 'L' press. Lift is mostly
 * documentation + structural readiness for when g_a5_-14429 gets
 * enabled (or the design-load chain populates it). */
static void   jt585(void)
{
	char           fn[44];
	unsigned char  slot_idx;
	unsigned char  slot_char;
	short          iter_guard;
	unsigned char *player;

	PROBE("jt585");

	player = (unsigned char *)g_a5_28006;
	if (player != NULL) {
		player[67] = g_a5_12288;
		player[68] = g_a5_12287;
		player[17] = g_a5_12286;
		player[19] = g_a5_18877;
		if (g_a5_18486 == 0)
			player[49] = 0;
	}

	slot_idx = 0xFF;
	for (iter_guard = 0; iter_guard < 8; iter_guard++) {
		short mode;
		short pick;

		jt179((short)9);
		mode = (g_a5_27990 == 2) ? (short)-1 : (short)0;
		pick = jt182((const char *)(uintptr_t)g_a5_13904,
		             g_a5_13776, (short)0, mode);
		slot_idx = (unsigned char)pick;

		/* Loop exit: slot_idx >= 10 OR (special-flag AND ESC). */
		if (slot_idx >= 10)
			break;
		if (g_a5_24139 != 0 && slot_idx == 27)
			break;
	}

	if (slot_idx > 9)
		return;                       /* cancelled */

	slot_char = (unsigned char)('A' + slot_idx);

	if (l005a() == 0)
		return;                       /* save disk missing */

	jt176();
	(void)jt94((short)0, (short)24, (short)0, (short)7,
	           "%s", "Saving...Please Wait");

	(void)jt394(fn, "%s%c", "SavGam", slot_char);
	jt419(fn, "csv", (short)1);

	g_a5_22733 = 1;
	if (l00e0(fn, (void *)jt580) == 0) {
		jt176();
		return;
	}

	if (g_a5_22218 != slot_char) {
		l1c92();
		g_a5_22218 = slot_char;
		l1cd2();
	}
	g_a5_27946 = 1;
	jt176();
}
/* JT[904] (CODE 19 + 0x213e) — Add Character roster screen.
 *
 * Structural level-2 lift. The interactive body is a do/while
 * loop that paints the roster via JT[182] and dispatches on the
 * returned key code:
 *
 *   *out_done = 0;
 *   g_a5_-27936 = g_a5_-27932;        // save design ptr
 *   L1276();                           // CODE-19 entry init
 *   jt399(&g_a5_-24126, 40, 0xFF);    // zero the 40-byte index buf
 *   byte exit_flag = 1;
 *
 *   do {
 *       byte status1 = 0;              // jt155 counter (fp@(-9))
 *       byte cond1 = 0, cond2 = 0;
 *
 *       // Walk 140 bytes at g_a5_-5806 + 198..337; if any low 7
 *       // bits are non-zero, set cond1.
 *       for (i = 0; i < 140; i++)
 *           if ((((char *)g_a5_-5806)[i + 198] & 0x7F) > 0)
 *               cond1 = 1;
 *
 *       // Walk 3 shorts at g_a5_-5806 + 76; if any is > 0, cond2.
 *       for (j = 0; j <= 2; j++)
 *           if (*(short *)((char *)g_a5_-5806 + 76 + j*2) > 0)
 *               cond2 = 1;
 *
 *       // 8 conditional JT[155] appends building the menu index:
 *       if (*(long *)((char *)g_a5_-5806 + 8) != 0)
 *           JT[155](0, &status1);
 *       if (cond1)
 *           JT[155](1, &status1);
 *       if ( ((byte g_a5_-5806[147] >= 128 && g_a5_-5806[382] &&
 *              g_a5_-5806[94] == 1) || cond2)
 *           && g_a5_-27990 != 5 )
 *           JT[155](2, &status1);
 *       if (cond2) {
 *           JT[155](g_a5_-27990 == 10 ? 3 : 4, &status1);
 *       }
 *       if (L4e56(g_a5_-27932) && g_a5_-27990 != 10)
 *           JT[155](5, &status1);
 *       if (L4ec6(g_a5_-27932) && g_a5_-27990 != 10)
 *           JT[155](6, &status1);
 *       JT[155](7, &status1);          // always: Cancel/Done entry
 *
 *       g_a5_-24140 = exit_flag;
 *       last_key = JT[182](STRS+0x5a34, g_a5_-13804, 0, 0);
 *       if (g_a5_-24139 && last_key == 27) last_key = 7;  // ESC -> 7
 *       exit_flag = g_a5_-24140;
 *
 *       switch (last_key) {
 *         case 0: L25ce(out_done); break;          // Add new
 *         case 1: JT[595](0, 0, &sel_key, &cond1); break;  // Create
 *         case 2: L4334(); break;                  // Delete
 *         case 3: case 4: L46e0(1); L19ac(); break;// Train (split path)
 *         case 5: L4f2c(g_a5_-27932); break;       // Import
 *         case 6: L4ff6(g_a5_-27932); break;       // Export
 *       }
 *       if (!*out_done && last_key < 3)
 *           L1276();                                // re-paint
 *   } while (last_key != 7 && !*out_done);
 *
 *   if (g_a5_-27990 == 5) JT[527]();
 *   JT[23]();                                       // graphic cleanup
 *
 * Lifted as the structural skeleton with all called helpers as
 * PROBE-only stubs. JT[182] (the interactive prompt) is the heavy
 * lift the dispatch waits on; until it returns a real selection
 * we'd spin forever — bound the loop with iter_guard so the trace
 * shows one pass of the dispatcher then exits.
 *
 * The 140-byte / 3-short walks dereference g_a5_-5806 which the
 * port hasn't bootstrapped (it's the per-character record pointer
 * the design-load path sets up). Reading through NULL would
 * bus-error, so the walks are guarded behind a non-NULL check.
 */
/* New PROBE-stub helper L1276's prologue calls. jt25 / jt94 are
 * already lifted earlier in this file. */
static void   jt82(void)                             { PROBE("jt82"); }

/* L1276 (CODE 19 + 0x1276) — character status panel renderer.
 *
 * Entry side effects + ~600 lines of formatted field paints. The
 * prologue is lifted faithfully; the field paints stay PROBE-only
 * until JT[94] (formatted text) and the character-data deref chain
 * are wired (none of the field paints fire safely when g_a5_-5806
 * is NULL).
 *
 * Prologue:
 *   g_a5_-5806 = g_a5_-27932;          ; current record = design ptr
 *   JT[82]();                          ; per-paint init
 *   JT[25](g_a5_-5806, 1, 1, 0);       ; paint frame
 *
 * Field-paint loop (deferred):
 *   "Status:" label at (20,1) — col 7 (color) or 11 (mono) via JT[94]
 *   class name      at (27,1) — table lookup g_a5_-14480[ rec[94] ]
 *   race name       at (1,3)  — table lookup g_a5_-14500[ rec[92] ]
 *   "%d years" age  at (8,3)  — JT[488] sprintf + JT[94]
 *   alignment name  at (1,4)  — table lookup g_a5_-14536[ rec[93] ]
 *   profession      at (20,4) — table lookup g_a5_-14564[ rec[88] ]
 *   gender          at (??,?) — table lookup g_a5_-14636[ rec[89] ]
 *   ability scores  at (??,?) — six fields (str/int/wis/dex/con/cha)
 *   HP / AC / saves / equipment / spells follow.
 *
 * Each field uses JT[94](x, y, color, format_mode, string) where
 * color is 7 (white) in color-QD mode (JT[1200]=3) or 11 (black on
 * white) in mono. format_mode controls bold / inverse highlight.
 *
 * The 14480 / 14500 / 14536 / 14564 / 14636 tables are arrays of
 * (char *) loaded from STRS during resource pool replay — index
 * the per-character record byte into them to get the name string.
 *
 * NULL guard: skip everything past the prologue when g_a5_-5806 is
 * NULL (no design loaded) — the Mac body would dereference rec[N]
 * for many N's, but the port's boot path has no character data.
 *
 * Called by jt904 once on entry and again from its dispatch loop
 * after cases 0/1/2 (re-paint after state change). */
static void l1276(void)
{
	PROBE("L1276");
	g_a5_long(-5806) = g_a5_long(-27932);
	jt82();
	jt25(g_a5_long(-5806), (short)1, (short)1, (short)0);

	if (g_a5_long(-5806) == 0)
		return;                /* no character record -> no field paints */

	/* Field paint loop deferred — see comment block above. */
}
static signed char l25ce(unsigned char *p)           { PROBE("L25ce"); if (p) *p = 1; return 0; }
static void   l4334(void)                            { PROBE("L4334"); }
static void   l46e0(short a)                         { PROBE("L46e0"); (void)a; }
static void   l19ac(void)                            { PROBE("L19ac"); }
static void   l4f2c(long ptr)                        { PROBE("L4f2c"); (void)ptr; }
static void   l4ff6(long ptr)                        { PROBE("L4ff6"); (void)ptr; }
/* JT[29] stub — character-class-conditional inequality probe.
 * The Mac body: tmp = JT[35](rec); return (rec[138] > tmp) ? -1 : 0;
 * JT[35] itself walks rec[88]/rec[88+i] for i=0..5 to derive a
 * threshold (class-table lookup). Lifting either requires the
 * class-table data and rec to be non-NULL; for our boot path
 * (rec = NULL), the gate trivially returns 0. */
static signed char jt29(const unsigned char *rec) __attribute__((unused));
static signed char jt29(const unsigned char *rec)
{
	PROBE("jt29");
	(void)rec;
	return 0;
}

/* L4e56 (CODE 19 + 0x4e56) — "import character?" gate for
 * jt904's jt155(5) call.
 *
 *   rec_a0 = rec;
 *   if (rec[89] == 3) goto check_mode;
 *   if (rec[167] > 0 && JT[29](rec)) goto check_mode;
 *   goto false;
 *
 *  check_mode:
 *   if (g_a5_-27990 == 5)        goto false;
 *   if (rec[94] != 0)             goto false;
 *   if (JT[41](rec, 140, &local)) goto true;
 *   goto false;
 *
 * NULL guard: returns 0 if rec is NULL — Mac body would deref
 * rec[89] / rec[94] etc, but the port's boot path has no
 * character record yet. L1276's prologue copies g_a5_-27932
 * (design ptr, NULL in boot) to g_a5_-5806, so jt904's call
 * `l4e56(g_a5_-27932)` passes NULL through. */
static signed char l4e56(long ptr)
{
	const unsigned char *rec = (const unsigned char *)(uintptr_t)ptr;
	long                 local = 0;

	PROBE("L4e56");
	if (rec == NULL)
		return 0;

	{
		int check_mode = 0;

		if (rec[89] == 3)
			check_mode = 1;
		else if (rec[167] > 0 && jt29(rec))
			check_mode = 1;

		if (!check_mode)
			return 0;
		if (g_a5_27990 == 5)
			return 0;
		if (rec[94] != 0)
			return 0;
		if (jt41(ptr, (short)140, &local) != 0)
			return (signed char)1;
		return 0;
	}
}

/* L4ec6 (CODE 19 + 0x4ec6) — "export character?" gate for
 * jt904's jt155(6) call.
 *
 *   if (rec[89] == 3) goto check_mode;
 *   if (rec[167] > 0 && JT[29](rec)) goto check_mode;
 *   goto false;
 *
 *  check_mode:
 *   if (g_a5_-27990 == 5)  goto false;
 *   if (rec[94] != 0)      goto false;
 *   if (rec[128] >= 1)     goto true;       ; the only diff vs L4e56
 *   goto false;
 *
 * The body is identical to L4e56 except the final check: L4ec6
 * tests rec[128] (a count byte — non-zero means "has exportable
 * items"), while L4e56 calls JT[41] to walk the import list. */
static signed char l4ec6(long ptr)
{
	const unsigned char *rec = (const unsigned char *)(uintptr_t)ptr;

	PROBE("L4ec6");
	if (rec == NULL)
		return 0;

	{
		int check_mode = 0;

		if (rec[89] == 3)
			check_mode = 1;
		else if (rec[167] > 0 && jt29(rec))
			check_mode = 1;

		if (!check_mode)
			return 0;
		if (g_a5_27990 == 5)
			return 0;
		if (rec[94] != 0)
			return 0;
		if (rec[128] >= 1)
			return (signed char)1;
		return 0;
	}
}
/* JT[182] (CODE 7 + 0x34f0) — interactive roster prompt dispatcher.
 *
 *   if (jt396(p2, g_a5_-14644))     ; case-insensitive strcmp
 *       L1806((char)arg4_lo);       ; quick-path action
 *       g_a5_-24139 = 0;
 *       return 13;                  ; "CR" (Mac confirm key)
 *
 *   char buf[80];
 *   L206e(p2, buf, p1, &arg3_lo);   ; build prompt cluster
 *   short tmp = L23b4((char)arg4_lo); ; mode -> opcode
 *   return L25b6(tmp, buf, &g_a5_-24139); ; interactive select
 *
 * The Mac stack from jt904's call:
 *   p1   (long) = STRS+0x5a34 prompt template
 *   p2   (long) = g_a5_-13804 roster cluster pointer
 *   arg3 (short) = first byte slot (out-param scratch)
 *   arg4 (short) = mode/opcode (low byte)
 *
 * Both arg3 and arg4 are accessed by the Mac via fp@(17) / fp@(19),
 * the low bytes of the two trailing short slots. Modelled here as
 * proper short args; their low bytes feed L1806 / L206e / L23b4.
 *
 * L1806 / L206e / L23b4 / L25b6 stay PROBE stubs — L25b6 alone is
 * a 200+-line interactive event loop with its own 8-arm JT[3]
 * dispatch (the design-roster selection grid). With L25b6
 * returning 0, jt182 returns 0, jt904 case 0 -> L25ce sets
 * *out_done = 1, jt904 exits cleanly through its loop predicate. */
static void  l1806(short v)                          { PROBE("L1806"); (void)v; }
/* New PROBE-stub helpers L206e calls. */
/* JT[482] (CODE 3 + 0x0024) — substring extract into g_a5_-10362.
 *
 *   L366a(src + offset - 1, &g_a5_-10362, count);    ; memcpy
 *   g_a5_-10362[count] = 0;                           ; null-terminate
 *   return &g_a5_-10362;
 *
 * Offset is 1-based (matches the THINK C / Pascal convention
 * the Mac engine uses throughout — `offset = 1` means "start at
 * the first character"). L366a is the engine's BlockMove-style
 * memcpy: L366a(src, dst, count). count is clamped to fit in
 * g_a5_-10362's 256-byte buffer.
 *
 * Returns a pointer to g_a5_-10362 — same shared scratch jt488
 * uses, so callers must consume the result before the next
 * sprintf-like call clobbers it. */
static const char *jt482(const char *src, short offset, short count)
{
	short copy_len;

	PROBE("jt482");
	if (src == NULL || count <= 0) {
		g_a5_10362[0] = 0;
		return g_a5_10362;
	}
	copy_len = (count < (short)(G_A5_10362_LEN - 1))
	         ? count : (short)(G_A5_10362_LEN - 1);
	memcpy(g_a5_10362, src + offset - 1, (size_t)copy_len);
	g_a5_10362[copy_len] = 0;
	return g_a5_10362;
}

/* JT[481] (CODE 3 + 0x01ba) — in-place delete of `count` chars
 * from `str` starting at 1-based `offset`.
 *
 *   short len_after_tail = JT[483](str + offset + count - 1);
 *   L366a(str + offset + count - 1,                 ; src
 *         str + offset - 1,                          ; dst
 *         len_after_tail + 1);                       ; count (+1 for NUL)
 *
 * Effectively `memmove(str + offset - 1,
 *                      str + offset + count - 1,
 *                      strlen(tail) + 1)`. Preserves the NUL
 * terminator since the +1 includes it.
 *
 * Used by L2184's final tail-trim path (`if last char == ' ' or
 * '@', delete one char`). */
static void jt481(char *str, short offset, short count)
{
	char  *dst;
	char  *src;
	size_t remaining;

	PROBE("jt481");
	if (str == NULL || count <= 0)
		return;
	dst = str + offset - 1;
	src = str + offset + count - 1;
	remaining = strlen(src) + 1;
	memmove(dst, src, remaining);
}

/* L2184 (CODE 7 + 0x2184) — prompt-word extractor.
 *
 * The body L206e calls first to populate g_a5_-13000 with the
 * roster prompt assembled from select source words. Walks the
 * source string counting uppercase/digit-led words; for each one
 * whose position matches the byte stored in g_a5_-24126[index*2]
 * (the entries jt904's jt155 sequence pre-loaded), extracts the
 * word and appends it to g_a5_-13000 via "%s%s" snprintf.
 *
 *   byte src_idx = 0, out_idx = 0, iter_char = 0;
 *   byte len = JT[483](src);
 *   jt384(&g_a5_-13000, "");                   ; clear cache
 *
 *   while (src_idx < len) {
 *       byte ch = src[src_idx];
 *       if (!is_upper(ch) && !is_digit(ch)) {
 *           src_idx++;                          ; non-boundary, skip
 *           continue;
 *       }
 *
 *       if (g_a5_-24126[out_idx * 2] != iter_char) {
 *           src_idx++;                          ; not the wanted entry
 *           iter_char++;
 *           continue;
 *       }
 *
 *       ; Match — record code, scan to end of word
 *       g_a5_-24126[out_idx*2 + 1] = ch;
 *       byte word_start = src_idx;
 *       src_idx++;
 *       while (src_idx < len
 *              && !is_upper(src[src_idx])
 *              && !is_digit(src[src_idx]))
 *           src_idx++;
 *
 *       ; Extract substring and append to g_a5_-13000
 *       short word_len = src_idx - word_start;
 *       const char *sub = JT[482](src, word_start + 1, word_len);
 *       jt384(tmp, sub);
 *       const char *built = JT[488]("%s%s", g_a5_-13000, tmp);
 *       jt384(g_a5_-13000, built);
 *
 *       out_idx++;
 *       iter_char++;
 *   }
 *
 *   ; Trim trailing ' ' or '@'
 *   byte len2 = JT[483](g_a5_-13000);
 *   if (g_a5_-13000[len2-1] == ' ' || g_a5_-13000[len2-1] == '@')
 *       JT[481](&g_a5_-13000, len2, 1);
 *
 * jt904's prep sequence calls jt155(N, &counter) for each menu
 * entry it wants visible — that writes N into g_a5_-24126[i*2]
 * for i = 0..count-1. L2184 then walks the source prompt picking
 * out the corresponding upper/digit-positioned words. The boot
 * path's jt904 only fires the unconditional `jt155(7, ...)`, so
 * g_a5_-24126[0] = 7 and L2184 hunts for the 8th upper/digit
 * word in src — without a real prompt source set up that means
 * zero matches, g_a5_-13000 stays empty, L1a0c returns 0.
 *
 * The Mac body uses byte-typed locals throughout (fp@(-1..-5)),
 * not shorts. That matches THINK C's compact code-gen — single
 * byte ops save bytes. Carried over literally to keep the
 * comparisons against g_a5_-24126[i*2] (also byte) typed-clean. */
static void l2184(const char *src)
{
	unsigned char  len;
	unsigned char  out_idx;
	unsigned char  src_idx;
	unsigned char  iter_char;
	unsigned char  tmp[86];
	unsigned char  ch;

	PROBE("L2184");
	if (src == NULL)
		return;

	len       = (unsigned char)jt483(src);
	jt384((char *)g_a5_buf(-13000), "");
	out_idx   = 0;
	iter_char = 0;
	src_idx   = 0;

	while (src_idx < len) {
		ch = (unsigned char)src[src_idx];
		if ((ch < 'A' || ch > 'Z')
		    && (ch < '0' || ch > '9')) {
			src_idx++;
			continue;
		}

		if (g_a5_24126[out_idx * 2] != iter_char) {
			src_idx++;
			iter_char++;
			continue;
		}

		/* Match — record code, scan to end of word. */
		g_a5_24126[out_idx * 2 + 1] = ch;
		{
			unsigned char word_start = src_idx;
			short         word_len;
			const char   *sub;
			const char   *built;

			src_idx++;
			while (src_idx < len) {
				unsigned char c2 = (unsigned char)src[src_idx];

				if ((c2 >= 'A' && c2 <= 'Z')
				    || (c2 >= '0' && c2 <= '9'))
					break;
				src_idx++;
			}
			word_len = (short)(src_idx - word_start);

			sub   = jt482(src, (short)(word_start + 1), word_len);
			jt384((char *)tmp, sub);
			built = jt488("%s%s",
			              (const char *)g_a5_buf(-13000),
			              (const char *)tmp);
			jt384((char *)g_a5_buf(-13000), built);
		}

		out_idx++;
		iter_char++;
	}

	{
		unsigned char len2 = (unsigned char)jt483(
		                       (const char *)g_a5_buf(-13000));
		unsigned char last;

		if (len2 == 0)
			return;
		last = g_a5_buf(-13000)[len2 - 1];
		if (last == ' ' || last == '@')
			jt481((char *)g_a5_buf(-13000),
			      (short)len2, (short)1);
	}
}
/* L1a0c (CODE 7 + 0x1a0c) — prompt-string word splitter.
 *
 * Splits the prompt into space-separated words at uppercase /
 * digit boundaries, populating buf[] with `char *` entries that
 * point into the original prompt buffer (which gets mutated —
 * the boundary chars are overwritten with NUL to terminate each
 * word in place). Returns the word count.
 *
 *   g_a5_-13002 = 0;
 *   while (*p == ' ') p++;
 *   if (*p == 0) { *buf = NULL; return 0; }
 *
 *   for (count = 0; !done; count++) {
 *       buf[count] = p;
 *       at_flag    = 0;
 *
 *       ; Scan until uppercase / digit / NUL boundary; '@'
 *       ; inside the word just sets the at_flag.
 *       do {
 *           p++;
 *           if (*p == '@') at_flag = 1;
 *       } while (*p != 0 && !is_upper(*p)
 *                && !(*p >= '0' && *p <= '9'));
 *
 *       if (at_flag) {
 *           g_a5_-13001 = (byte)count;       ; "selected" index
 *           g_a5_-13002 = 1;
 *       }
 *
 *       if (*p == 0) done = 1;
 *       else         *(p - 1) = 0;            ; terminate prev word
 *   }
 *   return count;
 *
 * Prompt format the FRUA roster expects:
 *
 *   "exit Add Modify Delete View"
 *
 * After split: buf = ["exit", "Add", "Modify", "Delete", "View"].
 * The leading lowercase intro ("exit") is word 0; each capitalised
 * action is a subsequent word. '@' anywhere in the prompt marks
 * the word containing it as the current selection.
 *
 * The Mac body mutates the prompt buffer in place (writes NUL at
 * each boundary). Caller therefore needs to pass a writable copy
 * — L206e already copies into g_a5_-12908 via jt384, so the
 * mutation lands in cache, not the original STRS resource.
 *
 * Returns the word count, which L206e stamps into g_a5_-13016
 * (L2170-equivalent) and feeds to L25b6 as arg_count. */
static short l1a0c(const char *prompt, void *buf)
{
	char       **out  = (char **)buf;
	char        *p    = (char *)(uintptr_t)prompt;
	short        count = 0;
	unsigned char at_flag;
	unsigned char done;

	PROBE("L1a0c");
	g_a5_byte(-13002) = 0;

	if (p == NULL || out == NULL)
		return 0;

	while (*p == ' ')
		p++;
	if (*p == 0) {
		*(long *)out = 0;
		return 0;
	}

	done = 0;
	while (!done) {
		out[count] = p;
		at_flag    = 0;

		/* Scan until boundary: '@' sets flag, NUL/upper/digit
		 * end the scan. Lowercase + special chars continue. */
		for (;;) {
			p++;
			if (*p == '@')
				at_flag = 1;
			if (*p == 0)
				break;
			if (l466a((short)(unsigned char)*p))
				break;
			if (*p >= '0' && *p <= '9')
				break;
		}

		if (at_flag != 0) {
			g_a5_byte(-13001) = (unsigned char)count;
			g_a5_byte(-13002) = 1;
		}

		if (*p == 0) {
			done = 1;
		} else {
			*(p - 1) = 0;       /* terminate previous word */
		}

		count++;
	}

	return count;
}
/* New PROBE-stub helpers L1bfe references. */
static short jt138(void *rec, short cmd, ...)        { PROBE("jt138"); (void)rec; (void)cmd; return 0; }
static short jt139(void *rec, short cmd, ...)        { PROBE("jt139"); (void)rec; (void)cmd; return 0; }
#define g_a5_13002 g_a5_byte(-13002)   /* "selection highlight active" flag */
#define g_a5_13001 g_a5_byte(-13001)   /* currently-selected item index */

/* L1aea (CODE 7 + 0x1aea) — per-row roster paint loop.
 *
 * Iterates the `count` entries in buf[] (an array of char*), one
 * DLItem per entry. For each row:
 *
 *   short item_len = jt423(buf[i]);
 *
 *   ; Pick the shape opcode based on highlight state:
 *   if (g_a5_-13002) {                  ; selection-highlight mode on
 *       shape = (g_a5_-13001 < i) ? 8089 : 8095;  ; before/after selected
 *   } else {
 *       shape = 8094;                   ; normal
 *   }
 *
 *   char first = buf[i][0];
 *   short upper = JT[422](first);       ; toupper
 *   short lower = JT[395](first);       ; tolower
 *
 *   ; Install DLItem stream:
 *   JT[452](1, shape, item_x, buf[i],
 *           36, item_len,                ; cmd=36 -> rec[24] = item_len
 *           32, upper,                   ; cmd=32 -> rec[29] = upper shortcut
 *           33, lower,                   ; cmd=33 -> rec[30] = lower shortcut
 *           20, 21, 0);                  ; cmd=20 enable, cmd=21 visible, 0 = end
 *
 *   ; Advance X coord for the next row, unless this was the
 *   ; selected entry (selected stays anchored at start_x).
 *   if (g_a5_-13002 && g_a5_-13001 == i)
 *       item_x = start_x;
 *   else
 *       item_x += (item_len + 1) * 4;
 *
 * The jt452 stream has cmd-arg pairs after the shape's three slots
 * (shape, item_x, str_ptr). Our port-side jt452 lift is simplified
 * for the boot-menu's (label, sel, page, phr) pattern — it consumes
 * the first 4 varargs after shape0 and drains the rest until a 0
 * sentinel. The L1aea call shape doesn't line up with that
 * (shape/item_x/str_ptr/36 -> label/sel/page/phr in jt452's eyes),
 * so the per-cell shortcut + size fields won't actually land in the
 * DLItem record. Tagged TODO — the eventual fix is to switch jt452
 * to a proper Mac-style stream parser keyed on cmd codes (1, 20,
 * 21, 32, 33, 36, etc.).
 *
 * Highlight-stay behaviour: the selected row anchors at start_x
 * while siblings advance, so the highlighted entry visually
 * overlaps the next slot. That's how the Mac roster shows a
 * "current selection" cell hovering at a fixed position while the
 * other entries flow past it. */
static void l1aea(short item_arg, short count, void *buf)
{
	short start_x = item_arg;
	short i;

	PROBE("L1aea");
	if (buf == NULL)
		return;

	for (i = 0; i < count; i++) {
		char **entry = (char **)((char *)buf + (long)i * 4);
		char  *str   = *entry;
		short  item_len;
		short  shape;
		short  upper;
		short  lower;

		if (str == NULL)
			continue;
		item_len = jt423(str);

		if (g_a5_13002 != 0) {
			shape = (g_a5_13001 < i) ? (short)8089 : (short)8095;
		} else {
			shape = (short)8094;
		}

		upper = jt422((short)(unsigned char)str[0]);
		lower = l46b2((short)(unsigned char)str[0]);

		/* TODO: jt452's current vararg consumer is simplified for
		 * the boot menu (label, sel, page, phr) — the cmd-arg
		 * stream below leaks past the first 4 slots. Switch to
		 * a Mac-style stream parser to surface the per-row size
		 * + shortcut fields in the DLItem record. */
		jt452((long)1, (long)shape, (long)item_arg,
		      (long)(uintptr_t)str,
		      (long)36, (long)item_len,
		      (long)32, (long)upper,
		      (long)33, (long)lower,
		      (long)20, (long)21, (long)0);

		if (g_a5_13002 != 0 && g_a5_13001 == i)
			item_arg = start_x;
		else
			item_arg = (short)(item_arg + (item_len + 1) * 4);
	}
}

/* L1bfe (CODE 7 + 0x1bfe) — roster-row content renderer.
 *
 * The body L206e hands to last. Paints the suffix string (with
 * tail-trim / space-append normalisation) and installs a shape-7
 * DLItem for the user response, dispatching the actual per-row
 * paint via L1aea when the response buffer is non-empty.
 *
 *   g_a5_-24139 = 0;
 *
 *   if (*suffix == 0) goto empty;       ; no suffix -> shape 8003
 *
 *   ; Normalise the suffix tail.
 *   char tmp[46];
 *   jt384(tmp, suffix);                  ; copy
 *   short len = jt423(tmp);              ; strlen
 *   if (tmp[len-1] == ' ' || tmp[len-1] == '@') {
 *       if (tmp[len-2] == ':') {         ; ":" before space -> trim
 *           len--;
 *           tmp[len-1] = 0;
 *       }
 *   } else if (tmp[len-1] != ':') {
 *       jt404(tmp, " ");                 ; append space
 *       len++;
 *   }
 *
 *   if (flag) JT[94](0, 24, 0, 7, "%s", tmp);   ; paint suffix
 *
 *   short item_arg = len * 4 + 8004;
 *   goto common;
 *
 *  empty:
 *   item_arg = 8003;
 *
 *  common:
 *   if (flag) {
 *       void *cb = trail ? JT[138] : JT[139];   ; pick callback
 *       JT[452](7, 20, cb, 0);                   ; install row item
 *       if (**buf != 0)
 *           L1aea(item_arg, width, buf);         ; render row content
 *   }
 *
 * Suffix normalisation is the bit that makes the Mac roster's
 * "Item:" prompts line up — trailing colons get stripped when
 * followed by a space/@, otherwise a space is appended so the
 * roster text never butts up against the user's typing area.
 *
 * L1aea (the row-content render) and JT[138] / JT[139] (the
 * DLItem callbacks) stay PROBE-only — they're the actual pixel
 * paint and the click-hit-test methods for the roster cells.
 * Lifting this commit surfaces the row-install + per-row paint
 * dispatch in the trace; L1aea is the natural next layer for
 * visible content. */
static void l1bfe(short width, void *buf, const char *suffix,
                  short trail, short flag)
{
	unsigned char  tmp[46];
	short          len;
	short          item_arg;

	PROBE("L1bfe");
	g_a5_24139 = 0;

	if (suffix == NULL || *suffix == 0) {
		item_arg = (short)8003;
	} else {
		jt384((char *)tmp, suffix);
		len = jt423((char *)tmp);
		if (len > 0
		    && (tmp[len - 1] == ' ' || tmp[len - 1] == '@')) {
			if (len > 1 && tmp[len - 2] == ':') {
				len--;
				tmp[len - 1] = 0;
			}
		} else if (len > 0 && tmp[len - 1] != ':') {
			jt404((char *)tmp, " ");
			len++;
		}

		if (flag != 0)
			(void)jt94((short)0, (short)24, (short)0,
			           (short)7, "%s", (const char *)tmp);

		item_arg = (short)(len * 4 + 8004);
	}

	if (flag != 0) {
		dlitem_method_t cb = (trail != 0)
		                   ? (dlitem_method_t)jt138
		                   : (dlitem_method_t)jt139;

		jt452((long)7, (long)20, (long)(uintptr_t)cb, (long)0);

		/* Mac: a0 = *(void **)buf; if (*(byte*)a0 != 0)
		 *   L1aea(item_arg, width, buf); */
		if (buf != NULL) {
			unsigned char *first = *(unsigned char **)buf;

			if (first != NULL && *first != 0)
				l1aea(item_arg, width, buf);
		}
	}
}
/* L162e is just an alias for JT[176] — the L address is what CODE
 * 7's internal call sites use; the JT entry is the same body at
 * CODE 7 + 0x162e. Route directly to the already-lifted jt176
 * (paint clip-rect setup + jt1001 pen commit + jt1193/L2062). */
static void  l162e(void)
{
	PROBE("L162e");
	jt176();
}

/* JT[393] (CODE 3 + 0x3b8c) — signed strcmp (returns -1 / 0 / 1).
 * Mac body walks both strings byte-by-byte until mismatch or NUL,
 * then sign-compares the last byte. */
static short jt393(const char *a, const char *b)
{
	PROBE("jt393");
	if (a == NULL || b == NULL)
		return 0;
	while (*a == *b && *a != 0) {
		a++;
		b++;
	}
	if (*(const unsigned char *)a < *(const unsigned char *)b)
		return (short)-1;
	if (*(const unsigned char *)a > *(const unsigned char *)b)
		return (short)1;
	return 0;
}

#define g_a5_13000_str g_a5_buf(-13000)    /* working copy of the prompt */
#define g_a5_12828_str g_a5_buf(-12828)    /* last-seen prompt string */
#define g_a5_12748_str g_a5_buf(-12748)    /* last-seen suffix string */
#define g_a5_12908_str g_a5_buf(-12908)    /* duplicate of -13000 for L1a0c */

/* L206e (CODE 7 + 0x206e) — prompt cluster builder.
 *
 *   L2184(prompt);                                   ; extract accelerator
 *                                                    ; letters into g_a5_-24126
 *
 *   ; Detect prompt / suffix change vs last-seen cache:
 *   if (jt393(&g_a5_-13000, &g_a5_-12828) != 0)
 *       g_a5_-12912 = 1;                              ; prompt changed
 *   else if (jt393(suffix, &g_a5_-12748) != 0)
 *       g_a5_-12912 = 1;                              ; suffix changed
 *   else if (g_a5_-13018 == 8)
 *       g_a5_-12912 = 0;                              ; mode 8 forces clean
 *
 *   ; Refresh the cache copies:
 *   jt384(&g_a5_-12908, &g_a5_-13000);                ; -12908 ← -13000
 *   jt384(&g_a5_-12828, &g_a5_-13000);                ; -12828 ← -13000
 *   jt384(&g_a5_-12748, suffix);                      ; -12748 ← suffix
 *
 *   short width = L1a0c(&g_a5_-12908, buf);           ; build prompt cluster
 *   L2170(width);                                     ; cache for L25b6
 *
 *   if (g_a5_-12912) {                                ; if dirty:
 *       jt447(); jt108(1); jt112(1); L162e();         ;   paint frame
 *   }
 *
 *   L1bfe(width, buf, suffix, *byte_ptr, g_a5_-12912); ; render content
 *
 *   if (g_a5_-12912) {
 *       jt449(1); jt112(0); jt117();                  ;   commit frame
 *   }
 *
 *   g_a5_-12911 = g_a5_-12912;                        ; remember dirty
 *   g_a5_-12912 = 0;
 *   return width;
 *
 * L2184 walks the prompt string extracting letters/digits into the
 * 20-entry g_a5_-24126 index buffer the L25b6 fallback path scans.
 * L1a0c builds the per-glyph buffer L1bfe paints; L1bfe is the
 * actual roster-row renderer. Helpers stay PROBE-only — the
 * cache-comparison + JT[384] copy chain runs as the Mac code
 * intended (jt393 lifted faithfully so the dirty flag tracks
 * real prompt changes). */
static void l206e(long p1, unsigned char *buf,
                  const char *suffix, unsigned char *byte_ptr)
{
	short width;

	PROBE("L206e");

	l2184((const char *)(uintptr_t)p1);

	/* Detect prompt change vs cached. */
	if (jt393((const char *)g_a5_13000_str,
	          (const char *)g_a5_12828_str) != 0) {
		g_a5_12912 = 1;
	} else if (jt393(suffix,
	                  (const char *)g_a5_12748_str) != 0) {
		g_a5_12912 = 1;
	} else if (g_a5_13018 == 8) {
		g_a5_12912 = 0;
	}

	/* Refresh cached copies. */
	jt384((char *)g_a5_12908_str, (const char *)g_a5_13000_str);
	jt384((char *)g_a5_12828_str, (const char *)g_a5_13000_str);
	if (suffix != NULL)
		jt384((char *)g_a5_12748_str, suffix);

	width = l1a0c((const char *)g_a5_12908_str, buf);
	g_a5_13016 = width;       /* L2170 inlined: g_a5_-13016 = arg */

	if (g_a5_12912 != 0) {
		jt447();
		(void)jt108((short)1);
		(void)jt112((short)1);
		l162e();
	}

	{
		short trail = (byte_ptr != NULL)
		            ? (short)(signed char)*byte_ptr
		            : (short)0;
		l1bfe(width, buf, suffix, trail, (short)g_a5_12912);
	}

	if (g_a5_12912 != 0) {
		jt449((short)1);
		(void)jt112((short)0);
		(void)jt117();
	}

	g_a5_12911 = g_a5_12912;
	g_a5_12912 = 0;
}
/* New PROBE-stub helpers L23b4 needs. */
static long  jt100(void)                             { PROBE("jt100"); return 0; }
static signed char jt1085(void)                      { PROBE("jt1085"); return 0; }
static void  jt1067(void)                            { PROBE("jt1067"); }
static void  jt46(short a, short b, short c, short d) { PROBE("jt46"); (void)a; (void)b; (void)c; (void)d; }

/* Forward — l15bc lifts further down with L25b6's helpers. */
static signed char l15bc(void);

#define g_a5_24238 g_a5_byte(-24238)   /* "wipe arg low byte" gate */
#define g_a5_22307 g_a5_byte(-22307)   /* mode-2/7/12/13 init counter */
#define g_a5_24321 g_a5_byte(-24321)   /* row-state enable flag */
#define g_a5_24206 g_a5_byte(-24206)   /* row-state cap */
#define g_a5_24205 g_a5_byte(-24205)   /* row-state index */
#define g_a5_24207 g_a5_byte(-24207)   /* row-state wrap value */
#define g_a5_24138 g_a5_long(-24138)   /* L23b4 timeout threshold (ticks) */

/* L23b4 (CODE 7 + 0x23b4) — poll-loop opcode selector.
 *
 * The body jt182 hands to between L206e (prompt setup) and L25b6
 * (interactive select). Loops on L2d3e + jt1085 until a positive
 * item index arrives, then returns it. For modes 2/7/12/13 it
 * also runs an animation timer that bumps g_a5_-24205 (the
 * "blinking cell index") every 50 ticks.
 *
 *   g_a5_-13006 = 0;                  ; clear "cached result"
 *   if (g_a5_-24238) arg_lo = 0;       ; wipe low byte
 *
 *   if (mode in {2, 7, 12, 13}) {
 *       g_a5_-22307 = 1;
 *       for (i = 1..4)
 *           fp@(-20 + i*4) = JT[100]();   ; 4 baseline timestamps
 *       fp@(-32) = JT[100]();
 *       fp@(-24) = JT[100]() + 30;
 *       fp@(-28) = fp@(-24) + 50;
 *   }
 *
 *   loop:
 *     if (!JT[1163]() && JT[1200]()) JT[1067]();   ; pre-poll hook
 *     rc   = JT[1085]();                            ; "abort?" probe
 *     item = L2d3e();                                ; DLItem event poll
 *     if (rc) goto exit;
 *     if (mode in {2, 7, 12, 13}) {
 *         ... mode-3 special case (g_a5_-24256 == 121, etc) ...
 *         if (arg_lo) {
 *             if (g_a5_-24321 > 0 && g_a5_-24206 >= 1) {
 *                 JT[46](3, 3, arg_lo, g_a5_-24205);  ; blink current cell
 *                 if (g_a5_-24207) JT[80](1);
 *                 if (JT[100]() - fp@(-32) >= 50) {
 *                     g_a5_-24205++;                   ; advance blink
 *                     if (g_a5_-24205 > g_a5_-24206)
 *                         g_a5_-24205 = g_a5_-24207;
 *                     fp@(-32) = JT[100]();
 *                 }
 *             } else { fp@(-32) = JT[100](); }
 *         } else { fp@(-32) = JT[100](); }
 *         if (g_a5_-24138 > 0 && JT[100]() - fp@(-32) >= g_a5_-24138) {
 *             g_a5_-13006 = 1;                          ; timeout
 *             goto post;
 *         }
 *         ... more mode-3 / 80-tick cell-advance ...
 *     }
 *     if (item < 0) goto loop;
 *
 *   post:
 *     if (L15bc()) item = 0;
 *     return item;
 *
 * Level-2 lift: the animation / timing block for modes 2/7/12/13
 * is deferred (it's only meaningful once the design-roster paint
 * actually runs). The boot path's jt166(9) leaves mode = 9, so
 * the loop just polls L2d3e for input — exactly what we want for
 * the "wait for user keypress" semantic.
 *
 * iter_guard caps the loop while jt1085 / L2d3e are still being
 * driven by the IKBD chain; release the cap once the per-mode
 * timing arms light up. */
static short l23b4(short arg)
{
	long           fp_minus_32 = 0;
	long           fp_minus_24 = 0;
	long           fp_minus_28 = 0;
	short          item;
	signed char    rc;
	unsigned char  arg_lo;
	short          iter_guard;
	short          mode_with_timer;

	PROBE("L23b4");
	g_a5_byte(-13006) = 0;

	arg_lo = (unsigned char)(arg & 0xff);
	if (g_a5_24238 != 0)
		arg_lo = 0;

	mode_with_timer = (g_a5_13018 == 2 || g_a5_13018 == 7
	                || g_a5_13018 == 13 || g_a5_13018 == 12);

	if (mode_with_timer) {
		long base = jt100();

		g_a5_22307     = 1;
		fp_minus_32    = base;
		fp_minus_24    = base + 30L;
		fp_minus_28    = fp_minus_24 + 50L;
	}

	item = -1;
	for (iter_guard = 0; iter_guard < 4096; iter_guard++) {
		if (jt1163() == 0 && jt1200() != 0)
			jt1067();
		rc   = jt1085();
		item = (short)l2d3e();

		if (rc != 0)
			break;

		if (mode_with_timer) {
			/* Mode-3 special: g_a5_-27990 == 3 + g_a5_-24256
			 * == 121 + g_a5_-24262 != 80 cell-advance. */
			if (g_a5_27990 == 3 && g_a5_24256 == 121
			    && g_a5_24262 != 80
			    && jt100() >= fp_minus_24) {
				fp_minus_24 = fp_minus_28 + 30L;
			}

			if (arg_lo != 0) {
				if (g_a5_24321 > 0 && g_a5_24206 >= 1) {
					jt46((short)3, (short)3,
					     (short)(signed char)arg_lo,
					     (short)g_a5_24205);
					if (g_a5_24207 != 0)
						jt80((short)1);
					if (jt100() - fp_minus_32 >= 50L) {
						g_a5_24205++;
						if (g_a5_24205 > g_a5_24206)
							g_a5_24205 = g_a5_24207;
						fp_minus_32 = jt100();
					}
				} else {
					fp_minus_32 = jt100();
				}
			} else {
				fp_minus_32 = jt100();
			}

			if (g_a5_24138 > 0
			    && jt100() - fp_minus_32 >= g_a5_24138) {
				g_a5_byte(-13006) = 1;
				break;
			}

			if (g_a5_27990 == 3 && g_a5_24256 == 121
			    && g_a5_24262 != 80
			    && jt100() >= fp_minus_28) {
				fp_minus_28 = fp_minus_24 + 50L;
			}
		}

		if (item >= 0)
			break;
	}

	if (l15bc())
		item = 0;

	return item;
}

/* New PROBE-stub helpers L25b6 calls. */
static short l217e(void)                             { PROBE("L217e"); return 0; }
static void  l2170(short arg)                        { PROBE("L2170"); (void)arg; }
static signed char l15bc(void)                       { PROBE("L15bc"); return 0; }
static void  jt548(short d, short a, short b)        { PROBE("jt548"); (void)d; (void)a; (void)b; }
static void  jt559(short a)                          { PROBE("jt559"); (void)a; }

#define g_a5_13006 g_a5_byte(-13006)   /* "use cached result" flag (L25b6 early-out) */
#define g_a5_24134 g_a5_byte(-24134)   /* cached result (when -13006 != 0) */
#define g_a5_13010 g_a5_word(-13010)   /* JT[548] arg pair */
#define g_a5_13008 g_a5_word(-13008)   /* JT[548] arg pair */
#define g_a5_13004 g_a5_byte(-13004)   /* mode-7/13 special-key gate */
#define g_a5_12914 g_a5_word(-12914)   /* pending-key extended code (high range) */
#define g_a5_12913 g_a5_byte(-12913)   /* pending-key char (low byte) */
#define g_a5_14644 g_a5_buf(-14644)    /* cached prompt string for jt396 fast-path */

/* L25b6 (CODE 7 + 0x25b6) — interactive roster select.
 *
 * The body L23b4's "opcode" -> L25b6 hands to. It reads the
 * dispatcher mode at g_a5_-13018 and either resolves the next
 * key from cached state or runs one of seven mode-specific arms:
 *
 *   mode 1, 4: 8-arm JT[3] dispatch on (arg_count - L217e()), each
 *             arm produces a high-bit "shape" byte (0x81..0x88)
 *             encoding the roster cell the user picked.
 *   mode 4 (special): when arg_count - L217e() == 9, return ' '
 *             (space = "next page").
 *   mode 5:    JT[548]() then return 27 (ESC) — paginated cancel.
 *   mode 10:   L2170 + L217e arithmetic on arg_count, JT[559](1),
 *             return low byte of arg_count.
 *   mode 7, 13: when g_a5_-13004 != 0, return 'S' (save shortcut).
 *   mode 12:   when L15bc() != 0, return 'S'.
 *
 * Fallback (no mode matched): scan g_a5_-24126 (the 20-entry
 * index buffer jt155 builds in jt904) for the character at
 * *(*(char**)buf + arg_count - 1) — i.e., the prompt's last
 * shortcut byte. Match returns the paired index; no match returns
 * -1 ("nothing pressed"). The byte 0x60 ('`') is remapped to 27
 * (ESC). Any returned byte <= 20 sets g_a5_-24139 (the special-
 * key flag jt182 reads).
 *
 * Exit: write 2 into g_a5_-13018 (mark "consumed"), return byte
 * result.
 *
 * Sub-helpers L217e / L2170 / L15bc / JT[548] / JT[559] all
 * PROBE-only. With L217e returning 0 and all gates false, the
 * fallback fires; arg_count = 0 from jt182's L23b4 result gives
 * the "arg8_count == 0" sub-path which reads g_a5_-12913 (pending
 * key char) and returns it. In the boot path the pending byte is
 * 0 so L25b6 returns 0 -> jt904 case 0 -> L25ce sets *out_done.
 */
static short l25b6(short arg_count, unsigned char *buf,
                   unsigned char *flag_out)
{
	short          tmp;
	unsigned char  result;
	unsigned char  ch;
	short          i;

	PROBE("L25b6");
	(void)flag_out;        /* &g_a5_-24139, same slot the body writes directly */

	/* Fast path: cached result. */
	if (g_a5_13006 != 0)
		return (short)g_a5_24134;

	tmp = l217e();
	if (g_a5_13018 == 1)
		tmp++;

	/* Modes 1 / 4: 8-arm JT[3] dispatch on (arg_count - tmp). */
	if (g_a5_13018 == 4 || g_a5_13018 == 1) {
		if (arg_count > tmp && (short)(tmp + 8) >= arg_count) {
			short arm = (short)(arg_count - tmp);
			g_a5_24139 = 1;
			switch (arm) {
			case 1: result = 0x86; goto exit;
			case 2: result = 0x88; goto exit;
			case 3: result = 0x82; goto exit;
			case 4: result = 0x84; goto exit;
			case 5: result = 0x87; goto exit;
			case 6: result = 0x81; goto exit;
			case 7: result = 0x83; goto exit;
			case 8: result = 0x85; goto exit;
			default: break;        /* fall through to mode 4 special */
			}
		}
	}

	/* Mode 4 special: +9 page-down marker. */
	if (g_a5_13018 == 4 && (short)(tmp + 9) == arg_count) {
		g_a5_24139 = 1;
		result = 0x20;             /* space */
		goto exit;
	}

	/* Mode 5: paginated cancel via JT[548]. */
	if (g_a5_13018 == 5 && arg_count > tmp && arg_count != 255) {
		jt548((short)(arg_count - tmp), g_a5_13010, g_a5_13008);
		result = 27;                /* ESC */
		goto exit;
	}

	/* Mode 10: row-advance via L2170 + L217e. */
	if (g_a5_13018 == 10 && arg_count > tmp && arg_count != 255) {
		short d = l217e();

		l2170((short)(d + 1));
		arg_count = (short)(arg_count - l217e());
		result = (unsigned char)(arg_count & 0xff);
		jt559((short)1);
		goto exit;
	}

	/* Modes 7 / 13: save shortcut. */
	if (g_a5_13018 == 7 && g_a5_13004 != 0) {
		result = 'S';
		goto exit;
	}
	if (g_a5_13018 == 13 && g_a5_13004 != 0) {
		result = 'S';
		goto exit;
	}

	/* Mode 12: L15bc-gated save. */
	if (g_a5_13018 == 12 && l15bc() != 0) {
		result = 'S';
		goto exit;
	}

	/* Fallback: lookup in the 20-entry index buffer. */
	if (arg_count == 0) {
		/* No items: surface the pending key. */
		if (g_a5_12914 > 255) {
			result = (unsigned char)((g_a5_12914 & 0x7F) + 128);
		} else {
			result = g_a5_12913;
			if (g_a5_12914 < 32)
				g_a5_24139 = 1;
		}
	} else {
		/* buf[] is an array of (char *) — the last entry's first
		 * character is the user's pressed key. Scan g_a5_-24126
		 * (20 entries x 2 bytes: [idx, code]) for a code match;
		 * return the paired idx on hit, -1 on miss. */
		ch = **((unsigned char **)buf + (arg_count - 1));
		for (i = 0; i < 20; i++) {
			if (g_a5_24126[i * 2 + 1] == ch)
				break;
		}
		if (i < 20)
			result = g_a5_24126[i * 2];
		else
			result = 0xff;          /* -1: no match */
	}

	if (result == 0x60)                  /* '`' -> ESC */
		result = 27;
	if (result <= 20)
		g_a5_24139 = 1;

exit:
	g_a5_13018 = 2;                       /* mark consumed */
	return (short)(unsigned char)result;
}

static short jt182(const char *p1, long p2, short arg3, short arg4)
{
	unsigned char buf[80];
	unsigned char arg3_lo;

	PROBE("jt182");

	if (jt396((const char *)(uintptr_t)p2, (const char *)g_a5_14644) != 0) {
		l1806((short)(signed char)(arg4 & 0xff));
		g_a5_24139 = 0;
		return (short)13;
	}

	arg3_lo = (unsigned char)(arg3 & 0xff);
	l206e(p2, buf, p1, &arg3_lo);
	{
		short tmp = l23b4((short)(signed char)(arg4 & 0xff));
		return l25b6(tmp, buf, &g_a5_24139);
	}
}
static short  jt595(short a, short b, short *p1, unsigned char *p2)
                                                     { PROBE("jt595"); (void)a; (void)b; (void)p1; (void)p2; return 0; }
static void   jt527(void)                            { PROBE("jt527"); }
static void   jt23(void)                             { PROBE("jt23"); }

#define g_a5_5806  g_a5_long(-5806)    /* per-character record ptr (NULL until design-load) */
#define g_a5_27936 g_a5_long(-27936)   /* saved design ptr cache */
#define g_a5_13804 g_a5_long(-13804)   /* roster cluster arg for jt182 */

static void jt904(unsigned char *out_done)
{
	unsigned char  status1, cond1, cond2;
	unsigned char  exit_flag;
	short          last_key;
	short          sel_key;
	short          iter_guard;
	unsigned char *base;

	PROBE("jt904");
	if (out_done == NULL)
		return;

	/* ===== TEST SCAFFOLD — REVERT WHEN jt1146/jt1153 LAND =====
	 * Mac's "menu disappears, roster takes over" comes from page
	 * flipping (jt1146 commits the off-page; jt1153's CGrafPort
	 * PixMap chase swaps the back buffer). The CGrafPort init isn't
	 * in the port yet (commit acfb400 deferred it). Without that,
	 * roster cells paint OVER the boot menu's bottom-left.
	 *
	 * Workaround: EraseRect the full screen via the QuickDraw shim
	 * before the roster paints. Removes the menu pixels cleanly so
	 * the roster has a clean canvas.
	 *
	 * Remove the EraseRect call once the page-flip chain produces
	 * proper double-buffered swaps. */
	{
		GrafPtr port = NULL;

		GetPort(&port);
		if (port != NULL) {
			Rect full = port->portRect;

			EraseRect(&full);
		}
	}
	/* ===== end TEST SCAFFOLD ===== */

	*out_done = 0;
	last_key  = -1;
	g_a5_27936 = g_a5_27932;
	l1276();
	jt399(g_a5_buf(-24126), (short)40, (short)0xFF);
	exit_flag = 1;

	/* iter_guard bounds the dispatch while JT[182] is a stub
	 * returning 7 ("exit"); once JT[182] paints the real roster
	 * and returns a user selection, this is the Mac do/while. */
	for (iter_guard = 0; iter_guard < 2; iter_guard++) {
		status1 = 0;
		cond1   = 0;
		cond2   = 0;

		base = (unsigned char *)(uintptr_t)g_a5_5806;
		if (base != NULL) {
			short i;
			short j;

			for (i = 0; i < 140; i++) {
				if ((base[i + 198] & 0x7F) > 0) {
					cond1 = 1;
					break;
				}
			}
			for (j = 0; j <= 2; j++) {
				if (*(short *)(base + 76 + j * 2) > 0) {
					cond2 = 1;
					break;
				}
			}
		}

		if (base != NULL && *(long *)(base + 8) != 0)
			jt155((short)0, &status1);
		if (cond1)
			jt155((short)1, &status1);
		{
			int special = 0;

			if (base != NULL && base[147] >= 128 && base[382] != 0
			    && base[94] == 1)
				special = 1;
			if (cond2)
				special = 1;
			if (special && g_a5_27990 != 5)
				jt155((short)2, &status1);
		}
		if (cond2) {
			jt155((short)(g_a5_27990 == 10 ? 3 : 4), &status1);
		}
		if (l4e56(g_a5_27932) && g_a5_27990 != 10)
			jt155((short)5, &status1);
		if (l4ec6(g_a5_27932) && g_a5_27990 != 10)
			jt155((short)6, &status1);
		jt155((short)7, &status1);

		/* Clear the roster-menu backdrop + prime present (the jt182 popup
		 * paints its items via l2d3e but the engine clear path is stubbed,
		 * so the items would otherwise sit on a black page — same fix as
		 * jt315 / l0aae). */
		{
			unsigned char *px; short pitch, sw, sh, yy;
			if (qd_screen_pixels(&px, &pitch, &sw, &sh) && px) {
				for (yy = 0; yy < sh; yy++)
					memset(px + (long)yy * pitch, 0x08, (size_t)sw);
				qd_present();
			}
		}

		g_a5_24140 = exit_flag;
		last_key   = jt182(ua_strs_at(0x5a34), g_a5_13804,
		                   (short)0, (short)0);
		if (g_a5_24139 != 0 && last_key == 27)
			last_key = 7;
		exit_flag  = g_a5_24140;
		sel_key    = -1;

		switch (last_key) {
		case 0: l25ce(out_done); break;
		case 1: jt595((short)0, (short)0, &sel_key, &cond1); break;
		case 2: l4334(); break;
		case 3:
		case 4: l46e0((short)1); l19ac(); break;
		case 5: l4f2c(g_a5_27932); break;
		case 6: l4ff6(g_a5_27932); break;
		default: break;
		}

		if (*out_done == 0 && last_key < (short)3)
			l1276();

		if (last_key == 7 || *out_done != 0)
			break;
	}

	if (g_a5_27990 == 5)
		jt527();
	jt23();
}
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
	/* Begin Adventuring. Faithful path: jt585() -> the CODE 15/19 adventure
	 * setup -> the play loop, gated on a created party (g_a5_27928). Both
	 * jt585 and character creation are still PROBE stubs, so bridge straight
	 * to the working first-person play loop (load level, place party, render
	 * jt312, walk with WASD; returns on 'q') so the chain
	 * menu -> Play -> Training Hall -> Begin Adventuring -> dungeon works
	 * end to end. Revert to the gated jt585() call once the adventure
	 * setup + party creation lift. */
	if (g_a5_14431 != 0 && g_a5_27928 != 0)
		jt585();
	port_play_demo();        /* enter the dungeon; returns to the hall on 'q' */
	return 0;                /* redraw the Training Hall */
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
	 * to L0dd4. Unbounded for-loop with the body at L0dd4: each
	 * iteration polls l0aae for a menu selection, then dispatches via
	 * the JT[3] switch. Cases return 0 (continue), 1 (exit with
	 * "success"), or -1 (exit with "cancel"). Earlier iter_guard
	 * defense against L0aae returning 0 forever has come off — with
	 * jt453 spinning on l2d3e and the IKBD chain live, the loop now
	 * blocks on real input. */
	for (;;) {
		/* Restore the menu display state after a dungeon visit (Begin
		 * Adventuring -> port_play_demo leaves clut 0..15 + deep mode
		 * changed) — same fix as jt315. */
		g_a5_2347 = 1;
		load_menu_ui();                  /* shared UI palette (was clut 129) */

		/* Paint the Training Hall on the shared menu chrome — the stone
		 * backdrop — ONCE per frame, before l02dc paints the roster grid and
		 * l0aae paints the menu (so the menu's draw no longer wipes the
		 * roster). jt131(6)/jt174 are stubs in the port, so do it here. */
		{
			unsigned char *px; short pitch, sw, sh, yy;
			if (qd_screen_pixels(&px, &pitch, &sw, &sh) && px) {
				if (g_menu_state == 1)
					fill_backdrop(px, pitch, 0, 0,
					              (short)(sw - 1), (short)(sh - 1));
				else
					for (yy = 0; yy < sh; yy++)
						memset(px + (long)yy * pitch, 0x08, (size_t)sw);
				qd_present();
			}
		}

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
			/* L0e98: fresh-init defaults.
			 *
			 * Port deviation: g_a5_14435 (Add Character) forced to
			 * 1. The Mac default is 0 because a fresh-start has no
			 * design loaded — Add only enables once g_a5_27932 (the
			 * design-pointer) is non-zero, which the saved-game arm
			 * (L0df6) handles. Until the design-load path lifts,
			 * the Mac-faithful default leaves the dispatch chain
			 * unobservable; setting it lets pressing 'A' exercise
			 * the cmd=5 match -> jt918/case5 L1036 -> jt904
			 * dispatch end-to-end in the trace. Revert when the
			 * design-load gating becomes meaningful.
			 */
			g_a5_14439 = 1;
			g_a5_14438 = 0;
			g_a5_14437 = 0;
			g_a5_14436 = 0;
			g_a5_14435 = 1;            /* port enable for trace */
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
