/*
 * gbitems.h — a Gold Box game's item data read as the game keeps it
 * (ADR-0026 step (f)): its item table (ITEM.DAT) and, out of DQK.EXE, the
 * item-name words past FRUA's and the shops' stock lists. Plain C99 with no
 * engine, shim or platform headers, so it builds on the host for
 * tests/test_gbitems.py.
 *
 * tools/gbgeo.py is the reference: item_table(), find_words() and
 * find_shops() describe the same data. They find the tables by shape; this
 * reads them at the Steam DQK.EXE's positions, each checked by a sum.
 */
#ifndef ENGINE_GBITEMS_H
#define ENGINE_GBITEMS_H

#include "rules_gb.h"           /* gbr_read_fn */

#define GBI_ITEMS      254      /* entries in ITEM.DAT and in FRUA's table  */
#define GBI_ITEM_IN     17      /* bytes an entry, the Dark Queen's         */
#define GBI_ITEM_OUT    18      /* FRUA's                                   */
#define GBI_FIRST_WORD 126      /* FRUA's table has words 0..125            */
#define GBI_WORDS       64
#define GBI_WORD_MAX    41      /* 40 characters and the NUL                */
#define GBI_SHOPS        9
#define GBI_SHOP_MAX   128

/* ITEM.DAT (`len` bytes) as FRUA's 254 entries of 18 bytes into `out`
 * (GBI_ITEMS * GBI_ITEM_OUT bytes): each entry the same fields with a 0 on
 * the end, a short file padded with 0xff entries. Returns the bytes written,
 * or -1 when `len` is not a whole number of entries. */
long gbi_item_table(const unsigned char *in, long len, unsigned char *out);

typedef struct gbi_tables {
	unsigned char words;                    /* from GBI_FIRST_WORD          */
	char          word[GBI_WORDS][GBI_WORD_MAX];
	unsigned char shop_n[GBI_SHOPS];        /* shop type n+1: its stock     */
	unsigned char shop[GBI_SHOPS][GBI_SHOP_MAX];
} gbi_tables;

#define GBI_OK      0
#define GBI_EREAD  -1
#define GBI_ESUM   -2           /* not the build the positions are for      */
#define GBI_ESHAPE -3           /* the tables are not shaped as they should */

/* Fill `t` from DQK.EXE. With `verify`, each table's sum must be the Steam
 * build's; without, the positions alone are trusted (synthetic tests). On
 * an error *which names the table. */
int gbi_load_dqk(gbr_read_fn rd, void *ctx, gbi_tables *t, int verify,
                 const char **which);

#endif
