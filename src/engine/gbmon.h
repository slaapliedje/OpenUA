/*
 * gbmon.h — a Gold Box game's monsters read as the game keeps them
 * (ADR-0026 step (f)): a record out of its monster library (the Dark
 * Queen's MONCHA.GLB) rewritten in FRUA's 450-byte layout. Plain C99 with
 * no engine, shim or platform headers, so it builds on the host for
 * tests/test_gbmon_c.py.
 *
 * tools/gbmon.py is the reference: load_library(), unpack() and convert()
 * describe the same work, and its docstring has the field map.
 */
#ifndef ENGINE_GBMON_H
#define ENGINE_GBMON_H

#include "rules_gb.h"           /* gbr_read_fn */

#define GBM_SIZE     450        /* FRUA's monster record                    */
#define GBM_MAX     1024        /* a Gold Box record, unpacked, at most     */
#define GBM_ITEM_IN   17        /* an ITEM.DAT entry, the Dark Queen's      */

#define GBM_ENOTLIB -1          /* not a GLIB/HLIB library                  */
#define GBM_ENONE   -2          /* no member of that number                 */
#define GBM_EREAD   -3
#define GBM_ESHAPE  -4          /* a member that does not unpack            */

/* Member `id` of a GLIB (big-endian) or HLIB (little-endian) library read
 * through `rd`: its id table (member 0) is searched, and the member, a
 * big-endian length word and PackBits, unpacked into `out` (`max` bytes).
 * Returns the unpacked length, or a GBM_E code. */
long gbm_member(gbr_read_fn rd, void *ctx, int id, unsigned char *out, long max);

/* One Dark Queen record (`len` bytes) as FRUA's into `out` (GBM_SIZE).
 * A carried item is found in `items` (ITEM.DAT, `items_len` bytes) by its
 * first ten bytes and stored as its number, entry k being item k + 1;
 * NULL leaves the items off. `native` keeps the game's race numbers and
 * its god, robe and order (a design playing by its rules). Returns 0, or
 * -1 for a record too short to be one. */
int gbm_convert(const unsigned char *q, long len, const unsigned char *items,
                long items_len, int native, unsigned char *out);

#endif
