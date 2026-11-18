/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_DRAW_HOST_H
#define WP_DRAW_HOST_H

#include "types.h"

/* Host-only introspection into plat_fill_rect: how many fills of each
   grey (r == g == b) have been asked for since the last reset, so a
   test can tell which look a painter chose without a screen to look
   at. Like metrics_host.h, never part of the portable surface. */

u32  draw_host_grey_fills(u8 level);
void draw_host_reset_fills(void);

/* plat_fill_shade calls since the last draw_host_reset_fills, and the
   last one's coverage. */
u32  draw_host_shade_calls(void);
u8   draw_host_last_shade(void);

/* plat_draw_mono calls since the last draw_host_reset_fills. */
u32  draw_host_mono_calls(void);

/* plat_fill_polygon and plat_draw_polyline since the last
   draw_host_reset_fills: how many of each, the last line's width and
   colour, and the box round every point either was given (WP_FALSE
   for none). */
u32     draw_host_polygon_calls(void);
u32     draw_host_polyline_calls(void);
i32     draw_host_last_line_width(void);
void    draw_host_last_line_rgb(u8 *rgb3);
wp_bool draw_host_vector_box(i32 *x0, i32 *y0, i32 *x1, i32 *y1);

/* The text plat_draw_text was given since the last
   draw_host_reset_text, the first DRAW_HOST_TEXT_MAX calls kept: where
   it went (x, y: the baseline's start) and the string, cut to 47 bytes. */
#define DRAW_HOST_TEXT_MAX 64
u32         draw_host_text_count(void);
const char *draw_host_text_at(u32 index, i32 *x_out, i32 *y_out);
void        draw_host_reset_text(void);

#endif /* WP_DRAW_HOST_H */
