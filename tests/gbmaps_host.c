/*
 * gbmaps_host.c — a host-side driver for src/engine/gbmaps.c and gblib.c,
 * used only by tests/test_gbmaps.py. Not part of any engine build.
 *
 *   gbmaps_host <LIB> ids              the id table's ids, in order
 *   gbmaps_host <LIB> geo <i>          map i as a GEO file, in hex
 *   gbmaps_host <LIB> desc <i> <boot>  map i's descriptor, in hex
 *   gbmaps_host <LIB> member <id>      member <id> as it is, in hex
 *   gbmaps_host <BLOCK> block <id> geo|desc <boot>
 *                                      a raw Gen1 block (1024 bytes, or
 *                                      1026 as a DAX holds it) as map <id>
 *   (or "ERR <code>")
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/gbmaps.h"

static long host_read(void *ctx, long off, void *buf, long len)
{
	FILE *f = (FILE *)ctx;

	if (fseek(f, off, SEEK_SET) != 0)
		return -1;
	return (long)fread(buf, 1, (size_t)len, f);
}

static void hex(const unsigned char *p, long n)
{
	long k;

	for (k = 0; k < n; k++)
		printf("%02x", p[k]);
	printf("\n");
}

int main(int argc, char **argv)
{
	static gbl_lib       lib;
	static gbg_map       m;
	static unsigned char out[GBG_GEO_MAX + 16384];
	FILE *f;
	long  n, at;
	int   rc;
	unsigned i, id;

	if (argc < 3 || (f = fopen(argv[1], "rb")) == NULL)
		return 2;
	if (strcmp(argv[2], "block") == 0 && argc >= 5) {
		static unsigned char raw[4096];

		n = (long)fread(raw, 1, sizeof raw, f);
		if ((rc = gbg_from_block(raw, n, (unsigned)atoi(argv[3]), &m)) != 0) {
			printf("ERR %d\n", rc);
			return 0;
		}
		n = strcmp(argv[4], "geo") == 0 ? gbg_geo(&m, out)
		  : gbg_desc(&m, argc > 5 ? (unsigned)atoi(argv[5]) : 0, out);
		if (n < 0) {
			printf("ERR %ld\n", n);
			return 0;
		}
		hex(out, n);
		return 0;
	}
	if ((rc = gbl_open(host_read, f, &lib)) != 0) {
		printf("ERR %d\n", rc);
		return 0;
	}
	if (strcmp(argv[2], "ids") == 0) {
		for (i = 0; gbl_entry(&lib, i, &id, &at, &n) == 0; i++)
			printf("%u ", id);
		printf("\n");
		return 0;
	}
	if (strcmp(argv[2], "member") == 0) {
		if ((rc = gbl_find(&lib, (unsigned)atoi(argv[3]), &at, &n)) != 0
		 || n > (long)sizeof out || host_read(f, at, out, n) != n) {
			printf("ERR %d\n", rc ? rc : -3);
			return 0;
		}
		hex(out, n);
		return 0;
	}
	if ((rc = gbg_read_map(host_read, f, &lib, (unsigned)atoi(argv[3]), &m)) != 0) {
		printf("ERR %d\n", rc);
		return 0;
	}
	if (strcmp(argv[2], "geo") == 0)
		n = gbg_geo(&m, out);
	else
		n = gbg_desc(&m, (unsigned)atoi(argv[4]), out);
	if (n < 0) {
		printf("ERR %ld\n", n);
		return 0;
	}
	hex(out, n);
	return 0;
}
