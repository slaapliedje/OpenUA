/*
 * Debug log — see dbglog.h.
 *
 * Writes to the VT-52 console; with Hatari's `--conout 2` the line is
 * mirrored to the host terminal, so the trail is captured even when the
 * program crashes too fast to read on screen.
 *
 * ── Why the deferred ring below exists ────────────────────────────────────
 * EVERY sink here traps: Cconws is GEMDOS, and so are Fcreate/Fopen/Fwrite.
 * GEMDOS is NOT reentrant, and the sound VBL (plat_sound_vbl) runs the
 * ENGINE's sequencer task from inside an interrupt. So any trace reachable
 * from the sequencer — and under -DFRUA_ENGINE_PROBE that is EVERY engine
 * stub it touches, e.g. jt1091 -> jt1149 (TickCount) — traps from interrupt
 * context, wrecks the GEMDOS stack, and the next `rts` returns to a negative
 * address:
 *
 *     Address Error reading at address $ffff00e1, PC=$193212  op=4e75 (rts)
 *
 * That is what made `make ENGINE_PROBE=1` die a couple of seconds into boot
 * while still logging ~1900 lines first — a probe build that CRASHES, and so
 * reports "0 calls" for everything after it. It produced three false
 * negatives in one session before anyone noticed the app was dead.
 *
 * platform/input.c already guards the same hazard for Supexec via
 * g_plat_in_super ("trapping from inside an interrupt handler is fatal").
 * The debug sinks were simply never given the same treatment.
 *
 * Fix: in interrupt context we DEFER instead of trapping — the line is copied
 * into a single-producer/single-consumer ring and flushed from the next call
 * made at task level. Nothing is dropped silently; if the ring ever overflows
 * the flush says so, loudly. A harness that lies is worse than no harness.
 */

#include <stddef.h>

#include <mint/osbind.h>

#include "dbglog.h"

/* Set by plat_sound_vbl around the engine's sound task (platform/input.c). */
extern volatile int g_plat_in_super;

#define DBG_RING     64                 /* power of two */
#define DBG_RING_MSK (DBG_RING - 1)
#define DBG_TEXT     72

enum { SINK_CON, SINK_FILE };

struct dbg_line {
	char  text[DBG_TEXT];               /* copied — callers may pass stack bufs */
	long  value;
	short has_value;
	short sink;
};

/* SPSC ring: producer = the VBL (interrupt), consumer = task level. Aligned
 * short loads/stores are atomic on the 68k, so no locking is needed. */
static struct dbg_line  g_ring[DBG_RING];
static volatile short   g_ring_head;    /* written by the producer only */
static volatile short   g_ring_tail;    /* written by the consumer only */
static volatile short   g_ring_lost;

static short str_len(const char *s)
{
	short n = 0;
	while (s[n] != '\0')
		n++;
	return n;
}

static short num_to_str(char *buf12, long value)
{
	short         i = 11;
	short         neg = (value < 0);
	unsigned long u = neg ? (unsigned long)-value : (unsigned long)value;

	buf12[i--] = '\0';
	do {
		buf12[i--] = (char)('0' + (short)(u % 10));
		u /= 10;
	} while (u != 0);
	if (neg)
		buf12[i--] = '-';
	return (short)(i + 1);
}

/* Copy src (truncating) into dst[cap]; returns the length written. */
static short str_copy(char *dst, short cap, const char *src)
{
	short n = 0;
	while (src[n] != '\0' && n < (short)(cap - 1)) {
		dst[n] = src[n];
		n++;
	}
	dst[n] = '\0';
	return n;
}

/* Producer — interrupt context. Never traps. */
static void dbg_defer(short sink, const char *label, const char *extra,
                      long value, short has_value)
{
	short next = (short)((g_ring_head + 1) & DBG_RING_MSK);
	struct dbg_line *ln;
	short n;

	if (next == g_ring_tail) {              /* full */
		g_ring_lost++;
		return;
	}
	ln = &g_ring[g_ring_head];
	n = str_copy(ln->text, DBG_TEXT, label);
	if (extra != NULL)
		str_copy(ln->text + n, (short)(DBG_TEXT - n), extra);
	ln->value     = value;
	ln->has_value = has_value;
	ln->sink      = sink;
	g_ring_head   = next;                   /* publish last */
}

/* ---- the trapping sinks (task level only) -------------------------------- */

static short g_dbg_fh_started = 0;

static long dbg_file_open(void)
{
	long fh;

	/* Fresh file the first call of the run, then append. Open/seek/close per
	 * call so every line is flushed (a hard pkill -9 can't lose buffered data).
	 * Recreate when the append-open fails — the host file may have been
	 * deleted mid-run; without this every later line is silently dropped. */
	if (!g_dbg_fh_started) {
		fh = Fcreate("DBG.LOG", 0);
		g_dbg_fh_started = 1;
	} else {
		fh = Fopen("DBG.LOG", 1);
		if (fh >= 0)
			Fseek(0L, (short)fh, 2);        /* SEEK_END */
		else
			fh = Fcreate("DBG.LOG", 0);
	}
	return fh;
}

static void emit_file(const char *label, const char *num)
{
	long fh = dbg_file_open();

	if (fh < 0)
		return;
	Fwrite((short)fh, (long)str_len(label), label);
	if (num != NULL)
		Fwrite((short)fh, (long)str_len(num), num);
	Fwrite((short)fh, 2L, "\r\n");
	Fclose((short)fh);
}

static void emit_con(const char *label, const char *num)
{
	Cconws(label);
	if (num != NULL)
		Cconws(num);
	Cconws("\r\n");
}

/* Consumer — task level. Drain whatever the VBL parked for us. */
static void dbg_flush_deferred(void)
{
	char buf[12];

	while (g_ring_tail != g_ring_head) {
		const struct dbg_line *ln = &g_ring[g_ring_tail];
		const char *num = NULL;

		if (ln->has_value)
			num = &buf[num_to_str(buf, ln->value)];
		if (ln->sink == SINK_FILE)
			emit_file(ln->text, num);
		else
			emit_con(ln->text, num);
		g_ring_tail = (short)((g_ring_tail + 1) & DBG_RING_MSK);
	}

	/* Never drop silently — a harness that lies is worse than no harness. */
	if (g_ring_lost != 0) {
		short lost = g_ring_lost;
		short off;

		g_ring_lost = 0;
		off = num_to_str(buf, (long)lost);
		emit_con("dbg: DEFERRED LINES LOST (ring full): ", &buf[off]);
		emit_file("dbg: DEFERRED LINES LOST (ring full): ", &buf[off]);
	}
}

/* ---- public API ---------------------------------------------------------- */

void dbg_log(const char *msg)
{
	if (g_plat_in_super) {                  /* VBL — must not trap */
		dbg_defer(SINK_CON, msg, NULL, 0L, 0);
		return;
	}
	dbg_flush_deferred();
	emit_con(msg, NULL);
}

void dbg_log_num(const char *label, long value)
{
	char  buf[12];
	short off;

	if (g_plat_in_super) {
		dbg_defer(SINK_CON, label, NULL, value, 1);
		return;
	}
	dbg_flush_deferred();
	off = num_to_str(buf, value);
	emit_con(label, &buf[off]);
}

void dbg_file_num(const char *label, long value)
{
	char  buf[12];
	short off;

	if (g_plat_in_super) {
		dbg_defer(SINK_FILE, label, NULL, value, 1);
		return;
	}
	dbg_flush_deferred();
	off = num_to_str(buf, value);
	emit_file(label, &buf[off]);
}

/* String variant — appends "label value\r\n". */
void dbg_file_str(const char *label, const char *value)
{
	if (g_plat_in_super) {
		dbg_defer(SINK_FILE, label, value, 0L, 0);
		return;
	}
	dbg_flush_deferred();
	emit_file(label, value);
}
