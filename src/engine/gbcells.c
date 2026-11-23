/*
 * gbcells.c — the Dark Queen's character cells. See gbcells.h; the reference
 * and the derivation from DQK.EXE are in tools/gbcells.py.
 */
#include "gbcells.h"

#define REC_KRYNN   91          /* rules.h: god, robe and order, packed */
#define NO_SLOT     -1
#define KRYNN_SLOT  -2

#define SPELL_LO    0x41f
#define SPELL_HI    0x4ab
#define SPELL_OFF   0xce
#define STATUS_CELL 0x5ee
#define STATUS_FLAG 0x18b

/* A cell's DQK record offset and width (1 or 2), or width 0. */
typedef struct { unsigned short off; unsigned char w; } field;

static field read_field(unsigned c)
{
	field f = { 0, 0 };

	if (c >= 0x411 && c <= 0x41c)       { f.off = (unsigned short)(0x78 + c - 0x411); f.w = 1; }
	else if (c >= 0x53c && c <= 0x543)  { f.off = (unsigned short)(0x93 + c - 0x53c); f.w = 1; }
	else if (c >= 0x54c && c <= 0x551)  { f.off = (unsigned short)(0x4c + ((c - 0x54c) & ~1U)); f.w = 2; }
	else if (c >= 0x552 && c <= 0x558)  { f.off = (unsigned short)(0xa5 + c - 0x552); f.w = 1; }
	else if (c >= 0x559 && c <= 0x55f)  { f.off = (unsigned short)(0xac + c - 0x559); f.w = 1; }
	else switch (c) {
	case 0x41d: f.off = 0x84;  f.w = 1; break;     /* exceptional strength */
	case 0x41e: f.off = 0x85;  f.w = 1; break;
	case 0x4ae: f.off = 0x58;  f.w = 2; break;     /* race */
	case 0x4af: f.off = 0x5a;  f.w = 2; break;     /* class */
	case 0x4b1:
	case 0x4b2: f.off = 0x52;  f.w = 2; break;     /* age */
	case 0x4b3: f.off = 0x89;  f.w = 1; break;
	case 0x4c9: f.off = 0x19a; f.w = 1; break;     /* knightly order */
	case 0x4ca: f.off = 0x19b; f.w = 2; break;     /* god */
	case 0x4cb: f.off = 0x19d; f.w = 1; break;     /* robe */
	case 0x538: f.off = 0x90;  f.w = 1; break;
	case 0x567: f.off = 0x60;  f.w = 2; break;     /* gender */
	case 0x568: f.off = 0x62;  f.w = 2; break;     /* alignment */
	case 0x571: f.off = 0xbb;  f.w = 1; break;
	case 0x5ec: f.off = 0xcc;  f.w = 1; break;
	case 0x5ee: f.off = 0x64;  f.w = 2; break;     /* status */
	case 0x5f0: f.off = 0x66;  f.w = 2; break;
	case 0x5fd: f.off = 0x198; f.w = 1; break;     /* current hit points */
	default: break;
	}
	return f;
}

static field write_field(unsigned c)
{
	field f = { 0, 0 };

	if (c >= 0x54c && c <= 0x551) {
		f.off = (unsigned short)(0x4c + ((c - 0x54c) & ~1U));
		f.w = 2;
		return f;
	}
	switch (c) {
	case 0x4b1: case 0x538: case 0x568: case 0x571:
	case 0x5ec: case 0x5ee: case 0x5f0: case 0x5fd:
		f = read_field(c);
		break;
	default:
		break;
	}
	return f;
}

/* Where DQK record byte `o` lives in the engine's record: gbmon.py's map,
 * with FRUA's in-memory byte order for the longs at 68/72 and the words at
 * 76..87. */
static int slot(unsigned o, unsigned *shift, unsigned *mask)
{
	if (o >= 0x44 && o < 0x4c)
		return (int)((o & ~3U) + 3 - (o & 3));
	if (o >= 0x4c && o < 0x58)
		return (int)(o ^ 1);
	if (o < 0x58)
		return (int)o;
	if (o < 0x68)
		return (o & 1) || o == 0x5e ? NO_SLOT : (int)(0x58 + (o - 0x58) / 2);
	if (o < 0x157)
		return (int)(o - 8);
	if (o < 0x182)
		return (int)(o - 4);
	if (o >= 0x18b && o < 0x199)
		return (int)(o - 0xd);
	switch (o) {
	case 0x19a: *shift = 5; *mask = 3; return KRYNN_SLOT;
	case 0x19b: *shift = 0; *mask = 7; return KRYNN_SLOT;
	case 0x19d: *shift = 3; *mask = 3; return KRYNN_SLOT;
	default:    return NO_SLOT;
	}
}

static unsigned get_byte(const unsigned char *rec, unsigned o)
{
	unsigned sh = 0, m = 0;
	int      s = slot(o, &sh, &m);

	if (s == NO_SLOT)
		return 0;
	if (s == KRYNN_SLOT)
		return (rec[REC_KRYNN] >> sh) & m;
	return rec[s];
}

static void set_byte(unsigned char *rec, unsigned o, unsigned v)
{
	unsigned sh = 0, m = 0;
	int      s = slot(o, &sh, &m);

	if (s == NO_SLOT)
		return;
	if (s == KRYNN_SLOT) {
		rec[REC_KRYNN] = (unsigned char)((rec[REC_KRYNN] & ~(m << sh)) | ((v & m) << sh));
		return;
	}
	rec[s] = (unsigned char)v;
}

unsigned short gbc_read(const unsigned char *rec, unsigned cell)
{
	field    f;
	unsigned v;

	if (rec == 0)
		return 0;
	if (cell >= SPELL_LO && cell <= SPELL_HI)
		return (unsigned short)get_byte(rec, SPELL_OFF + cell - SPELL_LO);
	f = read_field(cell);
	if (f.w == 0)
		return 0;
	v = get_byte(rec, f.off);
	if (f.w == 2)
		v |= get_byte(rec, f.off + 1U) << 8;
	return (unsigned short)v;
}

void gbc_write(unsigned char *rec, unsigned cell, unsigned short v)
{
	field f;

	if (rec == 0)
		return;
	if (cell >= SPELL_LO && cell <= SPELL_HI) {
		unsigned o = SPELL_OFF + cell - SPELL_LO, cur = get_byte(rec, o);

		if (v == 0xff)
			v = 0;
		if (v == 0 && cur != 0 && !(cur & 0x80))
			set_byte(rec, o, cur | 0x80);   /* cast: marked, not cleared */
		else
			set_byte(rec, o, v & 0xffU);
		return;
	}
	f = write_field(cell);
	if (f.w == 0)
		return;
	set_byte(rec, f.off, v & 0xffU);
	if (f.w == 2)
		set_byte(rec, f.off + 1U, (unsigned)v >> 8);
	if (cell == STATUS_CELL)
		set_byte(rec, STATUS_FLAG, v == 0);
}
