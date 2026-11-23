/*
 * gbcells_host.c — a host-side driver for src/engine/gbcells.c, used only by
 * tests/test_gbcells.py. Not part of any engine build.
 *
 *   gbcells_host <record file> [op ...]
 *
 * An op is "r<cell>" (print the value read) or "w<cell>=<value>" (write);
 * numbers are hex. After the ops the record is printed in hex.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/gbcells.h"

int main(int argc, char **argv)
{
	static unsigned char rec[512];
	FILE *f;
	int   k;

	if (argc < 2 || (f = fopen(argv[1], "rb")) == NULL)
		return 2;
	if (fread(rec, 1, sizeof rec, f) != sizeof rec) {
		fclose(f);
		return 2;
	}
	fclose(f);
	for (k = 2; k < argc; k++) {
		char         *eq;
		unsigned long cell = strtoul(argv[k] + 1, &eq, 16);

		if (argv[k][0] == 'r')
			printf("%lx=%x\n", cell, gbc_read(rec, (unsigned)cell));
		else if (argv[k][0] == 'w' && *eq == '=')
			gbc_write(rec, (unsigned)cell,
			          (unsigned short)strtoul(eq + 1, NULL, 16));
		else
			return 2;
	}
	for (k = 0; k < (int)sizeof rec; k++)
		printf("%02x", rec[k]);
	printf("\n");
	return 0;
}
