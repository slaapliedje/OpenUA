/*
 * qch.c — a .QCH as the .CCH stream jt577 reads. See qch.h; the reference
 * is tools/gbchar.py (convert) over tools/gbmon.py (convert), and
 * tests/test_qch.py holds this file to them byte for byte.
 */
#include <string.h>

#include "qch.h"

/* gbmon.RACE: the Dark Queen's race numbers as FRUA's (7, monster, is 6) */
static const unsigned char k_race[8] = { 0, 0, 1, 2, 2, 4, 5, 6 };

static int split(const unsigned char *q, long len, long *items, long *nodes)
{
	long rest;

	if (len < QCH_FIXED)
		return QCH_ESHORT;
	*items = q[8];
	rest = len - QCH_FIXED - QCH_ITEM * *items;
	if (rest < 0 || rest % QCH_NODE)
		return QCH_ESPLIT;
	*nodes = rest / QCH_NODE;
	return 0;
}

static void put_le32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

long qch_cch_size(const unsigned char *q, long len)
{
	long items, nodes, k, kept = 0;
	int  rc = split(q, len, &items, &nodes);

	if (rc)
		return rc;
	for (k = 0; k < items; k++)
		if (q[QCH_FIXED + QCH_ITEM * k] != 73)
			kept++;
	return QCH_REC + 18 * kept + 10 * nodes;
}

long qch_to_cch(const unsigned char *q, long len, unsigned char *out,
                long max, int native)
{
	long           items = 0, nodes = 0, k, kept = 0, size = qch_cch_size(q, len);
	unsigned char *r = out, *s;
	const unsigned char *node;

	if (size < 0)
		return size;
	if (size > max)
		return QCH_EROOM;
	(void)split(q, len, &items, &nodes);

	/* the record: gbmon.convert over the fixed part (no counts follow) */
	memset(r, 0, QCH_REC);
	memcpy(r, q, 0x58);
	for (k = 0; k < 8; k++)                 /* words, low bytes kept */
		r[0x58 + k] = q[0x58 + 2 * k];
	r[0x58] = native ? q[0x58] : (q[0x58] < 8 ? k_race[q[0x58]] : 6);
	/* rec[91] (rules.h REC_KRYNN): god, robe and knightly order, for a
	 * design on the game's own rules; FRUA's holds 0 */
	r[0x5b] = native ? (unsigned char)((q[0x19b] & 7) | (q[0x19d] & 3) << 3
	                                   | (q[0x19a] & 3) << 5) : 0;
	memcpy(r + 0x60, q + 0x68, 0x157 - 0x68);
	memcpy(r + 0x153, q + 0x157, 0x182 - 0x157);
	r[0xbf] &= 0xdf;
	if ((r[0xbf] & 0x0c) == 0x0c)
		r[0xbf] |= 0x20;
	memcpy(r + 0x17e, q + 0x18b, 0x18c - 0x17e);

	/* gbchar.convert: links cleared, the equip slots rebuilt by jt21 */
	memset(r, 0, 4);
	memset(r + 12, 0, 56);

	s = out + QCH_REC;
	for (k = 0; k < items; k++) {
		const unsigned char *it = q + QCH_FIXED + QCH_ITEM * k;

		if (it[0] == 73)                /* a container: contents not here */
			continue;
		memcpy(s, it, QCH_ITEM);
		s[QCH_ITEM] = 0;
		s += 18;
		kept++;
	}
	r[193] = (unsigned char)kept;
	put_le32(r + 8, kept ? 1 : 0);
	put_le32(r + 4, nodes ? 1 : 0);
	node = q + QCH_FIXED + QCH_ITEM * items;
	for (k = 0; k < nodes; k++, node += QCH_NODE) {
		s[0] = node[0];
		s[1] = 0;
		memcpy(s + 2, node + 1, 4);
		put_le32(s + 6, k == nodes - 1 ? 0 : 1);
		s += 10;
	}
	return size;
}

/* ---- writing a .QCH back ---------------------------------------------- */

static int nonzero4(const unsigned char *p)
{
	return p[0] | p[1] | p[2] | p[3];
}

long qch_from_cch(const unsigned char *c, long clen,
                  const unsigned char *base, long blen,
                  unsigned char *out, long max)
{
	static const unsigned char zero[QCH_FIXED];
	static const unsigned char one[4] = { 1, 0, 0, 0 };
	const unsigned char *b = base ? base : zero;
	const unsigned char *rec = c;
	long  n, p = QCH_REC, k, kept = 0, nodes = 0, bnodes = 0;
	long  bend = 0, size;
	unsigned char *q = out, v;

	if (clen < QCH_REC || (base != NULL && blen < QCH_FIXED))
		return QCH_ESHORT;
	if (base != NULL) {
		long bi, bn;

		if (split(base, blen, &bi, &bn) != 0)
			return QCH_ESPLIT;
		bend = QCH_FIXED + QCH_ITEM * bi;
		bnodes = bn;
	}
	if (max < QCH_FIXED)
		return QCH_EROOM;

	/* the fixed part, convert(native)'s copy run backwards */
	memcpy(q, b, QCH_FIXED);
	memcpy(q + 0x44, rec + 0x44, 0x58 - 0x44);
	for (k = 0; k < 8; k++)                 /* words, high bytes kept */
		q[0x58 + 2 * k] = rec[0x58 + k];
	q[0x5e] = b[0x5e];                      /* rec[0x5b] is REC_KRYNN */
	v = rec[0x5b];
	q[0x19b] = (unsigned char)((b[0x19b] & ~7) | (v & 7));
	q[0x19d] = (unsigned char)((b[0x19d] & ~3) | (v >> 3 & 3));
	q[0x19a] = (unsigned char)((b[0x19a] & ~3) | (v >> 5 & 3));
	memcpy(q + 0x68, rec + 0x60, 0x157 - 0x68);
	memcpy(q + 0x157, rec + 0x153, 0x182 - 0x157);
	q[0xc7] = (unsigned char)((rec[0xbf] & ~0x20) | (b[0xc7] & 0x20));
	memcpy(q + 0x18b, rec + 0x17e, 0x199 - 0x18b);

	/* the items, containers and what they hold left out */
	n = nonzero4(rec + 8) ? rec[193] : 0;
	size = QCH_FIXED;
	for (k = 0; k < n; k++) {
		const unsigned char *it = c + p;

		if (p + 18 > clen)
			return QCH_ESPLIT;
		p += 18;
		if (it[0] == 73) {              /* and its contents, it[13] */
			p += 18L * it[13];
			if (p > clen)
				return QCH_ESPLIT;
			continue;
		}
		if (size + QCH_ITEM > max)
			return QCH_EROOM;
		memcpy(out + size, it, QCH_ITEM);
		size += QCH_ITEM;
		kept++;
	}
	q[8] = (unsigned char)kept;
	q[0xc9] = (unsigned char)kept;

	/* the affect nodes, their far pointers the base's where it had one */
	if (nonzero4(rec + 4)) {
		while (p + 10 <= clen) {
			const unsigned char *d = c + p;
			int last;

			p += 10;
			last = !nonzero4(d + 6);
			if (size + QCH_NODE > max)
				return QCH_EROOM;
			out[size] = d[0];
			memcpy(out + size + 1, d + 2, 4);
			if (last)
				memset(out + size + 5, 0, 4);
			else if (nodes < bnodes
			         && nonzero4(base + bend + QCH_NODE * nodes + 5))
				memcpy(out + size + 5, base + bend + QCH_NODE * nodes + 5, 4);
			else
				memcpy(out + size + 5, one, 4);
			size += QCH_NODE;
			nodes++;
			if (last)
				break;
		}
	}
	if (nodes == 0)
		memset(q + 4, 0, 4);
	else if (!nonzero4(q + 4))
		memcpy(q + 4, one, 4);
	return size;
}
