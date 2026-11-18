/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include "papermetrics.h"
#include "utf8.h"

static wp_bool g_paper = WP_FALSE;

void plat_metrics_set_paper(wp_bool on) { g_paper = on ? WP_TRUE : WP_FALSE; }
wp_bool plat_metrics_paper(void) { return g_paper; }

/* Windows-1252's 0x80-0x9F, the table's slots for them. */
static const u16 g_cp1252[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039,
    0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
};

static u32 lower(u8 c) { return (c >= 'A' && c <= 'Z') ? (u32)(c + 32) : (u32)c; }

/* Does name contain word (ASCII, case ignored)? */
static wp_bool has(const u8 *name, u32 len, const char *word)
{
    u32 i, j;
    for (i = 0; i < len; i++) {
        for (j = 0; word[j] != '\0' && i + j < len && lower(name[i + j]) == (u32)word[j]; j++) { }
        if (word[j] == '\0') return WP_TRUE;
    }
    return WP_FALSE;
}

void plat_paper_face_init(plat_paper_face *pf, const u8 *name, u32 name_len,
                          i32 size_twips, u16 style_flags)
{
    u32 fam = (style_flags & PLAT_FAMILY_MASK) >> PLAT_FAMILY_SHIFT;
    u8 cls = 0;   /* 0 Times, 1 Helvetica, 2 Courier */

    if (name != NULL && name_len > 0 &&
        (has(name, name_len, "courier") || has(name, name_len, "mono") || has(name, name_len, "typewriter") ||
         has(name, name_len, "fixed") || has(name, name_len, "monaco") || has(name, name_len, "pica"))) cls = 2;
    else if (name != NULL && name_len > 0 &&
        (has(name, name_len, "helvet") || has(name, name_len, "arial") || has(name, name_len, "swiss") ||
         has(name, name_len, "sans") || has(name, name_len, "geneva") || has(name, name_len, "univers"))) cls = 1;
    else if (name != NULL && name_len > 0 &&
        (has(name, name_len, "times") || has(name, name_len, "roman") || has(name, name_len, "dutch") ||
         has(name, name_len, "serif") || has(name, name_len, "york") || has(name, name_len, "garamond"))) cls = 0;
    else if (fam == PLAT_FAMILY_MODERN) cls = 2;
    else if (fam == PLAT_FAMILY_SWISS) cls = 1;
    pf->face = (u8)(cls * 4u + ((style_flags & PLAT_STYLE_BOLD) ? 2u : 0u) + ((style_flags & PLAT_STYLE_ITALIC) ? 1u : 0u));
    pf->size_twips = size_twips;
}

static u32 width_of(const u16 *w, u32 cp)
{
    u32 slot = 0, k;
    if (cp >= 0x20 && cp < 0x7F) slot = cp - 0x20;
    else if (cp >= 0xA0 && cp <= 0xFF) slot = cp - 0x20;
    else {
        for (k = 0; k < 32; k++) if (g_cp1252[k] == cp && cp != 0) break;
        if (k < 32) slot = 0x60 + k;
        else return w['n' - 0x20];   /* a character the faces lack: an n's width */
    }
    return w[slot] != 0 ? w[slot] : w['n' - 0x20];
}

wp_status plat_paper_measure(const plat_paper_face *pf, const u8 *utf8, u32 len, i32 *width_twips_out)
{
    const u16 *w;
    u32 i = 0, em = 0;

    if (pf == NULL || width_twips_out == NULL || pf->face >= PLAT_PAPER_FACES) return WP_ERR;
    w = g_plat_paper_faces[pf->face].w;
    while (i < len) {
        u32 cp = 0, n = utf8_decode(utf8 + i, len - i, &cp);
        if (n == 0) { n = 1; cp = '?'; }
        em += width_of(w, cp);
        i += n;
    }
    /* Thousandths of an em at the size, rounded: one division for the
       run, so a run measures the same however it is split by the caller
       only to within a twip -- as any face's would. */
    *width_twips_out = (i32)(((long)em * pf->size_twips + 500L) / 1000L);
    return WP_OK;
}

void plat_paper_metrics(const plat_paper_face *pf, plat_font_metrics *out)
{
    const plat_paper_table *t = &g_plat_paper_faces[pf->face < PLAT_PAPER_FACES ? pf->face : 0];
    out->ascent = (i32)(((long)t->ascent * pf->size_twips + 500L) / 1000L);
    out->descent = (i32)(((long)t->descent * pf->size_twips + 500L) / 1000L);
    out->leading = 0;
}
