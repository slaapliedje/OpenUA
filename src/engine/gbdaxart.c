/*
 * gbdaxart.c — .DAX pictures as GLIB picture libraries. See gbdaxart.h;
 * tools/gbdaxart.py is the reference, tests/test_gbdaxart.py holds the two
 * together byte for byte.
 */
#include <string.h>

#include "gbdaxart.h"

#define BASE 32                 /* the CLUT entry EGA colour 0 goes to */

static const unsigned char k_ega[16][3] = {
	{ 0, 0, 0 }, { 0, 0, 170 }, { 0, 170, 0 }, { 0, 170, 170 },
	{ 170, 0, 0 }, { 170, 0, 170 }, { 170, 85, 0 }, { 170, 170, 170 },
	{ 85, 85, 85 }, { 85, 85, 255 }, { 85, 255, 85 }, { 85, 255, 255 },
	{ 255, 85, 85 }, { 255, 85, 255 }, { 255, 255, 85 }, { 255, 255, 255 }
};

typedef struct { unsigned char *out; long max, n; } wbuf;

static void put(wbuf *w, unsigned char b)
{
	if (w->out != NULL && w->n < w->max)
		w->out[w->n] = b;
	w->n++;
}

static void put16(wbuf *w, unsigned v) { put(w, (unsigned char)(v >> 8)); put(w, (unsigned char)v); }

static void put32(wbuf *w, unsigned long v)
{
	put16(w, (unsigned)(v >> 16));
	put16(w, (unsigned)v);
}

/* art_convert.packbits_row over one row of `w` pixels (BASE + index):
 * runs of 3+ break a literal, a run of 2 only when no literal is open,
 * literals cap at 128 */
static unsigned char pix(const unsigned char *p, long i)
{
	unsigned char b = p[i >> 1];

	return (unsigned char)(BASE + ((i & 1) ? (b & 15) : (b >> 4)));
}

static void packrow(wbuf *w, const unsigned char *p, long w0, long n)
{
	long i = 0, lit = -1;

	while (i < n) {
		long run = 1;

		while (i + run < n && pix(p, w0 + i + run) == pix(p, w0 + i) && run < 128)
			run++;
		if (run >= 3 || (run == 2 && lit < 0)) {
			if (lit >= 0) {         /* flush the literal */
				long j;

				for (j = lit; i - j > 128; j += 128) {
					long k;

					put(w, 127);
					for (k = 0; k < 128; k++)
						put(w, pix(p, w0 + j + k));
				}
				if (i > j) {
					long k;

					put(w, (unsigned char)(i - j - 1));
					for (k = j; k < i; k++)
						put(w, pix(p, w0 + k));
				}
				lit = -1;
			}
			put(w, (unsigned char)(257 - run));
			put(w, pix(p, w0 + i));
			i += run;
		} else {
			if (lit < 0)
				lit = i;
			i++;
			if (i - lit == 128) {
				long k;

				put(w, 127);
				for (k = lit; k < i; k++)
					put(w, pix(p, w0 + k));
				lit = -1;
			}
		}
	}
	if (lit >= 0) {
		long j, k;

		for (j = lit; n - j > 128; j += 128) {
			put(w, 127);
			for (k = 0; k < 128; k++)
				put(w, pix(p, w0 + j + k));
		}
		if (n > j) {
			put(w, (unsigned char)(n - j - 1));
			for (k = j; k < n; k++)
				put(w, pix(p, w0 + k));
		}
	}
}

typedef struct { long h, w, at; } frame;

/* The frames of a block: their geometry and where their pixels are. */
static int frames(const unsigned char *b, long len, frame *f, int max)
{
	long p = 1;
	int  k, n;

	if (len >= 17) {                        /* one plain frame: a big picture */
		long h = b[0] | b[1] << 8, w8 = b[2] | b[3] << 8;

		if (b[8] == 1 && h && w8 && len == 17 + h * w8 * 4) {
			f[0].h = h;
			f[0].w = 8 * w8;
			f[0].at = 17;
			return 1;
		}
	}
	if (len < 1)
		return GBDA_EFRAME;
	n = b[0];
	for (k = 0; k < n; k++) {
		long h, w;

		if (k >= max || p + 21 > len)
			return GBDA_EFRAME;
		h = b[p + 4] | b[p + 5] << 8;
		w = 8L * (b[p + 6] | b[p + 7] << 8);
		if (b[p + 12] != 1 || !h || !w || p + 21 + h * w / 2 > len)
			return GBDA_EFRAME;
		f[k].h = h;
		f[k].w = w;
		f[k].at = p + 21;
		p += 21 + h * w / 2;
	}
	return p == len ? n : GBDA_EFRAME;
}

long gbda_picture(const unsigned char *block, long len,
                  unsigned char *out, long max)
{
	frame    f[64];
	long     size[65], k, y, off;
	int      n = frames(block, len, f, 64), i;
	wbuf     w;

	if (n < 0)
		return n;
	/* the entries' sizes: the palette, then each frame measured */
	size[0] = 8 + 48;
	for (i = 0; i < n; i++) {
		w.out = NULL, w.max = 0, w.n = 0;
		for (y = 0; y < f[i].h; y++)
			packrow(&w, block + f[i].at, y * f[i].w, f[i].w);
		size[i + 1] = 8 + w.n + (w.n & 1);
	}
	w.out = out, w.max = max, w.n = 0;
	put(&w, 'G'); put(&w, 'L'); put(&w, 'I'); put(&w, 'B');
	off = GBDA_HDR + 4L * (n + 2);
	for (k = 0; k <= n; k++)
		off += size[k];
	put32(&w, (unsigned long)off);
	put16(&w, (unsigned)(n + 1));
	put(&w, 0);
	put(&w, 0);
	put(&w, 'T'); put(&w, 'I'); put(&w, 'L'); put(&w, 'E');
	off = GBDA_HDR + 4L * (n + 2);
	for (k = 0; k <= n; k++) {
		put32(&w, (unsigned long)off);
		off += size[k];
	}
	put32(&w, (unsigned long)off);
	put16(&w, 5);                           /* the palette */
	put16(&w, BASE);
	put16(&w, 16);
	put(&w, 0);
	put(&w, 0xc8);
	for (k = 0; k < 16; k++) {
		put(&w, k_ega[k][0]);
		put(&w, k_ega[k][1]);
		put(&w, k_ega[k][2]);
	}
	for (i = 0; i < n; i++) {               /* the frames */
		long start;

		put16(&w, (unsigned)f[i].h);
		put16(&w, 0);
		put16(&w, 0);
		put(&w, (unsigned char)(f[i].w / 8));
		put(&w, 0xc2);
		start = w.n;
		for (y = 0; y < f[i].h; y++)
			packrow(&w, block + f[i].at, y * f[i].w, f[i].w);
		if ((w.n - start) & 1)
			put(&w, 0);
	}
	if (out != NULL && w.n > max)
		return GBDA_EROOM;
	return w.n;
}

long gbda_head(const unsigned *id, const long *size, unsigned n, unsigned shift,
               unsigned char *out)
{
	wbuf     w;
	long     off, tab = 2 + 4L * n;
	unsigned k;

	w.out = out, w.max = 0x7fffffffL, w.n = 0;
	put(&w, 'G'); put(&w, 'L'); put(&w, 'I'); put(&w, 'B');
	off = GBDA_HDR + 4L * (n + 2) + tab;
	for (k = 0; k < n; k++)
		off += size[k];
	put32(&w, (unsigned long)off);
	put16(&w, n + 1);
	put(&w, 0);
	put(&w, 1);                             /* an id table */
	put(&w, 'G'); put(&w, 'L'); put(&w, 'I'); put(&w, 'B');
	off = GBDA_HDR + 4L * (n + 2);
	put32(&w, (unsigned long)off);
	off += tab;
	for (k = 0; k < n; k++) {
		put32(&w, (unsigned long)off);
		off += size[k];
	}
	put32(&w, (unsigned long)off);
	put16(&w, n);
	for (k = 0; k < n; k++) {
		put16(&w, id[k] + shift);
		put16(&w, k + 1);
	}
	return w.n;
}
