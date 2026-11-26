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

/* ---- Champions of Krynn and Death Knights of Krynn ----------------------
 *
 * tools/gbrules.py read_dax is the reference and its docstring the map:
 * the tables are in START.EXE's data segment, 8 slots with the Knight in
 * slot 7, levels to 12 (Champions) or 18; the engine's slot s is the
 * game's k_dax_slot[s], and kinds 1 and 7 trade places. */

enum {
	D_THAC0, D_ITEM_BIT, D_HALL_BIT, D_NAME_LEVEL, D_THIEF, D_RACE_THIEF,
	D_RACE_AB, D_RACE_KINDS, D_START_AGE, D_AGE_CAT, D_KIND_MIN, D_ALIGNS,
	D_PROGS, D_SAVES, D_HD_COUNT, D_HD_SIZE, D_ALIGN_INDEX, D_KIND_NAMES,
	D_RACE_NAMES, D_GOD_NAMES, D_ROBE_NAMES, D_MONEY_NAMES, D_LEVEL_CAP,
	D_COUNT
};

typedef struct dax_game {
	long           image;             /* the load image's length          */
	long           ds;
	int            top;               /* the top level its tables hold    */
	int            progs, prog_rec, spell_levels;
	struct { unsigned short off, len, sum; } tab[D_COUNT];
	long           orders;            /* GAME.OVR: the knightly orders    */
	unsigned short orders_len, orders_sum;
} dax_game;

static const char *const k_dax_names[D_COUNT] = {
	"slot_thac0", "slot_item_bit", "slot_hall_bit", "slot_name_level",
	"thief_skills", "race_thief_adj", "race_abilities", "race_kinds",
	"race_start_age", "race_age_cat", "kind_ability_min", "kind_aligns",
	"progressions", "slot_saves", "slot_hd_count", "slot_hd_size",
	"align_index", "kind_names", "race_names", "god_names", "robe_names",
	"money_names", "race_level_cap"
};

static const dax_game k_dax[2] = {
	{ 65568, 0xbc60, 12, 10, 99, 5, {
		{ 0x3882,  104, 0x11a4 }, { 0x38ea,    8, 0x00ff },
		{ 0x38f2,    8, 0x0051 }, { 0x3903,    8, 0x0059 },
		{ 0x3911,   96, 0x129f }, { 0x3971,   56, 0x085f },
		{ 0x39e0,  112, 0x06bc }, { 0x3a50,  105, 0x0219 },
		{ 0x3ab9,  224, 0x1028 }, { 0x3b99,   70, 0x12bb },
		{ 0x3bdf,  102, 0x01d1 }, { 0x3c45,   17, 0x026a },
		{ 0x3c6d,  990, 0xd72d }, { 0x405b,  480, 0x15fc },
		{ 0x0b6e,    8, 0x000b }, { 0x0b76,    8, 0x0040 },
		{ 0x0a9a,    6, 0x0015 }, { 0x0f5a,  459, 0x3faf },
		{ 0x1140,  128, 0x1e12 }, { 0x1259,   96, 0x164a },
		{ 0x12c0,   21, 0x0506 }, { 0x12e3,   77, 0x112d },
		{ 0,         0, 0 } },              /* no level-cap table */
	  0x26db3L, 41, 0x0d39 },
	{ 82336, 0xbce0, 18, 11, 204, 8, {
		{ 0x71f2,  152, 0x1b1d }, { 0x728a,    8, 0x00ff },
		{ 0x7293,    8, 0x0051 }, { 0x72a4,    8, 0x0059 },
		{ 0x72b4,  144, 0x2385 }, { 0x7344,   56, 0x085f },
		{ 0x73b3,  112, 0x06bf }, { 0x7423,  105, 0x0225 },
		{ 0x748c,  224, 0x1028 }, { 0x756c,   70, 0x12bb },
		{ 0x75b2,  102, 0x0306 }, { 0x7618,   17, 0x030a },
		{ 0x7640, 2244, 0x273f }, { 0x7f14,  720, 0x1ce4 },
		{ 0x0656,    8, 0x000b }, { 0x065e,    8, 0x0040 },
		{ 0x0584,    6, 0x0015 }, { 0x08ad,  459, 0x3faf },
		{ 0x0a93,  128, 0x1e12 }, { 0x0bac,   96, 0x164a },
		{ 0x0c13,   21, 0x0506 }, { 0x0c36,   77, 0x112d },
		{ 0x82d4,   56, 0x01b2 } },
	  0x23cb1L, 41, 0x0d39 },
};

static const unsigned char k_dax_slot[7] = { 0, 7, 2, 3, 4, 5, 6 };
/* the Dark Queen's spell rows by progression (gbrules.read_dax) */
static const unsigned char k_dax_spell_row[GBR_PROGS] = {
	0, 1, 255, 255, 2, 3, 4, 5, 6, 255, 7
};
/* the slots each kind holds, engine numbering, as a bit mask; the slot of
 * each progression (gbrules.KIND_SLOTS_OF, SLOT_OF_PROG) */
static const unsigned char k_kind_slots[GBR_KINDS] = {
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00, 0x05, 0x25, 0x11, 0x21,
	0x41, 0x24, 0x44, 0x64, 0x60
};
static const unsigned char k_prog_slot[GBR_PROGS] = {
	0, 0, 2, 1, 1, 1, 4, 5, 5, 6, 3
};

#define DAX_NEVER     0x7fffffffL
#define DAX_STEP_NONE 0x01ffffffL

static int dax_kind(int k)
{
	return k == 1 ? 7 : k == 7 ? 1 : k;
}

static void put_be32(unsigned char *d, long v)
{
	d[0] = (unsigned char)(v >> 24);
	d[1] = (unsigned char)(v >> 16);
	d[2] = (unsigned char)(v >> 8);
	d[3] = (unsigned char)v;
}

static long get_le32(const unsigned char *s)
{
	unsigned long v = s[0] | (unsigned long)s[1] << 8
	                | (unsigned long)s[2] << 16 | (unsigned long)s[3] << 24;

	return v & 0x80000000UL ? -(long)(~v & 0x7fffffffUL) - 1 : (long)v;
}

/* `count` Pascal strings `stride` apart into out[from..]; GBR_OK or
 * GBR_ENAME. */
static int dax_pascal(const unsigned char *b, int stride, int count,
                      char (*out)[GBR_NAME_MAX], int from)
{
	int i;

	for (i = 0; i < count; i++) {
		int n = b[stride * i];

		if (n >= stride || n >= GBR_NAME_MAX)
			return GBR_ENAME;
		memcpy(out[from + i], b + stride * i + 1, (size_t)n);
		out[from + i][n] = 0;
	}
	return GBR_OK;
}

static void upper(char *d, const char *s)
{
	for (; *s; s++, d++)
		*d = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
	*d = 0;
}

int gbr_load_dax(const unsigned char *img, long len, gbr_read_fn rd, void *ovr,
                 gbr_tables *t, int verify, const char **which, int *game)
{
	static gbr_tables    out;
	static long          xp[GBR_PROGS][GBR_XP_LEVELS];
	unsigned char        orders[64];
	char                 money[7][GBR_NAME_MAX];
	char                 names[GBR_KINDS][GBR_NAME_MAX];
	const dax_game      *g = 0;
	const unsigned char *b;
	int                  tab, top, i, k, s, r, lv, rc;

	*which = 0;
	for (i = 0; i < 2; i++)
		if (len == k_dax[i].image)
			g = &k_dax[i];
	if (g == 0)
		return GBR_EGAME;
	*game = g == &k_dax[0] ? GBR_COK : GBR_DKK;
	for (tab = 0; tab < D_COUNT; tab++) {
		*which = k_dax_names[tab];
		if (g->tab[tab].len == 0)
			continue;
		if (g->ds + g->tab[tab].off + g->tab[tab].len > len)
			return GBR_EREAD;
		if (verify && sum16(img + g->ds + g->tab[tab].off, g->tab[tab].len)
		              != g->tab[tab].sum)
			return GBR_ESUM;
	}
	*which = "orders";
	if (rd(ovr, g->orders, orders, g->orders_len) != g->orders_len)
		return GBR_EREAD;
	if (verify && sum16(orders, g->orders_len) != g->orders_sum)
		return GBR_ESUM;

#define T(x) (img + g->ds + g->tab[x].off)
	memset(&out, 0, sizeof out);
	top = g->top;
	memcpy(out.race_abilities, T(D_RACE_AB), sizeof out.race_abilities);
	memcpy(out.race_thief_adj, T(D_RACE_THIEF), sizeof out.race_thief_adj);
	for (s = 0; s < 7; s++) {
		out.slot_item_bit[s]   = T(D_ITEM_BIT)[k_dax_slot[s]];
		out.slot_hall_bit[s]   = T(D_HALL_BIT)[k_dax_slot[s]];
		out.slot_name_level[s] = T(D_NAME_LEVEL)[k_dax_slot[s]];
		out.slot_hd_count[s]   = T(D_HD_COUNT)[k_dax_slot[s]];
		out.slot_hd_size[s]    = T(D_HD_SIZE)[k_dax_slot[s]];
		/* THAC0 levels 0..top; saves 1..top (level 0 is level 1's) */
		b = T(D_THAC0) + (top + 1) * k_dax_slot[s];
		for (lv = 0; lv < 22; lv++)
			out.slot_thac0[22 * s + lv] = b[lv <= top ? lv : top];
		b = T(D_SAVES) + 5 * top * k_dax_slot[s];
		for (lv = 0; lv < 22; lv++) {
			int from = lv < 1 ? 0 : lv > top ? top - 1 : lv - 1;

			memcpy(out.slot_saves + 110 * s + 5 * lv, b + 5 * from, 5);
		}
	}
	for (lv = 0; lv < 18; lv++)
		memcpy(out.thief_skills + 8 * lv, T(D_THIEF) + 8 * (lv < top ? lv : top - 1), 8);

	for (r = 0; r < GBR_RACES; r++) {
		const unsigned char *row = T(D_RACE_KINDS) + 15 * r;

		out.race_kinds[GBR_KIND_ROW * r] = row[0];
		for (k = 0; k < 14; k++)
			out.race_kinds[GBR_KIND_ROW * r + 1 + k] =
				(unsigned char)(k < row[0] ? dax_kind(row[1 + k]) : row[1 + k]);
		for (s = 0; s < 7; s++) {
			const unsigned char *e = T(D_START_AGE) + 32 * r + 4 * k_dax_slot[s];
			unsigned char       *d = out.race_start_age + 28 * r + 4 * s;

			d[0] = e[1], d[1] = e[0], d[2] = e[2], d[3] = e[3];
		}
	}
	for (k = 0; k < 5 * GBR_RACES; k++)
		be16(out.race_age_cat + 2 * k, T(D_AGE_CAT) + 2 * k);
	for (k = 0; k < GBR_KINDS; k++) {
		unsigned char *al = out.kind_aligns + 12 * dax_kind(k);
		int            j, n = 0;

		memcpy(out.kind_ability_min + 6 * dax_kind(k), T(D_KIND_MIN) + 6 * k, 6);
		for (j = 0; j < 6; j++)
			if (T(D_ALIGNS)[k] >> j & 1)
				al[1 + n++] = T(D_ALIGN_INDEX)[j];
		al[0] = (unsigned char)n;
	}

	/* progressions: XP to levels 2..top, then the spells gained at each */
	for (i = 0; i < GBR_PROGS; i++) {
		const unsigned char *p = T(D_PROGS) + (long)g->prog_rec * i;
		int                  row = k_dax_spell_row[i];
		unsigned char        have[9] = { 0 };

		for (lv = 0; lv < GBR_XP_LEVELS; lv++) {
			long v = lv < 2 ? 0 : DAX_NEVER;

			if (lv >= 2 && lv <= top && i < g->progs) {
				v = get_le32(p + 4 * (lv - 2));
				if (v <= 0)
					v = DAX_NEVER;
			}
			xp[i][lv] = v;
			put_be32(out.xp + 4 * (GBR_XP_LEVELS * i + lv), v);
		}
		put_be32(out.xp_step + 4 * i, DAX_STEP_NONE);
		out.spell_row[i] = (unsigned char)row;
		if (row == 255)
			continue;
		if (i == 0 || i == 1 || i == 7 || i == 8)
			have[0] = 1;            /* a cleric's or mage's first */
		for (lv = 1; lv < 30; lv++) {
			if (lv >= 2 && lv <= top && i < g->progs)
				for (k = 0; k < g->spell_levels; k++)
					have[k] = (unsigned char)(have[k]
					    + p[4 * (top - 1) + g->spell_levels * (lv - 2) + k]);
			memcpy(out.spell_slots + 261 * row + 9 * (lv - 1), have, 9);
		}
	}

	for (r = 0; r < GBR_RACES; r++) {
		if (g->tab[D_LEVEL_CAP].len != 0) {
			for (s = 0; s < 7; s++)
				out.race_level_cap[7 * r + s] = T(D_LEVEL_CAP)[8 * r + k_dax_slot[s]];
			continue;
		}
		/* Champions: where a class's XP stops, for a race that may be it;
		 * a Kender fighter or ranger 7 (OVR 0x25061) */
		for (s = 0; s < 7; s++) {
			const unsigned char *row = out.race_kinds + GBR_KIND_ROW * r;
			int                  offered = 0, cap = 0;

			for (k = 0; k < row[0] && k < 14; k++)
				if (row[1 + k] < GBR_KINDS && k_kind_slots[row[1 + k]] >> s & 1)
					offered = 1;
			if (!offered)
				continue;
			for (i = 0; i < GBR_PROGS; i++) {
				if (k_prog_slot[i] != s)
					continue;
				for (lv = GBR_XP_LEVELS - 1; lv >= 1; lv--)
					if (lv < 2 || xp[i][lv] != DAX_NEVER)
						break;
				if (lv > cap)
					cap = lv;
			}
			if (r == 5 && (s == 2 || s == 4))
				cap = 7;
			out.race_level_cap[7 * r + s] = (unsigned char)cap;
		}
	}

	*which = "kind_names";
	if ((rc = dax_pascal(T(D_KIND_NAMES), 27, GBR_KINDS, names, 0)) != GBR_OK)
		return rc;
	for (k = 0; k < GBR_KINDS; k++) {
		strcpy(out.kind_name[k], names[dax_kind(k)]);
		strcpy(out.sheet_kind[k], names[dax_kind(k)]);
		upper(out.kind_label[k], names[dax_kind(k)]);
	}
	*which = "race_names";
	if ((rc = dax_pascal(T(D_RACE_NAMES), 16, GBR_RACES + 1, out.race_name, 0)) != GBR_OK)
		return rc;
	for (r = 0; r < GBR_RACES; r++)
		upper(out.race_label[r], out.race_name[r]);
	*which = "money_names";
	if ((rc = dax_pascal(T(D_MONEY_NAMES), 11, 7, money, 0)) != GBR_OK)
		return rc;
	for (i = 0; i < GBR_MONEY; i++)         /* Steel, Gems, Jewelry */
		strcpy(out.money_name[i], money[4 + i]);
	*which = "god_names";
	if ((rc = dax_pascal(T(D_GOD_NAMES), 12, GBR_GODS, out.god_name, 0)) != GBR_OK)
		return rc;
	*which = "robe_names";
	if ((rc = dax_pascal(T(D_ROBE_NAMES), 7, GBR_ROBES - 1, out.robe_name, 1)) != GBR_OK)
		return rc;
	*which = "orders";
	for (i = 1, k = 0; i < GBR_ORDERS; i++) {
		int n = orders[k];

		if (k + 1 + n > g->orders_len || n >= GBR_NAME_MAX)
			return GBR_ENAME;
		memcpy(out.order_name[i], orders + k + 1, (size_t)n);
		out.order_name[i][n] = 0;
		k += 1 + n;
	}
#undef T
	*which = 0;
	*t = out;
	return GBR_OK;
}
