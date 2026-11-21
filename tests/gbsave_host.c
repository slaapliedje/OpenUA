/*
 * gbsave_host.c — a host-side driver for src/engine/gbsave.c, used only by
 * tests/test_gbsave.py. Not part of any engine build.
 *
 *   gbsave_host <QSV>    one line "map script x y facing desc", then one
 *                        line per member: its record in hex
 *                        (or "ERR <code>")
 */
#include <stdio.h>

#include "../src/engine/gbsave.h"

int main(int argc, char **argv)
{
	static unsigned char q[GBV_SIZE + 4096];
	gbv_save s;
	FILE    *f;
	long     n, k;
	unsigned i;
	int      rc;

	if (argc != 2 || (f = fopen(argv[1], "rb")) == NULL)
		return 2;
	n = (long)fread(q, 1, sizeof q, f);
	fclose(f);
	if ((rc = gbv_read(q, n, &s)) != 0) {
		printf("ERR %d\n", rc);
		return 0;
	}
	printf("%u %u %u %u %u %u\n", s.map, s.script, s.x, s.y, s.facing, s.desc);
	for (i = 0; i < s.members; i++) {
		for (k = 0; k < s.len[i]; k++)
			printf("%02x", q[s.at[i] + k]);
		printf("\n");
	}
	return 0;
}
