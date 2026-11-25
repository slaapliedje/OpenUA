/*
 * gbdax_host.c — a host-side driver for src/engine/gbdax.c, used only by
 * tests/test_gbdax.py. Not part of any engine build.
 *
 *   gbdax_host <file.DAX>   one line per block: "<id> <hex>", or "ERR <code>"
 */
#include <stdio.h>

#include "../src/engine/gbdax.h"

int main(int argc, char **argv)
{
	static unsigned char d[1 << 20], out[1 << 16];
	unsigned char ids[256];
	FILE *f;
	long  n, k, size;
	int   count, i;

	if (argc != 2 || (f = fopen(argv[1], "rb")) == NULL)
		return 2;
	n = (long)fread(d, 1, sizeof d, f);
	fclose(f);
	count = gbdax_ids(d, n, ids, 256);
	if (count < 0) {
		printf("ERR %d\n", count);
		return 0;
	}
	for (i = 0; i < count && i < 256; i++) {
		size = gbdax_block(d, n, ids[i], out, (long)sizeof out);
		if (size < 0) {
			printf("%u ERR %ld\n", ids[i], size);
			continue;
		}
		printf("%u ", ids[i]);
		for (k = 0; k < size; k++)
			printf("%02x", out[k]);
		printf("\n");
	}
	return 0;
}
