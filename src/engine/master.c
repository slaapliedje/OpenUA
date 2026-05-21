/*
 * FRUA master init — lifted from CODE 5 + 0x4 (jump-table entry 1079).
 *
 * main()'s phase-3 bring-up: it runs the Mac Toolbox startup (JT[1144]),
 * the offscreen-page setup (JT[1157] / JT[1155] / JT[1138] and the CODE 5
 * helpers), and the file cache (fc_init, JT[463]). It is a pure sequencer —
 * all the real work is in the routines it calls.
 *
 * Only fc_init is lifted so far; the rest are the no-op stubs below, each
 * tagged with its CODE location (the fc_dump pattern). JT[1081], the
 * matching master shutdown, is the obvious next sibling. See
 * docs/decompilation.md for the Display subsystem map.
 */

#include "master.h"
#include "fc.h"               /* fc_init */

/* --- frontier: routines master_init() calls that are not lifted yet --- */
static void jt1144(short a, long b) { }   /* CODE 4 + 0x4774 — Mac Toolbox init */
static void jt1157(short a, long b) { }   /* CODE 4 + 0x61c6 */
static void jt1155(void)            { }   /* CODE 4 + 0x7972 */
static void jt1138(void)            { }   /* CODE 4 + 0x66f8 */
static void l0eda(short a, long b)  { }   /* CODE 5 + 0x0eda */
static void l01a2(void)             { }   /* CODE 5 + 0x01a2 */
static void l024c(short a)          { }   /* CODE 5 + 0x024c */
static void l27a4(void)             { }   /* CODE 5 + 0x27a4 */
static void l35e2(void)             { }   /* CODE 5 + 0x35e2 */

/*
 * master_init — CODE 5 + 0x4 (jump-table entry 1079).
 *
 * kb_min / kb_max bound the file-cache buffer; arg1 / arg2 pass through to
 * the Toolbox startup and the page setup.
 */
void master_init(short arg1, long arg2, short kb_min, short kb_max)
{
	jt1144(arg1, arg2);            /* Mac Toolbox startup  */
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
