/*
 * gbdaxart_host.c — a host-side driver for src/engine/gbdaxart.c (with
 * gbdax.c), used only by tests/test_gbdaxart.py. Not part of any engine
 * build.
 *
 *   gbdaxart_host <OUT> <shift> <file.DAX>...      (shift -1: sprites)
 *
 * writes the picture library of every block of the archives in their order
 * (an id seen before is left out), as the engine
 * assembles it: gbda_head, then each gbda_picture. Prints its size or
 * "ERR <code> <id>".
 */
#include <stdio.h>
#include <stdlib.h>

#include "../src/engine/gbdax.h"
#include "../src/engine/gbdaxart.h"

static unsigned char d[8][1 << 18];
static long          dl[8];

int main(int argc, char **argv)
{
	static unsigned char blk[1 << 16], pic[1 << 18], head[4096];
	unsigned id[256], n = 0, k, shift;
	long     size[256], total;
	int      f, nf = argc - 3, i, j, sprite;
	FILE    *o, *fh;

	if (argc < 4 || nf > 8)
		return 2;
	sprite = atoi(argv[2]) < 0;
	shift = sprite ? 0u : (unsigned)atoi(argv[2]);
	for (f = 0; f < nf; f++) {
		if ((fh = fopen(argv[3 + f], "rb")) == NULL)
			return 2;
		dl[f] = (long)fread(d[f], 1, sizeof d[f], fh);
		fclose(fh);
	}
	for (f = 0; f < nf; f++) {              /* archive order, first seen wins */
		unsigned char ids[256];
		int c = gbdax_ids(d[f], dl[f], ids, 256), seen;

		for (i = 0; i < c && i < 256; i++) {
			for (seen = 0, j = 0; j < (int)n; j++)
				if (id[j] == ids[i])
					seen = 1;
			if (!seen && n < 256)
				id[n++] = ids[i];
		}
	}
	for (k = 0; k < n; k++) {
		long len = -1;

		for (f = 0; f < nf && len < 0; f++)
			len = gbdax_block(d[f], dl[f], (int)id[k], blk, (long)sizeof blk);
		size[k] = sprite ? gbda_sprite(blk, len, NULL, 0)
		                 : gbda_picture(blk, len, NULL, 0);
		if (len < 0 || size[k] < 0) {
			printf("ERR %ld %u\n", size[k], id[k]);
			return 0;
		}
	}
	if ((o = fopen(argv[1], "wb")) == NULL)
		return 2;
	total = gbda_head(id, size, n, shift, head);
	fwrite(head, 1, (size_t)total, o);
	for (k = 0; k < n; k++) {
		long len = -1, m;

		for (f = 0; f < nf && len < 0; f++)
			len = gbdax_block(d[f], dl[f], (int)id[k], blk, (long)sizeof blk);
		m = sprite ? gbda_sprite(blk, len, pic, (long)sizeof pic)
		           : gbda_picture(blk, len, pic, (long)sizeof pic);
		fwrite(pic, 1, (size_t)m, o);
		total += m;
	}
	fclose(o);
	printf("%ld\n", total);
	return 0;
}
