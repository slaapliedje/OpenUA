/*
 * gbmon_host.c — a host-side driver for src/engine/gbmon.c, used only by
 * tests/test_gbmon_c.py. Not part of any engine build.
 *
 *   gbmon_host <LIB> <id> [ITEM.DAT|-] [native]
 *       the member unpacked, then FRUA's record, each a line of hex;
 *       or "ERR <code>"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/gbmon.h"

static long host_read(void *ctx, long off, void *buf, long len)
{
	FILE *f = (FILE *)ctx;

	if (fseek(f, off, SEEK_SET) != 0)
		return -1;
	return (long)fread(buf, 1, (size_t)len, f);
}

int main(int argc, char **argv)
{
	static unsigned char q[GBM_MAX], rec[GBM_SIZE], items[8192];
	long  n, k, ni = 0;
	FILE *f;

	if (argc < 3 || (f = fopen(argv[1], "rb")) == NULL)
		return 2;
	n = gbm_member(host_read, f, atoi(argv[2]), q, sizeof q);
	fclose(f);
	if (n < 0) {
		printf("ERR %ld\n", n);
		return 0;
	}
	if (argc > 3 && strcmp(argv[3], "-") != 0) {
		if ((f = fopen(argv[3], "rb")) == NULL)
			return 2;
		ni = (long)fread(items, 1, sizeof items, f);
		fclose(f);
	}
	for (k = 0; k < n; k++)
		printf("%02x", q[k]);
	printf("\n");
	if (gbm_convert(q, n, argc > 3 && strcmp(argv[3], "-") != 0 ? items : NULL, ni,
	                argc > 4 && strcmp(argv[4], "native") == 0, rec) != 0) {
		printf("SHORT\n");
		return 0;
	}
	for (k = 0; k < GBM_SIZE; k++)
		printf("%02x", rec[k]);
	printf("\n");
	return 0;
}
