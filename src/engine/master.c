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

/* --- frontier: routines master_init / master_shutdown call, not lifted --- */

/* CODE 4 — the Mac Toolbox / Window Manager layer. JT[1144] is the
 * standard Toolbox-init prologue; the shim's toolbox_init() runs the
 * managers in the Mac startup order. */
static void jt1158(void)            { }   /* CODE 4 + 0x4c48 */
static void jt1157(short a, long b) { }   /* CODE 4 + 0x61c6 */
static void jt1114(void)            { }   /* CODE 4 + 0x61ee */
static void jt1138(void)            { }   /* CODE 4 + 0x66f8 */
static void jt1156(void)            { }   /* CODE 4 + 0x670e */
static void jt1155(void)            { }   /* CODE 4 + 0x7972 */
static void jt1119(void)            { }   /* CODE 4 + 0x797e */

/* CODE 5 — intra-segment helpers. */
static void l01a2(void)             { }   /* CODE 5 + 0x01a2 */
static void l01ac(void)             { }   /* CODE 5 + 0x01ac */
static void l024c(short a)          { }   /* CODE 5 + 0x024c */
static void l0eda(short a, long b)  { }   /* CODE 5 + 0x0eda */
static void l0f14(void)             { }   /* CODE 5 + 0x0f14 */
static void l27a4(void)             { }   /* CODE 5 + 0x27a4 */
static void l27bc(void)             { }   /* CODE 5 + 0x27bc */
static void l35e2(void)             { }   /* CODE 5 + 0x35e2 */
static void l35f8(void)             { }   /* CODE 5 + 0x35f8 */

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
	fc_init(kb_min, kb_max);       /* file cache — JT[463] */
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
