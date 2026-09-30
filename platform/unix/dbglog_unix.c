/*
 * Debug-log backend — Unix (AMIX, Atari System V).
 *
 * The same two sinks as the Atari and Amiga backends, both to files and
 * standard error: dbg_log / dbg_log_num (the boot breadcrumbs) and
 * dbg_file_num / dbg_file_str all append to DBG.LOG in the current
 * directory, truncated on the first line of a run. Each line opens,
 * appends and closes, so the trail survives a crash. There is no interrupt
 * context on this port (sound and input run in the engine's own thread),
 * so nothing needs deferring.
 *
 * stdio's FILE is not shared with the AMIX libc (its layout differs under
 * GCC's alignment: see toolchain/sysv4-cc); this uses write(2).
 */
#ifdef FRUA_UNIX

#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdlib.h>

#include "dbglog.h"
#include "unix_vbl.h"

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

static int s_truncated;

static void log_line(const char *a, const char *b)
{
	int fd = open("DBG.LOG", O_WRONLY | O_CREAT | O_APPEND
	                         | (s_truncated ? 0 : O_TRUNC), 0666);
	s_truncated = 1;
	if (fd >= 0) {
		write(fd, a, strlen(a));
		write(fd, b, strlen(b));
		write(fd, "\n", 1);
		close(fd);
	}
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

void dbg_log(const char *msg)
{
	log_line(msg != NULL ? msg : "(null)", "");
}

void dbg_log_num(const char *label, long value)
{
	dbg_file_num(label, value);
}

/* --- the in-memory event trace (OPENUA_TRACE) ------------------------------ */

#define TRACE_MAX	8192		/* the last this many events are kept */

static struct { unsigned long t; const char *tag; long a, b; } s_tr[TRACE_MAX];
static long s_tr_n;
static int  s_tr_on = -1;

void dbg_trace(const char *tag, long a, long b)
{
	long i;

	if (s_tr_on < 0)
		s_tr_on = getenv("OPENUA_TRACE") != NULL;
	if (!s_tr_on)
		return;
	i = s_tr_n++ % TRACE_MAX;
	s_tr[i].t = unix_ticks();
	s_tr[i].tag = tag;
	s_tr[i].a = a;
	s_tr[i].b = b;
}

/* "trace: <tick> <tag> <a> <b>" per event, oldest first, in one pass */
void dbg_trace_dump(void)
{
	static char buf[4096];
	char num[12];
	long i, from = s_tr_n > TRACE_MAX ? s_tr_n - TRACE_MAX : 0;
	int  fd, n = 0;

	if (s_tr_on <= 0 || s_tr_n == 0)
		return;
	dbg_file_num("trace: events recorded = ", s_tr_n);
	fd = open("DBG.LOG", O_WRONLY | O_APPEND);
	if (fd < 0)
		return;
	for (i = from; i < s_tr_n; i++) {
		const char *tag = s_tr[i % TRACE_MAX].tag;
		int tl = (int)strlen(tag) > 40 ? 40 : (int)strlen(tag);

		num_to_dec((long)s_tr[i % TRACE_MAX].t, num);
		if (n > (int)sizeof buf - 128) {
			write(fd, buf, n);
			n = 0;
		}
		memcpy(buf + n, "trace: ", 7); n += 7;
		memcpy(buf + n, num, strlen(num)); n += (int)strlen(num);
		buf[n++] = ' ';
		memcpy(buf + n, tag, tl); n += tl;
		num_to_dec(s_tr[i % TRACE_MAX].a, num);
		buf[n++] = ' '; memcpy(buf + n, num, strlen(num)); n += (int)strlen(num);
		num_to_dec(s_tr[i % TRACE_MAX].b, num);
		buf[n++] = ' '; memcpy(buf + n, num, strlen(num)); n += (int)strlen(num);
		buf[n++] = '\n';
	}
	if (n)
		write(fd, buf, n);
	close(fd);
}

void dbg_log_screen_owned(void)
{
	/* the log never draws: nothing to hand over */
}

#endif /* FRUA_UNIX */
