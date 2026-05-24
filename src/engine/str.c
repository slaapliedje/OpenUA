/*
 * FRUA string utilities — lifted from CODE 3.
 *
 *   ua_strcmp      jump-table entry 393  (CODE 3 + 0x3b8c)
 *   jt480          jump-table entry 480  (CODE 3 + 0x3c6)
 *   ua_get_string  jump-table entry 475  (CODE 3 + 0x3da)
 */

#include <stddef.h>

#include "str.h"

/*
 * ua_strcmp — CODE 3 + 0x3b8c.
 *
 * The original walks both strings while the bytes are equal and non-zero,
 * then returns the sign of the difference of the first differing (or
 * terminating) bytes, compared as signed chars: -1, 0, or +1.
 */
int ua_strcmp(const char *s1, const char *s2)
{
	while (*s1 == *s2 && *s1 != '\0') {
		s1++;
		s2++;
	}
	if ((signed char)*s1 < (signed char)*s2)
		return -1;
	if ((signed char)*s1 > (signed char)*s2)
		return 1;
	return 0;
}

/*
 * The string table — the Mac A5-world globals at A5-10280 (the array) and
 * A5-10276 (the count). Installed by jt480 below; the THINK C runtime
 * calls jt480 from CODE 1 with values from the DATA + DREL resources.
 * The shim's main() does the same in ua_main's prologue. Empty until then.
 */
char  **g_ua_strtab;
short   g_ua_strtab_count;

/*
 * jt480 — CODE 3 + 0x3c6. The string-table setter. Two instructions:
 * movew arg1, A5_-10276; movel arg2, A5_-10280. ua_main forwards its own
 * arg1 / arg2 here, so the table the THINK C runtime computes flows in
 * before any other phase-4 code runs.
 */
void jt480(short count, void *table)
{
	g_ua_strtab_count = count;
	g_ua_strtab       = (char **)table;
}

/*
 * Out-of-range / empty-slot fallback. CREL relocation resolves the Mac
 * build's address to STRS+0x41c0 — an empty string in the string pool.
 */
const char *g_ua_strtab_default = "";

/*
 * ua_get_string — CODE 3 + 0x3da.
 *
 * Returns g_ua_strtab[index], bounds-checked against g_ua_strtab_count and
 * guarded for an unset table or an empty slot; otherwise the default.
 */
const char *ua_get_string(short index)
{
	if (g_ua_strtab != NULL
			&& index >= 0
			&& index < g_ua_strtab_count
			&& g_ua_strtab[index] != NULL)
		return g_ua_strtab[index];

	return g_ua_strtab_default;
}
