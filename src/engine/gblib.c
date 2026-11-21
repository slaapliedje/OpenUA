/*
 * gblib.c — the Gold Box games' GLIB/HLIB 'DATA' library (ADR-0026). See
 * gblib.h.
 */
#include <string.h>

#include "gblib.h"

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

int gbl_open(gbr_read_fn rd, void *ctx, gbl_lib *lib)
{
	static unsigned char buf[4 * (GBL_MEMBERS + 1)];
	long     tl;
	unsigned n, k, i;

	if (rd(ctx, 0, buf, 16) != 16)
		return GBL_EREAD;
	if (memcmp(buf, "HLIB", 4) == 0)
		lib->big = 0;
	else if (memcmp(buf, "GLIB", 4) == 0)
		lib->big = 1;
	else
		return GBL_ENOTLIB;
	n = rd16(buf + 8, lib->big);
	if (n < 1 || n > GBL_MEMBERS)
		return GBL_ENOTLIB;
	if (rd(ctx, 16, buf, 4 * ((long)n + 1)) != 4 * ((long)n + 1))
		return GBL_EREAD;
	for (i = 0; i <= n; i++) {
		lib->off[i] = rd32(buf + 4 * i, lib->big);
		if (i > 0 && lib->off[i] < lib->off[i - 1])
			return GBL_ENOTLIB;
	}
	lib->members = n;

	tl = (long)(lib->off[1] - lib->off[0]);
	if (tl < 2 || tl > (long)sizeof buf)
		return GBL_ENOTLIB;
	if (rd(ctx, (long)lib->off[0], buf, tl) != tl)
		return GBL_EREAD;
	k = rd16(buf, lib->big);
	if (k > GBL_MEMBERS || 2 + 4 * (long)k > tl)
		return GBL_ENOTLIB;
	for (i = 0; i < k; i++) {
		lib->id[i] = (unsigned short)rd16(buf + 2 + 4 * i, lib->big);
		lib->index[i] = (unsigned short)rd16(buf + 4 + 4 * i, lib->big);
	}
	lib->ids = k;
	return 0;
}

int gbl_entry(const gbl_lib *lib, unsigned i, unsigned *id, long *at, long *len)
{
	unsigned m;

	if (i >= lib->ids)
		return GBL_ENONE;
	m = lib->index[i];
	if (m == 0 || m >= lib->members)
		return GBL_ENONE;
	if (id != NULL)
		*id = lib->id[i];
	*at = (long)lib->off[m];
	*len = (long)(lib->off[m + 1] - lib->off[m]);
	return 0;
}

int gbl_find(const gbl_lib *lib, unsigned id, long *at, long *len)
{
	unsigned i;
	int      found = GBL_ENONE;

	for (i = 0; i < lib->ids; i++)          /* the last that is a member */
		if (lib->id[i] == id && gbl_entry(lib, i, NULL, at, len) == 0)
			found = 0;
	return found;
}
