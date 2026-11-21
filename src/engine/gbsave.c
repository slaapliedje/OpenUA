/*
 * gbsave.c — a Dark Queen of Krynn saved game (SAVGAMx.QSV) read as the game
 * writes it (ADR-0026 step (f)). See gbsave.h; tools/gbsave.py is the
 * reference.
 */
#include "gbsave.h"

#define OFF_X      1024
#define OFF_Y      1025
#define OFF_FACING 1026
#define OFF_DESC   1028
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
