/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include <string.h>
#include "platform.h"

/* plat_font_pick: one answer to "which of the faces I have is the one
   asked for", so that GDOS and diskfont backends -- each holding a list
   of names and nothing else -- do not grow two different ideas of it.
   Plain ASCII, no ctype: a face name in a font file is ASCII on every
   one of these machines, and a locale is not something the ST has. */

static u8 lower(u8 c) { return (c >= 'A' && c <= 'Z') ? (u8)(c + 32) : c; }

/* `name` (len bytes) contains `word` (lower-case), case ignored. */
static wp_bool contains(const u8 *name, u32 len, const char *word)
{
    u32 wl = (u32)strlen(word), i, j;

    for (i = 0; i + wl <= len; i++) {
        for (j = 0; j < wl && lower(name[i + j]) == (u8)word[j]; j++) { }
        if (j == wl) return WP_TRUE;
    }
    return WP_FALSE;
}

/* The shorter of a and b is a prefix of the longer, ending at a word
   boundary in it: "Times" and "Times New Roman", not "Time" and "Times". */
static wp_bool prefix_match(const u8 *a, u32 alen, const u8 *b, u32 blen)
{
    const u8 *lng = (alen >= blen) ? a : b;
    u32 n = (alen < blen) ? alen : blen, lng_len = (alen >= blen) ? alen : blen, i;

    if (n == 0) return WP_FALSE;
    for (i = 0; i < n; i++) if (lower(a[i]) != lower(b[i])) return WP_FALSE;
    return (n == lng_len || lng[n] == ' ' || lng[n] == '-') ? WP_TRUE : WP_FALSE;
}

static const char *const g_family_words[6][8] = {
    { NULL },
    { "times", "dutch", "roman", "georgia", "garamond", "new york", "palatino", NULL },
    { "helvetica", "swiss", "arial", "sans", "geneva", "helv", "univers", NULL },
    { "courier", "mono", "typewriter", "fixed", "monaco", "consol", "terminal", NULL },
    { "script", "chancery", "brush", "cursive", NULL },
    { "decor", "symbol", "dingbat", NULL }
};

static wp_bool of_family(const u8 *name, u32 len, u32 family)
{
    u32 i;

    if (family == 0 || family > 5) return WP_FALSE;
    /* "Sans Serif" is no serif, whatever the second word says; and a
       "Sans Mono" is a monospace before it is a sans. */
    if (family == PLAT_FAMILY_ROMAN && contains(name, len, "sans")) return WP_FALSE;
    if (family != PLAT_FAMILY_MODERN && of_family(name, len, PLAT_FAMILY_MODERN)) return WP_FALSE;
    for (i = 0; g_family_words[family][i] != NULL; i++) {
        if (contains(name, len, g_family_words[family][i])) return WP_TRUE;
    }
    return (family == PLAT_FAMILY_ROMAN && contains(name, len, "serif")) ? WP_TRUE : WP_FALSE;
}

i32 plat_font_pick(const char *(*name_at)(u32 index, void *user), void *user, u32 count,
                   const u8 *want, u32 want_len, u16 style_flags)
{
    u32 family = (style_flags & PLAT_FAMILY_MASK) >> PLAT_FAMILY_SHIFT;
    i32 best = -1, best_score = 0;
    u32 i;

    while (want_len > 0 && want[want_len - 1] == ' ') want_len--;

    for (i = 0; i < count; i++) {
        const char *nm = name_at(i, user);
        const u8 *name = (const u8 *)nm;
        u32 len;
        i32 score = 0;

        if (nm == NULL) continue;
        len = (u32)strlen(nm);
        while (len > 0 && name[len - 1] == ' ') len--;
        if (len == 0) continue;

        if (want_len > 0 && prefix_match(name, len, want, want_len)) {
            score = (len == want_len) ? 100 : 80;
        } else if (of_family(name, len, family)) {
            score = 50;
        }
        if (score == 0) continue;

        /* A weight or slant in the name that nobody asked for: the
           plain face is the better answer; one that was asked for is a
           small bonus, for a GDOS that lists "Swiss Bold" apart. */
        if (contains(name, len, "bold") || contains(name, len, "black") || contains(name, len, "heavy")) {
            score += (style_flags & PLAT_STYLE_BOLD) ? 5 : -10;
        }
        if (contains(name, len, "italic") || contains(name, len, "oblique")) {
            score += (style_flags & PLAT_STYLE_ITALIC) ? 5 : -10;
        }
        if (contains(name, len, "light") || contains(name, len, "condensed") ||
            contains(name, len, "narrow")) score -= 5;

        if (score > best_score) { best_score = score; best = (i32)i; }
    }
    return best;
}
