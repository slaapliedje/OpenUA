/*
 * FRUA string utilities.
 *
 * Lifted from the Macintosh build — CODE 3 (jump-table entries 393, 475,
 * 480).
 */

#ifndef ENGINE_STR_H
#define ENGINE_STR_H

/* Signed-char string compare; returns -1, 0, or +1. */
int ua_strcmp(const char *s1, const char *s2);

/*
 * The string table — FRUA's A5-world globals (A5-10280 / A5-10276 in the
 * Mac build). Populated by the string-table loader, which is not yet
 * lifted, so the table is empty until then.
 */
extern char       **g_ua_strtab;        /* array of string pointers */
extern short        g_ua_strtab_count;  /* number of entries        */
extern const char  *g_ua_strtab_default;/* out-of-range fallback    */

/* String `index` from the table, bounds-checked; returns the default for an
 * out-of-range index or an empty slot. */
const char *ua_get_string(short index);

/*
 * Pointer into the STRS resource at the given byte offset. The original
 * Mac engine uses CREL relocations to compute this address at load time
 * (the THINK C linker patches in STRS_load_addr + offset for every
 * `pea 0xXXXX  ; reloc STRS+0xXXXX` instruction); the lift resolves
 * the same offsets at call time via GetResource. Returns "" if STRS
 * isn't loaded.
 */
const char *ua_strs_at(long offset);

/*
 * Install the string table — the lifted JT[480] (CODE 3 + 0x3c6). The Mac
 * THINK C runtime calls this from CODE 1 with the count / pointer that the
 * DATA + DREL resources resolve to; the shim's main() does the same for
 * ua_main's prologue. After this call ua_get_string can resolve indices.
 */
void jt480(short count, void *table);

#endif /* ENGINE_STR_H */
