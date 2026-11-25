/*
 * gbdaxmon.h — Champions' and Death Knights' monsters and items, read as
 * the games keep them, in the forms the engine takes from the Dark Queen
 * (ADR-0026). Plain C99 with no engine, shim or platform headers, so it
 * builds on the host for tests/test_gbdaxmon.py.
 *
 * tools/gbdaxmon.py is the reference and its docstring the map: a monster
 * is a character, carried from game to game by the games' own imports
 * (Death Knights' GAME.OVR 0x12cb4 for Champions', DQK.EXE 0x33ec5 for
 * Death Knights'), and the Dark Queen's record is what gbmon.c already
 * turns into FRUA's. An item's 17 bytes go across as DQK.EXE 0x3497b
 * turns them, its word, spell and effect numbers translated.
 */
#ifndef ENGINE_GBDAXMON_H
#define ENGINE_GBDAXMON_H

#define GBX_COK        409      /* a Champions character                    */
#define GBX_DKK        216      /* a Death Knights character                */
#define GBX_DQK      0x19e      /* the Dark Queen's, its fixed part         */
#define GBX_MEM       0x3a      /* memorized spells in a caster block       */
#define GBX_ITEM_REC    63      /* an item record                           */
#define GBX_ITEM_TAIL 0x2e      /* ... where its 17 item bytes start        */
#define GBX_ITEM        17
#define GBX_NODE         9      /* an effect node                           */
#define GBX_WORDS      125      /* a game's word table, from word 1         */
#define GBX_WORD_MAX    21      /* string[20] and the NUL                   */
#define GBX_ITEMS      254      /* an item table at most                    */

#define GBX_ESHORT -1
#define GBX_EPACK  -2           /* not an EXEPACK program as expected       */
#define GBX_EROOM  -3

/* ---- words ------------------------------------------------------------ */

/* A Microsoft EXEPACK program (START.EXE) unpacked into `out` (`max`
 * bytes): the load image's length, or a GBX_E code. */
long gbx_unexepack(const unsigned char *exe, long len, unsigned char *out, long max);

/* The game's words 1..125 out of the unpacked image (a table of string[20]
 * from "Battle Axe") into words[1..125]; 0, or GBX_ESHORT when there is
 * no such table. */
int gbx_game_words(const unsigned char *img, long len,
                   char words[GBX_WORDS + 1][GBX_WORD_MAX]);

/* The game's word that the Dark Queen moves to word `number` (126..153),
 * or 0 for a number it moves nothing to. */
int gbx_word_source(int number);

/* ---- items ------------------------------------------------------------ */

/* The 17 item bytes as the game keeps them (a record from +0x2e, or a
 * Death Knights ITEM0.DAX entry) -> the Dark Queen's ITEM.DAT entry. */
void gbx_item17(const unsigned char *t, unsigned char *q);

/* Add entry `q` (17 bytes, the Dark Queen's) to `table` of `n` entries
 * unless it is there already (the readied byte not counted); the new
 * count. `table` has room for GBX_ITEMS. */
int gbx_table_add(unsigned char *table, int n, const unsigned char *q);

/* Append entry `q` whether or not it is there (Death Knights' ITEM0.DAX,
 * whose places are its scripts' item numbers), not readied; the new count. */
int gbx_table_put(unsigned char *table, int n, const unsigned char *q);

/* The number (entry + 1) of `q` in `table`, the readied byte not counted;
 * 0 when it is not there. */
int gbx_table_find(const unsigned char *table, int n, const unsigned char *q);

/* ---- monsters --------------------------------------------------------- */

/* A Champions character -> Death Knights' (`k`, GBX_DKK bytes) as Death
 * Knights imports one; its memorized spells into `mem` (GBX_MEM). Returns
 * 1 when it has them, 0 when its class has none, GBX_ESHORT. */
int gbx_cok_to_dkk(const unsigned char *c, long len, unsigned char *k,
                   unsigned char *mem);

/* A Death Knights character -> the Dark Queen's fixed record (`q`,
 * GBX_DQK bytes) as the Dark Queen imports one. 0, or GBX_ESHORT. */
int gbx_dkk_to_dqk(const unsigned char *k, long len, unsigned char *q);

/* The Dark Queen's MONCHA.GLB member gbm_convert reads, into `out`: the
 * fixed record `q`, the memorized spells `mem` (GBX_MEM bytes, NULL for
 * none) into its list, the effects of `spc` (9-byte nodes) and the items
 * of `itm` (63-byte records). Its length, or GBX_EROOM. */
long gbx_member(const unsigned char *q, const unsigned char *mem,
                const unsigned char *spc, long spc_len,
                const unsigned char *itm, long itm_len,
                unsigned char *out, long max);

#endif
