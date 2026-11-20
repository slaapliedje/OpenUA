/*
 * rules_gb_host.c — a host-side driver for src/engine/rules_gb.c, used only
 * by tests/test_rules_gb.py. Not part of any engine build.
 *
 *   rules_gb_host <exe> [verify]
 *
 * Prints "OK" or "ERR <code> <table>", then on success one line per table,
 * "<field> <hex>", and "name <field> <index> <text>" for the names, in the
 * order tools/gbrules.py's engine_tables() is compared in.
 */
#include <stdio.h>
#include <string.h>

#include "../src/engine/rules_gb.h"

static long host_read(void *ctx, long off, void *buf, long len)
{
	FILE *f = (FILE *)ctx;

	if (fseek(f, off, SEEK_SET) != 0)
		return -1;
	return (long)fread(buf, 1, (size_t)len, f);
}

static void hex(const char *field, const unsigned char *b, size_t n)
{
	size_t k;

	printf("%s ", field);
	for (k = 0; k < n; k++)
		printf("%02x", b[k]);
	printf("\n");
}

#define HEX(f) hex(#f, t.f, sizeof t.f)

int main(int argc, char **argv)
{
	static gbr_tables t;
	const char *which = 0;
	FILE       *f;
	int         rc, i;

	if (argc < 2 || (f = fopen(argv[1], "rb")) == NULL)
		return 2;
	rc = gbr_load_dqk(host_read, f, &t, argc > 2 && strcmp(argv[2], "verify") == 0,
	                  &which);
	fclose(f);
	if (rc != GBR_OK) {
		printf("ERR %d %s\n", rc, which ? which : "-");
		return 0;
	}
	printf("OK\n");
	HEX(race_abilities);   HEX(race_kinds);      HEX(race_start_age);
	HEX(race_age_cat);     HEX(race_level_cap);  HEX(race_thief_adj);
	HEX(kind_ability_min); HEX(kind_aligns);     HEX(xp);
	HEX(xp_step);          HEX(spell_row);       HEX(slot_thac0);
	HEX(spell_slots);
	HEX(slot_saves);       HEX(slot_item_bit);   HEX(slot_hall_bit);
	HEX(slot_name_level);  HEX(slot_hd_count);   HEX(slot_hd_size);
	HEX(thief_skills);
	for (i = 0; i < GBR_KINDS; i++)
		printf("name kind_names %d %s\n", i, t.kind_name[i]);
	for (i = 0; i <= GBR_RACES; i++)
		printf("name race_names %d %s\n", i, t.race_name[i]);
	for (i = 0; i < GBR_RACES; i++)
		printf("name race_labels %d %s\n", i, t.race_label[i]);
	for (i = 0; i < GBR_KINDS; i++)
		printf("name kind_labels %d %s\n", i, t.kind_label[i]);
	return 0;
}
