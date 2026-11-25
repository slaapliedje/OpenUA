/*
 * gbmaps.c — a Gold Box game's maps read as the game keeps them (ADR-0026
 * step (f)). See gbmaps.h; tools/gbgeo.py is the reference.
 */
#include <string.h>

#include "gbmaps.h"

#define GEN1_SIDE  16
#define GEN1_BYTES (4 * GEN1_SIDE * GEN1_SIDE)

/* geo.py's container: FORM/AMOD around four fixed chunks, big-endian */
#define GEO_SIZE   12962
#define HDR_SIZE   290
#define MAP_SIZE   3456
#define ENCR_SIZE  2000
#define STRG_SIZE  7168
#define FORM_SIZE  12954
#define AMOD_SIZE  12946
#define MAX_CELLS  576
#define VERSION    106

#define FRUA_SOLID 14

enum { N, E, S, W };

/* dsn.ART_BLOCK: the stock wall art, without which nothing draws */
static const unsigned char k_art[10] = {
	0x05, 0x08, 0x01, 0x01, 0x08, 0x01, 0x24, 0x03, 0x01, 0x01
};

int gbg_read_map(gbr_read_fn rd, void *ctx, const gbl_lib *lib, unsigned i, gbg_map *m)
{
	static unsigned char raw[8 + 4 * GBG_CELLS];
	unsigned id;
	long     at, len;

	if (gbl_entry(lib, i, &id, &at, &len) != 0)
		return GBL_ENONE;
	if (len > (long)sizeof raw)
		return GBG_ESHAPE;
	if (rd(ctx, at, raw, len) != len)
		return GBL_EREAD;
	m->id = id;
	if (len == GEN1_BYTES) {
		m->width = m->height = GEN1_SIDE;
		memcpy(m->planes, raw, GEN1_BYTES);
		return 0;
	}
	if (len >= 8 && len == 8 + 4L * raw[0] * raw[1]
	 && (long)raw[0] * raw[1] <= GBG_CELLS && raw[0] && raw[1]) {
		m->height = raw[0];
		m->width = raw[1];
		memcpy(m->planes, raw + 8, (size_t)(len - 8));
		return 0;
	}
	return GBG_ESHAPE;
}

int gbg_from_block(const unsigned char *raw, long len, unsigned id, gbg_map *m)
{
	if (len == GEN1_BYTES + 2) {
		/* a DOS DAX block's header word: Champions' 0x0400, Death
		 * Knights' another value (Greybox geo.ts); the planes follow */
		raw += 2;
		len -= 2;
	}
	if (len != GEN1_BYTES)
		return GBG_ESHAPE;
	m->id = id;
	m->width = m->height = GEN1_SIDE;
	memcpy(m->planes, raw, GEN1_BYTES);
	return 0;
}

/* The wall slot and door flag of edge `d` of cell (x, y). */
static unsigned slot(const gbg_map *m, int x, int y, int d)
{
	long n = (long)m->width * m->height, c = (long)y * m->width + x;
	unsigned char b = m->planes[(d < S ? 0 : n) + c];

	return (d == N || d == S) ? b >> 4 : b & 15;
}

static unsigned door(const gbg_map *m, int x, int y, int d)
{
	long n = (long)m->width * m->height;

	return m->planes[3 * n + (long)y * m->width + x] >> (2 * d) & 3;
}

static int passable(const gbg_map *m, int x, int y, int d)
{
	return slot(m, x, y, d) == 0 || door(m, x, y, d) != 0;
}

static void put32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)v;
}

static unsigned char *chunk(unsigned char *p, const char *tag, unsigned long size)
{
	memcpy(p, tag, 4);
	put32(p + 4, size);
	return p + 8;
}

long gbg_geo(const gbg_map *m, unsigned char *out)
{
	static const signed char step[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
	long  cells = (long)m->width * m->height;
	long  map = cells > MAX_CELLS ? cells * 6 : MAP_SIZE, extra = map - MAP_SIZE;
	int   x, y, d, ex = 0, ey = 0, ed = N, found = 0;
	unsigned char *hdr, *cell;

	memset(out, 0, (size_t)(GEO_SIZE + extra));
	hdr = chunk(chunk(chunk(out, "FORM", FORM_SIZE + extra), "AMOD", AMOD_SIZE + extra),
	            "HDR ", HDR_SIZE);
	cell = chunk(hdr + HDR_SIZE, "MAP ", (unsigned long)map);
	chunk(chunk(cell + map, "ENCR", ENCR_SIZE) + ENCR_SIZE, "STRG", STRG_SIZE);

	/* geo.Geo.blank(height, width): its `width` bounds the north-south
	 * axis and its `height` (the stride) the east-west one */
	hdr[0] = VERSION >> 8;
	hdr[1] = VERSION & 0xff;
	hdr[2] = m->height;
	hdr[3] = m->width;
	memcpy(hdr + 4, k_art, sizeof k_art);

	for (y = 0; y < m->height; y++)
		for (x = 0; x < m->width; x++) {
			unsigned char *c = cell + 6 * ((long)m->width * y + x);

			for (d = N; d <= W; d++) {   /* the edge bytes are N, E, S, W */
				unsigned s = slot(m, x, y, d);

				c[d] = (unsigned char)(s == 0 ? 0
				     : door(m, x, y, d) ? s : FRUA_SOLID << 4 | s);
			}
		}

	/* find_entry(): the first cell, reading order, with a way out */
	for (y = 0; y < m->height && !found; y++)
		for (x = 0; x < m->width && !found; x++)
			for (d = N; d <= W && !found; d++) {
				int nx = x + step[d][0], ny = y + step[d][1];

				if (nx >= 0 && nx < m->width && ny >= 0 && ny < m->height
				 && passable(m, x, y, d)) {
					ex = x, ey = y, ed = d;
					found = 1;
				}
			}
	hdr[14] = (unsigned char)ey;        /* set_entry_point(0, row=x, col=y) */
	hdr[15] = (unsigned char)ex;
	hdr[16] = (unsigned char)(2 * ed);
	return GEO_SIZE + extra;
}

long gbg_desc(const gbg_map *m, unsigned boot, unsigned char *out)
{
	long n = (long)m->width * m->height;

	if (m->id == 0 || m->id > 255 || boot > 255)
		return -1;
	memcpy(out, "GBD1", 4);
	out[4] = m->width;
	out[5] = m->height;
	out[6] = (unsigned char)m->id;
	out[7] = (unsigned char)boot;
	memcpy(out + 8, m->planes + 2 * n, (size_t)n);
	return 8 + n;
}
