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
	r[0x5b] = 0;
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
