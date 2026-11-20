/*
 * gbitems.c — a Gold Box game's item data read as the game keeps it
 * (ADR-0026 step (f)). See gbitems.h; tools/gbgeo.py is the reference.
 */
#include <string.h>

#include "gbitems.h"

#define DQK_DS     0x63a20L     /* file offset of the data segment         */
#define DQK_DS_SEG 0x322e

/* The item-name word table: 256 far pointers. Its words past FRUA's 126
 * (Hoopak, Dragonlance, ...) end where the old-age death messages begin,
 * which follow the words in the same table. The sum is of those words'
 * strings, NULs included. */
#define WORDS_OFF  0x3f55
#define WORDS_SUM  0x637c
static const char k_words_end[] = "who is looking very old";

/* The shops: nine first indexes, nine last indexes (1-based, each shop
 * starting one past the one before), then the list of item numbers, all
 * 16-bit. The sum is over all three. */
#define SHOPS_OFF  0x75c6
#define SHOPS_SUM  0x1cb7

long gbi_item_table(const unsigned char *in, long len, unsigned char *out)
{
	long k;

	if (len < 0 || len % GBI_ITEM_IN != 0)
		return -1;
	for (k = 0; k < GBI_ITEMS; k++) {
		unsigned char *d = out + k * GBI_ITEM_OUT;

		if ((k + 1) * GBI_ITEM_IN <= len)
			memcpy(d, in + k * GBI_ITEM_IN, GBI_ITEM_IN);
		else
			memset(d, 0xff, GBI_ITEM_IN);
		d[GBI_ITEM_IN] = 0;
	}
	return (long)GBI_ITEMS * GBI_ITEM_OUT;
}

static unsigned le16(const unsigned char *p)
{
	return p[0] | (unsigned)p[1] << 8;
}

static int load_words(gbr_read_fn rd, void *ctx, gbi_tables *t, int verify)
{
	static unsigned char ptrs[4 * 256];
	unsigned short sum = 0;
	int            k;

	if (rd(ctx, DQK_DS + WORDS_OFF, ptrs, sizeof ptrs) != (long)sizeof ptrs)
		return GBI_EREAD;
	t->words = 0;
	for (k = GBI_FIRST_WORD; k < 256 && t->words < GBI_WORDS; k++) {
		char *w = t->word[t->words];
		long  n;
		int   i;

		if (le16(ptrs + 4 * k + 2) != DQK_DS_SEG)
			break;
		n = rd(ctx, DQK_DS + (long)le16(ptrs + 4 * k), w, GBI_WORD_MAX - 1);
		if (n <= 0)
			break;
		w[n] = 0;
		for (i = 0; i < n && w[i] != 0; i++)
			;
		if (i == n)                     /* no NUL: not a word */
			return GBI_ESHAPE;
		if (strncmp(w, k_words_end, sizeof k_words_end - 1) == 0)
			break;
		memset(w + i, 0, (size_t)(GBI_WORD_MAX - i));
		for (i = 0; w[i] != 0; i++)
			sum = (unsigned short)(sum + (unsigned char)w[i]);
		t->words++;
	}
	if (t->words == 0)
		return GBI_ESHAPE;
	if (verify && sum != WORDS_SUM)
		return GBI_ESUM;
	return GBI_OK;
}

static int load_shops(gbr_read_fn rd, void *ctx, gbi_tables *t, int verify)
{
	static unsigned char raw[36 + 2 * GBI_SHOPS * GBI_SHOP_MAX];
	unsigned short sum = 0;
	unsigned       first[GBI_SHOPS], last[GBI_SHOPS], n;
	int            s, k;

	if (rd(ctx, DQK_DS + SHOPS_OFF, raw, 36) != 36)
		return GBI_EREAD;
	for (s = 0; s < GBI_SHOPS; s++) {
		first[s] = le16(raw + 2 * s);
		last[s] = le16(raw + 18 + 2 * s);
		if (first[s] != (s ? last[s - 1] + 1 : 1) || last[s] < first[s]
		 || last[s] - first[s] + 1 > GBI_SHOP_MAX)
			return GBI_ESHAPE;
	}
	n = last[GBI_SHOPS - 1];
	if (n > GBI_SHOPS * GBI_SHOP_MAX)
		return GBI_ESHAPE;
	if (rd(ctx, DQK_DS + SHOPS_OFF + 36, raw + 36, 2 * (long)n) != 2 * (long)n)
		return GBI_EREAD;
	for (k = 0; k < 36 + 2 * (int)n; k++)
		sum = (unsigned short)(sum + raw[k]);
	if (verify && sum != SHOPS_SUM)
		return GBI_ESUM;
	for (s = 0; s < GBI_SHOPS; s++) {
		t->shop_n[s] = (unsigned char)(last[s] - first[s] + 1);
		for (k = 0; k < t->shop_n[s]; k++) {
			unsigned v = le16(raw + 36 + 2 * (first[s] - 1 + k));

			if (v == 0 || v > 255)
				return GBI_ESHAPE;
			t->shop[s][k] = (unsigned char)v;
		}
	}
	return GBI_OK;
}

int gbi_load_dqk(gbr_read_fn rd, void *ctx, gbi_tables *t, int verify,
                 const char **which)
{
	int rc;

	memset(t, 0, sizeof *t);
	*which = "item words";
	rc = load_words(rd, ctx, t, verify);
	if (rc != GBI_OK)
		return rc;
	*which = "shops";
	return load_shops(rd, ctx, t, verify);
}
