/*
 * gbsave_host.c — a host-side driver for src/engine/gbsave.c, used only by
 * tests/test_gbsave.py. Not part of any engine build.
 *
 *   gbsave_host <QSV>    one line "map script x y facing desc", then one
 *                        line per member: its record in hex
 *                        (or "ERR <code>")
 *   gbsave_host -w <QSV> [<BASE>]
 *                        the save gbv_write makes of that save's own cells,
 *                        square, facing, descriptor and records (over BASE,
 *                        if given), in hex
 *   gbsave_host -1 <SAVGAMx.DAT|.PTY>
 *                        a Champions or Death Knights save: "dkk area x y
 *                        facing wall backdrop last state members", the
 *                        names a line each, then "cell value" for every
 *                        non-zero cell
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

	if (argc == 3 && argv[1][0] == '-' && argv[1][1] == '1') {
		static const unsigned blocks[3][2] = {
			{ 0x4b00, 0x400 }, { 0x7c00, 0x400 }, { 0x7a00, 0x200 }
		};
		gbs1_save g;
		unsigned  b, c;

		if ((f = fopen(argv[2], "rb")) == NULL)
			return 2;
		n = (long)fread(q, 1, sizeof q, f);
		fclose(f);
		if ((rc = gbs1_read(q, n, &g)) != 0) {
			printf("ERR %d\n", rc);
			return 0;
		}
		printf("%d %u %u %u %u %u %u %u %u %u\n", g.dkk, g.game_area, g.x,
		       g.y, g.facing, g.wall, g.backdrop, g.last_state, g.state,
		       g.members);
		for (i = 0; i < g.members; i++)
			printf("%s\n", g.names[i]);
		for (b = 0; b < 3; b++)
			for (c = blocks[b][0]; c < blocks[b][0] + blocks[b][1]; c++)
				if (gbs1_cell(&g, c) != 0)
					printf("%x %u\n", c, gbs1_cell(&g, c));
		return 0;
	}
	if (argc >= 3 && argv[1][0] == '-' && argv[1][1] == 'w') {
		static unsigned char b[GBV_SIZE + 4096], out[GBV_SIZE];
		const unsigned char *rec[GBV_MEMBERS];
		long blen = 0, w;

		if ((f = fopen(argv[2], "rb")) == NULL)
			return 2;
		n = (long)fread(q, 1, sizeof q, f);
		fclose(f);
		if (argc > 3) {
			if ((f = fopen(argv[3], "rb")) == NULL)
				return 2;
			blen = (long)fread(b, 1, sizeof b, f);
			fclose(f);
		}
		if ((rc = gbv_read(q, n, &s)) != 0) {
			printf("ERR %d\n", rc);
			return 0;
		}
		for (i = 0; i < s.members; i++)
			rec[i] = q + s.at[i];
		w = gbv_write(q, s.x, s.y, s.facing, s.desc, s.backdrop, rec, s.len, s.members,
		              argc > 3 ? b : NULL, blen, out, (long)sizeof out);
		if (w < 0) {
			printf("ERR %ld\n", w);
			return 0;
		}
		for (k = 0; k < w; k++)
			printf("%02x", out[k]);
		printf("\n");
		return 0;
	}
	if (argc != 2 || (f = fopen(argv[1], "rb")) == NULL)
		return 2;
	n = (long)fread(q, 1, sizeof q, f);
	fclose(f);
	if ((rc = gbv_read(q, n, &s)) != 0) {
		printf("ERR %d\n", rc);
		return 0;
	}
	printf("%u %u %u %u %u %u %u\n", s.map, s.script, s.x, s.y, s.facing, s.desc, s.backdrop);
	for (i = 0; i < s.members; i++) {
		for (k = 0; k < s.len[i]; k++)
			printf("%02x", q[s.at[i] + k]);
		printf("\n");
	}
	return 0;
}
