/*
 * gbdax.c — a Gold Box DOS .DAX archive read. See gbdax.h; the reference is
 * tools/ecl.py dax_blocks, and tests/test_gbdax.py holds the two together.
 */
#include "gbdax.h"

static int table(const unsigned char *d, long len, long *toc)
{
	if (len < 2)
		return GBDAX_ENOTDAX;
	*toc = d[0] | (long)d[1] << 8;
	if (*toc % 9 || 2 + *toc > len)
		return GBDAX_ENOTDAX;
	return 0;
}

int gbdax_ids(const unsigned char *d, long len, unsigned char *ids, int max)
{
	long toc, i;

	if (table(d, len, &toc))
		return GBDAX_ENOTDAX;
	for (i = 0; i < toc / 9 && i < max; i++)
		ids[i] = d[2 + 9 * i];
	return (int)(toc / 9);
}

long gbdax_block(const unsigned char *d, long len, int id,
                 unsigned char *out, long max)
{
	long toc, i;

	if (table(d, len, &toc))
		return GBDAX_ENOTDAX;
	for (i = 0; i < toc / 9; i++) {
		const unsigned char *e = d + 2 + 9 * i;
		unsigned long off;
		long size, packed, p, n = 0;
		const unsigned char *src;

		if (e[0] != id)
			continue;
		off = e[1] | (unsigned long)e[2] << 8 | (unsigned long)e[3] << 16
		    | (unsigned long)e[4] << 24;
		size = e[5] | (long)e[6] << 8;
		packed = e[7] | (long)e[8] << 8;
		if (off > (unsigned long)len || 2 + toc + (long)off + packed > len)
			return GBDAX_EBLOCK;
		if (size > max)
			return GBDAX_EROOM;
		src = d + 2 + toc + off;
		for (p = 0; p < packed; ) {
			unsigned char c = src[p++];

			if (c < 0x80) {         /* c + 1 literal bytes */
				long k, run = c + 1;

				if (p + run > packed || n + run > size)
					return GBDAX_EBLOCK;
				for (k = 0; k < run; k++)
					out[n++] = src[p++];
			} else {                /* the next byte, 256 - c times */
				long k, run = 256 - c;

				if (p >= packed || n + run > size)
					return GBDAX_EBLOCK;
				for (k = 0; k < run; k++)
					out[n++] = src[p];
				p++;
			}
		}
		if (n != size)
			return GBDAX_EBLOCK;
		return size;
	}
	return GBDAX_ENOBLOCK;
}
