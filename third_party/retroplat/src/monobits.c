/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include "platform.h"

/* plat_draw_mono for a backend without a raster blit of its own: each
   row's runs of set pixels as rectangles, as few as the row has. */
wp_status plat_mono_by_runs(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                            const u16 *rows, u32 row_words, u8 r, u8 g, u8 b)
{
    i32 row, col;

    if (rows == NULL || row_words == 0) return WP_ERR;
    if (w > (i32)(row_words * 16u)) w = (i32)(row_words * 16u);
    for (row = 0; row < h; row++) {
        const u16 *words = rows + (u32)row * row_words;

        col = 0;
        while (col < w) {
            i32 start;

#define MONO_BIT(c) (words[(c) >> 4] & (u16)(0x8000u >> ((c) & 15)))
            while (col < w && !MONO_BIT(col)) col++;
            if (col >= w) break;
            start = col;
            while (col < w && MONO_BIT(col)) col++;
#undef MONO_BIT
            (void)plat_fill_rect(gc, x + start, y + row, col - start, 1, r, g, b);
        }
    }
    return WP_OK;
}
