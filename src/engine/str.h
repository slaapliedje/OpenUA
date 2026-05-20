/*
 * FRUA string utilities.
 *
 * Lifted from the Macintosh build — CODE 3 (jump-table entries 393, 475).
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

#endif /* ENGINE_STR_H */
