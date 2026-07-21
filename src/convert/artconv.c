/* artconv — FRUA art-container converter core, DOS `HLIB` <-> Mac `GLIB`.
 *
 * Line-for-line port of tools/art_convert.py; that file is the byte-exact-
 * tested REFERENCE and carries the format documentation and the proofs
 * (the method-23 sweep law, the type-128 table, the byte[10]/[11] trap).
 * Comments here cover only what the C shape adds; see the Python for WHY.
 *
 * 68000-safe: every multi-byte field is read/written a byte at a time
 * (entries sit at arbitrary offsets inside the container).
 */
#include <string.h>

#include "artconv.h"

#define HDRSZ 16

static const unsigned char MAGIC_HLIB[4] = { 'H', 'L', 'I', 'B' };
static const unsigned char MAGIC_GLIB[4] = { 'G', 'L', 'I', 'B' };

/* ---- byte-order helpers (be = big-endian flag) --------------------------- */

static unsigned long rd32(const unsigned char *p, int be)
{
	if (be)
		return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16)
		     | ((unsigned long)p[2] << 8) | p[3];
	return ((unsigned long)p[3] << 24) | ((unsigned long)p[2] << 16)
	     | ((unsigned long)p[1] << 8) | p[0];
}

static unsigned rd16(const unsigned char *p, int be)
{
	return be ? (unsigned)((p[0] << 8) | p[1])
		  : (unsigned)((p[1] << 8) | p[0]);
}

static int rd16s(const unsigned char *p, int be)
{
	return (int)((rd16(p, be) ^ 0x8000u) - 0x8000u);
}

static void wr32(unsigned char *p, int be, unsigned long v)
{
	if (be) {
		p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
		p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
	} else {
		p[3] = (unsigned char)(v >> 24); p[2] = (unsigned char)(v >> 16);
		p[1] = (unsigned char)(v >> 8);  p[0] = (unsigned char)v;
	}
}

static void wr16(unsigned char *p, int be, unsigned v)
{
	if (be) {
		p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v;
	} else {
		p[1] = (unsigned char)(v >> 8); p[0] = (unsigned char)v;
	}
}

/* Python floor division (// with a possibly negative numerator: the mono
 * hot-spot rescale oyb = yb*num//den must round toward -inf, not 0). */
static long floordiv(long a, long b)
{
	long q = a / b, r = a % b;
	if (r != 0 && ((r < 0) != (b < 0)))
		q--;
	return q;
}

/* ---- scratch arena ------------------------------------------------------- */

typedef struct {
	unsigned char *base;
	long cap, used;
} arena;

static unsigned char *aalloc(arena *a, long n)
{
	unsigned char *p;
	long need = (n + 1) & ~1L;	/* even sizes keep everything aligned */
	if (n < 0 || a->used > a->cap - need)
		return 0;
	p = a->base + a->used;
	a->used += need;
	return p;
}

/* ---- codecs -------------------------------------------------------------- */

static int swap_flag_byte(int b, int to_mac)
{
	int lo = b & 0x0F, hi = b >> 4;
	if (hi == 0x0)
		return b;
	if (to_mac && hi == 0x1)
		return 0xC0 | lo;
	if (!to_mac && hi == 0xC)
		return 0x10 | lo;
	return -1;
}

static void deplanarize(const unsigned char *px, unsigned char *out,
			long w, long rows)
{
	long p, y, x, k = 0;
	for (p = 0; p < 4; p++)
		for (y = 0; y < rows; y++)
			for (x = p; x < w; x += 4)
				out[y * w + x] = px[k++];
}

static void planarize(const unsigned char *px, unsigned char *out,
		      long w, long rows)
{
	long p, y, x, k = 0;
	for (p = 0; p < 4; p++)
		for (y = 0; y < rows; y++)
			for (x = p; x < w; x += 4)
				out[k++] = px[y * w + x];
}

static void rows_deinterleave(const unsigned char *raw, unsigned char *out,
			      long w, long rows)
{
	long y, p, x, k = 0;
	for (y = 0; y < rows; y++)
		for (p = 0; p < 4; p++)
			for (x = p; x < w; x += 4)
				out[y * w + x] = raw[k++];
}

static void rows_interleave(const unsigned char *lin, unsigned char *out,
			    long w, long rows)
{
	long y, p, x, k = 0;
	for (y = 0; y < rows; y++)
		for (p = 0; p < 4; p++)
			for (x = p; x < w; x += 4)
				out[k++] = lin[y * w + x];
}

/* DRAW18.TXT run codec. `out` must have room for total + 128 bytes: like
 * the Python, a literal/repeat that straddles `total` is written whole and
 * the caller compares the produced length against the expectation.
 * Returns the decoded length, or ARTCONV_ERR_UNSUPPORTED on a repeat with
 * no value byte (the Python dies on the same stream and skips the file). */
static long rle_decode(const unsigned char *data, long dlen,
		       unsigned char *out, long total)
{
	long i = 0, o = 0;
	while (o < total && i < dlen) {
		int x = data[i++];
		if (x < 128) {
			long take = x + 1;
			if (take > dlen - i)
				take = dlen - i;
			memcpy(out + o, data + i, take);
			o += take;
			i += x + 1;
		} else if (x > 128) {
			if (i >= dlen)
				return ARTCONV_ERR_UNSUPPORTED;
			memset(out + o, data[i++], 257 - x);
			o += 257 - x;
		}
	}
	return o;
}

static long rle_encode(const unsigned char *buf, long n,
		       unsigned char *out, long cap)
{
	long o = 0, i = 0, ls = 0;	/* ls..i = pending literal run */

	while (i < n) {
		long j = i, run;
		while (j + 1 < n && buf[j + 1] == buf[i] && j - i + 1 < 128)
			j++;
		run = j - i + 1;
		if (run >= 3) {
			while (ls < i) {		/* flush literals */
				long chunk = i - ls;
				if (chunk > 128)
					chunk = 128;
				if (o + 1 + chunk > cap)
					return ARTCONV_ERR_SPACE;
				out[o++] = (unsigned char)(chunk - 1);
				memcpy(out + o, buf + ls, chunk);
				o += chunk;
				ls += chunk;
			}
			if (o + 2 > cap)
				return ARTCONV_ERR_SPACE;
			out[o++] = (unsigned char)(257 - run);
			out[o++] = buf[i];
			i = j + 1;
			ls = i;
		} else {
			i++;
		}
	}
	while (ls < i) {
		long chunk = i - ls;
		if (chunk > 128)
			chunk = 128;
		if (o + 1 + chunk > cap)
			return ARTCONV_ERR_SPACE;
		out[o++] = (unsigned char)(chunk - 1);
		memcpy(out + o, buf + ls, chunk);
		o += chunk;
		ls += chunk;
	}
	return o;
}

/* SSI's own row packer (the Python's packbits_row; law derived 2026-07-21
 * by re-encoding the Mac release's shipped .CTLs to identity): runs of 3+
 * break a literal, a run of EXACTLY 2 is taken as a run only when the
 * literal accumulator is empty, literals cap at 128. The Mac engine
 * decodes method 18 one row per _UnpackBits call, so each row must be an
 * independently terminated stream; the caller emits per row and handles
 * the trailing even-pad. */
static long pb_flush(const unsigned char *row, long j, long k,
		     unsigned char *out, long o, long cap)
{
	while (k - j > 128) {
		if (o + 129 > cap)
			return ARTCONV_ERR_SPACE;
		out[o++] = 127;
		memcpy(out + o, row + j, 128);
		o += 128;
		j += 128;
	}
	if (k > j) {
		if (o + 1 + (k - j) > cap)
			return ARTCONV_ERR_SPACE;
		out[o++] = (unsigned char)(k - j - 1);
		memcpy(out + o, row + j, k - j);
		o += k - j;
	}
	return o;
}

static long packbits_row(const unsigned char *row, long n,
			 unsigned char *out, long cap)
{
	long o = 0, i = 0, lit = -1;	/* lit < 0: accumulator empty */

	while (i < n) {
		long run = 1;
		while (i + run < n && row[i + run] == row[i] && run < 128)
			run++;
		if (run >= 3 || (run == 2 && lit < 0)) {
			if (lit >= 0) {
				o = pb_flush(row, lit, i, out, o, cap);
				if (o < 0)
					return o;
				lit = -1;
			}
			if (o + 2 > cap)
				return ARTCONV_ERR_SPACE;
			out[o++] = (unsigned char)(257 - run);
			out[o++] = row[i];
			i += run;
		} else {
			if (lit < 0)
				lit = i;
			i++;
			if (i - lit == 128) {
				o = pb_flush(row, lit, i, out, o, cap);
				if (o < 0)
					return o;
				lit = -1;
			}
		}
	}
	if (lit >= 0) {
		o = pb_flush(row, lit, n, out, o, cap);
		if (o < 0)
			return o;
	}
	return o;
}

/* Method-23 skip/literal stream -> (px, mask). px/mask must be zeroed
 * w*rows buffers. Returns the number of payload bytes consumed. */
static long m23_decode(const unsigned char *payload, long plen,
		       long w, long rows, int planar,
		       unsigned char *px, unsigned char *mask)
{
	long i = 0, sweep, y;
	int nsweep = planar ? 4 : 1;

	for (sweep = 0; sweep < nsweep; sweep++) {
		for (y = 0; y < rows; y++) {
			long x = planar ? sweep + 1 : 0;
			for (;;) {
				int v;
				if (i >= plen)
					return i;
				v = payload[i++];
				if (v == 0)
					break;
				if (v >= 128) {
					x += planar ? 4L * (256 - v)
						    : 257L - v;
					continue;
				}
				while (v--) {
					if (i >= plen)
						return i;
					if (x >= 0 && x < w) {
						px[y * w + x] = payload[i];
						mask[y * w + x] = 1;
					}
					i++;
					x += planar ? 4 : 1;
				}
			}
		}
	}
	return i;
}

/* (px, mask) -> a method-23 stream. The linear layout cannot express
 * 1-pixel transparent gaps; px/mask are MUTATED to pre-fill them (they
 * are scratch buffers in every caller). Returns the stream length. */
static long m23_encode(unsigned char *px, unsigned char *mask,
		       long w, long rows, int planar,
		       unsigned char *out, long cap)
{
	long o = 0, sweep, y;
	int nsweep = planar ? 4 : 1;
	long step = planar ? 4 : 1;

	if (!planar) {
		for (y = 0; y < rows; y++) {
			long row = y * w, x, first = -1, last = -1;
			for (x = 0; x < w; x++)
				if (mask[row + x]) {
					if (first < 0)
						first = x;
					last = x;
				}
			if (first < 0)
				continue;
			for (x = first + 1; x < last; x++)
				if (!mask[row + x] && mask[row + x - 1]
				    && mask[row + x + 1]) {
					mask[row + x] = 1;
					px[row + x] = px[row + x - 1];
				}
			if (first == 1) {
				mask[row] = 1;
				px[row] = px[row + 1];
			}
		}
	}

	for (sweep = 0; sweep < nsweep; sweep++) {
		for (y = 0; y < rows; y++) {
			long row = y * w;
			long x0 = planar ? sweep + 1 : 0;
			long ncols = x0 < w ? (w - x0 + step - 1) / step : 0;
			long last = -1, k;
			for (k = 0; k < ncols; k++)
				if (mask[row + x0 + k * step])
					last = k;
			k = 0;
			while (k <= last) {
				long x = x0 + k * step, run;
				if (!mask[row + x]) {
					run = 0;
					while (k + run <= last
					       && !mask[row + x0 + (k + run) * step])
						run++;
					if (planar) {
						while (run) {
							long take = run < 128 ? run : 128;
							if (o >= cap)
								return ARTCONV_ERR_SPACE;
							out[o++] = (unsigned char)(256 - take);
							k += take;
							run -= take;
						}
						continue;
					}
					while (run) {
						long take = run < 129 ? run : 129;
						if (run - take == 1)
							take--;
						if (take < 2)
							take = run < 2 ? run : 2;
						if (o >= cap)
							return ARTCONV_ERR_SPACE;
						out[o++] = (unsigned char)(257 - take);
						k += take;
						run -= take;
					}
					continue;
				}
				run = 0;
				while (k + run <= last
				       && mask[row + x0 + (k + run) * step]
				       && run < 127)
					run++;
				if (o + 1 + run > cap)
					return ARTCONV_ERR_SPACE;
				out[o++] = (unsigned char)run;
				{
					long j;
					for (j = 0; j < run; j++)
						out[o++] = px[row + x0 + (k + j) * step];
				}
				k += run;
			}
			if (o >= cap)
				return ARTCONV_ERR_SPACE;
			out[o++] = 0;
		}
	}
	return o;
}

/* Byte-swap a flat u16 array (odd trailing byte passes through). */
static void swap_u16_array(const unsigned char *src, unsigned char *dst,
			   long n)
{
	long i;
	for (i = 0; i + 1 < n; i += 2) {
		dst[i] = src[i + 1];
		dst[i + 1] = src[i];
	}
	if (n & 1)
		dst[n - 1] = src[n - 1];
}

/* ---- one image entry ----------------------------------------------------- */

static long conv_entry(const unsigned char *e, long elen,
		       unsigned char *out, long cap, arena *A, int to_mac)
{
	int src_be = !to_mac, dst_be = to_mac;
	unsigned height;
	int voff, hoff, w4, method, dos_method, nf;
	long w, plen, total, body;
	const unsigned char *payload;

	if (elen < 8) {
		if (elen > cap)
			return ARTCONV_ERR_SPACE;
		memcpy(out, e, elen);
		return elen;
	}
	height = rd16(e, src_be);
	voff = rd16s(e + 2, src_be);
	hoff = rd16s(e + 4, src_be);
	w4 = e[6];
	method = e[7];
	payload = e + 8;
	plen = elen - 8;

	if (8 + plen > cap)
		return ARTCONV_ERR_SPACE;	/* every path emits <= elen + small */

	/* Type-128 table (CBODY/COMSPR item 0): [6:8] is ONE u16, payload
	 * is a u16 array; everything byte-swaps. */
	if ((to_mac && w4 == 0x80 && method == 0x00)
	    || (!to_mac && w4 == 0x00 && method == 0x80)) {
		wr16(out, dst_be, height);
		wr16(out + 2, dst_be, (unsigned)voff & 0xFFFFu);
		wr16(out + 4, dst_be, (unsigned)hoff & 0xFFFFu);
		out[6] = to_mac ? 0x00 : 0x80;
		out[7] = to_mac ? 0x80 : 0x00;
		swap_u16_array(payload, out + 8, plen);
		return 8 + plen;
	}

	w = (long)w4 * (to_mac ? 4 : 8);
	dos_method = to_mac ? method : ((method & 0x0F) | 0x10);

	wr16(out, dst_be, height);
	wr16(out + 2, dst_be, (unsigned)voff & 0xFFFFu);
	wr16(out + 4, dst_be, (unsigned)hoff & 0xFFFFu);

	if (dos_method == 18) {			/* shared run codec */
		unsigned char *raw, *shuf;
		if (!w || !height)
			return ARTCONV_ERR_UNSUPPORTED;
		if (to_mac && (w % 8))
			return ARTCONV_ERR_UNSUPPORTED;
		total = w * height;
		raw = aalloc(A, total + 129);
		if (!raw)
			return ARTCONV_ERR_SPACE;
		if (rle_decode(payload, plen, raw, total) != total)
			return ARTCONV_ERR_UNSUPPORTED;
		shuf = aalloc(A, total);
		if (!shuf)
			return ARTCONV_ERR_SPACE;
		if (to_mac)
			rows_deinterleave(raw, shuf, w, height);
		else
			rows_interleave(raw, shuf, w, height);
		{
			long y, o = 0, r;
			for (y = 0; y < height; y++) {
				r = packbits_row(shuf + y * w, w,
						 out + 8 + o, cap - 8 - o);
				if (r < 0)
					return r;
				o += r;
			}
			if (o % 2) {	/* even-pad (SSI pads garbage, we 0) */
				if (8 + o + 1 > cap)
					return ARTCONV_ERR_SPACE;
				out[8 + o++] = 0;
			}
			body = o;
		}
	} else if (dos_method == 23) {		/* compressed transparent */
		unsigned char *px, *mask;
		long used;
		if (!w || !height)
			return ARTCONV_ERR_UNSUPPORTED;
		if (to_mac && (w % 8))
			return ARTCONV_ERR_UNSUPPORTED;
		total = w * height;
		px = aalloc(A, total);
		mask = aalloc(A, total);
		if (!px || !mask)
			return ARTCONV_ERR_SPACE;
		memset(px, 0, total);
		memset(mask, 0, total);
		used = m23_decode(payload, plen, w, height, to_mac, px, mask);
		if (used < plen - 1)	/* POR's FRAME.TLB carries 1 pad byte */
			return ARTCONV_ERR_UNSUPPORTED;
		body = m23_encode(px, mask, w, height, !to_mac, out + 8, cap - 8);
		if (body >= 0 && to_mac && (body % 2)) {
			/* SSI even-pads odd method-23 streams (FRAME 22-24) */
			if (8 + body + 1 > cap)
				return ARTCONV_ERR_SPACE;
			out[8 + body++] = 0;
		}
	} else if (dos_method == 25) {
		/* frame/border table: 6-byte records (byte, byte, u16, u16);
		 * the two u16s swap (base FRAME 17-20; POR's are all-zero) */
		long r6;
		memcpy(out + 8, payload, plen);
		for (r6 = 0; r6 + 5 < plen; r6 += 6) {
			unsigned char t;
			t = out[8 + r6 + 2];
			out[8 + r6 + 2] = out[8 + r6 + 3];
			out[8 + r6 + 3] = t;
			t = out[8 + r6 + 4];
			out[8 + r6 + 4] = out[8 + r6 + 5];
			out[8 + r6 + 5] = t;
		}
		body = plen;
	} else if (dos_method == 16 || dos_method == 17 || dos_method == 21) {
		if (w && height && 2 * w * height == plen) {
			/* AND/OR mask pair: two planes, shuffled independently */
			long half = w * height;
			if (to_mac && (w % 8))
				return ARTCONV_ERR_UNSUPPORTED;
			if (to_mac) {
				deplanarize(payload, out + 8, w, height);
				deplanarize(payload + half, out + 8 + half, w, height);
			} else {
				planarize(payload, out + 8, w, height);
				planarize(payload + half, out + 8 + half, w, height);
			}
			body = plen;
		} else if (w && height && w * height == plen) {
			if (to_mac && (w % 8))
				return ARTCONV_ERR_UNSUPPORTED;
			if (to_mac)
				deplanarize(payload, out + 8, w, height);
			else
				planarize(payload, out + 8, w, height);
			body = plen;
		} else {
			return ARTCONV_ERR_UNSUPPORTED;
		}
	} else if ((method & 0x0F) == 8) {	/* colour table */
		memcpy(out + 8, payload, plen);
		nf = swap_flag_byte(method, to_mac);
		if (nf < 0)
			return ARTCONV_ERR_UNSUPPORTED;
		out[6] = (unsigned char)w4;
		out[7] = (unsigned char)nf;
		return 8 + plen;
	} else if (method == 0x00 && (elen % 2) == 0) {
		/* WORD-TABLE entry (CPIC entry 0, the portrait directory):
		 * a u16 index, not an image — the whole entry, header words
		 * included, converts by swapping every u16. */
		swap_u16_array(e, out, elen);
		return elen;
	} else {
		return ARTCONV_ERR_UNSUPPORTED;
	}

	if (body < 0)
		return body;
	nf = swap_flag_byte(method, to_mac);
	if (nf < 0)
		return ARTCONV_ERR_UNSUPPORTED;
	out[6] = (unsigned char)(w / (to_mac ? 8 : 4));
	out[7] = (unsigned char)nf;
	return 8 + body;
}

/* ---- whole container ----------------------------------------------------- */

static long conv_container(const unsigned char *src, long n,
			   unsigned char *dst, long cap, arena *A,
			   const unsigned char *to)
{
	int src_be, to_mac, nested;
	long count, tab, pos, i;

	if (n < HDRSZ)
		return ARTCONV_ERR_BAD;
	if (!memcmp(src, MAGIC_GLIB, 4))
		src_be = 1;
	else if (!memcmp(src, MAGIC_HLIB, 4))
		src_be = 0;
	else
		return ARTCONV_ERR_BAD;
	if (!memcmp(src, to, 4)) {		/* already in the target format */
		if (n > cap)
			return ARTCONV_ERR_SPACE;
		memcpy(dst, src, n);
		return n;
	}
	to_mac = !memcmp(to, MAGIC_GLIB, 4);

	count = rd16(src + 8, src_be);
	tab = HDRSZ + 4 * (count + 1);
	if (tab > n || tab > cap)
		return ARTCONV_ERR_BAD;

	nested = !memcmp(src + 12, MAGIC_HLIB, 4)
	      || !memcmp(src + 12, MAGIC_GLIB, 4);

	pos = tab;
	for (i = 0; i < count; i++) {
		long off = (long)rd32(src + HDRSZ + 4 * i, src_be);
		long end = (long)rd32(src + HDRSZ + 4 * (i + 1), src_be);
		long elen, save, r;
		const unsigned char *e;
		if (off < 0 || end < off || end > n)
			return ARTCONV_ERR_BAD;
		e = src + off;
		elen = end - off;
		save = A->used;
		if (nested) {
			if (elen >= 4 && (!memcmp(e, MAGIC_HLIB, 4)
					  || !memcmp(e, MAGIC_GLIB, 4))) {
				r = conv_container(e, elen, dst + pos,
						   cap - pos, A, to);
			} else {
				if (elen > cap - pos)
					return ARTCONV_ERR_SPACE;
				swap_u16_array(e, dst + pos, elen);
				r = elen;
			}
		} else if (elen >= 4 && (!memcmp(e, MAGIC_HLIB, 4)
					 || !memcmp(e, MAGIC_GLIB, 4))) {
			/* PIC*/ /* BIGPIC event pictures: a complete sub-library
			 * inside an image-tagged outer container — only the
			 * entry's own magic says so */
			r = conv_container(e, elen, dst + pos, cap - pos, A, to);
		} else {
			r = conv_entry(e, elen, dst + pos, cap - pos, A, to_mac);
		}
		A->used = save;
		if (r < 0)
			return r;
		wr32(dst + HDRSZ + 4 * i, !src_be, (unsigned long)pos);
		pos += r;
	}
	wr32(dst + HDRSZ + 4 * count, !src_be, (unsigned long)pos);

	memcpy(dst, to, 4);
	wr32(dst + 4, !src_be, (unsigned long)pos);
	wr16(dst + 8, !src_be, (unsigned)count);
	dst[10] = src[10];			/* two BYTES — never swapped */
	dst[11] = src[11];
	if (nested)
		memcpy(dst + 12, to, 4);	/* nested library names its magic */
	else
		memcpy(dst + 12, src + 12, 4);
	return pos;
}

long artconv_convert(const unsigned char *src, long srclen,
		     unsigned char *dst, long dstcap,
		     unsigned char *scratch, long scratchcap)
{
	arena A;
	const unsigned char *to;

	if (srclen < 4)
		return ARTCONV_ERR_BAD;
	if (!memcmp(src, MAGIC_HLIB, 4))
		to = MAGIC_GLIB;
	else if (!memcmp(src, MAGIC_GLIB, 4))
		to = MAGIC_HLIB;
	else
		return ARTCONV_ERR_BAD;
	A.base = scratch;
	A.cap = scratchcap;
	A.used = 0;
	return conv_container(src, srclen, dst, dstcap, &A, to);
}

/* ---- mono (.tlb) synthesis ----------------------------------------------- */

/* The canonical mono palette item (16-colour block, flags 0x98). */
static const unsigned char MONO_PAL_ITEM[56] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x98,
	0x00, 0x00, 0x00,  0xff, 0xff, 0xff,  0x00, 0xab, 0x00,
	0x00, 0xab, 0xab,  0xab, 0x00, 0x00,  0xab, 0x00, 0xab,
	0xab, 0x57, 0x00,  0xab, 0xab, 0xab,  0x57, 0x57, 0x57,
	0x57, 0x57, 0xff,  0x57, 0xff, 0x57,  0x57, 0xff, 0xff,
	0xff, 0x57, 0x57,  0xff, 0x57, 0xff,  0xff, 0xff, 0x57,
	0xff, 0xff, 0xff
};

static const unsigned char BAYER4[4][4] = {
	{  0,  8,  2, 10 },
	{ 12,  4, 14,  6 },
	{  3, 11,  1,  9 },
	{ 15,  7, 13,  5 }
};

struct family {
	const char *stem;
	int num, den, mode;
};

static const struct family FAMILIES[] = {
	{ "8x8d", 2, 1, ARTCONV_MONO_PLANAR },
	{ "back", 2, 1, ARTCONV_MONO_PACK },
	{ "pic",  2, 1, ARTCONV_MONO_PACK },	/* pica..picf */
	{ "cpic", 4, 3, ARTCONV_MONO_PLANAR },
	{ "spri", 4, 3, ARTCONV_MONO_PLANAR },
	{ "bigp", 3, 2, ARTCONV_MONO_PACK },
	{ "dung", 4, 3, ARTCONV_MONO_PLANAR },
	{ "wild", 4, 3, ARTCONV_MONO_PLANAR },
	{ "cbod", 4, 3, ARTCONV_MONO_PLANAR },
	{ "coms", 4, 3, ARTCONV_MONO_PLANAR },
	{ 0, 0, 0, 0 }
};

static int lower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int artconv_mono_family(const char *filename, int *num, int *den, int *mode)
{
	const char *b = filename, *p;
	const struct family *f;

	for (p = filename; *p; p++)
		if (*p == '/' || *p == '\\' || *p == ':')
			b = p + 1;
	for (f = FAMILIES; f->stem; f++) {
		const char *s = f->stem, *q = b;
		while (*s && lower(*q) == *s) {
			s++;
			q++;
		}
		if (!*s) {
			*num = f->num;
			*den = f->den;
			*mode = f->mode;
			return 1;
		}
	}
	return 0;
}

/* 256-entry luminance table from the container's palette item, via the
 * engine's g_dsp_ink classing: (2r+5g+b)>>3. */
static void palette_lum(const unsigned char *glib, long n, long count,
			unsigned char *lum)
{
	long i;
	unsigned char pal[256][3];

	memset(pal, 0, sizeof pal);
	pal[15][0] = pal[15][1] = pal[15][2] = 255;
	for (i = 0; i < count; i++) {
		long off = (long)rd32(glib + HDRSZ + 4 * i, 1);
		long end = (long)rd32(glib + HDRSZ + 4 * (i + 1), 1);
		const unsigned char *e = glib + off;
		long elen = end - off, k, first, cnt, navail;
		if (off < 0 || end < off || end > n || elen < 8)
			continue;
		if ((e[7] & 0x0F) != 8)
			continue;
		first = rd16s(e + 2, 1);
		cnt = rd16s(e + 4, 1);
		navail = (elen - 8) / 3;
		if (cnt > navail)
			cnt = navail;
		for (k = 0; k < cnt; k++) {
			long idx = first + k;
			if (idx >= 0 && idx < 256) {
				pal[idx][0] = e[8 + k * 3];
				pal[idx][1] = e[8 + k * 3 + 1];
				pal[idx][2] = e[8 + k * 3 + 2];
			}
		}
	}
	for (i = 0; i < 256; i++)
		lum[i] = (unsigned char)((2 * pal[i][0] + 5 * pal[i][1]
					  + pal[i][2]) >> 3);
}

static long synth_item(const unsigned char *e, long elen,
		       unsigned char *out, long cap, arena *A,
		       const unsigned char *lum, int num, int den, int mode)
{
	unsigned rows;
	int yb, xb, w4, method, lo, flags;
	long w, plen, ow, oh, owb, x, y, body;
	const unsigned char *payload, *px;
	const unsigned char *m23mask = 0;
	unsigned char *data, *mask;
	unsigned short *sxl;
	int has_mask = 0;

	rows = rd16(e, 1);
	yb = rd16s(e + 2, 1);
	xb = rd16s(e + 4, 1);
	w4 = e[6];
	method = e[7];
	w = (long)w4 * 8;
	payload = e + 8;
	plen = elen - 8;

	if (w == 0 || rows == 0)
		goto verbatim;			/* degenerate/stub slot */
	lo = method & 0x0F;
	if (lo == 9)
		goto verbatim;			/* frame table: not pixels */
	if (w4 == 0x00 && method == 0x80)
		goto verbatim;			/* type-128 composite table */

	if (lo == 2) {				/* PackBits 8bpp */
		unsigned char *raw = aalloc(A, w * rows + 129);
		if (!raw)
			return ARTCONV_ERR_SPACE;
		if (rle_decode(payload, plen, raw, w * rows) < w * rows)
			return ARTCONV_ERR_UNSUPPORTED;
		px = raw;
	} else if (lo == 7) {			/* method 23 (linear, in .ctl) */
		unsigned char *dpx = aalloc(A, w * rows);
		unsigned char *dmask = aalloc(A, w * rows);
		if (!dpx || !dmask)
			return ARTCONV_ERR_SPACE;
		memset(dpx, 0, w * rows);
		memset(dmask, 0, w * rows);
		if (m23_decode(payload, plen, w, rows, 0, dpx, dmask) < plen - 1)
			return ARTCONV_ERR_UNSUPPORTED;
		px = dpx;
		m23mask = dmask;
	} else if (w * rows == plen) {		/* plain linear 8bpp */
		px = payload;
	} else if (2 * w * rows == plen) {	/* AND/OR mask pair */
		unsigned char *dmask = aalloc(A, w * rows);
		long i;
		if (!dmask)
			return ARTCONV_ERR_SPACE;
		px = payload + w * rows;
		for (i = 0; i < w * rows; i++)
			dmask[i] = (payload[i] == 0xFF) ? 0 : 1;
		m23mask = dmask;
	} else {
		return ARTCONV_ERR_UNSUPPORTED;
	}

	ow = (w * num + den - 1) / den;
	oh = ((long)rows * num + den - 1) / den;
	owb = (ow + 7) / 8;

	data = aalloc(A, owb * oh);
	mask = aalloc(A, owb * oh);
	sxl = (unsigned short *)aalloc(A, ow * 2);
	if (!data || !mask || !sxl)
		return ARTCONV_ERR_SPACE;
	memset(data, 0, owb * oh);
	memset(mask, 0, owb * oh);
	for (x = 0; x < ow; x++) {
		long sx = x * den / num;
		if (sx > w - 1)
			sx = w - 1;
		sxl[x] = (unsigned short)sx;
	}

	for (y = 0; y < oh; y++) {
		long sy = y * den / num;
		const unsigned char *srow, *mrow;
		unsigned char *drow = data + y * owb;
		unsigned char *krow = mask + y * owb;
		const unsigned char *thr = BAYER4[y & 3];
		if (sy > rows - 1)
			sy = rows - 1;
		srow = px + sy * w;
		mrow = m23mask ? m23mask + sy * w : 0;
		for (x = 0; x < ow; x++) {
			long sx = sxl[x];
			int p = srow[sx];
			int bit = 0x80 >> (x & 7);
			int transparent = mrow ? !mrow[sx] : (p == 255);
			if (transparent && mode == ARTCONV_MONO_PLANAR) {
				krow[x >> 3] |= bit;
				has_mask = 1;
				continue;
			}
			if (lum[p] >= 64 + thr[x & 3] * 6)
				drow[x >> 3] |= bit;
		}
	}

	if (mode == ARTCONV_MONO_PACK) {
		/* row-aligned PackBits: the engine decoder unpacks per row */
		long r;
		body = 0;
		for (r = 0; r < oh; r++) {
			long n = rle_encode(data + r * owb, owb,
					    out + 8 + body, cap - 8 - body);
			if (n < 0)
				return n;
			body += n;
		}
		flags = 0x92;
	} else if (has_mask) {
		if (8 + 2 * owb * oh > cap)
			return ARTCONV_ERR_SPACE;
		memcpy(out + 8, mask, owb * oh);	/* plane A = keep */
		memcpy(out + 8 + owb * oh, data, owb * oh);
		body = 2 * owb * oh;
		flags = 0x91;
	} else {
		if (8 + owb * oh > cap)
			return ARTCONV_ERR_SPACE;
		memcpy(out + 8, data, owb * oh);
		body = owb * oh;
		flags = 0x90;
	}

	wr16(out, 1, (unsigned)oh);
	wr16(out + 2, 1, (unsigned)floordiv((long)yb * num, den) & 0xFFFFu);
	wr16(out + 4, 1, (unsigned)floordiv((long)xb * num, den) & 0xFFFFu);
	out[6] = (unsigned char)owb;
	out[7] = (unsigned char)flags;
	return 8 + body;

verbatim:
	if (elen > cap)
		return ARTCONV_ERR_SPACE;
	memcpy(out, e, elen);
	return elen;
}

static long mono_container(const unsigned char *glib, long n,
			   unsigned char *dst, long cap, arena *A,
			   int num, int den, int mode)
{
	long count, tab, pos, i;
	unsigned char *lum;

	if (n < HDRSZ || memcmp(glib, MAGIC_GLIB, 4))
		return ARTCONV_ERR_BAD;
	count = rd16(glib + 8, 1);
	tab = HDRSZ + 4 * (count + 1);
	if (tab > n || tab > cap)
		return ARTCONV_ERR_BAD;

	lum = aalloc(A, 256);
	if (!lum)
		return ARTCONV_ERR_SPACE;
	palette_lum(glib, n, count, lum);

	pos = tab;
	for (i = 0; i < count; i++) {
		long off = (long)rd32(glib + HDRSZ + 4 * i, 1);
		long end = (long)rd32(glib + HDRSZ + 4 * (i + 1), 1);
		const unsigned char *e = glib + off;
		long elen = end - off, save, r;
		if (off < 0 || end < off || end > n)
			return ARTCONV_ERR_BAD;
		save = A->used;
		if (elen >= 4 && !memcmp(e, MAGIC_GLIB, 4)) {
			r = mono_container(e, elen, dst + pos, cap - pos, A,
					   num, den, mode);
		} else if (elen >= 8 && (e[7] & 0x0F) == 8) {
			if ((long)sizeof MONO_PAL_ITEM > cap - pos)
				return ARTCONV_ERR_SPACE;
			memcpy(dst + pos, MONO_PAL_ITEM, sizeof MONO_PAL_ITEM);
			r = sizeof MONO_PAL_ITEM;
		} else if (elen < 8) {
			if (elen > cap - pos)
				return ARTCONV_ERR_SPACE;
			memcpy(dst + pos, e, elen);
			r = elen;
		} else {
			r = synth_item(e, elen, dst + pos, cap - pos, A,
				       lum, num, den, mode);
		}
		A->used = save;			/* lum sits below save; kept */
		if (r < 0)
			return r;
		wr32(dst + HDRSZ + 4 * i, 1, (unsigned long)pos);
		pos += r;
	}
	wr32(dst + HDRSZ + 4 * count, 1, (unsigned long)pos);

	memcpy(dst, MAGIC_GLIB, 4);
	wr32(dst + 4, 1, (unsigned long)pos);
	wr16(dst + 8, 1, (unsigned)count);
	dst[10] = glib[10];
	dst[11] = glib[11];
	memcpy(dst + 12, glib + 12, 4);
	return pos;
}

long artconv_mono_synth(const unsigned char *glib, long len,
			unsigned char *dst, long dstcap,
			unsigned char *scratch, long scratchcap,
			int num, int den, int mode)
{
	arena A;
	long r, save;

	A.base = scratch;
	A.cap = scratchcap;
	A.used = 0;
	save = A.used;
	r = mono_container(glib, len, dst, dstcap, &A, num, den, mode);
	A.used = save;
	return r;
}
