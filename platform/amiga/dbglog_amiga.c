/*
 * Amiga debug-log backend (ADR-0012).
 *
 * Implements the debug HAL (dbglog.h). Two sinks, mirroring the Atari one:
 *   - dbg_log / dbg_log_num  -> intended for the serial debug port (RawPutChar,
 *     which amiberry mirrors to its log). The serial veneer is still TODO(hw);
 *     during bring-up these MIRROR to the file sink instead, so the boot trail
 *     (main.c's dbg_log breadcrumbs) is not lost — it is the only visibility
 *     we have inside amiberry until a frame renders.
 *   - dbg_file_num / dbg_file_str -> appended to "PROGDIR:DBG.LOG" via
 *     dos.library, the equivalent of the GEMDOS C:\DBG.LOG sink; truncated on
 *     the first call per run. Each line opens/appends/closes, so the trail
 *     survives a crash — exactly what a bring-up sink is for. Slow is fine.
 *
 * ★ THE INTERRUPT-CONTEXT TRAP APPLIES HERE TOO. dos.library must never be
 * called from an interrupt (the Atari sink learned this the hard way — see
 * platform/dbglog.c's deferred ring). These sinks are TASK-LEVEL ONLY; the
 * VBL server and anything reachable from it must not log. The deferral ring
 * gets ported when the synth sequencer lands on the Amiga VBL.
 */

#include "dbglog.h"

#ifdef FRUA_AMIGA

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

/* Minimal decimal formatting so the sink needs no libc. */
static void num_to_dec(long v, char *out)
{
	char tmp[12];
	int  n = 0, neg = 0;
	unsigned long u;
	if (v < 0) { neg = 1; u = (unsigned long)(-v); } else u = (unsigned long)v;
	if (u == 0) tmp[n++] = '0';
	while (u) { tmp[n++] = (char)('0' + (u % 10)); u /= 10; }
	{
		int i = 0;
		if (neg) out[i++] = '-';
		while (n) out[i++] = tmp[--n];
		out[i] = '\0';
	}
}

static long str_len(const char *s)
{
	long n = 0;
	while (s[n] != '\0')
		n++;
	return n;
}

/* --- file sink (PROGDIR:DBG.LOG) ----------------------------------------- */

static int s_truncated;

static void log_line(const char *a, const char *b)
{
	BPTR fh;

	if (!s_truncated) {
		/* First line of the run: truncate. */
		fh = Open((CONST_STRPTR)"PROGDIR:DBG.LOG", MODE_NEWFILE);
		s_truncated = 1;
	} else {
		fh = Open((CONST_STRPTR)"PROGDIR:DBG.LOG", MODE_OLDFILE);
		if (fh != 0)
			Seek(fh, 0, 1 /* OFFSET_END */);
	}
	if (fh == 0)
		return;
	if (a != NULL && a[0] != '\0')
		Write(fh, (APTR)a, str_len(a));
	if (b != NULL && b[0] != '\0')
		Write(fh, (APTR)b, str_len(b));
	Write(fh, (APTR)"\n", 1);
	Close(fh);
}

void dbg_file_str(const char *label, const char *value)
{
	log_line(label != NULL ? label : "", value != NULL ? value : "");
}

void dbg_file_num(const char *label, long value)
{
	char num[12];
	num_to_dec(value, num);
	log_line(label != NULL ? label : "", num);
}

/* --- "console" sink -------------------------------------------------------
 * TODO(hw): the real target is the serial debug port (exec RawPutChar via a
 * small LVO veneer), which amiberry mirrors to its log. Until that exists,
 * mirror to the file so the boot breadcrumb trail is visible in the mounted
 * directory — the whole point of the trail during bring-up. */

void dbg_log(const char *msg)
{
	log_line(msg != NULL ? msg : "(null)", "");
}

void dbg_log_num(const char *label, long value)
{
	dbg_file_num(label, value);
}

#endif /* FRUA_AMIGA */
