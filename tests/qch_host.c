/*
 * qch_host.c — a host-side driver for src/engine/qch.c, used only by
 * tests/test_qch.py. Not part of any engine build.
 *
 *   qch_host <file.QCH> [native]
 *   qch_host -w <file.CCH> [<base.QCH>]
 *
 * Prints the .CCH stream in hex, or with -w the .QCH qch_from_cch writes
 * back; or "ERR <code>".
 */
#include <stdio.h>
#include <string.h>

#include "../src/engine/qch.h"

int main(int argc, char **argv)
{
	static unsigned char q[8192], out[16384];
	FILE *f;
	long  n, len, k;

	if (argc >= 3 && strcmp(argv[1], "-w") == 0) {
		static unsigned char base[8192];
		long blen = 0;

		if ((f = fopen(argv[2], "rb")) == NULL)
			return 2;
		n = (long)fread(q, 1, sizeof q, f);
		fclose(f);
		if (argc > 3) {
			if ((f = fopen(argv[3], "rb")) == NULL)
				return 2;
			blen = (long)fread(base, 1, sizeof base, f);
			fclose(f);
		}
		len = qch_from_cch(q, n, argc > 3 ? base : NULL, blen,
		                   out, (long)sizeof out);
		if (len < 0) {
			printf("ERR %ld\n", len);
			return 0;
		}
		for (k = 0; k < len; k++)
			printf("%02x", out[k]);
		printf("\n");
		return 0;
	}
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
