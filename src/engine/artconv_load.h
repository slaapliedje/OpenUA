/* In-engine DOS-art conversion (ADR-0014): when an art probe misses a
 * `.ctl` but the design ships the DOS `HLIB` sibling `.tlb`, convert it
 * on first touch and write the `.ctl` back into the design folder — the
 * read path never sees an HLIB byte, and the cost is paid once per file. */
#ifndef ARTCONV_LOAD_H
#define ARTCONV_LOAD_H

/* Read `in_path` (a C path the file shim resolves, e.g.
 * "Game39.dsn:BACK1004.TLB"); if it is an HLIB container, convert to
 * GLIB and write `out_path`. Returns 1 when out_path was written, 0 on
 * any miss/failure (not HLIB, no memory, unsupported entry, write
 * failure) — callers just fall through to the base art. */
int ua_dos_art_convert_file(const char *in_path, const char *out_path);

#endif /* ARTCONV_LOAD_H */
