/*
 * gbsave.c — a Dark Queen of Krynn saved game (SAVGAMx.QSV) read as the game
 * writes it (ADR-0026 step (f)). See gbsave.h; tools/gbsave.py is the
 * reference.
 */
#include <string.h>

#include "gbsave.h"

#define OFF_X      1024
#define OFF_Y      1025
#define OFF_FACING 1026
#define OFF_DESC   1028
#define OFF_BACK   1033
#define OFF_COUNT  1037
#define OFF_PARTY  1039

#define REC_FIXED  0x19e        /* qch.h QCH_FIXED */
#define REC_ITEM   17
#define REC_NODE   9
#define REC_NODES  64

static unsigned long u32(const unsigned char *p)
{
	return p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16
	     | (unsigned long)p[3] << 24;
}

long gbv_record_length(const unsigned char *q, long len, long at)
{
	long p, k;

	if (at + REC_FIXED > len)
		return GBV_ERECORD;
	p = at + REC_FIXED + REC_ITEM * (long)q[at + 8];
	if (u32(q + at + 4) != 0) {
		for (k = 0; ; k++) {
			unsigned long next;

			if (k == REC_NODES || p + REC_NODE > len)
				return GBV_ERECORD;
			next = u32(q + p + 5);
			p += REC_NODE;
			if (next == 0)
				break;
		}
	}
	if (p > len)
		return GBV_ERECORD;
	return p - at;
}

int gbv_read(const unsigned char *q, long len, gbv_save *s)
{
	unsigned count, i;
	long     at = OFF_PARTY;

	if (len < OFF_PARTY)
		return GBV_ESHORT;
	s->cells  = q;
	s->x      = q[OFF_X];
	s->y      = q[OFF_Y];
	s->facing = (unsigned char)(q[OFF_FACING] >> 1 & 3);
	s->desc   = q[OFF_DESC];
	s->backdrop = (unsigned short)(q[OFF_BACK] | q[OFF_BACK + 1] << 8);
	s->map    = q[GBV_CELL_MAP - 1];
	s->script = q[GBV_CELL_SCRIPT - 1];
	count = q[OFF_COUNT] | (unsigned)q[OFF_COUNT + 1] << 8;
	if (count > GBV_MEMBERS)
		return GBV_ECOUNT;
	for (i = 0; i < count; i++) {
		long n = gbv_record_length(q, len, at);

		if (n < 0)
			return (int)n;
		s->at[i] = at;
		s->len[i] = n;
		at += n;
	}
	s->members = count;
	return 0;
}

long gbv_write(const unsigned char *cells, unsigned char x, unsigned char y,
               unsigned char facing, unsigned char desc, unsigned short backdrop,
               const unsigned char *const *rec, const long *rlen, unsigned n,
               const unsigned char *base, long blen,
               unsigned char *out, long max)
{
	long     at = OFF_PARTY, tail = OFF_PARTY, take, k;
	unsigned i;

	if (max < GBV_SIZE)
		return GBV_ESHORT;
	if (n > GBV_MEMBERS)
		return GBV_ECOUNT;
	memset(out, 0, GBV_SIZE);
	if (base != NULL) {
		gbv_save b;

		if (blen < OFF_PARTY || gbv_read(base, blen, &b) != 0)
			return GBV_ESHORT;
		memcpy(out, base, OFF_PARTY);
		for (i = 0; i < b.members; i++)
			tail += b.len[i];
	}
	memcpy(out, cells, GBV_CELLS);
	out[OFF_X] = x;
	out[OFF_Y] = y;
	out[OFF_FACING] = (unsigned char)((facing & 3) << 1);
	out[OFF_DESC] = desc;
	if (backdrop != 0) {
		out[OFF_BACK] = (unsigned char)backdrop;
		out[OFF_BACK + 1] = (unsigned char)(backdrop >> 8);
	}
	out[OFF_COUNT] = (unsigned char)n;
	out[OFF_COUNT + 1] = 0;
	for (i = 0; i < n; i++) {
		if (rlen[i] < 0 || at + rlen[i] > GBV_SIZE)
			return GBV_ECOUNT;
		memcpy(out + at, rec[i], (size_t)rlen[i]);
		at += rlen[i];
	}
	if (base != NULL) {                     /* the block, cut off at the end */
		take = blen - tail;
		if (take > GBV_SIZE - at)
			take = GBV_SIZE - at;
		if (take > 0)
			memcpy(out + at, base + tail, (size_t)take);
	} else {
		for (k = 0; k < 17 && at + k < GBV_SIZE; k++)
			out[at + k] = 0xff;
	}
	return GBV_SIZE;
}

/* ---- Champions and Death Knights ---------------------------------------- */

static const struct { unsigned base, n; } k_gbs1_blocks[3] = {
	{ 0x4b00, 0x400 }, { 0x7c00, 0x400 }, { 0x7a00, 0x200 }
};

int gbs1_read(const unsigned char *q, long len, gbs1_save *s)
{
	long at = 1;
	int  nsets, b;
	unsigned i;

	memset(s, 0, sizeof *s);
	if (len == GBS1_COK_SIZE) {
		s->width = 2;
		nsets = 3;
	} else if (len == GBS1_DKK_SIZE) {
		s->dkk = 1;
		s->width = 1;
		nsets = 4;
	} else
		return GBV_ESHORT;
	s->data = q;
	s->game_area = q[0];
	for (b = 0; b < 3; b++) {
		s->block[b] = at;
		at += (long)s->width * k_gbs1_blocks[b].n;
	}
	s->x = q[at];
	s->y = q[at + 1];
	s->facing = (unsigned char)(q[at + 2] >> 1 & 3);
	s->wall = q[at + 3];
	s->backdrop = q[at + 4];
	s->last_state = q[at + 5];
	s->state = q[at + 6];
	at += 7 + 4L * nsets;
	if (q[at] > GBS1_MEMBERS)
		return GBV_ECOUNT;
	s->members = q[at++];
	for (i = 0; i < s->members; i++) {
		const unsigned char *p = q + at + (long)GBS1_NAME * i;
		unsigned n = p[0] < GBS1_NAME - 1 ? p[0] : GBS1_NAME - 1;

		memcpy(s->names[i], p + 1, n);
		s->names[i][n] = 0;
	}
	return 0;
}

unsigned gbs1_cell(const gbs1_save *s, unsigned addr)
{
	int b;

	for (b = 0; b < 3; b++) {
		unsigned k = addr - k_gbs1_blocks[b].base;

		if (addr >= k_gbs1_blocks[b].base && k < k_gbs1_blocks[b].n) {
			const unsigned char *p = s->data + s->block[b] + (long)s->width * k;

			return s->width == 2 ? (unsigned)(p[0] | p[1] << 8) : p[0];
		}
	}
	return 0;
}
