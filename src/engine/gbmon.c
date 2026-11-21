/*
 * gbmon.c — a Gold Box game's monsters read as the game keeps them
 * (ADR-0026 step (f)). See gbmon.h; tools/gbmon.py is the reference.
 */
#include <string.h>

#include "gbmon.h"

#define ITEM_KEY 10             /* leading bytes that identify an entry      */
#define MEMBERS 512             /* library members we can index              */

static unsigned rd16(const unsigned char *p, int big)
{
	return big ? (unsigned)p[0] << 8 | p[1] : p[0] | (unsigned)p[1] << 8;
}

static unsigned long rd32(const unsigned char *p, int big)
{
	return big
	    ? (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16
	      | (unsigned long)p[2] << 8 | p[3]
	    : p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16
	      | (unsigned long)p[3] << 24;
}

long gbm_member(gbr_read_fn rd, void *ctx, int id, unsigned char *out, long max)
{
	static unsigned char buf[4 * (MEMBERS + 1)];
	static unsigned char packed[2 * GBM_MAX];
	unsigned long off0, off1, at, end;
	unsigned      n, k, i, index = 0;
	long          len, size, o, p;
	int           big;

	if (rd(ctx, 0, buf, 16) != 16)
		return GBM_EREAD;
	if (memcmp(buf, "HLIB", 4) == 0)
		big = 0;
	else if (memcmp(buf, "GLIB", 4) == 0)
		big = 1;
	else
		return GBM_ENOTLIB;
	n = rd16(buf + 8, big);
	if (n < 1 || n > MEMBERS)
		return GBM_ENOTLIB;
	if (rd(ctx, 16, buf, 4 * ((long)n + 1)) != 4 * ((long)n + 1))
		return GBM_EREAD;
	off0 = rd32(buf, big);
	off1 = rd32(buf + 4, big);

	/* member 0: the id table, {count, count x (id, index)} */
	{
		static unsigned char table[2 + 4 * MEMBERS];
		long tl = (long)(off1 - off0);

		if (off1 < off0 + 2 || tl > (long)sizeof table)
			return GBM_ENOTLIB;
		if (rd(ctx, (long)off0, table, tl) != tl)
			return GBM_EREAD;
		k = rd16(table, big);
		if (2 + 4 * (long)k > tl)
			return GBM_ENOTLIB;
		for (i = 0; i < k; i++)
			if ((int)rd16(table + 2 + 4 * i, big) == id)
				index = rd16(table + 4 + 4 * i, big);   /* the last, as
				                                           gbmon's dict */
	}
	if (index == 0 || index >= n)
		return GBM_ENONE;
	at = rd32(buf + 4 * index, big);
	end = rd32(buf + 4 * (index + 1), big);
	len = (long)(end - at);
	if (end < at + 2 || len > (long)sizeof packed)
		return GBM_ESHAPE;
	if (rd(ctx, (long)at, packed, len) != len)
		return GBM_EREAD;

	/* a big-endian length, then PackBits */
	size = (long)packed[0] << 8 | packed[1];
	if (size > max)
		return GBM_ESHAPE;
	for (o = 0, p = 2; p < len && o < size; ) {
		int c = packed[p++];

		if (c >= 0x80) {
			int r = 257 - c;

			if (p >= len)
				break;
			while (r-- > 0 && o < size)
				out[o++] = packed[p];
			p++;
		} else {
			int r = c + 1;

			while (r-- > 0 && o < size && p < len)
				out[o++] = packed[p++];
		}
	}
	return o;
}

/* The Dark Queen's race numbers as FRUA's (gbmon.RACE); others monster. */
static unsigned char frua_race(unsigned char r)
{
	static const unsigned char k[8] = { 0, 0, 1, 2, 2, 4, 5, 6 };

	return r < 8 ? k[r] : 6;
}

static int item_number(const unsigned char *it, const unsigned char *items,
                       long items_len)
{
	long k;

	for (k = 0; (k + 1) * GBM_ITEM_IN <= items_len; k++)
		if (memcmp(items + k * GBM_ITEM_IN, it, ITEM_KEY) == 0)
			return (int)(k + 1);
	return 0;
}

int gbm_convert(const unsigned char *q, long len, const unsigned char *items,
                long items_len, int native, unsigned char *f)
{
	int  neff, nitem, i, slot;
	long p;

	if (len < 0x1a0)
		return -1;
	memset(f, 0, GBM_SIZE);
	memcpy(f, q, 0x58);
	for (i = 0; i < 8; i++)
		f[0x58 + i] = q[0x58 + 2 * i];
	f[0x58] = native ? q[0x58] : frua_race(q[0x58]);
	/* rec[91] (rules.h REC_KRYNN): god (0x19b), robe (0x19d), order (0x19a) */
	f[0x5b] = native ? (unsigned char)((q[0x19b] & 7) | (q[0x19d] & 3) << 3
	                                   | (q[0x19a] & 3) << 5) : 0;
	memcpy(f + 0x60, q + 0x68, 0x157 - 0x68);
	memcpy(f + 0x153, q + 0x157, 0x182 - 0x157);
	f[0xbf] &= 0xdf;
	if ((f[0xbf] & 0x0c) == 0x0c)
		f[0xbf] |= 0x20;
	memcpy(f + 0x17e, q + 0x18b, 0x199 - 0x18b);

	neff = q[0x19e];
	nitem = q[0x19f];
	p = 0x1a0;
	if (p + 5L * neff + 17L * nitem > len)
		neff = nitem = 0;
	for (i = 0; i < neff && i < 10; i++)
		f[0x1ae + i] = q[p + 5 * i];
	p += 5L * neff;
	for (i = 0, slot = 0; i < nitem && items != NULL; i++) {
		int num = item_number(q + p + 17L * i, items, items_len);

		if (num == 0 || num > 254 || slot >= 16)
			continue;
		f[0x18e + slot++] = (unsigned char)num;
	}
	return 0;
}
