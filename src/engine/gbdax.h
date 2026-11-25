/*
 * gbdax.h — a Gold Box DOS .DAX archive (Champions and Death Knights of
 * Krynn keep their maps, scripts, pictures and monsters in them). Plain C99
 * with no engine, shim or platform headers, so it builds on the host for
 * tests/test_gbdax.py; the caller reads the file.
 *
 * tools/ecl.py dax_blocks is the reference (docs/dax-format.md): a u16 table
 * length, then 9-byte entries {u8 id, u32 offset, u16 size, u16 packed},
 * then the packed blocks, each a run of {c < 0x80: c + 1 literal bytes;
 * c >= 0x80: the next byte 256 - c times}.
 */
#ifndef ENGINE_GBDAX_H
#define ENGINE_GBDAX_H

#define GBDAX_ENOTDAX  -1       /* not an archive                          */
#define GBDAX_ENOBLOCK -2       /* no block of that id                     */
#define GBDAX_EBLOCK   -3       /* the block runs past the file, or does
                                   not unpack to its size                  */
#define GBDAX_EROOM    -4       /* `out` is too small                      */

/* The number of blocks, writing up to `max` of their ids into `ids` in the
 * archive's order; or GBDAX_ENOTDAX. */
int gbdax_ids(const unsigned char *d, long len, unsigned char *ids, int max);

/* Unpack block `id` into `out` (room for `max`): its size, or a GBDAX_E
 * code. */
long gbdax_block(const unsigned char *d, long len, int id,
                 unsigned char *out, long max);

#endif
