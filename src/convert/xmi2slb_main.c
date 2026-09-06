/* Host-side CLI over the xmi2slb core, for the byte-exactness tests
 * (tests/test_xmi2slb_c.py): the C core's output must equal the Python
 * reference's for every corpus module and synthetic file.
 *
 *   xmi2slb bank <out.slb> <q1.xmi|-> <q2.xmi|-> <q3.xmi|-> [scratch_bytes]
 *       '-' = that song is missing (becomes an empty song); the optional
 *       scratch size lets the tests prove a too-small arena FAILS CLEANLY
 *       (XMI2SLB_ERR_SPACE) instead of corrupting memory
 *   xmi2slb classify <basename>
 *       prints "<drv> <q>" (drv 0..4 = TY PC RO AD DQK) or "none"
 *
 * Builds with the host compiler (no cross toolchain needed):
 *   cc -O2 -std=gnu99 -o xmi2slb src/convert/xmi2slb.c src/convert/xmi2slb_main.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xmi2slb.h"

#define SCRATCH_CAP (256L * 1024)
#define DST_CAP     (64L * 1024)

static unsigned char *read_all(const char *path, long *len)
{
	FILE *f = fopen(path, "rb");
	unsigned char *buf;
	long n;
	if (!f)
		return 0;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(n ? n : 1);
	if (!buf || fread(buf, 1, n, f) != (size_t)n) {
		fclose(f);
		free(buf);
		return 0;
	}
	fclose(f);
	*len = n;
	return buf;
}

int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "classify") == 0) {
		int drv, q;
		if (xmi2slb_classify(argv[2], &drv, &q))
			printf("%d %d\n", drv, q);
		else
			printf("none\n");
		return 0;
	}
	if ((argc == 6 || argc == 7) && strcmp(argv[1], "bank") == 0) {
		const unsigned char *xmi[3] = { 0, 0, 0 };
		unsigned char *own[3] = { 0, 0, 0 };
		long len[3] = { 0, 0, 0 }, out;
		long scap = argc == 7 ? atol(argv[6]) : SCRATCH_CAP;
		unsigned char *dst = malloc(DST_CAP), *scratch = malloc(scap > 0 ? scap : 1);
		FILE *f;
		int i;
		if (!dst || !scratch) {
			fprintf(stderr, "out of memory\n");
			return 1;
		}
		for (i = 0; i < 3; i++) {
			if (strcmp(argv[3 + i], "-") == 0)
				continue;
			own[i] = read_all(argv[3 + i], &len[i]);
			if (!own[i]) {
				fprintf(stderr, "cannot read %s\n", argv[3 + i]);
				return 1;
			}
			xmi[i] = own[i];
		}
		out = xmi2slb_bank(xmi, len, dst, DST_CAP, scratch, scap);
		if (out < 0) {
			fprintf(stderr, "convert error %ld\n", out);
			return 2;
		}
		f = fopen(argv[2], "wb");
		if (!f || fwrite(dst, 1, out, f) != (size_t)out) {
			fprintf(stderr, "cannot write %s\n", argv[2]);
			return 1;
		}
		fclose(f);
		printf("%ld\n", out);
		return 0;
	}
	fprintf(stderr, "usage: xmi2slb bank <out.slb> <q1|-> <q2|-> <q3|->\n"
			"       xmi2slb classify <basename>\n");
	return 2;
}
