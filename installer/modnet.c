/* modnet — see modnet.h. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "modnet.h"

#define MN_CONNECT_MS  20000UL
/* Silence this long = a dead line. TCP's retransmit backoff on a lossy
 * link goes quiet for longer than 30 s: ARAnyM's emulated Ethernet under
 * MiNTnet stalled one 1.6 MB download that way (a bare blocking read()
 * sees the same gaps, so it is the link, not this code). */
#define MN_READ_MS     60000UL
#define MN_HEAD_MAX    4096
#define MN_REDIRECTS   3
#define MN_USER_AGENT  "OpenUA-modman/1"

static void set_err(char *err, unsigned cap, const char *a, const char *b)
{
	if (cap == 0)
		return;
	snprintf(err, cap, "%s%s", a, b ? b : "");
}

/* "http://host[:port]/path" -> its parts. 0 when it is not plain http. */
static int split_url(const char *url, char *host, unsigned hcap, unsigned *port,
                     const char **path)
{
	const char *h, *e, *colon;
	size_t n;

	if (strncmp(url, "http://", 7) != 0)
		return 0;
	h = url + 7;
	e = strchr(h, '/');
	if (!e)
		e = h + strlen(h);
	colon = memchr(h, ':', (size_t)(e - h));
	n = (size_t)((colon ? colon : e) - h);
	if (n == 0 || n >= hcap)
		return 0;
	memcpy(host, h, n);
	host[n] = 0;
	*port = colon ? (unsigned)atoi(colon + 1) : 80;
	*path = *e ? e : "/";
	return *port != 0;
}

static int send_all(plat_transport *t, const char *s)
{
	return plat_transport_write(t, (const u8 *)s, (u32)strlen(s), MN_READ_MS) == WP_OK;
}

/* A header's value (case-insensitive name), copied into `out`. */
static int header(const char *head, const char *name, char *out, unsigned cap)
{
	size_t n = strlen(name);
	const char *p = head;

	while ((p = strchr(p, '\n')) != 0) {
		p++;
		if (strlen(p) > n && p[n] == ':') {
			size_t k;

			for (k = 0; k < n; k++)
				if (tolower((unsigned char)p[k]) != tolower((unsigned char)name[k]))
					break;
			if (k == n) {
				const char *v = p + n + 1;
				unsigned o = 0;

				while (*v == ' ' || *v == '\t')
					v++;
				while (*v && *v != '\r' && *v != '\n' && o + 1 < cap)
					out[o++] = *v++;
				out[o] = 0;
				return 1;
			}
		}
	}
	return 0;
}

/* Feed body bytes through the chunked-transfer decoder (HTTP/1.1 servers
 * sometimes chunk even a 1.0 reply). State lives in *st. */
typedef struct chunk_state {
	int           phase;            /* 0 size line, 1 data, 2 CRLF, 3 done */
	unsigned long left;
} chunk_state;

static int dechunk(chunk_state *st, const unsigned char *p, unsigned long n,
                   mn_sink sink, void *ctx, unsigned long *got)
{
	unsigned long i = 0;

	while (i < n && st->phase != 3) {
		if (st->phase == 0) {
			int c = p[i++];

			if (isxdigit(c))
				st->left = st->left * 16 + (unsigned long)(isdigit(c) ? c - '0'
				                                   : tolower(c) - 'a' + 10);
			else if (c == '\n')
				st->phase = st->left ? 1 : 3;
		} else if (st->phase == 1) {
			unsigned long take = n - i < st->left ? n - i : st->left;

			if (!sink(ctx, p + i, take))
				return 0;
			*got += take;
			i += take;
			st->left -= take;
			if (st->left == 0)
				st->phase = 2;
		} else {                        /* the CRLF after a chunk */
			if (p[i++] == '\n')
				st->phase = 0;
		}
	}
	return 1;
}

static int get_once(const char *url, mn_sink sink, mn_progress progress,
                    void *ctx, char *location, unsigned loccap,
                    char *err, unsigned errcap)
{
	char host[128], endpoint[160], line[512], value[64];
	char *head = malloc(MN_HEAD_MAX + 1);
	unsigned char *buf = malloc(2048);
	const char *path;
	unsigned port, hl = 0;
	plat_transport *t = 0;
	wp_status st;
	long total = -1;
	unsigned long got = 0;
	int status = 0, ok = 0, chunked = 0;
	chunk_state cs;

	memset(&cs, 0, sizeof cs);
	location[0] = 0;
	if (!head || !buf) {
		set_err(err, errcap, "out of memory", 0);
		goto done;
	}
	if (!split_url(url, host, sizeof host, &port, &path)) {
		set_err(err, errcap, "not a plain http:// address: ", url);
		goto done;
	}
	snprintf(endpoint, sizeof endpoint, "tcp:%s:%u", host, port);
	st = plat_transport_open(endpoint, MN_CONNECT_MS, &t);
	if (st == WP_UNSUPPORTED) {
		set_err(err, errcap, "no network: this machine has no TCP/IP stack running", 0);
		goto done;
	}
	if (st != WP_OK) {
		set_err(err, errcap, "could not connect to ", host);
		goto done;
	}
	snprintf(line, sizeof line, "GET %s HTTP/1.0\r\nHost: %s\r\n"
	         "User-Agent: " MN_USER_AGENT "\r\nAccept: */*\r\n"
	         "Connection: close\r\n\r\n", path, host);
	if (!send_all(t, line)) {
		set_err(err, errcap, "the connection failed while asking for ", path);
		goto done;
	}

	/* the reply's head, up to the blank line */
	for (;;) {
		u32 n = 0;
		char *end;

		st = plat_transport_read(t, (u8 *)head + hl, MN_HEAD_MAX - hl, MN_READ_MS, &n);
		if (st == WP_TIMEOUT) {
			set_err(err, errcap, "no answer from ", host);
			goto done;
		}
		if (st != WP_OK) {
			set_err(err, errcap, "the server closed the connection early", 0);
			goto done;
		}
		hl += n;
		head[hl] = 0;
		if ((end = strstr(head, "\r\n\r\n")) != 0 || (end = strstr(head, "\n\n")) != 0) {
			unsigned body = (unsigned)(end - head) + (end[0] == '\r' ? 4 : 2);

			if (sscanf(head, "HTTP/%*d.%*d %d", &status) != 1) {
				set_err(err, errcap, "not an HTTP reply", 0);
				goto done;
			}
			*end = 0;                   /* the head, as a string */
			if (status >= 300 && status < 400) {
				if (!header(head, "Location", location, loccap))
					set_err(err, errcap, "a redirect with nowhere to go", 0);
				goto done;
			}
			if (status != 200) {
				snprintf(err, errcap, "the server answered %d", status);
				goto done;
			}
			if (header(head, "Content-Length", value, sizeof value))
				total = atol(value);
			if (header(head, "Transfer-Encoding", value, sizeof value)
			 && strstr(value, "chunked"))
				chunked = 1;
			if (body < hl) {
				unsigned long rest = hl - body;

				if (chunked) {
					if (!dechunk(&cs, (unsigned char *)head + body, rest, sink, ctx, &got))
						goto stopped;
				} else {
					if (!sink(ctx, (unsigned char *)head + body, rest))
						goto stopped;
					got += rest;
				}
			}
			break;
		}
		if (hl >= MN_HEAD_MAX) {
			set_err(err, errcap, "the reply's header is too long", 0);
			goto done;
		}
	}
	if (progress)
		progress(ctx, got, total);

	/* the body, until the server closes (or Content-Length is met) */
	while (!(total >= 0 && (long)got >= total) && cs.phase != 3) {
		u32 n = 0;

		st = plat_transport_read(t, buf, 2048, MN_READ_MS, &n);
		if (st == WP_TIMEOUT) {
			set_err(err, errcap, "the download stalled", 0);
			goto done;
		}
		if (st != WP_OK)
			break;                      /* closed */
		if (chunked) {
			if (!dechunk(&cs, buf, n, sink, ctx, &got))
				goto stopped;
		} else {
			if (!sink(ctx, buf, n))
				goto stopped;
			got += n;
		}
		if (progress)
			progress(ctx, got, total);
	}
	if (total >= 0 && (long)got < total) {
		snprintf(err, errcap, "the download was cut short (%lu of %ld bytes)", got, total);
		goto done;
	}
	if (chunked && cs.phase != 3) {
		set_err(err, errcap, "the download was cut short", 0);
		goto done;
	}
	ok = 1;
	goto done;
stopped:
	set_err(err, errcap, "stopped", 0);
done:
	if (t)
		plat_transport_close(t);
	free(head);
	free(buf);
	return ok;
}

int mn_http_get(const char *url, mn_sink sink, mn_progress progress, void *ctx,
                char *err, unsigned errcap)
{
	char cur[512], next[512];
	int hop;

	snprintf(cur, sizeof cur, "%s", url);
	for (hop = 0; hop <= MN_REDIRECTS; hop++) {
		err[0] = 0;
		if (get_once(cur, sink, progress, ctx, next, sizeof next, err, errcap))
			return 1;
		if (!next[0])
			return 0;
		if (next[0] == '/') {           /* a path on the same server */
			char host[128];
			unsigned port;
			const char *path;

			if (!split_url(cur, host, sizeof host, &port, &path))
				return 0;
			if (port == 80)
				snprintf(cur, sizeof cur, "http://%s%s", host, next);
			else
				snprintf(cur, sizeof cur, "http://%s:%u%s", host, port, next);
		} else {
			snprintf(cur, sizeof cur, "%s", next);
		}
	}
	set_err(err, errcap, "too many redirects", 0);
	return 0;
}

int mn_buf_sink(void *p, const unsigned char *data, unsigned long n)
{
	mn_buf *b = p;

	if (b->cap && b->len + n > b->cap)
		return 0;
	if (b->len + n + 1 > b->room) {
		unsigned long room = b->room ? b->room : 16384;
		unsigned char *d;

		while (room < b->len + n + 1)
			room *= 2;
		d = realloc(b->data, room);
		if (!d)
			return 0;
		b->data = d;
		b->room = room;
	}
	memcpy(b->data + b->len, data, n);
	b->len += n;
	b->data[b->len] = 0;
	return 1;
}

/* ------------------------------------------------------------ the list */

static void unescape(char *s)
{
	static const struct { const char *e; char c; } ent[] = {
		{ "&amp;", '&' }, { "&quot;", '"' }, { "&lt;", '<' },
		{ "&gt;", '>' }, { "&#39;", '\'' }, { "&apos;", '\'' }
	};
	char *r = s, *w = s;

	while (*r) {
		unsigned k;

		if (*r == '&') {
			for (k = 0; k < sizeof ent / sizeof ent[0]; k++) {
				size_t n = strlen(ent[k].e);

				if (strncmp(r, ent[k].e, n) == 0) {
					*w++ = ent[k].c;
					r += n;
					break;
				}
			}
			if (k < sizeof ent / sizeof ent[0])
				continue;
		}
		*w++ = *r++;
	}
	*w = 0;
}

int mn_export_parse(char *text, unsigned long len, mn_module *out, int max)
{
	char *p, *end, *line;
	int count = 0;

	text[len] = 0;                      /* (mn_buf keeps a spare byte) */
	p = strstr(text, "<pre>");
	end = p ? strstr(p, "</pre>") : 0;
	if (!p || !end)
		return -1;
	p += 5;
	*end = 0;
	for (line = p; line && *line && count < max; ) {
		char *nl = strchr(line, '\n'), *f[11];
		int k;

		if (nl)
			*nl++ = 0;
		f[0] = line;
		for (k = 1; k < 11; k++) {
			char *bar = strchr(f[k - 1], '|');

			if (!bar)
				break;
			*bar = 0;
			f[k] = bar + 1;
		}
		if (k == 11 && f[0][0]) {
			char *cr = strchr(f[10], '\r');

			if (cr)
				*cr = 0;
			for (k = 0; k < 11; k++)
				unescape(f[k]);
			out[count].path = f[0];
			out[count].title = f[2];
			out[count].size_kb = atol(f[3]);
			out[count].author = f[4];
			out[count].levels = f[5];
			out[count].equipment = f[6];
			out[count].hacked = strcmp(f[7], "Hacked") == 0;
			out[count].date = f[9];
			out[count].about = f[10];
			count++;
		}
		line = nl;
	}
	return count;
}

void mn_module_url(const mn_module *m, char *out, unsigned cap)
{
	snprintf(out, cap, "http://" MN_ARCHIVE_HOST "/%s", m->path);
}
