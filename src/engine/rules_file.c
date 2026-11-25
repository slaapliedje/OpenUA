/*
 * rules_file.c — a ruleset file (RULES.DAT) parsed in place (ADR-0026 step
 * (g)). See rules_file.h; tools/rules.py is the reference.
 */
#include <string.h>

#include "rules_file.h"

/* Each table's tag, element width and shape, as rules.py TABLES. A shape
 * is rows x columns, each a constant or one of the header's numbers. */
enum { N_ONE = -1, N_RACES = -2, N_PROGS = -3, N_LEVELS = -4, N_KROW = -5,
       N_SPELLS29 = -6 };

static const struct {
	char tag[5];
	short width, rows, cols;
} k_tab[RLF_TABLES] = {
	{ "RBAS", 1, N_ONE,      N_RACES   },
	{ "KLST", 1, N_ONE,      RLF_KINDS },
	{ "RKND", 1, N_RACES,    N_KROW    },
	{ "KALN", 1, RLF_KINDS,  12        },
	{ "KMIN", 1, RLF_KINDS,  6         },
	{ "RABL", 1, N_RACES,    16        },
	{ "RAGC", 2, N_RACES,    5         },
	{ "RSTA", 4, N_RACES,    RLF_SLOTS },
	{ "RCAP", 1, N_RACES,    RLF_SLOTS },
	{ "XPTB", 4, N_PROGS,    N_LEVELS  },
	{ "XPST", 4, N_ONE,      N_PROGS   },
	{ "SROW", 1, N_ONE,      N_PROGS   },
	{ "SSLT", 1, N_SPELLS29, 9         },
	{ "SPRG", 1, N_ONE,      RLF_SLOTS },
	{ "THAC", 1, RLF_SLOTS,  22        },
	{ "SAVE", 1, RLF_SLOTS * 22, 5     },
	{ "IBIT", 1, N_ONE,      RLF_SLOTS },
	{ "HBIT", 1, N_ONE,      RLF_SLOTS },
	{ "NLVL", 1, N_ONE,      RLF_SLOTS },
	{ "HDCT", 1, N_ONE,      RLF_SLOTS },
	{ "HDSZ", 1, N_ONE,      RLF_SLOTS },
	{ "THSK", 1, 18,         8         },
	{ "RTHF", 1, N_RACES,    8         },
};

static const unsigned char k_sel_len[RLF_SELECTORS] = { 8, 4, 4 };

const char *rlf_tag(int table)
{
	return table >= 0 && table < RLF_TABLES ? k_tab[table].tag : "-";
}

static long dim(const rlf_rules *r, int n)
{
	switch (n) {
	case N_ONE:      return 1;
	case N_RACES:    return r->races;
	case N_PROGS:    return r->progressions;
	case N_LEVELS:   return r->xp_levels;
	case N_KROW:     return r->kind_row;
	case N_SPELLS29: return (long)r->spell_rows * 29;
	default:         return n;
	}
}

long rlf_table_len(const rlf_rules *r, int table)
{
	if (table < 0 || table >= RLF_TABLES)
		return 0;
	return dim(r, k_tab[table].rows) * dim(r, k_tab[table].cols)
	     * k_tab[table].width;
}

typedef struct {
	const unsigned char *data;
	long                 len;
} chunk;

/* `n` NUL-ended strings filling the chunk exactly. */
static int strings(chunk c, const char **out, int n)
{
	long p = 0;
	int  i;

	for (i = 0; i < n; i++) {
		const unsigned char *e;

		if (p >= c.len)
			return 0;
		e = memchr(c.data + p, 0, (size_t)(c.len - p));
		if (e == NULL)
			return 0;
		out[i] = (const char *)c.data + p;
		p = (long)(e - c.data) + 1;
	}
	return p == c.len;
}

int rlf_parse(const unsigned char *q, long len, rlf_rules *r, const char **what)
{
	static const char *const named[] = { "HEAD", "NAME", "COIN", "KNAM",
	                                     "RNAM", "KLAB", "RLAB", "PSEL",
	                                     "SHET", "SLOT", "PROG" };
	enum { C_HEAD, C_NAME, C_COIN, C_KNAM, C_RNAM, C_KLAB, C_RLAB, C_PSEL,
	       C_SHET, C_SLOT, C_PROG, C_NAMED };
	chunk    c[C_NAMED], t[RLF_TABLES];
	long     p = 8;
	int      i, k;
	const char *dummy;

	if (what == NULL)
		what = &dummy;
	*what = 0;
	memset(r, 0, sizeof *r);
	memset(c, 0, sizeof c);
	memset(t, 0, sizeof t);
	if (len < 8 || memcmp(q, "UARULES1", 8) != 0)
		return RLF_EMAGIC;
	while (p < len) {
		long n;

		if (p + 8 > len)
			return RLF_ECHUNK;
		n = (long)q[p + 4] << 8 | q[p + 5];
		if (p + 8 + n > len) {
			*what = (const char *)q + p;    /* not NUL-ended: 4 chars */
			return RLF_ECHUNK;
		}
		for (i = 0; i < C_NAMED; i++)
			if (memcmp(q + p, named[i], 4) == 0)
				c[i].data = q + p + 8, c[i].len = n;
		for (i = 0; i < RLF_TABLES; i++)
			if (memcmp(q + p, k_tab[i].tag, 4) == 0)
				t[i].data = q + p + 8, t[i].len = n;
		p += 8 + n + (-n & 3);
	}

	*what = "HEAD";
	if (c[C_HEAD].data == NULL || c[C_HEAD].len != 8)
		return RLF_EHEAD;
	r->races        = c[C_HEAD].data[0];
	r->progressions = c[C_HEAD].data[1];
	r->xp_levels    = c[C_HEAD].data[2];
	r->kind_row     = c[C_HEAD].data[3];
	r->spell_rows   = c[C_HEAD].data[4];
	r->default_race = c[C_HEAD].data[5];
	r->flags        = c[C_HEAD].data[6];
	if (r->races < 1 || r->races > RLF_MAX_RACES
	    || r->progressions < 1 || r->progressions > RLF_MAX_PROGS
	    || r->xp_levels < 2 || r->xp_levels > RLF_MAX_LEVELS
	    || r->kind_row < 2 || r->kind_row > RLF_KINDS + 1
	    || r->spell_rows < 1 || r->spell_rows > RLF_MAX_SPELLS
	    || r->default_race >= r->races)
		return RLF_EHEAD;

	*what = "NAME";
	if (c[C_NAME].data == NULL)
		return RLF_EMISSING;
	if (!strings(c[C_NAME], &r->name, 1))
		return RLF_ENAMES;
	*what = "COIN";
	if (c[C_COIN].data != NULL && !strings(c[C_COIN], &r->coin, 1))
		return RLF_ENAMES;
	{
		static const struct { int chunk, count; } lists[4] = {
			{ C_KNAM, RLF_KINDS }, { C_RNAM, -1 }, { C_KLAB, RLF_KINDS },
			{ C_RLAB, -2 }
		};
		const char **dst[4];

		dst[0] = r->kind_name;
		dst[1] = r->race_name;
		dst[2] = r->kind_label;
		dst[3] = r->race_label;
		for (i = 0; i < 4; i++) {
			int n = lists[i].count == -1 ? r->races + 1
			      : lists[i].count == -2 ? r->races : lists[i].count;

			*what = named[lists[i].chunk];
			if (c[lists[i].chunk].data == NULL)
				return RLF_EMISSING;
			if (!strings(c[lists[i].chunk], dst[i], n))
				return RLF_ENAMES;
		}
	}

	/* the text's names: optional here, the engine never shows them */
	*what = "SLOT";
	if (c[C_SLOT].data != NULL && !strings(c[C_SLOT], r->slot_name, RLF_SLOTS))
		return RLF_ENAMES;
	*what = "PROG";
	if (c[C_PROG].data != NULL
	    && !strings(c[C_PROG], r->prog_name, r->progressions))
		return RLF_ENAMES;

	*what = "SHET";
	if (c[C_SHET].data != NULL) {
		const char *w[RLF_KINDS + RLF_GODS + RLF_ROBES + RLF_ORDERS];

		if (!strings(c[C_SHET], w, (int)(sizeof w / sizeof w[0])))
			return RLF_ENAMES;
		for (i = 0; i < (int)(sizeof w / sizeof w[0]); i++)
			if (strlen(w[i]) > 31)          /* the sheet's line has room */
				return RLF_ENAMES;
		memcpy(r->sheet_kind, w, sizeof r->sheet_kind);
		memcpy(r->god_name, w + RLF_KINDS, sizeof r->god_name);
		memcpy(r->robe_name, w + RLF_KINDS + RLF_GODS, sizeof r->robe_name);
		memcpy(r->order_name, w + RLF_KINDS + RLF_GODS + RLF_ROBES,
		       sizeof r->order_name);
	}

	for (i = 0; i < RLF_TABLES; i++) {
		long want = dim(r, k_tab[i].rows) * dim(r, k_tab[i].cols)
		          * k_tab[i].width;

		*what = k_tab[i].tag;
		if (t[i].data == NULL)
			return RLF_EMISSING;
		if (t[i].len != want)
			return RLF_ESIZE;
		r->table[i] = t[i].data;
		r->table_len[i] = t[i].len;
	}

	/* what the engine would index out of bounds with */
	*what = "RBAS";
	for (i = 0; i < r->races; i++)
		if (r->table[RLF_RACE_BASE][i] > 6)
			return RLF_EVALUE;
	*what = "SROW";
	for (i = 0; i < r->progressions; i++) {
		unsigned char v = r->table[RLF_SLOT_SPELL_ROW][i];

		if (v != 255 && v >= r->spell_rows)
			return RLF_EVALUE;
	}
	*what = "SPRG";
	for (i = 0; i < RLF_SLOTS; i++)
		if (r->table[RLF_SLOT_PROG][i] >= r->progressions)
			return RLF_EVALUE;
	*what = "RKND";
	for (i = 0; i < r->races; i++) {
		const unsigned char *e = r->table[RLF_RACE_KINDS] + (long)i * r->kind_row;

		if (e[0] > r->kind_row - 1)
			return RLF_EVALUE;
		for (k = 0; k < e[0]; k++)
			if (e[1 + k] >= RLF_KINDS)
				return RLF_EVALUE;
	}

	for (i = 0; i < RLF_SELECTORS; i++)
		r->sel_slot[i] = 255;
	if (c[C_PSEL].data != NULL) {
		const unsigned char *s = c[C_PSEL].data;

		*what = "PSEL";
		if (c[C_PSEL].len != 3 + 8 + 4 + 4)
			return RLF_ESIZE;
		for (i = 0; i < RLF_SELECTORS; i++) {
			if (s[0] != 255) {
				if (s[0] >= RLF_SLOTS)
					return RLF_EVALUE;
				for (k = 0; k < k_sel_len[i]; k++)
					if (s[1 + k] >= r->progressions)
						return RLF_EVALUE;
				r->sel_slot[i] = s[0];
				r->sel[i] = s + 1;
			}
			s += 1 + k_sel_len[i];
		}
	}
	*what = 0;
	return 0;
}

/* ---- writing --------------------------------------------------------- */

typedef struct {
	unsigned char *out;
	long           max, n, start;
	int            bad;
} wbuf;

static void put(wbuf *w, const void *p, long n)
{
	if (w->bad || w->n + n > w->max) {
		w->bad = 1;
		return;
	}
	memcpy(w->out + w->n, p, (size_t)n);
	w->n += n;
}

/* A chunk is its tag, a big-endian u16 length, a u16 0, the data, and
 * zeros to a multiple of 4: open one, put its data, close it. */
static void open_chunk(wbuf *w, const char *tag)
{
	static const unsigned char zero[4];

	put(w, tag, 4);
	put(w, zero, 4);
	w->start = w->n;
}

static void close_chunk(wbuf *w)
{
	static const unsigned char zero[4];
	long n = w->n - w->start;

	if (w->bad)
		return;
	if (n > 0xffff) {
		w->bad = 1;
		return;
	}
	w->out[w->start - 4] = (unsigned char)(n >> 8);
	w->out[w->start - 3] = (unsigned char)n;
	put(w, zero, -n & 3);
}

static void put_strings(wbuf *w, const char *const *xs, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		size_t k;

		if (xs[i] == NULL || (k = strlen(xs[i])) > 40) {
			w->bad = 1;
			return;
		}
		put(w, xs[i], (long)k + 1);
	}
}

static void string_chunk(wbuf *w, const char *tag, const char *const *xs, int n)
{
	open_chunk(w, tag);
	put_strings(w, xs, n);
	close_chunk(w);
}

long rlf_write(const rlf_rules *r, unsigned char *out, long max)
{
	wbuf w;
	unsigned char head[8];
	int  i, k, any = 0;

	w.out = out, w.max = max, w.n = 0, w.start = 0, w.bad = 0;
	put(&w, "UARULES1", 8);
	head[0] = r->races, head[1] = r->progressions, head[2] = r->xp_levels;
	head[3] = r->kind_row, head[4] = r->spell_rows;
	head[5] = r->default_race, head[6] = r->flags, head[7] = 0;
	open_chunk(&w, "HEAD");
	put(&w, head, 8);
	close_chunk(&w);
	string_chunk(&w, "NAME", &r->name, 1);
	if (r->coin != NULL)
		string_chunk(&w, "COIN", &r->coin, 1);
	string_chunk(&w, "KNAM", r->kind_name, RLF_KINDS);
	string_chunk(&w, "RNAM", r->race_name, r->races + 1);
	string_chunk(&w, "KLAB", r->kind_label, RLF_KINDS);
	string_chunk(&w, "RLAB", r->race_label, r->races);
	string_chunk(&w, "SLOT", r->slot_name, RLF_SLOTS);
	string_chunk(&w, "PROG", r->prog_name, r->progressions);
	for (i = 0; i < RLF_TABLES; i++) {
		long want = dim(r, k_tab[i].rows) * dim(r, k_tab[i].cols)
		          * k_tab[i].width;

		if (r->table[i] == NULL || r->table_len[i] != want)
			return -1;
		open_chunk(&w, k_tab[i].tag);
		put(&w, r->table[i], want);
		close_chunk(&w);
	}
	if (r->god_name[0] != NULL) {
		open_chunk(&w, "SHET");
		put_strings(&w, r->sheet_kind, RLF_KINDS);
		put_strings(&w, r->god_name, RLF_GODS);
		put_strings(&w, r->robe_name, RLF_ROBES);
		put_strings(&w, r->order_name, RLF_ORDERS);
		close_chunk(&w);
	}
	for (i = 0; i < RLF_SELECTORS; i++)
		if (r->sel_slot[i] != 255)
			any = 1;
	if (any) {
		open_chunk(&w, "PSEL");
		for (i = 0; i < RLF_SELECTORS; i++) {
			put(&w, &r->sel_slot[i], 1);
			for (k = 0; k < k_sel_len[i]; k++) {
				unsigned char v = r->sel_slot[i] != 255 && r->sel[i] != NULL
				                ? r->sel[i][k] : 0;

				put(&w, &v, 1);
			}
		}
		close_chunk(&w);
	}
	return w.bad ? -1 : w.n;
}
