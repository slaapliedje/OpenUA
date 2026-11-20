/*
 * qch_host.c — a host-side driver for src/engine/qch.c, used only by
 * tests/test_qch.py. Not part of any engine build.
 *
 *   qch_host <file.QCH> [native]
 *
 * Prints the .CCH stream in hex, or "ERR <code>".
 */
#include <stdio.h>
#include <string.h>

#include "../src/engine/qch.h"

int main(int argc, char **argv)
{
	static unsigned char q[8192], out[16384];
	FILE *f;
	long  n, len, k;

	if (argc < 2 || (f = fopen(argv[1], "rb")) == NULL)
		return 2;
	n = (long)fread(q, 1, sizeof q, f);
	fclose(f);
	len = qch_to_cch(q, n, out, (long)sizeof out,
	                 argc > 2 && strcmp(argv[2], "native") == 0);
	if (len < 0) {
		printf("ERR %ld\n", len);
		return 0;
	}
	if (qch_cch_size(q, n) != len) {
		printf("ERR size\n");
		return 0;
	}
	for (k = 0; k < len; k++)
		printf("%02x", out[k]);
	printf("\n");
	return 0;
}
