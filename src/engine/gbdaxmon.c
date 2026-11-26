/*
 * gbdaxmon.c — Champions' and Death Knights' monsters and items. See
 * gbdaxmon.h; tools/gbdaxmon.py is the reference and
 * tests/test_gbdaxmon.py holds this file to it.
 */
#include <string.h>

#include "gbdaxmon.h"

/* Death Knights' GAME.OVR 0x12cb4, the Champions import: from, to, count */
static const unsigned short k_cok_dkk[][3] = {
	{ 0x000, 0x00, 0x1e }, { 0x058, 0x1e, 0x0b }, { 0x0cf, 0x29, 0x08 },
	{ 0x0da, 0x31, 0x0c }, { 0x0f3, 0x3f, 0x27 }, { 0x11a, 0x6b, 0x02 },
	{ 0x130, 0x6d, 0x03 }, { 0x135, 0x70, 0x0c }, { 0x142, 0x7c, 0x46 },
	{ 0x188, 0xc7, 0x11 }
};
static const unsigned char k_cok_clear[] = { 0x7d, 0x3a, 0xbd, 0xb9, 0xc2 };

/* DQK.EXE 0x33ec5, the Death Knights import, in the game's order */
static const unsigned short k_dkk_dqk[][3] = {
	{ 0x01, 0x068, 15 }, { 0x10, 0x078, 12 }, { 0x1c, 0x084, 1 }, { 0x1d, 0x085, 1 },
	{ 0x1e, 0x086, 1 }, { 0x1f, 0x087, 1 }, { 0x20, 0x058, 1 }, { 0x21, 0x05a, 1 },
	{ 0x22, 0x19a, 1 }, { 0x23, 0x19b, 1 }, { 0x24, 0x19d, 1 }, { 0x25, 0x088, 1 },
	{ 0x26, 0x052, 2 }, { 0x28, 0x089, 1 }, { 0x29, 0x08a, 1 }, { 0x2a, 0x08b, 5 },
	{ 0x2f, 0x090, 1 }, { 0x30, 0x091, 1 }, { 0x31, 0x05c, 1 }, { 0x32, 0x093, 8 },
	{ 0x3e, 0x09b, 1 }, { 0x3f, 0x04c, 6 }, { 0x4d, 0x09e, 8 }, { 0x45, 0x0a5, 8 },
	{ 0x55, 0x060, 1 }, { 0x56, 0x062, 1 }, { 0x57, 0x0b3, 2 }, { 0x59, 0x0b5, 2 },
	{ 0x5b, 0x0b7, 2 }, { 0x5d, 0x0b9, 2 }, { 0x5f, 0x0bb, 1 }, { 0x60, 0x0bc, 1 },
	{ 0x61, 0x0bd, 1 }, { 0x62, 0x044, 4 }, { 0x66, 0x048, 4 }, { 0x6a, 0x0be, 1 },
	{ 0x6b, 0x0bf, 1 }, { 0x6c, 0x0c0, 1 }, { 0x6d, 0x054, 2 }, { 0x70, 0x0c3, 1 },
	{ 0x71, 0x0c4, 1 }, { 0x72, 0x0c5, 1 }, { 0x73, 0x0c6, 1 }, { 0x7a, 0x0c7, 1 },
	{ 0x7b, 0x0c8, 1 }, { 0x7c, 0x0c9, 1 }, { 0xb5, 0x0ca, 1 }, { 0xb6, 0x0cb, 1 },
	{ 0xb7, 0x056, 2 }, { 0xc1, 0x0cc, 1 }, { 0xc6, 0x0cd, 1 }, { 0xc7, 0x064, 1 },
	{ 0xc8, 0x18b, 1 }, { 0xc9, 0x066, 1 }, { 0xca, 0x18c, 1 }, { 0xcb, 0x18d, 1 },
	{ 0xcc, 0x18e, 2 }, { 0xce, 0x190, 2 }, { 0xd0, 0x192, 2 }, { 0xd2, 0x194, 2 },
	{ 0xd4, 0x196, 2 }, { 0xd6, 0x198, 1 }
};
#define DQK_MEMORIZED 0xce

/* DQK.EXE's translations from Death Knights' numbers: {from, to} */
static const unsigned char k_word[][2] = {
	{ 12, 138 }, { 38, 139 }, { 40, 140 }, { 52, 141 }, { 59, 142 }, { 67, 126 },
	{ 68, 143 }, { 69, 127 }, { 74, 144 }, { 75, 145 }, { 76, 146 }, { 77, 129 },
	{ 84, 147 }, { 85, 148 }, { 86, 149 }, { 96, 150 }, { 105, 135 }, { 119, 151 },
	{ 120, 152 }, { 124, 153 }
};
static const unsigned char k_spell[][2] = {
	{ 2, 90 }, { 4, 96 }, { 7, 105 }, { 17, 102 }, { 40, 56 }, { 53, 36 },
	{ 56, 110 }, { 57, 127 }, { 59, 53 }, { 60, 111 }, { 61, 130 }, { 62, 131 },
	{ 63, 112 }, { 64, 133 }, { 65, 134 }, { 66, 113 }, { 72, 114 }, { 90, 119 },
	{ 95, 135 }, { 96, 132 }, { 97, 136 }, { 98, 118 }, { 99, 137 }, { 101, 57 },
	{ 102, 59 }, { 103, 60 }, { 104, 61 }, { 105, 62 }, { 106, 63 }, { 107, 64 },
	{ 108, 115 }, { 109, 116 }, { 110, 117 }, { 111, 120 }, { 112, 122 },
	{ 113, 121 }, { 114, 123 }
};
static const unsigned char k_affect[][2] = {
	{ 2, 0x81 }, { 6, 0x82 }, { 7, 0x83 }, { 9, 0x4a }, { 13, 0x47 }, { 32, 0x45 },
	{ 34, 0x20 }, { 40, 0 }, { 48, 13 }, { 57, 0x76 }, { 58, 0x7b }, { 59, 0x39 },
	{ 60, 0x7a }, { 61, 0x44 }, { 65, 6 }, { 66, 0x3c }, { 67, 0x65 }, { 68, 0x66 },
	{ 69, 0x84 }, { 70, 0x85 }, { 74, 0x79 }, { 75, 0x86 }, { 76, 0x87 }, { 77, 0x88 },
	{ 78, 0 }, { 79, 0x89 }, { 80, 0x8a }, { 81, 0x7c }, { 85, 0 }, { 87, 0x4f },
	{ 90, 0 }, { 91, 0x8c }, { 93, 0x42 }, { 96, 0x8d }, { 97, 0x8e }, { 99, 0x8f },
	{ 100, 0 }, { 104, 0 }, { 106, 0 }, { 108, 0 }, { 114, 0xc8 }, { 115, 0x91 },
	{ 116, 0x92 }, { 117, 0x93 }, { 118, 0x94 }, { 120, 0 }, { 121, 0 }
};

#define COUNT(a) (sizeof (a) / sizeof (a)[0])

static unsigned char map(const unsigned char (*m)[2], unsigned n, unsigned char v)
{
	unsigned i;

	for (i = 0; i < n; i++)
		if (m[i][0] == v)
			return m[i][1];
	return v;
}

static unsigned u16(const unsigned char *p)
{
	return p[0] | (unsigned)p[1] << 8;
}

/* ---- words ------------------------------------------------------------ */

long gbx_unexepack(const unsigned char *exe, long len, unsigned char *out, long max)
{
	long cblp, cp, hdr, cs, size, dest, src, dst, n, i;
	const unsigned char *img;

	if (len < 0x1c || exe[0] != 'M' || exe[1] != 'Z')
		return GBX_EPACK;
	cblp = u16(exe + 2);
	cp = u16(exe + 4);
	hdr = u16(exe + 8);
	cs = u16(exe + 0x16);
	size = cp * 512 - (cblp ? 512 - cblp : 0);
	if (size > len || hdr * 16 > size)
		return GBX_EPACK;
	img = exe + hdr * 16;
	size -= hdr * 16;
	if (cs * 16 + 16 > size)
		return GBX_EPACK;
	dest = (long)u16(img + cs * 16 + 12) * 16;
	if (dest < cs * 16)
		dest = cs * 16;
	if (dest > max)
		return GBX_EROOM;
	memcpy(out, img, (size_t)(cs * 16));
	src = cs * 16;
	dst = dest;
	while (src > 0 && img[src - 1] == 0xff)
		src--;
	for (;;) {
		unsigned char cmd;

		if (src < 3)
			return GBX_EPACK;
		cmd = img[src - 1];
		n = u16(img + src - 3);
		src -= 3;
		if (n > dst)
			return GBX_EPACK;
		if ((cmd & 0xfe) == 0xb0) {
			if (src < 1)
				return GBX_EPACK;
			memset(out + dst - n, img[src - 1], (size_t)n);
			src -= 1;
		} else if ((cmd & 0xfe) == 0xb2) {
			if (n > src)
				return GBX_EPACK;
			for (i = 1; i <= n; i++)        /* the ranges may overlap */
				out[dst - i] = img[src - i];
			src -= n;
		} else {
			return GBX_EPACK;
		}
		dst -= n;
		if (cmd & 1)
			return dest;
	}
}

int gbx_game_words(const unsigned char *img, long len,
                   char words[GBX_WORDS + 1][GBX_WORD_MAX])
{
	static const char first[] = "\x0a" "Battle Axe";
	long at, p;
	int  k;

	for (at = 0; at + 21L * GBX_WORDS <= len; at++)
		if (memcmp(img + at, first, sizeof first - 1) == 0)
			break;
	if (at + 21L * GBX_WORDS > len)
		return GBX_ESHORT;
	words[0][0] = 0;
	for (k = 0; k < GBX_WORDS; k++) {
		p = at + 21L * k;
		if (img[p] > 20)
			return GBX_ESHORT;
		memcpy(words[k + 1], img + p + 1, img[p]);
		words[k + 1][img[p]] = 0;
	}
	return 0;
}

int gbx_word_source(int number)
{
	unsigned i;

	for (i = 0; i < COUNT(k_word); i++)
		if (k_word[i][1] == number)
			return k_word[i][0];
	return 0;
}

/* ---- items ------------------------------------------------------------ */

static unsigned char spell(unsigned char b)
{
	return (unsigned char)(map(k_spell, COUNT(k_spell), b & 0x7f) | (b & 0x80));
}

void gbx_item17(const unsigned char *t, unsigned char *q)
{
	int i;

	q[0] = t[0];
	for (i = 1; i < 4; i++)
		q[i] = map(k_word, COUNT(k_word), t[i]);
	q[4] = t[9];                            /* weight */
	q[5] = t[10];
	q[6] = t[12];                           /* value  */
	q[7] = t[13];
	memcpy(q + 8, t + 4, 5);                /* plus .. cursed */
	q[13] = t[11];                          /* count  */
	memcpy(q + 14, t + 14, 3);
	if (q[0] == 39 || q[0] == 40 || q[0] == 73) {   /* scrolls (0x59ab7) */
		for (i = 14; i < 17; i++)
			q[i] = spell(q[i]);
	} else if (q[16] == 0x80) {
		q[15] = map(k_affect, COUNT(k_affect), q[15]);
	} else {
		q[15] = spell(q[15]);
	}
	if (q[0] == 0x34) {
		q[0] = 0x33;
		q[16] = 0;
	}
}

static int same(const unsigned char *a, const unsigned char *b)
{
	return memcmp(a, b, 10) == 0 && memcmp(a + 11, b + 11, GBX_ITEM - 11) == 0;
}

int gbx_table_find(const unsigned char *table, int n, const unsigned char *q)
{
	int k;

	for (k = 0; k < n; k++)
		if (same(table + (long)k * GBX_ITEM, q))
			return k + 1;
	return 0;
}

int gbx_table_put(unsigned char *table, int n, const unsigned char *q)
{
	if (n >= GBX_ITEMS)
		return n;
	memcpy(table + (long)n * GBX_ITEM, q, GBX_ITEM);
	table[(long)n * GBX_ITEM + 10] = 0;     /* not readied */
	return n + 1;
}

int gbx_table_add(unsigned char *table, int n, const unsigned char *q)
{
	if (gbx_table_find(table, n, q))
		return n;
	return gbx_table_put(table, n, q);
}

/* ---- monsters --------------------------------------------------------- */

int gbx_cok_to_dkk(const unsigned char *c, long len, unsigned char *k,
                   unsigned char *mem)
{
	unsigned i;

	if (len < GBX_COK)
		return GBX_ESHORT;
	memset(k, 0, GBX_DKK);
	for (i = 0; i < COUNT(k_cok_dkk); i++)
		memcpy(k + k_cok_dkk[i][1], c + k_cok_dkk[i][0], k_cok_dkk[i][2]);
	for (i = 0; i < sizeof k_cok_clear; i++)
		memset(k + k_cok_clear[i], 0, 4);
	if (k[0x21] == 2 || k[0x21] == 6 || k[0x21] == 14)
		return 0;
	memcpy(mem, c + 0x1e, GBX_MEM);
	return 1;
}

int gbx_dkk_to_dqk(const unsigned char *k, long len, unsigned char *q)
{
	unsigned i;

	if (len < GBX_DKK)
		return GBX_ESHORT;
	memset(q, 0, GBX_DQK);
	for (i = 0; i < COUNT(k_dkk_dqk); i++) {
		unsigned src = k_dkk_dqk[i][0], dst = k_dkk_dqk[i][1];

		if (src == 0x45)                        /* 0x34222..0x3427b */
			q[0x9f] = q[0xa5];
		memcpy(q + dst, k + src, k_dkk_dqk[i][2]);
		if (src == 0x01)                        /* the name, NUL-ended */
			q[0x68 + (k[0] < 15 ? k[0] : 15)] = 0;
		else if (src == 0x21 && q[0x5a] == 7 && q[0x5b] == 0)
			q[0x5a] = 1;
		else if (src == 0x45) {
			q[0xa6] = q[0xac];
			memset(q + 0xac, 0, 8);
		}
	}
	return 0;
}

long gbx_member(const unsigned char *q, const unsigned char *mem,
                const unsigned char *spc, long spc_len,
                const unsigned char *itm, long itm_len,
                unsigned char *out, long max)
{
	long ne = spc_len / GBX_NODE, ni = itm_len / GBX_ITEM_REC, p, i;

	if (ne > 255)
		ne = 255;
	if (ni > 255)
		ni = 255;
	if (GBX_DQK + 2 + 5 * ne + GBX_ITEM * ni > max)
		return GBX_EROOM;
	memcpy(out, q, GBX_DQK);
	if (mem != NULL)
		for (i = 0; i < GBX_MEM; i++)
			out[DQK_MEMORIZED + i] = mem[i] ? map(k_spell, COUNT(k_spell), mem[i]) : 0;
	out[GBX_DQK] = (unsigned char)ne;
	out[GBX_DQK + 1] = (unsigned char)ni;
	p = GBX_DQK + 2;
	for (i = 0; i < ne; i++, p += 5) {
		out[p] = map(k_affect, COUNT(k_affect), spc[GBX_NODE * i]);
		memcpy(out + p + 1, spc + GBX_NODE * i + 1, 4);
	}
	for (i = 0; i < ni; i++, p += GBX_ITEM)
		gbx_item17(itm + GBX_ITEM_REC * i + GBX_ITEM_TAIL, out + p);
	return p;
}

/* ---- characters ------------------------------------------------------- */

/* GAME.OVR 0x12eba..0x12f45, the rest of the Champions import: from, to,
 * count into the zeroed 220-byte caster block */
static const unsigned short k_cok_wiz[][3] = {
	{ 0x01e, 0x00, 0x3a }, { 0x063, 0x4a, 0x6b }, { 0x11c, 0xbc, 5 },
	{ 0x121, 0xc4, 5 }, { 0x126, 0xcc, 5 }, { 0x12b, 0xd4, 5 }
};
#define WIZ_MEMORIZED 0x4a      /* a character's list, zeros first        */
#define WIZ_KNOWN     0x4a      /* a byte a spell, id - 1                 */
#define WIZ_KNOWN_N   0x6b
#define WIZ_SLOT      0xbc      /* four groups of eight spell levels      */
#define DQK_KNOWN    0x157      /* the Dark Queen's known-spell bits      */
#define DQK_KNOWN_N    128
/* the cleric's and the mage's slot groups -> the Dark Queen's slot rows */
static const unsigned short k_wiz_slots[][2] = { { 0, 0x167 }, { 3, 0x179 } };

int gbx_cok_wiz(const unsigned char *c, long len, unsigned char *wiz)
{
	unsigned char k[GBX_DKK], mem[GBX_MEM];
	unsigned      i;
	int           has = gbx_cok_to_dkk(c, len, k, mem);

	if (has <= 0)
		return has;
	memset(wiz, 0, GBX_WIZ);
	for (i = 0; i < COUNT(k_cok_wiz); i++)
		memcpy(wiz + k_cok_wiz[i][1], c + k_cok_wiz[i][0], k_cok_wiz[i][2]);
	return 1;
}

long gbx_qch(const unsigned char *k, long klen, const unsigned char *wiz,
             const unsigned char *eff, long eff_len,
             const unsigned char *jnk, long jnk_len,
             unsigned char *out, long max)
{
	long ni = jnk_len / GBX_ITEM_REC, ne = eff_len / GBX_NODE, p, i;
	int  rc;

	if (ni > 255)
		ni = 255;
	if (ne > 64)
		ne = 64;
	if (GBX_DQK + GBX_ITEM * ni + GBX_NODE * ne > max)
		return GBX_EROOM;
	if ((rc = gbx_dkk_to_dqk(k, klen, out)) != 0)
		return rc;
	if (wiz != NULL) {
		unsigned n = 0;

		for (i = 0; i < WIZ_MEMORIZED; i++)
			if (wiz[i])
				out[DQK_MEMORIZED + n++] = map(k_spell, COUNT(k_spell), wiz[i]);
		for (i = 0; i < WIZ_KNOWN_N; i++)
			if (wiz[WIZ_KNOWN + i]) {
				unsigned s = map(k_spell, COUNT(k_spell), (unsigned char)(i + 1));

				if (s >= 1 && s <= DQK_KNOWN_N)
					out[DQK_KNOWN + ((s - 1) >> 3)] |=
					    (unsigned char)(1u << ((s - 1) & 7));
			}
		for (i = 0; i < (long)COUNT(k_wiz_slots); i++)
			memcpy(out + k_wiz_slots[i][1],
			       wiz + WIZ_SLOT + 8 * k_wiz_slots[i][0], 8);
	}
	out[8] = (unsigned char)ni;
	out[0xc9] = (unsigned char)ni;
	p = GBX_DQK;
	for (i = 0; i < ni; i++, p += GBX_ITEM)
		gbx_item17(jnk + GBX_ITEM_REC * i + GBX_ITEM_TAIL, out + p);
	for (i = 0; i < ne; i++, p += GBX_NODE) {
		memcpy(out + p, eff + GBX_NODE * i, GBX_NODE);
		out[p] = map(k_affect, COUNT(k_affect), eff[GBX_NODE * i]);
	}
	return p;
}
