/*
 * gbdaxart.h — Champions' and Death Knights' pictures (.DAX blocks) as the
 * Mac GLIB picture libraries FRUA's engine draws. Plain C99 with no engine,
 * shim or platform headers, so it builds on the host for
 * tests/test_gbdaxart.py. tools/gbdaxart.py is the reference and its
 * docstring has the formats: a block is a frame count and per frame a
 * 4-byte delay, the 17-byte frame header and 4-bit pixels (a big picture:
 * one plain frame); a picture is a nested "TILE" GLIB, entry 0 the 16 EGA
 * colours at CLUT 32..47, then each frame run-coded a row at a time.
 */
#ifndef ENGINE_GBDAXART_H
#define ENGINE_GBDAXART_H

#define GBDA_EFRAME  -1         /* not a block of frames this reads        */
#define GBDA_EROOM   -2         /* `out` is too small                      */

#define GBDA_HDR     16         /* a GLIB's header, before its offsets     */

/* One picture, its nested library, into `out` (room for `max`; NULL with
 * max 0 only measures). Its size, or a GBDA_E code. */
long gbda_picture(const unsigned char *block, long len,
                  unsigned char *out, long max);

/* The library's head: the GLIB header and offset table for `n` pictures
 * whose sizes are `size[]`, then entry 0, the id table (`id[k] + shift`
 * against entry k + 1), into `out`. Returns the bytes written; the
 * pictures follow in order. `out` needs GBDA_HDR + 4 * (n + 2) + 2 + 4 * n. */
long gbda_head(const unsigned *id, const long *size, unsigned n, unsigned shift,
               unsigned char *out);

#endif
