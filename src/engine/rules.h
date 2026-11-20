/*
 * rules.h — the class and race rules a game is played under (ADR-0026).
 *
 * FRUA keeps these rules in about twenty tables in the A5 world, read in
 * place by the engine. A ruleset is a set of pointers to such tables: FRUA's
 * own (g_rules_frua) points at the A5 tables themselves, so the engine reads
 * exactly the bytes it always has, the DOS CKIT.EXE overlay included; a
 * Gold Box game's ruleset points at its own arrays, sized for its own races.
 *
 * Every field is a byte pointer and keeps the table's own layout, so a
 * reader's arithmetic is unchanged when it moves from the A5 offset to the
 * ruleset. "slot" is a class-level slot (rec[157 + slot], 0..6); "kind" is
 * one of the 17 class kinds (rec[89], 0..16), whose first seven are the
 * slots; "race" is rec[88].
 *
 * The comments give each table's FRUA A5 offset and shape.
 */
#ifndef ENGINE_RULES_H
#define ENGINE_RULES_H

typedef struct ruleset {
	const char *name;
	unsigned char races;             /* player races, 0 .. races-1          */
	unsigned char monster_race;      /* the race number meaning "monster"   */

	/* Shapes. FRUA's readers still use FRUA's numbers; a Gold Box game's
	 * tables are stated in these, and the readers take them up when the
	 * game's rules are played by (ADR-0026 step (c)). */
	unsigned char race_kinds_stride; /* race_kinds row: FRUA 14, Krynn 15   */
	unsigned char progressions;      /* rows of slot_xp, slot_xp_step and
	                                    slot_spell_row: FRUA's are its 7
	                                    slots; Krynn has 11 (clerics by
	                                    god, knights by order, mages by
	                                    robe: tools/gbrules.py)             */
	unsigned char xp_levels;         /* thresholds a slot_xp row: 12 / 19   */

	/* names */
	const unsigned char *kind_names;     /* -14636  17 char * by kind        */
	const unsigned char *race_names;     /* -14564  char * by race (+monster)*/

	/* creation */
	const unsigned char *race_kinds;     /* -30864  race x 14: count, kinds  */
	const unsigned char *kind_aligns;    /* -30450  17 x 12: count, aligns   */
	const unsigned char *kind_ability_min;/* -30552 17 x 6                   */
	const unsigned char *race_abilities; /* -30960  race x 16: min/max pairs */
	const unsigned char *race_age_cat;   /* -30612  race x 5 words           */
	const unsigned char *race_start_age; /* -30780  race x 7 {w, dice, sides}*/
	const unsigned char *race_level_cap; /* -28048  race x 7 slots           */

	/* progression, by slot (the first three by progression, above) */
	const unsigned char *slot_xp;        /* -30212  7 x 12 longs             */
	const unsigned char *slot_xp_step;   /* -31228  7 longs, past level 11   */
	const unsigned char *slot_thac0;     /* -23184  7 x 22, by level         */
	const unsigned char *slot_saves;     /* -28818  7 x 22 levels x 5        */
	const unsigned char *slot_item_bit;  /* -23030  7: bit in rec[183]       */
	const unsigned char *slot_hall_bit;  /* -23023  7: training-hall bit     */
	const unsigned char *slot_name_level;/* -23007  7: last level with a die */
	const unsigned char *slot_hd_count;  /* -23000  7: dice at level 1       */
	const unsigned char *slot_hd_size;   /* -22993  7: die size              */
	const unsigned char *slot_spell_row; /* -30220  7: spell row, 255 none   */

	/* thief skills */
	const unsigned char *thief_skills;   /* -22980  18 levels x 8            */
	const unsigned char *race_thief_adj; /* -22836  race x 8, signed         */
} ruleset_t;

extern const ruleset_t  g_rules_frua;
extern const ruleset_t *g_rules;         /* the rules being played under     */

/* The Dark Queen of Krynn's rules, read from DQK.EXE (open on `refnum`).
 * Returns them, or NULL when the file is not the DQK.EXE they can be read
 * from (logged). Reading does not play by them: g_rules is not changed. */
const ruleset_t *rules_load_dqk(short refnum);

#endif
