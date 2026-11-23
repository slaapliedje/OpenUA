/*
 * rules_gb.c — Dark Queen of Krynn's class and race rules, read out of
 * DQK.EXE (ADR-0026). See rules_gb.h; tools/gbrules.py is the reference
 * this is held to.
 *
 * Each table is at a fixed offset in DQK.EXE's data segment and carries the
 * 16-bit sum of its bytes in the Steam release (positions and sums only, as
 * a4_map.c does for CKIT.EXE), so a different build is refused rather than
 * read from the wrong place. A name table's sum is over its strings, NULs
 * included: the pointers are the build's, the text the game's.
 */
#include <string.h>

#include "rules_gb.h"

#define DQK_DS     0x63a20L     /* file offset of the data segment         */
#define DQK_DS_SEG 0x322e       /* the segment word in its far pointers    */

enum {
	T_XP_STEP, T_RACE_AB, T_RACE_KINDS, T_START_AGE, T_AGE_CAT, T_KIND_MIN,
	T_ALIGNS, T_SPELL_ROW, T_XP, T_SAVES, T_LEVEL_CAP, T_THAC0, T_ITEM_BIT,
	T_HALL_BIT, T_NAME_LEVEL, T_HD_COUNT, T_HD_SIZE, T_THIEF, T_RACE_THIEF,
	T_KIND_NAMES, T_RACE_NAMES, T_MONEY_NAMES, T_LABELS, T_SPELL_SLOTS,
	T_SHEET_KINDS, T_GOD_NAMES, T_ROBE_NAMES, T_ORDER_NAMES, T_COUNT
};

static const struct {
	unsigned short off, len, sum;
	const char    *name;
} k_dqk[T_COUNT] = {
	{ 0x028c,   44, 0x0af5, "xp_step" },
	{ 0x02b8,  112, 0x06bf, "race_abilities" },
	{ 0x0328,  203, 0x0218, "race_kinds" },
	{ 0x03f3,  196, 0x1011, "race_start_age" },
	{ 0x04b7,   70, 0x12bb, "race_age_cat" },
	{ 0x04fd,  102, 0x0306, "kind_ability_min" },
	{ 0x0563,  357, 0x0222, "kind_aligns" },
	{ 0x0847,   11, 0x0319, "spell_row" },
	{ 0x0852,  836, 0xb462, "xp" },
	{ 0x13cc,  770, 0x1b15, "slot_saves" },
	{ 0x17dc,   49, 0x037a, "race_level_cap" },
	{ 0x6359,  154, 0x1c23, "slot_thac0" },
	{ 0x63f3,    7, 0x00df, "slot_item_bit" },
	{ 0x63fa,    7, 0x007f, "slot_hall_bit" },
	{ 0x640a,    7, 0x004a, "slot_name_level" },
	{ 0x6411,    7, 0x0009, "slot_hd_count" },
	{ 0x6418,    7, 0x0038, "slot_hd_size" },
	{ 0x6425,  144, 0x237a, "thief_skills" },
	{ 0x64b5,   56, 0x085f, "race_thief_adj" },
	{ 0x3b3e,   68, 0x4bd6, "kind_names" },
	{ 0x3b86,   32, 0x1da4, "race_names" },
	{ 0x3bd2,   12, 0x066b, "money_names" },
	{ 0x7330,  296, 0x4b9f, "labels" },
	{ 0x0b96, 2088, 0x1308, "spell_slots" },
	{ 0x3af6,   68, 0x3ea4, "sheet_kind_names" },
	{ 0x3c0a,   32, 0x1612, "god_names" },
	{ 0x3c2a,   16, 0x04f9, "robe_names" },
	{ 0x8d8c,   41, 0x0d13, "order_names" },
};

#define RAW_MAX 2088            /* the largest table (spell_slots)          */

/* The creation screen's labels: one block of NUL-ended strings, 7 races,
 * 2 genders, 17 kinds. */
static int split_labels(const unsigned char *raw, long n, gbr_tables *t)
{
	long p = 0;
	int  i, k;

	if (n == 0 || raw[n - 1] != 0)
		return GBR_ENAME;
	for (i = 0; i < GBR_RACES + 2 + GBR_KINDS; i++) {
		char *d = i < GBR_RACES ? t->race_label[i]
		        : i >= GBR_RACES + 2 ? t->kind_label[i - GBR_RACES - 2] : 0;

		for (k = 0; p < n && raw[p] != 0; k++, p++)
			if (d != 0 && k < GBR_NAME_MAX - 1)
				d[k] = (char)raw[p];
		if (p >= n)
			return GBR_ENAME;
		p++;                            /* the NUL */
	}
	return GBR_OK;
}

/* The orders a knight's kind ends with on the sheet: one block of
 * NUL-ended strings, Crown, Sword, Rose; order 0 has none. */
static int split_orders(const unsigned char *raw, long n, gbr_tables *t)
{
	long p = 0;
	int  i, k;

	if (n == 0 || raw[n - 1] != 0)
		return GBR_ENAME;
	for (i = 1; i < GBR_ORDERS; i++) {
		for (k = 0; p < n && raw[p] != 0; k++, p++)
			if (k < GBR_NAME_MAX - 1)
				t->order_name[i][k] = (char)raw[p];
		if (p >= n)
			return GBR_ENAME;
		p++;
	}
	return GBR_OK;
}

static int fetch(gbr_read_fn rd, void *ctx, int tab, unsigned char *buf)
{
	long n = k_dqk[tab].len;

	return rd(ctx, DQK_DS + k_dqk[tab].off, buf, n) == n;
}

static unsigned short sum16(const unsigned char *b, long n)
{
	unsigned short s = 0;
	long           k;

	for (k = 0; k < n; k++)
		s = (unsigned short)(s + b[k]);
	return s;
}

/* A far-pointer table's strings into `out` (count entries); the sum of the
 * strings' bytes, NULs included, into *sum. */
static int read_names(gbr_read_fn rd, void *ctx, int tab, char *out,
                      int count, unsigned short *sum)
{
	unsigned char ptrs[4 * (GBR_KINDS + 1)];
	int           i;

	if (!fetch(rd, ctx, tab, ptrs))
		return GBR_EREAD;
	*sum = 0;
	for (i = 0; i < count; i++) {
		unsigned off = ptrs[4 * i] | (unsigned)ptrs[4 * i + 1] << 8;
		unsigned seg = ptrs[4 * i + 2] | (unsigned)ptrs[4 * i + 3] << 8;
		char    *s = out + (long)i * GBR_NAME_MAX;
		long     n;
		int      k;

		if (seg != DQK_DS_SEG)
			return GBR_ENAME;
		n = rd(ctx, DQK_DS + (long)off, s, GBR_NAME_MAX - 1);
		if (n <= 0)
			return GBR_EREAD;
		s[n] = 0;
		for (k = 0; k < n && s[k] != 0; k++)
			*sum = (unsigned short)(*sum + (unsigned char)s[k]);
		if (k == n)
			return GBR_ENAME;           /* no NUL: not a name */
		memset(s + k, 0, (size_t)(GBR_NAME_MAX - k));
	}
	return GBR_OK;
}

static void be16(unsigned char *d, const unsigned char *s)
{
	d[0] = s[1];
	d[1] = s[0];
}

static void be32(unsigned char *d, const unsigned char *s)
{
	d[0] = s[3];
	d[1] = s[2];
	d[2] = s[1];
	d[3] = s[0];
}

int gbr_load_dqk(gbr_read_fn rd, void *ctx, gbr_tables *t, int verify,
                 const char **which)
{
	static unsigned char raw[RAW_MAX];
	static gbr_tables    out;
	unsigned short       sum;
	int                  tab, i, k, rc;

	memset(&out, 0, sizeof out);
	for (tab = 0; tab < T_COUNT; tab++) {
		*which = k_dqk[tab].name;
		if (tab == T_KIND_NAMES || tab == T_RACE_NAMES || tab == T_MONEY_NAMES
		    || tab == T_SHEET_KINDS || tab == T_GOD_NAMES || tab == T_ROBE_NAMES) {
			char *dst = tab == T_KIND_NAMES ? &out.kind_name[0][0]
			          : tab == T_RACE_NAMES ? &out.race_name[0][0]
			          : tab == T_MONEY_NAMES ? &out.money_name[0][0]
			          : tab == T_SHEET_KINDS ? &out.sheet_kind[0][0]
			          : tab == T_GOD_NAMES ? &out.god_name[0][0]
			          : &out.robe_name[0][0];
			int   n = tab == T_KIND_NAMES || tab == T_SHEET_KINDS ? GBR_KINDS
			        : tab == T_RACE_NAMES ? GBR_RACES + 1
			        : tab == T_MONEY_NAMES ? GBR_MONEY
			        : tab == T_GOD_NAMES ? GBR_GODS : GBR_ROBES;

			rc = read_names(rd, ctx, tab, dst, n, &sum);
			if (rc != GBR_OK)
				return rc;
			if (verify && sum != k_dqk[tab].sum)
				return GBR_ESUM;
			continue;
		}
		if (!fetch(rd, ctx, tab, raw))
			return GBR_EREAD;
		if (verify && sum16(raw, k_dqk[tab].len) != k_dqk[tab].sum)
			return GBR_ESUM;

		switch (tab) {
		case T_XP_STEP:
			for (k = 0; k < GBR_PROGS; k++)
				be32(out.xp_step + 4 * k, raw + 4 * k);
			break;
		case T_XP:
			for (k = 0; k < GBR_PROGS * GBR_XP_LEVELS; k++)
				be32(out.xp + 4 * k, raw + 4 * k);
			break;
		case T_AGE_CAT:
			for (k = 0; k < 5 * GBR_RACES; k++)
				be16(out.race_age_cat + 2 * k, raw + 2 * k);
			break;
		case T_START_AGE:                   /* {word, dice, sides} */
			for (k = 0; k < 7 * GBR_RACES; k++) {
				be16(out.race_start_age + 4 * k, raw + 4 * k);
				out.race_start_age[4 * k + 2] = raw[4 * k + 2];
				out.race_start_age[4 * k + 3] = raw[4 * k + 3];
			}
			break;
		case T_RACE_KINDS:                  /* {count, 14 words} -> 15 */
			for (i = 0; i < GBR_RACES; i++) {
				const unsigned char *r = raw + 29 * i;

				out.race_kinds[GBR_KIND_ROW * i] = r[0];
				for (k = 0; k < 14; k++)
					out.race_kinds[GBR_KIND_ROW * i + 1 + k] = r[1 + 2 * k];
			}
			break;
		case T_ALIGNS:                      /* {count, 10 words} -> 12 */
			for (i = 0; i < GBR_KINDS; i++) {
				const unsigned char *r = raw + 21 * i;

				out.kind_aligns[12 * i] = r[0];
				for (k = 0; k < 10; k++)
					out.kind_aligns[12 * i + 1 + k] = r[1 + 2 * k];
			}
			break;
		case T_RACE_AB:     memcpy(out.race_abilities, raw, sizeof out.race_abilities);     break;
		case T_KIND_MIN:    memcpy(out.kind_ability_min, raw, sizeof out.kind_ability_min); break;
		case T_SPELL_ROW:   memcpy(out.spell_row, raw, sizeof out.spell_row);               break;
		case T_SAVES:       memcpy(out.slot_saves, raw, sizeof out.slot_saves);             break;
		case T_LEVEL_CAP:   memcpy(out.race_level_cap, raw, sizeof out.race_level_cap);     break;
		case T_THAC0:       memcpy(out.slot_thac0, raw, sizeof out.slot_thac0);             break;
		case T_ITEM_BIT:    memcpy(out.slot_item_bit, raw, sizeof out.slot_item_bit);       break;
		case T_HALL_BIT:    memcpy(out.slot_hall_bit, raw, sizeof out.slot_hall_bit);       break;
		case T_NAME_LEVEL:  memcpy(out.slot_name_level, raw, sizeof out.slot_name_level);   break;
		case T_HD_COUNT:    memcpy(out.slot_hd_count, raw, sizeof out.slot_hd_count);       break;
		case T_HD_SIZE:     memcpy(out.slot_hd_size, raw, sizeof out.slot_hd_size);         break;
		case T_THIEF:       memcpy(out.thief_skills, raw, sizeof out.thief_skills);         break;
		case T_RACE_THIEF:  memcpy(out.race_thief_adj, raw, sizeof out.race_thief_adj);     break;
		case T_SPELL_SLOTS: memcpy(out.spell_slots, raw, sizeof out.spell_slots);           break;
		case T_LABELS:
			if ((rc = split_labels(raw, k_dqk[tab].len, &out)) != GBR_OK)
				return rc;
			break;
		case T_ORDER_NAMES:
			if ((rc = split_orders(raw, k_dqk[tab].len, &out)) != GBR_OK)
				return rc;
			break;
		}
	}
	*which = 0;
	*t = out;
	return GBR_OK;
}
