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

	/* The FRUA race each race number plays as, 256 entries. FRUA's rules
	 * written into code test race numbers (0 elf ... 5 human, 6 and up
	 * monster): every such test reads RACE(r) instead, so a ruleset's
	 * races keep their own numbers and inherit those rules. FRUA's is the
	 * identity. */
	const unsigned char *race_base;

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
	const unsigned char *spell_slots;    /* -29876  rows x 261 (29 levels x
	                                        9 spell levels), by spell row    */
	const unsigned char *slot_prog;      /* 7: the progression each slot
	                                        plays by (FRUA: the slot)        */

	/* thief skills */
	const unsigned char *thief_skills;   /* -22980  18 levels x 8            */
	const unsigned char *race_thief_adj; /* -22836  race x 8, signed         */

	/* The creation screen (l3666). A label is STRS text (FRUA's, by pool
	 * offset) or the game's own (`*_label_text`, when not NULL). The class
	 * list holds its kinds in kind order: kind_list[k] is the kind's row,
	 * with bit 7 set for a kind no race may take, which L3666 flags (jt452
	 * cmd 17) and gives a neighbour's row, as it does FRUA's druid, monk
	 * and cleric/thief. No race enabling it, the row is never shown. */
	const unsigned short *race_label_strs;
	const unsigned short *kind_label_strs;
	const char *const    *race_label_text;
	const char *const    *kind_label_text;
	const unsigned char  *kind_list;     /* 17                               */
	unsigned char         default_race;  /* the race the screen starts on    */

	/* The training hall (jt557). FRUA's demihumans stop below their cap
	 * with a low prime requisite, a rule written into the code; Krynn has
	 * one maximum per race and class, its race_level_cap. */
	unsigned char         prime_limits;  /* FRUA 1, Krynn 0                  */

	/* The progression a character's class slot plays by, when it depends
	 * on the character (Krynn: a cleric's god, a knight's order, a mage's
	 * robe, kept in rec[REC_KRYNN]); NULL: the slot's, slot_prog. */
	unsigned char (*prog)(const unsigned char *rec, int slot);
	/* Knights of the Sword and Rose cast clerical spells (jt908), and a
	 * Knight of the Rose has no race level cap (jt557). FRUA 0. */
	unsigned char         knights;
	/* The coin's name, as its money row shows it ("Steel"); NULL: FRUA's
	 * platinum. FRUA has one coin and so do the Krynn games, held in the
	 * same record word (rec[76]), so only the name differs. */
	const char           *coin;
} ruleset_t;

/* rec[91], which FRUA's record leaves 0 (no code reads it; every FRUA
 * character and monster holds 0): a Krynn character's god (bits 0-2, DQK
 * rec 0x19b: 1 Paladine, 2 Majere, 3 Kiri-Jolith, 4 Mishakal, 5 Sirrion,
 * 6 Reorx, 7 Shinare), robe (bits 3-4, DQK 0x19d: 1 white, 2 red,
 * 3 black) and knightly order (bits 5-6, DQK 0x19a: 1 Crown, 2 Sword,
 * 3 Rose). 0 is none. */
#define REC_KRYNN 91
#define KRYNN_GOD(rec)   ((rec)[REC_KRYNN] & 7)
#define KRYNN_ROBE(rec)  ((rec)[REC_KRYNN] >> 3 & 3)
#define KRYNN_ORDER(rec) ((rec)[REC_KRYNN] >> 5 & 3)
#define KRYNN_PACK(god, robe, order) \
	((unsigned char)(((god) & 7) | ((robe) & 3) << 3 | ((order) & 3) << 5))
enum { ORDER_CROWN = 1, ORDER_SWORD, ORDER_ROSE };
enum { ROBE_WHITE = 1, ROBE_RED, ROBE_BLACK };

extern const ruleset_t  g_rules_frua;
extern const ruleset_t *g_rules;         /* the rules being played under     */

/* FRUA's race numbers, as RACE() returns them. */
enum { RACE_ELF, RACE_HALF_ELF, RACE_DWARF, RACE_GNOME, RACE_HALFLING,
       RACE_HUMAN, RACE_MONSTER };
#define RACE(r) (g_rules->race_base[(unsigned char)(r)])

/* What a Krynn god grants (DQK.EXE 0x3ddff at creation, 0x625d4 on every
 * THAC0 recompute), behind g_rules->knights:
 *  - KRYNN_TOHIT: Kiri-Jolith, and Reorx for a dwarf, add 1 to the base
 *    THAC0 rec[127] (DQK 0x87). DQK clears and re-derives it, then adds
 *    this, so it is a standing bonus, not a one-off.
 *  - KRYNN_GOD_SPELLS: the god's own spells, bits of the known-spell byte
 *    rec[346] (DQK 0x15e; bit k is spell 57 + k, the class-3 "Special"
 *    spells): Paladine Prot. From Evil 10', Majere Silence 15', Kiri-Jolith
 *    Detect Magic, Mishakal Remove Curse/Bless/Charm Person, Sirrion
 *    Burning Hands, Shinare Charm Person; Reorx none. */
#define KRYNN_TOHIT(rec) ((g_rules->knights \
	&& (KRYNN_GOD(rec) == 3 \
	    || (KRYNN_GOD(rec) == 6 && RACE((rec)[88]) == RACE_DWARF))) ? 1 : 0)
#define KRYNN_SPELL_BYTE 346
#define KRYNN_GOD_SPELLS(god) \
	((unsigned char)("\000\001\004\010\160\200\000\100"[(god) & 7]))

/* The progression (row of slot_xp, slot_xp_step, slot_spell_row) a
 * character's class slot plays by: the slot's own in FRUA, the character's
 * god, order or robe in Krynn (ADR-0026 step (e)). */
#define PROG(rec, slot) (g_rules->prog \
	? g_rules->prog((const unsigned char *)(rec), (slot)) \
	: g_rules->slot_prog[(unsigned char)(slot)])

/* The name of money type `i` (0 the coin, 1 gems, 2 jewelry) for a money
 * row: FRUA's -14492 table, the coin's from the rules being played. */
const char *rules_money_name(int i);

/* `s` with the word "platinum" in it, in any of its cases, as the coin of
 * the rules being played ("steel", "Steel", "STEEL"). Under FRUA's rules
 * `s` itself; otherwise one of a few rotating buffers, good until the
 * fourth call after. */
const char *rules_coin_text(const char *s);

/* The Dark Queen of Krynn's rules, read from DQK.EXE (open on `refnum`).
 * Returns them, or NULL when the file is not the DQK.EXE they can be read
 * from (logged). Reading does not play by them: g_rules is not changed. */
const ruleset_t *rules_load_dqk(short refnum);

/* A design's own rules, read from its RULES.DAT (open on `refnum`;
 * tools/rules.py compiles it, src/engine/rules_file.c parses it). Returns
 * them, or NULL when the file is not one (logged). Reading frees the last
 * file read, so call it only while g_rules is not that file's. */
const ruleset_t *rules_load_file(short refnum);

#endif
