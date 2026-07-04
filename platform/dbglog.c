/*
 * Debug log — see dbglog.h.
 *
 * Writes to the VT-52 console; with Hatari's `--conout 2` the line is
 * mirrored to the host terminal, so the trail is captured even when the
 * program crashes too fast to read on screen.
 */

#include <mint/osbind.h>

#include "dbglog.h"

void dbg_log(const char *msg)
{
	Cconws(msg);
	Cconws("\r\n");
}

void dbg_log_num(const char *label, long value)
{
	char          buf[12];
	short         i = 11;
	short         neg = (value < 0);
	unsigned long u = neg ? (unsigned long)-value : (unsigned long)value;

	buf[i--] = '\0';
	do {
		buf[i--] = (char)('0' + (short)(u % 10));
		u /= 10;
	} while (u != 0);
	if (neg)
		buf[i--] = '-';

	Cconws(label);
	Cconws(&buf[i + 1]);
	Cconws("\r\n");
}

static short g_dbg_fh_started = 0;

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

static short str_len(const char *s)
{
	short n = 0;
	while (s[n] != '\0')
		n++;
	return n;
}

void dbg_file_num(const char *label, long value)
{
	char  buf[12];
	short off;
	long  fh;

	/* Fresh file the first call of the run, then append. Open/seek/close per
	 * call so every line is flushed (a hard pkill -9 can't lose buffered data).
	 * Recreate when the append-open fails — the host file may have been
	 * deleted mid-run (a probing session truncating between reads); without
	 * this every later line is silently dropped. */
	if (!g_dbg_fh_started) {
		fh = Fcreate("DBG.LOG", 0);
		g_dbg_fh_started = 1;
	} else {
		fh = Fopen("DBG.LOG", 1);
		if (fh >= 0)
			Fseek(0L, (short)fh, 2);   /* SEEK_END */
		else
			fh = Fcreate("DBG.LOG", 0);
	}
	if (fh < 0)
		return;

	off = num_to_str(buf, value);
	Fwrite((short)fh, (long)str_len(label), label);
	Fwrite((short)fh, (long)str_len(&buf[off]), &buf[off]);
	Fwrite((short)fh, 2L, "\r\n");
	Fclose((short)fh);
}

/* String variant of dbg_file_num — appends "label value\r\n". Same
 * open/append/close-per-call flush discipline. */
void dbg_file_str(const char *label, const char *value)
{
	long fh;

	if (!g_dbg_fh_started) {
		fh = Fcreate("DBG.LOG", 0);
		g_dbg_fh_started = 1;
	} else {
		fh = Fopen("DBG.LOG", 1);
		if (fh >= 0)
			Fseek(0L, (short)fh, 2);
		else
			fh = Fcreate("DBG.LOG", 0);
	}
	if (fh < 0)
		return;

	Fwrite((short)fh, (long)str_len(label), label);
	Fwrite((short)fh, (long)str_len(value), value);
	Fwrite((short)fh, 2L, "\r\n");
	Fclose((short)fh);
}
