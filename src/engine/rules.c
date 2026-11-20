/*
 * rules.c — the rulesets (ADR-0026). See rules.h.
 *
 * FRUA's own points at the A5 tables the engine has always read. The Dark
 * Queen of Krynn's is read out of DQK.EXE by rules_gb.c (positions, sums and
 * conversions there; tools/gbrules.py is the reference).
 */
#include <stddef.h>
#include <stdint.h>

#include "rules.h"
#include "rules_gb.h"
#include "data_pool_replay.h"
#include "dbglog.h"
#include "files.h"              /* SetFPos / FSRead */

#define A5(off) ((const unsigned char *)(g_a5_below + A5_BELOW_SIZE + (off)))

#define R4(n)  (n), (n) + 1, (n) + 2, (n) + 3
#define R16(n) R4(n), R4((n) + 4), R4((n) + 8), R4((n) + 12)
#define R64(n) R16(n), R16((n) + 16), R16((n) + 32), R16((n) + 48)
static const unsigned char k_race_identity[256] = {
	R64(0), R64(64), R64(128), R64(192)
};

/* FRUA's creation screen, as L3666 builds it: the labels' STRS offsets, and
 * the class rows (the flagged druid, monk and cleric/thief share a row). */
static const unsigned short k_frua_race_strs[6] = {
	0x48c6, 0x48ca, 0x48d4, 0x48da, 0x48e0, 0x48ea
};
static const unsigned short k_frua_kind_strs[17] = {
	0x48fe, 0x4906, 0x490c, 0x4914, 0x491c, 0x4924, 0x4930, 0x4936, 0x493c,
	0x494c, 0x4960, 0x496e, 0x4980, 0x498e, 0x49a2, 0x49b0, 0x49c2
};
#define HIDDEN 0x80
static const unsigned char k_frua_kind_list[17] = {
	0, 1 | HIDDEN, 1, 2, 3, 4, 5, 6 | HIDDEN, 6, 7, 8, 9, 9 | HIDDEN, 10, 11, 12, 13
};

const ruleset_t g_rules_frua = {
	.name             = "FRUA",
	.races            = 6,         /* elf, half-elf, dwarf, gnome, halfling, human */
	.monster_race     = 6,
	.race_kinds_stride = 14,
	.progressions     = 7,
	.xp_levels        = 12,
	.race_base        = k_race_identity,

	.kind_names       = A5(-14636),
	.race_names       = A5(-14564),

	.race_kinds       = A5(-30864),
	.kind_aligns      = A5(-30450),
	.kind_ability_min = A5(-30552),
	.race_abilities   = A5(-30960),
	.race_age_cat     = A5(-30612),
	.race_start_age   = A5(-30780),
	.race_level_cap   = A5(-28048),

	.slot_xp          = A5(-30212),
	.slot_xp_step     = A5(-31228),
	.slot_thac0       = A5(-23184),
	.slot_saves       = A5(-28818),
	.slot_item_bit    = A5(-23030),
	.slot_hall_bit    = A5(-23023),
	.slot_name_level  = A5(-23007),
	.slot_hd_count    = A5(-23000),
	.slot_hd_size     = A5(-22993),
	.slot_spell_row   = A5(-30220),
	.spell_slots      = A5(-29876),
	.slot_prog        = k_race_identity,   /* 0..6: each slot its own */

	.thief_skills     = A5(-22980),
	.race_thief_adj   = A5(-22836),

	.race_label_strs  = k_frua_race_strs,
	.kind_label_strs  = k_frua_kind_strs,
	.kind_list        = k_frua_kind_list,
	.default_race     = 5,         /* human */
};

const ruleset_t *g_rules = &g_rules_frua;

/* The Dark Queen's. The name tables are what the engine reads FRUA's as:
 * a pointer per entry, a long on the 68k. */
static gbr_tables s_dqk;
static long       s_dqk_kind_names[GBR_KINDS + 1];
static long       s_dqk_race_names[GBR_RACES + 1];
static unsigned char s_dqk_race_base[256];
static const char   *s_dqk_race_labels[GBR_RACES];

/* Until a character's god, order and robe are read (step (e)): a cleric of
 * good, a Knight of the Crown, a White Robe (gbrules.PROGRESSIONS). */
static const unsigned char k_dqk_slot_prog[7] = { 0, 3, 2, 10, 6, 7, 9 };
static const char   *s_dqk_kind_labels[GBR_KINDS];
static unsigned char s_dqk_kind_list[GBR_KINDS];

/* Each Krynn race plays FRUA's rules written into code for its nearest FRUA
 * race: both elves as elves, the dwarves as dwarves, kender as halflings
 * (small, resistant to magic and poison, as the Journal has them); race 7
 * is the monster. What Krynn adds to a race (the kender's taunt, ...) is a
 * feature for later (ADR-0026 decision 6). */
static const unsigned char k_dqk_race_base[GBR_RACES] = {
	RACE_ELF, RACE_ELF, RACE_HALF_ELF, RACE_DWARF, RACE_DWARF,
	RACE_HALFLING, RACE_HUMAN
};
static ruleset_t  s_rules_dqk;

static long dqk_read(void *ctx, long off, void *buf, long len)
{
	short refnum = *(short *)ctx;
	long  count = len;

	if (SetFPos(refnum, fsFromStart, off) != noErr)
		return -1;
	(void)FSRead(refnum, &count, buf);   /* eofErr on a short read: count says */
	return count;
}

/* The class list of a game that brings only its race-class rows: every kind
 * some race may take gets the next row; the others are flagged, sharing the
 * row of the next kind offered (the last row when none follows). */
static void kind_list(const unsigned char *race_kinds, int races, int stride,
                      unsigned char *out)
{
	unsigned char offered[GBR_KINDS] = { 0 };
	int           r, k, row = 0;

	for (r = 0; r < races; r++) {
		const unsigned char *e = race_kinds + (long)r * stride;

		for (k = 0; k < e[0] && k < stride - 1; k++)
			if (e[1 + k] < GBR_KINDS)
				offered[e[1 + k]] = 1;
	}
	for (k = 0; k < GBR_KINDS; k++)
		if (offered[k])
			out[k] = (unsigned char)row++;
	for (k = GBR_KINDS - 1, row = row ? row - 1 : 0; k >= 0; k--) {
		if (offered[k])
			row = out[k];
		else
			out[k] = (unsigned char)(row | HIDDEN);
	}
}

const ruleset_t *rules_load_dqk(short refnum)
{
	const char *which = 0;
	int         rc, i;

	rc = gbr_load_dqk(dqk_read, &refnum, &s_dqk, 1, &which);
	if (rc != GBR_OK) {
		dbg_log(rc == GBR_ESUM ? "rules: DQK.EXE is not the build its "
		                         "rules are read from; table:"
		                       : "rules: DQK.EXE could not be read; table:");
		dbg_log(which ? which : "-");
		return NULL;
	}
	for (i = 0; i < GBR_KINDS; i++)
		s_dqk_kind_names[i] = (long)(uintptr_t)s_dqk.kind_name[i];
	for (i = 0; i <= GBR_RACES; i++)
		s_dqk_race_names[i] = (long)(uintptr_t)s_dqk.race_name[i];
	for (i = 0; i < GBR_RACES; i++)
		s_dqk_race_labels[i] = s_dqk.race_label[i];
	for (i = 0; i < GBR_KINDS; i++)
		s_dqk_kind_labels[i] = s_dqk.kind_label[i];
	kind_list(s_dqk.race_kinds, GBR_RACES, GBR_KIND_ROW, s_dqk_kind_list);
	for (i = 0; i < 256; i++)               /* 7 and up: monsters, as FRUA's 6 */
		s_dqk_race_base[i] = (unsigned char)(i < GBR_RACES ? k_dqk_race_base[i] : i - 1);

	s_rules_dqk = (ruleset_t){
		.name             = "Dark Queen of Krynn",
		.races            = GBR_RACES,
		.monster_race     = GBR_RACES,
		.race_kinds_stride = GBR_KIND_ROW,
		.progressions     = GBR_PROGS,
		.xp_levels        = GBR_XP_LEVELS,
		.race_base        = s_dqk_race_base,

		.kind_names       = (const unsigned char *)s_dqk_kind_names,
		.race_names       = (const unsigned char *)s_dqk_race_names,

		.race_kinds       = s_dqk.race_kinds,
		.kind_aligns      = s_dqk.kind_aligns,
		.kind_ability_min = s_dqk.kind_ability_min,
		.race_abilities   = s_dqk.race_abilities,
		.race_age_cat     = s_dqk.race_age_cat,
		.race_start_age   = s_dqk.race_start_age,
		.race_level_cap   = s_dqk.race_level_cap,

		.slot_xp          = s_dqk.xp,
		.slot_xp_step     = s_dqk.xp_step,
		.slot_thac0       = s_dqk.slot_thac0,
		.slot_saves       = s_dqk.slot_saves,
		.slot_item_bit    = s_dqk.slot_item_bit,
		.slot_hall_bit    = s_dqk.slot_hall_bit,
		.slot_name_level  = s_dqk.slot_name_level,
		.slot_hd_count    = s_dqk.slot_hd_count,
		.slot_hd_size     = s_dqk.slot_hd_size,
		.slot_spell_row   = s_dqk.spell_row,
		.spell_slots      = s_dqk.spell_slots,
		.slot_prog        = k_dqk_slot_prog,

		.thief_skills     = s_dqk.thief_skills,
		.race_thief_adj   = s_dqk.race_thief_adj,

		.race_label_text  = s_dqk_race_labels,
		.kind_label_text  = s_dqk_kind_labels,
		.kind_list        = s_dqk_kind_list,
		.default_race     = 6,         /* human */
	};
	dbg_log("rules: Dark Queen of Krynn's read from DQK.EXE");
	return &s_rules_dqk;
}
