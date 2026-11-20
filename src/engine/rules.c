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

const ruleset_t g_rules_frua = {
	.name             = "FRUA",
	.races            = 6,         /* elf, half-elf, dwarf, gnome, halfling, human */
	.monster_race     = 6,
	.race_kinds_stride = 14,
	.progressions     = 7,
	.xp_levels        = 12,

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

	.thief_skills     = A5(-22980),
	.race_thief_adj   = A5(-22836),
};

const ruleset_t *g_rules = &g_rules_frua;

/* The Dark Queen's. The name tables are what the engine reads FRUA's as:
 * a pointer per entry, a long on the 68k. */
static gbr_tables s_dqk;
static long       s_dqk_kind_names[GBR_KINDS + 1];
static long       s_dqk_race_names[GBR_RACES + 1];
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

	s_rules_dqk = (ruleset_t){
		.name             = "Dark Queen of Krynn",
		.races            = GBR_RACES,
		.monster_race     = GBR_RACES,
		.race_kinds_stride = GBR_KIND_ROW,
		.progressions     = GBR_PROGS,
		.xp_levels        = GBR_XP_LEVELS,

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

		.thief_skills     = s_dqk.thief_skills,
		.race_thief_adj   = s_dqk.race_thief_adj,
	};
	dbg_log("rules: Dark Queen of Krynn's read from DQK.EXE");
	return &s_rules_dqk;
}
