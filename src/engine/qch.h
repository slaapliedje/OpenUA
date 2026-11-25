/*
 * qch.h — a Dark Queen of Krynn character file (.QCH) read as the .CCH
 * stream FRUA's own loader (jt577) takes (ADR-0026 step (d)). Plain C99 with
 * no engine, shim or platform headers, so it builds on the host for
 * tests/test_qch.py; the caller reads the file.
 *
 * tools/gbchar.py is the reference: what a .QCH holds and how each field
 * lands in FRUA's record are described there and in tools/gbmon.py.
 */
#ifndef ENGINE_QCH_H
#define ENGINE_QCH_H

#define QCH_FIXED   0x19e       /* the record, before its items           */
#define QCH_ITEM    17          /* one ITEM.DAT entry                     */
#define QCH_NODE    9           /* an affect: 5 data bytes + far pointer  */
#define QCH_REC     398         /* the .CCH record                        */
#define QCH_NAME    0x68        /* the name, 16 bytes (FRUA's rec[96])    */

#define QCH_ESHORT  -1          /* shorter than the record                */
#define QCH_ESPLIT  -2          /* the rest is not items + 9-byte nodes   */
#define QCH_EROOM   -3          /* `out` is too small                     */

/* The .CCH stream's length for this file, or a QCH_E* error. */
long qch_cch_size(const unsigned char *q, long len);

/* Write the .CCH stream for the .QCH `q` into `out` (room for `max`) and
 * return its length, or a QCH_E* error. `native`: the design plays by the
 * Dark Queen's own rules, so the race stays the game's; otherwise it is
 * mapped to FRUA's (gbmon.RACE). */
long qch_to_cch(const unsigned char *q, long len, unsigned char *out,
                long max, int native);

/* The inverse, for a design on the Dark Queen's own rules: the .QCH the
 * game reads, from the .CCH stream `c` the engine's hall writes (`clen`
 * bytes). What FRUA's record has no room for comes from `base`, the
 * hero's .QCH as it was (NULL: zeros) — tools/gbchar.py to_qch. Writes
 * into `out` (room for `max`) and returns the length, or a QCH_E* error
 * (QCH_ESHORT: `c` or `base` too short, QCH_ESPLIT: the stream does not
 * hold its items). */
long qch_from_cch(const unsigned char *c, long clen,
                  const unsigned char *base, long blen,
                  unsigned char *out, long max);

#endif
