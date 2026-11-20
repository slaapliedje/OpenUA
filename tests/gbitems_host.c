/*
 * gbitems_host.c — a host-side driver for src/engine/gbitems.c, used only
 * by tests/test_gbitems.py. Not part of any engine build.
 *
 *   gbitems_host items <ITEM.DAT>        the FRUA table in hex, or ERR
 *   gbitems_host exe <DQK.EXE> [verify]  OK, then "word <text>" per word
 *                                        and "shop <n> <hex items>" per
 *                                        shop; or ERR <code> <table>
 */
#include <stdio.h>
#include <string.h>

#include "../src/engine/gbitems.h"

static long host_read(void *ctx, long off, void *buf, long len)
{
	FILE *f = (FILE *)ctx;

	if (fseek(f, off, SEEK_SET) != 0)
		return -1;
	return (long)fread(buf, 1, (size_t)len, f);
}

int main(int argc, char **argv)
{
	static unsigned char in[8192], out[GBI_ITEMS * GBI_ITEM_OUT];
	static gbi_tables    t;
	const char          *which = 0;
	FILE                *f;
	long                 n, k;
	int                  rc, s;

	if (argc < 3 || (f = fopen(argv[2], "rb")) == NULL)
		return 2;
	if (strcmp(argv[1], "items") == 0) {
		n = (long)fread(in, 1, sizeof in, f);
		fclose(f);
		n = gbi_item_table(in, n, out);
		if (n < 0) {
			printf("ERR %ld\n", n);
			return 0;
		}
		for (k = 0; k < n; k++)
			printf("%02x", out[k]);
		printf("\n");
		return 0;
	}
	rc = gbi_load_dqk(host_read, f, &t, argc > 3 && strcmp(argv[3], "verify") == 0,
	                  &which);
	fclose(f);
	if (rc != GBI_OK) {
		printf("ERR %d %s\n", rc, which ? which : "-");
		return 0;
	}
	printf("OK\n");
	for (s = 0; s < t.words; s++)
		printf("word %s\n", t.word[s]);
	for (s = 0; s < GBI_SHOPS; s++) {
		printf("shop %d ", s + 1);
		for (k = 0; k < t.shop_n[s]; k++)
			printf("%02x", t.shop[s][k]);
		printf("\n");
	}
	return 0;
}
