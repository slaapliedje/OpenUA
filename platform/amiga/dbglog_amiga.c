/*
 * Amiga debug-log backend (ADR-0012).
 *
 * Implements the debug HAL (dbglog.h). Two sinks, mirroring the Atari one:
 *   - dbg_log / dbg_log_num  -> the serial debug port via the ROM's RawPutChar
 *     (KPrintF-style). amiberry captures serial to its log/console, so this is
 *     the equivalent of Hatari's --conout 2.
 *   - dbg_file_num / dbg_file_str -> appended to "PROGDIR:DBG.LOG" via
 *     dos.library, the equivalent of the GEMDOS C:\DBG.LOG file sink; truncated
 *     on the first call per run.
 *
 * ★ THE INTERRUPT-CONTEXT TRAP APPLIES HERE TOO. The Atari sink learned the
 * hard way that ANY debug sink called from an interrupt handler (the audio VBL
 * runs the engine sequencer) is fatal if it traps: GEMDOS/AmigaOS calls are not
 * safe from a Forbid/interrupt. The Atari fix DEFERS the line to a task-level
 * flush (see platform/dbglog.c, g_plat_in_super). The Amiga backend MUST grow
 * the same deferral before anything reachable from plat_sound_vbl() logs — the
 * serial RawPutChar is arguably interrupt-safe, but dos.library Write is NOT.
 * For the scaffold both sinks are task-level-only; wire the defer when the VBL
 * sequencer lands.
 *
 * ★ SCAFFOLD STATUS: structure real; RawDoFmt/serial and dos.library bodies are
 * TODO(hw) until the toolchain lands. A bring-up aid; not for shipping code.
 */

#include "dbglog.h"

#ifdef FRUA_AMIGA

#include <exec/types.h>
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

static void serial_str(const char *s)
{
	(void)s;
	/* TODO(hw): emit each char over the serial debug port. The clean way is
	 * RawDoFmt("%s\n", &s, RawPutChar_stub, NULL) using the ROM RawPutChar
	 * (exec.library offset -516), which amiberry mirrors to its serial log. */
}

void dbg_log(const char *msg)
{
	serial_str(msg ? msg : "(null)");
}

void dbg_log_num(const char *label, long value)
{
	char num[12];
	num_to_dec(value, num);
	serial_str(label ? label : "");
	serial_str(num);
}

/* --- file sink (PROGDIR:DBG.LOG) ----------------------------------------- */

static BPTR s_log;          /* dos.library file handle; 0 = not open yet     */
static int  s_truncated;

static BPTR log_open(void)
{
	if (!s_truncated) {
		/* TODO(hw): Open("PROGDIR:DBG.LOG", MODE_NEWFILE) once to truncate,
		 * then reopen MODE_OLDFILE + Seek(END) for appends. */
		s_truncated = 1;
	}
	/* TODO(hw): return an opened append handle; 0 for now. */
	return s_log;
}

void dbg_file_str(const char *label, const char *value)
{
	BPTR fh = log_open();
	(void)fh; (void)label; (void)value;
	/* TODO(hw): Write(fh, label, len); Write(fh, value, len); Write(fh, "\n",1) */
}

void dbg_file_num(const char *label, long value)
{
	char num[12];
	num_to_dec(value, num);
	dbg_file_str(label, num);
}

#endif /* FRUA_AMIGA */
