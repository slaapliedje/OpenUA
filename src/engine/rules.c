/*
 * rules.c — the rulesets (ADR-0026). See rules.h.
 *
 * FRUA's own points at the A5 tables the engine has always read. The Dark
 * Queen of Krynn's is read out of DQK.EXE by rules_gb.c (positions, sums and
 * conversions there; tools/gbrules.py is the reference).
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rules.h"
#include "rules_file.h"
#include "rules_gb.h"
#include "gbdaxmon.h"         /* gbx_unexepack */
#include "data_pool_replay.h"
#include "dbglog.h"
#include "files.h"              /* SetFPos / FSRead / GetEOF */
#include "macmemory.h"          /* NewPtr / DisposePtr */

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
	.prime_limits     = 1,
};

const ruleset_t *g_rules = &g_rules_frua;

/* A Krynn game's rules, read into the Dark Queen's tables (rules_gb.c):
 * the Dark Queen's from DQK.EXE, Champions' and Death Knights' from
 * START.EXE. The name tables are what the engine reads FRUA's as: a
 * pointer per entry, a long on the 68k. */
typedef struct krynn_rules {
	gbr_tables    t;
	long          kind_names[GBR_KINDS + 1];
	long          race_names[GBR_RACES + 1];
	unsigned char race_base[256];
	const char   *race_labels[GBR_RACES];
	const char   *kind_labels[GBR_KINDS];
	const char   *sheet_kind[GBR_KINDS];
	const char   *god_name[GBR_GODS];
	const char   *robe_name[GBR_ROBES];
	const char   *order_name[GBR_ORDERS];
	unsigned char kind_list[GBR_KINDS];
	ruleset_t     rs;
} krynn_rules;

static krynn_rules s_dqk, s_dax;

/* The slots' progressions with no character to ask (gbrules.PROGRESSIONS):
 * a cleric of good, a Knight of the Crown, a White Robe. */
static const unsigned char k_dqk_slot_prog[7] = { 0, 3, 2, 10, 6, 7, 9 };

/* DQK.EXE 0xbb19, the progression a character's slot plays by: a cleric's
 * god above Mishakal (4) is of neutrality (1), else of good (0); a knight's
 * order Crown/Sword/Rose is 3/4/5; a mage in the white robe is 7, any other
 * robe (none included) red, 8. The game leaves an order of 0 undefined; it
 * plays as the Crown here. Champions and Death Knights pick the same way
 * (their training code, GAME.OVR 0x194a3, by the same 11 progressions). */
static unsigned char dqk_prog(const unsigned char *rec, int slot)
{
	switch (slot) {
	case 0: return KRYNN_GOD(rec) > 4 ? 1 : 0;
	case 1: return KRYNN_ORDER(rec) == ORDER_SWORD ? 4
	             : KRYNN_ORDER(rec) == ORDER_ROSE ? 5 : 3;
	case 5: return KRYNN_ROBE(rec) == ROBE_WHITE ? 7 : 8;
	default: return k_dqk_slot_prog[slot >= 0 && slot < 7 ? slot : 2];
	}
}

/* Each Krynn race plays FRUA's rules written into code for its nearest FRUA
 * race: both elves as elves, the dwarves as dwarves, kender as halflings
 * (small, resistant to magic and poison, as the Journal has them); race 7
 * is the monster. What Krynn adds to a race (the kender's taunt, ...) is a
 * feature for later (ADR-0026 decision 6). */
static const unsigned char k_dqk_race_base[GBR_RACES] = {
	RACE_ELF, RACE_ELF, RACE_HALF_ELF, RACE_DWARF, RACE_DWARF,
	RACE_HALFLING, RACE_HUMAN
};

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

/* The ruleset of tables `k->t` holds, named `name`. */
static const ruleset_t *krynn_ruleset(krynn_rules *k, const char *name)
{
	int i;

	for (i = 0; i < GBR_KINDS; i++)
		k->kind_names[i] = (long)(uintptr_t)k->t.kind_name[i];
	for (i = 0; i <= GBR_RACES; i++)
		k->race_names[i] = (long)(uintptr_t)k->t.race_name[i];
	for (i = 0; i < GBR_RACES; i++)
		k->race_labels[i] = k->t.race_label[i];
	for (i = 0; i < GBR_KINDS; i++)
		k->kind_labels[i] = k->t.kind_label[i];
	for (i = 0; i < GBR_KINDS; i++)
		k->sheet_kind[i] = k->t.sheet_kind[i];
	for (i = 0; i < GBR_GODS; i++)
		k->god_name[i] = k->t.god_name[i];
	for (i = 0; i < GBR_ROBES; i++)
		k->robe_name[i] = k->t.robe_name[i];
	for (i = 0; i < GBR_ORDERS; i++)
		k->order_name[i] = k->t.order_name[i];
	kind_list(k->t.race_kinds, GBR_RACES, GBR_KIND_ROW, k->kind_list);
	for (i = 0; i < 256; i++)               /* 7 and up: monsters, as FRUA's 6 */
		k->race_base[i] = (unsigned char)(i < GBR_RACES ? k_dqk_race_base[i] : i - 1);

	k->rs = (ruleset_t){
		.name             = name,
		.races            = GBR_RACES,
		.monster_race     = GBR_RACES,
		.race_kinds_stride = GBR_KIND_ROW,
		.progressions     = GBR_PROGS,
		.xp_levels        = GBR_XP_LEVELS,
		.race_base        = k->race_base,

		.kind_names       = (const unsigned char *)k->kind_names,
		.race_names       = (const unsigned char *)k->race_names,

		.race_kinds       = k->t.race_kinds,
		.kind_aligns      = k->t.kind_aligns,
		.kind_ability_min = k->t.kind_ability_min,
		.race_abilities   = k->t.race_abilities,
		.race_age_cat     = k->t.race_age_cat,
		.race_start_age   = k->t.race_start_age,
		.race_level_cap   = k->t.race_level_cap,

		.slot_xp          = k->t.xp,
		.slot_xp_step     = k->t.xp_step,
		.slot_thac0       = k->t.slot_thac0,
		.slot_saves       = k->t.slot_saves,
		.slot_item_bit    = k->t.slot_item_bit,
		.slot_hall_bit    = k->t.slot_hall_bit,
		.slot_name_level  = k->t.slot_name_level,
		.slot_hd_count    = k->t.slot_hd_count,
		.slot_hd_size     = k->t.slot_hd_size,
		.slot_spell_row   = k->t.spell_row,
		.spell_slots      = k->t.spell_slots,
		.slot_prog        = k_dqk_slot_prog,

		.thief_skills     = k->t.thief_skills,
		.race_thief_adj   = k->t.race_thief_adj,

		.race_label_text  = k->race_labels,
		.kind_label_text  = k->kind_labels,
		.kind_list        = k->kind_list,
		.default_race     = 6,         /* human */
		.prog             = dqk_prog,
		.knights          = 1,
		.coin             = k->t.money_name[0],
		.sheet_kind       = k->sheet_kind,
		.god_name         = k->god_name,
		.robe_name        = k->robe_name,
		.order_name       = k->order_name,
	};
	return &k->rs;
}

const ruleset_t *rules_load_dqk(short refnum)
{
	const char *which = 0;
	int         rc;

	rc = gbr_load_dqk(dqk_read, &refnum, &s_dqk.t, 1, &which);
	if (rc != GBR_OK) {
		dbg_log(rc == GBR_ESUM ? "rules: DQK.EXE is not the build its "
		                         "rules are read from; table:"
		                       : "rules: DQK.EXE could not be read; table:");
		dbg_log(which ? which : "-");
		return NULL;
	}
	dbg_log("rules: Dark Queen of Krynn's read from DQK.EXE");
	return krynn_ruleset(&s_dqk, "Dark Queen of Krynn");
}

/* Champions' or Death Knights': START.EXE read whole and unpacked into a
 * block of its own for the time it takes (about 75 KB and 83 KB), the
 * knightly orders read out of GAME.OVR. */
const ruleset_t *rules_load_dax(short start, short ovr)
{
	const char    *which = 0;
	unsigned char *exe, *img;
	long           len = 0, count, n;
	int            rc = GBR_EREAD, game = 0;

	if (GetEOF(start, &len) != noErr || len < 64 || len > 0x20000L) {
		dbg_log("rules: START.EXE could not be read");
		return NULL;
	}
	exe = (unsigned char *)NewPtr(len);
	img = (unsigned char *)NewPtr(GBR_DAX_IMAGE_MAX);
	count = len;
	if (exe != NULL && img != NULL && SetFPos(start, fsFromStart, 0) == noErr
	    && FSRead(start, &count, exe) == noErr && count == len) {
		n = gbx_unexepack(exe, len, img, GBR_DAX_IMAGE_MAX);
		rc = n < 0 ? GBR_EGAME
		   : gbr_load_dax(img, n, dqk_read, &ovr, &s_dax.t, 1, &which, &game);
	}
	if (exe != NULL)
		DisposePtr((Ptr)exe);
	if (img != NULL)
		DisposePtr((Ptr)img);
	if (rc != GBR_OK) {
		dbg_log(rc == GBR_ESUM ? "rules: START.EXE is not the build its "
		                         "rules are read from; table:"
		      : rc == GBR_EGAME ? "rules: START.EXE is not Champions' or "
		                          "Death Knights'"
		                        : "rules: START.EXE could not be read; table:");
		if (rc != GBR_EGAME)
			dbg_log(which ? which : "-");
		return NULL;
	}
	dbg_log(game == GBR_COK ? "rules: Champions of Krynn's read from START.EXE"
	                        : "rules: Death Knights of Krynn's read from START.EXE");
	return krynn_ruleset(&s_dax, game == GBR_COK ? "Champions of Krynn"
	                                             : "Death Knights of Krynn");
}

/* A design's own, from its RULES.DAT (tools/rules.py compiles it). The
 * file is read whole into a block of its own and its tables are played in
 * place; the name tables are pointers, a long each, as the engine reads
 * FRUA's. */
static unsigned char *s_file;           /* the file, NewPtr'd               */
static rlf_rules      s_rlf;
static long           s_file_kind_names[RLF_KINDS];
static long           s_file_race_names[RLF_MAX_RACES + 1];
static unsigned char  s_file_race_base[256];
static ruleset_t      s_rules_file;

/* The progression a character's slot plays by, picked by its god, order
 * or robe where the file says so (DQK.EXE 0xbb19 as data), else the
 * slot's own. */
static unsigned char file_prog(const unsigned char *rec, int slot)
{
	unsigned char by[RLF_SELECTORS];
	int           i;

	by[0] = KRYNN_GOD(rec);
	by[1] = KRYNN_ORDER(rec);
	by[2] = KRYNN_ROBE(rec);
	for (i = 0; i < RLF_SELECTORS; i++)
		if (s_rlf.sel_slot[i] == slot)
			return s_rlf.sel[i][by[i]];
	return s_rlf.table[RLF_SLOT_PROG][slot >= 0 && slot < RLF_SLOTS ? slot : 0];
}

const ruleset_t *rules_load_file(short refnum)
{
	const char *what = 0;
	long        len = 0, count;
	int         rc, i;

	if (s_file != NULL) {           /* the last design's; g_rules is FRUA's */
		DisposePtr((Ptr)s_file);
		s_file = NULL;
	}
	if (GetEOF(refnum, &len) != noErr || len < 8 || len > 0xffffL) {
		dbg_log("rules: RULES.DAT could not be read");
		return NULL;
	}
	s_file = (unsigned char *)NewPtr(len);
	count = len;
	if (s_file == NULL || SetFPos(refnum, fsFromStart, 0) != noErr
	    || FSRead(refnum, &count, s_file) != noErr || count != len) {
		dbg_log("rules: RULES.DAT could not be read");
		goto fail;
	}
	rc = rlf_parse(s_file, len, &s_rlf, &what);
	if (rc != 0) {
		dbg_log("rules: RULES.DAT is not a ruleset this engine reads; chunk:");
		dbg_log(what ? what : "-");
		goto fail;
	}
	for (i = 0; i < RLF_KINDS; i++)
		s_file_kind_names[i] = (long)(uintptr_t)s_rlf.kind_name[i];
	for (i = 0; i <= s_rlf.races; i++)
		s_file_race_names[i] = (long)(uintptr_t)s_rlf.race_name[i];
	/* past the player races, monsters: race `races` plays as FRUA's 6 */
	for (i = 0; i < 256; i++) {
		int m = i - s_rlf.races + RACE_MONSTER;

		s_file_race_base[i] = (unsigned char)(i < s_rlf.races
		        ? s_rlf.table[RLF_RACE_BASE][i] : m > 255 ? 255 : m);
	}

	s_rules_file = (ruleset_t){
		.name             = s_rlf.name,
		.races            = s_rlf.races,
		.monster_race     = s_rlf.races,
		.race_kinds_stride = s_rlf.kind_row,
		.progressions     = s_rlf.progressions,
		.xp_levels        = s_rlf.xp_levels,
		.race_base        = s_file_race_base,

		.kind_names       = (const unsigned char *)s_file_kind_names,
		.race_names       = (const unsigned char *)s_file_race_names,

		.race_kinds       = s_rlf.table[RLF_RACE_KINDS],
		.kind_aligns      = s_rlf.table[RLF_KIND_ALIGNS],
		.kind_ability_min = s_rlf.table[RLF_KIND_ABILITY_MIN],
		.race_abilities   = s_rlf.table[RLF_RACE_ABILITIES],
		.race_age_cat     = s_rlf.table[RLF_RACE_AGE_CAT],
		.race_start_age   = s_rlf.table[RLF_RACE_START_AGE],
		.race_level_cap   = s_rlf.table[RLF_RACE_LEVEL_CAP],

		.slot_xp          = s_rlf.table[RLF_SLOT_XP],
		.slot_xp_step     = s_rlf.table[RLF_SLOT_XP_STEP],
		.slot_thac0       = s_rlf.table[RLF_SLOT_THAC0],
		.slot_saves       = s_rlf.table[RLF_SLOT_SAVES],
		.slot_item_bit    = s_rlf.table[RLF_SLOT_ITEM_BIT],
		.slot_hall_bit    = s_rlf.table[RLF_SLOT_HALL_BIT],
		.slot_name_level  = s_rlf.table[RLF_SLOT_NAME_LEVEL],
		.slot_hd_count    = s_rlf.table[RLF_SLOT_HD_COUNT],
		.slot_hd_size     = s_rlf.table[RLF_SLOT_HD_SIZE],
		.slot_spell_row   = s_rlf.table[RLF_SLOT_SPELL_ROW],
		.spell_slots      = s_rlf.table[RLF_SPELL_SLOTS],
		.slot_prog        = s_rlf.table[RLF_SLOT_PROG],

		.thief_skills     = s_rlf.table[RLF_THIEF_SKILLS],
		.race_thief_adj   = s_rlf.table[RLF_RACE_THIEF_ADJ],

		.race_label_text  = s_rlf.race_label,
		.kind_label_text  = s_rlf.kind_label,
		.kind_list        = s_rlf.table[RLF_KIND_LIST],
		.default_race     = s_rlf.default_race,
		.prime_limits     = (s_rlf.flags & RLF_FLAG_PRIME_LIMITS) != 0,
		.prog             = (s_rlf.sel_slot[0] != 255 || s_rlf.sel_slot[1] != 255
		                     || s_rlf.sel_slot[2] != 255) ? file_prog : NULL,
		.knights          = (s_rlf.flags & RLF_FLAG_KNIGHTS) != 0,
		.coin             = s_rlf.coin,
		.sheet_kind       = s_rlf.god_name[0] ? s_rlf.sheet_kind : NULL,
		.god_name         = s_rlf.god_name[0] ? s_rlf.god_name : NULL,
		.robe_name        = s_rlf.god_name[0] ? s_rlf.robe_name : NULL,
		.order_name       = s_rlf.god_name[0] ? s_rlf.order_name : NULL,
	};
	dbg_log("rules: the design's own, from RULES.DAT:");
	dbg_log(s_rlf.name);
	return &s_rules_file;

fail:
	if (s_file != NULL)
		DisposePtr((Ptr)s_file);
	s_file = NULL;
	return NULL;
}

/* The names a file keeps for its text only (rules.py FRUA_SLOTS and
 * gbrules.SLOTS / PROGRESSIONS), which no ruleset in memory carries. */
static const char *const k_frua_slots[RLF_SLOTS] = {
	"cleric", "druid", "fighter", "paladin", "ranger", "magic-user", "thief"
};
static const char *const k_dqk_slots[RLF_SLOTS] = {
	"cleric", "knight", "fighter", "paladin", "ranger", "mage", "thief"
};
static const char *const k_dqk_progs[GBR_PROGS] = {
	"cleric of good", "cleric of neutrality", "fighter",
	"knight of the crown", "knight of the sword", "knight of the rose",
	"ranger", "white robe mage", "red robe mage", "thief", "paladin"
};

long rules_write(const ruleset_t *rs, const char *(*strs)(unsigned short),
                 unsigned char *out, long max)
{
	static const int kslot[RLF_SELECTORS] = { 0, 1, 5 };
	static const unsigned char kn[RLF_SELECTORS] = { RLF_GODS, RLF_ORDERS,
	                                                 RLF_ROBES };
	unsigned char sel[RLF_SELECTORS][RLF_GODS];
	rlf_rules     r;
	int           i, k, rows = 0;

	if (rs == &s_rules_file)                /* a design's own: as it came */
		return rlf_write(&s_rlf, out, max);
	memset(&r, 0, sizeof r);
	r.races = rs->races;
	r.progressions = rs->progressions;
	r.xp_levels = rs->xp_levels;
	r.kind_row = rs->race_kinds_stride;
	for (i = 0; i < rs->progressions; i++)  /* the rows the slots cast by */
		if (rs->slot_spell_row[i] != 255 && rs->slot_spell_row[i] + 1 > rows)
			rows = rs->slot_spell_row[i] + 1;
	r.spell_rows = (unsigned char)rows;
	r.default_race = rs->default_race;
	r.flags = (unsigned char)((rs->prime_limits ? RLF_FLAG_PRIME_LIMITS : 0)
	                          | (rs->knights ? RLF_FLAG_KNIGHTS : 0));
	r.name = rs->name;
	r.coin = rs->coin;
	for (i = 0; i < RLF_KINDS; i++)
		r.kind_name[i] = (const char *)(uintptr_t)
		                 ((const long *)(const void *)rs->kind_names)[i];
	for (i = 0; i <= rs->races; i++)
		r.race_name[i] = (const char *)(uintptr_t)
		                 ((const long *)(const void *)rs->race_names)[i];
	for (i = 0; i < RLF_KINDS; i++)
		r.kind_label[i] = rs->kind_label_text ? rs->kind_label_text[i]
		                : strs(rs->kind_label_strs[i]);
	for (i = 0; i < rs->races; i++)
		r.race_label[i] = rs->race_label_text ? rs->race_label_text[i]
		                : strs(rs->race_label_strs[i]);
	for (i = 0; i < RLF_SLOTS; i++)
		r.slot_name[i] = rs->knights ? k_dqk_slots[i] : k_frua_slots[i];
	for (i = 0; i < rs->progressions; i++)
		r.prog_name[i] = rs->knights && rs->progressions == GBR_PROGS
		               ? k_dqk_progs[i] : k_frua_slots[i < RLF_SLOTS ? i : 0];
	if (rs->god_name != NULL) {
		for (i = 0; i < RLF_KINDS; i++)
			r.sheet_kind[i] = rs->sheet_kind[i];
		for (i = 0; i < RLF_GODS; i++)
			r.god_name[i] = rs->god_name[i];
		for (i = 0; i < RLF_ROBES; i++)
			r.robe_name[i] = rs->robe_name[i];
		for (i = 0; i < RLF_ORDERS; i++)
			r.order_name[i] = rs->order_name[i];
	}

	r.table[RLF_RACE_BASE] = rs->race_base;
	r.table[RLF_KIND_LIST] = rs->kind_list;
	r.table[RLF_RACE_KINDS] = rs->race_kinds;
	r.table[RLF_KIND_ALIGNS] = rs->kind_aligns;
	r.table[RLF_KIND_ABILITY_MIN] = rs->kind_ability_min;
	r.table[RLF_RACE_ABILITIES] = rs->race_abilities;
	r.table[RLF_RACE_AGE_CAT] = rs->race_age_cat;
	r.table[RLF_RACE_START_AGE] = rs->race_start_age;
	r.table[RLF_RACE_LEVEL_CAP] = rs->race_level_cap;
	r.table[RLF_SLOT_XP] = rs->slot_xp;
	r.table[RLF_SLOT_XP_STEP] = rs->slot_xp_step;
	r.table[RLF_SLOT_SPELL_ROW] = rs->slot_spell_row;
	r.table[RLF_SPELL_SLOTS] = rs->spell_slots;
	r.table[RLF_SLOT_PROG] = rs->slot_prog;
	r.table[RLF_SLOT_THAC0] = rs->slot_thac0;
	r.table[RLF_SLOT_SAVES] = rs->slot_saves;
	r.table[RLF_SLOT_ITEM_BIT] = rs->slot_item_bit;
	r.table[RLF_SLOT_HALL_BIT] = rs->slot_hall_bit;
	r.table[RLF_SLOT_NAME_LEVEL] = rs->slot_name_level;
	r.table[RLF_SLOT_HD_COUNT] = rs->slot_hd_count;
	r.table[RLF_SLOT_HD_SIZE] = rs->slot_hd_size;
	r.table[RLF_THIEF_SKILLS] = rs->thief_skills;
	r.table[RLF_RACE_THIEF_ADJ] = rs->race_thief_adj;
	for (i = 0; i < RLF_TABLES; i++)
		r.table_len[i] = rlf_table_len(&r, i);

	/* The progression a slot plays by, by god, order or robe: asked of
	 * the ruleset's own (DQK.EXE 0xbb19), for the slot each picks. */
	for (i = 0; i < RLF_SELECTORS; i++)
		r.sel_slot[i] = 255;
	if (rs->prog != NULL) {
		for (i = 0; i < RLF_SELECTORS; i++) {
			for (k = 0; k < kn[i]; k++) {
				unsigned char rec[REC_KRYNN + 1];

				memset(rec, 0, sizeof rec);
				rec[REC_KRYNN] = i == 0 ? KRYNN_PACK(k, 0, 0)
				               : i == 1 ? KRYNN_PACK(0, 0, k)
				               : KRYNN_PACK(0, k, 0);
				sel[i][k] = rs->prog(rec, kslot[i]);
			}
			r.sel_slot[i] = (unsigned char)kslot[i];
			r.sel[i] = sel[i];
		}
	}
	return rlf_write(&r, out, max);
}

const char *rules_money_name(int i)
{
	if (i == 0 && g_rules->coin != NULL)
		return g_rules->coin;
	return (const char *)(uintptr_t)*(const long *)A5(-14492 + 4 * i);
}

static int up(int c)  { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static int low(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

const char *rules_coin_text(const char *s)
{
	static const char word[] = "platinum";
	static char       buf[4][160];
	static int        next;
	const char       *coin = g_rules->coin;
	char             *d;
	int               n = 0, k;

	if (coin == NULL)
		return s;
	d = buf[next];
	next = (next + 1) & 3;
	while (*s != 0 && n < (int)sizeof buf[0] - 1) {
		for (k = 0; word[k] != 0 && low((unsigned char)s[k]) == word[k]; k++)
			;
		if (word[k] != 0) {
			d[n++] = *s++;
			continue;
		}
		/* "PLATINUM" upper case, "Platinum" capitalised, else lower */
		for (k = 0; coin[k] != 0 && n < (int)sizeof buf[0] - 1; k++) {
			int c = (unsigned char)coin[k];

			d[n++] = (char)(s[1] >= 'A' && s[1] <= 'Z' ? up(c)
			              : k == 0 && s[0] >= 'A' && s[0] <= 'Z' ? up(c) : low(c));
		}
		s += sizeof word - 1;
	}
	d[n] = 0;
	return d;
}
