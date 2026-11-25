/*
 * gbdaxmon_host.c — a host-side driver for src/engine/gbdaxmon.c, used only
 * by tests/test_gbdaxmon.py. Not part of any engine build.
 *
 *   gbdaxmon_host words START.EXE        the 125 words, a line each
 *   gbdaxmon_host source N               gbx_word_source(N)
 *   gbdaxmon_host items FILE             each 17-byte entry of FILE as the
 *                                        Dark Queen's, a line of hex each
 *   gbdaxmon_host table FILE...          gbx_table_add over every 17-byte
 *                                        entry of the files in turn; then
 *                                        each entry's gbx_table_find
 *   gbdaxmon_host monster CHA WIZ SPC ITM   the member, a line of hex
 *                                        ("-" for a block it lacks)
 * Errors print "ERR <code>".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/gbdaxmon.h"

static long slurp(const char *path, unsigned char *buf, long max)
{
	FILE *f;
	long  n;

	if (strcmp(path, "-") == 0)
		return 0;
	if ((f = fopen(path, "rb")) == NULL)
		return -1;
	n = (long)fread(buf, 1, (size_t)max, f);
	fclose(f);
	return n;
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
	static unsigned char a[262144], b[262144], c[65536], d[65536], e[65536];
	static unsigned char table[GBX_ITEMS * GBX_ITEM];
	static char          words[GBX_WORDS + 1][GBX_WORD_MAX];

	if (argc < 3)
		return 2;
	if (strcmp(argv[1], "words") == 0) {
		long n = slurp(argv[2], a, sizeof a), m;
		int  k;

		m = gbx_unexepack(a, n, b, sizeof b);
		if (m < 0) {
			printf("ERR %ld\n", m);
			return 0;
		}
		if ((k = gbx_game_words(b, m, words)) != 0) {
			printf("ERR %d\n", k);
			return 0;
		}
		for (k = 1; k <= GBX_WORDS; k++)
			printf("%s\n", words[k]);
		return 0;
	}
	if (strcmp(argv[1], "source") == 0) {
		printf("%d\n", gbx_word_source(atoi(argv[2])));
		return 0;
	}
	if (strcmp(argv[1], "items") == 0) {
		long n = slurp(argv[2], a, sizeof a), i;

		for (i = 0; i + GBX_ITEM <= n; i += GBX_ITEM) {
			gbx_item17(a + i, b);
			hex(b, GBX_ITEM);
		}
		return 0;
	}
	if (strcmp(argv[1], "table") == 0) {
		int  n = 0, f, k;
		long i, m;

		for (f = 2; f < argc; f++) {
			int put = argv[f][0] == '=';

			m = slurp(argv[f] + put, a, sizeof a);
			for (i = 0; i + GBX_ITEM <= m; i += GBX_ITEM)
				n = put ? gbx_table_put(table, n, a + i)
				        : gbx_table_add(table, n, a + i);
		}
		for (k = 0; k < n; k++)
			hex(table + (long)k * GBX_ITEM, GBX_ITEM);
		printf("--\n");
		for (f = 2; f < argc; f++) {
			m = slurp(argv[f] + (argv[f][0] == '='), a, sizeof a);
			for (i = 0; i + GBX_ITEM <= m; i += GBX_ITEM)
				printf("%d\n", gbx_table_find(table, n, a + i));
		}
		return 0;
	}
	if (strcmp(argv[1], "monster") == 0 && argc >= 6) {
		static unsigned char k[GBX_DKK], q[GBX_DQK], mem[GBX_MEM], out[8192];
		long cn = slurp(argv[2], a, sizeof a);
		long wn = slurp(argv[3], c, sizeof c);
		long sn = slurp(argv[4], d, sizeof d);
		long in = slurp(argv[5], e, sizeof e);
		int  has = 0, rc;
		long m;

		if (cn < 0 || wn < 0 || sn < 0 || in < 0)
			return 2;
		if (cn >= GBX_COK) {
			has = gbx_cok_to_dkk(a, cn, k, mem);
			if (has < 0) {
				printf("ERR %d\n", has);
				return 0;
			}
		} else {
			memcpy(k, a, cn < GBX_DKK ? (size_t)cn : GBX_DKK);
			if (wn >= GBX_MEM) {
				memcpy(mem, c, GBX_MEM);
				has = 1;
			}
		}
		if ((rc = gbx_dkk_to_dqk(k, cn >= GBX_COK ? GBX_DKK : cn, q)) != 0) {
			printf("ERR %d\n", rc);
			return 0;
		}
		m = gbx_member(q, has ? mem : NULL, d, sn, e, in, out, sizeof out);
		if (m < 0) {
			printf("ERR %ld\n", m);
			return 0;
		}
		hex(out, m);
		return 0;
	}
	return 2;
}
