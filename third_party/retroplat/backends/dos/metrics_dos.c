/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include "platform.h"
#include "utf8.h"
#include "metrics_dos_internal.h"

i32 dos_twips_to_col(i32 twips) { return twips / DOS_CELL_WIDTH_TWIPS; }
i32 dos_twips_to_row(i32 twips) { return twips / DOS_CELL_HEIGHT_TWIPS; }
i32 dos_col_to_twips(i32 col)   { return col * DOS_CELL_WIDTH_TWIPS; }
i32 dos_row_to_twips(i32 row)   { return row * DOS_CELL_HEIGHT_TWIPS; }

wp_status plat_font_open(const u8 *utf8_font_name, u32 name_len,
                          i32 size_twips, u16 style_flags, plat_font **out)
{
    dos_font *f;

    WP_UNUSED(utf8_font_name);
    WP_UNUSED(name_len);

    f = (dos_font *)mem_alloc((u32)sizeof(dos_font));
    if (f == NULL) return WP_NOMEM;
    f->size_twips = size_twips;
    f->bold = (wp_bool)((style_flags & PLAT_STYLE_BOLD) ? WP_TRUE : WP_FALSE);
    *out = (plat_font *)f;
    return WP_OK;
}

void plat_font_close(plat_font *f)
{
    mem_free(f);
}

/* One face, the adapter's: nothing to choose between. */
u32 plat_font_face_count(void) { return 0; }

wp_status plat_font_face_name(u32 index, u8 *utf8_out, u32 cap, u32 *len_out)
{
    WP_UNUSED(index);
    WP_UNUSED(utf8_out);
    WP_UNUSED(cap);
    *len_out = 0;
    return WP_RANGE;
}

/* Fixed regardless of the font's requested size_twips -- see
   metrics_dos_internal.h's own comment on dos_font. ascent/descent are
   a plausible split of the one fixed cell height (192+48 = 240,
   DOS_CELL_HEIGHT_TWIPS), not a claim of matching any real ROM font's
   published metrics (BIOS text mode doesn't publish any). */
wp_status plat_font_metrics_get(plat_font *f, plat_font_metrics *out)
{
    if (f == NULL || out == NULL) return WP_ERR;
    out->ascent = 192;
    out->descent = 48;
    out->leading = 0;
    return WP_OK;
}

wp_status plat_measure_text(plat_font *f, const u8 *utf8, u32 len,
                             i32 *width_twips_out)
{
    u32 cp_count;

    if (f == NULL || width_twips_out == NULL) return WP_ERR;
    cp_count = utf8_cp_count(utf8, len);
    *width_twips_out = DOS_CELL_WIDTH_TWIPS * (i32)cp_count;
    return WP_OK;
}
