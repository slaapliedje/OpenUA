/*
 * rules_file.h — a ruleset file (RULES.DAT): the classes and races a design
 * brings for itself (ADR-0026 step (g)). Plain C99 with no engine, shim or
 * platform headers, so it builds on the host for tests/test_rules_file.py;
 * the caller reads the file.
 *
 * tools/rules.py is the reference and compiles the file from text; its
 * docstring has the layout. The tables are in the layout rules.h describes
 * (big-endian, the engine's strides), each on a long boundary of the file,
 * so a parsed file is read in place: keep the buffer while the rules are
 * played by, and give it long alignment.
 */
#ifndef ENGINE_RULES_FILE_H
#define ENGINE_RULES_FILE_H

#define RLF_KINDS       17
#define RLF_SLOTS       7
#define RLF_MAX_RACES   7       /* the creation screen's room              */
#define RLF_MAX_PROGS   16
#define RLF_MAX_LEVELS  29
#define RLF_MAX_SPELLS  16      /* spell rows                              */
#define RLF_SELECTORS   3       /* god, knightly order, robe               */
#define RLF_GODS        8       /* rec[91]'s values, 0 included            */
#define RLF_ROBES       4
#define RLF_ORDERS      4

#define RLF_FLAG_PRIME_LIMITS 1
#define RLF_FLAG_KNIGHTS      2

#define RLF_EMAGIC   -1         /* not a ruleset file                      */
#define RLF_ECHUNK   -2         /* a chunk runs past the end               */
#define RLF_EHEAD    -3         /* no HEAD, or a shape out of range        */
#define RLF_EMISSING -4         /* a chunk the rules need is absent        */
#define RLF_ESIZE    -5         /* a table of the wrong size               */
#define RLF_ENAMES   -6         /* a name list of the wrong count          */
#define RLF_EVALUE   -7         /* a value out of range                    */

enum {
	RLF_RACE_BASE, RLF_KIND_LIST, RLF_RACE_KINDS, RLF_KIND_ALIGNS,
	RLF_KIND_ABILITY_MIN, RLF_RACE_ABILITIES, RLF_RACE_AGE_CAT,
	RLF_RACE_START_AGE, RLF_RACE_LEVEL_CAP, RLF_SLOT_XP, RLF_SLOT_XP_STEP,
	RLF_SLOT_SPELL_ROW, RLF_SPELL_SLOTS, RLF_SLOT_PROG, RLF_SLOT_THAC0,
	RLF_SLOT_SAVES, RLF_SLOT_ITEM_BIT, RLF_SLOT_HALL_BIT,
	RLF_SLOT_NAME_LEVEL, RLF_SLOT_HD_COUNT, RLF_SLOT_HD_SIZE,
	RLF_THIEF_SKILLS, RLF_RACE_THIEF_ADJ, RLF_TABLES
};

typedef struct rlf_rules {
	unsigned char races, progressions, xp_levels, kind_row, spell_rows;
	unsigned char default_race, flags;

	const char *name;
	const char *coin;                        /* NULL: FRUA's platinum   */
	const char *kind_name[RLF_KINDS];
	const char *race_name[RLF_MAX_RACES + 1]; /* the last the monster's  */
	const char *kind_label[RLF_KINDS];
	const char *race_label[RLF_MAX_RACES];
	/* SHET, the character sheet's words; all NULL when absent */
	const char *sheet_kind[RLF_KINDS];
	const char *god_name[RLF_GODS];
	const char *robe_name[RLF_ROBES];
	const char *order_name[RLF_ORDERS];

	const unsigned char *table[RLF_TABLES];  /* by RLF_ name, rules.h's  */
	long                 table_len[RLF_TABLES];

	/* The progression a slot plays by, by the character's god (8),
	 * knightly order (4) or robe (4): slot 255 is not picked so. */
	unsigned char        sel_slot[RLF_SELECTORS];
	const unsigned char *sel[RLF_SELECTORS];
} rlf_rules;

/* Parse the file `q` (`len` bytes). 0, or an RLF_E code; `*what`, when not
 * NULL, is set to the chunk at fault. */
int rlf_parse(const unsigned char *q, long len, rlf_rules *r, const char **what);

/* The tag a table is kept under ("RKND"), for messages. */
const char *rlf_tag(int table);

#endif
