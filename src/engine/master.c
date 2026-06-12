/*
 * FRUA master init / shutdown — lifted from CODE 5 (jump-table entries
 * 1079 and 1081).
 *
 * master_init() is main()'s phase-3 bring-up; master_shutdown() is the
 * matching teardown main() runs on the way out. Both are pure sequencers:
 *
 *   master_init      JT[1079]  Toolbox startup, offscreen-page setup, fc_init
 *   master_shutdown  JT[1081]  Toolbox shutdown, page teardown, fc_cleanup
 *
 * Only fc_init / fc_cleanup are lifted so far; the routines around them are
 * the no-op stubs below, each tagged with its CODE location (the fc_dump
 * pattern). See docs/decompilation.md for the Display subsystem map.
 */

#include "master.h"
#include "fc.h"               /* fc_init, fc_cleanup */
#include "toolbox.h"          /* toolbox_init — the lifted JT[1144] */
#include "data_pool_replay.h" /* g_a5_byte / g_a5_long / g_a5_word (jt1138) */
#include "input.h"            /* plat_ticks — jt1155 stamps tick origin */
#include "boot.h"             /* glib_pool_open — FAR pool (JT[463]) */

/* Stub-trace probe; same as boot.c — see docs/engine-bring-up.md. */
#ifdef FRUA_ENGINE_PROBE
#  include "dbglog.h"
#  define PROBE(name) dbg_log("stub: " name)
#else
#  define PROBE(name) ((void)0)
#endif

/* --- frontier: routines master_init / master_shutdown call, not lifted --- */

/* CODE 4 — the Mac Toolbox / Window Manager layer. JT[1144] is the
 * standard Toolbox-init prologue; the shim's toolbox_init() runs the
 * managers in the Mac startup order. */
static void jt1158(void)            { PROBE("jt1158"); }
static void jt1157(short a, long b) { PROBE("jt1157"); }

/* JT[1114] (CODE 4 + 0x61ee) — flag "engine initialized."
 *   g_a5_-900 = 1;
 * Set by master_shutdown so the next master_init can clean up
 * any pending state. Mac body is exactly two instructions. */
static void jt1114(void)
{
	PROBE("jt1114");
	g_a5_byte(-900) = 1;
}
/* JT[1138] (CODE 4 + 0x66f8) — reset engine input state.
 *
 *   g_a5_-809 = 0;          // macro start-of-stream flag
 *   g_a5_-810 = 0;          // macro continue-replay flag
 *   g_a5_-820 = 0;          // key-pending flag
 *   g_a5_-814 = 0;          // macro buffer pointer
 *   g_a5_-2592 = 0;         // last-event.what cache
 *
 * Called once from master_init's CODE 5 + 0x36 init: drops any
 * leftover macro / key state from a previous engine session so
 * the first jt1133() call hits the live-poll path. Single
 * fire in the boot trace.
 */
static void jt1138(void)
{
	PROBE("jt1138");
	g_a5_byte(-809)  = 0;
	g_a5_byte(-810)  = 0;
	g_a5_byte(-820)  = 0;
	g_a5_long(-814)  = 0;
	g_a5_word(-2592) = 0;
}
/* JT[1156] (CODE 4 + 0x670e) — no-op (bare rts in the Mac body).
 * Kept as a real lift so master.c stops marking it stub. */
static void jt1156(void)
{
	PROBE("jt1156");
}

/* JT[1155] (CODE 4 + 0x7972) — stamp the engine's tick origin.
 *   g_a5_-130 = TickCount();
 * Used as the reference point that jt1134 / jt1149 / L79d4 / L79ec
 * subtract from to compute elapsed game time. */
static void jt1155(void)
{
	PROBE("jt1155");
	g_a5_long(-130) = (long)plat_ticks();
}

/* JT[1119] (CODE 4 + 0x797e) — no-op (bare rts in the Mac body).
 * Pair to jt1156 in the master_shutdown sequence. */
static void jt1119(void)
{
	PROBE("jt1119");
}

/* CODE 5 — intra-segment helpers. */
static void l01a2(void)             { PROBE("l01a2"); }
static void l01ac(void)             { PROBE("l01ac"); }
static void l024c(short a)          { PROBE("l024c"); }
static void l0eda(short a, long b)  { PROBE("l0eda"); }
static void l0f14(void)             { PROBE("l0f14"); }
static void l27a4(void)             { PROBE("l27a4"); }
static void l27bc(void)             { PROBE("l27bc"); }
static void l35e2(void)             { PROBE("l35e2"); }
static void l35f8(void)             { PROBE("l35f8"); }

/*
 * master_init — CODE 5 + 0x4 (jump-table entry 1079).
 *
 * kb_min / kb_max bound the file-cache buffer; arg1 / arg2 pass through to
 * the Toolbox startup and the page setup.
 */
void master_init(short arg1, long arg2, short kb_min, short kb_max)
{
	toolbox_init();                /* Mac Toolbox startup  — JT[1144] */
	l0eda(arg1, arg2);
	jt1157(arg1, arg2);
	jt1155();
	jt1138();
	l01a2();
	l024c(15);
	fc_init(kb_min, kb_max);       /* file cache — JT[463]'s size half */
	glib_pool_open();              /* FAR pool — JT[463]/_LBOpen proper */
	l35e2();
	l27a4();
}

/*
 * master_shutdown — CODE 5 + 0x62 (jump-table entry 1081).
 *
 * The teardown counterpart of master_init, run by main() on the way out.
 */
void master_shutdown(void)
{
	l27bc();
	l35f8();
	fc_cleanup();                  /* file cache — JT[466] */
	jt1156();
	l01ac();
	jt1119();
	jt1114();
	l0f14();
	jt1158();
}
