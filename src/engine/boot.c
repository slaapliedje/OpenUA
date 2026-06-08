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
#include "mac_font.h"         /* mac_font_pixel (the in-dungeon party HUD) */

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
#include "dbglog.h"     /* dbg_log / dbg_log_num — always declared (the bodies
                         * live in platform/dbglog.c); only the per-call PROBE
                         * trace is gated, so harnesses can dbg_log without the
                         * full probe flood. */
#ifdef FRUA_ENGINE_PROBE
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

/* JT[1171] (CODE 4 + 0x108e) — the Mac `_UnpackBits` trap (0xa8d0)
 * wrapper. The Mac code is literally:
 *
 *   UnpackBits(&srcPtr, &dstPtr, dstBytes);  // trap 0xa8d0
 *   return srcPtr;                            // advanced source
 *
 * i.e. standard PackBits expansion that produces exactly `dstBytes`
 * output bytes and returns the source pointer advanced past the runs
 * it consumed. Faithfully reimplemented here (no Toolbox trap on the
 * Atari). A run that would overshoot the requested byte count is
 * written in full, matching the Mac trap — callers (L6028's record
 * buffer, the type-2 strip decoder) size their destination with the
 * slack to absorb it. Used by L6028 to expand RLE-packed MONST
 * records; the chunky UI blit path uses the whole-buffer `unpackbits`
 * helper instead. */
static void *jt1171(const void *src, void *dst, short dstBytes)
{
	const unsigned char *s = (const unsigned char *)src;
	unsigned char       *d = (unsigned char *)dst;
	long                 n = dstBytes;

	PROBE("jt1171");
	while (n > 0) {
		signed char c = (signed char)*s++;
		if (c >= 0) {                         /* c+1 literal bytes */
			short cnt = (short)(c + 1);
			n -= cnt;
			while (cnt-- > 0)
				*d++ = *s++;
		} else if (c != -128) {               /* repeat next byte 1-c times */
			short cnt = (short)(1 - c);
			unsigned char v = *s++;
			n -= cnt;
			while (cnt-- > 0)
				*d++ = v;
		}                                     /* c == -128: no-op */
	}
	return (void *)s;
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
#ifdef FRUA_L6234_VERIFY
void port_l6234_verify(void);           /* faithful 3D-render geometry verification */
#endif
static short jt953(void);                /* exploration command dispatcher */
#ifdef FRUA_CHARGEN
static int   jt574(long ctx);            /* CODE 17 char-gen entry (harness) */
#endif

/*
 * ua_main — CODE 6 + 0x58a (jump-table entry 12).
 *
 * arg1 / arg2 are the two values the THINK C runtime passes; they flow
 * through to the screen and secondary init.
 */
static void port_show_intro(void);          /* title / credits sequence */
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

	/* Title / credits intro — the SSI / AD&D / Forgotten Realms / Unlimited
	 * Adventures / credits screens (TITLE.CTL art) shown before the menu.
	 * No-ops when the design data isn't mounted (e.g. a plain `make run`).
	 * Skipped in the 3D-walk demo build so it lands straight in the view. */
#ifndef FRUA_MAP_DEMO
	port_show_intro();
#endif

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
#ifdef FRUA_L6234_VERIFY
	port_l6234_verify();    /* never returns — geometry check for L6234 */
#endif
#ifdef FRUA_CHARGEN
	/* Jump straight to character generation (jt574) to verify the GLIB
	 * glyph blit (markers/buttons/frame via L148a/jt76 -> L309c -> L2d4e).
	 * Opt in with `make EXTRA_CFLAGS=-DFRUA_CHARGEN`. Never returns. */
	(void)jt574(0);
	for (;;)
		jt920();
#endif
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
/* Port character-management screens (defined further down, used by the
 * jt918 case handlers above their definitions). */
static void          cg_view_sheet(void);
static void          cg_modify_sheet(void);
static void          cg_add_character(void);
static void          cg_remove_from_party(void);
static void          cg_delete_character(void);
static short         cg_collect_party(unsigned char **out, short max);
static void          cg_message(const char *l1, const char *l2);
static void          save_roster(void);
extern short         ua_rand(short n);   /* rand.h — CODE 5 / JT[1083] LCG */
void                 port_begin_adventure(void);
/* JT[937] (CODE 12 + 0x02dc, 28 sites) — public alias for L02dc
 * (Modify Character roster grid, lifted further down). */
static void          l02dc(long highlight);
static void          jt937(long a)
{
	PROBE("jt937");
	l02dc(a);
}
static void          jt938(void);                                                /* CODE 12 + 0x0562 — clock/position HUD, lifted after its deps */
static void          jt217(short a, short b, short c, short d) { PROBE("jt217"); }
                                                                                 /* CODE 7 + 0x57d2 */
/* jt948's callees not yet declared at this point. */
static void  jt19(short a, short b);
static void  jt20(void);
static void  jt23(void);
static int   l5baa(short row, short col);                 /* CODE 7+0x5baa — cell bounds test */
static void  jt214(void);                                 /* CODE 7+0x71c6 — view setup */
static void  jt80(short arg);                             /* CODE 6+0x68ae */
static int   jt108(short a);                              /* CODE 6+0x38d0 */
static int   jt117(void);                                 /* CODE 6+0x3994 */
static short l5368(short span, short dim, signed char *coord,
                   short origin, short wrap);              /* CODE 7+0x5368 — toroidal mover */
/* L50fe area-map render callees, defined further down. */
static short jt397(short a, short b);
static short jt413(short a, short b);
static void  jt1135(short v1, short v2, short *out1, short *out2);
static void  jt1161(short top, short left, short bottom, short right, short fill);
static void  jt448(short x, short y, short color, short glyph);
static short l5e52(short row, short col, short dir);
static void  jt124(long h);                               /* CODE 6+0x3eea — free art handle */
static void  jt1173(short top, short left, short bottom, short right); /* CODE 4+0x... — set clip */
static void  l5b42(unsigned char *page, short y, short x, short ydelta,
                   short xdelta, short code, short sub);   /* CODE 7+0x5b42 — draw one wall slot */
static short jt212(short row, short col, short edge);      /* CODE 7+0x5cc8 — wall high nibble */
static void  jt199(unsigned char *page, short Y, short X, short row,
                   short col, short facing);                /* JT[199]=CODE 7+0x6234 frustum walker */
static void  render_3d_faithful(unsigned char *px, short pitch, short sw, short sh);
static void  port_draw_play_frame(unsigned char *px, short pitch, short sw, short sh);

/* jt221's inner renderers — the deep view-draw layer. PROBE stubs for now;
 * L6234 in particular is the ~1083-instruction first-person render (the
 * faithful equivalent of the port's jt312 / render_3d_* path) and is a lift
 * of its own. */
static long l37aa(long base_long, short idx);             /* CODE 5+0x37aa — GLIB item lookup */
static int  wallset_for_id(short id, short *file, short *set);  /* wall-set id -> (file,set) */

/* L6eea (CODE 7 + 0x6eea) — load a wall set's tile library into a per-group
 * tile-lib handle for the first-person view. In the DEEP view (jt1200()==3)
 * the walls are the 1bpp .TLB sets (8X8DB for wall-set ids 1..9, 8X8DC for
 * 10..16), each a GLIB-of-GLIBs; l37aa picks the set's 48-tile sub-GLIB,
 * stored in the handle table g_a5_-27894 + type*4, which jt114 -> l309c_tile
 * -> bp_blit blits 1:1. The Mac builds the name (JT[394]) + loads via JT[110],
 * choosing .ctl(colour)/.tlb(1bpp) by display mode; this focused load opens
 * the .TLB for the deep view and GLIB-indexes the set with the port's
 * primitives. The JT[111] synthesis of the bigger near tiles from the shipped
 * far ones is deferred (TODO). type 2 (backdrop) is skipped. */
/* The two colour wall libraries are cached one buffer PER FILE (not per
 * group): a level's three groups usually share a file (e.g. all in 8X8DB),
 * so three per-group 327KB buffers blew the heap and starved the last load.
 * Each file is read once; the per-group handle is just a sub-pointer into it. */
#define CW_FILEBUF_SZ 327680
static unsigned char *g_wallset_filebuf[2];      /* resident .CTL per file (heap) */
static long           g_wallset_filebase[2];     /* file GLIB base (0 = unloaded) */
static void l6eea(short zone, short type)
{
	/* The persistent HUD first-person view uses the COLOUR .CTL sets (8bpp,
	 * clut-129), all tiles present (no synthesis). 8X8DC for ids 1..9 vs DB
	 * picked by wallset_for_id's file. type 0/1/2 = the level's three wall
	 * groups (ds[4]=Wall1 -> -27894, ds[5]=Wall2 -> -27890, ds[6]=Wall3 ->
	 * -27886); jt200 folds a wall code's group and indexes -27894 + group*4. */
	static const char *const ctl[2] = { "\0118X8DC.CTL", "\0118X8DB.CTL" };
	short file = 0, set = 0, refnum = 0;
	long  count, base, sub;

	PROBE("L6eea");
	if ((zone & 0xff) == 255 || type < 0 || type > 2)
		return;
	if (!wallset_for_id(zone, &file, &set))
		return;
	file &= 1;
	if (g_wallset_filebase[file] == 0) {     /* load this .CTL once, resident */
		unsigned char *buf = g_wallset_filebuf[file];
		if (buf == NULL) {
			buf = (unsigned char *)NewPtr(CW_FILEBUF_SZ);
			g_wallset_filebuf[file] = buf;
		}
		if (buf == NULL)
			return;
		if (FSOpen((ConstStr255Param)ctl[file], 0, &refnum) != noErr)
			return;
		count = CW_FILEBUF_SZ;
		(void)FSRead(refnum, &count, buf);
		(void)FSClose(refnum);
		if (l37aa((long)(uintptr_t)buf, 0) == 0)   /* validate 'GLIB' magic */
			return;
		g_wallset_filebase[file] = (long)(uintptr_t)buf;
	}
	base = g_wallset_filebase[file];
	sub = l37aa(base, set);                   /* the set's 48-tile sub-GLIB */
	if (sub != 0)
		g_a5_long(-27894 + (long)type * 4) = sub;
}

/* L6148 (CODE 7 + 0x6148) — load the current level's 3D art. Gated on the
 * party's view-distance setting (record[19] >= 5). Lazily (re)loads, keyed by
 * the level's zone bytes, via L6eea(zone, type):
 *   lvl[6] (Wall3) when it changed since -12296,
 *   lvl[4] (Wall1) into handle -27894,
 *   lvl[5] (Wall2) into handle -27890.
 *
 * LEVEL-CHANGE RELOAD: the Mac disposes -27894/-27890 every frame (L6234's
 * prologue) and lets the Resource Manager cache the reload, so each frame
 * picks up the current level's sets for free. Re-reading the 296KB .CTL per
 * frame is far too slow here, so instead we track the loaded Wall1/Wall2 ids
 * (s_w1/s_w2) and clear the handle when the id changes — l6eea then reloads
 * the new set (the per-file buffer stays cached; only the sub-GLIB pointer is
 * recomputed). Wall3 already reloads on change via the -12296 key.
 *
 * The backdrop-bitmap reload dance (JT[468]/JT[1004]/JT[459]/JT[405]/JT[115]
 * on the -27886 handle) is the deferred deep layer. */
static void l6148(void)
{
	const unsigned char *h = (const unsigned char *)g_a5_28006;
	const unsigned char *lvl;
	static short s_w1 = -1, s_w2 = -1;      /* last-loaded Wall1/Wall2 ids */

	PROBE("L6148");
	jt131(0);
	if (h == NULL || h[19] < 5)             /* view-distance gate */
		return;
	lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	if (lvl == NULL)
		return;

	if (lvl[6] != 0 && (short)lvl[6] != (short)g_a5_word(-12296)) {
		l6eea((short)lvl[6], 2);        /* wall set 3 (Wall3) -> handle -27886 */
		/* TODO: the Mac post-processes -27886 (JT[468]/JT[1004]/JT[459]/
		 * JT[406]/JT[115]) after the load — deferred; the raw set suffices. */
		g_a5_word(-12296) = (short)lvl[6];
	}
	if ((short)lvl[4] != s_w1) {            /* Wall1 changed -> force reload */
		g_a5_long(-27894) = 0;
		s_w1 = (short)lvl[4];
	}
	if ((short)lvl[5] != s_w2) {            /* Wall2 changed -> force reload */
		g_a5_long(-27890) = 0;
		s_w2 = (short)lvl[5];
	}
	if (lvl[4] != 0 && g_a5_long(-27894) == 0)
		l6eea((short)lvl[4], 0);        /* wall set 1 */
	if (lvl[5] != 0 && g_a5_long(-27890) == 0)
		l6eea((short)lvl[5], 1);        /* wall set 2 */
}

/* L52b8 (CODE 7 + 0x52b8) — step the area-map scroll origin. For each axis it
 * runs the toroidal mover L5368 (span, the map dimension, the party coord,
 * the current window origin, wrap) to get the new top-left origin; when that
 * differs from the window-origin global (-12278 axis 1 / -12276 axis 2) it
 * flags a change and stores the new origin. Optionally writes the new origins
 * back through out1/out2 (jt221 passes NULL). Returns 1 if either moved.
 * L5368 also wraps the coord in place. */
static signed char l52b8(signed char *coord1, signed char *coord2,
                         short *out1, short *out2,
                         short span1, short span2, short wrap)
{
	const unsigned char *lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	signed char changed = 0;
	short n1, n2;

	PROBE("L52b8");
	n1 = l5368(span1, (short)lvl[2], coord1, (short)g_a5_word(-12278), wrap);
	if (n1 != (short)g_a5_word(-12278)) { changed = 1; g_a5_word(-12278) = n1; }
	n2 = l5368(span2, (short)lvl[3], coord2, (short)g_a5_word(-12276), wrap);
	if (n2 != (short)g_a5_word(-12276)) { changed = 1; g_a5_word(-12276) = n2; }
	if (out1 != NULL) *out1 = n1;
	if (out2 != NULL) *out2 = n2;
	return changed;
}
/* L5bfa (CODE 7 + 0x5bfa) — the wall-style code at cell (row,col) edge `dir`.
 * For an odd dir (an edge between two cells) it first steps to the neighbour
 * cell via the direction tables (-27862 drow / -27853 dcol) and normalises the
 * edge; bounds-tests with L5baa (0 outside the map); then returns the high
 * nibble of the cell record's edge byte (lvl[290 + (lvl[3]*col+row)*6 + dir/2]
 * >> 4) — the same record layout jt205/jt212 read. */
static short l5bfa(short row, short col, short dir)
{
	const unsigned char *lvl;
	long  idx;
	short eidx;

	PROBE("L5bfa");
	if (dir & 1) {                                  /* edge between cells */
		dir--;
		row = (short)(row + (signed char)g_a5_byte(-27862 + dir));
		col = (short)(col + (signed char)g_a5_byte(-27853 + dir));
		dir = (short)((dir + 2) & 6);
	}
	if (l5baa(row, col) == 0)
		return 0;
	eidx = (short)((dir & 6) >> 1);
	lvl  = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	idx  = (long)((short)lvl[3] * col + row);
	return (short)((lvl[290 + idx * 6 + eidx] >> 4) & 15);
}

/* L5484 (CODE 7 + 0x5484) — classify map cell (row,col) edge `dir` for the
 * area map: 0 = nothing (no edge present, L5e52==0), else map the wall-style
 * code (L5bfa, 0..15) to 2 for the "open/secret" styles {1,3,5,14} and 1 for
 * every other present style. JT[3] table @0x54ba. */
static short l5484(short row, short col, short dir)
{
	short t;

	PROBE("L5484");
	if (l5e52(row, col, dir) == 0)
		return 0;
	t = (short)(l5bfa(row, col, dir) & 0xff);
	switch (t) {                                    /* JT[3] @0x54ba */
	case 1: case 3: case 5: case 14:
		return 2;
	case 0:
		return 0;
	default:
		return 1;
	}
}

/* L54f2 (CODE 7 + 0x54f2) — draw one area-map cell at screen (sy,sx). Queries
 * the cell (L5484, edge 8); empty -> nothing. Otherwise paints the cell's top
 * edge as a 1px bar (fill 7), and for a style-1 cell adds a short centre mark
 * (fill 8) — this is the dashed top-down automap look. */
static void l54f2(short maprow, short mapcol, short sy, short sx)
{
	short cs    = (short)g_a5_word(-12272);         /* cell size */
	short right = (short)(sx + cs);
	short half  = (short)((cs - 2) / 2);
	short t;

	PROBE("L54f2");
	t = l5484(maprow, mapcol, 8);
	if (t == 0)
		return;
	jt1161(sy, sx, (short)(sy + 1), right, 7);      /* top bar */
	if (t == 1) {
		short x0 = (short)(sx + half);
		jt1161(sy, x0, (short)(sy + 1), (short)(x0 + 2), 8);
	}
}

/* L5752 (CODE 7 + 0x5752) — draw the party marker on the area map. Places it
 * at the view origin + the party's window-relative cell, picks the facing
 * arrow glyph (17 + facing/2, +4 for the 12px cell size), and blits it via
 * JT[448] in colour 12. */
static void l5752(short rrow, short rcol, short vy, short vx, short facing)
{
	short cs    = (short)g_a5_word(-12272);
	short sy    = (short)(vy + rrow * cs);
	short sx    = (short)(vx + rcol * cs);
	short glyph = (short)(((cs == 12) ? 4 : 0) + ((facing & 6) >> 1) + 17);

	PROBE("L5752");
	jt448(sy, sx, 12, glyph);
}

/* L50fe (CODE 7 + 0x50fe) — paint the top-down area map. Transforms the view
 * anchor (p4/p5, 8000-space) to screen via JT[397]/JT[1135], fills the view
 * interior (JT[1161], colour 8), stores the view origin (-12282/-12280) and
 * window dimensions in cells (-12274/-12273 = JT[413](JT[397](span),mapdim)),
 * then loops the visible window drawing each cell (L54f2) offset by the scroll
 * origin (-12278/-12276). Finally, if p8 is set, draws the party marker
 * (L5752). p9 gates the JT[108] dialog-suppress bracket. */
static void l50fe(short y, short x, short facing, short p4, short p5,
                  short p6, short p7, short p8, short p9)
{
	const unsigned char *lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short oy, ox;                                   /* view origin (screen) */
	short r, c, sy;

	PROBE("L50fe");
	if ((p9 & 0xff) == 0)
		jt108(1);
	if (jt1200() == 3)
		g_a5_word(-12272) = 16;

	oy = jt397(p4, 8000);
	ox = jt397(p5, 8000);
	jt1135(oy, ox, &oy, &ox);                       /* 8000-space -> screen */
	if (jt1200() == 3) { oy = (short)(oy - 28); ox = (short)(ox - 28); }

	jt1161(oy, ox,
	       (short)(p6 * (short)g_a5_word(-12272) + oy),
	       (short)(p7 * (short)g_a5_word(-12272) + ox), 8);
	if ((p9 & 0xff) == 0)
		jt108(1);

	g_a5_word(-12282) = oy;
	g_a5_word(-12280) = ox;
	g_a5_byte(-12274) = (unsigned char)jt413(jt397(p6, 1), (short)lvl[2]);
	g_a5_byte(-12273) = (unsigned char)jt413(jt397(p7, 1), (short)lvl[3]);

	sy = oy;
	for (r = 0; r < (short)(unsigned char)g_a5_byte(-12274); r++) {
		for (c = 0; c < (short)(unsigned char)g_a5_byte(-12273); c++) {
			short maprow = (short)(r + (short)g_a5_word(-12278));
			short mapcol = (short)(c + (short)g_a5_word(-12276));
			short sx = (short)(c * (short)g_a5_word(-12272) + ox);
			l54f2(maprow, mapcol, sy, sx);
		}
		sy = (short)(sy + (short)g_a5_word(-12272));
	}

	if ((p8 & 0xff) != 0)
		l5752((short)(y - (short)g_a5_word(-12278)),
		      (short)(x - (short)g_a5_word(-12276)),
		      (short)g_a5_word(-12282), (short)g_a5_word(-12280), facing);

	if (jt1200() == 3)
		g_a5_word(-12272) = 16;
	(void)jt117();
}
static void  l57f2(void)              { PROBE("L57f2"); } /* CODE 7-local — dungeon-view prep */
static void  jt44(void)               { PROBE("jt44"); }   /* CODE 6+0x5822 — full play-screen redraw */
static void  l2cf4(void)              { PROBE("L2cf4"); }  /* CODE 12-local — redraw tail */

/* JT[221] (CODE 7 + 0x6076) — render the play view at the party position
 * (x,y,facing). Brackets the draw with JT[131]/JT[80] (begin) and dispatches
 * on g_a5_-12290 (the overland/automap flag):
 *   != 0  -> area-map: L52b8 steps the scroll origin (it writes back through
 *            &x/&y), then L50fe paints the top-down map; x/y are restored.
 *   == 0  -> dungeon: JT[108](1), L57f2 preps, then L6234 paints the first-
 *            person 3D view anchored at 8012/8012 (8000-space), JT[117] ends.
 * The renderers (L6234/L52b8/L50fe/L57f2) are the deferred deep layer. */
static void jt221(short x, short y, short facing)
{
	PROBE("jt221");
	jt131(0);                               /* begin / lock the port */
	jt80(0);

	if (g_a5_byte(-12290) != 0) {           /* L608e — area-map / overland */
		/* L52b8 wraps/steps the coords in place; L50fe paints with the
		 * stepped values. The party x/y aren't returned, so the asm's
		 * save/restore of them is a no-op here. */
		signed char cy = (signed char)y, cx = (signed char)x;
		l52b8(&cy, &cx, NULL, NULL, (short)11, (short)11, (short)1);
		l50fe((short)cy, (short)cx, facing, (short)8012, (short)8012,
		      (short)11, (short)11, (short)1, (short)0);
	} else {                                /* L610a — first-person dungeon */
		unsigned char *page; short pitch, sw, sh;
		jt108(1);
		l57f2();
		/* The faithful first-person view: render_3d_faithful does the full
		 * frame-integrated draw — load_wall_groups (the three Wall1-3 CLUTs at
		 * clut 32/64/96), L6148 (the -27894 tile-lib handles), the viewport
		 * clip + backdrop, and jt199 (= L6234's frustum walk). It reads the
		 * party from g_a5_-12286/-12287/-12288 (same source as jt221's args).
		 * (page is the port's drawing surface the Mac draws to the port.) */
		if (qd_screen_pixels(&page, &pitch, &sw, &sh) && page != NULL)
			render_3d_faithful(page, pitch, sw, sh);
		(void)jt117();
	}
}

/* JT[201] (CODE 7 + 0x5f6a) — return the special-feature byte of map cell
 * (x,y). L5baa bounds-tests the cell; out of range -> 0. Otherwise it indexes
 * the level's 6-byte cell records (base lvl+290, same layout jt205/jt212 read)
 * at field +4: lvl[290 + (lvl[3]*y + x)*6 + 4]. jt948/L709e use it to apply
 * the cell the party stands on (special encounters / triggers). */
static short jt201(short x, short y)
{
	const unsigned char *lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	long idx;

	PROBE("jt201");
	if (lvl == NULL || l5baa(x, y) == 0)            /* L5f88 — out of bounds */
		return 0;
	idx = (long)((short)lvl[3] * y + x);            /* cell index (col=y, row=x) */
	return (short)lvl[290 + idx * 6 + 4];           /* a0@(294): record field +4 */
}

/* JT[935] (CODE 12 + 0x4) — the play-screen refresh. Skips while -27982 is
 * set (mid transition). On first entry (record[34]==0, record[36]==1, dirty
 * flag -23188 set) it does the full chrome redraw (JT[214] view setup +
 * JT[44] + L2cf4); otherwise, when in a render mode (-27990==4, or ==3 unless
 * record[36]==1), it renders the first-person/area view at the party position
 * via JT[221](x,y,facing). Clears the dirty flag after either render. */
static void jt935(void)
{
	unsigned char *h;

	PROBE("jt935");
	if (g_a5_byte(-27982) != 0)                     /* L0080 — suppressed */
		return;

	h = (unsigned char *)g_a5_28006;
	if (h != NULL && h[34] == 0 && h[36] == 1
	 && g_a5_byte(-23188) != 0) {                   /* first-entry full redraw */
		jt214();
		jt44();
		l2cf4();
		g_a5_byte(-23188) = 0;                  /* L007c */
		return;
	}

	/* L003e: render the view when in a play render mode. */
	if (g_a5_27990 == 4
	 || (g_a5_27990 == 3 && !(h != NULL && h[36] == 1))) {
		jt221((short)(signed char)g_a5_12288,
		      (short)(signed char)g_a5_12287,
		      (short)(signed char)g_a5_12286);
		g_a5_byte(-23188) = 0;                  /* L007c */
	}
}
static void  jt955(void) __attribute__((unused));
static void  jt955(void)              { PROBE("jt955"); }   /* CODE 21+0x453c — used by a deferred jt948 arm */
static void  l0006_20(void)           { PROBE("L0006_20"); } /* CODE 20+0x6 — post-load init */
static void  l709e(short a)           { PROBE("L709e"); (void)a; }  /* CODE 20-local — apply cell special */
static void  l473e(short a)           { PROBE("L473e"); (void)a; }  /* CODE 20-local */
static void  l47f2(void)              { PROBE("L47f2"); }   /* CODE 20-local */
static signed char l4738(void)        { PROBE("L4738"); return 0; }  /* CODE 20-local */
static short jt240(short cmd, long *flagsp, unsigned char *rec);     /* deep walk loop (CODE 11+0x4ffe), defined below */
/* The command-bar index L63c0 latches on exit (jt152/l25b6 result, 0..7 =
 * Move/Area/Cast/View/Encamp/Search/Look/Inv), or -1 for an Esc / non-command
 * exit. jt948's dungeon walk loop reads it to dispatch the command. */
static short g_walk_cmd = -1;
static void  jt23(void);                                            /* play-frame stand-up (defined below) */
static void  jt904(unsigned char *out_done);                        /* View-character screen (defined below) */

/* JT[948] (CODE 20 + 0x4a12) — the adventure-level dungeon loop, the body
 * L07dc runs once the party is assembled. STRUCTURAL SKELETON (level 2): the
 * spine is faithful — (re)load the level (L0bbc), set up the play screen,
 * then spin the exploration command dispatcher (JT[953]) and react to its
 * result — but the intricate level-transition arms (the [133]/[134]/[49]
 * stair-direction + level-number scroll setup at 0x4ad6..0x4be4, and the
 * saved-game party-handle walk) are translated to their spine and marked
 * TODO. JT[953] is the lifted play loop; L0bbc is the lifted level loader.
 *
 * Outer loop L4a1a reloads the level on a stair transition; the inner loop
 * L4be8 re-dispatches JT[953] until the player leaves the area. Result codes
 * from JT[953]: 4 = exit the adventure (L473e), 6 = area/stairs swap (reload),
 * else keep exploring. -27982 is the "active adventure" gate (set by jt918's
 * Begin-Adventuring path); -5221 forces an immediate area-leave. */
static void jt948(void)
{
	unsigned char *h;
	unsigned char *node;
	signed char    res;             /* fp@(-1): JT[953] result   */
	signed char    want = 1;        /* fp@(-7)                   */
	short          special;         /* fp@(-8): JT[201] cell code */

	PROBE("jt948");

	/* PORT DEVIATION (native 320x200): the Mac runs the dungeon screen at
	 * g_a5_2347=0 (jt1135 scale 3 = its 640x400 doubled space). Natively we
	 * want the command bar / HUD at scale 2 (the layout the menu uses), so
	 * run the play screen at g_a5_2347=1; the first-person view still renders
	 * deep via g_cwf_force_deep (see render_3d_faithful). */
	g_a5_2347 = 1;

	for (;;) {                                      /* L4a1a — level (re)load */
		g_a5_long(-5218) = g_a5_long(-27932);
		g_a5_byte(-23188) = 1;
		g_a5_byte(-5220) = 0;
		g_a5_byte(-5221) = 0;
		jt942(0);

		h = (unsigned char *)g_a5_28006;
		if (h != NULL && h[22] != 0) {          /* L4a64 — keep current zone */
			g_a5_byte(-22285) = h[22];
		} else if (g_a5_byte(-27988) != 0) {    /* saved game */
			g_a5_byte(-22285) = 7;
		} else {                                /* fresh: zone 4 + roster */
			g_a5_byte(-22285) = 4;
			jt937(g_a5_long(-27932));
		}
		if (h != NULL && h[34] == 0)            /* L4a82 */
			g_a5_27990 = 3;

		/* TODO: faithful L0bbc takes the zone (-22285) as a word arg; the
		 * port's l0bbc() reads the level/zone from globals instead, so the
		 * arg is dropped here until l0bbc grows the parameter. */
		l0bbc();                                /* load the level + place party */
		l0006_20();
		l0bbc();                                /* faithful: L0bbc runs twice */

		if (g_a5_byte(-27988) != 0) {           /* saved-game: walk the handle list */
			node = (unsigned char *)(uintptr_t)g_a5_long(-27928);
			while (node != NULL) {          /* L4ab0 */
				g_a5_long(-27932) = (long)(uintptr_t)node;
				node = *(unsigned char **)node;
				jt19(1, 1);
			}
			break;                          /* L4d22 — exit */
		}

		/* L4ad6 — the level-entry / stair-transition arms (0x4ad6..0x4be4).
		 * h[133] = pending stair-scroll direction, h[134] = "view
		 * established" flag, h[49] = level-number special; -18878 = current
		 * level (>=5 = dungeon), -27990 = play render mode. These set the
		 * dungeon render mode (4), draw the entry view, and apply the special
		 * of the cell the party lands on (L709e) — picking the arm by which of
		 * h[134]/h[133]/h[49] is live. Without the -27990=4 dungeon-mode setup
		 * jt953's mode switch falls through and the play loop escapes. */
		h = (unsigned char *)g_a5_28006;

		/* 4ad6: not mode 3, and the view already established -> set dungeon
		 * mode and (when no stair scroll is pending) redraw. */
		if (g_a5_27990 != 3 && h != NULL && h[134] != 0) {
			if ((short)g_a5_18878 >= 5)
				g_a5_27990 = 4;
			if (h[133] == 0) {              /* L4af6 */
				jt23();
				g_a5_byte(-23188) = 1;
				jt935();
			}
		}

		/* L4b0e: a dungeon entered fresh (no view yet) -> mode 4 + jt23. */
		if ((short)g_a5_18878 >= 5 && h != NULL && h[134] == 0) {
			g_a5_27990 = 4;
			jt23();
		}

		/* L4b2a / L4b3e: mark dirty, reset the move flag, draw the view
		 * (unless a stair scroll is pending on h[133]). */
		g_a5_byte(-23188) = 1;
		if (h != NULL && h[133] == 0)
			jt935();
		g_a5_byte(-22268) = 0;
		want = 1;
		if (h != NULL && h[133] == 0)
			jt935();

		/* L4b56: pick the entry arm and apply the landing cell's special. */
		if (h != NULL && h[134] == 0) {        /* fresh entry */
			special = jt201((short)(signed char)g_a5_12288,
			                (short)(signed char)g_a5_12287);
			l709e(special);
			h[134] = 1;
			if (g_a5_byte(-27982) == 0) {
				jt938();                /* status / clock line */
				jt935();
			}
		} else if (h != NULL && h[133] != 0) { /* L4ba6 — stair direction */
			l709e((short)h[133]);
		} else if (h != NULL && h[49] != 0) {  /* L4bc6 — level-number */
			l709e((short)h[49]);
		}

		for (;;) {                              /* L4be8 — the play loop */
			g_a5_byte(-24140) = want;
			if ((short)g_a5_18878 >= 5) {
				/* DUNGEON: run the faithful unified walk + command loop
				 * (jt240 -> l63c0, a_deep=1). It registers the walk input
				 * sources (l6256) AND the command bar (jt179/jt148), then
				 * l63c0 polls both: arrow / move keys step the party (case 0
				 * -> jt297 -> l1908 -> jt312 re-render), command-bar picks end
				 * the loop. jt240 commits the party position to -12288 on exit.
				 * This is the real movement loop CODE 20/21's jt953 (command/
				 * turn only) never provided; for now its return leaves the area
				 * (the per-command menu dispatch is the next wiring step). */
				static unsigned char play_rec[342];
				long  pflags = 0;
				short kk;
				memset(play_rec, 0, sizeof play_rec);
				play_rec[4] = 1;            /* dungeon kind */
				for (kk = 0; kk < 6; kk++)
					play_rec[46 + kk] = g_a5_byte(-12288 + kk);
				g_walk_cmd = -1;
				(void)jt240((short)11, &pflags, play_rec);
				/* Dispatch the latched command bar pick (0..7 = Move/Area/
				 * Cast/View/Encamp/Search/Look/Inv). Encamp (4) or an Esc /
				 * non-command exit (-1) leaves the area; every other command
				 * runs its action (the lifted View/Inv; Area/Cast/Search/Look
				 * are still stubbed) and keeps exploring. This mirrors jt953's
				 * command switch but on l63c0's unified walk+command loop. */
				if (g_walk_cmd == 4 || g_walk_cmd < 0) {
					res = 4;            /* Encamp / Esc -> leave the area */
				} else {
					switch (g_walk_cmd) {
					case 3: {           /* View character */
						unsigned char done = 0;
						jt904(&done);
						break;
					}
					case 7:             /* Inventory */
						jt23();
						break;
					default:            /* Move / Area / Cast / Search / Look */
						break;          /* (actions TODO) — stay in the dungeon */
					}
					continue;           /* re-enter the walk loop */
				}
			} else {
				res = (signed char)jt953(); /* overland command dispatch */
			}
			want = (signed char)g_a5_byte(-24140);
			g_a5_long(-5218) = g_a5_long(-27932);
			if (g_a5_byte(-5221) != 0)
				break;                  /* immediate area-leave */

			if (res == 4) {                 /* L4c18 — exit adventure */
				l473e(1);
				break;
			}
			if (res == 6) {                 /* L4c3c — area/stairs swap */
				h = (unsigned char *)g_a5_28006;
				if (h != NULL) {
					g_a5_byte(-22284) = (unsigned char)(h[25] & 1);
					h[25] = 1;
				}
				g_a5_byte(-23188) = 1;
				jt935();
				l47f2();
				h = (unsigned char *)g_a5_28006;
				if (h != NULL)
					h[25] = g_a5_byte(-22284);
				break;
			}
			if (g_a5_byte(-27982) != 0)     /* still adventuring -> keep playing */
				continue;
			break;
		}

		/* L4cda: reload the level for a stair transition while the
		 * adventure is still active; otherwise fall through to exit. */
		if (g_a5_byte(-27982) != 0) {
			g_a5_byte(-27982) = 0;
			continue;                       /* -> L4a1a reload */
		}

		/* exit: walk the party-handle list (JT[19]) then JT[582]. */
		node = (unsigned char *)(uintptr_t)g_a5_long(-27928);
		while (node != NULL) {                  /* L4cf6 */
			g_a5_long(-27932) = (long)(uintptr_t)node;
			node = *(unsigned char **)node;
			jt19(0, 1);
		}
		if (l4738() != 0)
			jt582();
		break;
	}
}
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

/* JT[CHAR_AC] / L46b2 (CODE 3 + 0x46b2) — tolower. JT[CHAR_AC] route lands here. */
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
static long port_ui_group_base(short group);   /* groups 0/1 -> ALWAYS/FRAME GLIB */

static long jt468(short tag)
{
	short id;
	long  ui;

	PROBE("jt468");
	if (tag < 0 || (unsigned short)tag >= (unsigned short)G_A5_10074_LEN)
		return 0;
	/* Groups 0 (ALWAYS.CTL) and 1 (FRAME.CTL) hold the UI glyphs — button
	 * faces, radio markers, frame bevel edges (see [[glib-resource-groups]]).
	 * The faithful jt997 group loader isn't wired in the port, so resolve
	 * these two from the resident GLIB buffers the port already loads, so the
	 * L148a/jt76 -> L309c glyph blit can find them. Other groups fall through
	 * to the engine resource cache. */
	ui = port_ui_group_base(tag);
	if (ui != 0)
		return ui;
	id = (signed char)g_a5_10074[tag];
	if (id < 0 || (unsigned short)id >= (unsigned short)G_A5_10270_LEN)
		return 0;
	return g_a5_10270[id];
}

/* Forward decls for the GLIB glyph blit entry below (full bodies later). */
static long l2856(long font_handle, short size, void *out_8bytes);
static void jt1135(short v1, short v2, short *out1, short *out2);

/* l2d4e (CODE 5 + 0x2d4e) — the GLIB bitmap blit dispatcher + clipper,
 * the leaf L309c delegates the actual pixel write to.
 *
 * Faithful to L2d4e's prologue: reject when the (y,height)x(x,width)
 * rectangle falls fully outside the QuickDraw clip rect (top=g_a5_-3054,
 * bottom=-3050, left=-3056, right=-3052), then dispatch on the glyph's
 * mode nibble (metric[7] & 15):
 *   - 2  colour-map blit        (Mac L2bfc)
 *   - 3  multi-source composite (Mac L289a loop)
 *   - 7  transparency RLE       (Mac L2b9a, JT[1195])
 *   - 10 horizontal wrap        (Mac self-recursion)
 *   - else  the mono OR leaf    (Mac L2970 default arm)
 *
 * The Mac leaf (L2970) writes through a family of page-descriptor row
 * writers (JT[1165/1172/1176/1202]) that scale by jt1200() for the
 * 320->640 doubling. The port renders native 1:1 into the 8bpp shim
 * surface (per the all-320x200 decision), so the mono arm here is the
 * straight per-pixel copy into that surface.
 *
 * Glyph storage (confirmed from ALWAYS.CTL / FRAME.CTL item sizes): the
 * resident UI GLIBs are 8bpp COLOUR — one CLUT index per pixel, byte 255
 * = transparent, width_px = bpp_w*8 = bytes per row (the flags 0x40 bit,
 * set on every UI item, marks colour; the dungeon's 1bpp DUNGCOM tiles
 * clear it). So the common arm here is the 8bpp 255-keyed copy (Mac mode
 * 5 -> JT[1190]); mode 0 (opaque frame edges) shares it harmlessly.
 * Mode 2 (Mac L2bfc) is PackBits-compressed 8bpp colour, decoded per row
 * via jt1171 (_UnpackBits) — the FRAME.CTL top/bottom bars and the play
 * command bar. The remaining arms — 7 = RLE transparency (L2b9a/
 * JT[1195]), 3 = composite, 10 = wrap — stay deferred. A 1bpp source
 * (flags 0x40 clear) falls back to the mono OR leaf in the current QD
 * foreground colour. */
static void l2d4e(const unsigned char *src, short bpp_w, short height,
                  short y, short x, short flags)
{
	short top    = g_a5_3054;
	short bottom = g_a5_3050;
	short left   = g_a5_3056;
	short right  = g_a5_3052;
	short pix_w  = (short)(bpp_w * 8);
	short mode   = (short)(flags & 15);
	unsigned char *px;
	short pitch, sw, sh;
	short r, c;

	if (src == NULL || height <= 0 || bpp_w <= 0)
		return;

	/* clip-reject (L2d4e 0x2d52..0x2d8c) */
	if (y >= bottom || y + height <= top)
		return;
	if (x >= right || x + pix_w <= left)
		return;

	if (mode == 3 || mode == 7 || mode == 10) {
		PROBE("l2d4e-mode");          /* deferred composite/RLE/wrap arms */
		return;
	}

	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == NULL)
		return;
	if (top < 0)     top = 0;
	if (left < 0)    left = 0;
	if (bottom > sh) bottom = sh;
	if (right > sw)  right = sw;

	if (mode == 2) {
		/* PackBits-compressed 8bpp colour rows (Mac L2bfc): each row is
		 * RLE-packed; jt1171 (_UnpackBits) expands pix_w bytes and returns
		 * the advanced source, so rows are decoded in sequence. 255 =
		 * transparent. The scratch is oversized to absorb a trailing run
		 * that overshoots pix_w (jt1171 emits the full run, as the Mac
		 * trap does). */
		unsigned char rowbuf[640];
		const unsigned char *s = src;

		if (pix_w > 512)                  /* keep the run overshoot in bounds */
			return;
		for (r = 0; r < height; r++) {
			short dy = (short)(y + r);

			s = (const unsigned char *)jt1171(s, rowbuf, pix_w);
			if (dy < top || dy >= bottom)
				continue;
			for (c = 0; c < pix_w; c++) {
				unsigned char v = rowbuf[c];
				short dx;

				if (v == 255)
					continue;
				dx = (short)(x + c);
				if (dx < left || dx >= right)
					continue;
				px[(long)dy * pitch + dx] = v;
			}
		}
		return;
	}

	if (flags & 0x40) {
		/* 8bpp colour glyph: stride = pix_w bytes, 255 = transparent. */
		for (r = 0; r < height; r++) {
			short dy = (short)(y + r);
			const unsigned char *srow = src + (long)r * pix_w;

			if (dy < top || dy >= bottom)
				continue;
			for (c = 0; c < pix_w; c++) {
				unsigned char v = srow[c];
				short dx;

				if (v == 255)
					continue;
				dx = (short)(x + c);
				if (dx < left || dx >= right)
					continue;
				px[(long)dy * pitch + dx] = v;
			}
		}
	} else {
		/* 1bpp mono glyph: set bits -> fgColor (Mac L2970 mode-0 OR). */
		GrafPtr port;
		unsigned char fg = 0;

		GetPort(&port);
		if (port != NULL)
			fg = ((CGrafPtr)port)->fgColor;
		for (r = 0; r < height; r++) {
			short dy = (short)(y + r);
			const unsigned char *srow = src + (long)r * bpp_w;

			if (dy < top || dy >= bottom)
				continue;
			for (c = 0; c < pix_w; c++) {
				short dx;

				if (!(srow[c >> 3] & (0x80 >> (c & 7))))
					continue;
				dx = (short)(x + c);
				if (dx < left || dx >= right)
					continue;
				px[(long)dy * pitch + dx] = fg;
			}
		}
	}
}

/* L309c (CODE 5 + 0x309c) = JT[999] — the UI GLIB glyph blit entry.
 *
 * Faithful to L309c's prologue (0x30a0..0x30ea): remap the pen (a=top,
 * b=left) through jt1135, fetch glyph item `size` from `handle` via
 * l2856 (8-byte metric + the 1bpp bits), early-out when absent, then
 * back off the pen by the glyph bearings (ybear @2, xbear @4). A mode-9
 * glyph is a multi-part composite (0x30f4: it walks 6-byte sub-part
 * descriptors via JT[406] and recurses) — deferred, the resident UI
 * glyphs are single-part. Everything else hands the decoded bitmap to
 * l2d4e. (Distinct from l309c_tile, the dungeon's 8bpp tile channel.) */
static void l309c(short a, short b, long handle, short size)
{
	unsigned char metric[8];
	long  info;
	short sy = a, sx = b;
	short ybear, xbear, mode, height, bpp_w;

	PROBE("L309c");

	jt1135(a, b, &sy, &sx);              /* 8000-space -> pixels */
	info = l2856(handle, size, metric);
	if (info == 0)
		return;
	ybear = (short)(((unsigned short)metric[2] << 8) | metric[3]);
	xbear = (short)(((unsigned short)metric[4] << 8) | metric[5]);
	sy = (short)(sy - ybear);
	sx = (short)(sx - xbear);

	mode = (short)metric[7];
	if ((mode & 15) == 9) {
		PROBE("l309c-composite");
		return;
	}

	height = (short)(((unsigned short)metric[0] << 8) | metric[1]);
	bpp_w  = (short)metric[6];
	l2d4e((const unsigned char *)(uintptr_t)info, bpp_w, height,
	      sy, sx, mode);
}

/* JT[1001] (CODE 5 + 0x31ac) — select GLIB resource group `c`, blit its
 * item `d` at pen (a, b).
 *
 *   movew fp@(12), sp@-     ; push c  (3rd arg = the group)
 *   jsr   JT[468]           ; base = jt468(c)
 *   ...
 *   jsr   L309c             ; l309c(a, b, jt468(c), d)
 *
 * jt76 calls jt1001(8000,8000,1,1..4) — FRAME.CTL (group 1) bevel
 * pieces; L148a calls jt1001(top,left,style,size) — ALWAYS.CTL markers/
 * buttons. (The earlier stub swapped args 2/3: it did jt468(b)+l309c(a,c)
 * — harmless while L309c was a no-op, fixed now that the blit is live.) */
static void jt1001(short a, short b, short c, short d)
{
	long base;

	PROBE("jt1001");
	base = jt468(c);                     /* c = resource group id */
	l309c(a, b, base, d);                /* d = glyph index in the group */
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

/* L148a (CODE 3 + 0x148a) — per-item glyph/marker paint dispatcher.
 *
 *   if (jt1200() == 3)  jt995(top, left, style, size_h, 2);   // deep
 *   else                jt1001(top, left, style, size_h);     // shallow
 *
 * Both arms blit GLIB glyph item `size_h` from group jt468(style) at
 * (top,left) via L309c. For a shape-3 pick row that glyph IS the radio
 * MARKER: L14d0 calls l148a(rec[16], rec[18], style=0, 16+hl), style 0 =
 * ALWAYS.CTL, and the marker states live at items 16..20 there —
 *   16 hl=0 available (white disc + black ring),
 *   17 hl=1 selected  (red centre dot),
 *   18 hl=2 unavailable (white disc, no ring),
 *   19/20 the focus variants.
 * Each is a 7-row 8bpp colour glyph (255-keyed) the L309c/L2d4e path now
 * blits straight into the shim surface — replacing the old port-collapse
 * diamond. The shallow/deep split is only the clip mode; jt995 carries
 * its own deep blit. */
static void l148a(short top, short left, short style, short size_high) __attribute__((unused));
static void l148a(short top, short left, short style, short size_high)
{
	PROBE("L148a");

	if (jt1200() == 3)
		(void)jt995(top, left, style, size_high, 2);   /* deep */
	else
		jt1001(top, left, style, size_high);           /* shallow */
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

static void jt103(short top, short left, short right, short bottom);  /* CODE 6 + 0x4bf6 */

static void jt76(void)
{
	/* CODE 6 + 0x670c — a fixed setup sequence: clear via jt108, paint the
	 * window box (jt103 = CODE 6 + 0x4bf6), then four channel-init calls
	 * through JT[1001], closing with jt174's two-byte flag.
	 *
	 * The Mac pushes 22,38,1,1 → jt103(1,1,38,22): the char-cell box
	 * (top=1,left=1,right=38,bottom=22) → the ~(8,8)-(312,184)px grey panel
	 * the PICK lists sit in. (An earlier cut called a stub l4bf6 with the
	 * args reversed, so the rect was degenerate and no panel drew.) */
	PROBE("jt76");
	(void)jt108(0);
	jt103((short)1, (short)1, (short)38, (short)22);
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

/* JT[96] (CODE 6 + 0x43c4, 43 sites) — the record-body word-wrap text
 * renderer (the row of stats below an entry's name, driven by jt18/jt20).
 * Lifted below, after its paint deps (jt1089/jt103/jt483/l3994). */
static void jt96(short page, short row, short width, short height,
                 short s5,   short s6,  short s7,
                 long  val,  short s9);

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

/* JT[913] (CODE 19 + 0x528) — format a 0..N number into buf with a leading
 * zero when < 10 (the minutes field of the game clock: "05", "45"). */
static void jt913(short val, char *buf)
{
	PROBE("jt913");
	jt394(buf, (val < 10) ? "0%d" : "%d", (int)val);
}

/* jt938 (CODE 12 + 0x562) — the play-screen clock + party-position panel
 * (the Mac HUD's "11,6 / 12:05 AM" box). Faithful lift: reads the game clock
 * from the player-data handle g_a5_-28006 (byte 8 = hour 0..23, bytes 7/6 =
 * minutes tens/ones), formats 12-hour AM/PM (jt913 zero-pads the minutes,
 * jt488 builds the string, jt384 copies it), boxes it with jt103, and draws
 * the time + the party cell (g_a5_-12288,-12287) via jt94. The deep/encounter
 * arm (g_a5_-27990 == 3, L0844) is deferred. */
static void jt938(void)
{
	const unsigned char *h = (const unsigned char *)g_a5_28006;
	char  timebuf[90];                      /* fp@(-90) */
	char  posbuf[48];                       /* fp@(-48) */
	char  minbuf[8];                        /* fp@(-6)  */
	short px, py, hour, minutes;

	PROBE("jt938");
	if (g_a5_27990 == 3)                    /* L0844 deep arm — deferred */
		return;
	if (h == NULL)
		return;

	px = (short)(unsigned char)g_a5_byte(-12288);   /* fp@(-1) party col */
	py = (short)(unsigned char)g_a5_byte(-12287);   /* fp@(-2) party row */

	minutes = (short)(h[7] * 10 + h[6]);
	jt913(minutes, minbuf);                 /* "MM" */
	jt103((short)26, (short)13, (short)38, (short)15);      /* clock box */

	hour = (short)h[8];
	if (hour / 12 == 0) {                   /* AM (hours 0..11) */
		if (hour == 0)
			jt384(timebuf, jt488("12:%s AM", minbuf));
		else
			jt384(timebuf, jt488("%d:%s AM", (int)hour, minbuf));
	} else {                                /* PM (hours 12..23) */
		if (hour == 12)
			jt384(timebuf, jt488("%d:%s PM", (int)hour, minbuf));
		else
			jt384(timebuf, jt488("%d:%s PM", (int)(hour - 12), minbuf));
	}
	jt94((short)26, (short)15, (short)7, (short)0, "%s", timebuf);

	if (h[26] == 0) {                       /* show the party cell */
		jt384(posbuf, jt488("%d,%d", (int)px, (int)py));
		jt94((short)26, (short)13, (short)7, (short)0, "%s", posbuf);
	}
}
static void jt97(short col, short row, short page, short style,
                 short a, short ch, short flag)
                                            { PROBE("jt97"); (void)col;
                                              (void)row; (void)page;
                                              (void)style; (void)a;
                                              (void)ch; (void)flag; }
/* jt103 (CODE 6 + 0x4bf6) — draw a dialog window box.  Scales the char-cell
 * rect (cols/rows) into the 8000-anchored coordinate space — each edge *4,
 * the top/left anchored at 8000 and the bottom/right at 8004 — and paints it
 * via the rect-fill primitive (the Mac routes through L3f88 → JT[1161]; L3f88
 * is a pure thunk, inlined here).  fill mode 8 is the dialog-box style.  The
 * 8000-space coords are turned into pixels by JT[1135] inside jt1161 (x2 in
 * dialog mode), so a 38x22 box lands as the ~304px-wide popup frame. */
static void jt1161(short top, short left, short bottom, short right, short fill);
static void jt1135(short v1, short v2, short *out1, short *out2);

/* Draw a 1px 3D bevel on the pixel rect [x1,x2) x [y1,y2). raised=0 gives the
 * LOWERED/inset look (dark top+left, light bottom+right); raised=1 the opposite.
 * The Mac frames its dialog boxes (jt76's GLIB edge pieces) and bevels its
 * buttons this way; this port-collapse strokes the edges into the HAL
 * framebuffer directly (same collapse as jt1089/jt1161). Still used for the
 * jt103 panel + jt382 button plates; the radio markers now come from the real
 * GLIB blit (L148a -> L309c -> L2d4e), and the frame/button glyphs are the
 * next pieces to migrate off this hack. */
static void draw_bevel(unsigned char *px, short pitch, short sw, short sh,
                       short x1, short y1, short x2, short y2, int raised)
{
	const unsigned char DARK = 0, LIGHT = 7;     /* black / light grey */
	unsigned char tl = raised ? LIGHT : DARK;    /* top + left edge */
	unsigned char br = raised ? DARK : LIGHT;    /* bottom + right edge */
	short i;

	if (x2 <= x1 || y2 <= y1)
		return;
	for (i = x1; i < x2; i++) {              /* top / bottom rows */
		if (i >= 0 && i < sw) {
			if (y1 >= 0 && y1 < sh)        px[(long)y1 * pitch + i] = tl;
			if (y2 - 1 >= 0 && y2 - 1 < sh) px[(long)(y2 - 1) * pitch + i] = br;
		}
	}
	for (i = y1; i < y2; i++) {              /* left / right cols */
		if (i >= 0 && i < sh) {
			if (x1 >= 0 && x1 < sw)        px[(long)i * pitch + x1] = tl;
			if (x2 - 1 >= 0 && x2 - 1 < sw) px[(long)i * pitch + (x2 - 1)] = br;
		}
	}
}

static void jt103(short top, short left, short right, short bottom)
{
	unsigned char *px;
	short pitch, sw, sh;
	short y1 = 0, x1 = 0, y2 = 0, x2 = 0;

	PROBE("jt103");
	(void)px; (void)pitch; (void)sw; (void)sh;
	(void)y1; (void)x1; (void)y2; (void)x2;
	/* Faithful: jt103 (L4bf6) is ONLY a jt1161 mode-8 box fill — the visible
	 * 3D frame comes from the FRAME.CTL edge glyphs jt76 blits (jt1001), not
	 * from jt103. The earlier port-added draw_bevel here stroked a second
	 * border at the panel edge, doubling the line next to the FRAME.CTL edge
	 * and making the frame look too wide; removed. */
	jt1161((short)(left   * 4 + 8000),    /* L3f88 arg1 (d0 = left)   */
	       (short)(top    * 4 + 8000),    /* L3f88 arg2 (d1 = top)    */
	       (short)(bottom * 4 + 8004),    /* L3f88 arg3 (d2 = bottom) */
	       (short)(right  * 4 + 8004),    /* L3f88 arg4 (d3 = right)  */
	       8);
}

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
	/* L78e8: remap (top, left) 8000-space -> pixel via L77fe (the same
	 * >6000 gate jt1135 uses; values <=6000 pass through as absolute
	 * pixels), then add the height/width — each likewise remapped when
	 * given in 8000-space. */
	short scale = (g_a5_2347 == 0) ? 3 : 2;
	short top2  = (top  > 6000) ? (short)((top  - 8000) * scale) : top;
	short left2 = (left > 6000) ? (short)((left - 8000) * scale) : left;

	PROBE("jt1141");
	if (out_bottom != NULL)
		*out_bottom = (h > 6000)
		    ? (short)((h - 8000) * scale + top2)
		    : (short)(top2 + h);
	if (out_right != NULL)
		*out_right = (w > 6000)
		    ? (short)((w - 8000) * scale + left2)
		    : (short)(left2 + w);
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

/* ===================================================================== *
 * jt96 — the record-body word-wrap text renderer (CODE 6 + 0x43c4), and
 * its leaf helpers. Flows a string into a char-grid cell rect, greedily
 * wrapping on word boundaries. The pen (column g_a5_-27912 / row -27911)
 * persists across calls so consecutive jt96 calls continue the same cell.
 * ===================================================================== */

/* JT[390] (CODE 3 + 0x3e3c) — scan `set` for `ch`; return a pointer to the
 * match, or to the terminating NUL when not found (strchr that yields the
 * NUL slot instead of NULL). */
static const char *jt390(const char *set, short ch)
{
	PROBE("jt390");
	while (*set != 0 && (unsigned char)*set != (unsigned char)ch)
		set++;
	return set;
}

/* L433a (CODE 6 + 0x433a) — is `ch` a word-break delimiter? Returns the char
 * (non-zero) when it's one of "()[]{}-.,?!\":;", else 0. */
static short l433a(short ch)
{
	PROBE("L433a");
	return (short)(unsigned char)*jt390("()[]{}-.,?!\":;", ch);
}

/* L42a0 (CODE 6 + 0x42a0) — draw `ch` `count` times from char-cell (col,row),
 * each glyph one cell (4 units in 8000-space) apart, in colour (s6<<4)|s5
 * (s6 defaults to style 8). The Mac passes jt1089 in Point (v,h) order; the
 * port's jt1089 takes (h,v), so x/y are swapped here as in jt94. */
static void l42a0(short col, short row, short s5, short s6,
                  short count, short ch)
{
	short ybase = (short)((row << 2) + 8000);
	short xbase = (short)((col << 2) + 8000);
	short i, color;

	PROBE("L42a0");
	if (s6 == 0)
		s6 = 8;
	color = (short)((s6 << 4) | (unsigned char)s5);
	for (i = 0; i < count; i++)
		jt1089((short)((i << 2) + xbase), ybase, color,
		       ua_strs_at(0x6c4) /* "%c" */, (int)(unsigned char)ch);
}

static void l435a(void);                /* CODE 6 — slow-text pacing (lifted after its deps) */
static void l4c46(void);                /* CODE 6 — pagination "press a key" pause (lifted later) */
static void jt176(void);                /* CODE 7+0x162e — scroll/clear (lifted below) */

/* jt96 (CODE 6 + 0x43c4) — render `val` word-wrapped into the cell rect
 * (a,b)-(c,d) in char coords; s6 = style/colour, s7 != 0 draws the box.
 * Lift level 3: the CFG mirrors the asm (L44b8..L4756) via labels; the
 * slow-text pacing (g_a5_-27981) and the overflow pagination prompt are the
 * only deferred arms (l435a / l4c46 stubs). */
static void jt96(short a, short b, short c, short d,
                 short s5, short s6, short s7, long val, short s9)
{
	const char *str = (const char *)(uintptr_t)val;
	short out, i, len, width;

	PROBE("jt96");
	(void)s9;
	l3994();                                /* GrafPort snapshot */
	if (s6 == 0)                            /* style default */
		s6 = 8;

	/* cell-rect bounds (char coords): col 0..39, row 0..24 */
	if ((unsigned char)a > 39 || (unsigned char)b > 24
	 || (unsigned char)c > 39 || (unsigned char)d > 39)
		return;

	/* re-home the persistent pen if it's outside this cell (L4428..L4462) */
	{
		short pc = (short)(unsigned char)g_a5_byte(-27912);
		short pr = (short)(unsigned char)g_a5_byte(-27911);
		if (!(pc >= a && pc <= (short)(c + 1) && pr >= b && pr <= d)) {
			g_a5_byte(-27912) = (unsigned char)a;
			g_a5_byte(-27911) = (unsigned char)b;
		}
	}
	if (s7 != 0) {                          /* draw the cell box */
		jt103(a, b, c, d);
		g_a5_byte(-27912) = (unsigned char)a;
		g_a5_byte(-27911) = (unsigned char)b;
	}
	if (str == NULL)
		return;
	len = (short)(jt483(str) - 1);          /* last char index */
	if (len < 0)
		return;

	out = 0;
 word:                                          /* L44b8 */
	i = out;
	while (i < len && l433a((short)(unsigned char)str[i]) != 0)
		i++;                            /* skip leading delimiters */
	while (i < len && l433a((short)(unsigned char)str[i]) == 0
	       && str[i] != ' ')
		i++;                            /* scan the word */
	while (i < len && l433a((short)(unsigned char)str[i]) != 0)
		i++;                            /* skip trailing delimiters */
	if (i < len && str[i] != ' ')
		i--;                            /* back up off a non-space */

	width = (short)((i - out) + (short)(unsigned char)g_a5_byte(-27912));
	if (width <= c)
		goto draw_fit;                  /* L4744 — fits this line */
	if (width == (short)(c + 1) && str[i] == ' ') {
		i--;
		goto draw_trail;                /* L4612 — draw, then break */
	}
	goto break_line;                        /* L461c */

 draw_fit:                                      /* L46f4 / L4744 */
	while (out <= i) {
		l42a0((short)(unsigned char)g_a5_byte(-27912),
		      (short)(unsigned char)g_a5_byte(-27911), s5, s6, 1,
		      (short)(unsigned char)str[out]);
		if (g_a5_27981 != 0)
			l435a();
		out++;
		g_a5_byte(-27912) = (unsigned char)(g_a5_byte(-27912) + 1);
	}
	goto next;                              /* L474e */

 draw_trail:                                    /* L45b0 / L4612, then falls into break */
	while (out <= i) {
		l42a0((short)(unsigned char)g_a5_byte(-27912),
		      (short)(unsigned char)g_a5_byte(-27911), s5, s6, 1,
		      (short)(unsigned char)str[out]);
		if (g_a5_27981 != 0)
			l435a();                /* asm uses JT[476] here; same slow-text no-op */
		out++;
		g_a5_byte(-27912) = (unsigned char)(g_a5_byte(-27912) + 1);
	}
	/* fall through */

 break_line:                                    /* L461c */
	g_a5_byte(-27912) = (unsigned char)a;
	g_a5_byte(-27911) = (unsigned char)(g_a5_byte(-27911) + 1);
	while (out <= len && str[out] == ' ')
		out++;                          /* skip leading spaces (L462c) */
	if ((short)(unsigned char)g_a5_byte(-27911) <= d)
		goto next;                      /* still room — wrap, re-scan word */
	if (out >= len)
		goto next;
	/* overflowed the cell bottom: paginate (prompt + clear + redraw box) */
	g_a5_byte(-27912) = (unsigned char)a;
	g_a5_byte(-27911) = (unsigned char)b;
	jt176();
	l4c46();
	jt103(a, b, c, d);
	while (out <= i) {                      /* L4698 — draw on the fresh page */
		l42a0((short)(unsigned char)g_a5_byte(-27912),
		      (short)(unsigned char)g_a5_byte(-27911), s5, s6, 1,
		      (short)(unsigned char)str[out]);
		if (g_a5_27981 != 0)
			l435a();
		out++;
		g_a5_byte(-27912) = (unsigned char)(g_a5_byte(-27912) + 1);
	}
	/* fall through */

 next:                                          /* L474e */
	if (out <= len)
		goto word;
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
 *        - HP byte = entry[CHAR_HP].  Classify into colour 0/1/2 and
 *          draw via JT[34] at col 32 + colour.
 *        - AC byte = entry[CHAR_AC].  Classify; if JT[1200]() == 3 and
 *          entry[197] != 0, paint a "*" overlay via JT[97] at col 35.
 *          Draw AC via JT[32] at col 36 + colour.
 *      Step row, advance.
 *   5. Footer: if row < 12, draw final separator via JT[103]. */
/* Roster-column data lives at port-local record offsets until the
 * canonical play-record layout (race/class/level positions) is lifted
 * with the combat/play code. cg_build_record and the test-party seed are
 * the only writers; l02dc reads them for the Name/Race/Class/Level grid.
 * The gap 199..383 sits between the +197/198 status flags and AC@385. */
#define CHAR_RACE   200
#define CHAR_CLASS  201
#define CHAR_LEVEL  202
#define CHAR_STATS  203   /* 6 bytes: STR INT WIS DEX CON CHA (record order) */
#define CHAR_ALIGN  209   /* index into cg_aligns[9]                          */
#define CHAR_INPARTY 210  /* 1 = in the active adventuring party, 0 = benched */
#define CHAR_XP      212  /* experience points (long; +212..215)              */
#define CHAR_MAXHP   216  /* full HP, to heal toward on rest                  */

/* Faithful in-memory combatant offsets, decoded from the CODE 19 record
 * sheet (jt892) + combat: AC@385 (JT[34], combat reads defender AC here),
 * HP@395 (JT[32]), THAC0 = 60 - CHAR_THAC0 (the sheet's "THAC0." line),
 * Movement@396 (JT[63]). The port now stores records at these offsets. */
#define CHAR_AC      385
#define CHAR_HP      395
#define CHAR_THAC0   384  /* displayed THAC0 = 60 - this byte                 */
#define CHAR_MOVE    396

#define CG_PARTY_MAX 6    /* active-party slot count                          */

/* Short race / class names for the narrow roster columns (the char-gen
 * tables use the long "Magic-User"; the roster abbreviates to "Mage"). */
static const char *const k_roster_races[6] = {
	"Human", "Elf", "Half-Elf", "Dwarf", "Gnome", "Halfling",
};
static const char *const k_roster_classes[6] = {
	"Cleric", "Fighter", "Mage", "Thief", "Paladin", "Ranger",
};

static void l02dc(long highlight)
{
	const unsigned char *handle = (const unsigned char *)g_a5_28006;
	const unsigned char *entry;
	short page;
	short row;

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
	/* Faithful headers (CODE 12 + 0x348/0x36c): "Name" at the base page,
	 * "AC HP" at page 33, both red (colour 12). */
	jt94(page,       row, 12, 0, ua_strs_at(0x5e5e)); /* "Name"  */
	jt94((short)33,  row, 12, 0, ua_strs_at(0x5e64)); /* "AC HP" */
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

		/* HP (page 36) + AC (page 33), faithfully shaded by value range.
		 * The Mac paints these through JT[34]/JT[32] (called here for the
		 * trace); those are PROBE stubs, so the numbers are drawn via JT[94]
		 * off the migrated CHAR_HP/CHAR_AC offsets. */
		{
			short hp = entry[CHAR_HP], colour;
			short ac = (signed char)entry[CHAR_AC];   /* AD&D AC can be negative */

			if (hp == 0 || hp > 69) colour = 0;
			else if (hp < 50)       colour = 1;
			else if (hp < 60)       colour = 2;
			else                    colour = 1;
			jt34((long)(uintptr_t)entry, (short)(32 + colour), row, 0);
			jt94((short)36, row, 7, 0, "%d", (int)hp);

			if (ac > 99) colour = 0; else if (ac > 9) colour = 1;
			else         colour = 2;
			/* "*" overlay before the AC number (faithful: mid-operation
			 * marker when jt1200()==3 and the +197 flag is set). */
			if (jt1200() == 3 && entry[197] != 0)
				jt97(35, row, 12, 0, 1, 42, 1);
			jt32((long)(uintptr_t)entry, (short)(36 + colour), row, 0, 0);
			jt94((short)33, row, 7, 0, "%d", (int)ac);
		}

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
/* JT[449] (CODE 3) — the DLItem paint walker. The Mac jump table exports it
 * as L2c60 itself (entry_jt449: L2c60), so jt449 IS l2c60: walk the item list
 * and repaint. The port had stubbed it separately, making every jt449(1) in
 * the play paths (jt240/jt241/l206e) a no-op — the command bar only drew
 * because the modal poll's own l2c60 happened to run. Wire it to l2c60. */
static void    l2c60(short force_paint);            /* defined below (CODE 3+0x2c60) */
static void    jt449(short a)                       { PROBE("jt449"); l2c60(a); }
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

/* jt376 — shape 7 method: the keyboard / command source. Real JT[376]
 * (CODE 3+0x2862): on a key event (cmd 5) with an action proc installed at
 * rec[4] and the item enabled (rec[28] & 3 == 0), call that proc with the
 * source's index ((rec - g_a5_-9254) / DLITEM_BYTES) and the key, returning
 * its result. cmd 4 returns 0; everything else (paint, etc.) delegates to
 * l1676. The dungeon-walk keyboard source's action proc is JT[287] (installed
 * by l6256); until JT[287] is lifted the proc returns 0, so l2d3e doesn't yet
 * latch a key — but the handler dispatch is now faithful. */
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

	if (cmd == 5) {
		long proc = *(long *)(rec + 4);
		if (proc != 0 && (rec[28] & 3) == 0) {
			short idx = (short)(((unsigned char *)rec
			            - (unsigned char *)(uintptr_t)g_a5_9254)
			            / DLITEM_BYTES);
			return ((short (*)(short, short))(uintptr_t)proc)(idx, a);
		}
		return l1676(rec, cmd, a, b);   /* no proc / disabled -> default */
	}
	if (cmd == 4)
		return 0;
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

/* L14d0 (CODE 3 + 0x14d0) — shape-3 (list-row / radio-item) paint.
 * jt380 cmd=1 funnels here. Faithful to the Mac body:
 *
 *   rec[28] |= 0x80;                       // mark painted
 *   if (rec[28] & 0x02) return;            // disabled -> no draw
 *   // highlight/style offset hl from the state bits:
 *   if      (rec[28] & 0x01) hl = 2;       // pressed/selected
 *   else  { hl = (rec[28] & 0x08) ? 3 : 0; // focus
 *           if (rec[28] & 0x04) hl++; }    // dim
 *   short ss = rec[26];
 *   if (ss >= 0)                            // ss < 0 = no glyph/style
 *       L148a(rec[16], rec[18], (ss?ss>>10:0),
 *             (ss?ss&0x3ff:arg) + hl);      // font/style paint
 *   if (rec[12] == 0) return;              // no label
 *   // colour byte from rec[31] + selection state:
 *   if (rec[28] & 0x01)                     // selected
 *       col = rec[31] ? ((rec[31]&0xf0)==0x90 ? 152
 *                        : (rec[31]&0xf0)|7) : 23;
 *   else                                    // normal
 *       col = rec[31] ? rec[31] : 240;
 *   jt1141(rec[16], rec[18], 0, 8006, &v, &h);  // -> pixel, +6 inset
 *   jt1089(v, h, col, rec[12]);            // draw the label string
 *
 * L148a routes through jt995 / jt1001 (font-bitmap blit) which are
 * PROBE stubs in the port, so the visible glyphs come from jt1089's
 * DrawString — the same QuickDraw shim path the headers (l35f8) and
 * jt382 button labels render through. The jt452 list-builds tag each
 * row with rec[31] = g_a5_-7000 (135 shallow / 15 deep), so col's low
 * nibble lands on a visible CLUT index (7 = light grey) rather than
 * the bare-default 0xf0 (index 0). The selection marker is a separate
 * highlight DLItem jt568 toggles, not a colour change here. */
static void l14d0(unsigned char *rec, short arg)
{
	short hl;
	short ss;
	short col;
	short v = 0, h = 0;

	PROBE("L14d0");

	rec[28] |= 0x80;                         /* mark painted */
	if ((rec[28] & 0x02) != 0)
		return;                             /* disabled */

	if ((rec[28] & 0x01) != 0) {
		hl = 2;
	} else {
		hl = ((rec[28] & 0x08) != 0) ? 3 : 0;
		if ((rec[28] & 0x04) != 0)
			hl++;
	}

	ss = *(short *)(rec + 26);
	if (ss >= 0) {
		short style = (ss != 0) ? (short)(ss >> 10) : (short)0;
		short size  = (ss != 0) ? (short)(ss & 0x03ff) : arg;
		l148a(*(short *)(rec + 16), *(short *)(rec + 18),
		      style, (short)(size + hl));
	}

	if (*(long *)(rec + 12) == 0)
		return;                             /* no label string */

	if ((rec[28] & 0x01) != 0) {            /* selected */
		if (rec[31] != 0)
			col = ((rec[31] & 0xf0) == 0x90)
			    ? (short)152
			    : (short)((rec[31] & 0xf0) | 7);
		else
			col = 23;
	} else {                                /* normal */
		col = (rec[31] != 0) ? (short)rec[31] : (short)240;
	}

	jt1141(*(short *)(rec + 16), *(short *)(rec + 18),
	       0, 8006, &v, &h);
	/* Mac calls jt1089(v, h, ...) in Point (vertical, horizontal) order;
	 * the port's jt1089 takes (horizontal, vertical) — it swaps at both
	 * the sink and every caller (see jt94), so a faithful caller must
	 * pass (h, v) here to land on the port convention.
	 * v+6: the radio marker glyph is drawn with its TOP at the row pen
	 * (jt1135(rec16)), but DrawString puts the label BASELINE there, so the
	 * 8px text rode ~6px high above the 7px marker. +6 drops the baseline to
	 * vertically centre the label on the marker (race/align/gender/class). */
	jt1089(h, (short)(v + 6), col, "%s", *(const char **)(rec + 12));
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

	if (cmd == 1) {
		/* L2266: paint via L14d0(rec, 16). */
		l14d0(rec, (short)16);
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
/* jt381 — shape 2 method: the radio-GROUP container (L20c4). Its children
 * are the 1-based DLItems that immediately follow it (child N = rec + N*32).
 * rec[24] = the selected child index, rec[8] = the value-output global ptr,
 * rec[4] = the action proc fired on a pick (jt566/jt567/jt568/jt569/jt570).
 *
 *   cmd 0  (L2114) sync: read the value global, select that child.
 *   cmd 4  (L214c) select index: clear the radio bit (cmd 26 -> bit2) on the
 *          old child, set it (cmd 18 -> bit2) on the new, write the value
 *          global, then fire the action proc.
 *   cmd 16..22 / 24..30 (L21f4): forward the set/clear-bit to every child.
 *   else   (L222c): default to l1676.
 *
 * The bit2 the radio sets is what L14d0/L148a render as the SELECTED marker
 * (red-dot circle); the action proc's jt444 enable/disable of sibling items
 * (e.g. jt566 disabling classes a race can't be) drives the UNAVAILABLE
 * (no-ring) marker via bit0. */
static short jt381(void *rec_v, short cmd, ...)
{
	unsigned char *rec = (unsigned char *)rec_v;
	va_list ap;

	PROBE("jt381");
	SHAPE_CMD_PROBE("jt381");

	switch (cmd) {
	case 0: {
		long  vp  = *(long *)(rec + 8);
		short val = (vp != 0) ? *(short *)(uintptr_t)vp : (short)1;
		(void)jt381(rec, (short)4, (int)val);
		return 0;
	}
	case 4: {
		short idx, old;
		va_start(ap, cmd);
		idx = (short)va_arg(ap, int);
		va_end(ap);
		old = *(short *)(rec + 24);
		if (idx != old) {
			if (old != 0)
				(void)jt380(rec + (long)old * DLITEM_BYTES,
				            (short)26, 0, 0);  /* clear bit2 (old) */
			*(short *)(rec + 24) = idx;
			{
				long vp = *(long *)(rec + 8);
				if (vp != 0)
					*(short *)(uintptr_t)vp = idx;
			}
		}
		if (idx != 0)
			(void)jt380(rec + (long)idx * DLITEM_BYTES,
			            (short)18, 0, 0);      /* set bit2 (new) */
		if (*(long *)(rec + 4) != 0) {
			void (*proc)(void) = *(void (**)(void))(rec + 4);
			proc();                            /* action proc reads globals */
		}
		return 0;
	}
	case 16: case 17: case 18: case 19: case 20: case 21: case 22:
	case 24: case 25: case 26: case 27: case 28: case 29: case 30: {
		short n = *(short *)(rec + 22), i;
		for (i = 1; i <= n; i++)
			(void)jt380(rec + (long)i * DLITEM_BYTES, cmd, 0, 0);
		return 0;
	}
	default: {
		short a, b;
		va_start(ap, cmd);
		a = (short)va_arg(ap, int);
		b = (short)va_arg(ap, int);
		va_end(ap);
		return l1676(rec, cmd, a, b);
	}
	}
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
/* Set while the play screen repaints its command bar (jt312 forced frame) so
 * jt382 draws each command label on its own beveled MENU.CTL plate in the
 * dungeon-safe HUD palette (253/254), instead of the menu's clut 7/15 (wall
 * colours in the dungeon). Off everywhere else. */
static short g_hud_paint;
static void  port_menu_bar(short top, short left, short width, short idx); /* MENU.CTL plate */
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
		short dim;

		rec[28] |= 0x80;

		if ((rec[28] & 0x02) != 0)
			return 0;                       /* disabled, no draw */

		highlighted = ((rec[28] & 0x09) != 0) ? 1 : 0;
		/* Bit 2 (0x04) = "dimmed" — visible but disabled, drawn in a
		 * darker stone grey so the player reads it as unavailable. Set
		 * by the menu builders (jt452 cmd 18) on recessed commands. */
		dim = ((rec[28] & 0x04) != 0) ? 1 : 0;

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

			/* The Mac paint dispatches l148a(rec16, rec18, style,
			 * size+hl) here; for ss=0 buttons size=14, i.e. ALWAYS
			 * item 14 — which is a small DOWN-ARROW glyph. The Mac
			 * doesn't render it visibly on menu/DONE-EXIT buttons (the
			 * face is the command-bar plate + centred label), and now
			 * that the GLIB blit is live it drew a stray triangle below
			 * each button, so it's suppressed. (The shape-3 radio
			 * markers go through l14d0's own l148a — unaffected.) */
			(void)style; (void)size;

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
				/* No port plate here: jt382 cmd-1 only lays down the label. The
				 * button's beveled BAR is real screen chrome from the GLIB blit —
				 * for char-gen that's FRAME.CTL item 4 (the full-width bottom
				 * command bar) via l35f8 -> jt76 -> jt1001(FRAME) -> L309c ->
				 * L2d4e (mode-2). The draw_bevel stopgap is retired. (The main
				 * MENU still uses the port menu_draw_plates stand-in until the
				 * faithful CODE-15/19 menu is lifted — task #105.) */
				if (len > 0) {
					/* Play command bar: draw each command on its own
					 * beveled MENU.CTL plate (the Mac HUD's per-command
					 * bevel cells). Cells advance (len+1)*8 px (l1aea's
					 * (len+1)*4 at scale 2), so this width abuts them; the
					 * left cap sits just left of the label origin. */
					if (g_hud_paint)
						port_menu_bar((short)(y_pix - 9),
						              (short)(x_pix - 4),
						              (short)((len + 1) * 8), (short)1);
					/* The Mac paint chain (L148a/jt995) sets the pen
					 * colour; our collapsed path doesn't, so the label
					 * would inherit a stale fgColor. Draw the body in the
					 * UI's light grey (clut 7) and the accelerator letter
					 * in white (clut 15) — the highlighted hotkey letter
					 * in data/frua_mac_menu.png. The accelerator code is
					 * rec[29] (set by jt452 cmd 32); we highlight its first
					 * case-insensitive match in the label. */
					const unsigned char BODY = g_hud_paint ? 253 : (dim ? 18 : 7);
					const unsigned char HOT  = g_hud_paint ? 254 : (dim ? 18 : 15);
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

					/* Left-justify the label at the button origin (the plate
						 * carries its own left margin), like the Mac menu — the
						 * labels are NOT centred. */
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
/* Viewport clip for the faithful 3D blit (l309c_tile): pixels outside
 * [clip_l, clip_r) x [clip_t, clip_b) are dropped so the corridor stays in
 * the play screen's 3D pane (L6234's clip resolves to left=8 top=24 right=184
 * bottom=200). cw_view_clip() sets it; default = whole surface. */
static short g_cwf_clip_l, g_cwf_clip_t, g_cwf_clip_r, g_cwf_clip_b;
/* Pane-origin shift for the first-person view: l5b42's deep transform places
 * the view at its natural origin (~4,12); these slide it (uniformly, so the
 * internal layout is preserved) to the FRAME.CTL viewport hole. Set by the
 * renderer; 0 = natural origin. */
static short g_cwf_ox, g_cwf_oy;
/* Set while render_3d_faithful walks the frustum (jt199 -> l5b42 -> jt200), so
 * the view's deep slot transform fires regardless of g_a5_2347. This decouples
 * the native 88x88 3D view from the global display mode: the play screen runs
 * at g_a5_2347=1 (scale 2) so the command bar / HUD draw at the native
 * 320x200 layout the menu was built for, while the 3D view still renders deep.
 * (Without this, the deep view required g_a5_2347=0 -> jt1135 scale 3 = the
 * Mac's 640x400 doubled space -> the command bar landed at 1.5x position.) */
static short g_cwf_force_deep;
/* Set while l63c0 is in its walk loop, so l2d3e routes movement/control keys
 * (arrows / Esc / Return) to the keyboard source (return 0) instead of the
 * command-bar DLItem match — otherwise an arrow is consumed as a command and
 * the play loop exits. Off everywhere else so the menus are unaffected. */
static short g_walk_input;
/* Colour slot (= wall group 0/1/2) of the tile l309c_tile is currently
 * blitting; set by jt200_layer. Selects the clut band (g_cw_base) the tile's
 * 32-based bytes rebase into, so each wall set keeps its own CLUT. */
static short g_cwf_slot;
static short g_cwf_blits __attribute__((unused));    /* debug: blit count   */

/* Set the faithful-view clip rect (and the surface dims it blits into). */
static void cw_view_clip(short pitch, short sw, short sh,
                         short l, short t, short r, short b)
{
	g_cwf_pitch = pitch; g_cwf_sw = sw; g_cwf_sh = sh;
	g_cwf_clip_l = (l < 0) ? 0 : l;
	g_cwf_clip_t = (t < 0) ? 0 : t;
	g_cwf_clip_r = (r > sw) ? sw : r;
	g_cwf_clip_b = (b > sh) ? sh : b;
}

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
			/* The bright horizon/transparency markers (the magenta key,
			 * the cyan transition, and any other strongly blue-dominant
			 * marker) read as speckle noise in the first-person view — a
			 * stone floor/ceiling has no saturated blue — so fold them to
			 * a dark shadow tone, keeping the horizon band neutral. */
			if ((r == 255 && g == 103 && b == 255) ||
			    (r == 127 && g == 255 && b == 255) ||
			    (b > 150 && b >= r + 48 && b >= g + 16)) {
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
	/* The faithful Mac play viewport is 88x88 at (24,24)-(111,111) (the hole
	 * in FRAME.CTL's set-9 frame; BACK.CTL backdrops are 88x88 -> 1:1). Centre
	 * (68,68), half-size 44; the perspective rings keep the original ramp
	 * ratios scaled to the square 44-half viewport. */
	static const short hw[5] = { 44, 27, 17, 10, 6 };
	static const short hh[5] = { 44, 27, 17, 10, 6 };
	const short vcx = 68, vcy = 68, VOFF = 8;
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

	/* 8bpp COLOUR tile (.CTL, flags bit 6 set; body = h*width, width =
	 * 8*bpp_w, one clut-129 index per pixel, 255 = transparent) — the
	 * persistent HUD wall format. Blit straight into the 8bpp framebuffer
	 * (g_cwf_px, set by the caller) instead of the 1bpp bp_blit page. */
	if ((metric[7] & 0x40) && g_cwf_px != NULL) {
		short h = (short)(((unsigned short)metric[0] << 8) | metric[1]);
		short w = (short)(metric[6] * 8);
		const unsigned char *body = (const unsigned char *)(uintptr_t)info;
		short x0 = (short)(sx - xbear), y0 = (short)(sy - ybear);
		short r, c;
		/* The tile bytes are 32-based indices into the set's own CLUT; rebase
		 * them into this group's clut band (g_cw_base[slot] = 32/64/96) so
		 * Wall1/2/3 each keep their palette. Bytes outside the band (shared
		 * mortar/shadow colours) and the 255 / per-set magenta keys pass or
		 * skip unchanged. */
		short slot = (g_cwf_slot >= 0 && g_cwf_slot < CW_SLOTS) ? g_cwf_slot : 0;
		short base = g_cw_base[slot];
		for (r = 0; r < h; r++) {
			short dy = (short)(y0 + r);
			if (dy < g_cwf_clip_t || dy >= g_cwf_clip_b)
				continue;
			for (c = 0; c < w; c++) {
				unsigned char v = body[(long)r * w + c];
				short off, dx;
				if (v == 255)
					continue;            /* global transparency key */
				off = (short)(v - 32);
				if (off >= 0 && off < CW_BAND) {
					if (g_cw_strans[slot][off])
						continue;        /* per-set magenta key */
					v = (unsigned char)(base + off);
				}
				dx = (short)(x0 + c);
				if (dx < g_cwf_clip_l || dx >= g_cwf_clip_r)
					continue;
				g_cwf_px[(long)dy * g_cwf_pitch + dx] = v;
			}
		}
		return;
	}

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
static void cw_blit_piece(short top, short left, short idx) __attribute__((unused));
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

/* JT[114] (CODE 6 + 0x3804) — blit wall tile `idx` from a wall-set tile-
 * library handle. The Mac forwards to JT[1001] (CODE 5+0x31ac, = JT[468] +
 * L309c, the GLIB tile blit); the port routes to l309c_tile with the handle.
 * `handle` is one of the per-group wall-set GLIBs L6eea loads. */
static void jt114(unsigned char *page, short top, short left, short idx,
                  long handle)
{
	PROBE("jt114");
	if (handle != 0)
		l309c_tile(page, top, left, (long)(uintptr_t)handle, idx);
}

/* jt200_layer — draw one wall-tile layer for jt200. FAITHFUL path: blit tile
 * `idx` from the per-group wall-set tile-library handle (the table at
 * g_a5_-27894, one long per group), via JT[114]. L6eea loads those handles
 * from the level's colour wall sets (8X8DC-family). The cw_blit_piece colour
 * stand-in and the group-2 DUNGCOM fallback are dropped: DUNGCOM is ENCOUNTER
 * art (the combat sub-map), not the 3D walls — see [[dungeon-render-architecture]].
 * Groups whose handle is still 0 (L6eea not yet lifted / set not configured)
 * draw nothing rather than a stand-in. */
static void jt200_layer(unsigned char *page, short top, short left,
                        short group, short idx)
{
	/* The group is also its colour slot (Wall1->slot0, Wall2->slot1,
	 * Wall3->slot2): l309c_tile rebases the tile's 32-based bytes into that
	 * slot's clut band (32/64/96) so each set keeps its own CLUT. */
	g_cwf_slot = group;
	jt114(page, top, left, idx, g_a5_long(-27894 + (long)group * 4));
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
	if ((sub & 0xff) < 8) {
		/* step the VERTICAL anchor (asm fp@10).
		 * L59d4 5a28-5a52: in deep mode adds 16 to fp@(10) — the 8016-anchored
		 * coord, which l5b42 emits as jt200's `top` (vertical). The old lift
		 * stepped `left` (horizontal), transposing the per-layer step and
		 * inverting the depth stack (far walls rode up to the screen top, near
		 * walls sat too low — "facing wall too far back, tops don't meet").
		 * Native 320x200 halves the Mac's 16 -> 8 (see l5b42's halved deep
		 * transform); top here is already the transformed native coord.
		 * g_cwf_force_deep keeps the deep step firing while the play screen
		 * runs at g_a5_2347=1 (so the command bar draws at native scale). */
		if (jt1200() == 3 || g_cwf_force_deep) {
			if (top < 8000)
				top = (short)(top + 8);
		} else {
			top = (short)(top + 4);
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
/* AXIS MAPPING (RESOLVED 2026-06-05, from L6234/L5b42 asm):
 * the L6234 deep-mode clip setup (0x628c..0x62c8) builds JT[1173]'s rect
 * from its two coord args: fp@(8) feeds left/right, fp@(10) feeds top/bottom.
 * So fp@(8) is the HORIZONTAL (X) anchor and fp@(10) the VERTICAL (Y) anchor.
 * At each L5b42 call (e.g. 0x63ee..0x6412) the first delta (the -12222/-12240
 * "wide" family, +soff) is added to fp@(8) -> HORIZONTAL, and the second
 * delta (the -12202/-12220 "narrow" family) is added to fp@(10) -> VERTICAL;
 * jt200 is then called (fp@8, fp@10) = (horizontal, vertical).
 *
 * jt199 is called jt199(8012, 8016, ...) so its arg1 (the `y` param here) is
 * the asm's fp@(8) = the HORIZONTAL anchor (8012), and arg2 (`x`) is fp@(10) =
 * the VERTICAL anchor (8016). The helpers pass the wide -12222/-12240 family
 * as `ydelta` (added to fp@(8)) and the narrow -12202/-12220 family as
 * `xdelta` (added to fp@(10)). So `ydelta` (wide) + `y` (8012) form the
 * HORIZONTAL coord and `xdelta` (narrow) + `x` (8016) the VERTICAL — no axis
 * cross; the only fix vs the original lift is the jt200 output order, since
 * jt200(arg1, arg2) = (horizontal, vertical) but our jt200 takes (top, left).
 * The original added each delta to its anchor but emitted jt200(y, x) as
 * (top, left), putting the wide corridor-receding spread on the vertical axis
 * and transposing the view. Deltas are the LOW BYTE, signed (asm: moveb +
 * extw + lslw#2). Deep-mode (jt1200()==3) remap is ((v-8012)<<2)+8 on both;
 * with these anchors that yields horiz = 16*wide + 8, vert = 16*narrow + 24. */
static void l5b42(unsigned char *page, short y, short x, short ydelta,
                  short xdelta, short code, short sub) __attribute__((unused));
static void l5b42(unsigned char *page, short y, short x, short ydelta,
                  short xdelta, short code, short sub)
{
	/* y (8012) + wide ydelta -> horizontal; x (8016) + narrow xdelta -> vertical */
	short horiz = (short)(y + ((short)(signed char)ydelta << 2));
	short vert  = (short)(x + ((short)(signed char)xdelta << 2));
	if (jt1200() == 3 || g_cwf_force_deep) {
		/* The Mac deep transform is ((v-8012)<<2)+8 — that is the 640x400
		 * DOUBLED space. FRUA is natively 320x200 (the 3D pane is 88x88, not
		 * 176x176), so halve the scale to <<1 (+4) for the native view. This
		 * also closes the far-row gaps: 8px tiles stepped 16px apart in the
		 * doubled space land 8px apart native, so they abut. */
		horiz = (short)(((short)(horiz - 8012) << 1) + 4 + g_cwf_ox);
		vert  = (short)(((short)(vert  - 8012) << 1) + 4 + g_cwf_oy);
	}
#ifdef FRUA_COORD_TRACE
	dbg_log_num("l5b42 wide=", (long)(signed char)ydelta);
	dbg_log_num("     narrow=", (long)(signed char)xdelta);
	dbg_log_num("   scr top=", (long)vert);
	dbg_log_num("  scr left=", (long)horiz);
	dbg_log_num("    cd/sub=", (long)code * 100 + sub);
#endif
	jt200(page, vert, horiz, code, sub);   /* jt200(top, left) */
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
	static short s_chrome_drawn = 0;          /* FRAME.CTL chrome painted? */
	/* The native 320x200 first-person pane: 88x88 at FRAME.CTL set 9's hole
	 * (24,24)-(112,112). l5b42's deep transform places the view at its natural
	 * origin (~4,12); g_cwf_ox/oy slide it into the hole (24-4=20, 24-12=12). */
	const short VL = 24, VT = 24, VR = 112, VB = 112;
	static short s_off_init = 0;
	if (!s_off_init) {
		/* Faithful native placement: the Mac deep-view clip origin is (4,12)
		 * native; FRAME.CTL set 9's hole is at (24,24), so slide the view by
		 * (24-4, 24-12) = (20, 12). Measured span (FRUA_COORD_TRACE) confirms
		 * the geometry's vanishing centre sits ~mid-hole at this offset; the
		 * near side-walls overhang and clip at the pane edges by design.
		 * Set once so the FRUA_L6234_VERIFY walk loop can nudge it live. */
#ifdef FRUA_COORD_TRACE
		g_cwf_ox = 0; g_cwf_oy = 0;    /* measurement build: log natural span */
#else
		g_cwf_ox = 20; g_cwf_oy = 12;
#endif
		s_off_init = 1;
	}
	const unsigned char *ds = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short f = (short)(g_a5_12286 & 7);
	short x, y;

	if (ds == NULL)
		return;

	/* Install the level's THREE wall sets' CLUTs (Wall1/2/3 = ds[4..6]) into
	 * their clut bands (32/64/96) via the existing per-slot loader; l309c_tile
	 * rebases each group's 32-based tile bytes into its band. Reload only when
	 * the group ids change (each set's CLUT lives in its .CTL, sub-GLIB 0). */
	if (ds[4] != g_cw_grp[0] || ds[5] != g_cw_grp[1] || ds[6] != g_cw_grp[2]) {
		load_wall_groups(ds);                 /* slots 0/1/2 + cw_finalize bands */
		load_backdrop(g_back_set);            /* re-lay backdrop band (clut 145) */
		s_chrome_drawn = 0;                   /* clut wiped -> repaint chrome */
	}

	/* Draw the FRAME.CTL play-screen chrome once (and after a wall-set change).
	 * MUST run AFTER load_wall_groups, which wipes the clut, so the frame band
	 * (16-31) survives; the 88x88 view then draws into the hole over it. The
	 * chrome is static, so subsequent frames only repaint the hole. */
	if (!s_chrome_drawn) {
		port_draw_play_frame(px, pitch, sw, sh);
		s_chrome_drawn = 1;
	}

	/* backdrop (floor/ceiling/sky) inside the viewport rect */
	{
		short vh = (short)(VB - VT), vw = (short)(VR - VL);
		for (y = VT; y < VB; y++) {
			short by = g_back_h ? (short)(((long)(y - VT) * g_back_h) / vh) : 0;
			for (x = VL; x < VR; x++) {
				short c;
				if (g_back_w) {
					short bx = (short)(((long)(x - VL) * g_back_w) / vw);
					unsigned char v = g_back_img[(long)by * g_back_w + bx];
					c = v ? (short)v : 4;
				} else {
					c = (y < (short)((VT + VB) / 2)) ? 4 : 5;
				}
				map_px(px, pitch, sw, sh, x, y, (unsigned char)c);
			}
		}
	}

	/* faithful walk: l6148 loads Wall1-3 into the -27894/-27890/-27886
	 * handles; jt199 -> l5b42 -> jt200 -> jt114 -> l309c_tile blits the
	 * pre-sized colour tiles, clipped to the viewport. jt199 args are
	 * (horizontal=8012, vertical=8016, row=partyY, col=partyX, facing). */
	g_cwf_px = px;
	cw_view_clip(pitch, sw, sh, VL, VT, VR, VB);
	l6148();
#ifdef FRUA_ENGINE_PROBE
	dbg_log_num("faithful: ds[4..6]=", (long)ds[4] * 10000 + ds[5] * 100 + ds[6]);
#endif
	/* Force the deep slot transform for the view walk regardless of g_a5_2347,
	 * so the play screen can stay at g_a5_2347=1 (native scale 2) for the
	 * command bar / HUD while the 88x88 first-person view still renders deep. */
	g_cwf_force_deep = 1;
	jt199(page, (short)8012, (short)8016,
	      (short)g_a5_12287, (short)g_a5_12288, f);
	g_cwf_force_deep = 0;
	g_cwf_px = NULL;
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

/* --- play-screen frame (FRAME.CTL) --- *
 *
 * The Mac play screen's chrome is FRAME.CTL: 29 top-level pieces that
 * self-position by their metric bearings (ybear/xbear, subtracted by
 * ui_glib_blit). set 0 is the frame's 16-colour palette (greys); the
 * pieces index 16..31 (the partition's "window borders / menu controls"
 * band), with index 0 = transparent. So the band loads into clut 16..31
 * and the pieces blit over the play background.
 *   set 9      = ornate 88x88-hole viewport frame (top-left)
 *   sets 1..8  = screen border strips + the viewport/panel divider
 *   sets 17-20 = type-9 composite corner/edge assemblies
 *   set 21 + 22-25 = compass surround + faces
 */
static long g_frame_base;
static void ui_glib_blit(long handle, short idx, short top, short left,
                         int transparent, int flip);   /* defined below */

static void port_frame_load(void)
{
	static unsigned char fbuf[40000];
	short refnum = 0;
	long  n, base;

	if (g_frame_base)
		return;
	if (FSOpen((ConstStr255Param)"\011FRAME.CTL", 0, &refnum) != noErr)
		return;
	n = (long)sizeof fbuf;
	(void)FSRead(refnum, &n, fbuf);
	(void)FSClose(refnum);
	base = (long)(uintptr_t)fbuf;
	if (l37aa(base, 0) != 0)
		g_frame_base = base;
}

/* ALWAYS.CTL = jt468 group 0 — the always-resident UI glyph GLIB (button
 * faces, radio markers). Loaded resident like FRAME.CTL. */
static long g_always_base;
static void port_always_load(void)
{
	static unsigned char abuf[8192];
	short refnum = 0;
	long  n, base;

	if (g_always_base)
		return;
	if (FSOpen((ConstStr255Param)"\012ALWAYS.CTL", 0, &refnum) != noErr)
		return;
	n = (long)sizeof abuf;
	(void)FSRead(refnum, &n, abuf);
	(void)FSClose(refnum);
	base = (long)(uintptr_t)abuf;
	if (l37aa(base, 0) != 0)               /* 'GLIB' magic */
		g_always_base = base;
}

/* MENU.CTL = jt468 group 24 — the menu chrome GLIB. Item 1 is a 320x16
 * mode-0 8bpp command-BAR glyph (plate-face grey indices in the FRAME band
 * 16..31, with a stone/bevel top edge); item 2 is the recessed variant. The
 * faithful main menu (jt315/CODE 22) draws a bar per command from this; the
 * port blits it per button via port_menu_bar (clipped to the button width).
 * See [[faithful-main-menu-code22]]. */
static long g_menu_base;
static void port_menu_load(void)
{
	static unsigned char mbuf[12000];      /* MENU.CTL is ~11064 bytes */
	short refnum = 0;
	long  n, base;

	if (g_menu_base)
		return;
	if (FSOpen((ConstStr255Param)"\010MENU.CTL", 0, &refnum) != noErr)
		return;
	n = (long)sizeof mbuf;
	(void)FSRead(refnum, &n, mbuf);
	(void)FSClose(refnum);
	base = (long)(uintptr_t)mbuf;
	if (l37aa(base, 0) != 0)                /* 'GLIB' magic */
		g_menu_base = base;
}

/* Blit a clean MENU.CTL command-bar plate `idx` (1 = raised, 2 = recessed)
 * at pixel (top,left), `width` px, mode-0 raw 8bpp.
 *
 * MENU.CTL item 1/2 is a 320x16 glyph: a horizontal command bar split into
 * THREE ~106px slots by [22,0] dividers. Its art (dumped from the live glyph):
 *   vertical bevel (any face column) row0=31 (bright top), row1=7 (highlight),
 *     rows2..10=23 (face), row11=30 (shadow), row12=0 (black), row13=18 (dark).
 *   left cap (cols 0..3) = [30,30,30,7] (dark outer border + inner highlight).
 * The port draws individual 2-column menu plates wider than a slot, so a
 * straight clip catches a divider. Instead reconstruct an arbitrary-width
 * beveled plate from the bar's REAL pixels: the left cap verbatim (cols 0..3),
 * a tiled face column for the middle, and the right cap as the MIRROR of the
 * left cap ([7,30,30,30] = inner highlight then dark border). Draw 14 rows
 * (0..13), which tiles exactly at the menu's 14px row pitch (engine y step 7 @
 * scale 2): no 2px overlap, and row13's dark line abuts the next plate's bright
 * top to form the recessed divider the Mac shows. The FRAME band (clut 16..31)
 * is installed by load_menu_ui before this. */
#define MENU_BAR_FACE_COL 50    /* a clean face column (full vertical bevel) */
#define MENU_BAR_ROWS     14    /* draw 14 of the 16 rows -> exact 14px tiling */
static void port_menu_bar(short top, short left, short width, short idx)
{
	unsigned char metric[8];
	long  info;
	const unsigned char *src;
	unsigned char *px;
	short pitch, sw, sh, r, c, h, w, nrows;

	port_menu_load();
	if (!g_menu_base)
		return;
	info = l2856(g_menu_base, idx, metric);
	if (info == 0)
		return;
	h = (short)(((unsigned short)metric[0] << 8) | metric[1]);   /* 16 */
	w = (short)(metric[6] * 8);                                   /* 320 */
	if (h <= 0 || w <= 0)
		return;
	src = (const unsigned char *)(uintptr_t)info;                /* raw 8bpp */
	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == NULL)
		return;
	nrows = (h < MENU_BAR_ROWS) ? h : MENU_BAR_ROWS;
	for (r = 0; r < nrows; r++) {
		short dy = (short)(top + r);
		const unsigned char *s = src + (long)r * w;

		if (dy < 0 || dy >= sh)
			continue;
		for (c = 0; c < width; c++) {
			short dx = (short)(left + c);
			short srcc;
			unsigned char v;

			if (dx < 0 || dx >= sw)
				continue;
			if (c < 4)
				srcc = c;                       /* real left cap */
			else if (c >= width - 4)
				srcc = (short)(3 - (c - (width - 4)));  /* mirror = right cap */
			else
				srcc = MENU_BAR_FACE_COL;       /* tiled face column */
			v = s[srcc];
			if (v == 255)
				continue;
			px[(long)dy * pitch + dx] = v;
		}
	}
}

/* jt468 groups 0/1/24 -> the port's resident UI GLIBs (see [[glib-resource-groups]]). */
static long port_ui_group_base(short group)
{
	if (group == 0)  { port_always_load(); return g_always_base; }
	if (group == 1)  { port_frame_load();  return g_frame_base;  }
	if (group == 24) { port_menu_load();   return g_menu_base;   }
	return 0;
}

/* Fill the play background grey, install the frame palette band (clut
 * 16..31), and blit every FRAME.CTL piece at its bearing. Called on the
 * dungeon view's full-clear frames (the chrome is static; the 88x88
 * viewport is overdrawn by the 3D render and refreshed via present-rect). */
static void port_draw_play_frame(unsigned char *px, short pitch, short sw, short sh)
{
	const unsigned char *base8;
	short count, s, r;

	port_frame_load();
	if (!g_frame_base)
		return;
	base8 = (const unsigned char *)(uintptr_t)g_frame_base;

	/* set 0 -> clut 16..31 (re-installed each call: the wall-set load
	 * rewrites the CLUT, and the frame band must survive it). */
	{
		const unsigned char *pp =
			(const unsigned char *)(uintptr_t)(l37aa(g_frame_base, 0) + 8);
		RGBColor fp[16];
		short k;
		for (k = 0; k < 16; k++) {
			fp[k].red   = (unsigned short)((pp[k*3+0] << 8) | pp[k*3+0]);
			fp[k].green = (unsigned short)((pp[k*3+1] << 8) | pp[k*3+1]);
			fp[k].blue  = (unsigned short)((pp[k*3+2] << 8) | pp[k*3+2]);
		}
		qd_set_palette(fp, (short)16, (short)16);
	}

	/* grey stone background (clut 21 ~ mid stone) under the chrome. */
	for (r = 0; r < sh; r++)
		memset(px + (long)r * pitch, 21, (size_t)sw);

	count = (short)(((unsigned)base8[8] << 8) | base8[9]);
	for (s = 1; s < count; s++)
		ui_glib_blit(g_frame_base, s, (short)0, (short)0, 1, 0);
}

static signed char jt1160(void);        /* view-mode bit; defined below */

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
	short pitch, sw, sh, cell;

	(void)page;
	/* First-person dungeon view only. The view-mode bit jt1160() reads
	 * (g_a5_-2592 & 0x02: set = top-down/overland, clear = first-person)
	 * is the engine's real dungeon-vs-overland discriminator, independent
	 * of the display scale. The old gate was jt1200()==3, which conflates
	 * "deep view" with "640x400 scale mode" (g_a5_-2347==0): the port runs
	 * EVERY screen at native 320x200 (g_a5_-2347==1), so jt1200() is never
	 * 3 in live play and that gate would bail before rendering. The 88x88
	 * view still renders deep via g_cwf_force_deep (set inside
	 * render_3d_faithful), so gating on jt1160() is the right, scale-
	 * independent test. See [[screen-320x200-not-640x400]]. */
	if (ds == NULL || jt1160() != 0)        /* first-person dungeon view only */
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

	/* Draw the play-screen frame (FRAME.CTL chrome) + full-present once;
	 * thereafter only the 88x88 viewport changes, so we present just that
	 * rect (the c2p of the static 320x400 screen was the perf wall). The
	 * chrome is static, so it persists between viewport-only presents. */
	if (s_view_first || g_view_force_full) {
		port_draw_play_frame(px, pitch, sw, sh);
	}
	/* LIVE DEFAULT: render_3d_faithful — the 1:1 Mac jt199 -> l5b42 -> jt200
	 * slot-assembly view (perspective fixed in 0f62432). This matches jt221's
	 * dungeon branch, so the initial draw (jt214 -> jt221) and the per-step
	 * movement redraws (l63c0 -> jt312) now use the SAME faithful renderer.
	 * FRUA_CORRIDOR (texture-mapped trapezoids) and FRUA_RAYCAST (the wider
	 * 3-column frustum that opens side passages) remain selectable fallbacks. */
#if defined(FRUA_CORRIDOR)
	render_3d_view(px, pitch, sw, sh);
#elif defined(FRUA_RAYCAST)
	render_3d_raycast(px, pitch, sw, sh);
#else
	render_3d_faithful(px, pitch, sw, sh);
#endif
	/* HUD command bar: on the chrome-redraw frames, lay the beveled command-bar
	 * plate and force-repaint the command-word DLItems on top. The chrome wipe
	 * (port_draw_play_frame) and the view both ran above, and the bar's labels
	 * carry the "painted" bit (set when jt240's jt449 first drew them), so the
	 * poll-loop l2c60(0) would skip them — l2c60(1) forces the repaint. Each
	 * command word gets its own beveled MENU.CTL plate (jt382 draws it per
	 * label when g_hud_paint), matching the Mac HUD's per-command bevel cells;
	 * the labels paint in the HUD palette (253 grey / 254 white hotkey),
	 * installed right here so dungeon_view_setup's clut-129 256-entry load can't
	 * clobber it. */
	if (s_view_first || g_view_force_full) {
		RGBColor hud[2];

		hud[0].red = hud[0].green = hud[0].blue = (unsigned short)0xC8C8;
		hud[1].red = hud[1].green = hud[1].blue = (unsigned short)0xFFFF;
		qd_set_palette(hud, (short)253, (short)2);
		g_hud_paint = 1;
		l2c60((short)1);                /* force-repaint the bar (plate per word) */
		g_hud_paint = 0;
	}
	if (s_view_first || g_view_force_full) {
		/* DOUBLE full present: the HAL double-buffers two planar pages and
		 * each qd_present c2p's the full chunky buffer to ONE page then
		 * flips, so present TWICE to put the complete frame (the static
		 * FRAME.CTL chrome + the view) on BOTH pages. Otherwise the next
		 * viewport-only qd_present_rect flips to the page that never received
		 * the chrome -> a black frame around the view on the first movement
		 * (the dungeon-render / round-trip black, #103). */
		qd_present();
		qd_present();
		s_view_first = 0;
		g_view_force_full = 0;
	} else {
		/* Both pages already hold the chrome (from the double present above);
		 * c2p just the 88x88 viewport at (24,24)-(111,111) to the back page
		 * each frame — its hole is refreshed right before the flip, so every
		 * flip shows the current view over the persistent chrome. */
		qd_present_rect((short)24, (short)24, (short)88, (short)88);
	}
}

/* ===================================================================== *
 * CODE 11 exploration loop — L63c0 (CODE 11 + 0x63c0), milestone 2 of the
 * play-loop lift. FRUA's dungeon-walk loop: paint the view background +
 * status line + first-person view, then spin an input loop — poll (l2d3e,
 * the real JT[456] DLItem event poll) -> classify (jt152) -> JT[3] dispatch
 * (move / turn / select) -> redraw ->
 * repeat until an exit command. Structural skeleton (lift level 2): the CFG
 * + every JT call are faithful; the leaf action routines below are PROBE
 * stubs until lifted, so the loop's shape is real but the per-arm movement /
 * cell-change work is deferred. Unwired for now (the faithful caller is the
 * JT[233/234] play-screen entry, milestone 3); replaces port_play_demo's
 * loop once wired. */
/* L63c0 deps defined later in the file — forward decls. */
static void jt1173(short top, short left, short bottom, short right);
static void jt1193(void);
static void jt1067(void);
static void jt1080(void);
static short       jt358(void);                                                              /* CODE 8+0x6e4a — defined below */
/* JT[273] (CODE 22+0x4900) — deep/turn-counter gate: returns the counter
 * jt358() when it is <= 4, else 0. (l63c0 ORs this into its deep-view flag.) */
static int         jt273(void)
{
	short v = (short)(jt358() & 0xff);
	PROBE("jt273");
	return (v <= 4) ? (int)v : 0;
}
static void        l4226(void *rec)                     { PROBE("L4226"); (void)rec; }        /* CODE 11-local */
static void        l4268(void *rec)                     { PROBE("L4268"); (void)rec; }        /* CODE 11-local */
static short       jt354(void)                          { PROBE("jt354"); return 0; }         /* CODE 8+0x5ef8 */
static void        jt365(void)                          { PROBE("jt365"); }                   /* CODE 8+0x7238 */
/* JT[358] (CODE 8+0x6e4a) — read the game counter byte g_a5_-10374
 * (shown in the status line via jt367; gated by jt273). */
static short       jt358(void)                          { PROBE("jt358"); return (short)(unsigned char)g_a5_byte(-10374); }
static void        jt367(short v, void *buf)            { PROBE("jt367"); (void)v;(void)buf; } /* CODE 8+0x6e78 counter fmt */

/* JT[303] (CODE 22 + 0x2180) — the play-view status header, drawn with jt1089.
 * Line positions depend on rec[4] (wilderness vs dungeon). Draws the module
 * title (g_a5_-18876 override padded to 16 via JT[394], else g_a5_-10616), a
 * counter line (JT[358] -> JT[367]; both stubbed for now), and the map
 * dimensions (g_a5_-12300[2] x [3], fmt g_a5_-11324). jt1161/JT[394]/jt1089
 * are real, so the header text renders.
 *
 * TODO: the design-name line (memmove level[+118] -> buf, then draw "%s" at
 * 140) is deferred. It goes through JT[406], whose port lift jt406(dst,src)
 * has its first two params REVERSED vs the real L57f8 BlockMove(src,dst) —
 * calling it faithfully (jt406(level+118, buf, 16)) would copy buf INTO the
 * level header and corrupt it. Restore this line once jt406's arg order is
 * audited and fixed across its callers. */
static void jt303(void *rec_v)
{
	unsigned char *rec = (unsigned char *)rec_v;
	char buf[40];                           /* fp@(-40) */
	short tx, ty, dx, dy;
	const unsigned char *lvl;

	PROBE("jt303");

	if (rec[4] == 0) {                      /* wilderness */
		tx = 8068; ty = 8004; dx = 8053; dy = 8104;
	} else {                                /* dungeon */
		tx = 8008; ty = 8092; dx = 8020; dy = 8092;
	}

	if (g_a5_byte(-18876) != 0)
		jt394(buf, ua_strs_at(0x3072) /* "%*s" */, 16,
		      (const char *)&g_a5_byte(-18876));
	else
		jt394(buf, (const char *)(uintptr_t)g_a5_long(-10616));
	jt1089(tx, ty, (short)135, ua_strs_at(0x3076) /* "%s" */, buf);

	jt367((short)(jt358() & 0xff), buf);
	jt1089((short)(tx + 4), ty, (short)135, buf);

	/* design-name line deferred — see the jt406 note above. */

	lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	jt1089(dx, dy, (short)135,
	       (const char *)(uintptr_t)g_a5_long(-11324),
	       (int)lvl[2], (int)lvl[3]);
}
static void        l2788(void)                          { PROBE("L2788"); }                                   /* CODE 22-local wilderness compass */

/* JT[280] (CODE 22 + 0x265e) — the dungeon position / compass readout drawn
 * at the top of the play view (all via jt1089, the real text drawer):
 *   line 1: the party (x, y) coordinate — format g_a5_-11332 with the party
 *           coords (g_a5_-12288/-12287) when mode != 0, else g_a5_-11328 with
 *           " -" placeholders (STRS 0x308c / 0x3090).
 *   line 2 (x+4): the area name — g_a5_-10820 plus the facing-indexed name
 *           from g_a5_-10924[(facing & 6) >> 1], or a "%12s" placeholder when
 *           the cell's name slot (rec[5]) is hidden.
 * The wilderness arm (rec[4]==0) defers to L2788 (a directional indicator) —
 * off the dungeon path, stubbed. */
static void jt280(void *rec_v, short x, short y, short mode)
{
	unsigned char *rec = (unsigned char *)rec_v;

	PROBE("jt280");

	if (mode != 0)
		jt1089(x, y, (short)135,
		       (const char *)(uintptr_t)g_a5_long(-11332),
		       (int)(short)(signed char)g_a5_byte(-12288),
		       (int)(short)(signed char)g_a5_byte(-12287));
	else
		jt1089(x, y, (short)135,
		       (const char *)(uintptr_t)g_a5_long(-11328),
		       ua_strs_at(0x308c), ua_strs_at(0x3090));

	if (rec[4] == 0) {              /* wilderness */
		l2788();
		return;
	}

	/* dungeon: the area-name line, one step below the coords */
	{
		int show_name;
		if (rec[5] == 255)      show_name = 1;
		else if (rec[5] != 0)   show_name = 0;
		else                    show_name = (jt273() != 0) ? 0 : 1;

		if (!show_name)
			jt1089((short)(x + 4), y, (short)135,
			       ua_strs_at(0x30a6) /* "%12s" */,
			       ua_strs_at(0x30ac));
		else if (mode != 0) {
			short idx = (short)((g_a5_byte(-12286) & 6) >> 1);
			jt1089((short)(x + 4), y, (short)135,
			       ua_strs_at(0x3094) /* "%s %5s" */,
			       (const char *)(uintptr_t)g_a5_long(-10820),
			       (const char *)(uintptr_t)g_a5_long(-10924 + idx * 4));
		} else
			jt1089((short)(x + 4), y, (short)135,
			       ua_strs_at(0x309c) /* "%s -    " */,
			       (const char *)(uintptr_t)g_a5_long(-10820));
	}
}
static void        jt1113(short *o1, short *o2)         { PROBE("jt1113"); if(o1)*o1=0; if(o2)*o2=0; }        /* CODE 4+0x6204 */
static short       l2d3e(void);                         /* JT[456] event poll, CODE 3+0x2d3e (full lift, defined below) */
static short       jt152(short sel);                    /* CODE 7+0x3370 classifier (defined below) */
/* forward decls for jt297 (the movers + helpers are defined just below). */
static signed char jt1160(void);
static void        jt311(void *rec_v, short dir, long cb);
static signed char jt218(signed char *rowp, signed char *colp,
                         short *out_row, short *out_col,
                         short span_r, short span_c, short wrap);
static void        l1798(void *rec, short a);
static void        jt50(void) { PROBE("jt50"); }   /* CODE 6+0x5ac2 — page (deferred) */
static void        jt51(void) { PROBE("jt51"); }   /* CODE 6+0x5ad8 — page (deferred) */

/* L05ca (CODE 22 + 0x05ca) — wall code on cell `cell`'s edge facing `dir`:
 * edge = (dir & 6) >> 1 (0..3); returns the HIGH nibble of the cell record's
 * edge byte (lvl + cell*6 + 290 + edge), the wall-art/style code. jt297 treats
 * a result > 1 as "blocked" before stepping forward. */
static short l05ca(short cell, short dir)
{
	const unsigned char *lvl;
	short edge = (short)((dir & 6) >> 1);

	PROBE("L05ca");
	if (edge < 0 || edge >= 4)
		return 0;
	lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	if (lvl == NULL)
		return 0;
	return (short)((lvl[(long)cell * 6 + 290 + edge] >> 4) & 15);
}

/* L1908 (CODE 22 + 0x1908) — commit a first-person move/turn: normalise the
 * new facing to [1,8], write the party cell (-12287 row / -12288 col / -12286
 * facing), recentre the view (JT[218]), mirror the cell into rec+46, and (when
 * the redraw flag is set and rec is a live deep view) repaint via jt312.
 * MINIMAL lift: the move sound + smooth-scroll animation (L4900 / L423e /
 * L3998) and the blocked-step recentre (JT[213]) are deferred — the geometry
 * (cell update + recentre + redraw) is what makes the party walk. */
static void l1908(void *rec_v, short row, short col, short facing, short redraw)
{
	unsigned char *rec = *(unsigned char **)rec_v;
	signed char moved;
	short k;

	PROBE("L1908");
	if (facing <= 0)
		facing = (short)(facing + 8);
	else if (facing > 8)
		facing = (short)(facing - 8);

	/* TODO: L4900 / L423e / L3998 — move sound + smooth-scroll animation. */
	g_a5_byte(-12287) = (unsigned char)row;
	g_a5_byte(-12288) = (unsigned char)col;
	g_a5_byte(-12286) = (unsigned char)facing;

	moved = jt218((signed char *)&g_a5_byte(-12287),
	              (signed char *)&g_a5_byte(-12288),
	              (short *)&g_a5_byte(-11706),
	              (short *)&g_a5_byte(-11704),
	              (short)g_a5_byte(-11708),
	              (short)g_a5_byte(-11707),
	              (short)(rec[4] == 0 ? 1 : 0));

	/* mirror the (possibly wrapped) party cell into rec+46. */
	for (k = 0; k < 6; k++)
		rec[46 + k] = g_a5_byte(-12288 + k);

	if (moved)
		l1798(rec_v, 0);
	/* else: JT[213] blocked-step recentre redraw — deferred. */

	if (redraw && rec[4] != 0 && rec[5] == 0)
		jt312((unsigned char *)rec_v);
}

/* JT[297] (CODE 22 + 0x1c3e) — the keyboard movement handler l63c0 dispatches
 * arrow keys (257..264) to. Routes per jt1160(): TOP-DOWN -> jt311 (the
 * overland absolute mover, gated on map size); FIRST-PERSON -> facing-relative
 * via L1908. Controls: 264 = forward (L05ca wall check first), 258 = turn
 * right (+2), 262 = turn left (-2), 260 = about-face (+4), 257/263 = strafe
 * (turn, step, turn back), 259/261 = back-step (about-face, step, face back).
 * Keys 338/339 page via JT[50]/JT[51] (deferred). The first-person+arrow case
 * swaps in the rec+46 party mirror for the move, then restores the view cell. */
static void jt297(void *rec_v, short key, long cb)
{
	unsigned char *rec = *(unsigned char **)rec_v;
	const unsigned char *lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short cell, fp3, fp4, facing, k;
	unsigned char snap[6];
	int snapped = 0;

	PROBE("jt297");
	if (key == 338) { jt50(); return; }
	if (key == 339) { jt51(); return; }
	if (lvl == NULL)
		return;

	cell = (short)((short)lvl[3] * (short)(signed char)g_a5_byte(-12287)
	               + (short)(signed char)g_a5_byte(-12288));
	fp3  = jt1160() ? 0 : 1;                 /* first-person? */

	if (fp3 && rec[4] == 1 && key >= 257 && key <= 264) {
		for (k = 0; k < 6; k++) snap[k] = g_a5_byte(-12288 + k);
		for (k = 0; k < 6; k++) g_a5_byte(-12288 + k) = rec[46 + k];
		snapped = 1;
	}
	fp4    = (rec[4] == 0 && rec[9] != 0) ? 1 : 0;
	facing = (short)g_a5_byte(-12286);

	#define JT297_ROW ((short)(signed char)g_a5_byte(-12287))
	#define JT297_COL ((short)(signed char)g_a5_byte(-12288))
	#define JT297_FWD_ROW ((short)(JT297_ROW + (signed char)g_a5_byte(-11693 + g_a5_byte(-12286))))
	#define JT297_FWD_COL ((short)(JT297_COL + (signed char)g_a5_byte(-11684 + g_a5_byte(-12286))))

	switch (key) {
	case 264:                                /* forward */
		if (!fp3) {
			if ((short)lvl[2] > 20) jt311(rec_v, key, cb); else jt1080();
		} else if (fp4 && l05ca(cell, facing) > 1) {
			jt1080();
		} else {
			l1908(rec_v, JT297_FWD_ROW, JT297_FWD_COL, facing, 1);
		}
		break;
	case 260:                                /* about-face (+4) */
		if (!fp3) { if ((short)lvl[2] > 20) jt311(rec_v, key, cb); else jt1080(); }
		else l1908(rec_v, JT297_ROW, JT297_COL, (short)(facing + 4), 1);
		break;
	case 262:                                /* turn left (-2) */
		if (!fp3) { if ((short)lvl[3] > 21) jt311(rec_v, key, cb); else jt1080(); }
		else l1908(rec_v, JT297_ROW, JT297_COL, (short)(facing - 2), 1);
		break;
	case 258:                                /* turn right (+2) */
		if (!fp3) { if ((short)lvl[3] > 21) jt311(rec_v, key, cb); else jt1080(); }
		else l1908(rec_v, JT297_ROW, JT297_COL, (short)(facing + 2), 1);
		break;
	case 257: case 263:                      /* strafe: turn, step, turn back */
		if (!fp3) {
			if ((short)lvl[2] > 20 || (short)lvl[3] > 21) jt311(rec_v, key, cb);
			else jt1080();
		} else {
			short t = (key == 257) ? 2 : -2;
			l1908(rec_v, JT297_ROW, JT297_COL, (short)(facing + t), 0);
			if (fp4 && l05ca(cell, (short)g_a5_byte(-12286)) > 1)
				jt1080();
			else
				l1908(rec_v, JT297_FWD_ROW, JT297_FWD_COL,
				      (short)g_a5_byte(-12286), 0);
			l1908(rec_v, JT297_ROW, JT297_COL,
			      (short)((short)g_a5_byte(-12286) - t), 1);
		}
		break;
	case 259: case 261:                      /* back-step: about-face, step, face back */
		if (!fp3) {
			if ((short)lvl[2] > 20 || (short)lvl[3] > 21) jt311(rec_v, key, cb);
			else jt1080();
		} else {
			l1908(rec_v, JT297_ROW, JT297_COL, (short)(facing + 4), 0);
			if (fp4 && l05ca(cell, (short)g_a5_byte(-12286)) > 1)
				jt1080();
			else
				l1908(rec_v, JT297_FWD_ROW, JT297_FWD_COL,
				      (short)g_a5_byte(-12286), 0);
			l1908(rec_v, JT297_ROW, JT297_COL,
			      (short)((short)g_a5_byte(-12286) - 4), 1);
		}
		break;
	case 0:
		break;
	default:
		jt1080();
		break;
	}

	#undef JT297_ROW
	#undef JT297_COL
	#undef JT297_FWD_ROW
	#undef JT297_FWD_COL

	if (snapped)
		for (k = 0; k < 6; k++) g_a5_byte(-12288 + k) = snap[k];
}
/* JT[1160] (CODE 4 + 0x67c6) — is the TOP-DOWN / automap view active? Tests
 * bit 1 of g_a5_-2592 (the view-mode flags). jt297 uses it to route keyboard
 * movement: set -> the overland absolute mover (jt311); clear -> the
 * first-person facing-relative mover (L1908). */
static signed char jt1160(void)
{
	PROBE("jt1160");
	return (g_a5_byte(-2592) & 0x02) ? 1 : 0;
}
static void        l1798(void *rec, short a)            { PROBE("L1798"); (void)rec;(void)a; }                /* CODE 22-local post-move default */

/* L5368 (CODE 7 + 0x5368) — one-axis scroll-window solver for the overland
 * map. Wraps *coord into [0, dim) (the map is toroidal), then returns the
 * viewport origin that keeps the wrapped cell inside a `span`-wide window:
 * the origin is clamped onto the map and the cell is re-followed when it
 * scrolls off an edge. wrap != 0 takes the keep-centred path; wrap == 0 the
 * edge-follow path. JT[397]/JT[413] are the clamp(>=) / clamp(<=) helpers. */
static short l5368(short span, short dim, signed char *coord, short origin, short wrap)
{
	short c;

	PROBE("L5368");

	if (dim <= 0)                   /* guard: %dim / -dim with a zero/garbage
	                                 * map dimension is a 68k divide-by-zero
	                                 * (the move-crash); leave the coord put. */
		return (short)*coord;

	if (*coord < 0) {
		short rem = (short)(((short)(-(short)*coord)) % dim);
		*coord = (signed char)(dim - rem);
	} else {
		*coord = (signed char)((short)*coord % dim);
	}

	span   = jt413(dim, jt397(1, span));
	origin = jt413(jt397(0, origin), (short)(dim - span));

	c = (short)*coord;
	if ((wrap & 0xff) != 0) {
		short v = jt413((short)(dim - span), (short)(c - span / 2));
		return jt397(0, v);
	}
	if (c >= (short)(origin + span))
		return jt397(0, jt413((short)(dim - span),
		                      (short)(c - span + 1)));
	if (c >= origin)
		return origin;
	return c;
}

/* JT[218] (CODE 7 + 0x52b8) — overland move + view recentre. Wraps the
 * proposed row/col edge markers into the map (L5368 per axis), updates the
 * map-view centre (g_a5_-12278 / -12276) and the caller's origin out-params,
 * and returns 1 if the view actually moved (else 0 = blocked). */
static signed char jt218(signed char *rowp, signed char *colp,
                         short *out_row, short *out_col,
                         short span_r, short span_c, short wrap)
{
	const unsigned char *lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short new_r, new_c;
	signed char changed = 0;

	PROBE("jt218");
	new_r = l5368(span_r, (short)lvl[2], rowp, g_a5_word(-12278), wrap);
	new_c = l5368(span_c, (short)lvl[3], colp, g_a5_word(-12276), wrap);

	if (new_r != g_a5_word(-12278)) { changed = 1; g_a5_word(-12278) = new_r; }
	if (new_c != g_a5_word(-12276)) { changed = 1; g_a5_word(-12276) = new_c; }

	if (out_row != NULL) *out_row = new_r;
	if (out_col != NULL) *out_col = new_c;
	return changed;
}

/* JT[311] (CODE 22 + 0x1a6e) — directional move on the overland / automap.
 * Computes the party's offset within the view (JT[397] clamp), pushes the
 * row/col edge markers in the requested direction (the 257..264 key codes),
 * then JT[218] wraps + recentres the view; on a real move it updates the
 * party cell (g_a5_-12288 / -12287) and fires the per-step callback (cb,
 * here L63c0's cb2 = JT[236]), else JT[1080] sounds the blocked cue. */
static void jt311(void *rec_v, short dir, long cb)
{
	const unsigned char *lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short off_y, off_x;
	signed char edge_r, edge_c;

	PROBE("jt311");

	off_y = jt397(0, (short)((signed char)g_a5_byte(-12287) - g_a5_word(-11706)));
	off_x = jt397(0, (short)((signed char)g_a5_byte(-12288) - g_a5_word(-11704)));
	edge_r = (signed char)g_a5_byte(-11705);
	edge_c = (signed char)g_a5_byte(-11703);

	switch (dir) {                  /* row edge marker */
	case 257: case 263: case 264:
		edge_r = (signed char)jt397(0, (short)(g_a5_word(-11706) - 19));
		break;
	case 259: case 260: case 261:
		edge_r = (signed char)jt413((short)(lvl[2] - 1),
		                            (short)(g_a5_word(-11706) + 39));
		break;
	default: break;
	}
	switch (dir) {                  /* col edge marker */
	case 261: case 262: case 263:
		edge_c = (signed char)jt397(0, (short)(g_a5_word(-11704) - 20));
		break;
	case 257: case 258: case 259:
		edge_c = (signed char)jt413((short)(lvl[3] - 1),
		                            (short)(g_a5_word(-11704) + 41));
		break;
	default: break;
	}

	if (jt218(&edge_r, &edge_c,
	          (short *)&g_a5_byte(-11706), (short *)&g_a5_byte(-11704),
	          20, 21, 0) != 0) {
		g_a5_byte(-12287) = (unsigned char)(g_a5_word(-11706) + off_y);
		g_a5_byte(-12288) = (unsigned char)(g_a5_word(-11704) + off_x);
		if (cb != 0)
			((void (*)(long))(uintptr_t)cb)(*(long *)rec_v);
		else
			l1798(rec_v, 0);
	} else {
		jt1080();
	}
}
static signed char l67e4(void *rec, short a)            { PROBE("L67e4"); (void)rec;(void)a; return 0; }       /* CODE 11-local */
static void        jt238(void *rec)                     { PROBE("jt238"); (void)rec; }                        /* default dispatch cb, CODE 11+0x67d0 */

/* JT[241] play-action helpers — leaf / CODE-local deps stubbed pending their
 * own lifts. l429c builds the view layers from rec[4] (the area kind);
 * l476e flips the party/screen entry mode; jt148 (CODE 7+0x33dc) paints the
 * command-prompt line; l4810 releases a transient. jt287/jt294 (CODE 22) are
 * the action procs the registered walk DLItems carry (see l6256). */
static void        jt148(long prompt, char *title, short flag);  /* CODE 7+0x33dc — lifted near l206e */
static void        l206e(long p1, unsigned char *buf, const char *suffix, unsigned char *byte_ptr); /* CODE 7+0x206e */
static void        l1f3e(short a8, short a10);            /* CODE 7+0x1f3e — bar sizer */
static void        l2858(short mode);                    /* CODE 7+0x2858 */
static void        l429c(short a, short b)               { PROBE("L429c"); (void)a;(void)b; }                  /* CODE 11-local */
/* L476e (CODE 11 + 0x476e) — set up the play-view interior rect. `active`
 * (low byte) gates; `layout` picks the view: 0 = the compact dungeon view
 * (9x9 cells anchored at 8008,8068), nonzero = the full area map (20x21 cells
 * at 8008,8004). Stores the rect's screen-space top-left in g_a5_-11674/-11672
 * (via jt1135) and the pixel dimensions in g_a5_-11670/-11668 (= min(view
 * cells, map dim from g_a5_-12300[2]/[3]) * cellsize g_a5_-12272). These feed
 * L63c0's jt1161 background fill and the cell-coordinate mapping. */
static void l476e(short active, short layout)
{
	const unsigned char *lvl;

	PROBE("L476e");
	if ((active & 0xff) == 0)
		return;

	if (layout == 0) {                      /* compact dungeon view */
		g_a5_byte(-11708) = 9;
		g_a5_byte(-11707) = 9;
		jt1135((short)8008, (short)8068,
		       (short *)&g_a5_byte(-11674), (short *)&g_a5_byte(-11672));
	} else {                                /* full area map */
		g_a5_byte(-11708) = 20;
		g_a5_byte(-11707) = 21;
		jt1135((short)8008, (short)8004,
		       (short *)&g_a5_byte(-11674), (short *)&g_a5_byte(-11672));
	}

	lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	g_a5_word(-11670) = (short)(jt413((short)(unsigned char)g_a5_byte(-11708),
	                                  (short)lvl[2]) * g_a5_word(-12272));
	g_a5_word(-11668) = (short)(jt413((short)(unsigned char)g_a5_byte(-11707),
	                                  (short)lvl[3]) * g_a5_word(-12272));
}

/* JT[215] (CODE 7 + 0x57a6) — set the automap cell pixel size. 12 in the
 * deep dungeon-view mode (jt1200()==3), 8 in the normal 2D area-map mode.
 * The play-screen entry runs this before L476e (which multiplies it into
 * the view-rect dimensions) and before the cell mapping; with it 0 the whole
 * map collapses to a point. */
static void jt215(void)
{
	PROBE("jt215");
	g_a5_word(-12272) = (jt1200() == 3) ? (short)12 : (short)8;
}
static void        l4810(void *p, long a)               { PROBE("L4810"); (void)p;(void)a; }                  /* CODE 11-local */
static signed char jt276(short cell)                    { PROBE("jt276"); (void)cell; return 0; }             /* CODE 22+0x475e */
/* JT[287] (CODE 22 + 0x1bc6) — the dungeon-walk KEYBOARD action proc l6256
 * installs on the shape-7 keyboard source. l2d3e calls it (idx, key) on a key
 * event; it accepts the movement / command keys and stashes the accepted key
 * in g_a5_-10372 (where L63c0's CODE 11 keyboard arm reads it), returning 1 to
 * signal "consumed". Accepted: ESC (27); the cursor / first-person move codes
 * 256..264; and the command-letter range 320..344. Anything else clears the
 * stash and returns 0 (let the command-bar DLItems try the key). Faithful to
 * the asm (the inner L1bee range test). */
static short       jt287(short idx, short key)
{
	PROBE("jt287");
	(void)idx;
	if (key == 27) {                          /* ESC */
		g_a5_word(-10372) = key;
		return 1;
	}
	if ((key >= 256 && key <= 264) || (key >= 320 && key <= 344)) {
		g_a5_word(-10372) = key;
		return 1;
	}
	g_a5_word(-10372) = 0;
	return 0;
}
static short       jt294(short flag, short y, short x)   { PROBE("jt294"); (void)flag;(void)y;(void)x; return 0; } /* region action proc, CODE 22+0x1c26 */
static void        jt179(short count);                  /* defined far below (CODE 7+0x11ee) */
static void        jt155(short value, void *counter);   /* CODE 7+0x11a8 — slot append */
static void        jt452(long shape0, ...);             /* DLItem stream builder (defined below) */

/* L6256 (CODE 11 + 0x6256) — register the dungeon-walk input sources.
 * jt241 calls this at the top of the play render path (l6256(rec[4], 1));
 * it runs only for area kind == 1 (a dungeon). It maps the party cell to a
 * screen origin (JT[1139]), resets the DLItem source table (JT[447]), then
 * installs the walk controls via the JT[452] stream builder:
 *   - shape 7: the keyboard / command source, carrying action proc JT[287];
 *   - five shape-5 hit regions positioned around the view, each tagged with
 *     a key code in rec[20] (5 / 7 / 9 / 11) — the directional + region pads;
 *   - a shape-5 select region carrying action proc JT[294] (rec[4]);
 *   - a trailing JT[452](41, ...) stamping the last source's rec[20] with
 *     -1 or 1 per the caller's flag byte.
 * Each source's rec[28] bit 4 (the "active" flag) is set (JT[452] cmd 20).
 * l2d3e polls these by index, matching L63c0's switch (0=keyboard, 1-4=dir,
 * 5=select). They stay inert until their shape handlers (g_a5_-9282[]) and
 * the action procs JT[287]/JT[294] are lifted. */
static void l6256(short kind, short flag)
{
	short row, col;                          /* fp@(-2), fp@(-4): view origin */
	short cy, cx;

	PROBE("L6256");
	if (kind != 1)                           /* wilderness: no walk sources */
		return;

	cy = (short)((unsigned char)g_a5_byte(-11708) * g_a5_word(-12272));
	cx = (short)((unsigned char)g_a5_byte(-11707) * g_a5_word(-12272));
	jt1139((short)8000, (short)8000, cy, cx, &row, &col);

	jt447();
	jt452((long)7, (long)(uintptr_t)&jt287, (long)20, (long)0);
	jt452((long)5, (long)8004, (long)(col + 8004), (long)(row + 8),
	      (long)4, (long)41, (long)5, (long)20, (long)0);
	jt452((long)5, (long)(row + 8008), (long)8004, (long)4, (long)col,
	      (long)41, (long)7, (long)20, (long)0);
	jt452((long)5, (long)8004, (long)8000, (long)(row + 8), (long)4,
	      (long)41, (long)9, (long)20, (long)0);
	jt452((long)5, (long)8004, (long)8004, (long)4, (long)col,
	      (long)41, (long)11, (long)20, (long)0);
	jt452((long)5, (long)8008, (long)8004, (long)row, (long)col,
	      (long)34, (long)(uintptr_t)&jt294, (long)20, (long)0);
	if ((flag & 0xff) != 0)
		jt452((long)41, (long)-1, (long)0);
	else
		jt452((long)41, (long)1, (long)0);
}

/* JT[236] (CODE 11 + 0x5868) — L63c0's cb2 (default / dungeon arm): is the
 * party's current cell blocked? Cell index = level_width*party_y + party_x
 * into JT[276]; returns 1 when that cell tests as 0 (impassable), else 0. */
static short jt236(void)
{
	const unsigned char *lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short cell;
	PROBE("jt236");
	cell = (short)((short)lvl[3] * (short)(signed char)g_a5_12287
	               + (short)(signed char)g_a5_12288);
	return jt276(cell) == 0 ? 1 : 0;
}

static signed char l63c0(unsigned char *rec, short a_wild, short a_sel,
                         short a_deep, long cb1, long cb2)
{
	unsigned char  ctx[54];                 /* fp@(-54): {rec ptr, working copy} */
	unsigned char  deep;                    /* fp@(-2)  */
	signed char    exitflag = 0;            /* fp@(-1): 0 keep looping, !=0 exit */
	short          pollres, procres = 0;    /* fp@(-8), fp@(-6) */
	short          o10 = 0, o12 = 0;        /* fp@(-10), fp@(-12) */

	PROBE("L63c0");

	/* deep flag = wilderness arg set AND (cell flag rec[5] or jt273) */
	deep = ((unsigned char)a_wild != 0
	        && (rec[5] != 0 || jt273() != 0)) ? 1 : 0;

	*(unsigned char **)ctx = rec;           /* ctx[0..3] = rec ptr */
	g_a5_long(-11666) = (long)(uintptr_t)ctx;

	/* paint the view-interior background rect (jt1161, fill colour 8) */
	jt1161((short)g_a5_word(-11674), (short)g_a5_word(-11672),
	       (short)(g_a5_word(-11674) + g_a5_word(-11670)),
	       (short)(g_a5_word(-11672) + g_a5_word(-11668)), (short)8);
	l4226(ctx);
	jt303(rec);                             /* status line */

	/* initial first-person view (deep) or top-down (wilderness) */
	if ((unsigned char)a_deep) {
		jt1173((short)8024, (short)8092, (short)8058, (short)8156);
		jt312(ctx);
		jt1193();
	} else {
		jt280(rec, (short)8024, (short)8092, (short)0);
	}

	if (cb1 == 0)
		cb1 = (long)(uintptr_t)(void *)jt238;
	((void (*)(unsigned char *))(uintptr_t)cb1)(rec);   /* per-screen dispatch */

	/* Port adaptation: the Mac draws immediately to the visible CGrafPort,
	 * but our QuickDraw shim renders into a back-buffer that the Falcon HAL
	 * c2p-flips on qd_present. The initial play frame (bg fill + status +
	 * coords + automap above) is now complete, so flush it to VIDEL; the
	 * modal poll loop (l2d3e) re-presents on changes. */
	qd_present();

	/* Set the view-mode bit jt1160() reads (top-down for the wilderness, so
	 * arrow keys route through jt297 -> jt311; first-person for a_deep), and
	 * arm the walk-input gate so l2d3e routes movement keys to the keyboard
	 * source instead of the command bar. */
	if ((unsigned char)a_deep)
		g_a5_byte(-2592) = (unsigned char)(g_a5_byte(-2592) & ~0x02);
	else
		g_a5_byte(-2592) = (unsigned char)(g_a5_byte(-2592) | 0x02);
	g_walk_input = 1;

	/* --- the input / movement loop (L64ae .. L67ae) --- */
	for (;;) {
		if (jt1163() == 0 && jt1200() != 0)
			jt1067();
		if ((unsigned char)a_deep)
			jt1173((short)8024, (short)8092, (short)8058, (short)8156);
		jt1113(&o10, &o12);
		/* TODO: the cell-change detection + re-render arms
		 * (L64f2..L666c: jt272/jt284 hit-test, facing/coord update into
		 * g_a5_-12286/-12287/-12288, jt312/jt280 redraw) are deferred —
		 * they need jt272/284/297/311 lifted. */
		(void)o12;

		pollres = l2d3e();              /* event poll (JT[456]) */
		if (pollres < 0) {
			jt1080();                   /* idle */
			if (exitflag != 0)
				break;
			continue;
		}
		if (deep)
			rec[5] = (unsigned char)exitflag;

		/* pollres 0 = the keyboard source (l2d3e routes arrows/Esc/Return
		 * here): go straight to the switch (case 0 -> jt297) — don't run it
		 * through jt152, which would mis-classify it as a command-bar pick
		 * (the port's command-bar base isn't above the movement sources) and
		 * break the play loop. Real command-bar items have pollres > 5. */
		procres = (pollres == 0) ? (short)-1 : jt152(pollres);
		if (procres >= 0) {
			/* a command-bar command: latch it for the caller (jt948's
			 * dungeon loop dispatches by this index) and end the walk loop.
			 * Return 1 only for the implicit Move (procres 0) outside deep. */
			g_walk_cmd = procres;
			exitflag = (procres == 0 && (unsigned char)a_deep == 0)
			           ? 1 : -1;
			break;
		}

		/* procres < 0: not a command -> dispatch the input source */
		switch (pollres) {              /* JT[3] min 0 max 5 */
		case 0:                         /* keyboard (L66e8) */
			/* Route arrow / move keys (257..264) to jt297, which itself
			 * dispatches overland (jt311, jt1160()!=0) vs first-person
			 * (l1908, jt1160()==0). The faithful L66e8 gates the call on
			 * jt1160()!=0 and idles DEEP arrows; but jt297's deep arm is the
			 * proven first-person mover (steps rec[46..51] via l1908), so we
			 * route deep arrows here too — that's what makes the dungeon walk
			 * with the arrow keys (verified: rec[46..51] steps; -12288 is
			 * restored by jt297 and committed by the jt240/l63c0 caller). */
			if ((unsigned short)g_a5_word(-10372) >= 257
			    && (unsigned short)g_a5_word(-10372) <= 264) {
				jt297(ctx, (short)g_a5_word(-10372), cb2);
			} else if (g_a5_word(-10372) == 27) {
				exitflag = -1;          /* Esc */
				g_walk_cmd = -1;        /* no command picked */
			} else if (g_a5_word(-10372) == 13 && (unsigned char)a_sel == 0) {
				exitflag = 1;           /* Return */
			} else {
				jt1080();
			}
			break;
		case 1: case 2: case 3: case 4: /* directional move (L674a) */
			jt311(ctx, (short)(258 + (pollres - 1) * 2), cb2);
			break;
		case 5:                         /* select (L676a) */
			if ((unsigned char)a_sel && l67e4(ctx, (short)(signed char)a_wild)
			    && cb2 != 0
			    && ((signed char (*)(unsigned char *))(uintptr_t)cb2)(rec))
				exitflag = 2;
			else
				jt1080();
			break;
		default:
			jt1080();
			break;
		}
		if (exitflag != 0)
			break;

		/* Re-render after a move so it's visible (the faithful per-step
		 * re-render arms L64f2..L666c are deferred). Repaint the view +
		 * automap (cb1 = jt237) at the new party position and flush. */
		if ((unsigned char)a_deep) {
			jt1173((short)8024, (short)8092, (short)8058, (short)8156);
			jt312(ctx);
		} else {
			jt280(rec, (short)8024, (short)8092, (short)0);
		}
		((void (*)(unsigned char *))(uintptr_t)cb1)(rec);
		qd_present();
	}

	g_walk_input = 0;
	jt451();
	(void)l4268;
	return (exitflag > 0) ? exitflag : 0;
}

/* JT[296] (CODE 22 + 0x3792) — map a cell (x,y) to screen coords within the
 * visible map window. dx/dy = cell - window origin (g_a5_-12278/-12276);
 * out-of-window -> (-1,-1); else cellsize (g_a5_-12272) * d + screen origin
 * (g_a5_-12282/-12280). */
static void jt296(short cellX, short cellY, short width, short height,
                  short *outX, short *outY)
{
	short dx = (short)(cellX - g_a5_word(-12278));
	short dy = (short)(cellY - g_a5_word(-12276));

	PROBE("jt296");
	if (dx < 0 || dy < 0
	    || (short)(unsigned char)width <= dx
	    || dy >= (short)(unsigned char)height) {
		if (outX) *outX = (short)-1;
		if (outY) *outY = (short)-1;
	} else {
		if (outX) *outX = (short)(g_a5_word(-12272) * dx + g_a5_word(-12282));
		if (outY) *outY = (short)(g_a5_word(-12272) * dy + g_a5_word(-12280));
	}
}

/* forward decls — defined later in the file */
static void jt1007(short cur_sel, short key);
static void jt216(short cellX, short cellY, short screenX, short screenY, short facing);
static short jt212(short row, short col, short edge);
static short l0788(short cx, short cy);
static void l3fd8(short p_y, short p_x, short facing, short v1, short v2,
                  short cells_a, short cells_b, short special,
                  short pmark, short batch, short entry);

/* jt304 deps: L3806 (CODE 22+0x3806, ~125 instrs) is the FLAT automap renderer
 * — the live path when jt273()==0 (i.e. JT[358] view-depth 0); it is the next
 * lift. jt213 (CODE 7+0x56f2) records the party cell; jt1088 (CODE 5+0xa8) is a
 * post-render leaf. Stubbed for now. */
/* JT[205] (CODE 7 + 0x5f18) — wall-STYLE test, twin of JT[212]: same GEO cell
 * record read (lvl + 290 + (lvl[3]*col + row)*6 + ((edge&6)>>1)) but returns
 * the LOW nibble (the wall graphic/style; JT[212] returns the high nibble). */
static short jt205(short row, short col, short edge)
{
	const unsigned char *lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short eidx = (short)((edge & 6) >> 1);
	long  idx  = (long)((short)lvl[3] * col + row);

	PROBE("jt205");
	return (short)(lvl[290 + idx * 6 + eidx] & 15);
}

static void  jt89(void)               { PROBE("jt89"); }   /* CODE 6+0x4d7c pre-fill */
static void  jt90(void)               { PROBE("jt90"); }   /* CODE 6+0x4d8c post-fill */
static short l06e2(short cx, short cy) { PROBE("L06e2"); (void)cx;(void)cy; return 0; } /* CODE 22+0x6e2 */

/* L3a1a (CODE 22 + 0x3a1a) — draw one flat-automap cell. Clears the cell,
 * dots its 4 corners, draws a wall line on each edge that has a wall style
 * (JT[205]; colour 15 in deep mode else the style), opens a door gap where the
 * wall bit (JT[212]) is a doorway (<= 1), then either: special 0 -> per-edge
 * door jamb marks; special 1/2/3 -> a feature fill (colour from L06e2 / L0788 /
 * the cell's byte[294]); special 4 -> nothing more. cx/cy = cell, sa/scrx =
 * its screen top-left, cs = cell pixel size. */
static void l3a1a(short cx, short cy, short sa, short scrx, short special)
{
	const unsigned char *lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short cs    = g_a5_word(-12272);
	short far_x = (short)(sa + cs);
	short far_y = (short)(scrx + cs);
	short st_n, st_e, st_s, st_w;       /* JT[205] wall styles, edges 8/2/4/6 */
	short bt_n, bt_e, bt_s, bt_w;       /* JT[212] wall bits */
	short off, color, px, py;
	short deep = (jt1200() == 3);

	PROBE("L3a1a");

	jt1161(sa, scrx, far_x, far_y, (short)0);           /* clear */

	/* four corner dots (colour 15) */
	jt1135(sa, scrx, &px, &py);
	jt1161(px, py, (short)(px + 1), (short)(py + 1), (short)15);
	jt1135(sa, (short)(far_y - 1), &px, &py);
	jt1161(px, py, (short)(px + 1), (short)(py + 1), (short)15);
	jt1135((short)(far_x - 1), scrx, &px, &py);
	jt1161(px, py, (short)(px + 1), (short)(py + 1), (short)15);
	jt1135((short)(far_x - 1), (short)(far_y - 1), &px, &py);
	jt1161(px, py, (short)(px + 1), (short)(py + 1), (short)15);

	/* wall lines: edge 8/N, 2/E, 4/S, 6/W */
	st_n = jt205(cy, cx, (short)8);
	if (st_n != 0) {
		color = deep ? (short)15 : st_n;
		jt1161(sa, (short)(scrx + 1), (short)(sa + 1), (short)(far_y - 1), color);
	}
	st_e = jt205(cy, cx, (short)2);
	if (st_e != 0) {
		color = deep ? (short)15 : st_e;
		jt1161((short)(sa + 1), (short)(far_y - 1), (short)(far_x - 1), far_y, color);
	}
	st_s = jt205(cy, cx, (short)4);
	if (st_s != 0) {
		color = deep ? (short)15 : st_s;
		jt1161((short)(far_x - 1), (short)(scrx + 1), far_x, (short)(far_y - 1), color);
	}
	st_w = jt205(cy, cx, (short)6);
	if (st_w != 0) {
		color = deep ? (short)15 : st_w;
		jt1161((short)(sa + 1), scrx, (short)(far_x - 1), (short)(scrx + 1), color);
	}

	/* wall bits (doorways) */
	bt_n = jt212(cy, cx, (short)8);
	bt_e = jt212(cy, cx, (short)2);
	bt_s = jt212(cy, cx, (short)4);
	bt_w = jt212(cy, cx, (short)6);

	off = (short)((cs - 2) / 2);

	/* door gaps (clear, colour 0) where there is a wall and a doorway bit */
	if (st_n != 0 && bt_n <= 1)
		jt1161(sa, (short)(scrx + off), (short)(sa + 1), (short)(scrx + off + 2), (short)0);
	if (st_e != 0 && bt_e <= 1)
		jt1161((short)(sa + off), (short)(far_y - 1), (short)(sa + off + 2), far_y, (short)0);
	if (st_s != 0 && bt_s <= 1)
		jt1161((short)(far_x - 1), (short)(scrx + off), far_x, (short)(scrx + off + 2), (short)0);
	if (st_w != 0 && bt_w <= 1)
		jt1161((short)(sa + off), scrx, (short)(sa + off + 2), (short)(scrx + 1), (short)0);

	if ((special & 0xff) == 0) {
		/* per-edge door jamb marks (colour = the wall bit value) */
		short o2 = (short)(off - 1);
		if (bt_n != 0)
			jt1161((short)(sa + 1), (short)(scrx + o2), (short)(sa + 2),
			       (short)(scrx + o2 + 4), bt_n);
		if (bt_e != 0)
			jt1161((short)(sa + o2), (short)(far_y - 2), (short)(sa + o2 + 4),
			       (short)(far_y - 1), bt_e);
		if (bt_s != 0)
			jt1161((short)(far_x - 2), (short)(scrx + o2), (short)(far_x - 1),
			       (short)(scrx + o2 + 4), bt_s);
		if (bt_w != 0)
			jt1161((short)(sa + o2), (short)(scrx + 1), (short)(sa + o2 + 4),
			       (short)(scrx + 2), bt_w);
		return;
	}

	if ((special & 0xff) == 4)
		return;

	/* special 1/2/3: a feature fill, inset by 1px */
	sa    = (short)(sa + 1);
	scrx  = (short)(scrx + 1);
	far_x = (short)(far_x - 1);
	far_y = (short)(far_y - 1);
	switch (special & 0xff) {
	case 2:  color = (short)(l0788(cx, cy) & 0xff); break;
	case 1:  color = (short)(l06e2(cx, cy) & 0xff); break;
	case 3: {
		const unsigned char *cell = lvl + (long)((short)lvl[3] * cx + cy) * 6;
		color = (cell[294] != 0) ? (short)15 : (short)0;
		break;
	}
	default: color = 0; break;
	}
	if (deep) jt89();
	jt1161(sa, scrx, far_x, far_y, color);
	if (deep) jt90();
}

/* L3806 (CODE 22 + 0x3806) — the FLAT automap renderer (jt304's depth-0 path;
 * the live top-down area map). Maps the view anchor to a screen top-left
 * (jt1135), fills the view background (jt1161), stores the screen origin
 * (g_a5_-12282/-12280) and visible window dims (g_a5_-12274/-12273 =
 * clamp(view cells, map dim)), loops the visible cells through L3a1a, and
 * draws the party marker (JT[216]). Simpler than L3fd8 (no clip/backdrop/3D
 * setup). Args mirror jt304's call. */
static void l3806(short p_y, short p_x, short facing, short v1, short v2,
                  short cells_a, short cells_b, short special, short pmark, short batch)
{
	const unsigned char *lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short sa, sb, t1, t2, cs, i, j;

	PROBE("L3806");

	if ((batch & 0xff) == 0)
		jt108((short)1);

	t1 = jt397(v1, (short)8000);
	t2 = jt397(v2, (short)8000);
	jt1135(t1, t2, &sa, &sb);
	cs = g_a5_word(-12272);

	jt1161(sa, sb, (short)(cells_a * cs + sa),
	       (short)(cells_b * cs + sb), (short)8);

	if ((batch & 0xff) != 0)
		jt108((short)1);

	g_a5_word(-12282) = sa;
	g_a5_word(-12280) = sb;
	g_a5_byte(-12274) = (unsigned char)jt413(jt397(cells_a, (short)1), (short)lvl[2]);
	g_a5_byte(-12273) = (unsigned char)jt413(jt397(cells_b, (short)1), (short)lvl[3]);

	for (i = 0; i < (short)(unsigned char)g_a5_byte(-12274); i++) {
		for (j = 0; j < (short)(unsigned char)g_a5_byte(-12273); j++)
			l3a1a((short)(i + g_a5_word(-12278)),
			      (short)(j + g_a5_word(-12276)),
			      sa, (short)(j * cs + sb), (short)(special & 0xff));
		sa = (short)(sa + cs);
	}

	if ((pmark & 0xff) != 0)
		jt216((short)(p_x - g_a5_word(-12278)),
		      (short)(p_y - g_a5_word(-12276)),
		      g_a5_word(-12282), g_a5_word(-12280), facing);

	jt117();
}
static void jt213(short a, short b, short c)
                  { PROBE("jt213"); (void)a;(void)b;(void)c; }   /* CODE 7+0x56f2 */
static void jt1088(void)      { PROBE("jt1088"); }              /* CODE 5+0xa8 */

/* JT[304] (CODE 22 + 0x17ca) — the automap-view setup jt237 runs before its
 * cell pass. Picks the view anchor by area kind (dungeon 8008,8004 / wilderness
 * 8008,8068), runs the selection-nav primitive JT[1007], then dispatches the
 * actual render by view depth: jt273() (= JT[358], the depth, 0..4) selects
 * L3fd8 (the deep/3D view, depth >= 1) or L3806 (the flat automap, depth 0).
 * For a dungeon it then records the party cell (JT[213] from rec[46..48]) and
 * runs the post-render leaf JT[1088]. Args: rec, batch flag (fp@13). */
static void jt304(void *rec_v, short batch)
{
	unsigned char *rec = (unsigned char *)rec_v;
	short v1, v2, depth;

	PROBE("jt304");
	v1 = (short)8008;
	v2 = (rec[4] == 0) ? (short)8068 : (short)8004;

	jt1007((short)0, (short)26);
	depth = (short)jt273();

	if (depth != 0)
		l3fd8((short)(signed char)g_a5_byte(-12287),
		      (short)(signed char)g_a5_byte(-12288),
		      (short)g_a5_byte(-12286), v1, v2,
		      (short)(signed char)g_a5_byte(-11708),
		      (short)(signed char)g_a5_byte(-11707),
		      (short)rec[5], (short)0, batch, depth);
	else
		l3806((short)(signed char)g_a5_byte(-12287),
		      (short)(signed char)g_a5_byte(-12288),
		      (short)g_a5_byte(-12286), v1, v2,
		      (short)(signed char)g_a5_byte(-11708),
		      (short)(signed char)g_a5_byte(-11707),
		      (short)rec[5], (short)(rec[4] == 0 ? 1 : 0), batch);

	if (rec[4] == 1)
		jt213((short)(signed char)rec[47], (short)(signed char)rec[46],
		      (short)(signed char)rec[48]);
	jt1088();
}
static void jt1148(void)      { PROBE("jt1148"); }             /* CODE 4+0x61f8 init   */
static void jt1087(short a)   { PROBE("jt1087"); (void)a; }    /* CODE 5+0x12c per-row */

/* L3fd8 cluster deps — the per-cell tile renderer and a few leaf setups stay
 * PROBE stubs pending their own lifts. L4430 (CODE 22+0x4430, ~270 instrs +
 * ~17 local helpers) is the dungeon-cell tile draw; jt214 (CODE 7+0x71c6) and
 * jt124 (CODE 6+0x3eea) are per-view setup leaves; jt448 (CODE 3+0x148a) is the
 * glyph drawer JT[216] uses. */
static short l0788(short cx, short cy)
                              { PROBE("L0788"); (void)cx;(void)cy; return 0; } /* CODE 22+0x788 cell-fill colour */

/* JT[212] (CODE 7 + 0x5cc8) — wall-bit test for the automap. Reads the GEO
 * cell record (level base g_a5_-12300 + 290 + (lvl[3]*col + row)*6) and returns
 * the high nibble of the byte for the requested edge ((edge & 6) >> 1) — 0 =
 * open, nonzero = a wall of that type. */
static short jt212(short row, short col, short edge)
{
	const unsigned char *lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short eidx = (short)((edge & 6) >> 1);
	long  idx  = (long)((short)lvl[3] * col + row);
	unsigned char w = lvl[290 + idx * 6 + eidx];

	PROBE("jt212");
	return (short)((w >> 4) & 15);
}

/* L4430 (CODE 22 + 0x4430) — draw one automap cell. Dispatches on the cell
 * `special` kind: 0 = a normal cell (draw a white wall line on each of the 4
 * edges that has no open passage to its neighbour — tested via JT[212] and the
 * direction-step tables g_a5_-27862 [drow] / -27853 [dcol]); 2 = a filled cell
 * (L0788 colour) + optional marker; 3 = a special/visited marker; 1/4 = blank.
 * (cx,cy) is the cell, (sa,scrx) its screen top-left, cs the cell pixel size. */
static void l4430(short cx, short cy, short sa, short scrx, short special)
{
	const unsigned char *lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short cs    = g_a5_word(-12272);
	short far_x = (short)(sa + cs);         /* fp@(-2) */
	short far_y = (short)(scrx + cs);       /* fp@(-4) */
	short edge;

	PROBE("L4430");
	if ((special & 0xff) == 4)
		return;

	switch (special & 0xff) {
	case 0:                                 /* normal cell — wall lines */
		for (edge = 2; edge <= 8; edge += 2) {
			short ncol = (short)((signed char)g_a5_byte(-27853 + (edge & 7)) + cx);
			short nrow = (short)((signed char)g_a5_byte(-27862 + edge) + cy);
			short opp  = (short)(edge + 4);
			short wall = 1;

			if (opp > 8) opp -= 8;
			if (ncol >= 0 && ncol < (short)lvl[2]
			 && nrow >= 0 && nrow < (short)lvl[3]
			 && jt212(nrow, ncol, opp) == 0)
				wall = 0;               /* open passage -> no wall */
			if (!wall)
				continue;
			switch (edge) {
			case 2:  /* right edge */
				jt1161((short)(sa + 1), (short)(far_y - 1),
				       (short)(far_x - 1), far_y, (short)15);
				break;
			case 4:  /* bottom edge */
				jt1161((short)(far_x - 1), (short)(scrx + 1),
				       far_x, (short)(far_y - 1), (short)15);
				break;
			case 6:  /* left edge */
				jt1161((short)(sa + 1), scrx,
				       (short)(far_x - 1), (short)(scrx + 1), (short)15);
				break;
			case 8:  /* top edge */
				jt1161(sa, (short)(scrx + 1),
				       (short)(sa + 1), (short)(far_y - 1), (short)15);
				break;
			default: break;
			}
		}
		break;

	case 2: {                               /* filled cell + marker */
		short color = (short)(l0788(cx, cy) & 0xff);
		long  idx;
		jt1161((short)(sa + 2), (short)(scrx + 2),
		       (short)(far_x - 2), (short)(far_y - 2), color);
		idx = (long)((short)lvl[3] * cx + cy);
		if (jt276((short)idx) != 0)
			jt1161((short)(sa + 3), (short)(scrx + 3),
			       (short)(far_x - 3), (short)(far_y - 3), (short)15);
		break;
	}

	case 3: {                               /* special / visited marker */
		long idx = (long)((short)lvl[3] * cx + cy);
		if (jt276((short)idx) != 0) {
			jt1161((short)(sa + 3), (short)(scrx + 3),
			       (short)(far_x - 3), (short)(far_y - 3), (short)15);
		} else {
			const unsigned char *cell = lvl + idx * 6;
			if (cell[294] != 0) {
				/* a hollow 2px box border (four edge bands) */
				jt1161(sa, scrx, (short)(sa + 2), far_y, (short)15);
				jt1161(sa, (short)(far_y - 2), far_x, far_y, (short)15);
				jt1161((short)(far_x - 2), scrx, far_x, far_y, (short)15);
				jt1161(sa, scrx, far_x, (short)(scrx + 2), (short)15);
			}
		}
		break;
	}
	default:
		break;
	}
}
static void jt214(void)       { PROBE("jt214"); }              /* CODE 7+0x71c6 view setup */
static void jt124(long h)     { PROBE("jt124"); (void)h; }     /* CODE 6+0x3eea backdrop   */
/* JT[448] (CODE 3 + 0x148a) — blit glyph `glyph` (a font index) at (x,y) in
 * colour. Forwards to the glyph blitter by display mode: deep (jt1200()==3)
 * -> JT[995] with mode 2, else JT[1001]. Used for the automap party marker
 * (jt216, glyph 17+facing) and other index-glyph draws that were no-ops while
 * this was stubbed. */
static void jt448(short x, short y, short color, short glyph)
{
	PROBE("jt448");
	if (jt1200() == 3)
		jt995(x, y, color, glyph, (short)2);
	else
		jt1001(x, y, color, glyph);
}

/* JT[216] (CODE 7 + 0x5752) — draw the party-position marker on the automap.
 * Advances (screenX, screenY) by the cell offset * cellsize, picks a glyph
 * from the facing (17 + ((facing&6)>>1), +4 in the cellsize-12 layout), and
 * blits it via jt448. */
static void jt216(short cellX, short cellY, short screenX, short screenY, short facing)
{
	short glyph;

	PROBE("jt216");
	screenX = (short)(cellX * g_a5_word(-12272) + screenX);
	screenY = (short)(cellY * g_a5_word(-12272) + screenY);
	glyph   = (g_a5_word(-12272) == 12) ? (short)4 : (short)0;
	glyph  += (short)((facing & 6) >> 1);
	glyph  += (short)17;
	jt448(screenX, screenY, (short)12, glyph);
}

/* L3fd8 (CODE 22 + 0x3fd8) — the play-view (automap / dungeon-view) renderer.
 * Maps the view anchor to a screen top-left (jt1135), fills the view-interior
 * background (jt1161), stores the screen origin (g_a5_-12282/-12280) and the
 * visible window dimensions (g_a5_-12274/-12273 = clamp(view cells, map dim)),
 * sets the engine clip to the cell grid (jt1173), positions the backdrop
 * (jt1139 / JT[1001] / jt124 / jt1193), then loops the visible cells drawing
 * each through L4430 and finally the party marker (JT[216]).
 *
 * Structural lift (level 2): the setup / origin / clip / window / fill and the
 * cell-loop control flow are faithful; the per-cell tile draw (L4430) is a stub
 * pending its own lift, so the grid frames correctly but the wall/floor tiles
 * are blank. jt214 / jt124 (leaf setups) are stubs too. Args mirror jt304's
 * call: party (y,x,facing), the view anchor (v1,v2), the view-cell dims
 * (cells_a,cells_b), and the special/marker/batch/entry flags. */
static void l3fd8(short p_y, short p_x, short facing, short v1, short v2,
                  short cells_a, short cells_b, short special,
                  short pmark, short batch, short entry) __attribute__((unused));
static void l3fd8(short p_y, short p_x, short facing, short v1, short v2,
                  short cells_a, short cells_b, short special,
                  short pmark, short batch, short entry)
{
	const unsigned char *lvl;
	short sa, sb;                   /* fp@(-10)/(-12): screen top-left */
	short t1, t2;                   /* fp@(-6)/(-8) */
	short i, j, cs;

	PROBE("L3fd8");

	if ((batch & 0xff) == 0)
		jt112((short)1);        /* paint batch begin (JT[108] in asm; jt112 ok) */

	t1 = jt397(v1, (short)8000);
	t2 = jt397(v2, (short)8000);
	jt1135(t1, t2, &sa, &sb);

	cs = g_a5_word(-12272);

	/* view-interior background fill (colour 8) */
	jt1161((short)(((batch & 0xff) == 0 ? (short)(cs * 15) : (short)0) + sa),
	       sb,
	       (short)(cells_a * cs + sa),
	       (short)(cells_b * cs + sb), (short)8);

	if ((batch & 0xff) != 0)
		jt112((short)1);

	entry = jt397((short)1, jt413((short)4, (short)(entry & 0xff)));
	jt214();

	g_a5_word(-12282) = sa;
	g_a5_word(-12280) = sb;
	lvl = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
	g_a5_byte(-12274) = (unsigned char)jt413(jt397(cells_a, (short)1), (short)lvl[2]);
	g_a5_byte(-12273) = (unsigned char)jt413(jt397(cells_b, (short)1), (short)lvl[3]);

	/* clip to the visible cell grid */
	jt1173(sa, sb,
	       (short)((short)(unsigned char)g_a5_byte(-12274) * cs + sa),
	       (short)((short)(unsigned char)g_a5_byte(-12273) * cs + sb));

	/* position the backdrop under the cells */
	t1 = (short)(g_a5_word(-12278) * cs);
	t2 = (short)(g_a5_word(-12276) * cs);
	{
		short bx, by;
		jt1139(sa, sb, t1, t2, &bx, &by);
		jt1001((short)(8000 - bx), (short)(8000 - by),
		       *(short *)(uintptr_t)g_a5_long(-24260), (short)1);
	}
	jt124(g_a5_long(-24260));
	jt1193();

	/* per-cell render (L4430 stubbed) */
	if ((special & 0xff) != 4) {
		for (i = 0; i < (short)(unsigned char)g_a5_byte(-12274); i++) {
			for (j = 0; j < (short)(unsigned char)g_a5_byte(-12273); j++)
				l4430((short)(i + g_a5_word(-12278)),
				      (short)(j + g_a5_word(-12276)),
				      sa, (short)(j * cs + sb), (short)(special & 0xff));
			sa = (short)(sa + cs);
		}
	}

	/* party-position marker */
	if ((pmark & 0xff) != 0)
		jt216((short)(p_x - g_a5_word(-12278)),
		      (short)(p_y - g_a5_word(-12276)),
		      g_a5_word(-12282), g_a5_word(-12280), facing);

	jt117();
}

/* L52f2 (CODE 11 + 0x52f2) — draw one automap cell: map the cell's stored
 * (x,y) to the screen (jt296); if it lands in the map window, draw its column
 * number via jt1089 (colour alternates per map row). jt1139 re-derives the
 * row/col anchor the Mac feeds the text. */
static void l52f2(short cellX, short cellY, short width, short height,
                  short col, short rowhalf)
{
	short outX = 0, outY = 0, sx, sy;

	PROBE("L52f2");
	jt296(cellX, cellY, width, height, &outX, &outY);
	if (outX < 0 || outY < 0)
		return;
	sx = (short)(outX + g_a5_word(-12272));
	sy = (short)(outY + g_a5_word(-12272));
	jt1139((short)8000, (short)8000, outX, outY, &sx, &sy);
	jt1089((short)(sx + 8000), (short)(sy + 8000),
	       ((unsigned char)rowhalf != 0) ? (short)15 : (short)240,
	       "%d", (int)((unsigned char)col + 1));
}

/* JT[237] (CODE 11 + 0x5236) — the automap render (L63c0's cb1 in the
 * wilderness arm). jt304 setup, then walk the 5x8 visible cells drawing each
 * one's number (L52f2). jt1087 advances the map row each pass (stubbed, so the
 * skeleton repeats the first row until lifted); jt1148/jt1130 frame it. */
static void jt237(unsigned char *rec)
{
	short row, col;

	PROBE("jt237");
	jt304(rec, (short)0);
	jt1148();
	for (row = 0; row < 5; row++) {
		for (col = 7; col >= 0; col--) {
			unsigned char *cell = (unsigned char *)(uintptr_t)
			    (g_a5_long(-12300) + (long)col * 4);
			l52f2((short)cell[14], (short)cell[15],
			      (short)g_a5_byte(-11708), (short)g_a5_byte(-11707),
			      col, (short)(row & 1));
		}
		jt1087((short)2);
	}
	jt1130();
}

/* L53a6 (CODE 11 + 0x53a6) — the automap screen's title + area-list panel
 * (L63c0's wilderness-arm header). Fills a header panel (jt1161), prints the
 * design + module title (jt488 "%s %s" -> jt1089 "%*s"), then walks the 8
 * visible map areas printing each one's number + "(x, y) name" — the name is
 * a fixed string (jt273 set) or the per-area label table at g_a5_-10924.
 * Uses the already-lifted jt1161 / jt488 / jt1089; coords match the asm. */
static void l53a6(void)
{
	short       a1 = (short)8032;   /* fp@(-2): row anchor, advances down */
	short       a2 = (short)8092;   /* fp@(-4): column, constant          */
	short       i;
	const char *title;

	PROBE("L53a6");
	jt1161(a1, a2, (short)(a1 + 56), (short)(a2 + 64), (short)8);
	title = jt488("%s %s", (const char *)(uintptr_t)g_a5_long(-10804),
	              (const char *)(uintptr_t)g_a5_long(-10788));
	jt1089((short)(a1 + 2), a2, (short)139, "%*s", (int)16, title);
	jt1089((short)(a1 + 6), a2, (short)139, "%s",
	       (const char *)(uintptr_t)g_a5_long(-11140));
	a1 = (short)(a1 + 10);
	for (i = 0; i < 8; i++) {
		unsigned char *cell = (unsigned char *)(uintptr_t)
		    (g_a5_long(-12300) + (long)i * 4);
		const char    *name;

		jt1089(a1, a2, (short)240, "%d", (int)(i + 1));
		if (jt273()) {
			name = ua_strs_at(0x2b06);
		} else {
			short idx = (short)((cell[16] & 6) >> 1);
			name = (const char *)(uintptr_t)g_a5_long(-10924 + idx * 4);
		}
		jt1089((short)(a1 + 6), a2, (short)135, "(%2d, %2d) %5s",
		       (int)cell[15], (int)cell[14], name);
		a1 = (short)(a1 + 6);
	}
}

/* JT[241] (CODE 11 + 0x5514) — the play-action state machine. The play loop
 * calls it with a command code and a status-flags word; it sets up the
 * dungeon/wilderness view, lays the title (JT[394]) and command bar
 * (JT[179] + JT[148]), draws the area-list header (L53a6), then runs the
 * walk loop (L63c0 with the automap/cell callbacks JT[237] / JT[236]). On
 * return it reconciles the party<->cell occupancy tables and, when the
 * resolved command is 2, writes the party position back into the A5 world
 * (g_a5_-12288..). Returns the (possibly rewritten) command code.
 *
 * Args: cmd = command code (word, fp@8); flagsp = status-flags long
 * (fp@10); rec = the play record (fp@14).  rec layout touched here:
 *   rec[4]  area kind (0 wilderness / 1 dungeon)
 *   rec[17] state bits (bit 2 = "cells claimed")
 *   rec[39] "blocked" marker
 *   rec[46..51] party x/y/facing snapshot
 *   rec[i*4 + 52..54] per-slot occupancy (mirrors g_a5_-12300[i*4 +14..16]) */
static short jt241(short cmd, long *flagsp, unsigned char *rec) __attribute__((unused));
static short jt241(short cmd, long *flagsp, unsigned char *rec)
{
	char        title[26];          /* fp@(-26): "<area> <sub>" */
	short       i;                  /* fp@(-28): loop counter    */
	signed char t;                  /* fp@(-1):  walk result/nibble */

	PROBE("jt241");

	if (rec == NULL)                /* L5526 */
		return cmd;

	/* JT[1] sparse switch on the command code (CODE 1+0x130 dispatch):
	 * case 2 -> resume, case 11 -> first entry, default -> coerce to 2. */
	switch (cmd) {
	case 2:                         /* L5542 */
		rec[17] &= (unsigned char)~(1 << 2);
		if (rec[4] == 0)
			l476e(1, 1);
		rec[4] = 1;
		break;
	case 11:                        /* L5576 */
		cmd = 2;
		if (!(rec[17] & (1 << 2))) {
			/* first entry: claim the cells and seed each slot's
			 * occupancy from the live level table (-12300). */
			rec[17] |= (unsigned char)(1 << 2);
			for (i = 0; i < 8; i++) {
				unsigned char *re = rec + i * 4;
				const unsigned char *ae =
				    (const unsigned char *)(uintptr_t)g_a5_long(-12300)
				    + i * 4;
				*(long *)(re + 52) = *(const long *)(ae + 14);
			}
		}
		/* If the flags word carries a destination cell (low nibble set),
		 * stamp the party position into that level-table slot. */
		if ((*flagsp & 15) != 0) {
			unsigned char *ae;
			t  = (signed char)((*flagsp & 0xff0) >> 4);
			ae = (unsigned char *)(uintptr_t)g_a5_long(-12300) + t * 4;
			ae[15] = g_a5_12288;
			ae[14] = g_a5_12287;
			ae[16] = g_a5_12286;
		}
		break;
	default:                        /* L562c */
		cmd = 2;
		break;
	}

	/* --- L5632: build the view layers, command bar, header --- */
	l6256(rec[4], 1);               /* register the walk sources (dungeon kind) */
	jt108(1);
	jt112(1);
	l429c(0, rec[4]);
	jt394(title, ua_strs_at(0x2b08),                /* "%s %s" */
	      (const char *)(uintptr_t)g_a5_long(-10696),
	      (const char *)(uintptr_t)g_a5_long(-10692));
	jt179(1);                                       /* seed command-bar slots */
	jt148(g_a5_long(-13952), title, 0);             /* prompt line */
	jt449(1);
	l53a6();                                        /* area-list header panel */
	jt112(0);
	jt117();

	*flagsp &= ~0xffff0000L;                        /* clear flags' high word */

	/* --- the walk loop; cb1=automap (JT[237]), cb2=cell-block (JT[236]) --- */
	t = l63c0(rec, 1, 1, 0,
	          (long)(uintptr_t)&jt237, (long)(uintptr_t)&jt236);

	if (t == 0) {                                   /* L57ce: walk returned 0 */
		if (rec[17] & (1 << 2)) {
			/* restore each slot's occupancy from the rec mirror. */
			for (i = 0; i < 8; i++) {
				unsigned char *ae =
				    (unsigned char *)(uintptr_t)g_a5_long(-12300)
				    + i * 4;
				const unsigned char *re = rec + i * 4;
				*(long *)(ae + 14) = *(const long *)(re + 52);
			}
		}
	} else {
		/* JT[3] on the walk result: 1 -> occupancy compare, else blocked. */
		if (t == 1) {                           /* L5704 */
			signed char changed = 0;
			for (i = 0; i < 8 && changed == 0; i++) {
				const unsigned char *ae =
				    (const unsigned char *)(uintptr_t)g_a5_long(-12300)
				    + i * 4;
				const unsigned char *re = rec + i * 4;
				if (ae[15] != re[53]) { changed = 1; continue; }
				if (ae[14] != re[52]) { changed = 1; continue; }
				if (ae[16] != re[54]) { changed = 1; }
			}
			if (changed) {                  /* L57a4 */
				((unsigned char *)flagsp)[3] |= 1;
				rec[39] = 0xff;
			}
		} else {                                /* L57bc */
			*flagsp |= 3;
			cmd = 11;
		}
	}

	/* --- L5812: cleanup + (cmd==2) party-position write-back --- */
	jt451();
	if (cmd == 2) {
		unsigned char *dst = &g_a5_12288;       /* -12288 .. -12283 */
		for (i = 0; i < 6; i++)
			dst[i] = rec[46 + i];
		if (jt273() == 0)                       /* L5836 */
			l4810(rec + 4, 0L);
		if (rec[4] == 0)                        /* L5846 */
			l476e(1, 0);
	}
	return cmd;                                     /* L5860 / L5864 */
}

/* L5126 (CODE 11 + 0x5126) — the deep dungeon status-header panel jt240 draws
 * above the first-person view. Fills the header rect (JT[1161]) then paints
 * four text rows via JT[1089]: the area-name line (g_a5_-10640/-11136/-10816),
 * a second descriptor line (-10556/-10640/-10780), the party-position line
 * (format string at g_a5_-11332 with rec[46]=x, rec[47]=y), and a status line
 * (-10820 + a light/condition word indexed by rec[48] bits 1-2 into the
 * g_a5_-10924 table). Faithful to the asm; the strings are the runtime
 * area-state globals the area-load seeds. */
static void l5126(unsigned char *rec)
{
	const short top = 8058, left = 8092;            /* fp@(-2), fp@(-4) */

	PROBE("L5126");
	jt1161((short)(top + 4), left, (short)(top + 26),
	       (short)(left + 64), (short)8);
	jt1089((short)(top + 4), left, (short)139, "%s %s %s",
	       (const char *)(uintptr_t)g_a5_long(-10640),
	       (const char *)(uintptr_t)g_a5_long(-11136),
	       (const char *)(uintptr_t)g_a5_long(-10816));
	jt1089((short)(top + 12), left, (short)135, "%s %s %s",
	       (const char *)(uintptr_t)g_a5_long(-10556),
	       (const char *)(uintptr_t)g_a5_long(-10640),
	       (const char *)(uintptr_t)g_a5_long(-10780));
	jt1089((short)(top + 16), left, (short)135,
	       (const char *)(uintptr_t)g_a5_long(-11332),
	       (int)rec[46], (int)rec[47]);
	jt1089((short)(top + 20), left, (short)135, "%s %5s",
	       (const char *)(uintptr_t)g_a5_long(-10820),
	       (const char *)(uintptr_t)g_a5_long(-10924 + ((rec[48] & 6) >> 1) * 4));
}

/* JT[240] (CODE 11 + 0x4ffe) — the DEEP (first-person) dungeon walk driver,
 * the dungeon counterpart of jt241 (which is the top-down wilderness walker).
 * It registers the walk input sources (l6256), lays the title + command bar +
 * the deep status header (l5126), then spins the faithful walk loop L63c0 with
 * a_deep = 1 (first-person) and the cell-block callback JT[236]. On a moving
 * exit it advances the party-position snapshot in the record (rec[46..51]) and
 * flags the redraw; on a non-moving exit it restores the party position from
 * that snapshot. Returns the command code unchanged.
 *
 * Args mirror jt241: cmd (word, fp@8), flagsp (long, fp@10), rec (long, fp@14).
 * NOTE: this is the driver; the deep first-person STEP itself lives in L63c0's
 * deep arms (L64f2..L666c), which are still deferred — so jt240 runs the loop
 * but the per-cell step lands once those arms are lifted. */
static short jt240(short cmd, long *flagsp, unsigned char *rec) __attribute__((unused));
static short jt240(short cmd, long *flagsp, unsigned char *rec)
{
	char        title[24];          /* fp@(-20) */
	signed char awild = 1;          /* fp@(-21): wilderness/deep flag */

	PROBE("jt240");

	if (rec == NULL)                /* L5010 */
		return cmd;

	l6256(rec[4], 0);               /* register the dungeon walk sources */
	jt108(1);
	jt112(1);
	l429c(0, rec[4]);
	jt394(title, ua_strs_at(0x2ac0),               /* "%s" */
	      (const char *)(uintptr_t)g_a5_long(-10692));
	/* Seed command-bar slots in g_a5_-24126 the faithful jt953 way: jt155(1..7)
	 * writes slot VALUES 1..7, so L2184 extracts the command words at uppercase
	 * positions 1..7 of the string ("Move Area Cast View Encamp Search Look
	 * Inv") = Area..Inv — SKIPPING "Move" (index 0), the implicit walk default.
	 * That is exactly what the Mac HUD shows (Area..Inv), and the 7 words fit
	 * 320px where all 8 (jt179(7)) overran the right edge past INV. */
	{
		unsigned char cnt = 0;
		short ci;
		for (ci = 1; ci <= 7; ci++)
			jt155(ci, &cnt);
	}
	/* PORT: the faithful jt240 lays a one-line TITLE here (jt148(-10484,
	 * title)); the Mac deep walk reaches the "Move Area Cast..." command bar
	 * only through jt953/jt164. The port routes the dungeon walk through jt240
	 * (arrow movement), so to surface the real command bar in the walk we run
	 * jt164's BUILD here (minus its l23b4 modal — l63c0's own poll drives it):
	 * l206e lays the A5-13764 command string ("Move Area Cast View Encamp
	 * Search Look Inv") into command DLItems + caches it in g_a5_-13000 (what
	 * l63c0's jt152 classifier reads), l1f3e sizes the bar to the party, and
	 * the four shape-5 bevel-frame items draw the bar frame. */
	{
		static unsigned char cbar_buf[80];
		unsigned char defitem = 0;

		g_a5_19172 = 8016;
		g_a5_19174 = 8068;
		l2858((short)1);
		l206e(g_a5_long(-13764), cbar_buf,
		      (const char *)(uintptr_t)g_a5_long(-13952), &defitem);
		l1f3e((short)g_a5_19172, (short)g_a5_19174);
		if (g_a5_12911 != 0) {
			jt452((long)5, (long)8000, (long)8000, (long)50, (long)20,
			      (long)41, (long)22, (long)20,
			      (long)5, (long)8000, (long)8020, (long)50, (long)28,
			      (long)41, (long)11, (long)20,
			      (long)5, (long)8000, (long)8048, (long)50, (long)20,
			      (long)41, (long)21, (long)20,
			      (long)5, (long)8050, (long)8000, (long)30, (long)68,
			      (long)41, (long)23, (long)20,
			      (long)0);
		}
	}
	jt449(1);
	l5126(rec);                                     /* deep status header panel */
	jt112(0);
	jt117();

	*flagsp &= ~0xffff0000L;                        /* clear flags' high word */

	/* the deep walk loop: cb1 = default (jt238), cb2 = cell-block (JT[236]) */
	if (l63c0(rec, awild, 1, 1, 0, (long)(uintptr_t)&jt236) != 0) {
		/* moved: shift the old snapshot down, refresh it from the live
		 * party position, and flag the redraw. */
		short i;
		for (i = 0; i < 6; i++)
			rec[40 + i] = rec[46 + i];
		for (i = 0; i < 6; i++)
			rec[46 + i] = (&g_a5_12288)[i];
		((unsigned char *)flagsp)[3] |= 1;
	} else {
		/* no move: restore the party position from the snapshot. */
		short i;
		for (i = 0; i < 6; i++)
			(&g_a5_12288)[i] = rec[46 + i];
	}

	jt451();
	return cmd;                                     /* L5122 */
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

/* The play/area-state record the engine reaches through g_a5_-28006. The Mac
 * stands it up in L4cc0 (CODE 6 + 0x4cc0) as JT[387](1024) stored base-1 so
 * the asm's field offsets are 1-based; every reader here is lifted C using the
 * same p[N] offsets, so a plain buffer is equivalent (no faithful asm writes
 * it). l0bbc / jt948 read h[17] facing, h[34]/h[36] mode flags, h[67]/h[68]
 * saved party x/y, h[133] stair dir, h[134] "view established". */
static unsigned char g_area_state[1024];
static int           g_savgame_loaded;   /* a BasiliskII save was resumed */

#ifdef FRUA_L6234_VERIFY
static short l3198(short kind, long p1, long p2);   /* event poll (defined below) */
/* port_l6234_verify — geometry-verification harness for the faithful
 * first-person render (jt221 -> L6234). Loads a real level so L5e52 has map
 * data, engages deep mode (g_a5_2347=0 -> jt1200()==3 -> the L6234 branch),
 * drops the party at a known dungeon spot, loads the colour wall pieces, then
 * renders via jt221 (which logs the slot-layout globals + dispatches to L6234)
 * and holds the frame for a screenshot. Lets us confirm L6234 walks the map
 * and assembles slots correctly (geometry) before L6eea (textures) lands. */
void port_l6234_verify(void)
{
	unsigned char *pl, *px;
	short pitch, sw, sh;
	short k;
	static const signed char drow[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
	static const signed char dcol[8] = {  0,  1, 1, 1, 0, -1, -1, -1 };

	pl = (unsigned char *)g_a5_28006;
	if (pl != NULL)
		pl[134] = 0;
	g_a5_18485 = 0;
	g_a5_18878 = 5;                         /* a DUNGEON level (<=4 = overland, no wall sets) */
	g_a5_18488 = 0;
	l0bbc();                                /* load the dungeon level + place party */

	g_a5_2347 = 0;                          /* deep dungeon-view mode */
	g_a5_byte(-12290) = 0;                  /* dungeon (not automap) branch */
	for (k = 0; k < 8; k++) {
		g_a5_byte(-27862 + k) = (unsigned char)drow[k];
		g_a5_byte(-27853 + k) = (unsigned char)dcol[k];
	}
	/* Diagnostic: dump the loaded map's cell records to see whether wall
	 * data is present and which nibble it lives in (L5e52 reads the low
	 * nibble; jt212 the high). cell records start at ds+290, 6 bytes each. */
	{
		const unsigned char *ds = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
		short mw = ds ? ds[2] : 0, mh = ds ? ds[3] : 0;
		const unsigned char *base = ds ? ds + 290 : NULL;
		long total = (long)mw * mh * 6, i, nnz = 0, nlo = 0, nhi = 0, k = 0;
		dbg_log_num("map mw = ", mw);
		dbg_log_num("map mh = ", mh);
		for (i = 0; base && i < total; i++) {
			unsigned char bb = base[i];
			if (bb != 0) nnz++;
			if (bb & 0x0f) nlo++;
			if (bb & 0xf0) nhi++;
		}
		dbg_log_num("cell bytes nonzero = ", nnz);
		dbg_log_num("  low-nibble set   = ", nlo);
		dbg_log_num("  high-nibble set  = ", nhi);
		for (i = 0; base && i < total && k < 10; i++)
			if (base[i] != 0) {
				dbg_log_num("  nz cell off = ", i);
				dbg_log_num("       val   = ", (long)base[i]);
				k++;
			}
	}

	/* Use l0bbc's REAL party placement (it loads the design and seeds the
	 * party cell + facing at -12288/-12287/-12286) rather than a synthetic
	 * corner scan — a real in-map vantage is what gives jt199 walls to walk.
	 * The earlier scan overwrote the loaded spot with the map's first wall
	 * cell, which sat at the (0,0) corner and saw almost nothing. */
	dbg_log_num("verify party x=", (long)(signed char)g_a5_byte(-12288));
	dbg_log_num("  party y=", (long)(signed char)g_a5_byte(-12287));
	dbg_log_num("  facing =", (long)(signed char)g_a5_byte(-12286));

	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == NULL)
		return;

	/* Faithful COLOUR render: L6148 loads the wall set's .CTL (8bpp colour)
	 * into the -27894 handles; jt199 walks the frustum and jt200 -> jt114 ->
	 * l309c_tile's 8bpp arm blits the clut-129 colour tiles straight into the
	 * 8bpp framebuffer (g_cwf_px). The game palette (clut 129) is installed at
	 * boot, so present directly. (No cw_* stand-in, no DUNGCOM.) */
	{
		short row = (short)(signed char)g_a5_byte(-12287);
		short col = (short)(signed char)g_a5_byte(-12288);
		short f   = (short)(g_a5_byte(-12286) & 7);
		long i;

		/* Install the wall set's RGB band (sub-GLIB item 0) at clut 32 so the
		 * tile bytes (32..) index real colours, and clear the screen. MUST use
		 * the SAME library file as L6eea loads the tiles from: wallset_for_id
		 * maps ds[4] to (file,set); load_color_wallset reads its band from the
		 * global g_cw_file, so set it first — otherwise the band comes from the
		 * other .CTL (8X8DC vs 8X8DB) and every wall is mis-tinted (blue). */
		{
			const unsigned char *ds = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
			if (ds)
				load_wall_groups(ds);   /* Wall1/2/3 CLUTs -> clut bands 32/64/96 */
		}
		(void)i;
		/* render_3d_faithful (via jt935 -> jt221) now draws the FRAME.CTL
		 * chrome itself, once, after the wall load, then the view into the
		 * 88x88 hole — so no explicit chrome draw here. */
		l6148();                                /* load the .CTL wall sets (for dump) */
		/* Dump the perspective layout table (-12240..-12196, word each) and
		 * the level's three wall-set ids (ds[4..6]) — if the table is zero the
		 * slot coords collapse to the top-left and only soff separates them. */
		{
			const unsigned char *ds = (const unsigned char *)(uintptr_t)g_a5_long(-12300);
			short off;
			dbg_log_num("jt1200 = ", (long)jt1200());
			dbg_log_num("ds[4] wall1 = ", ds ? (long)ds[4] : -1);
			dbg_log_num("ds[5] wall2 = ", ds ? (long)ds[5] : -1);
			dbg_log_num("ds[6] wall3 = ", ds ? (long)ds[6] : -1);
			dbg_log_num("band slot0 sid = ", (long)g_cw_sid[0]);
			dbg_log_num("  band[0] r = ", (long)g_cw_sr[0][0]);
			dbg_log_num("  band[0] g = ", (long)g_cw_sg[0][0]);
			dbg_log_num("  band[0] b = ", (long)g_cw_sb[0][0]);
			dbg_log_num("  band[8] r = ", (long)g_cw_sr[0][8]);
			dbg_log_num("  band[8] g = ", (long)g_cw_sg[0][8]);
			dbg_log_num("  band[8] b = ", (long)g_cw_sb[0][8]);
			dbg_log_num("hdl grp0 = ", g_a5_long(-27894));
			dbg_log_num("hdl grp1 = ", g_a5_long(-27890));
			dbg_log_num("hdl grp2 = ", g_a5_long(-27886));
			for (off = 12240; off >= 12196; off -= 2)
				dbg_log_num((off == 12240 ? "layout[-12240..] = " : "  ... = "),
				            (long)g_a5_word(-off));
		}
		(void)row; (void)col; (void)f;

		/* Begin-Adventuring reachability proof: seed the design + party the
		 * way l07dc does, then confirm jt918's case-10 gate inputs are live
		 * (g_a5_27932 = design -> enables g_a5_14430; g_a5_27928 = party).
		 * With both set, l115a (case 10) returns 1 and l07dc proceeds to
		 * jt948 -> jt935 -> jt221 -> the faithful dungeon render. */
		{
			extern void port_test_seed_design(void);
			port_test_seed_design();
			dbg_log("=== Begin-Adventuring gate ===");
			dbg_log_num("  design g_a5_27932 = ", g_a5_long(-27932));
			dbg_log_num("  party  g_a5_27928 = ", g_a5_long(-27928));
		}

		/* Render through the REAL play-refresh entry (jt935 -> jt221 ->
		 * render_3d_faithful), not jt199 directly, so the path Begin
		 * Adventuring reaches is what we exercise. g_a5_27990=4 = the
		 * command-bar render mode; -27982=0 = not mid-transition; -12290=0 =
		 * dungeon (not automap). */
		g_cwf_px = NULL;                         /* jt221 sets its own */
		g_a5_27990 = 4;
		g_a5_byte(-27982) = 0;
		g_a5_byte(-12290) = 0;
		{
			unsigned char *hh = (unsigned char *)g_a5_28006;
			if (hh != NULL) hh[36] = 0;     /* avoid jt935's first-entry redraw arm */
		}
		dbg_log("=== FRUA_L6234_VERIFY: jt935 -> jt221 (play path) ===");
		jt935();
		dbg_log("=== jt935 returned ===");
#ifdef FRUA_HOLD_JT935
		/* Gap-A diagnostic: freeze on jt935 -> jt221 -> render_3d_faithful's
		 * output (the play-path view, BEFORE jt948/jt953/jt164 touch it). */
		qd_present();
		dbg_log("=== HOLD: jt935/jt221 render ===");
		for (;;)
			qd_present();
#endif

		/* Movement test: drive jt297 (the keyboard mover) first-person and
		 * confirm the party turns + steps. A minimal ctx (rec[4]=1 first-
		 * person, rec[5]=1 so l1908 skips the jt312 redraw here, rec[9]=0),
		 * deep mode (jt1160 false via -2592 bit1 clear). 258=turn right,
		 * 264=forward. */
		{
			static unsigned char trec[128];
			static unsigned char *tctx;
			tctx = trec;
			/* rec[4]=0 = the normal first-person walk (no view/party
			 * snapshot decoupling), rec[5]=1 so l1908 skips jt312 here. */
			trec[4] = 0; trec[5] = 1; trec[9] = 0;
			{ int kk; for (kk = 0; kk < 6; kk++) trec[46 + kk] = g_a5_byte(-12288 + kk); }
			g_a5_byte(-2592) = (unsigned char)(g_a5_byte(-2592) & ~0x02);
			dbg_log("=== jt297 movement test ===");
			dbg_log_num("  facing before = ", (long)g_a5_byte(-12286));
			dbg_log_num("  x before = ", (long)(signed char)g_a5_byte(-12288));
			dbg_log_num("  y before = ", (long)(signed char)g_a5_byte(-12287));
			jt297(&tctx, 258, 0);                  /* turn right */
			dbg_log_num("  facing after turn = ", (long)g_a5_byte(-12286));
			jt297(&tctx, 264, 0);                  /* step forward */
			dbg_log_num("  x after fwd = ", (long)(signed char)g_a5_byte(-12288));
			dbg_log_num("  y after fwd = ", (long)(signed char)g_a5_byte(-12287));
			dbg_log_num("  facing after fwd = ", (long)g_a5_byte(-12286));
		}

#ifdef FRUA_PLAY_JT948
		/* Deterministic repro of the live "Begin Adventuring -> dungeon"
		 * crash: drive the real adventure loop. The probe trace's last line
		 * before the run_probe SIGKILL pinpoints where it dies/hangs. */
		/* The FRUA_L6234_VERIFY geometry pass above zeroed h[134] (line ~10034)
		 * for its FRESH placement test; the real l07dc->jt918->jt948 flow never
		 * runs that pass, so re-assert the resume state a loaded save set up,
		 * exercising l0bbc's RESUME branch here the way the real flow does. */
		if (g_savgame_loaded) {
			g_a5_28006 = g_area_state;
			g_area_state[134] = 1;
		}
		dbg_log("=== FRUA: jt948 (adventure loop) ===");
		jt948();
		dbg_log("=== jt948 returned ===");
#endif

		/* Interactive first-person dungeon walk on the faithful 88x88 render.
		 * Deep mode (jt1160 false) so jt297 takes the first-person arms ->
		 * L1908: Up = forward (264), Left/Right = turn (262/258), Down =
		 * about-face (260). Poll keys via l3198 (jt1125 maps arrows to
		 * 257..264), move, re-render. Esc holds the last frame. */
		{
			static unsigned char wrec[128];
			static unsigned char *wctx;
			short ky, kx, ev, kc;
			int kk;

			wctx = wrec;
			wrec[4] = 0; wrec[5] = 1; wrec[9] = 0;
			g_a5_byte(-2592) = (unsigned char)(g_a5_byte(-2592) & ~0x02);
			(void)px; (void)pitch; (void)sw; (void)sh;
			/* Switch to the NATIVE display mode (g_a5_-2347 = 1 -> jt1135
			 * scale 2, jt1200() != 3) the real play screen uses — the verify
			 * pass above ran deep (g_a5_-2347 = 0). This proves jt312 now
			 * renders under the live-play condition: the view stays deep via
			 * g_cwf_force_deep and the gate keys off jt1160() (first-person),
			 * not the old jt1200()==3 that native scale never satisfies. */
			g_a5_2347 = 1;
			/* Route the walk through the LIVE jt312 render so the harness
			 * exercises the production path (#103): jt312 fetches its own back
			 * page, draws the FRAME.CTL chrome on the first/forced frame and
			 * DOUBLE-presents (both flip pages get the chrome), then on every
			 * subsequent frame rect-presents just the 88x88 viewport. Forcing
			 * the wall ids to 0xFF makes jt312 reload + set g_view_force_full
			 * for the first frame. */
			g_cw_grp[0] = g_cw_grp[1] = g_cw_grp[2] = 0xff;
			jt312(NULL);
			dbg_log("=== dungeon walk: arrows move, []/,. nudge view, Esc holds ===");
			for (;;) {
				ky = 0; kx = 0;
				ev = l3198((short)7, (long)&ky, (long)&kx);
				if (ev == 1 || ev == 2) {            /* keyDown */
					kc = ky;                     /* arrows -> 257..264 */
					if (kc == 27)
						break;
					/* live view-offset tuning: [ / ] shift X, , / . shift Y.
					 * Force a full present so both pages pick up the shift. */
					if (kc == '[' || kc == ']' || kc == ',' || kc == '.') {
						if (kc == '[') g_cwf_ox--;
						else if (kc == ']') g_cwf_ox++;
						else if (kc == ',') g_cwf_oy--;
						else g_cwf_oy++;
						dbg_log_num("view ox=", (long)g_cwf_ox);
						dbg_log_num("     oy=", (long)g_cwf_oy);
						g_view_force_full = 1;
						jt312(NULL);
					} else if (kc >= 257 && kc <= 264) {
						for (kk = 0; kk < 6; kk++)
							wrec[46 + kk] = g_a5_byte(-12288 + kk);
						jt297(&wctx, kc, 0);
						/* movement: jt312 rect-presents the viewport only —
						 * the case the double-buffer fix must keep framed. */
						jt312(NULL);
					}
				}
			}
		}

		qd_present();
		for (;;)
			qd_present();
	}
}
#endif

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

/* Real-play flag: set by port_begin_adventure (Begin Adventuring) so the
 * shared dungeon loop shows the party HUD and drops the wall-set browse
 * keys; clear for the regression browse demo. */
static int g_adventure_mode;

/* The QuickDraw shim's embedded 8x8 fallback font (compat/font_8x8.c),
 * one byte per row, MSB-first. */
extern const unsigned char qd_font_8x8[256][8];

/* hud_text — blit a string straight to the chunky screen buffer at (x,y)
 * in `color` (a clut index), clipped to the screen. Bypasses jt94 / the
 * QuickDraw text path, which clips to the 3D viewport and assumes the menu
 * palette — neither holds in the deep dungeon view, so the HUD draws its
 * own glyphs in dedicated HUD-palette colours. Uses the real Mac font
 * (every glyph) when loaded, else the sparse 8x8 fallback. */
static void hud_text(unsigned char *px, short pitch, short sw, short sh,
                     short x, short y, unsigned char color, const char *s)
{
	if (g_mac_font_loaded) {
		short h = g_mac_font.height;
		for (; *s != 0 && x < sw; s++) {
			short c   = (short)(unsigned char)*s;
			short gw  = mac_font_strike_width(c);
			short adv = mac_font_advance(c);
			short row, col;
			for (row = 0; row < h && y + row < sh; row++)
				for (col = 0; col < gw && x + col < sw; col++)
					if (mac_font_pixel(c, col, row))
						px[(long)(y + row) * pitch
						   + x + col] = color;
			x = (short)(x + (adv > 0 ? adv : gw + 1));
		}
		return;
	}
	for (; *s != 0 && x + 8 <= sw; s++, x = (short)(x + 8)) {
		const unsigned char *g = qd_font_8x8[(unsigned char)*s];
		short row, col;
		for (row = 0; row < 8 && y + row < sh; row++) {
			unsigned char  bits = g[row];
			unsigned char *dst  = px + (long)(y + row) * pitch + x;
			for (col = 0; col < 8; col++)
				if (bits & (unsigned char)(0x80 >> col))
					dst[col] = color;
		}
	}
}

/* HUD clut slots — high indices the dungeon wall palette doesn't touch, so
 * the panel text stays a stable white/gold regardless of the loaded wall
 * set's colours (which run roughly clut 0..165). */
#define HUD_CLUT_WHITE 254
#define HUD_CLUT_GOLD  255

/* Draw the active-party status panel down the right of the dungeon view
 * (the ~220px viewport leaves x>=226 free): a gold header, then each
 * member's name (white) and HP/AC. Drawn straight to the chunky surface
 * after jt312's viewport render. Two full presents push it into both flip
 * buffers (jt312's rect-present only refreshes the viewport). */
static void draw_party_panel(void)
{
	unsigned char *px; short pitch, sw, sh, y;
	unsigned char *party[16];
	short          n, i;
	const short    X = 226;
	RGBColor       hud[2];

	if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == 0 || sw <= X)
		return;

	hud[0].red = hud[0].green = hud[0].blue = 0xffff;           /* white */
	hud[1].red = 0xffff; hud[1].green = 0xd000; hud[1].blue = 0; /* gold */
	qd_set_palette(hud, HUD_CLUT_WHITE, 2);

	/* clear the panel column (clut 0 = black) so no stale pixels show */
	for (y = 0; y < sh; y++)
		memset(px + (long)y * pitch + X, 0, (size_t)(sw - X));

	n = cg_collect_party(party, 16);
	hud_text(px, pitch, sw, sh, X, (short)4, HUD_CLUT_GOLD, "PARTY");
	for (i = 0; i < n; i++) {
		char  hp[20];
		short ty = (short)(16 + i * 18);
		hud_text(px, pitch, sw, sh, X, ty, HUD_CLUT_WHITE,
		         (const char *)&party[i][96]);
		sprintf(hp, "HP%d AC%d", (int)party[i][CHAR_HP],
		        (int)party[i][CHAR_AC]);
		hud_text(px, pitch, sw, sh, X, (short)(ty + 9), HUD_CLUT_WHITE,
		         hp);
	}
	qd_present();
	qd_present();   /* both flip buffers (jt312 rect-presents the viewport) */
}

/* ---- Encounters (play-engine, first slice) -------------------------
 *
 * The faithful tactical combat (CODE 15-19: the combat grid, initiative,
 * spells, the per-level monster-data tables) is the large deferred
 * remainder. This is a self-contained encounter loop hung off dungeon
 * movement: stepping into a monster zone may spring an encounter, and the
 * party can fight (a simplified round-based auto-resolve over the real
 * party HP / level / AC) or try to flee. Outcomes — HP loss, deaths —
 * persist to the roster. Drawn on a black screen via hud_text so the
 * dungeon palette is left intact (no menu-palette switch to restore).
 *
 * Monster types stand in for the real MONSTnnn.DAT tables until those
 * lift; the cell's monster zone scales which one + the group size. */
static const struct { const char *name; short hd; short dmg; } k_monsters[] = {
	{ "Giant Rats", 1,  2 },
	{ "Kobolds",    1,  3 },
	{ "Goblins",    1,  4 },
	{ "Skeletons",  1,  5 },
	{ "Orcs",       2,  6 },
	{ "Hobgoblins", 2,  7 },
	{ "Gnolls",     3,  8 },
	{ "Ogres",      4, 10 },
};
#define N_MONSTERS ((short)(sizeof k_monsters / sizeof k_monsters[0]))

/* Real monsters loaded from the design's MONSTnnn.DAT records (read straight
 * off disk; the sample files are stored uncompressed at the full 450 bytes).
 * Decoded fields:
 *   +96  name    C-string
 *   +137 THAC0   AD&D to-hit (14/13/11/8 across the sample) — confident
 *   +135 dmg     per-hit damage die sides
 *   +385 AC      the combatant AC offset — confirmed from combat (CODE 16
 *                jt608 reads defender AC@385 for the to-hit; CODE 18 armor
 *                spells modify @385 by +/-2..4). It reads 0 in these four
 *                template files: either AC 0, or the template's AC is filled
 *                into @385 by the combat-setup conversion (unlifted), so we
 *                fall back to a THAC0-derived AC when it's 0.
 *   +395 HP      (63/59/50/34) — the combatant HP offset.
 * Faithful in-combat layout (CODE 19 record sheet): AC@385, HP@395,
 * THAC0=60-[384], move@396, encumbrance@86. */
static struct { char name[24]; short hp, thac0, dmg, ac; } g_mdb[24];
static short g_mdb_n = -1;     /* -1 = not yet scanned */

static void load_monsters(void)
{
	short num;

	if (g_mdb_n >= 0)
		return;
	g_mdb_n = 0;
	for (num = 100; num <= 140 && g_mdb_n < 24; num++) {
		static unsigned char rec[450];
		char  fn[16];
		short refnum = 0, c;
		long  n;

		fn[0] = 12;                          /* "MONSTnnn.DAT" */
		fn[1]='M'; fn[2]='O'; fn[3]='N'; fn[4]='S'; fn[5]='T';
		fn[6] = (char)('0' + (num / 100) % 10);
		fn[7] = (char)('0' + (num / 10)  % 10);
		fn[8] = (char)('0' + num % 10);
		fn[9]='.'; fn[10]='D'; fn[11]='A'; fn[12]='T';
		if (FSOpen((ConstStr255Param)fn, 0, &refnum) != noErr)
			continue;
		n = 450;
		if (FSRead(refnum, &n, rec) == noErr && n == 450 && rec[96]) {
			char *d = g_mdb[g_mdb_n].name;
			short v;
			for (c = 0; c < 22 && rec[96 + c]; c++)
				d[c] = (char)rec[96 + c];
			d[c] = 0;
			if (d[0] >= 'a' && d[0] <= 'z')  /* capitalise the name */
				d[0] = (char)(d[0] - 32);
			v = (short)rec[CHAR_AC];                       /* HP    */
			g_mdb[g_mdb_n].hp    = (v < 1) ? 8 : v;
			v = (short)rec[137];                       /* THAC0 */
			g_mdb[g_mdb_n].thac0 = (v < 2 || v > 20) ? 19 : v;
			v = (short)rec[135];                       /* dmg die */
			g_mdb[g_mdb_n].dmg   = (v < 2) ? 4 : (v > 12 ? 12 : v);
			g_mdb[g_mdb_n].ac    = (short)rec[CHAR_HP];    /* AC (0 = fall back) */
			g_mdb_n++;
		}
		(void)FSClose(refnum);
	}
}

/* Fill the screen black and set the white/gold HUD clut for encounter text. */
static unsigned char *encounter_screen(short *pitch, short *sw, short *sh)
{
	unsigned char *px; short y;
	RGBColor       hud[2];

	if (!qd_screen_pixels(&px, pitch, sw, sh) || px == 0)
		return NULL;
	hud[0].red = hud[0].green = hud[0].blue = 0xffff;            /* white */
	hud[1].red = 0xffff; hud[1].green = 0xd000; hud[1].blue = 0; /* gold  */
	qd_set_palette(hud, HUD_CLUT_WHITE, 2);
	for (y = 0; y < *sh; y++)
		memset(px + (long)y * *pitch, 0, (size_t)*sw);
	return px;
}

/* port_play_message — a two-line notice during play (black screen + the
 * hud_text glyph blitter). Used instead of cg_message in the dungeon: the
 * deep view (g_a5_2347 == 0) is jt94's broken-text mode, so the chrome
 * cg_message would draw blank. Waits for a key. */
static void port_play_message(const char *l1, const char *l2)
{
	unsigned char *px; short pitch, sw, sh;
	unsigned char  scan = 0, ascii = 0;

	while (plat_kb_poll(&scan, &ascii))
		;
	px = encounter_screen(&pitch, &sw, &sh);
	if (px) {
		hud_text(px, pitch, sw, sh, 20, 60, HUD_CLUT_WHITE, l1);
		if (l2)
			hud_text(px, pitch, sw, sh, 20, 80, HUD_CLUT_WHITE, l2);
		qd_present(); qd_present();
	}
	while (!plat_kb_poll(&scan, &ascii))
		;
}

/* port_run_encounter — announce, then Fight (auto-resolve) or Run. */
static void port_run_encounter(short zone)
{
	unsigned char *party[16];
	short          nparty = cg_collect_party(party, 16);
	unsigned char *px; short pitch, sw, sh, i;
	unsigned char  scan = 0, ascii = 0;
	short          mcount, hp_each, round, mthac0, mdmg, mac;
	const char    *mname;
	long           mhp, xp_award = 0;
	int            fled = 0, victory = 0;
	char           line[48];

	if (nparty == 0)
		return;

	/* pick a monster — a real MONSTnnn.DAT one (true HP + THAC0 + damage)
	 * if the design has any, else a synthesized stand-in; the zone scales
	 * the group size. The monster's own AC isn't pinned on disk yet, so
	 * it's derived from THAC0 (tougher attacker -> better armour). */
	load_monsters();
	mac = 0;
	if (g_mdb_n > 0) {
		short mi = (short)ua_rand(g_mdb_n);
		mname  = g_mdb[mi].name;
		hp_each = g_mdb[mi].hp;
		mthac0 = g_mdb[mi].thac0;
		mdmg   = g_mdb[mi].dmg;
		mac    = g_mdb[mi].ac;       /* AC@385; 0 -> derive below */
	} else {
		short mi = (short)(zone % N_MONSTERS);
		mname  = k_monsters[mi].name;
		hp_each = (short)(k_monsters[mi].hd * 4 + 3);
		mthac0 = (short)(20 - k_monsters[mi].hd);
		mdmg   = k_monsters[mi].dmg;
	}
	if (mac <= 0)                        /* template AC unset -> derive */
		mac = (short)(mthac0 - 4);
	if (mac < 2)  mac = 2;
	if (mac > 10) mac = 10;
	/* real records can be boss-tier (50+ HP), so keep the group small */
	mcount  = (short)(1 + (short)ua_rand(hp_each >= 30 ? 2 : 4) + (zone & 1));
	if (mcount > 5) mcount = 5;
	mhp     = (long)mcount * hp_each;

	/* announce + choice */
	while (plat_kb_poll(&scan, &ascii))
		;
	for (;;) {
		px = encounter_screen(&pitch, &sw, &sh);
		if (px) {
			hud_text(px, pitch, sw, sh, 20, 24, HUD_CLUT_GOLD,
			         "-- ENCOUNTER --");
			sprintf(line, "%d %s block the way!", (int)mcount,
			        mname);
			hud_text(px, pitch, sw, sh, 20, 48, HUD_CLUT_WHITE, line);
			hud_text(px, pitch, sw, sh, 20, 80, HUD_CLUT_WHITE,
			         "F) Fight");
			hud_text(px, pitch, sw, sh, 20, 96, HUD_CLUT_WHITE,
			         "R) Run");
			qd_present(); qd_present();
		}
		while (!plat_kb_poll(&scan, &ascii))
			;
		if (ascii == 'f' || ascii == 'F')
			break;
		if (ascii == 'r' || ascii == 'R' || ascii == 27) {
			fled = (ua_rand(2) == 0);   /* 50% clean escape */
			break;
		}
	}

	if (fled) {
		port_play_message("You slip away!", "Press any key.");
		return;
	}

	/* round-based auto-resolve over the real party, AD&D to-hit both ways:
	 * a hit needs d20 >= attacker THAC0 - defender AC (lower AC = harder). */
	for (round = 1; round <= 50 && mhp > 0; round++) {
		short msurv, alive;

		for (i = 0; i < nparty; i++) {               /* party strikes */
			short lvl, pthac0;
			if (party[i][CHAR_HP] == 0)
				continue;
			lvl    = party[i][CHAR_LEVEL]; if (lvl < 1) lvl = 1;
			pthac0 = (short)(21 - lvl);    if (pthac0 < 1) pthac0 = 1;
			if ((short)(ua_rand(20) + 1) >= pthac0 - mac)
				mhp -= (long)(ua_rand(8) + 1 + lvl / 2);
		}
		if (mhp <= 0) { victory = 1; break; }

		msurv = (short)((mhp + hp_each - 1) / hp_each);
		if (msurv > mcount) msurv = mcount;
		for (i = 0; i < msurv; i++) {                /* monsters strike */
			short live[16], nl = 0, t, hp;
			for (t = 0; t < nparty; t++)
				if (party[t][CHAR_HP] != 0) live[nl++] = t;
			if (nl == 0) break;
			t = live[ua_rand(nl)];
			if ((short)(ua_rand(20) + 1) >= mthac0 - party[t][CHAR_AC]) {
				hp = (short)(party[t][CHAR_HP]
				             - (short)(ua_rand(mdmg) + 1));
				party[t][CHAR_HP] = (unsigned char)(hp < 0 ? 0 : hp);
			}
		}
		alive = 0;
		for (i = 0; i < nparty; i++)
			if (party[i][CHAR_HP] != 0) alive++;
		if (alive == 0) break;
	}
	if (mhp <= 0) victory = 1;

	/* award XP on victory: a monster-XP pool (HP-scaled) split among the
	 * survivors, added to each one's running XP (CHAR_XP). */
	if (victory) {
		short surv = 0;
		long  pool;
		for (i = 0; i < nparty; i++)
			if (party[i][CHAR_HP] != 0) surv++;
		pool = (long)mcount * hp_each * 10;
		xp_award = surv ? pool / surv : 0;
		for (i = 0; i < nparty; i++)
			if (party[i][CHAR_HP] != 0)
				*(long *)(party[i] + CHAR_XP) += xp_award;
	}

	save_roster();                                   /* persist HP / deaths / XP */

	/* outcome */
	while (plat_kb_poll(&scan, &ascii))
		;
	px = encounter_screen(&pitch, &sw, &sh);
	if (px) {
		hud_text(px, pitch, sw, sh, 20, 24,
		         victory ? HUD_CLUT_GOLD : HUD_CLUT_WHITE,
		         victory ? "Victory!" : "The party has fallen...");
		for (i = 0; i < nparty; i++) {
			short ty = (short)(48 + i * 14);
			unsigned char col = party[i][CHAR_HP]
			                  ? HUD_CLUT_WHITE : HUD_CLUT_GOLD;
			hud_text(px, pitch, sw, sh, 20, ty, col,
			         (const char *)&party[i][96]);
			if (party[i][CHAR_HP])
				sprintf(line, "HP %d", (int)party[i][CHAR_HP]);
			else
				sprintf(line, "DEAD");
			hud_text(px, pitch, sw, sh, 170, ty, col, line);
		}
		if (victory && xp_award > 0) {
			sprintf(line, "Each survivor earns %ld XP.",
			        xp_award);
			hud_text(px, pitch, sw, sh, 20, 150, HUD_CLUT_GOLD,
			         line);
		}
		hud_text(px, pitch, sw, sh, 20, 170, HUD_CLUT_WHITE,
		         "Press any key.");
		qd_present(); qd_present();
	}
	while (!plat_kb_poll(&scan, &ascii))
		;
}

/* encounter_check — after a dungeon step, read the new cell's monster zone
 * (high 6 bits of the cell's 6th byte) and, if non-zero, roll for a random
 * encounter. Returns 1 if one fired (so the caller can refresh the view). */
static int encounter_check(void)
{
	const unsigned char *ds =
		(const unsigned char *)(uintptr_t)g_a5_long(-12300);
	short x = (short)g_a5_12288, y = (short)g_a5_12287;
	short w, h, zone;
	long  cell;

	if (ds == NULL)
		return 0;
	w = (unsigned char)ds[2];
	h = (unsigned char)ds[3];
	if (x < 0 || y < 0 || x >= w || y >= h)
		return 0;
	cell = (long)x * h + y;
	zone = (short)(ds[290 + cell * 6 + 5] >> 2);     /* monster/event zone */
	if (zone == 0)
		return 0;
	if (ua_rand(8) != 0)                             /* ~12% per step */
		return 0;
	port_run_encounter(zone);
	return 1;
}

/* port_rest — camp to recover. Heals every living member to full
 * (CHAR_MAXHP), with a ~1-in-4 chance the rest is interrupted by a
 * wandering encounter (no healing then). The dead (HP 0) need a raise,
 * not rest, so they stay down. */
static void port_rest(void)
{
	unsigned char *party[16];
	short          nparty = cg_collect_party(party, 16), i;
	unsigned char  scan = 0, ascii = 0;

	if (nparty == 0)
		return;
	while (plat_kb_poll(&scan, &ascii))
		;

	if (ua_rand(4) == 0) {                  /* wandering monsters! */
		const unsigned char *ds =
			(const unsigned char *)(uintptr_t)g_a5_long(-12300);
		short zone = 1;
		if (ds != NULL) {
			short x = (short)g_a5_12288, y = (short)g_a5_12287;
			short w = (unsigned char)ds[2], h = (unsigned char)ds[3];
			if (x >= 0 && y >= 0 && x < w && y < h) {
				long cell = (long)x * h + y;
				zone = (short)(ds[290 + cell * 6 + 5] >> 2);
				if (zone == 0) zone = 1;
			}
		}
		port_play_message("Your rest is interrupted!", "Press any key.");
		port_run_encounter(zone);
		return;
	}

	for (i = 0; i < nparty; i++) {
		short mx;
		if (party[i][CHAR_HP] == 0)         /* the dead need a raise, not rest */
			continue;
		mx = party[i][CHAR_MAXHP];
		if (mx < party[i][CHAR_HP]) mx = party[i][CHAR_HP];   /* legacy clamp */
		party[i][CHAR_MAXHP] = (unsigned char)mx;
		party[i][CHAR_HP] = (unsigned char)mx;
	}
	save_roster();
	port_play_message("The party makes camp and recovers.", "Press any key.");
}

/* L0096 (CODE 22 + 0x96) — the master mode loop. The whole play side is a
 * state machine over a 342-byte context struct (set up by L0004, CODE 22+0x4):
 *   ctx[0]  byte  stop flag — the loop runs while it is 0
 *   ctx[2]  word  command (passed to / returned by the mode handler)
 *   ctx[4]  word  current mode (1..21), the JT[3] selector
 *   ctx[6]  ..    published via A5-11714 to JT[316..321] (start/stop pokes)
 *   ctx[8]  long  status flags (JT[241]'s flagsp = &ctx[8])
 *   ctx[16] ..    the play record (JT[241]'s rec = &ctx[16])
 *   ctx[328]      a per-mode scratch sub-struct some handlers point into
 *
 * Each iteration dispatches ctx[4] to its mode handler, then commits
 * (ctx[2]=ctx[4]; ctx[4]=result) and re-tests the stop flag. Mode 14 is the
 * play/dungeon action (JT[241]); a terminal mode (1) or any unhandled mode
 * sets the stop flag and ends the machine.
 *
 * The full mode -> handler map from the JT[3] table @ 0x00aa (min 1 max 21):
 *   1  stop          6  JT[258]    11 JT[248]    16 JT[259]
 *   2  JT[243]       7  JT[263]    12 JT[249]    17..21 JT[?] (CODE 2/8/10)
 *   3  JT[253]       8  JT[269]    13 JT[239]
 *   4  JT[251]       9  JT[233]    14 JT[241]  <-- play action
 *   5  JT[250]      10  JT[247]    15 JT[240]
 * Only mode 14 is wired live here; the other arms are deferred (level-2
 * skeleton, per CLAUDE.md) — they need their CODE 2/8/10/11 handlers lifted,
 * and reaching them faithfully needs L0004's menu-mode entry path. Until then
 * an unhandled mode stops the machine rather than spinning on a stub. */
/* JT[251] (CODE 2 + 0x4284) — the mode-4 handler: the design/play action
 * dispatcher. L0096 calls it as jt251(command=ctx[2], flagsp=&ctx[8],
 * rec=ctx+16). It echoes/reloads the command per a JT[1] switch, runs the
 * record-stage op (JT[325], which advances master[10] = the NEXT mode),
 * clamps mode 6->5, rebuilds the mode-change flag bits in *flagsp from the
 * resolved mode, and returns rec[2] (= master[10]) as the next mode for
 * L0096 to dispatch.
 *
 * rec is the 342-ctx play record (ctx+16):
 *   rec[0] word command echo  rec[2] word next mode
 *   rec[4] word sub-selector  rec[6] long master sub-struct
 * master[10] word = mode field; master[12] word = the cell/page index the
 * flag arms fold into *flagsp's low word. The three inner JT[1] tables were
 * decoded with tools/jt1_extract.py (cmd @0x42b6, mode @0x4388, an empty
 * 0-case sub-switch @0x430c). */
static void jt365(void);
static short jt251(short command, long *flagsp, void *rec_v) __attribute__((unused));
static short jt251(short command, long *flagsp, void *rec_v)
{
	unsigned char *rec = (unsigned char *)rec_v;
	unsigned char *master;
	short          state = 3;               /* fp@(-7) */
	short          res;                     /* fp@(-6): JT[325] status */

	PROBE("jt251");
	if (rec_v == NULL)
		return 0;                       /* L428e */

	master = *(unsigned char **)(rec + 6);

	switch (command) {                      /* JT[1] @ 0x42b6 */
	case 1:                                 /* L42ce */
		*(short *)(rec + 0) = command;
		state = 2;
		break;
	case 5:                                 /* L42e0 */
		command = *(short *)(rec + 0);
		state = 2;
		break;
	case 10:                                /* L42f2 */
		*(short *)(rec + 2) = *(short *)(master + 10);
		/* inner JT[1] @ 0x430c has 0 cases -> falls through. */
		break;
	default:                                /* 8, 11 -> L4310 */
		break;
	}

	/* L4310: the record-stage op. fp@(-1) (the "skip" guard) is always 0
	 * on this entry, so the block always runs. */
	res = jt325(command, flagsp, master, (short)53,
	            g_a5_buf(-18876), state, (short)388);
	if (*(short *)(master + 10) == 6)       /* clamp 6 -> 5 */
		*(short *)(master + 10) = 5;
	*(short *)(rec + 2) = *(short *)(master + 10);
	if (res == 1)
		jt365();

	/* L4372: clear *flagsp's low word, then OR in the redraw bits keyed
	 * by the resolved next mode (JT[1] @ 0x4388). */
	*flagsp &= 0xffff0000L;
	switch (*(short *)(rec + 2)) {
	case 10:                                /* L43a0 */
		if (*(short *)(rec + 2) == *(short *)(master + 10))
			*flagsp |= (long)*(short *)(master + 12);
		else
			*flagsp |= (long)*(short *)(rec + 4);
		break;
	case 8:                                 /* L43da */
		((unsigned char *)flagsp)[4] = 1;
		*flagsp |= (long)*(short *)(master + 12);
		break;
	case 11:                                /* L44ae */
		*flagsp |= (long)*(short *)(master + 12);
		break;
	case 5:                                 /* L43f8 */
		/* TODO: the receding-cell redraw-hint encoding (0x43f8..0x445c)
		 * folds (master[12]&0xff)<<16 plus a cell index derived from the
		 * level base into *flagsp. The two A5-12300 loads in the disasm
		 * are a CREL reloc the disassembler couldn't tell apart, so the
		 * second global is unresolved — deferred. It only tunes the
		 * redraw hint for mode-5 transitions, not the returned mode. */
		break;
	default:                                /* 1 -> L44be */
		break;
	}

	return *(short *)(rec + 2);             /* next mode = master[10] */
}

static short l0096(unsigned char *ctx)
{
	short res = 0;                          /* fp@(-2): handler result */

	PROBE("L0096");
	while (ctx[0] == 0) {                    /* L0468 -> L009e */
		short mode = *(short *)(ctx + 4);

		switch (mode) {                 /* JT[3] on ctx[4] */
		case 4:                         /* L0314 — design/play action (JT[251]) */
			res = jt251(*(short *)(ctx + 2),
			            (long *)(ctx + 8), ctx + 16);
			break;
		case 14:                        /* L02f4 — play/dungeon action */
			res = jt241(*(short *)(ctx + 2),
			            (long *)(ctx + 8), ctx + 16);
			break;
		default:                        /* L0448 + deferred modes: stop */
			ctx[0] = 1;
			res = mode;
			break;
		}
		*(short *)(ctx + 2) = *(short *)(ctx + 4);  /* L0450 */
		*(short *)(ctx + 4) = res;
	}
	return *(short *)(ctx + 2);
}

/* L0004 (CODE 22 + 0x4) — the play/edit state-machine ENTRY. This is the
 * faithful menu->play bridge L0096's header refers to ("reaching them needs
 * L0004's menu-mode entry path"). It builds the 342-byte master context on
 * the stack, clears it (JT[399]), publishes ctx+6 to A5-11714 (the
 * JT[316..321] start/stop hook), seeds command=1, then JT[3]-switches the
 * entry arg to the STARTING mode and runs the mode loop:
 *
 *   arg 6 -> mode 4  (JT[251], CODE 2 — the design-action dispatcher)
 *   arg 7 -> mode 2  (JT[243], CODE 11)
 *   arg 8 -> mode 8  (JT[269], CODE 10); also seeds ctx[12]=JT[354](),
 *                                        ctx[14]=-1
 *   arg 9 -> mode 7  (JT[263], CODE 10)
 *   else  -> set the stop flag (no run)
 *
 * Before the loop JT[358]() is OR'd into the flags' high word (ctx[8]).
 * The chosen start handler then transitions among modes until one returns
 * mode 14 (JT[241], the play/dungeon action) — e.g. mode 4 -> ... -> mode 11
 * (JT[248], CODE 2+0x2a86 returns 14) -> mode 14. Those CODE 2/10 mode
 * handlers are the deferred next step (see jt315's selection dispatch); until
 * they land an unhandled start mode stops L0096 cleanly, so this entry is
 * lifted but not yet wired into the live menu (port_play_demo still seeds
 * mode 14 directly). Called locally from jt315 (CODE 22+0x5180/0x5266).
 *
 * Field offsets in the 342-byte ctx (= L0096's struct):
 *   ctx[0] byte stop  ctx[2] word command  ctx[4] word mode
 *   ctx[6] hook->A5-11714  ctx[8] long flags  ctx[12] byte  ctx[14] word */
static short l0004_22(short arg) __attribute__((unused));
static short l0004_22(short arg)
{
	unsigned char ctx[342];

	PROBE("L0004_22");
	jt399(ctx, (short)342, (short)0);               /* clear the context */
	g_a5_long(-11714) = (long)(uintptr_t)(ctx + 6); /* JT[316..321] hook */
	*(short *)(ctx + 2) = 1;                         /* command = 1 */

	switch (arg) {                                  /* JT[3] min=6 max=9 */
	case 6:                                         /* L003c */
		*(short *)(ctx + 4) = 4;
		break;
	case 7:                                         /* L0044 */
		*(short *)(ctx + 4) = 2;
		break;
	case 8:                                         /* L004c */
		ctx[12] = (unsigned char)jt354();
		*(short *)(ctx + 14) = (short)-1;
		*(short *)(ctx + 4) = 8;
		break;
	case 9:                                         /* L0062 */
		*(short *)(ctx + 4) = 7;
		break;
	default:                                        /* L006a */
		ctx[0] = 1;                             /* stop -> no run */
		break;
	}

	/* L0070: fold JT[358]'s byte into the flags' high word, then run. */
	*(long *)(ctx + 8) |= ((long)(jt358() & 0xff)) << 16;
	if (ctx[0] == 0)
		(void)l0096(ctx);
	return 0;
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
	short pitch, sw, sh, refnum = 0, i;
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
	/* The faithful play entry (jt241 -> l63c0 with a_deep=0) draws the
	 * TOP-DOWN area map, not the first-person 3D view, so it must run in the
	 * normal 2D display mode: jt1135 uses scale 2 when g_a5_-2347 != 0, scale
	 * 3 (the deep dungeon-view layout) when 0. Under scale 3 the map's jt1089
	 * text lands at vertical ~276 — off the bottom of the screen — which is
	 * why the frame was invisible; scale 2 keeps it on-screen. */
	g_a5_2347 = 1;

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

	/* --- the faithful play loop ---
	 * Drive CODE 22's master mode machine (L0096) seeded at mode 14 (the
	 * play/dungeon action, JT[241]). l0bbc above loaded the level and placed
	 * the party; jt241 sets up the view layers, the title, the command bar
	 * (JT[179]+JT[148]) and the area-list header (L53a6), then runs the walk
	 * loop (L63c0) with the automap / cell-block callbacks (JT[237]/JT[236]).
	 *
	 * The ctx struct mirrors L0004's: ctx[4]=mode, ctx[2]=command,
	 * ctx[8]=status flags, ctx[16]=play record. We enter directly at the
	 * play mode (the menu-mode entry path L0004 walks — modes 6..9 -> ... ->
	 * 14 — needs its CODE 2/8/10 handlers lifted first).
	 *
	 * L6256 (via the rec[4]=1 seed) registers the walk input-source DLItems,
	 * and l63c0 polls them through the real event loop (l2d3e). The top-down
	 * area map (bg fill, status header jt303, coords jt280, automap jt237 ->
	 * l52f2 cells) renders via jt1089 in the scale-2 mode set above. This
	 * replaces the old port-side WASD demo walk. */
	/* Reproduce the play-screen entry setup the port shortcuts past: L476e
	 * lays the full area-map view-interior rect (g_a5_-11674.. / -11670..)
	 * that L63c0's jt1161 background fill and the cell mapping need, and the
	 * engine clip is opened to the full screen (the real entry path narrows
	 * it via jt1173; full-screen is a safe superset for the demo). Without
	 * these the view rect and clip are 0, so every fill/text clips to nothing. */
	jt215();                  /* set the automap cell size (2D map -> 8px) */
	l476e((short)1, (short)1);
	g_a5_3054 = 0; g_a5_3056 = 0; g_a5_3050 = sh; g_a5_3052 = sw;

	{
		static unsigned char ctx[342];     /* L0004's master state struct */
		memset(ctx, 0, sizeof ctx);
		*(short *)(ctx + 4) = 14;          /* mode 14 = play/dungeon action */
		*(short *)(ctx + 2) = 11;          /* first-entry command           */
		/* rec = &ctx[16]; rec[4] = area kind (1 = dungeon). The menu-driven
		 * level-entry path the port shortcuts past would set this; seed it so
		 * L6256 registers the walk input sources (it no-ops for kind 0). */
		ctx[16 + 4] = 1;
		g_a5_long(-11714) = (long)(uintptr_t)(ctx + 6);  /* JT[316..321] hook */
		(void)px; (void)pitch; (void)tvbase;
		(void)l0096(ctx);
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

/* --- roster save / load: one file per character ---------------------------
 *
 * Step 1 of the faithful file-based roster (docs/decompilation.md "Character
 * storage"): FRUA keeps each saved character in its own file, not a single
 * blob. The port mirrors that — every pool slot persists to "CHARnnnn.CHR"
 * (512 bytes, type 'FRUA'/'SAVE'). GEMDOS is 8.3 and character names carry
 * spaces, so files are slot-numbered (CHAR0000..CHAR0015) with the name kept
 * inside the record at +96; the faithful name-derived path + the JT[589]
 * directory enumeration are the next steps (this scheme probes fixed slots).
 *
 * The saved-character POOL is every character in the design; the active
 * PARTY is the CHAR_INPARTY subset threaded into the g_a5_-27928 list by
 * cg_party_relink. CHAR_INPARTY rides in each record, so party membership
 * round-trips with the per-character files. */
static unsigned char cg_pool[16][512];
static short         cg_pool_count;


/* Pascal filename "CHARnnnn.CHR" for pool slot 0..15. */
static void cg_char_fn(short slot, char *fn)
{
	fn[0]  = 12;
	fn[1]  = 'C'; fn[2] = 'H'; fn[3] = 'A'; fn[4] = 'R';
	fn[5]  = (char)('0' + (slot / 1000) % 10);
	fn[6]  = (char)('0' + (slot / 100)  % 10);
	fn[7]  = (char)('0' + (slot / 10)   % 10);
	fn[8]  = (char)('0' +  slot         % 10);
	fn[9]  = '.'; fn[10] = 'C'; fn[11] = 'H'; fn[12] = 'R';
}

/* Rebuild the active-party list (g_a5_-27928) from the in-party pool
 * records, in pool order — the roster grid (l02dc) and the party screens
 * walk this list. */
static void cg_party_relink(void)
{
	unsigned char *prev = NULL;
	short          i;

	g_a5_long(-27928) = 0;
	for (i = 0; i < cg_pool_count; i++) {
		if (cg_pool[i][CHAR_INPARTY] == 0)
			continue;
		if (prev == NULL)
			g_a5_long(-27928) = (long)(uintptr_t)cg_pool[i];
		else
			*(long *)prev = (long)(uintptr_t)cg_pool[i];
		prev = cg_pool[i];
		*(long *)prev = 0;
	}
}

static short cg_party_size(void)
{
	short i, n = 0;
	for (i = 0; i < cg_pool_count; i++)
		if (cg_pool[i][CHAR_INPARTY])
			n++;
	return n;
}

/* Port-local node pool for the 40-byte roster / design-list nodes the engine
 * allocates from g_a5_-21156 (JT[477] reserve / JT[471] free).  The bucket
 * layout matches what jt477 reads: max_count@0, record_size@2, base_ptr@4,
 * then a 1-bit-per-slot allocation bitmap@8.  The Mac's pool init isn't lifted
 * yet, so stand one up here; until it's set up, jt477's NULL-bucket guard
 * makes the roster builders harmless no-ops (empty list). */
#define NODE_POOL_COUNT 64
static unsigned char g_node_pool_store[NODE_POOL_COUNT * 40];
static unsigned char g_node_pool_bucket[8 + (NODE_POOL_COUNT + 7) / 8]
	__attribute__((aligned(4)));

static void node_pool_init(void)
{
	*(short *)(void *)(g_node_pool_bucket + 0) = NODE_POOL_COUNT;
	*(short *)(void *)(g_node_pool_bucket + 2) = 40;
	*(long  *)(void *)(g_node_pool_bucket + 4) =
		(long)(uintptr_t)g_node_pool_store;
	memset(g_node_pool_bucket + 8, 0, sizeof g_node_pool_bucket - 8);
	g_a5_21156 = (long)(uintptr_t)g_node_pool_bucket;
}

/* Write each pool character to its own CHARnnnn.CHR file; delete the files
 * for the now-unused higher slots so a removed character doesn't linger. */
static void save_roster(void)
{
	short i, refnum;
	long  n;
	char  fn[16];

	for (i = 0; i < 16; i++) {
		cg_char_fn(i, fn);
		if (i < cg_pool_count) {
			(void)Create((ConstStr255Param)fn, 0,
			             0x46525541L /* 'FRUA' */,
			             0x53415645L /* 'SAVE' */);
			if (FSOpen((ConstStr255Param)fn, 0, &refnum) == noErr) {
				(void)SetEOF(refnum, 0);
				n = 512;
				(void)FSWrite(refnum, &n, cg_pool[i]);
				(void)FSClose(refnum);
			}
		} else {
			(void)FSDelete((ConstStr255Param)fn, 0);  /* clear stale slot */
		}
	}
}

/* Scan the design folder for CHAR*.CHR via the FS shim's directory
 * enumeration (the capability JT[589] needs), reading each into the pool,
 * then relink the party from each record's CHAR_INPARTY flag. Returns 1 if
 * any character loaded. */
static int load_roster(void)
{
	char  cname[16];
	short refnum, n2 = 0;
	long  n;
	int   found;

	for (found = files_find_first("CHAR*.CHR", cname, (int)sizeof cname);
	     found && n2 < 16;
	     found = files_find_next(cname, (int)sizeof cname)) {
		char pfn[18];
		short len = 0;
		while (cname[len] != 0 && len < 16) {
			pfn[len + 1] = cname[len];
			len++;
		}
		pfn[0] = (char)len;
		if (FSOpen((ConstStr255Param)pfn, 0, &refnum) != noErr)
			continue;
		n = 512;
		if (FSRead(refnum, &n, cg_pool[n2]) == noErr && n == 512)
			n2++;
		(void)FSClose(refnum);
	}
	if (n2 == 0)
		return 0;
	cg_pool_count = n2;
	cg_party_relink();
	return 1;
}

/* port_load_savgame — load a real BasiliskII-saved game (SAVE/SavGam<X>.csv,
 * 10284 bytes) into the port party. The save embeds the FAITHFUL character
 * record(s) (the same 536-byte layout as BOB.cch: name@96, class@88, kind@89,
 * abilities@112, maxHP@82, HP@395) plus a header carrying the saved dungeon
 * state. Diffing SAVGAMA vs SavGamB pinned the volatile play-state bytes:
 * party x @66 (also @1024), party y @67, level @18. This is a PORT loader
 * (the faithful jt582 + the l0bbc saved-game resume are the follow-up); it
 * proves the save reads + parses + populates real play state. Returns 1 on
 * success. The combat block (name@96/AC@385/HP@395) is at the same offsets in
 * the faithful and port records, so the HUD roster reads it directly; the
 * char-sheet fields (class/stats/maxHP) are translated to the port CHAR_*. */
static int port_load_savgame(void)
{
	static unsigned char sg[12288];
	short refnum = 0;
	long  n;
	long  i;

	dbg_log("port_load_savgame: trying SAVGAMA.CSV");
	if (FSOpen((ConstStr255Param)"\013SAVGAMA.CSV", 0, &refnum) != noErr) {
		dbg_log("  FSOpen failed");
		return 0;
	}
	n = (long)sizeof sg;
	/* FSRead returns eofErr (not noErr) when the file is smaller than the
	 * requested count — that's a FULL read, not a failure; trust the byte
	 * count it writes back into n. */
	(void)FSRead(refnum, &n, sg);
	(void)FSClose(refnum);
	if (n < 1600) {
		dbg_log_num("  FSRead short, n=", n);
		return 0;
	}
	dbg_log_num("  read bytes=", n);

	/* Find the first embedded faithful character record: a printable name at
	 * +96, a class at +88 in range, and a plausible max HP at +82. */
	for (i = 0; i + 536 <= n; i++) {
		unsigned char *r = sg + i;
		short c;
		unsigned char *dst;

		if (!(r[96] >= 'A' && r[96] <= 'Z') || r[88] > 16
		    || r[82] == 0 || r[82] >= 200)
			continue;

		dst = cg_pool[0];
		memcpy(dst, r, 512);                  /* the faithful record as-is */
		/* Translate the char-sheet fields to the port CHAR_* layout so the
		 * roster grid (l02dc) and rest-heal read them; the combat block
		 * (name@96/AC@385/HP@395) is already at matching offsets. */
		dst[CHAR_MAXHP] = r[82];               /* faithful max HP @82 */
		dst[CHAR_CLASS] = 1;                   /* Fighter (label; faithful @88 kept) */
		dst[CHAR_RACE]  = 0;                   /* Human (label) */
		dst[CHAR_LEVEL] = 5;
		for (c = 0; c < 6; c++)                /* abilities: @112 value-pairs */
			dst[CHAR_STATS + c] = r[112 + c * 2];
		dst[CHAR_ALIGN]   = 0;
		dst[CHAR_INPARTY] = 1;
		/* AC: the faithful record stores 60 - displayed_AC at @385 (parallel to
		 * THAC0 @384). The port's roster/combat read CHAR_AC as the DISPLAYED
		 * value (the seeded path stores it that way), so translate: BOB's
		 * @385 = 61 -> displayed -1. */
		dst[CHAR_AC] = (unsigned char)(60 - (short)r[385]);
		cg_pool_count = 1;
		cg_party_relink();

		/* Restore the saved dungeon position/level (header fields pinned by
		 * the A-vs-B diff). Route the position through the FAITHFUL resume
		 * path: stand up the area-state record (g_a5_-28006, normally
		 * allocated by L4cc0) and set the saved party x/y/facing at
		 * h[67]/h[68]/h[17] plus the "view established" flag h[134]. On entry
		 * l0bbc sees h[134] != 0 and takes its RESUME branch — restoring the
		 * saved cell instead of re-placing the party at the level start tile;
		 * jt948 then takes the "view established" arm (no fresh-entry special).
		 * The direct g_a5_-12288/-12287/-12286 writes below match what l0bbc
		 * will restore (belt-and-suspenders for the pre-l0bbc render). */
		g_a5_18878 = (short)(sg[18] ? sg[18] : 5);   /* level (>=5 dungeon) */
		g_a5_12288 = (unsigned char)sg[66];          /* party x */
		g_a5_12287 = (unsigned char)sg[67];          /* party y */
		if (g_a5_12286 == 0)
			g_a5_12286 = 1;                      /* facing (default N) */

		memset(g_area_state, 0, sizeof g_area_state);
		g_a5_28006 = g_area_state;
		g_area_state[134] = 1;                       /* view established -> resume */
		g_area_state[67]  = (unsigned char)sg[66];   /* saved party x */
		g_area_state[68]  = (unsigned char)sg[67];   /* saved party y */
		g_area_state[17]  = (unsigned char)g_a5_12286; /* saved facing */
		g_savgame_loaded  = 1;
		dbg_log("port_load_savgame: loaded SAVGAMA.CSV");
		dbg_log_num("  party x@66 = ", (long)sg[66]);
		dbg_log_num("  party y@67 = ", (long)sg[67]);
		dbg_log_num("  level @18  = ", (long)sg[18]);
		dbg_log_num("  HP @395    = ", (long)r[395]);
		return 1;
	}
	dbg_log("  no character record found in save");
	return 0;
}

static void l29ae(unsigned char *rec);   /* CODE 17 max-HP finalize (below) */

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

	/* Seed a test pool so the Training Hall has real characters. The pool
	 * (cg_pool) is the master list of saved characters; the active party
	 * (the roster grid l02dc walks off g_a5_-27928) is the CHAR_INPARTY
	 * subset, rebuilt by cg_party_relink. Seeded ONCE — l07dc re-runs this
	 * on every Play, but the pool persists in memory (and on disk via
	 * save_roster), so we just relink the party each Play. That keeps
	 * created / added / removed / deleted edits across Play sessions. */
	{
		static const char *const k_names[4] =
		    { "Bramble", "Korin Vale", "Sable", "Bob" };
		static const unsigned char k_hp[4]    = { 18, 24, 11, 0 };
		static const unsigned char k_ac[4]    = {  5,  7,  4, 5 };
		/* race / class indices into k_roster_races / k_roster_classes:
		 * Bramble = Human Fighter L3, Korin = Elf Mage L2,
		 * Sable = Halfling Thief L3, Bob = the real BasiliskII-saved
		 * character (BOB.cch) — Human Fighter, HP computed by L29ae. */
		static const unsigned char k_race[4]  = { 0, 1, 5, 0 };
		static const unsigned char k_class[4] = { 1, 2, 3, 1 };
		static const unsigned char k_lvl[4]   = { 3, 2, 3, 5 };
		/* ability scores (STR INT WIS DEX CON CHA) + alignment index.
		 * Bob's are BOB.cch's real rolled stats (record @112). */
		static const unsigned char k_stats[4][6] = {
			{ 16, 10, 11, 14, 15, 12 },   /* Bramble  */
			{  9, 17, 12, 16, 11, 13 },   /* Korin    */
			{ 12, 13, 10, 17, 14, 11 },   /* Sable    */
			{ 16, 13, 16, 14, 17, 15 },   /* Bob (real)*/
		};
		static const unsigned char k_align[4] = { 0, 4, 6, 0 };
		/* seed XP near the train thresholds (level*1000): Bramble L3 +
		 * Korin L2 ready to train, Sable L3 not yet. */
		static const long k_xp[4] = { 3100, 2100, 1500, 5000 };
		/* faithful CODE 17 class / combination-type (rec[88]/rec[89]) for
		 * the L29ae HP finalize. 0xff = skip (no faithful fields). Bob uses
		 * BOB.cch's real class=2 / kind=2 -> the live -30780 table entry
		 * (count=5 die=4 base=40) -> HP = 5d4 + 40 + 10 (= 55..70). */
		static const unsigned char k_fclass[4] = { 0xff, 0xff, 0xff, 2 };
		static const unsigned char k_fkind[4]  = { 0,    0,    0,    2 };
		static const int           k_count = 4;
		static int seeded = 0;

		if (!seeded) {
			seeded = 1;
			node_pool_init();            /* roster / design node pool */
			/* Prefer a real BasiliskII saved game (SAVGAMA.CSV) if present,
			 * else the persisted port roster, else the synthetic seed. */
			if (port_load_savgame()) {
				/* real save loaded — party + saved dungeon position */
			} else if (!load_roster()) {        /* no disk save -> seed pool */
				int p, c;
				for (p = 0; p < k_count; p++) {
					unsigned char *r = cg_pool[p];
					memset(r, 0, 512);
					for (c = 0; k_names[p][c] != 0 && c < 15; c++)
						r[96 + c] = (unsigned char)k_names[p][c];
					r[96 + c] = 0;
					r[CHAR_HP] = k_hp[p];
					r[CHAR_MAXHP] = k_hp[p];
					r[CHAR_AC] = k_ac[p];
					r[CHAR_THAC0] = (unsigned char)(39 + k_lvl[p]);
					r[CHAR_MOVE]  = 12;
					r[CHAR_RACE]  = k_race[p];
					r[CHAR_CLASS] = k_class[p];
					r[CHAR_LEVEL] = k_lvl[p];
					for (c = 0; c < 6; c++)
						r[CHAR_STATS + c] = k_stats[p][c];
					r[CHAR_ALIGN]   = k_align[p];
					r[CHAR_INPARTY] = 1;
					*(long *)(r + CHAR_XP) = k_xp[p];

					/* Real-data path: when a faithful class/kind is set,
					 * compute max HP via the validated CODE 17 finalize
					 * (L29ae) from the live -30780 class table, and surface
					 * it into the port roster's HP fields. */
					if (k_fclass[p] != 0xff) {
						short hp;
						r[88] = k_fclass[p];   /* faithful class */
						r[89] = k_fkind[p];    /* faithful kind  */
						l29ae(r);              /* -> max HP at rec[82] */
						hp = *(short *)(r + 82);
						if (hp > 0 && hp < 256) {
							r[CHAR_HP]    = (unsigned char)hp;
							r[CHAR_MAXHP] = (unsigned char)hp;
						}
					}
				}
				cg_pool_count = k_count;
				save_roster();       /* persist the seed as CHAR*.CHR so
				                      * the saved-character roster
				                      * (jt589 / L01be) can enumerate it */
			}
		}
		cg_party_relink();           /* rebuild the party list each Play */
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
/* L30ba (CODE 3 + 0x30ba) — call method(rec, cmd, ...) for items
 * [start..end]. jt453 runs l30ba(0, count-1, 0) before the poll, so every
 * item gets a cmd 0; the shape-2 radio containers (jt381) act on it to sync
 * their selection from the value global (highlighting the default pick). */
static void   l30ba(short start, short end, short cmd)
{
	short i;

	PROBE("L30ba");
	if (g_a5_9248 == 0)
		return;
	for (i = start; i <= end; i++) {
		unsigned char *rec = (unsigned char *)(uintptr_t)g_a5_9254
		    + (long)i * DLITEM_BYTES;
		short (*method)(void *, short, ...) =
		    *(short (**)(void *, short, ...))rec;
		if (method != NULL)
			(void)method(rec, cmd, (short)0, (short)0);
	}
}

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
short g_event_was_click;   /* set by jt1125: 1 if the last event was a click */

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

	g_event_was_click = (ev.what == mouseDown) ? 1 : 0;
	switch (ev.what) {
	case keyDown:
	case autoKey: {
		short ascii = (short)(ev.message & 0xff);
		short scan  = (short)((ev.message >> 8) & 0xff);
		/* Arrow keys carry no ASCII (ascii==0); their scancode is in the
		 * high byte. Map the cursor keys to the engine's movement codes
		 * (257..264) the play loop (jt297/jt311) expects. */
		if (ascii == 0) {
#ifdef FRUA_ENGINE_PROBE
			dbg_log_num("keyDown scan = ", (long)scan);
#endif
			switch (scan) {
			case 0x48: ascii = 264; break;   /* Up    -> N / forward */
			case 0x50: ascii = 260; break;   /* Down  -> S */
			case 0x4b: ascii = 262; break;   /* Left  -> W / turn left */
			case 0x4d: ascii = 258; break;   /* Right -> E / turn right */
			default: break;
			}
		}
		*out1 = ascii;
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
		g_a5_word(-818) = ascii;
		g_a5_byte(-820) = 1;
		return (ev.modifiers & cmdKey) ? (short)2 : (short)1;
	}
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
 * Full lift: all five phases run, with the real event-read helpers
 * (l3198 -> JT[1125] WaitNextEvent, l31ea -> JT[1118], l31f0 -> JT[1133]
 * keyboard read). The DLItem method table (g_a5_-9282, the 7 shape handlers
 * jt376..jt382) is populated at boot, so JT[452] parks real method pointers
 * and the per-item hit-test / key dispatch fires; the NULL guard stays
 * defensive. Returns the index of the DLItem that caught the event, or -1.
 *
 * This is JT[456]: L63c0's exploration loop and JT[453]'s modal loop both
 * poll it. For the dungeon walk to resolve movement/keys, the play screen
 * must first register its input-source DLItems (keyboard, the four
 * directional pads, select) via JT[447]/JT[452] — the next step.
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

	/* l3198/jt1125 return the event TYPE (1 key / 2 cmd-key / 1 mouse); the
	 * actual key char is in mouse_y (out1) for a keyDown, modifiers in mouse_x.
	 * Translate the cursor keys to the engine's movement codes (257..264) and
	 * stash where l63c0's keyboard arm (case 0) reads them. */
	if ((key == 1 || key == 2) && !g_event_was_click) {
		short kc = mouse_y;           /* jt1125 already maps arrows->257..264 */
#ifdef FRUA_ENGINE_PROBE
		if (g_walk_input)
			dbg_log_num("walk key = ", (long)kc);
#endif
		g_a5_word(-10372) = kc;
		/* In the walk loop, route movement/control keys to the keyboard
		 * source (return 0) before the command-bar DLItem match, so they
		 * reach l63c0's switch(0) -> jt297 rather than exiting the loop. */
		if (g_walk_input
		    && ((kc >= 257 && kc <= 264) || kc == 27 || kc == 13))
			return (short)0;
	}

	/* Port mouse hit-test: the faithful per-item method(rec,2,y,x) hit-test
	 * needs the shape-7 DLItem method dispatch, which is unlifted (the method
	 * pointers are NULL), so a click never lands. When jt1125 reported a
	 * mouseDown (mouse_y = where.h = x, mouse_x = where.v = y), walk the
	 * positioned DLItems (rec[16]=y, rec[18]=x in 8000-space) and reproduce
	 * the plate rect (menu_draw_plates: jt1135 -> [pxx-5,pxx+145) x
	 * [py-7,py+3)); commit + return the item under the click. */
	if (key != 0 && g_event_was_click) {
		short cx = mouse_y, cy = mouse_x;
		unsigned char *hr = (unsigned char *)g_a5_9254;
		for (i = 0; i < count; i++) {
			short iy = *(short *)(hr + 16);
			short ix = *(short *)(hr + 18);
			if (iy >= 8000) {              /* a positioned (drawable) item */
				short py, pxx;
				jt1135(iy, ix, &py, &pxx);
				if (cx >= (short)(pxx - 5) && cx < (short)(pxx + 145)
				 && cy >= (short)(py - 7) && cy < (short)(py + 3)) {
					hr[28] |= 0x10;
					return i;
				}
			}
			hr += DLITEM_BYTES;
		}
		/* click missed every item — fall through (no selection) */
	}

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

	/* Port mouse tracking: the software cursor is composited at present
	 * time (qd_present), but this modal spin otherwise only presents on a
	 * content change, so a moving pointer would lag a frame behind the
	 * IKBD. Present whenever the mouse has moved since the last spin so the
	 * sword tracks smoothly without burning c2p while the pointer is idle. */
	{
		static short last_h = -0x7fff, last_v = -0x7fff;
		short mh, mv;
		plat_mouse_pos(&mh, &mv);
		if (mh != last_h || mv != last_v) {
			last_h = mh;
			last_v = mv;
			qd_present();
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

/* ===================================================================== *
 * jt96 slow-text-pacing + pagination-pause cluster (was stubbed). All of
 * these bottom out on already-lifted leaves (jt1118/jt1125/jt1133/jt983/
 * jt453/jt1151/jt1134 + the DLItem/text primitives), so the whole chain
 * is real now — no stubs left in jt96. Defined here, after jt453.
 * ===================================================================== */

static void l162e(void);                /* CODE 7 — defined below */
static void l2062(void);                /* CODE 7 — defined below */

/* JT[1154] (CODE 4+0x77e8) — read the page-scroll latch g_a5_-806. */
static short jt1154_pg(void) { PROBE("jt1154"); return (short)(unsigned char)g_a5_byte(-806); }
/* JT[1140] (CODE 4+0x77d0) — set the page-scroll latch; when clearing it
 * (v==0) reset the scroll state via JT[1151] (=L765c). */
static void  jt1140_pg(short v)
{
	PROBE("jt1140");
	if ((v & 0xff) == 0)
		jt1151();                       /* L765c — scroll-state reset */
	g_a5_byte(-806) = (unsigned char)v;
}

/* L5f3a (CODE 6+0x5f3a) — stash the pause "cancel" key in g_a5_-13084. */
static void l5f3a(short v) { PROBE("L5f3a"); g_a5_byte(-13084) = (unsigned char)v; }

/* L5ac2 (CODE 6+0x5ac2) — page-up while paused: toggle the latch via
 * JT[1140](!JT[1154]). */
static void l5ac2(void) { PROBE("L5ac2"); jt1140_pg((short)(jt1154_pg() == 0)); }

/* L5ad8 (CODE 6+0x5ad8) — page-down while paused: toggle g_a5_-17443 and
 * feed it to the scroll dispatcher JT[983]. */
static void l5ad8(void)
{
	PROBE("L5ad8");
	g_a5_byte(-17443) = (unsigned char)(g_a5_byte(-17443) == 0);
	jt983((short)(unsigned char)g_a5_byte(-17443));
}

/* L5f84 (CODE 6+0x5f84) — the pause key reader: poll a key (jt1133), handle
 * Esc/`(27/96 -> cancel via L5f3a) and the page-up/down keys (338/339), then
 * drain any further pending events (jt1118). Returns the key (>=256 folded by
 * -128, the extended-key normalisation). */
static short l5f84(void)
{
	short key;

	PROBE("L5f84");
	if (g_a5_byte(-27988) != 0) {
		key = jt1118() ? (short)jt1133() : (short)0;
		if (key == 27 || key == 96)
			l5f3a(1);
	} else {
		key = (short)jt1133();
	}
	if (key == 338) l5ac2();
	if (key == 339) l5ad8();
	if (key != 0) {
		while (jt1118()) {
			key = (short)jt1133();
			if (key == 338) l5ac2();
			if (key == 339) l5ad8();
			if (g_a5_byte(-27988) != 0 && (key == 27 || key == 96))
				l5f3a(1);
		}
	}
	if (key >= 256)
		key = (short)(key - 128);
	return (short)(unsigned char)key;
}

/* L604e (CODE 6+0x604e) — pump one event: if an event is pending (jt1118)
 * run the pause key reader (L5f84), then refresh the IKBD via jt1125(7). */
static void l604e(void)
{
	long a = 0, b = 0;
	PROBE("L604e");
	if (jt1118())
		(void)l5f84();
	(void)jt1125((short)7, (long)&b, (long)&a);
}

/* L6048 / JT[66] (CODE 6+0x6048) — thin wrapper over L604e. */
static void l6048(void) { PROBE("L6048"); l604e(); }

/* L435a (CODE 6+0x435a) — slow-text per-glyph pacing: accumulate the party
 * speed (handle[18]) into g_a5_-17522, convert each 6 units into a target
 * tick on g_a5_-17526, and busy-wait jt1134() up to that tick. */
static void l435a(void)
{
	const unsigned char *h = (const unsigned char *)g_a5_28006;
	short count;
	long  t;

	PROBE("L435a");
	if (h == NULL)
		return;
	g_a5_long(-17522) = g_a5_long(-17522) + (long)(h[18] & 0xff);
	if (g_a5_long(-17522) < 6)
		return;
	count = 0;
	while (g_a5_long(-17522) >= 6) {
		g_a5_long(-17522) = g_a5_long(-17522) - 6;
		count++;
	}
	g_a5_long(-17526) = g_a5_long(-17526) + count;
	t = jt1134();
	if (t > g_a5_long(-17526))
		g_a5_long(-17526) = t;          /* fell behind: catch the target up */
	else
		while (jt1134() < g_a5_long(-17526))
			;                       /* pace to the target tick */
}

/* L177a (CODE 7+0x177a) — lay the "Press <Return> to continue." prompt: pump
 * (jt66/L6048), open the frame (jt108 + L162e), draw the text (jt94), build
 * the Return button DLItem (jt447 + jt452), paint (jt449), commit (jt117). */
static void l177a(void)
{
	PROBE("L177a");
	l6048();                                /* JT[66] */
	(void)jt108((short)1);
	l162e();
	jt94((short)7, (short)24, (short)0, (short)7, "%s",
	     ua_strs_at(0x275c) /* "Press        to continue." */);
	l2062();
	jt447();
	jt452((long)1, (long)8094, (long)8056,
	      (long)(uintptr_t)ua_strs_at(0x2776) /* "Return" */,
	      (long)36, (long)6, (long)32, (long)64,
	      (long)20, (long)22, (long)21, (long)0);
	(void)jt449((short)1);
	(void)jt117();
}

/* jt175 (CODE 7+0x17f8) — show the prompt (L177a) then run the modal poll
 * (jt453) until the user dismisses it. */
static void jt175(void)
{
	PROBE("jt175");
	l177a();
	(void)jt453((jt453_filter_t)0);
}

/* L4b84 (CODE 6+0x4b84) — thin wrapper over jt175 (the modal prompt). */
static void l4b84(void) { PROBE("L4b84"); jt175(); }

/* L4c46 (CODE 6+0x4c46) — the pagination "press a key to continue" pause:
 * pump events (L6048) then run the modal prompt (L4b84 -> jt175). */
static void l4c46(void) { PROBE("L4c46"); l6048(); l4b84(); }

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

/* Decode a "type 7" GLIB piece (metric[7] & 0x0f == 7): the chunky
 * transparency RLE that the faithful blit hands to JT[1195] (CODE 4 +
 * 0xc08) one strip at a time. JT[1195] walks the source as a stream of
 * control bytes, writing into the destination scanline and advancing
 * by the destination row stride on a 0 byte:
 *
 *   b == 0            -> end of row: drop to the next scanline (x = 0)
 *   b in 1..127       -> copy b literal pixels, advance src + dst by b
 *   b in 0x80..0xFF   -> skip (256 - b) transparent pixels (dst only)
 *
 * (In the Mac, the 0-byte advance is `a3 = a2 + L04de()` where L04de is
 * the dest row stride, 320 in 8-bit colour; here we model the same as
 * a newline into a w-wide chunky buffer.) Transparent pixels are left
 * as index 0, so the caller blits with index-0 transparency. Decodes
 * the whole image (h rows) into dst[w*h]; dst must be pre-zeroed.
 * Returns the source pointer advanced past the bytes consumed. */
static const unsigned char *decode_glib_t7(const unsigned char *src,
                                           unsigned char *dst,
                                           short w, short h)
{
	short x = 0, y = 0;
	while (y < h) {
		unsigned char b = *src++;
		if (b == 0) {                         /* next row */
			y++;
			x = 0;
		} else if (b < 128) {                 /* copy b literals */
			short n = b;
			while (n-- > 0) {
				if (x < w)
					dst[(long)y * w + x] = *src;
				src++;
				x++;
			}
		} else {                              /* skip 256-b transparent */
			x = (short)(x + (256 - b));
		}
	}
	return src;
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
static unsigned char g_glib_dec[320 * 200];  /* PackBits decode scratch
                                              * (full screen: the title
                                              * backdrop is 320x200) */

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

	int xparent = transparent;
	if ((flags & 0x0f) == 2) {               /* PackBits 8bpp */
		long cap = (long)w * h;
		if (cap > (long)sizeof g_glib_dec)
			return;
		(void)unpackbits((const unsigned char *)(uintptr_t)info,
		                 (long)sizeof g_glib_dec, g_glib_dec, cap);
		src = g_glib_dec;
	} else if ((flags & 0x0f) == 7) {        /* transparency RLE (JT[1195]) */
		long cap = (long)w * h;
		if (cap > (long)sizeof g_glib_dec)
			return;
		memset(g_glib_dec, 0, (size_t)cap);
		(void)decode_glib_t7((const unsigned char *)(uintptr_t)info,
		                     g_glib_dec, w, h);
		src = g_glib_dec;
		xparent = 1;                     /* type 7 is intrinsically sparse */
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
			if (xparent && v == 0)       /* index 0 = transparent */
				continue;
			if (dx < 0 || dx >= sw)
				continue;
			d[dx] = v;
		}
	}
}

/* port_show_intro — FRUA's title / credits sequence.
 *
 * TITLE.CTL is a GLIB of 7 sets.  Set 0 is a screen-order table; sets 1..6
 * are one screen each — item 0 is the screen's 256-entry RGB palette and
 * items 1.. are the art pieces.  Each piece is a metric-headed chunky 8bpp
 * image: a type-2 whole-screen PackBits backdrop (SSI/credits surrounds,
 * the Unlimited Adventures title) or a type-7 transparency-RLE logo placed
 * by its bearings.  The faithful blit (l309c / l2d4e) routes type 2 through
 * the PackBits strip decoder and type 7 through JT[1195]; ui_glib_blit now
 * mirrors both onto the chunky surface, so each screen is just "install the
 * palette, composite the pieces."
 *
 * Palette model (verified against the real Mac game running in BasiliskII):
 *   - Set 1 is the title MASTER: its 256-entry palette + its red-gradient /
 *     stone-frame background IMAGE (item 1). The logo screens are authored
 *     for this master palette, NOT their own — e.g. Micro Magic's gold, the
 *     AD&D logo's metal and Forgotten Realms' gold lettering live in set 1's
 *     ramp; each screen's own item-0 palette is a red herring for the title.
 *   - Logo screens (sets 2=SSI, 3=AD&D/TSR, 4=Forgotten Realms, 6=credits):
 *     blit set 1's backdrop opaque, then the screen's logo pieces with index-0
 *     transparency, all under the master palette.
 *   - The Unlimited Adventures title (set 5) is the exception: a full-screen
 *     image authored for its OWN palette, so set 5's 0..223 overlay the master
 *     (whose 240..254 keep clut 129's blue/grey ramp for the crystal-ball
 *     globe). It is blitted opaque with no set-1 backdrop.
 * (The copy-protection / manual-check prompt is a separate grey dialog the
 * port bypasses entirely.) The UI palette (clut 129) is restored for the menu
 * on the way out. */
extern void load_frua_palette(void);

/* Overlay a TITLE.CTL screen's RGB palette (item 0 of set `set`) onto
 * `dst[]`, honouring the palette's allotted CLUT range. Item 0 is an
 * 8-byte-metric-headed block of RGB byte triples; the metric encodes the
 * partition (per FRUA's design): ybear (metric[2..3]) = the FIRST CLUT slot
 * the sub-palette occupies, xbear (metric[4..5]) = the entry count. So
 * set 1 is start=0/count=256 (the full 256-colour base/master), while the
 * picture screens (sets 2..6) are start=32/count=224 — their sub-palette
 * loads into slots 32..255, leaving the 0..31 system/UI base intact.
 *
 * Magenta (255,0,255) is the "unused / defer" sentinel: those slots are
 * skipped so `dst` keeps its base value. Returns the number of entries
 * written. (The Unlimited Adventures globe's blues live at set 5 entries
 * 208..223 -> slots 240..255 — which is why loading at slot 0 turned the
 * globe magenta and the art washed-out.) */
static short intro_load_palette(long base, short set, RGBColor *dst)
{
	long  handle = l37aa(base, set);
	long  p0, p1;
	const unsigned char *m, *pp;
	short start, n, avail, k;

	if (handle == 0)
		return 0;
	p0 = l37aa(handle, 0);
	p1 = l37aa(handle, 1);
	if (p0 == 0 || p1 <= p0)
		return 0;
	m     = (const unsigned char *)(uintptr_t)p0;          /* 8-byte metric */
	start = (short)(((unsigned short)m[2] << 8) | m[3]);   /* first CLUT slot */
	n     = (short)(((unsigned short)m[4] << 8) | m[5]);   /* entry count     */
	avail = (short)((p1 - p0 - 8) / 3);
	if (n > avail) n = avail;
	pp = (const unsigned char *)(uintptr_t)(p0 + 8);
	for (k = 0; k < n; k++) {
		short         slot = (short)(start + k);
		unsigned char r = pp[k*3+0], g = pp[k*3+1], b = pp[k*3+2];
		if (slot < 0 || slot > 255)
			continue;
		if (r == 255 && g == 0 && b == 255)  /* magenta sentinel: defer */
			continue;
		dst[slot].red   = (unsigned short)((r << 8) | r);
		dst[slot].green = (unsigned short)((g << 8) | g);
		dst[slot].blue  = (unsigned short)((b << 8) | b);
	}
	return n;
}

static void port_show_intro(void)
{
	static unsigned char file[200000];      /* TITLE.CTL (~168KB)        */
	static RGBColor      base_pal[256];     /* set 1: the shared base    */
	static RGBColor      pal[256];
	short        refnum = 0, set;
	long         count, base;

	extern long TickCount(void);

	if (FSOpen((ConstStr255Param)"\011TITLE.CTL", 0, &refnum) != noErr)
		return;                          /* data not mounted — skip   */
	count = (long)sizeof file;
	(void)FSRead(refnum, &count, file);
	(void)FSClose(refnum);
	base = (long)(uintptr_t)file;
	if (l37aa(base, 0) == 0)                 /* not a GLIB                */
		return;

	/* The Mac hides the cursor through the whole intro (clicks still skip);
	 * the sword cursor only returns at the menu. ShowCursor() below re-shows. */
	HideCursor();

	/* Build the shared base CLUT in two layers (the upper indices 224..255
	 * are common to all screens and come from neither the screen nor set 1
	 * alone):
	 *   1. clut 129 — the 256-entry master FRUA palette (already resident
	 *      from boot). Supplies the blue/grey ramp at 240..254 the Unlimited
	 *      Adventures globe draws from.
	 *   2. set 1 — the title base (the bypassed copy-protection backdrop's
	 *      palette). Overrides clut 129 with the silver-stone ramp at
	 *      224..239 the logo frames + title text use. Its 240..254 are the
	 *      magenta "defer" sentinel, so clut 129 shows through there.
	 * Each screen then overlays its own 0..223 (magenta entries deferring to
	 * this base) — see intro_load_palette. */
	memset(base_pal, 0, sizeof base_pal);
	{
		Handle ch = GetResource(0x636C7574L /* 'clut' */, 129);
		if (ch != NULL && *ch != NULL) {
			const unsigned char *cd = (const unsigned char *)*ch;
			short i;
			for (i = 0; i < 256; i++) {
				base_pal[i].red   = (unsigned short)
				    ((cd[8+i*8+2] << 8) | cd[8+i*8+3]);
				base_pal[i].green = (unsigned short)
				    ((cd[8+i*8+4] << 8) | cd[8+i*8+5]);
				base_pal[i].blue  = (unsigned short)
				    ((cd[8+i*8+6] << 8) | cd[8+i*8+7]);
			}
		}
	}
	(void)intro_load_palette(base, 1, base_pal);   /* set 1: full 0..255 base */

	for (set = 2; set <= 6; set++) {
		long          handle, set1h, deadline;
		unsigned char *px;
		short          pitch, sw, sh, i, r;
		EventRecord    ev;

		handle = l37aa(base, set);
		if (handle == 0)
			break;
		if (!qd_screen_pixels(&px, &pitch, &sw, &sh) || px == NULL)
			break;

		/* TRANSITION: blank the *displayed* screen to a solid field and
		 * present it under the CURRENT palette, BEFORE swapping palettes.
		 * Otherwise the next qd_set_palette recolours the previous screen's
		 * still-displayed image for a frame (the corrupted-image flash). */
		for (r = 0; r < sh; r++)
			memset(px + (long)r * pitch, 0, (size_t)sw);
		qd_present();

		/* Every screen's palette = the set-1 base (full 0..255) + this
		 * screen's own sub-palette loaded at its metric start slot (32 for
		 * the picture screens, so their art's 32..255 indices get the right
		 * colours — Micro Magic gold, the AD&D logo, the FR plaque, the UA
		 * globe). intro_load_palette honours the start slot + skips magenta. */
		memcpy(pal, base_pal, sizeof pal);
		(void)intro_load_palette(base, set, pal);
		qd_set_palette(pal, (short)0, (short)256);

		if (set == 5) {
			/* The Unlimited Adventures title is a full-screen image — blit
			 * its pieces opaque, no backdrop. */
			for (i = 1; i < 4; i++)
				ui_glib_blit(handle, i, (short)0, (short)0, 0, 0);
		} else {
			/* Logo screens: set 1's red-gradient / stone backdrop opaque,
			 * then the logo pieces with index-0 transparency. */
			set1h = l37aa(base, (short)1);
			if (set1h != 0)
				ui_glib_blit(set1h, (short)1, (short)0, (short)0, 0, 0);
			for (i = 1; i < 4; i++)
				ui_glib_blit(handle, i, (short)0, (short)0, 1, 0);
		}
		qd_present();

		/* hold until a key / click, or auto-advance after ~4 s so a
		 * headless boot can't wedge. (Cursor hidden, so nothing to redraw.) */
		deadline = TickCount() + 240;    /* 60 ticks/s */
		for (;;) {
			if (WaitNextEvent(everyEvent, &ev, 6, NULL)
			 && (ev.what == keyDown || ev.what == mouseDown))
				break;
			if (TickCount() >= deadline)
				break;
		}
	}

	/* Blank to solid before the menu palette loads, so the last screen's
	 * image (the UA title) can't flash corrupted under clut 129. */
	{
		unsigned char *px;
		short pitch, sw, sh, r;
		if (qd_screen_pixels(&px, &pitch, &sw, &sh) && px != NULL) {
			for (r = 0; r < sh; r++)
				memset(px + (long)r * pitch, 0, (size_t)sw);
			qd_present();
		}
	}
	ShowCursor();                            /* cursor returns for the menu */
	load_frua_palette();                     /* restore the UI palette    */
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
	short i, py = 0, pxx = 0;

	/* One beveled plate per LABELLED command, reconstructed from MENU.CTL
	 * item 1 (raised) / item 2 (recessed) — see port_menu_bar. Label-less
	 * entries are layout spacers: the Mac shows BARE STONE there (a gap
	 * between the main command block and the bottom Unlock/Quit row), so we
	 * skip them rather than drawing an empty plate. */
	for (i = 0; i < n; i++) {
		if (items[i].label == NULL)
			continue;
		jt1135(items[i].y, items[i].x, &py, &pxx);
		port_menu_bar((short)(py - 9), (short)(pxx - 5),
		              (short)150, items[i].recessed ? (short)2 : (short)1);
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
	SetCursor(qd_sword_cursor());        /* engine sword cursor at the UI */
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
		if (items[i].label != NULL) {
			/* Recessed (disabled) commands get an extra cmd 18 =
			 * set rec[28] bit 2, so jt382 paints their label in the
			 * dim stone grey. cmd 36 consumes the first 18 as its
			 * arg (rec[24]); the second 18 is the standalone set-bit. */
			if (items[i].recessed)
				jt452((long)1, (long)items[i].y, (long)items[i].x,
				      (long)(uintptr_t)items[i].label,
				      (long)32, (long)items[i].hotkey,
				      (long)36, (long)18, (long)18,
				      (long)20, (long)21, (long)0);
			else
				jt452((long)1, (long)items[i].y, (long)items[i].x,
				      (long)(uintptr_t)items[i].label,
				      (long)32, (long)items[i].hotkey,
				      (long)36, (long)18, (long)20, (long)21, (long)0);
		}
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
		/* "Unlimited Adventures" (20 chars) centred in the 40-col screen
		 * (page = (40-20)/2 = 10), matching the Mac title. */
		jt94((short)10, (short)3, (short)11, (short)0, "Unlimited Adventures");
		/* Port version + build date. The Mac drew one A5 string here, laid
		 * out version at the left and the date right-of-centre; mirror that
		 * spread (version col 4, date col 23) with __DATE__ so the build
		 * date tracks the build. Edit "Version 0.1" to bump the version. */
		jt94((short)4,  (short)4, (short)7, (short)0, "Version 0.1");
		jt94((short)23, (short)4, (short)7, (short)0, "%s", __DATE__);
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
		for (i = 0; i < 12; i++) {
			jt444(i, (short)(*flags[i] != 0 ? 24 : 16), 0, 0);
			/* Disabled commands (flag 0) get rec[28] bit 2 set so
			 * jt382 paints the label in the dim stone grey. The
			 * DLItem recs are pool slots 0..11 (jt452 allocated them
			 * in order above). */
			if (*flags[i] == 0)
				(g_dlitem_pool + (long)i * DLITEM_BYTES)[28]
				    |= 0x04;
		}

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
	/* Mac coords are (v, h); the port's jt1089 takes (h, v) — pass swapped
	 * so RACE/ALIGNMENT/GENDER stack down the left column and CLASS sits in
	 * the right column (the real FRUA two-column pick layout).
	 * PICK RACE y nudged 8006->8008 (y12->y16): at the port's mandatory ×2
	 * the 8px header font reaches up to y5 and clipped the FRAME top bar
	 * (y0-8); +4px clears it while keeping an 8px gap to the ELF row (y24).
	 * The other headers sit lower and already clear the bar. (320x200
	 * content-vs-frame alignment — the content is also drawable at the Mac's
	 * ×3 where the fixed frame has more room; we never use ×3 — task #108.) */
	jt1089((short)8006, (short)8008, col, "PICK RACE");
	jt1089((short)8006, (short)8040, col, "PICK ALIGNMENT");
	jt1089((short)8006, (short)8076, col, "PICK GENDER");
	jt1089((short)8068, (short)8012, col, "PICK CLASS");
}

/* L29ae (CODE 17 + 0x29ae) — character max-HP finalize. Computes rec[82]
 * (max HP, a word) from the class hit-dice table at g_a5_-30780 (28 bytes
 * per class = 7 four-byte sub-entries of [xp16][count][die]) selected by the
 * class rec[88] and the class-combination type rec[89]:
 *   single-class (rec[89] 0..6): e = base + rec[89]*4;
 *       HP = jt870(e[2], e[3])  [a real rec[89]==count d rec[3] roll]
 *            + word(e[0:1]) + 10.
 *   multi-class (rec[89] 8..16, JT[3] @0x2a74): the deterministic product
 *       HP = e[3]*e[2] + word(e[0:1]) of a fixed sub-entry (0 for 8..12,
 *       2 for 14, 5 for 13/15/16) — the pre-averaged multi-class HP.
 * The asm tail (L2da6) republishes g_a5_-27932 and refreshes the status
 * panel (JT[886]/L24d2/L23fa/L618c); that redraw is deferred — this lift is
 * the HP computation. Fed by the pick flow (jt568 sets rec[88]/rec[89]); the
 * -30780 data is live from the DATA pool ([[code17-chargen-map]]). */
static void l29ae(unsigned char *rec) __attribute__((unused));
static void l29ae(unsigned char *rec)
{
	short          cls, kind, sub = -1;
	unsigned char *base, *e;

	PROBE("l29ae");
	if (rec == NULL)
		return;
	cls  = (short)rec[88];
	kind = (short)rec[89];
	base = (unsigned char *)g_a5_buf(-30780) + (long)cls * 28;

	if ((kind & 0xff) <= 6) {                       /* single-class: real roll */
		e = base + (long)kind * 4;
		*(short *)(rec + 82) = (short)(jt870((short)e[2], (short)e[3])
		                               + *(short *)e + 10);
	} else {
		switch (kind & 0xff) {                  /* JT[3] @0x2a74, 8..16 */
		case 8: case 9: case 10:
		case 11: case 12: sub = 0; break;
		case 14:          sub = 2; break;
		case 13: case 15:
		case 16:          sub = 5; break;
		default:          sub = -1; break;      /* -> no HP set */
		}
		if (sub >= 0) {
			e = base + (long)sub * 4;
			*(short *)(rec + 82) =
			    (short)((short)e[3] * (short)e[2] + *(short *)e);
		}
	}

	/* L2da6 tail — republish the record; status-panel refresh deferred. */
	g_a5_long(-27932) = (long)(uintptr_t)rec;
}

/* CODE 17-local list helpers jt568 calls. L30de redraws the option-list
 * highlight; L2f8e / L31d4 are the pass-1-match / pass-2-fallback finalizers.
 * PROBE stubs for now — the grid-scan/highlight bookkeeping (jt568's own body,
 * lifted below) is the state machine; these draw/finalize the result and are
 * the remaining leaf work. */
/* L31d4 (CODE 17 + 0x31d4) — pass-2 fallback: when the current selection has no
 * valid grid cell, reset the highlight position by class (g_a5_-7018 - 1):
 * the multi-class-ish set {6,12,14,15,16} -> (2,2), the rest {0..5,7..11,13} ->
 * (1,1); out of range leaves it unchanged. */
static void l31d4(void)
{
	PROBE("L31d4");
	switch ((short)g_a5_word(-7018) - 1) {
	case 6: case 12: case 14: case 15: case 16:
		g_a5_word(-7024) = 2; g_a5_word(-7022) = 2;
		break;
	case 0: case 1: case 2: case 3: case 4: case 5:
	case 7: case 8: case 9: case 10: case 11: case 13:
		g_a5_word(-7024) = 1; g_a5_word(-7022) = 1;
		break;
	default:
		break;
	}
}

/* L2f8e (CODE 17 + 0x2f8e) — re-validate the alignment grid position. Pass 1
 * tests whether the current (-7024,-7022) cell holds a valid option value
 * (-7012 latches the match); if not, pass 2 sweeps -7022 over 1..3 for a valid
 * cell; if still none, L31d4 resets to the class default. Walks the option
 * value list at g_a5_-30450 (byte[0]=count, byte[1..]=values). */
static void l2f8e(void)
{
	unsigned char *rec  = (unsigned char *)g_a5_ptr(-7008);
	short          base = (short)(((short)(unsigned char)g_a5_byte(-7027)
	                               + rec[89]) * 12);

	PROBE("L2f8e");
	g_a5_byte(-7011) = ((unsigned char *)g_a5_buf(-30450) + base)[0];
	g_a5_byte(-7012) = 0;

	g_a5_byte(-22307) = 0;                          /* pass 1: current cell */
	while ((unsigned char)g_a5_byte(-22307) < (unsigned char)g_a5_byte(-7011)
	       && g_a5_byte(-7012) == 0) {
		unsigned char *e;
		rec[93] = (unsigned char)(((short)g_a5_word(-7024) - 1) * 3
		          + (short)g_a5_word(-7022) - 1);
		e = (unsigned char *)g_a5_buf(-30450) + base
		    + (unsigned char)g_a5_byte(-22307);
		if ((short)(signed char)e[1] == (short)(unsigned char)rec[93])
			g_a5_byte(-7012) = 1;
		g_a5_byte(-22307)++;
	}
	if (g_a5_byte(-7012) != 0)
		return;

	{                                               /* pass 2: sweep -7022 */
		short matched = 0;
		g_a5_word(-7022) = 1;
		while ((unsigned short)g_a5_word(-7022) < 3 && matched == 0) {
			rec[93] = (unsigned char)(((short)g_a5_word(-7024) - 1) * 3
			          + (short)g_a5_word(-7022) - 1);
			g_a5_byte(-22307) = 0;
			while ((unsigned char)g_a5_byte(-22307)
			       < (unsigned char)g_a5_byte(-7011) && matched == 0) {
				unsigned char *e = (unsigned char *)g_a5_buf(-30450)
				    + base + (unsigned char)g_a5_byte(-22307);
				if ((short)(signed char)e[1] == (short)(unsigned char)rec[93])
					matched = 1;
				g_a5_byte(-22307)++;
			}
			g_a5_word(-7022)++;
		}
		g_a5_word(-7022)--;
		if (matched == 0)
			l31d4();
	}
}

/* L30de (CODE 17 + 0x30de) — redraw the option-list highlight: for each of the
 * three columns, test whether the current selection ((-7024-1)*3 + col - 1) is
 * a valid option value and enable (group 24) or disable (group 16) that
 * column's highlight DLItem (item = col + 32). */
static void l30de(void)
{
	unsigned char *rec  = (unsigned char *)g_a5_ptr(-7008);
	short          base = (short)(((short)(unsigned char)g_a5_byte(-7027)
	                               + rec[89]) * 12);
	short          count = ((unsigned char *)g_a5_buf(-30450) + base)[0];
	short          col;

	PROBE("L30de");
	for (col = 1; col <= 3; col++) {
		short matched = 0;
		g_a5_byte(-22307) = 0;
		while ((unsigned char)g_a5_byte(-22307) < (unsigned char)count
		       && matched == 0) {
			unsigned char *e;
			rec[93] = (unsigned char)(((short)g_a5_word(-7024) - 1) * 3
			          + col - 1);
			e = (unsigned char *)g_a5_buf(-30450) + base
			    + (unsigned char)g_a5_byte(-22307);
			if ((short)(signed char)e[1] == (short)(unsigned char)rec[93])
				matched = 1;
			g_a5_byte(-22307)++;
		}
		jt444((short)(col + 32), matched ? 24 : 16, 0, 0);
	}
}

/* L2f74 (CODE 17 + 0x2f74) — fold the two alignment-axis grid coords into the
 * record's linear option index: rec[93] = (-7024 - 1)*3 + -7022 - 1. */
static void l2f74(void)
{
	unsigned char *rec = (unsigned char *)g_a5_ptr(-7008);
	PROBE("L2f74");
	rec[93] = (unsigned char)(((short)g_a5_word(-7024) - 1) * 3
	          + (short)g_a5_word(-7022) - 1);
}

/* JT[568] (CODE 17 + 0x3382) — the character-creation PICK state machine: per
 * step, set up the option list + the highlight cursor for the current
 * selection. g_a5_-7018 = the step (1=race, 2=align, 3=gender, 4=class, ...);
 * g_a5_-7008 = the working 398-byte record; rec[89] = step-1; rec[93] = the
 * current linear option index. The option table at g_a5_-30450 is 12 bytes per
 * entry: byte[0] = option count, byte[1..count] = the valid option values for
 * that step (indexed by g_a5_-7027 race-base + rec[89]).
 *
 * Body (faithful to 0x3382): (1) enable/disable the step's three option-column
 * DLItems via JT[444] (group 24 = enable / clear the disable bit, 16 = disable
 * / set it); alignment (steps 1,3) hides columns 30/31. (2) read the option
 * count into g_a5_-7011. (3) two symmetric passes scan the option values to
 * locate the current selection (rec[93]) in the 3-column grid: pass 1 varies
 * the column g_a5_-7009 over the fixed row g_a5_-7024; pass 2 varies g_a5_-7010
 * over g_a5_-7022. -7014 / -7013 latch the match; -22307 is the per-entry walk
 * counter. (4) redraw the list (L30de) and set the highlight DLItems 28/32 to
 * the resolved rows. The list-draw leaves (L2f8e/L31d4/L30de) are stubs. */
static void jt568(void) __attribute__((unused));
static void jt568(void)
{
	unsigned char *rec  = (unsigned char *)g_a5_ptr(-7008);
	short          step = g_a5_word(-7018);
	short          base;

	PROBE("jt568");
	rec[89] = (unsigned char)(step - 1);

	switch (step - 1) {                     /* JT[3] @0x339a — column enables */
	case 1: case 3:                         /* alignment: enable col 29, hide 30/31 */
		jt444(29, 24, 0, 0);
		jt444(30, 16, 0, 0);
		jt444(31, 16, 0, 0);
		break;
	case 0: case 2: case 4: case 5: case 6: case 8: case 9:
	case 10: case 11: case 12: case 13: case 14: case 15: case 16:
		jt444(29, 24, 0, 0);            /* race/gender/class: enable all 3 */
		jt444(30, 24, 0, 0);
		jt444(31, 24, 0, 0);
		break;
	default:                                /* step 7 / out of range: no change */
		break;
	}

	/* L3444 — option count for this step's table entry */
	base = (short)(((short)(unsigned char)g_a5_byte(-7027) + rec[89]) * 12);
	g_a5_byte(-7011) = ((unsigned char *)g_a5_buf(-30450) + base)[0];
	g_a5_byte(-7014) = 0;
	g_a5_byte(-7009) = 1;

	/* L34f0 — pass 1: find the column (-7009) of the current selection */
	while ((unsigned char)g_a5_byte(-7009) < 3 && g_a5_byte(-7014) == 0) {
		g_a5_byte(-22307) = 0;                          /* L3472 */
		while ((unsigned char)g_a5_byte(-22307) < (unsigned char)g_a5_byte(-7011)
		       && g_a5_byte(-7014) == 0) {              /* L34d6/L3478 */
			unsigned char *e;
			rec[93] = (unsigned char)
			    (((short)g_a5_word(-7024) - 1) * 3
			     + (short)(unsigned char)g_a5_byte(-7009) - 1);
			e = (unsigned char *)g_a5_buf(-30450) + base
			    + (unsigned char)g_a5_byte(-22307);
			if ((short)(signed char)e[1] == (short)(unsigned char)rec[93])
				g_a5_byte(-7014) = 1;
			g_a5_byte(-22307)++;
		}
		g_a5_byte(-7009)++;                             /* L34ec */
	}
	g_a5_byte(-7009)--;                                     /* L3504 */

	if (g_a5_byte(-7014) != 0) {
		l2f8e();                                       /* pass 1 matched */
	} else {
		g_a5_byte(-7013) = 0;                          /* L3516 — pass 2 */
		g_a5_byte(-7010) = 1;
		while ((unsigned char)g_a5_byte(-7010) < 3 && g_a5_byte(-7013) == 0) {
			g_a5_byte(-22307) = 0;                  /* L3524 */
			while ((unsigned char)g_a5_byte(-22307) < (unsigned char)g_a5_byte(-7011)
			       && g_a5_byte(-7013) == 0) {      /* L3586/L352a */
				unsigned char *e;
				rec[93] = (unsigned char)
				    (((short)(unsigned char)g_a5_byte(-7010) - 1) * 3
				     + (short)g_a5_word(-7022) - 1);
				e = (unsigned char *)g_a5_buf(-30450) + base
				    + (unsigned char)g_a5_byte(-22307);
				if ((short)(signed char)e[1] == (short)(unsigned char)rec[93])
					g_a5_byte(-7013) = 1;
				g_a5_byte(-22307)++;
			}
			g_a5_byte(-7010)++;                     /* L359c */
		}
		g_a5_byte(-7010)--;                             /* L35b4 */
		if (g_a5_byte(-7013) != 0)
			g_a5_word(-7024) = (short)(unsigned char)g_a5_byte(-7010);
		else
			l31d4();
	}

	/* L35ce — redraw the list + set the two highlight DLItems */
	l30de();
	jt444(28, 4, (short)g_a5_word(-7024), 0);
	jt444(32, 4, (short)g_a5_word(-7022), 0);
}

/* JT[567] (CODE 17 + 0x3372) — the GENDER list action proc: store the picked
 * gender (g_a5_-7020, 1-based) into rec[92]. */
static void jt567(void) __attribute__((unused));
static void jt567(void)
{
	unsigned char *rec = (unsigned char *)g_a5_ptr(-7008);
	PROBE("jt567");
	rec[92] = (unsigned char)((short)g_a5_word(-7020) - 1);
}

/* JT[569] (CODE 17 + 0x336c) — alignment axis-1 (law/chaos) action proc:
 * recompute the linear alignment index. */
static void jt569(void) __attribute__((unused));
static void jt569(void)
{
	PROBE("jt569");
	l2f74();
}

/* JT[570] (CODE 17 + 0x334c) — alignment axis-2 (good/evil) action proc:
 * redraw the list highlight, set DLItem 32 to the axis-2 row, recompute the
 * linear alignment index. */
static void jt570(void) __attribute__((unused));
static void jt570(void)
{
	PROBE("jt570");
	l30de();
	l2f8e();
	jt444(32, 4, (short)g_a5_word(-7022), 0);
	l2f74();
}

/* JT[571] (CODE 17 + 0x2f6c) — the "Exit" button action proc: set the
 * cancel/result flag g_a5_-7038 = 1 so L3666's poll (jt453) ends. */
static void jt571(void) __attribute__((unused));
static void jt571(void)
{
	PROBE("jt571");
	g_a5_byte(-7038) = 1;
}

/* CODE 17-local record finalizers jt572 calls (L2284 = pre-finalize, L13ee =
 * derived-field setup), plus the CODE 19 stat-finalize entries JT[907] (run
 * when rec[163] > 0) / JT[906]. PROBE stubs — char-gen commits the picked
 * fields without them; the derived combat stats they compute land when CODE
 * 17/19 finalize is fully lifted. */
static void l2284(unsigned char *rec) { PROBE("L2284"); (void)rec; }
static void l13ee(unsigned char *rec) { PROBE("L13ee"); (void)rec; }
static void jt906(unsigned char *rec) { PROBE("jt906"); (void)rec; }
static void jt907(unsigned char *rec) { PROBE("jt907"); (void)rec; }

/* JT[566] (CODE 17 + 0x3222) — the RACE list action proc. Stores the picked
 * race (g_a5_-7026, 1-based) in rec[88], then for every class index 0..16 scans
 * the race's allowed-class list at g_a5_-30864 (14 bytes/race: byte[0] = count,
 * byte[1..count] = the allowed class indices) and enables (group 24) or
 * disables (group 16) that class's DLItem (item = class + 11). If the currently
 * selected class (g_a5_-7018 - 1) is no longer allowed, reset the class-list
 * selection (DLItem 10) to the first allowed class. */
static void jt566(void) __attribute__((unused));
static void jt566(void)
{
	unsigned char *rec = (unsigned char *)g_a5_ptr(-7008);
	short count, firstok = 0xff, invalid = 0, cls;

	PROBE("jt566");
	rec[88] = (unsigned char)((short)g_a5_word(-7026) - 1);
	count = ((unsigned char *)g_a5_buf(-30864) + (long)rec[88] * 14)[0];

	for (cls = 0; cls <= 16; cls++) {
		unsigned char *e = (unsigned char *)g_a5_buf(-30864)
		    + (long)rec[88] * 14;
		short matched = 0, i;
		for (i = 0; i < count && matched == 0; i++) {
			if ((short)(signed char)e[1 + i] == cls) {
				if (firstok == 0xff)
					firstok = cls;
				matched = 1;
			}
		}
		if (matched) {
			jt444((short)(cls + 11), 24, 0, 0);    /* enable */
		} else {
			if (cls == (short)g_a5_word(-7018) - 1)
				invalid = 1;
			jt444((short)(cls + 11), 16, 0, 0);    /* disable */
		}
	}
	if (invalid)
		jt444(10, 4, (short)(firstok + 1), 0);         /* reset selection */
	g_a5_byte(-7027) = 0;
}

/* JT[572] (CODE 17 + 0x2e6c) — the "Done" button finalize: commit the picked
 * character. L2284 pre-finalize; copy the design handle into rec[68]; mark
 * rec[137] = 1; L13ee derived setup; JT[907] if rec[163] > 0; derive rec[127]
 * (the max per-ability table[0] over the six scores) and rec[183] (the bonus
 * sum) from g_a5_-23184 (22 bytes/ability, indexed by the score rec[157+i]) and
 * g_a5_-23030; JT[906]; then set the created record current (g_a5_-27932). */
static void jt572(void) __attribute__((unused));
static void jt572(void)
{
	unsigned char *rec = (unsigned char *)g_a5_ptr(-7008);
	short i;

	PROBE("jt572");
	l2284(rec);
	*(long *)(rec + 68) = g_a5_long(-18882);
	rec[137] = 1;
	l13ee(rec);
	if ((short)(signed char)rec[163] > 0)
		jt907(rec);
	rec[183] = 0;
	rec[127] = 0;
	for (i = 0; i <= 6; i++) {
		unsigned char *e;
		short score = rec[157 + i];
		if (score == 0)
			continue;
		e = (unsigned char *)g_a5_buf(-23184) + (long)i * 22 + score;
		if ((unsigned char)e[0] > (unsigned char)rec[127])
			rec[127] = e[0];
		rec[183] = (unsigned char)(rec[183]
		           + ((unsigned char *)g_a5_buf(-23030) + i)[0]);
	}
	jt906(rec);
	g_a5_long(-27932) = (long)(uintptr_t)rec;
}

/* L3666 (CODE 17 + 0x3666) — character-creation screen init + header draw.
 * Sets the window dims for the display mode, paints the PICK headers, and
 * seeds the wizard state (step g_a5_-7018). The FULL Mac body then rolls
 * ability scores (the L34f0 loop over the race/class tables at g_a5_-30450)
 * and runs the pick state machine — that is the large multi-session
 * remainder of the CODE 17 lift and stays TODO. */
static int l3666(void)
{
	unsigned char *rec = (unsigned char *)g_a5_ptr(-7008);

	PROBE("L3666");
	g_a5_byte(-7038) = 0;
	if (jt1200() == 3) {                 /* deep mode */
		g_a5_word(-7000) = 15;
		g_a5_word(-7016) = 15;
	} else {
		g_a5_word(-7000) = 135;
		g_a5_word(-7016) = 140;
	}
	l35f8();                             /* PICK headers */
	jt117();
	g_a5_word(-7026) = 6;                /* default race index */
	g_a5_word(-7024) = 1;
	g_a5_word(-7022) = 1;
	g_a5_word(-7020) = 1;
	g_a5_word(-7018) = 3;                /* default class index */
	rec[88] = 5; rec[93] = 0; rec[92] = 0; rec[89] = 2;

	jt174();
	jt447();                             /* reset the DLItem table */

	/* The faithful jt452 option-list builds (one container [shape 2] + item
	 * rows [shape 3], each carrying the value-output global + the action proc
	 * jt453's poll fires on a pick). Streams transcribed from L3666's asm
	 * (cmd 34 = action proc, 35 = value addr, 38 = rec[31] row tag, 17 = the
	 * default-row flag). g_a5_-7000 = the per-row tag (15 deep / 135 shallow). */

	/* RACE list (jt566 -> g_a5_-7026): ELF..HUMAN */
	jt452(2L, 6L, 34L, (long)(uintptr_t)&jt566, 35L, (long)(uintptr_t)&g_a5_byte(-7026),
	      3L, 8012L, 8006L, (long)(uintptr_t)ua_strs_at(0x48c6), 38L, (long)g_a5_word(-7000),
	      3L, 8016L, 8006L, (long)(uintptr_t)ua_strs_at(0x48ca), 38L, (long)g_a5_word(-7000),
	      3L, 8020L, 8006L, (long)(uintptr_t)ua_strs_at(0x48d4), 38L, (long)g_a5_word(-7000),
	      3L, 8024L, 8006L, (long)(uintptr_t)ua_strs_at(0x48da), 38L, (long)g_a5_word(-7000),
	      3L, 8028L, 8006L, (long)(uintptr_t)ua_strs_at(0x48e0), 38L, (long)g_a5_word(-7000),
	      3L, 8032L, 8006L, (long)(uintptr_t)ua_strs_at(0x48ea), 38L, (long)g_a5_word(-7000),
	      0L);

	/* GENDER list (jt567 -> g_a5_-7020): MALE, FEMALE */
	jt452(2L, 2L, 34L, (long)(uintptr_t)&jt567, 35L, (long)(uintptr_t)&g_a5_byte(-7020),
	      3L, 8082L, 8006L, (long)(uintptr_t)ua_strs_at(0x48f0), 38L, (long)g_a5_word(-7000),
	      3L, 8086L, 8006L, (long)(uintptr_t)ua_strs_at(0x48f6), 38L, (long)g_a5_word(-7000),
	      0L);

	/* CLASS list (jt568 -> g_a5_-7018), built in three jt452 calls: the single
	 * classes then the multi-class combos (cmd 17 flags the default row). */
	jt452(2L, 17L, 34L, (long)(uintptr_t)&jt568, 35L, (long)(uintptr_t)&g_a5_byte(-7018),
	      3L, 8018L, 8070L, (long)(uintptr_t)ua_strs_at(0x48fe), 38L, (long)g_a5_word(-7000),
	      3L, 8022L, 8070L, (long)(uintptr_t)ua_strs_at(0x4906), 38L, (long)g_a5_word(-7000), 17L,
	      3L, 8022L, 8070L, (long)(uintptr_t)ua_strs_at(0x490c), 38L, (long)g_a5_word(-7000),
	      3L, 8026L, 8070L, (long)(uintptr_t)ua_strs_at(0x4914), 38L, (long)g_a5_word(-7000),
	      3L, 8030L, 8070L, (long)(uintptr_t)ua_strs_at(0x491c), 38L, (long)g_a5_word(-7000),
	      0L);
	jt452(3L, 8034L, 8070L, (long)(uintptr_t)ua_strs_at(0x4924), 38L, (long)g_a5_word(-7000),
	      3L, 8038L, 8070L, (long)(uintptr_t)ua_strs_at(0x4930), 38L, (long)g_a5_word(-7000),
	      3L, 8042L, 8070L, (long)(uintptr_t)ua_strs_at(0x4936), 38L, (long)g_a5_word(-7000), 17L,
	      3L, 8042L, 8070L, (long)(uintptr_t)ua_strs_at(0x493c), 38L, (long)g_a5_word(-7000),
	      3L, 8046L, 8070L, (long)(uintptr_t)ua_strs_at(0x494c), 38L, (long)g_a5_word(-7000),
	      3L, 8050L, 8070L, (long)(uintptr_t)ua_strs_at(0x4960), 38L, (long)g_a5_word(-7000),
	      0L);
	jt452(3L, 8054L, 8070L, (long)(uintptr_t)ua_strs_at(0x496e), 38L, (long)g_a5_word(-7000),
	      3L, 8054L, 8070L, (long)(uintptr_t)ua_strs_at(0x4980), 38L, (long)g_a5_word(-7000), 17L,
	      3L, 8058L, 8070L, (long)(uintptr_t)ua_strs_at(0x498e), 38L, (long)g_a5_word(-7000),
	      3L, 8062L, 8070L, (long)(uintptr_t)ua_strs_at(0x49a2), 38L, (long)g_a5_word(-7000),
	      3L, 8066L, 8070L, (long)(uintptr_t)ua_strs_at(0x49b0), 38L, (long)g_a5_word(-7000),
	      3L, 8070L, 8070L, (long)(uintptr_t)ua_strs_at(0x49c2), 38L, (long)g_a5_word(-7000),
	      0L);

	/* ALIGNMENT: axis-2 law/chaos (jt570 -> g_a5_-7024) then axis-1 good/evil
	 * (jt569 -> g_a5_-7022), both in one jt452 call. */
	jt452(2L, 3L, 34L, (long)(uintptr_t)&jt570, 35L, (long)(uintptr_t)&g_a5_byte(-7024),
	      3L, 8046L, 8006L, (long)(uintptr_t)ua_strs_at(0x49d4), 38L, (long)g_a5_word(-7000),
	      3L, 8050L, 8006L, (long)(uintptr_t)ua_strs_at(0x49dc), 38L, (long)g_a5_word(-7000),
	      3L, 8054L, 8006L, (long)(uintptr_t)ua_strs_at(0x49e4), 38L, (long)g_a5_word(-7000),
	      2L, 3L, 34L, (long)(uintptr_t)&jt569, 35L, (long)(uintptr_t)&g_a5_byte(-7022),
	      3L, 8060L, 8006L, (long)(uintptr_t)ua_strs_at(0x49ec), 38L, (long)g_a5_word(-7000),
	      3L, 8064L, 8006L, (long)(uintptr_t)ua_strs_at(0x49f2), 38L, (long)g_a5_word(-7000),
	      3L, 8068L, 8006L, (long)(uintptr_t)ua_strs_at(0x49fa), 38L, (long)g_a5_word(-7000),
	      0L);

	/* Done (jt572, hotkey 'D') + Exit (jt571, hotkey 'E') buttons [shape 1].
	 * y=8098 (vs the Mac's 8094) seats the label + item-14 glyph on the
	 * FRAME.CTL bottom command bar (item 4, drawn by jt76) — verified. */
	jt452(1L, 8098L, 8004L, (long)(uintptr_t)ua_strs_at(0x4a00), 20L, 32L, 68L, 36L, 4L,
	      34L, (long)(uintptr_t)&jt572, 21L,
	      1L, 8098L, 8024L, (long)(uintptr_t)ua_strs_at(0x4a06), 20L, 32L, 69L, 33L, 35L, 36L, 4L,
	      34L, (long)(uintptr_t)&jt571, 21L,
	      0L);

	/* DONE / EXIT each get the real MENU.CTL command-bar plate (item 1),
	 * the same plate the main-menu command buttons get — jt382 (shape 1)
	 * only paints the label + item-14 glyph, so the plate is drawn here
	 * (before the paint walk, so the labels land on top). Each is ~one
	 * button wide; DONE at x 8004, EXIT at x 8024 (the build coords). */
	{
		short py = 0, pxx = 0;
		/* Two abutting plates (DONE x 8004, EXIT x 8024 = 40px apart). Like
		 * menu_draw_plates, the plate starts 5px LEFT of the button origin
		 * so the left-justified label (drawn at the origin by jt382) sits in
		 * from the plate's left bevel instead of on it. The shared edge
		 * reads as the Mac's DONE|EXIT divider. */
		jt1135((short)8098, (short)8004, &py, &pxx);
		port_menu_bar((short)(py - 9), (short)(pxx - 5), (short)40, (short)1);
		jt1135((short)8098, (short)8024, &py, &pxx);
		port_menu_bar((short)(py - 9), (short)(pxx - 5), (short)40, (short)1);
	}

	jt449(1);
	jt453((jt453_filter_t)0);            /* the modal pick poll */
	jt451();

	/* return 1 = Done / committed, 0 = Exit / cancelled (g_a5_-7038 set). */
	return (g_a5_byte(-7038) == 0) ? 1 : 0;
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

/* Per-class ability minimums (AD&D-1e prime-requisite / restriction
 * thresholds; 0 = no minimum). Stat order STR INT WIS DEX CON CHA;
 * class order Cleric Fighter Mage Thief Paladin Ranger. */
static const unsigned char cg_class_min[CG_NCLASSES][6] = {
	{  0,  0,  9,  0,  0,  0 },   /* Cleric:  WIS 9                       */
	{  9,  0,  0,  0,  0,  0 },   /* Fighter: STR 9                       */
	{  0,  9,  0,  0,  0,  0 },   /* Magic-User: INT 9                    */
	{  0,  0,  0,  9,  0,  0 },   /* Thief:   DEX 9                       */
	{ 12,  9, 13,  0,  9, 17 },   /* Paladin: STR12 INT9 WIS13 CON9 CHA17 */
	{ 13, 13, 14,  0, 14,  0 },   /* Ranger:  STR13 INT13 WIS14 CON14     */
};

/* Roll the six ability scores: 3d6 each (the FRUA LCG) + the race
 * adjustment, clamped 3..18, and re-rolled per stat until the chosen
 * class's minimum is met (so the character is class-legal — the game
 * guarantees a usable roll for the picked class). */
static void cg_roll_stats(short race, short klass, short *stats)
{
	short i, t;
	for (i = 0; i < 6; i++) {
		short mn = (short)cg_class_min[klass][i];
		short v  = 3;
		for (t = 0; t < 2000; t++) {
			v = (short)((ua_rand(6) + 1) + (ua_rand(6) + 1)
			          + (ua_rand(6) + 1) + cg_race_adj[race][i]);
			if (v < 3)  v = 3;
			if (v > 18) v = 18;
			if (v >= mn)
				break;
		}
		if (v < mn)            /* fallback (shouldn't happen, mn <= 18) */
			v = mn;
		stats[i] = v;
	}
}

#define CG_NALIGNS 9
static const char *const cg_aligns[CG_NALIGNS] = {
	"Lawful Good", "Lawful Neut", "Lawful Evil",
	"Neutral Good", "Neutral",     "Neutral Evil",
	"Chaotic Good", "Chaotic Neut", "Chaotic Evil",
};

/* Which alignments each class may be (bit i = cg_aligns[i]). The AD&D-1e
 * restrictions FRUA follows; the game's own table is the faithful source. */
static const unsigned short cg_class_aligns[CG_NCLASSES] = {
	0x1FF,   /* Cleric:     any          */
	0x1FF,   /* Fighter:    any          */
	0x1FF,   /* Magic-User: any          */
	0x1F8,   /* Thief:      non-lawful   */
	0x001,   /* Paladin:    Lawful Good  */
	0x049,   /* Ranger:     any Good     */
};

static short cg_allowed_aligns(short klass, short *out)
{
	short i, n = 0;
	unsigned short mask = cg_class_aligns[klass];
	for (i = 0; i < CG_NALIGNS; i++)
		if (mask & (1u << i))
			out[n++] = i;
	return n;
}

/* The whole char-gen pick state. Steps: 0 race, 1 gender, 2 class,
 * 3 alignment, 4 stats, 5 name. The class list is gated by race; the
 * alignment list by the chosen class. */
typedef struct {
	short race, gender, ksel, asel, step;
	short allowed[CG_NCLASSES]; short nallowed;   /* race-gated classes   */
	short aligned[CG_NALIGNS];  short naligned;    /* class-gated aligns   */
	short stats[6];
	char  name[16]; short namelen;
} cg_state;

/* Draw the char-gen screen from the pick state: each region shows its
 * current choice in cyan once reached, the rest light grey; the class and
 * alignment lists show only their gated options.
 * (Superseded by the faithful L3666 DLItem lists; kept for reference.) */
static void cg_draw(const cg_state *s) __attribute__((unused));
static void cg_draw(const cg_state *s)
{
	short k;

	for (k = 0; k < CG_NRACES; k++)
		jt1089((short)8006, (short)(8010 + 3 * k),
		       (short)(k == s->race ? 11 : 7), "%s", cg_races[k]);
	for (k = 0; k < CG_NGENDERS; k++)
		jt1089((short)8076, (short)(8010 + 3 * k),
		       (short)(k == s->gender && s->step >= 1 ? 11 : 7), "%s",
		       cg_genders[k]);
	if (s->step >= 2)                    /* class list (race-gated) */
		for (k = 0; k < s->nallowed; k++)
			jt1089((short)8006, (short)(8035 + 3 * k),
			       (short)(k == s->ksel ? 11 : 7), "%s",
			       cg_classes[s->allowed[k]]);
	if (s->step >= 3)                    /* alignment list (class-gated) */
		for (k = 0; k < s->naligned; k++)
			jt1089((short)8040, (short)(8010 + 3 * k),
			       (short)(k == s->asel ? 11 : 7), "%s",
			       cg_aligns[s->aligned[k]]);
	else                                 /* before class pick: show all 9 */
		for (k = 0; k < CG_NALIGNS; k++)
			jt1089((short)8040, (short)(8010 + 3 * k), (short)7,
			       "%s", cg_aligns[k]);
	if (s->step >= 4)                    /* rolled ability scores */
		for (k = 0; k < 6; k++)
			jt1089((short)8076, (short)(8024 + 3 * k), (short)7,
			       "%s %d", cg_stat_names[k], s->stats[k]);
	if (s->step == 4)
		jt1089((short)8006, (short)8058, (short)7,
		       "R = re-roll   Return = keep");
	else if (s->step == 5)               /* name entry */
		jt1089((short)8006, (short)8058, (short)11,
		       "Name: %s_", s->name);
}

/* Per-class hit die (Cleric Fighter Mage Thief Paladin Ranger). */
static const unsigned char cg_class_hd[CG_NCLASSES] = { 8, 10, 4, 6, 10, 8 };

/* Build a character record from the finished pick state and append it to
 * the roster (g_a5_-27928 linked list, next ptr at +0). Sets the fields
 * the roster grid reads — name@+96, AC@+385, HP@+395 — with HP from the
 * class hit die + CON bonus and AC from 10 - DEX bonus (AD&D-1e style).
 * The full play-record (stats/class/saves at their faithful offsets) is
 * the next slice; this makes the created character appear in the party. */
static void cg_build_record(const cg_state *s)
{
	unsigned char *rec;
	short klass  = s->allowed[s->ksel];
	short con    = s->stats[4], dex = s->stats[3];
	short conmod = (con >= 16) ? 2 : (con >= 15) ? 1 : (con <= 6) ? -1 : 0;
	short dexmod = (dex >= 15) ? (dex - 14) : 0;
	short hp, ac, c;

	if (cg_pool_count >= 16)             /* pool full — drop the create */
		return;
	if (dexmod > 4) dexmod = 4;
	hp = (short)(ua_rand(cg_class_hd[klass]) + 1 + conmod);
	if (hp < 1) hp = 1;
	ac = (short)(10 - dexmod);

	rec = cg_pool[cg_pool_count++];
	memset(rec, 0, 512);
	for (c = 0; c < s->namelen && c < 15; c++)
		rec[96 + c] = (unsigned char)s->name[c];
	rec[96 + c]  = 0;
	rec[CHAR_HP]     = (unsigned char)hp;
	rec[CHAR_MAXHP] = (unsigned char)hp;
	rec[CHAR_AC]     = (unsigned char)ac;
	rec[CHAR_THAC0] = 40;                /* level 1 -> THAC0 20 */
	rec[CHAR_MOVE]  = 12;
	rec[CHAR_RACE]  = (unsigned char)s->race;
	rec[CHAR_CLASS] = (unsigned char)klass;
	rec[CHAR_LEVEL] = 1;
	for (c = 0; c < 6; c++)
		rec[CHAR_STATS + c] = (unsigned char)s->stats[c];
	rec[CHAR_ALIGN] = (unsigned char)s->aligned[s->asel];
	/* Creating a character joins the active party if there's a free slot;
	 * otherwise it stays benched in the pool (add it later via Add). */
	rec[CHAR_INPARTY] = (cg_party_size() < CG_PARTY_MAX) ? 1 : 0;

	cg_party_relink();
	save_roster();
}

static long jt1199(long a);   /* design-handle helper (defined below) */

/* JT[574] (CODE 17 + 0x3b5e) — the character create/train entry (l0f1a /
 * case 0). Shows the char-creation screen (L3666 -> the PICK race/class/
 * gender/alignment headers + the option lists) on the shared stone chrome,
 * then holds until a key. The selection + stat-roll state machine is the
 * deferred remainder (docs/menu-wiring-plan.md / TODO). */
static int  jt574(long ctx)
{
	unsigned char *px; short pitch, sw, sh, yy;

	PROBE("jt574");
	(void)ctx;

	/* 320x200 ×2 — like EVERY screen. The port is full-screen 320x200; the
	 * Mac's 640x400 (×3 / g_a5_-2347 = 0) is a windowed-desktop scale we
	 * NEVER use (640x400 @256c exceeds a stock Falcon's VIDEL bandwidth).
	 * Char-gen's 8006.. coords render correctly here (PICK RACE y12 etc.);
	 * do NOT switch to ×3 to gain margin (it engages deep mode — wrong
	 * colours/markers — and overflows 320x200). Any header-vs-frame
	 * tightness is a 320x200 alignment detail (task #108). */
	g_a5_2347 = 1;
	load_menu_ui();                      /* shared UI palette + backdrop */

	if (qd_screen_pixels(&px, &pitch, &sw, &sh) && px) {
		if (g_menu_state == 1)
			fill_backdrop(px, pitch, 0, 0,
			              (short)(sw - 1), (short)(sh - 1));
		else
			for (yy = 0; yy < sh; yy++)
				memset(px + (long)yy * pitch, 0x08, (size_t)sw);
		qd_present();
		/* Open the engine clip to the full screen. The faithful entry sets
		 * this when the char-gen window opens; the port shortcuts that, so
		 * without it g_a5_-3050/-3052 are 0 and every jt1161 fill (the jt76
		 * grey panel, l3666's frame) clips to nothing. jt1089 text is
		 * unaffected (it doesn't clip), which is why the labels showed but
		 * the panel didn't. */
		g_a5_3054 = 0; g_a5_3056 = 0; g_a5_3050 = sh; g_a5_3052 = sw;
	}
	/* Set up the faithful working record (g_a5_-7008) the pick action procs
	 * write into, seed the char-gen defaults, and the design handle jt572
	 * (Done) stamps into rec[68]. Then run the faithful pick screen L3666 (the
	 * jt452 race/gender/class/alignment lists + the jt453 modal poll firing
	 * jt566..jt572). On a Done commit jt572 sets g_a5_-27932 = rec; Exit/cancel
	 * returns 0. */
	{
		static unsigned char cg_rec[398];

		memset(cg_rec, 0, sizeof cg_rec);
		g_a5_ptr(-7008) = cg_rec;
		cg_rec[179] = 50; cg_rec[127] = 40; cg_rec[94] = 0;
		cg_rec[382] = 1;  cg_rec[130] = 1;  cg_rec[189] = 8;
		g_a5_18882 = jt1199(g_a5_18844);   /* new-character design handle */

		if (l3666() != 0) {
			/* Done committed: add a roster character from the faithful picks.
			 * The faithful derived-stat finalize (L238e/L0006) + name-entry +
			 * .CHR save (jt584) aren't lifted yet, so the picked RACE drives a
			 * valid class/alignment, the stats are rolled port-side, and the
			 * name is a placeholder (rename later). cg_build_record threads it
			 * into the pool/party (g_a5_-27928) and persists it. */
			cg_state s;
			short k;
			static const char placeholder[] = "NEW HERO";

			memset(&s, 0, sizeof s);
			s.race = (short)(unsigned char)g_a5_byte(-7026) - 1;
			if (s.race < 0 || s.race >= CG_NRACES)
				s.race = 0;
			s.gender = (short)(unsigned char)g_a5_byte(-7020) - 1;
			if (s.gender < 0 || s.gender >= CG_NGENDERS)
				s.gender = 0;
			s.nallowed = cg_allowed_classes(s.race, s.allowed);
			s.ksel     = 0;                /* first class the race allows */
			s.naligned = cg_allowed_aligns(s.allowed[s.ksel], s.aligned);
			s.asel     = 0;
			cg_roll_stats(s.race, s.allowed[s.ksel], s.stats);
			for (k = 0; placeholder[k] && k < 15; k++)
				s.name[k] = placeholder[k];
			s.name[k] = 0;
			s.namelen = k;
			cg_build_record(&s);
		}
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
/* Train Character — the port's leveling screen (case 0). Page the party;
 * a member whose running XP (CHAR_XP) has reached the next-level threshold
 * (level * 1000) can train up: level +1 and a hit-die + CON HP gain. Combat
 * THAC0 improves implicitly (the resolver uses 21 - level). The faithful
 * per-class advancement tables (saves, spell slots, exact THAC0 steps) are
 * the deferred remainder. */
static long cg_train_threshold(short level)
{
	return (long)(level < 1 ? 1 : level) * 1000;
}

static void cg_train_screen(void)
{
	unsigned char *party[16];
	short          nparty, sel = 0;
	unsigned char *px; short pitch, sw, sh, yy;
	unsigned char  scan = 0, ascii = 0;

	nparty = cg_collect_party(party, 16);
	if (nparty == 0)
		return;
	g_a5_2347 = 1;
	load_menu_ui();
	while (plat_kb_poll(&scan, &ascii))
		;

	for (;;) {
		unsigned char *c    = party[sel];
		short          lvl  = c[CHAR_LEVEL] ? c[CHAR_LEVEL] : 1;
		long           xp   = *(long *)(c + CHAR_XP);
		long           need = cg_train_threshold(lvl);
		int            dead  = (c[CHAR_HP] == 0);
		int            ready = (!dead && xp >= need && lvl < 20);
		char           buf[48];

		if (qd_screen_pixels(&px, &pitch, &sw, &sh) && px) {
			if (g_menu_state == 1) {
				fill_backdrop(px, pitch, 0, 0,
				              (short)(sw - 1), (short)(sh - 1));
				draw_plate(px, pitch, sw, sh, 6, 8, 313, 150, 1);
			} else {
				for (yy = 0; yy < sh; yy++)
					memset(px + (long)yy * pitch, 0x08,
					       (size_t)sw);
			}
		}
		jt94((short)3, (short)2, 14, 0, "Training Hall");
		jt94((short)3, (short)4, 15, 0, "%s",
		     (const char *)&c[96]);
		sprintf(buf, "Level %d   XP %ld", (int)lvl, xp);
		jt94((short)3, (short)6, 7, 0, "%s", buf);
		if (dead) {
			jt94((short)3, (short)7, 12, 0, "Has fallen in battle.");
			jt94((short)3, (short)10, 11, 0,
			     "The temple can raise them.  (R)");
		} else {
			sprintf(buf, "Train to level %d at %ld XP",
			        (int)(lvl + 1), need);
			jt94((short)3, (short)7, 7, 0, "%s", buf);
			if (ready)
				jt94((short)3, (short)10, 11, 0,
				     "Ready to train!  (T)");
			else
				jt94((short)3, (short)10, 7, 0,
				     "Not enough experience yet.");
		}
		jt94((short)3, (short)16, 7, 0,
		     "T train  R raise  Up/Dn  Esc done");
		qd_present();

		while (!plat_kb_poll(&scan, &ascii))
			;
		if (ascii == 27 || ascii == 13 || ascii == 3)
			break;
		else if (scan == 0x48)
			sel = (short)((sel + nparty - 1) % nparty);
		else if (scan == 0x50)
			sel = (short)((sel + 1) % nparty);
		else if ((ascii == 't' || ascii == 'T') && ready) {
			short klass  = c[CHAR_CLASS]; if (klass >= CG_NCLASSES) klass = 0;
			short con    = c[CHAR_STATS + 4];
			short conmod = (con >= 16) ? 2 : (con >= 15) ? 1
			             : (con <= 6) ? -1 : 0;
			short gain   = (short)(ua_rand(cg_class_hd[klass]) + 1 + conmod);
			short nhp, nmax;
			if (gain < 1) gain = 1;
			nhp  = (short)(c[CHAR_HP] + gain);
			nmax = (short)(c[CHAR_MAXHP] + gain);
			c[CHAR_HP]        = (unsigned char)(nhp  > 255 ? 255 : nhp);
			c[CHAR_MAXHP] = (unsigned char)(nmax > 255 ? 255 : nmax);
			c[CHAR_LEVEL] = (unsigned char)(lvl + 1);
			c[CHAR_THAC0] = (unsigned char)(39 + (lvl + 1)); /* THAC0 improves */
			save_roster();
		} else if ((ascii == 'r' || ascii == 'R') && dead) {
			/* temple raise: restore the fallen to full HP */
			short mx = c[CHAR_MAXHP];
			if (mx < 1) mx = (short)(lvl * 4 + 1);   /* legacy/unset */
			c[CHAR_HP] = (unsigned char)(mx > 255 ? 255 : mx);
			save_roster();
		}
	}
}

static int l0f1a(short a)
{
	(void)a;
	PROBE("jt918/case0 L0f1a");
	if (g_a5_14440 != 0)
		cg_train_screen();           /* port: level up on earned XP */
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
/* jt169 (CODE 7 + 0x3600) — the scrollable saved-character list picker the
 * roster dialogs (L15e2 / L12a0) open: it renders the node list (names at +5,
 * .next at 0) inside the dialog window and lets the user move a selection and
 * pick one.  The Mac body is a ~225-instruction interactive loop with its own
 * render/scroll/input sub-helpers; this is a functional reimplementation over
 * the port's primitives — jt94 for the rows, WaitNextEvent for the keys —
 * preserving the faithful contract: return 0 + *next = chosen node on select,
 * 27 on Escape (or empty list), and *idx = the current row.  Up/Down move the
 * selection; Return/Enter select; Escape cancels. */
static int  jt169(long h1, long h2, short top, short left,
                  short right, short bottom, long head,
                  short a, short b,
                  unsigned char *flag, short *idx, long *next)
{
	EventRecord ev;
	long        e;
	short       count = 0, sel, visible, scroll, i;
	int         dirty = 1;

	PROBE("jt169");
	(void)h1; (void)h2; (void)right; (void)a; (void)b;

	for (e = head; e != 0; e = *(const long *)(uintptr_t)e)
		count++;
	if (count == 0) {
		if (idx  != NULL) *idx  = 0;
		if (next != NULL) *next = 0;
		return 27;
	}

	sel = (idx != NULL) ? *idx : 0;
	if (sel < 0)        sel = 0;
	if (sel >= count)   sel = (short)(count - 1);
	visible = (short)(bottom - top);
	if (visible < 1)    visible = 1;

	for (;;) {
		if (dirty) {
			scroll = (short)((sel >= visible) ? sel - visible + 1 : 0);
			e = head;
			for (i = 0; i < scroll && e != 0; i++)
				e = *(const long *)(uintptr_t)e;
			for (i = 0; i < visible && e != 0; i++) {
				const unsigned char *n =
					(const unsigned char *)(uintptr_t)e;
				short row = (short)(top + i);
				short who = (short)(scroll + i);
				jt94(left, row, (short)(who == sel ? 15 : 11), 0,
				     "%c%s", (who == sel) ? '>' : ' ',
				     (const char *)&n[5]);
				e = *(const long *)(uintptr_t)e;
			}
			if (flag != NULL) *flag = 1;
			qd_present();           /* flush the rows to VIDEL */
			dirty = 0;
		}

		if (!WaitNextEvent(everyEvent, &ev, 1, NULL))
			continue;
		if (ev.what != keyDown && ev.what != autoKey)
			continue;

		switch ((short)(ev.message & 0xff)) {
		case 30:                                   /* up arrow      */
			sel = (short)((sel > 0) ? sel - 1 : count - 1);
			dirty = 1;
			break;
		case 31:                                   /* down arrow    */
			sel = (short)((sel < count - 1) ? sel + 1 : 0);
			dirty = 1;
			break;
		case 13: case 3:                           /* Return / Enter */
			if (idx != NULL) *idx = sel;
			if (next != NULL) {
				e = head;
				for (i = 0; i < sel && e != 0; i++)
					e = *(const long *)(uintptr_t)e;
				*next = e;
			}
			return 0;
		case 27:                                   /* Escape        */
			if (idx  != NULL) *idx  = sel;
			if (next != NULL) *next = 0;
			return 27;
		default:
			break;
		}
	}
}
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
/* jt990 / jt991 (CODE 5 + 0x1b76 / +0x1cb6) — enumerate the entries of a
 * folder, one at a time, with a keep-files / keep-dirs / name-match filter.
 *
 * The Mac builds a "*.*" pattern under the folder path and opens an indexed
 * catalog read (JT[426]); jt991 walks it (JT[432]) and, per entry, drops
 * directories unless g_a5_-4657 is set, drops files unless g_a5_-4656 is set,
 * skips "." / "..", and (when a match name g_a5_-4654 is set) keeps only names
 * that JT[396]-match it — returning the entry's name pointer and writing the
 * is-directory flag through the caller's pointer, or 0 at end.
 *
 * The faithful Atari mapping is a GEMDOS Fsfirst/Fsnext wildcard scan, which
 * the file shim already wraps (compat/files.c files_find_first/_next — the
 * same primitives load_roster uses).  jt990 takes the GEMDOS pattern directly
 * and primes the scan; Fsfirst's attr 0 already excludes directories, so the
 * keep-dirs arm never fires.  The Mac's filter state is preserved in the same
 * A5 globals (g_a5_-4654 match / -4656 keep-files / -4657 keep-dirs) for the
 * consumers that read them.
 *
 * Port reconciliation: the Mac scans the design's HFS SAVE folder filtered by
 * the "cch" character-file type; the port stores saved characters as
 * slot-named CHAR*.CHR files in the working dir (GEMDOS 8.3 can't hold the
 * Mac's name-based files), so L01be passes the "CHAR*.CHR" pattern and reads
 * each record's name field for the display name. */
static char g_dir_cur[16];          /* DTA name of the pending entry        */
static int  g_dir_pending;          /* an entry is waiting in g_dir_cur     */
static char g_dir_ret[16];          /* stable name handed back to caller    */

static int jt990(short drive, void *pattern, const void *matchname,
                 short keepfiles, short keepdirs)
{
	PROBE("jt990");
	(void)drive;
	g_a5_long(-4654) = (long)(uintptr_t)matchname;   /* name filter         */
	g_a5_byte(-4656) = (signed char)keepfiles;       /* keep files          */
	g_a5_byte(-4657) = (signed char)keepdirs;        /* keep directories    */
	if (pattern == NULL)
		return 0;
	g_dir_pending = files_find_first((const char *)pattern, g_dir_cur,
	                                 (int)sizeof g_dir_cur);
	return g_dir_pending ? 1 : 0;
}

static long jt991(void *out_is_dir)
{
	char name[16];
	int  i;

	PROBE("jt991");
	while (g_dir_pending) {
		for (i = 0; i < (int)sizeof name - 1 && g_dir_cur[i] != 0; i++)
			name[i] = g_dir_cur[i];
		name[i] = 0;
		g_dir_pending = files_find_next(g_dir_cur,    /* prime next call */
		                                (int)sizeof g_dir_cur);

		if (g_a5_byte(-4656) == 0)
			continue;                     /* not keeping files       */
		if (g_a5_long(-4654) != 0
		 && jt396((const char *)(uintptr_t)g_a5_long(-4654), name) == 0)
			continue;                     /* name filter rejected    */

		for (i = 0; i < (int)sizeof g_dir_ret - 1 && name[i] != 0; i++)
			g_dir_ret[i] = name[i];
		g_dir_ret[i] = 0;
		if (out_is_dir != NULL)
			*(signed char *)out_is_dir = 0;   /* Fsfirst attr 0 = file */
		return (long)(uintptr_t)g_dir_ret;
	}
	return 0;
}

/* l005a (the save-folder precondition) and jt42 (the notice alert) are lifted
 * further down the file; forward-declare them so jt589's roster dispatch here
 * can call them. */
static int  l005a(void);
static void jt42(const char *msg);

/* L01be (CODE 15 + 0x1be) — build the in-memory saved-character roster.
 *
 * Enumerates the saved-character files (JT[990] open, JT[991] per-entry) and,
 * for each, allocates a 40-byte node from the g_a5_21156 pool (JT[477]),
 * clears it (JT[399]) and copies the character's display name into the node's
 * name slot at +5 (JT[384]); .next is at offset 0.  TWO parallel lists are
 * built (out1 / out2 — a peer list and the display-name list); jt589 hands
 * both back, l15e2 / l4f2c walk out2 and free both via JT[471].  The Mac's
 * opaque two-cursor pool relink (asm 0x2ae..0x2d8 — clears nodes *after*
 * linking) is reimplemented as a plain tail-append: same singly-linked result.
 *
 * Port reconciliation: the Mac's saved characters are name-as-filename files
 * in the design SAVE folder, so it copies the filename to the node.  The port
 * stores them as slot-named CHAR*.CHR (GEMDOS 8.3), so we scan that pattern
 * and read the real display name from each record (name@96) instead — folding
 * in the record read the Mac defers to its save-mode L0006/jt576 arm. */
static void l01be(const char *suffix, long *out1, long *out2)
{
	signed char    isdir;                          /* jt991 is-dir out      */
	long           entry;                          /* current file name     */
	long           node1 = 0, node2 = 0;           /* freshly-alloc'd nodes  */
	long           tail1 = 0, tail2 = 0;           /* append cursors         */

	PROBE("L01be");
	(void)suffix;
	if (out2 != NULL) *out2 = 0;
	if (out1 != NULL) *out1 = 0;

	jt990(0, "CHAR*.CHR", NULL, 1, 0);             /* scan saved-char files */
	entry = jt991(&isdir);                         /* first entry           */
	if (entry == 0)
		return;                                /* no saved characters   */

	for (;;) {
		char          pfn[20];
		unsigned char rec[128];
		const char   *cfn;
		short         refnum, len;
		long          n;

		jt477((void *)(uintptr_t)g_a5_21156, 40, &node1); /* peer node   */
		jt477((void *)(uintptr_t)g_a5_21156, 40, &node2); /* name node   */
		if (node2 == 0) {                      /* pool exhausted        */
			if (out1 != NULL) *out1 = 0;
			if (out2 != NULL) *out2 = 0;
			return;
		}
		jt399((void *)(uintptr_t)node1, 40, 0);          /* clear (.next=0) */
		jt399((void *)(uintptr_t)node2, 40, 0);

		/* Pull the display name from the record (name@96, a C string). */
		cfn = (const char *)(uintptr_t)entry;
		for (len = 0; cfn[len] != 0 && len < 16; len++)
			pfn[len + 1] = cfn[len];
		pfn[0] = (char)len;
		if (FSOpen((ConstStr255Param)pfn, 0, &refnum) == noErr) {
			n = (long)sizeof rec;
			if (FSRead(refnum, &n, rec) == noErr && n > 96) {
				rec[sizeof rec - 1] = 0;
				jt384((char *)(uintptr_t)(node2 + 5),
				      (const char *)&rec[96]);
			}
			(void)FSClose(refnum);
		}
		g_a5_long(-6922) = node1 + 5;                    /* name-field cache */

		/* Tail-append onto both lists (.next at offset 0). */
		if (tail1 != 0) *(long *)(uintptr_t)tail1 = node1;
		else if (out1 != NULL) *out1 = node1;
		tail1 = node1;
		if (tail2 != 0) *(long *)(uintptr_t)tail2 = node2;
		else if (out2 != NULL) *out2 = node2;
		tail2 = node2;

		entry = jt991(&isdir);                 /* next entry            */
		if (entry == 0)
			break;
	}
}

/* jt589 (CODE 15 + 0x362) — roster-list builder entry.  Checks the save
 * folder is reachable (L005a); if not, hands back two empty lists.  Otherwise,
 * in save (delete) mode (the JT[3] switch on g_a5_22733, case 1) builds the
 * roster node lists via L01be("cch").  Finally, if the primary list is empty,
 * posts the "No characters to load/delete." notice (JT[42]); flag selects the
 * verb.  L01be now enumerates the port's CHAR*.CHR saved characters, so the
 * lists populate with real names; the notice fires only when none are saved. */
static void jt589(short flag, long *tail, long *head)
{
	PROBE("jt589");
	if (!l005a()) {
		if (head != NULL) *head = 0;
		if (tail != NULL) *tail = 0;
		return;
	}
	switch (g_a5_22733) {
	case 1:
		l01be(ua_strs_at(0x4c44), tail, head); /* "cch"                */
		break;
	default:
		break;
	}
	if (tail != NULL && *tail == 0)
		jt42(flag ? ua_strs_at(0x4c48)         /* "No ... to delete."  */
		          : ua_strs_at(0x4c62));       /* "No ... to load."    */
}
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

			/* Port: delete the saved character.  The Mac looks
			 * the entry up (jt165), builds its SAVE path and
			 * unlinks the file (jt988).  The port keeps characters
			 * as CHAR*.CHR mirroring cg_pool, so drop the matching
			 * pool slot and let save_roster rewrite the files (it
			 * deletes the now-stale top slot), then relink. */
			{
				const char *nm = (const char *)&e[5 + marker];
				short si, sj;
				for (si = 0; si < cg_pool_count; si++)
					if (jt396((const char *)&cg_pool[si][96],
					          nm) != 0) {
						for (sj = si;
						     sj < cg_pool_count - 1; sj++)
							memcpy(cg_pool[sj],
							       cg_pool[sj + 1],
							       512);
						cg_pool_count--;
						break;
					}
				save_roster();
				cg_party_relink();
			}

			/* Unlink the picked node from the list so the
			 * refreshed picker drops it, and reclaim it. */
			if (head == entry) {
				head = *(long *)(uintptr_t)entry;
			} else {
				long pr = head;
				while (pr != 0 &&
				       *(long *)(uintptr_t)pr != entry)
					pr = *(long *)(uintptr_t)pr;
				if (pr != 0)
					*(long *)(uintptr_t)pr =
						*(long *)(uintptr_t)entry;
			}
			jt471(entry, 40, (void *)(uintptr_t)g_a5_21156);
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

			/* Port: add the picked saved character to the active
			 * party.  The Mac copies the saved record into a fresh
			 * 398-byte party-pool slot (jt477 + jt165 + jt587); the
			 * port's party is the CHAR_INPARTY subset of cg_pool, so
			 * flag the matching slot and relink.  Matched on the raw
			 * name (before the "* " marker is prefixed below). */
			{
				short pi;
				for (pi = 0; pi < cg_pool_count; pi++)
					if (jt396((const char *)&cg_pool[pi][96],
					          (const char *)&e[5]) != 0) {
						cg_pool[pi][CHAR_INPARTY] = 1;
						break;
					}
				cg_party_relink();
				save_roster();
			}

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
static short jt182(const char *p1, long p2, short arg3, short arg4);
/* jt159 (CODE 7 + 0x16ea) — a yes/no confirmation prompt; returns 1 to
 * confirm, 0 to cancel.  Renders `prompt` through the faithful JT[182] alert
 * (layout L206e / modal loop L23b4 / button map L25b6) and maps the picked
 * button: first button (Yes) => confirm, second (No) => cancel.  The Mac reads
 * the buttons from g_a5_-13852, but that slot holds a filename template in the
 * port's A5 image (the init that re-points it to the confirm buttons isn't
 * lifted), so pass the "Yes No" labels explicitly. */
static int    jt159(const char *prompt, short b)
{
	short r;

	PROBE("jt159");
	(void)b;
	jt179(1);
	r = jt182(prompt, (long)(uintptr_t)"Yes No", 1, g_a5_22281);
	return (r == 0) ? 1 : 0;
}
/* JT[1173] (CODE 4 + 0x164c) — set the engine clip rect. Lifted: each arg is
 * an 8000-space coord run through L77fe (== jt1135: (v>6000)?(v-8000)*scale:v,
 * scale = (g_a5_-2347==0)?3:2) into the clip globals, then clamped to the
 * screen and zeroed if degenerate. Arg order is (top, left, bottom, right) —
 * the old stub's name had bottom/right swapped, but the call sites already
 * pass the values positionally, so they're unchanged.
 *
 *   g_a5_-3054 = top    g_a5_-3056 = left
 *   g_a5_-3050 = bottom g_a5_-3052 = right     (L04cc = height, L04de = width) */
static void jt1173(short top, short left, short bottom, short right)
{
	PROBE("jt1173");
	jt1135(top, left, &g_a5_3054, &g_a5_3056);
	jt1135(bottom, right, &g_a5_3050, &g_a5_3052);
	if (g_a5_3054 < 0)
		g_a5_3054 = 0;
	if (g_a5_3056 < 0)
		g_a5_3056 = 0;
	if (l04cc() < g_a5_3050)
		g_a5_3050 = l04cc();
	if (l04de() < g_a5_3052)
		g_a5_3052 = l04de();
	if (g_a5_3054 >= g_a5_3050 || g_a5_3056 >= g_a5_3052) {
		g_a5_3050 = 0;
		g_a5_3052 = 0;
		g_a5_3054 = 0;
		g_a5_3056 = 0;
	}
}
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

/* L005a (CODE 15 + 0x5a) — "is the save medium reachable?" precondition for
 * the roster builders.  The Mac builds the design's SAVE path and opens a scan
 * over it (JT[990]); on failure it prompts "Please insert save disk."
 * (JT[182], Ok/Exit) and retries, returning 0 if the user gives up.
 *
 * Port reconciliation: the Atari's saved characters live on the always-present
 * hard disk (CHAR*.CHR in the working dir), so the medium is always reachable
 * and the removable-disk prompt is moot — return 1.  Whether any characters
 * actually exist is then up to L01be's scan (jt589 posts "No characters ..."
 * when the resulting list is empty). */
static int l005a(void)
{
	PROBE("L005a");
	return 1;
}

/* New PROBE-stub helpers jt585 calls. */
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
 *   short lower = JT[CHAR_AC](first);       ; tolower
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

		/* jt452 is the full Mac stream parser (verified vs CODE 3 +
		 * 0x29a0): cmd 1 alloc (rec[16]=shape, rec[18]=item_arg,
		 * rec[12]=str), then 36=rec[24] size, 32/33=rec[29]/[30]
		 * shortcut upper/lower, 20/21=set rec[28] bits 4/5. */
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

		/* Faithful arg order (CODE 7 + 0x1ce4): jt452(7, cb, 20, 0) — cmd 7
		 * stores the action proc in rec[4], THEN cmd 20 sets rec[28] bit 4.
		 * The lift had (7, 20, cb, 0): rec[4]=20 and cb mis-parsed as a
		 * command -> a malformed type-7 DLItem at (0,0) that rendered as the
		 * top stripes over the roster. */
		jt452((long)7, (long)(uintptr_t)cb, (long)20, (long)0);

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

static void  l2170(short arg);          /* defined below (CODE 7+0x2170) */
static void  l2858(short mode);         /* defined below (CODE 7+0x2858) */

/* JT[455] (CODE 3 + 0x2c5a) — return the live DLItem count (g_a5_-9250). */
static short jt455(void)
{
	PROBE("jt455");
	return (short)g_a5_word(-9250);
}

/* jt148 (CODE 7 + 0x33dc) — the play-screen command/prompt bar paint, the
 * piece jt240/jt241 call to lay the bottom bar. Two modes, on JT[396](title,
 * g_a5_-14644) — the engine's "is this the standard play prompt" compare:
 *
 *   nonzero (a one-off "press a key" prompt): draw "Press ... to continue."
 *     (jt94) + a single "Return" shape-1 button DLItem (jt452), arm the modal
 *     (l2062 / l2858(2)), clear the cancel byte (-24139).
 *
 *   zero (the standard command bar): the prompt-cluster build, mirroring
 *     l206e but with the bar's own STRS suffixes and an unconditional dirty:
 *     L2184(title) fills the working prompt (-13000); copy it to the L1a0c
 *     buffer (-12908) and stage the two command suffixes (STRS 0x27ae/0x27b0)
 *     into -12828/-12748; L1a0c measures the glyph layout into buf; L2170
 *     caches the base index (g_a5_-13016); L1bfe paints the bar from buf with
 *     the caller's prompt as the suffix; then roll the dirty cache. The bar
 *     now renders through the real DLItem/GLIB glyph path (l1bfe -> the glyph
 *     blitter) instead of the old stub, so the in-dungeon command bar draws
 *     with the same foundation as the menus. */
static void jt148(long prompt, char *title, short flag)
{
	unsigned char buf[82];          /* fp@(-82) — the L1a0c glyph layout */
	short         width;            /* fp@(-2)  */

	PROBE("jt148");
	g_a5_word(-12666) = jt455();

	if (jt396(title, (const char *)(uintptr_t)g_a5_long(-14644)) != 0) {
		/* one-off "press a key" prompt + a Return button */
		jt94((short)7, (short)24, (short)7, (short)0, "%s",
		     ua_strs_at(0x278c));               /* "Press ... to continue." */
		jt452((long)1, (long)8094, (long)8056,
		      (long)(uintptr_t)ua_strs_at(0x27a6),      /* "Return" */
		      (long)32, (long)6, (long)36, (long)20, (long)22, (long)21, (long)0);
		l2062();
		l2858((short)2);
		g_a5_byte(-24139) = 0;
		return;
	}

	/* the standard command bar (prompt cluster) */
	l2184(title);
	g_a5_12912 = 1;                                 /* force a repaint */
	jt384((char *)g_a5_12908_str, (const char *)g_a5_13000_str);
	jt384((char *)g_a5_12828_str, ua_strs_at(0x27ae));
	jt384((char *)g_a5_12748_str, ua_strs_at(0x27b0));
	width = l1a0c((const char *)g_a5_12908_str, buf);
	l2170(width);                                   /* g_a5_-13016 = base */
	l1bfe(width, buf, (const char *)(uintptr_t)prompt,
	      (short)(flag & 0xff), (short)g_a5_12912);
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

		/* jt1085 (rc) != 0 only SKIPS the per-iteration timer/animation
		 * block (asm L23b4 0x245e: bne L259a) — it is NOT a loop exit. The
		 * loop exits at L259a/259e when l2d3e returns a selection
		 * (item >= 0), or via the mode-2/7/12/13 timeout below. The earlier
		 * `if (rc != 0) break` was wrong (it would return item=-1 on any
		 * pending event once jt1085 is lifted); harmless while jt1085 stubs
		 * to 0, fixed here for faithfulness. */
		if (rc == 0 && mode_with_timer) {
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

/* L217e / L2170 (CODE 7 + 0x217e / +0x2188) — getter/setter for the alert's
 * button-base index in g_a5_-13016 (the word L206e stamps during layout).
 * L25b6 maps the picked DLItem to a button via (arg_count - L217e()). */
static short l217e(void)                             { PROBE("L217e");
                                                       return g_a5_13016; }
static void  l2170(short arg)                        { PROBE("L2170");
                                                       g_a5_13016 = arg; }
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

/* L2858 (CODE 7 + 0x2858) — stash the picker "mode" word that the bar's
 * party-row layout (L1f3e) branches on. jt164 sets mode 1. */
static void l2858(short mode)
{
	g_a5_13018 = mode;
}

/* L1f3e (CODE 7 + 0x1f3e) — size the button bar to the active party. Walks
 * the g_a5_-27928 party list (.next @ +0, capped at 9) to count members and
 * caches the count in g_a5_-19176 / g_a5_-12910. When the prompt changed it
 * also installs one shape-5 DLItem spanning the roster row (proc JT[140] /
 * JT[156] per the L2858 mode) so a click on the party area registers, and on
 * a count change it refreshes via JT[444]. That roster-row target is cosmetic
 * for command selection (the command buttons come from L206e), so the DLItem
 * install is deferred here — the faithful count walk + cache update run so the
 * picker's refresh bookkeeping stays correct. */
static void l1f3e(short a8, short a10)
{
	long  node = g_a5_long(-27928);
	short n    = 0;

	PROBE("L1f3e");
	(void)a8; (void)a10;
	while (node != 0 && n < 9) {
		node = *(long *)(uintptr_t)node;       /* follow .next (offset 0) */
		n++;
	}
	g_a5_19176 = n;
	/* TODO: install the roster-row DLItem (jt452 shape 5 + JT[156]/JT[140])
	 * / JT[444] refresh, per CODE 7 + 0x1f6e..0x2054. */
	g_a5_12910 = g_a5_19176;
}

/* jt171 (CODE 7 + 0x30aa) — the direction-bar picker. The play "Move"
 * sub-mode (jt953 state 3) runs through here: it is jt164's sibling, but
 * instead of a horizontal word bar it lays a 3x3 directional PAD of nine
 * shape-5 hit regions (the 8 compass cells + a centre), polls via L23b4
 * (mode 4, set by L2858), and maps the pick via L25b6 — whose mode-4 arm
 * returns 0x81..0x88 (129..136), the absolute facing the caller writes to
 * g_a5_-12286. Faithful transcription of CODE 7 + 0x30aa..0x336e: same
 * L2062 / L2858 / L206e / L23b4 / L25b6 spine as jt164; the three JT[452]
 * streams below are the pad geometry (rec16/18 = top/left, rec22/24 = h/w,
 * cmd 41 = rec20 key code, cmd 20 = set rec[28] bit 4), positions computed
 * from the base (x,y) the caller passes (player-handle bytes 38/37), each
 * pre-scaled x4 like the asm. Args mirror the asm: (prompt, cmdstring, a3,
 * a4, x, y). */
static short jt171(long prompt, long cmdstring, short a3, short a4,
                   short ax, short ay) __attribute__((unused));
static short jt171(long prompt, long cmdstring, short a3, short a4,
                   short ax, short ay)
{
	unsigned char buf[80];
	unsigned char a3_lo = (unsigned char)(a3 & 0xff);
	short         x, y, tmp;

	PROBE("jt171");
	l2062();
	x = (short)(((unsigned)(unsigned char)ax << 2) & 0xff);   /* fp@21 x4 */
	y = (short)(((unsigned)(unsigned char)ay << 2) & 0xff);   /* fp@23 x4 */
	l2858((short)4);
	l206e(cmdstring, buf, (const char *)(uintptr_t)prompt, &a3_lo);
	/* (L2170(width) is redundant — l206e already set g_a5_-13016.) */

	if (g_a5_12911 != 0) {
		/* Stream 1 (asm 0x3154-0x31ea): the N/E/S/W cells (key codes
		 * 9/11/5/7). cmd 5 = {rec16=top, rec18=left, rec22=h, rec24=w}. */
		jt452((long)5, (long)(x + 8002), (long)7999, (long)6, (long)(y + 3),
		      (long)41, (long)9,  (long)20,
		      (long)5, (long)7999, (long)(y + 8002), (long)(x + 3), (long)6,
		      (long)41, (long)11, (long)20,
		      (long)5, (long)(x + 8002), (long)(y + 8008), (long)6, (long)(255 - y),
		      (long)41, (long)5,  (long)20,
		      (long)5, (long)(x + 8008), (long)(y + 8002), (long)(255 - x), (long)6,
		      (long)41, (long)7,  (long)20,
		      (long)0);
		/* Stream 2 (asm 0x31fa-0x3300): the four diagonal cells
		 * (key codes 10/4/6/8). */
		jt452((long)5, (long)7999, (long)7999, (long)(x + 3), (long)(y + 3),
		      (long)41, (long)10, (long)20,
		      (long)5, (long)7999, (long)(y + 8008), (long)(x + 3), (long)(255 - y),
		      (long)41, (long)4,  (long)20,
		      (long)5, (long)(x + 8008), (long)(y + 8008), (long)(255 - x), (long)(255 - y),
		      (long)41, (long)6,  (long)20,
		      (long)5, (long)(x + 8008), (long)7999, (long)(255 - x), (long)(y + 3),
		      (long)41, (long)8,  (long)20,
		      (long)0);
		/* Stream 3 (asm 0x3304-0x3338): the centre cell (key code 3). */
		jt452((long)5, (long)(x + 8002), (long)(y + 8002), (long)6, (long)6,
		      (long)41, (long)3, (long)0);
	}

	tmp = l23b4((short)(signed char)(a4 & 0xff));
	return l25b6(tmp, buf, &g_a5_24139);
}

/* jt164 (CODE 7 + 0x2fa4) — the horizontal button-bar picker. The play
 * command bar runs through here: L206e lays a space-delimited button string
 * (e.g. "Move Area Cast View Encamp Search Look Inv", the A5-13764 global)
 * into DLItems, the JT[452] stream paints the bar's four bevel-frame items,
 * L1f3e sizes it to the party, then L23b4 spins the modal loop and L25b6 maps
 * the pick to a 0-based button index. Sibling of jt182 (the alert); shares
 * L206e / L23b4 / L25b6. Args mirror jt182: prompt, button string, the
 * default-item byte (arg3), and the modal-loop seed (arg4). */
static short jt164(long prompt, long cmdstring, short arg3, short arg4)
{
	unsigned char buf[80];
	unsigned char arg3_lo = (unsigned char)(arg3 & 0xff);
	short         tmp;

	PROBE("jt164");
	g_a5_19172 = 8016;
	g_a5_19174 = 8068;
	l2858((short)1);
	l206e(cmdstring, buf, (const char *)(uintptr_t)prompt, &arg3_lo);
	l1f3e((short)g_a5_19172, (short)g_a5_19174);
	if (g_a5_12911 != 0) {
		/* The bar's four bevel-frame DLItems — shape 5 (rec[16]=y,
		 * rec[18]=x, rec[22], rec[24]) + cmd 41 setter + cmd 20 set-bit,
		 * faithful to CODE 7 + 0x2ff4..0x3076 (read in JT[452] order). */
		jt452((long)5, (long)8000, (long)8000, (long)50, (long)20,
		      (long)41, (long)22, (long)20,
		      (long)5, (long)8000, (long)8020, (long)50, (long)28,
		      (long)41, (long)11, (long)20,
		      (long)5, (long)8000, (long)8048, (long)50, (long)20,
		      (long)41, (long)21, (long)20,
		      (long)5, (long)8050, (long)8000, (long)30, (long)68,
		      (long)41, (long)23, (long)20,
		      (long)0);
	}
	tmp = l23b4((short)(signed char)(arg4 & 0xff));
	return l25b6(tmp, buf, &g_a5_24139);
}

/* JT[152] (CODE 7 + 0x3370) — classify a poll result as a command-bar
 * command or "not a command". L63c0's input loop runs procres = jt152(pollres)
 * and, if procres >= 0, treats it as a command (and ends the walk loop); if
 * procres < 0 it dispatches pollres as movement / keyboard / select.
 *
 * It copies the live command string (g_a5_-13000) into the scratch buffer
 * (g_a5_-12908) via JT[384], splits it into words (L1a0c -> count), and maps
 * a poll index that falls inside [g_a5_-12666, g_a5_-12666 + count] to a
 * command code via L25b6 (against the accelerator table at g_a5_-24139). An
 * empty bar returns 13 (the implicit "Move"); an index outside the bar's
 * range returns -1, which is exactly what routes the directional / keyboard
 * sources on to L63c0's movement switch. */
static short jt152(short sel)
{
	char *split[20];                /* fp@(-82): L1a0c word-offset scratch */
	short count;

	PROBE("jt152");
	jt384((char *)g_a5_buf(-12908), (const char *)g_a5_buf(-13000));
	count = l1a0c((const char *)g_a5_buf(-12908), split);
	if (count == 0)
		return 13;
	if (sel < g_a5_word(-12666))
		return (short)-1;
	if ((short)(g_a5_word(-12666) + count) < sel)
		return (short)-1;
	return (short)(l25b6((short)(sel - g_a5_word(-12666)),
	                     (unsigned char *)split,
	                     &g_a5_byte(-24139)) & 0xff);
}

/* Forward decls for jt953's arms defined later in this file. */
static void jt23(void);
static void jt904(unsigned char *out_done);

/* jt953 (JT[953] = CODE 21 + 0x4038) — the exploration command processor.
 * Structural skeleton (lift level 2): the CFG + the loop + every *available*
 * JT call are faithful; arms whose action helper isn't lifted yet are TODO.
 *
 * The play selector g_a5_-27990 (JT[3] switch) picks the mode: 4 = the
 * standing command bar, 3 = the "Move" direction sub-mode.
 *
 * State 4 — the command-bar loop: each pass runs JT[155] x7 (the per-slot
 * input / roster-status refresh), shows the jt164 button bar over the
 * A5-13764 command string ("Move Area Cast View Encamp Search Look Inv"),
 * then dispatches the picked index 0..7:
 *   0 Move | 1 Area | 2 Cast | 3 View | 4 Encamp (leave) | 5 Search |
 *   6 Look | 7 Inv. It repeats until Encamp, or g_a5_-24139 flags a cancel.
 *   The Area / Cast / Search / Look / Inv / Move-step actions reach
 *   still-stubbed JT entries (JT[221] area, L06d6 cast, JT[201/202] step,
 *   JT[914/947], L3b80) and are deferred here.
 * State 3 — Move sub-mode: JT[171] is the direction-bar variant (result
 *   129..136 -> facing g_a5_-12286); jt171 isn't lifted, so deferred whole.
 *
 * On the way out (L44f6) it stands up the play frame once via jt103. Returns
 * the last picked command byte. */
static short jt953(void)
{
	unsigned char *pl = (unsigned char *)(uintptr_t)g_a5_28006;
	unsigned char  saved6;
	unsigned char  exit_flag = 0;
	short          cmd = -1;
	short          i;
	short          local4;
	short          guard;

	PROBE("jt953");
	if (pl != NULL)
		pl[3] = 0;
	saved6 = g_a5_24140;

	switch (g_a5_27990) {
	case 4:
		/* L4068 — the command-bar loop. The faithful loop is bounded only
		 * by the exit command; jt164's modal (L23b4) blocks per pass, so
		 * this isn't a busy-spin. A guard caps it defensively (matching
		 * L23b4's own guard) in case it runs without a live event source. */
		for (guard = 0; guard < 256 && exit_flag == 0; guard++) {
			local4 = 0;
			for (i = 1; i <= 7; i++)
				jt155(i, &local4);
			g_a5_24140 = saved6;
			cmd = jt164(g_a5_long(-13952), g_a5_long(-13764),
			            (short)1, (short)0);
			saved6 = g_a5_24140;
			g_a5_22268 = 1;
			if (g_a5_24139 != 0) {          /* L424e — cancel/commit */
				jt938();
				exit_flag = 1;
				cmd = -1;
				continue;
			}
			switch (cmd) {                  /* JT[3] min 0 max 7 */
			case 0:                         /* Move */
				g_a5_22268 = 1;
				break;
			case 1:                         /* Area — TODO: JT[221]/JT[101] */
				break;
			case 2:                         /* Cast — TODO: L06d6 */
				break;
			case 3: {                       /* View character */
				unsigned char done = 0;
				jt904(&done);
				break;
			}
			case 4:                         /* Encamp -> leave the loop */
				exit_flag = 1;
				break;
			case 5:                         /* Search — TODO: JT[914/201/947] */
				jt938();
				break;
			case 6:                         /* Look — TODO */
				jt938();
				break;
			case 7:                         /* Inventory — TODO: L3b80 */
				jt23();
				break;
			default:                        /* L421e — TODO: JT[936/934] */
				break;
			}
		}
		break;

	case 3:
		/* L43ce — the Move sub-mode: loop the direction pad (jt171) and set
		 * the absolute facing g_a5_-12286 on a committed pick (129..136 ->
		 * facing 1..8); a commit or cmd 4 (back) ends the sub-mode. Faithful
		 * to CODE 21 + 0x43ce..0x44f4. (A guard caps the no-input spin, like
		 * the state-4 loop; the asm relies on jt171 returning a commit.) */
		g_a5_22268 = 1;
		if (pl != NULL && pl[36] != 0) {
			short done = 0;
			for (guard = 0; guard < 256 && !done; guard++) {
				jt399(g_a5_buf(-24126), (short)40, (short)0xFF);
				local4 = 0;
				jt399(g_a5_buf(-24126), (short)40, (short)0xFF);
				jt155((short)4, &local4);
				g_a5_24140 = saved6;
				cmd = jt171(g_a5_long(-13952), g_a5_long(-13764),
				            (short)1, (short)0,
				            (short)pl[38], (short)pl[37]);
				saved6 = g_a5_24140;
				if (g_a5_24139 != 0) {        /* L4486 — committed pick */
					done = 1;
					if (cmd >= 129 && cmd <= 136)
						g_a5_12286 = (unsigned char)(cmd - 128);
					else
						done = 0;             /* L44ea — no facing */
				} else if (cmd == 4) {        /* L4474 — back / exit */
					done = 1;
				}
			}
		}
		break;

	default:
		break;
	}

	/* L44f6 — stand up the play frame the first time through (jt103). */
	if (g_a5_byte(-22278) == 0) {
		jt103((short)1, (short)17, (short)38, (short)22);
		g_a5_byte(-22278) = 1;
	}
	return cmd;
}

static short  jt595(short a, short b, short *p1, unsigned char *p2)
                                                     { PROBE("jt595"); (void)a; (void)b; (void)p1; (void)p2; return 0; }
static void   jt527(void)                            { PROBE("jt527"); }
static void   jt23(void)                             { PROBE("jt23"); }

#define g_a5_5806  g_a5_long(-5806)    /* per-character record ptr (NULL until design-load) */
#define g_a5_27936 g_a5_long(-27936)   /* saved design ptr cache */
#define g_a5_13804 g_a5_long(-13804)   /* roster cluster arg for jt182 */

static void jt904(unsigned char *out_done) __attribute__((unused));
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
	if (g_a5_14439 != 0) {
		l15e2();                     /* faithful skeleton (trace) */
		cg_modify_sheet();           /* port: rename / re-roll    */
	}
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
	jt560();                             /* faithful skeleton (trace) */
	cg_delete_character();               /* port: erase from the pool */
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

	cg_remove_from_party();              /* port: bench (back to pool) */

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
	(void)local_byte;
	PROBE("jt918/case5 L1036");
	if (g_a5_14435 == 0)
		return 0;
	/* Faithful Add is jt904 -> the jt182 Add/Modify/Delete popup (a
	 * blocking l2d3e pump, only partially lifted); the port screen stands
	 * in for it, so jt904 isn't called here. It stays the deferred
	 * remainder. */
	cg_add_character();                  /* port: pool -> active party */
	return 0;
}

/* ---- Character-sheet helpers (shared by View / Modify / Delete) ------
 *
 * The faithful paths (l12a0 / l15e2 / jt560 -> the jt568 selection
 * machine) are the deferred remainder; these port screens walk the
 * roster list off g_a5_-27928 directly. Stats / alignment come from the
 * port-local record offsets (CHAR_STATS / CHAR_ALIGN) that
 * cg_build_record + the test-party seed write (see the CHAR_* macros
 * above l02dc). */

/* Collect the active party (linked list, .next at +0) into `out`; the cap
 * is `max`. Returns the entry count. */
static short cg_collect_party(unsigned char **out, short max)
{
	unsigned char *e;
	short          n = 0;

	for (e = (unsigned char *)(uintptr_t)g_a5_long(-27928);
	     e != NULL && n < max;
	     e = *(unsigned char **)e)
		out[n++] = e;
	return n;
}

/* Collect every pool character into `out` (Delete picker). */
static short cg_collect_pool(unsigned char **out, short max)
{
	short i, n = 0;
	for (i = 0; i < cg_pool_count && n < max; i++)
		out[n++] = cg_pool[i];
	return n;
}

/* Collect benched pool characters (CHAR_INPARTY == 0) into `out` — the
 * Add picker's candidates. */
static short cg_collect_addable(unsigned char **out, short max)
{
	short i, n = 0;
	for (i = 0; i < cg_pool_count && n < max; i++)
		if (cg_pool[i][CHAR_INPARTY] == 0)
			out[n++] = cg_pool[i];
	return n;
}

/* A one/two-line notice on the shared chrome; waits for any key. */
static void cg_message(const char *l1, const char *l2)
{
	unsigned char *px; short pitch, sw, sh, yy;
	unsigned char  scan = 0, ascii = 0;

	while (plat_kb_poll(&scan, &ascii))
		;
	if (qd_screen_pixels(&px, &pitch, &sw, &sh) && px) {
		if (g_menu_state == 1) {
			fill_backdrop(px, pitch, 0, 0,
			              (short)(sw - 1), (short)(sh - 1));
			draw_plate(px, pitch, sw, sh, 6, 60, 313, 130, 1);
		} else {
			for (yy = 0; yy < sh; yy++)
				memset(px + (long)yy * pitch, 0x08, (size_t)sw);
		}
	}
	jt94((short)6, (short)9,  15, 0, "%s", l1);
	if (l2)
		jt94((short)6, (short)11, 7, 0, "%s", l2);
	qd_present();
	while (!plat_kb_poll(&scan, &ascii))
		;
}

/* Draw one character's full sheet (name, race/class/level, alignment, the
 * six ability scores, HP, AC) on the shared stone chrome with `footer` as
 * the bottom hint line, then present. */
/* JT[892] (CODE 19 + 0x1abe) — the combatant record-sheet stats block:
 * Hit Points / Armor Class / Encumbrance / THAC0 / Movement. Structural
 * lift of the CODE 19 painter: faithful label set + screen coords, the
 * JT[1200] label-colour split (grey 7 in combat / cyan 11 on the sheet),
 * the AC-shade classify over rec[385], and THAC0 = 60 - rec[384]. The
 * value funnels JT[32]/JT[34] (and the unlifted JT[59]/JT[63]/JT[37]) are
 * PROBE stubs, so the numbers are drawn via JT[94] off the faithful CHAR_*
 * offsets too (the port-addition pattern l02dc uses). The faithful code
 * reads g_a5_-27932; the port passes the viewed record. STRS label offsets
 * (non-encounter copies): HP 0x599e, AC 0x59b6, Enc 0x59ce, THAC0 0x59e2,
 * Move 0x59f4. */
static void jt892(const unsigned char *rec)
{
	long  e   = (long)(uintptr_t)rec;
	short lc  = (jt1200() == 3) ? (short)7 : (short)11;   /* label colour */
	short ac  = rec[CHAR_AC];
	short enc = (short)(((unsigned)rec[86] << 8) | rec[87]);
	short shade;

	/* Hit Points */
	jt94((short)20, (short)3, lc, 0, "Hit Points");
	jt103((short)31, (short)3, (short)37, (short)3);
	jt32(e, (short)31, (short)3, 0, 1);
	jt94((short)31, (short)3, 7, 0, "%d", (int)rec[CHAR_HP]);

	/* Armor Class — value shaded by the faithful classify of +385 */
	jt94((short)1, (short)17, lc, 0, "Armor Class");
	if      (ac < 1 || ac > 69) shade = 15;
	else if (ac <= 50)          shade = 16;
	else if (ac <= 60)          shade = 17;
	else                        shade = 16;
	jt34(e, shade, (short)17, 0);
	jt94((short)17, (short)17, 7, 0, "%d", (int)ac);

	/* Encumbrance (word @ +86) */
	jt94((short)20, (short)17, lc, 0, "Encumbrance");
	jt94((short)33, (short)17, 7, 0, "%d", (int)enc);

	/* THAC0 = 60 - rec[384] */
	jt94((short)1, (short)18, lc, 0, "THAC0.");
	jt94((short)16, (short)18, 7, 0, "%d", (int)(60 - rec[CHAR_THAC0]));

	/* Movement */
	jt94((short)20, (short)18, lc, 0, "Movement");
	jt94((short)33, (short)18, 7, 0, "%d", (int)rec[CHAR_MOVE]);
}

static void cg_draw_sheet(const unsigned char *c, const char *footer)
{
	unsigned char *px; short pitch, sw, sh, yy, i;
	short race  = c[CHAR_RACE], klass = c[CHAR_CLASS];
	short lvl   = c[CHAR_LEVEL] ? c[CHAR_LEVEL] : 1;
	short align = c[CHAR_ALIGN];

	if (qd_screen_pixels(&px, &pitch, &sw, &sh) && px) {
		if (g_menu_state == 1) {
			fill_backdrop(px, pitch, 0, 0,
			              (short)(sw - 1), (short)(sh - 1));
			draw_plate(px, pitch, sw, sh, 6, 8, 313, 160, 1);
		} else {
			for (yy = 0; yy < sh; yy++)
				memset(px + (long)yy * pitch, 0x08,
				       (size_t)sw);
		}
	}

	jt94((short)3,  (short)2, 14, 0, "%s",
	     (const char *)&c[96]);              /* name, gold */

	jt94((short)3,  (short)4,  7, 0, "Race:");
	jt94((short)10, (short)4, 15, 0, "%s",
	     (race  < 6) ? k_roster_races[race]    : "?");
	jt94((short)24, (short)4,  7, 0, "Class:");
	jt94((short)31, (short)4, 15, 0, "%s",
	     (klass < 6) ? k_roster_classes[klass] : "?");

	jt94((short)3,  (short)5,  7, 0, "Level:");
	jt94((short)10, (short)5, 15, 0, "%d", (int)lvl);
	/* Alignment gets its own full-width row — "Lawful Good" etc is too
	 * long to share row 5 without running off the plate. */
	jt94((short)3,  (short)6,  7, 0, "Align:");
	jt94((short)10, (short)6, 15, 0, "%s",
	     (align < CG_NALIGNS) ? cg_aligns[align] : "?");

	jt94((short)3,  (short)8, 11, 0, "Ability Scores");
	for (i = 0; i < 6; i++) {
		short col = (i < 3) ? (short)3 : (short)20;
		short rw  = (short)(9 + (i % 3));
		jt94(col,              rw,  7, 0, "%s", cg_stat_names[i]);
		jt94((short)(col + 6), rw, 15, 0, "%d",
		     (int)c[CHAR_STATS + i]);
	}

	/* Faithful combat-stats block (HP / AC / Encumbrance / THAC0 / Move). */
	jt892(c);

	if (footer)
		jt94((short)3, (short)14, 7, 0, "%s", footer);

	qd_present();
}

/* View Character — read-only sheet; Up/Down page the party, Esc/Return
 * back out to the Training Hall. */
static void cg_view_sheet(void)
{
	unsigned char *party[16];
	short          nparty, sel = 0;
	unsigned char  scan = 0, ascii = 0;

	nparty = cg_collect_party(party, 16);
	if (nparty == 0)
		return;

	g_a5_2347 = 1;                       /* ×2 scale, matching the roster */
	load_menu_ui();
	while (plat_kb_poll(&scan, &ascii))  /* drain the triggering key */
		;

	for (;;) {
		char foot[64];
		if (nparty > 1)
			sprintf(foot, "%d of %d   Up/Down: next   Esc: back",
			        (int)(sel + 1), (int)nparty);
		else
			sprintf(foot, "Esc: back");
		cg_draw_sheet(party[sel], foot);

		while (!plat_kb_poll(&scan, &ascii))
			;
		if (ascii == 27 || ascii == 13 || ascii == 3)
			break;                       /* Esc / Return -> back */
		if (scan == 0x48)                    /* Up   */
			sel = (short)((sel + nparty - 1) % nparty);
		else if (scan == 0x50)               /* Down */
			sel = (short)((sel + 1) % nparty);
	}
}

/* Inline name editor for Modify — types into the record's name (+96)
 * live (the gold sheet name updates as you go); Return commits + persists,
 * Esc restores the original. */
static void cg_rename(unsigned char *c)
{
	unsigned char orig[16];
	short         len, k;
	unsigned char scan = 0, ascii = 0;

	for (k = 0; k < 16; k++)
		orig[k] = c[96 + k];
	for (len = 0; len < 15 && c[96 + len] != 0; len++)
		;
	while (plat_kb_poll(&scan, &ascii))
		;

	for (;;) {
		char foot[40];
		sprintf(foot, "Name: %s_", (const char *)&c[96]);
		cg_draw_sheet(c, foot);

		while (!plat_kb_poll(&scan, &ascii))
			;
		if (ascii == 13 || ascii == 3) {            /* Return -> commit */
			if (len > 0)
				save_roster();
			else                                /* empty -> restore */
				for (k = 0; k < 16; k++)
					c[96 + k] = orig[k];
			return;
		}
		if (ascii == 27) {                          /* Esc -> cancel */
			for (k = 0; k < 16; k++)
				c[96 + k] = orig[k];
			return;
		}
		if ((ascii == 8 || ascii == 127) && len > 0) {
			c[96 + --len] = 0;
		} else if (ascii >= 32 && ascii < 127 && len < 15) {
			c[96 + len++] = (unsigned char)ascii;
			c[96 + len]   = 0;
		}
	}
}

/* Modify Character — page the party (Up/Down); N renames, R re-rolls the
 * ability scores (race/class-legal, re-deriving AC from the new DEX — HP
 * is earned over levels, so it's left intact). Esc/Return done. */
static void cg_modify_sheet(void)
{
	unsigned char *party[16];
	short          nparty, sel = 0;
	unsigned char  scan = 0, ascii = 0;

	nparty = cg_collect_party(party, 16);
	if (nparty == 0)
		return;

	g_a5_2347 = 1;
	load_menu_ui();
	while (plat_kb_poll(&scan, &ascii))
		;

	for (;;) {
		unsigned char *c = party[sel];
		cg_draw_sheet(c,
		    "N rename  R reroll  Up/Dn  Esc done");

		while (!plat_kb_poll(&scan, &ascii))
			;
		if (ascii == 27 || ascii == 13 || ascii == 3)
			break;
		else if (scan == 0x48)
			sel = (short)((sel + nparty - 1) % nparty);
		else if (scan == 0x50)
			sel = (short)((sel + 1) % nparty);
		else if (ascii == 'n' || ascii == 'N')
			cg_rename(c);
		else if (ascii == 'r' || ascii == 'R') {
			short st[6];
			short race  = c[CHAR_RACE], klass = c[CHAR_CLASS];
			short dex, dexmod, k;

			if (race  >= CG_NRACES)   race  = 0;
			if (klass >= CG_NCLASSES) klass = 0;
			cg_roll_stats(race, klass, st);
			for (k = 0; k < 6; k++)
				c[CHAR_STATS + k] = (unsigned char)st[k];
			dex    = st[3];
			dexmod = (dex >= 15) ? (short)(dex - 14) : 0;
			if (dexmod > 4) dexmod = 4;
			c[CHAR_AC] = (unsigned char)(10 - dexmod);   /* AC */
			save_roster();
		}
	}
}

/* Add Character — page the benched pool characters with Up/Down; Return
 * brings the highlighted one into the active party (sets CHAR_INPARTY), up
 * to CG_PARTY_MAX slots. Esc backs out. */
static void cg_add_character(void)
{
	unsigned char *cand[16];
	short          n, sel = 0;
	unsigned char  scan = 0, ascii = 0;

	if (cg_collect_addable(cand, 16) == 0) {
		cg_message("No characters to add.",
		           "Create one first.  Any key to go back.");
		return;
	}

	g_a5_2347 = 1;
	load_menu_ui();
	while (plat_kb_poll(&scan, &ascii))
		;

	for (;;) {
		char foot[64];

		n = cg_collect_addable(cand, 16);
		if (n == 0)                          /* added the last one */
			break;
		if (sel >= n)
			sel = (short)(n - 1);

		if (cg_party_size() >= CG_PARTY_MAX)
			sprintf(foot, "Party full (%d)   Esc back",
			        (int)CG_PARTY_MAX);
		else
			sprintf(foot, "Up/Dn char   Return add   Esc back");
		cg_draw_sheet(cand[sel], foot);

		while (!plat_kb_poll(&scan, &ascii))
			;
		if (ascii == 27)                     /* Esc -> back */
			break;
		if ((ascii == 13 || ascii == 3)      /* Return -> add */
		    && cg_party_size() < CG_PARTY_MAX) {
			cand[sel][CHAR_INPARTY] = 1;
			cg_party_relink();
			save_roster();
		} else if (scan == 0x48)
			sel = (short)((sel + n - 1) % n);
		else if (scan == 0x50)
			sel = (short)((sel + 1) % n);
	}
}

/* Remove Character — page the active party with Up/Down; Return benches the
 * highlighted member (clears CHAR_INPARTY — it stays in the pool, addable
 * again later). Esc backs out. Non-destructive, so no confirm. */
static void cg_remove_from_party(void)
{
	unsigned char *party[16];
	short          nparty, sel = 0;
	unsigned char  scan = 0, ascii = 0;

	if (cg_collect_party(party, 16) == 0)
		return;

	g_a5_2347 = 1;
	load_menu_ui();
	while (plat_kb_poll(&scan, &ascii))
		;

	for (;;) {
		nparty = cg_collect_party(party, 16);
		if (nparty == 0)
			break;
		if (sel >= nparty)
			sel = (short)(nparty - 1);

		cg_draw_sheet(party[sel],
		    "Up/Dn char   Return remove   Esc back");

		while (!plat_kb_poll(&scan, &ascii))
			;
		if (ascii == 27)                     /* Esc -> back */
			break;
		if (ascii == 13 || ascii == 3) {     /* Return -> bench */
			party[sel][CHAR_INPARTY] = 0;
			cg_party_relink();
			save_roster();
		} else if (scan == 0x48)
			sel = (short)((sel + nparty - 1) % nparty);
		else if (scan == 0x50)
			sel = (short)((sel + 1) % nparty);
	}
}

/* Delete Character — page the whole pool with Up/Down; Return picks the
 * highlighted character and asks Y/N, then erases it from the pool (and so
 * the party) permanently and persists. */
static void cg_delete_character(void)
{
	unsigned char *pool[16];
	short          n, sel = 0;
	unsigned char  scan = 0, ascii = 0;
	int            confirming = 0;

	if (cg_collect_pool(pool, 16) == 0)
		return;

	g_a5_2347 = 1;
	load_menu_ui();
	while (plat_kb_poll(&scan, &ascii))
		;

	for (;;) {
		char foot[64];

		n = cg_collect_pool(pool, 16);
		if (n == 0)
			break;
		if (sel >= n)
			sel = (short)(n - 1);

		if (confirming)
			sprintf(foot, "Delete %s?   Y / N",
			        (const char *)&pool[sel][96]);
		else
			sprintf(foot, "Up/Dn char   Return delete   Esc back");
		cg_draw_sheet(pool[sel], foot);

		while (!plat_kb_poll(&scan, &ascii))
			;

		if (confirming) {
			if (ascii == 'y' || ascii == 'Y') {
				/* Find pool[sel]'s index, shift the array down. */
				short k, idx = -1;
				for (k = 0; k < cg_pool_count; k++)
					if (cg_pool[k] == pool[sel]) { idx = k; break; }
				if (idx >= 0) {
					for (k = idx; k < cg_pool_count - 1; k++)
						memcpy(cg_pool[k], cg_pool[k + 1], 512);
					cg_pool_count--;
				}
				cg_party_relink();
				save_roster();
			}
			confirming = 0;
			continue;
		}

		if (ascii == 27)                            /* Esc -> back */
			break;
		if (ascii == 13 || ascii == 3)              /* Return -> confirm */
			confirming = 1;
		else if (scan == 0x48)
			sel = (short)((sel + n - 1) % n);
		else if (scan == 0x50)
			sel = (short)((sel + 1) % n);
	}
}

/* "The party sets forth" — list the assembled party on the shared chrome
 * before descending, so it's clear which characters are adventuring. (An
 * in-dungeon status HUD wants jt94's deep-mode text path sorted out — a
 * follow-up; this confirms the party on the chrome, where text is solid.) */
static void cg_party_setforth_screen(void)
{
	unsigned char *party[16];
	short          n, i;
	unsigned char *px; short pitch, sw, sh, yy;
	unsigned char  scan = 0, ascii = 0;

	n = cg_collect_party(party, 16);
	g_a5_2347 = 1;
	load_menu_ui();
	while (plat_kb_poll(&scan, &ascii))
		;
	if (qd_screen_pixels(&px, &pitch, &sw, &sh) && px) {
		if (g_menu_state == 1) {
			fill_backdrop(px, pitch, 0, 0,
			              (short)(sw - 1), (short)(sh - 1));
			draw_plate(px, pitch, sw, sh, 6, 8, 313, 150, 1);
		} else {
			for (yy = 0; yy < sh; yy++)
				memset(px + (long)yy * pitch, 0x08, (size_t)sw);
		}
	}

	jt94((short)3,  (short)2, 14, 0, "The party sets forth!");
	jt94((short)3,  (short)4, 12, 0, "Name");
	jt94((short)17, (short)4, 12, 0, "Class");
	jt94((short)28, (short)4, 12, 0, "HP");
	for (i = 0; i < n; i++) {
		short row   = (short)(6 + i);
		short klass = party[i][CHAR_CLASS];
		jt94((short)3,  row, 15, 0, "%s",
		     (const char *)&party[i][96]);
		jt94((short)17, row, 7, 0, "%s",
		     (klass < 6) ? k_roster_classes[klass] : "?");
		jt94((short)28, row, 7, 0, "%d", (int)party[i][CHAR_HP]);
	}
	jt94((short)3, (short)16, 7, 0, "Press any key to descend.");
	qd_present();

	while (!plat_kb_poll(&scan, &ascii))
		;
}

/* port_begin_adventure — the real "Begin Adventuring" entry (Training Hall
 * case 9 / l1142). Gated on a non-empty active party (the faithful CODE
 * 15-19 play setup is the deferred remainder): show who is adventuring,
 * then drop the party into the dungeon via the shared play loop with the
 * wall-set browse keys off. */
void port_begin_adventure(void)
{
	if (g_a5_long(-27928) == 0) {        /* no one in the party */
		cg_message("You have no party!",
		           "Add or create a character first.");
		return;
	}
	cg_party_setforth_screen();          /* show who is adventuring */
	g_adventure_mode = 1;
	port_play_demo();                    /* movement: WASD, M map, Q quit */
	g_adventure_mode = 0;
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
	cg_view_sheet();                     /* port: the read-only sheet */
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

/* L10ca — case 8 (Exit From Play / proceed-to-adventure). CODE 12 + 0x10ca.
 *
 * THIS is the real "Begin Adventuring" bridge: its L112c path is the ONLY
 * jt918 arm that returns 1, and L07dc proceeds into the dungeon exactly when
 * jt918 returns non-zero (`if (jt918(1) == 0) goto cleanup`). The L112c path
 * calls JT[942](1) (the adventure-mode setup) then returns 1. (Case 9 / l1142,
 * once mislabelled "Begin Adventuring", is actually Save Game — JT[585].)
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

/* L1142 — case 9 = the "Begin Adventuring" menu item. CODE 12 + 0x1142.
 *
 * RAW-DISASM ODDITY (verified, 2026-06-07): the STRS label rendered at this
 * build slot is "Begin Adventuring" (dumped live from the resource), the
 * jt918 JT[3] table (0x0efc) maps selection 9 -> 0x1142 (identity), yet the
 * 0x1142 body calls JT[585] = CODE 15+0x1a24, which is the SAVE routine
 * ("Saving...Please Wait", "SavGam%s%c"+"csv"). So the shipped CODE-12
 * dispatch sends the "Begin Adventuring" button to save code and "Save
 * Current Game" (sel 10 -> l115a) to the return-1 play-entry — inverted from
 * the labels. That label/case knot in the original save subsystem is not yet
 * fully resolved; rather than ship a button that lies, the port routes the
 * "Begin Adventuring" item to the engine's begin-adventure primitive so the
 * labelled button reaches the faithful dungeon.
 *
 * The begin primitive is l10ca's L112c arm verbatim: jt942(1) (sets the
 * adventure loop-continue flag g_a5_-4944 that l07dc's jt943 reads),
 * g_a5_-27982 = 1 (active-adventure gate jt948 checks), return 1 — which
 * makes l07dc proceed l67ca/jt937/jt938 -> jt217 -> JT[948], the faithful
 * dungeon walk loop (jt240 -> l63c0 -> jt297 -> l1908 -> jt312). Gated like
 * the real arm on the item-enable flag (g_a5_-14431) + an assembled party
 * (g_a5_-27928). Replaces the old port_begin_adventure/port_play_demo bridge. */
static int l1142(short a)
{
	(void)a;
	PROBE("jt918/case9 L1142");
	if (g_a5_14431 == 0)            /* "Begin Adventuring" item disabled */
		return 0;
	if (g_a5_27928 == 0) {          /* no party assembled */
		jt159(ua_strs_at(0x5fb6), 1);   /* faithful: the item would be dimmed */
		return 0;
	}
	jt942(1);                       /* adventure-mode loop flag (l10ca L112c) */
	g_a5_27982 = 1;                 /* active-adventure gate */
	return 1;                       /* -> l07dc -> jt948 (faithful dungeon) */
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
				if (g_menu_state == 1) {
					fill_backdrop(px, pitch, 0, 0,
					              (short)(sw - 1), (short)(sh - 1));
					/* Flat recessed plate behind the roster grid (top of
					 * the screen, l02dc rows ~2..12) so the names + AC/HP
					 * read clearly off the stone — the original draws the
					 * roster on a flat grey area too. */
					draw_plate(px, pitch, sw, sh, 6, 8, 313, 104, 1);
				} else {
					for (yy = 0; yy < sh; yy++)
						memset(px + (long)yy * pitch, 0x08, (size_t)sw);
				}
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
			g_a5_14438 = 1;            /* port enable: Delete   */
			g_a5_14437 = 0;
			g_a5_14436 = 1;            /* port enable: Remove   */
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
