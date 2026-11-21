/*
 * rules_file_host.c — a host-side driver for src/engine/rules_file.c, used
 * only by tests/test_rules_file.py. Not part of any engine build.
 *
 *   rules_file_host <RULES.DAT>
 *
 * prints what the parser found, a line each: "head" and the header's
 * numbers, "name"/"coin" and the string, each name list ("knam ..." the
 * names joined by '|'), each table as "<TAG> <hex>", and "sel <slot> <hex>"
 * for each selector; or "ERR <code> <chunk>".
 */
#include <stdio.h>

#include "../src/engine/rules_file.h"

static void list(const char *tag, const char *const *xs, int n)
{
	int i;

	printf("%s ", tag);
	for (i = 0; i < n; i++)
		printf("%s%s", i ? "|" : "", xs[i]);
	printf("\n");
}

int main(int argc, char **argv)
{
	static unsigned char q[65536];
	rlf_rules   r;
	const char *what;
	FILE       *f;
	long        n, k;
	int         rc, i;

	if (argc != 2 || (f = fopen(argv[1], "rb")) == NULL)
		return 2;
	n = (long)fread(q, 1, sizeof q, f);
	fclose(f);
	if ((rc = rlf_parse(q, n, &r, &what)) != 0) {
		printf("ERR %d %.4s\n", rc, what ? what : "-");
		return 0;
	}
	printf("head %u %u %u %u %u %u %u\n", r.races, r.progressions,
	       r.xp_levels, r.kind_row, r.spell_rows, r.default_race, r.flags);
	printf("name %s\n", r.name);
	printf("coin %s\n", r.coin ? r.coin : "-");
	list("knam", r.kind_name, RLF_KINDS);
	list("rnam", r.race_name, r.races + 1);
	list("klab", r.kind_label, RLF_KINDS);
	list("rlab", r.race_label, r.races);
	for (i = 0; i < RLF_TABLES; i++) {
		printf("%s ", rlf_tag(i));
		for (k = 0; k < r.table_len[i]; k++)
			printf("%02x", r.table[i][k]);
		printf("\n");
	}
	for (i = 0; i < RLF_SELECTORS; i++) {
		static const int len[RLF_SELECTORS] = { 8, 4, 4 };

		printf("sel %u ", r.sel_slot[i]);
		if (r.sel[i] != NULL)
			for (k = 0; k < len[i]; k++)
				printf("%02x", r.sel[i][k]);
		printf("\n");
	}
	return 0;
}
