/*
 * rules_gb.h — a Gold Box game's class and race rules, read out of its
 * executable (ADR-0026). Plain C99 with no engine, shim or platform headers,
 * so it builds on the host for tests/test_rules_gb.py; the file is read
 * through the caller's callback.
 *
 * tools/gbrules.py is the reference: where each table is, what it holds and
 * how it becomes the engine's layout (rules.h) are described there.
 */
#ifndef ENGINE_RULES_GB_H
#define ENGINE_RULES_GB_H

#define GBR_RACES      7        /* Dark Queen's player races              */
#define GBR_KINDS     17
#define GBR_PROGS     11        /* XP progressions (gbrules.PROGRESSIONS) */
#define GBR_XP_LEVELS 19
#define GBR_KIND_ROW  15        /* race_kinds row: count + 14 kinds       */
#define GBR_NAME_MAX  32
#define GBR_SPELL_ROWS 8        /* spell_slots rows, 261 bytes each       */

/* The tables in the layout the engine indexes: bytes as the game keeps
 * them, words and longs big-endian, rows at the strides rules.h states. */
typedef struct gbr_tables {
	unsigned char race_abilities[16 * GBR_RACES];
	unsigned char race_kinds[GBR_KIND_ROW * GBR_RACES];
	unsigned char race_start_age[28 * GBR_RACES];
	unsigned char race_age_cat[10 * GBR_RACES];
	unsigned char race_level_cap[7 * GBR_RACES];
	unsigned char race_thief_adj[8 * GBR_RACES];
	unsigned char kind_ability_min[6 * GBR_KINDS];
	unsigned char kind_aligns[12 * GBR_KINDS];
	unsigned char xp[4 * GBR_XP_LEVELS * GBR_PROGS];
	unsigned char xp_step[4 * GBR_PROGS];
	unsigned char spell_row[GBR_PROGS];
	unsigned char spell_slots[261 * GBR_SPELL_ROWS];
	unsigned char slot_thac0[22 * 7];
	unsigned char slot_saves[110 * 7];
	unsigned char slot_item_bit[7];
	unsigned char slot_hall_bit[7];
	unsigned char slot_name_level[7];
	unsigned char slot_hd_count[7];
	unsigned char slot_hd_size[7];
	unsigned char thief_skills[8 * 18];
	char          kind_name[GBR_KINDS][GBR_NAME_MAX];
	char          race_name[GBR_RACES + 1][GBR_NAME_MAX];   /* + Monster */
	char          race_label[GBR_RACES][GBR_NAME_MAX];      /* creation   */
	char          kind_label[GBR_KINDS][GBR_NAME_MAX];
} gbr_tables;

/* Read `len` bytes at file offset `off` into `buf`; return the count read. */
typedef long (*gbr_read_fn)(void *ctx, long off, void *buf, long len);

#define GBR_OK        0
#define GBR_EREAD    -1         /* a table could not be read whole        */
#define GBR_ESUM     -2         /* a table's sum is not this build's      */
#define GBR_ENAME    -3         /* a name pointer is not into the DS      */
                                /* (or the label block does not split)    */

/* Fill `t` from DQK.EXE. With `verify`, every table's 16-bit sum must be
 * the Steam DQK.EXE's (a different build is refused, nothing is filled
 * half-way); without, the positions alone are trusted (tests on synthetic
 * files). On an error, *which names the table. */
int gbr_load_dqk(gbr_read_fn rd, void *ctx, gbr_tables *t, int verify,
                 const char **which);

#endif
