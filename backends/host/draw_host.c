/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include <string.h>
#include "platform.h"
#include "draw_host.h"
#include "utf8.h"

/* No real drawing surface on the host backend -- glyph draw only notes
   what it was asked for, so engine/layout code can link and run headless
   under test and a test can see where text went. */

static struct {
    i32  x, y;
    char text[48];
} g_texts[DRAW_HOST_TEXT_MAX];
static u32 g_text_count;

wp_status plat_draw_text(plat_gc *gc, plat_font *f, i32 x, i32 y,
                          const u8 *utf8, u32 len, u8 r, u8 g, u8 b)
{
    WP_UNUSED(gc); WP_UNUSED(f);
    WP_UNUSED(r); WP_UNUSED(g); WP_UNUSED(b);
    if (g_text_count < DRAW_HOST_TEXT_MAX) {
        u32 n = len < 47u ? len : 47u, i;
        g_texts[g_text_count].x = x;
        g_texts[g_text_count].y = y;
        for (i = 0; i < n; i++) g_texts[g_text_count].text[i] = (char)utf8[i];
        g_texts[g_text_count].text[n] = '\0';
    }
    g_text_count++;
    return WP_OK;
}

u32 draw_host_text_count(void)
{
    return g_text_count;
}

const char *draw_host_text_at(u32 index, i32 *x_out, i32 *y_out)
{
    if (index >= g_text_count || index >= DRAW_HOST_TEXT_MAX) return NULL;
    if (x_out != NULL) *x_out = g_texts[index].x;
    if (y_out != NULL) *y_out = g_texts[index].y;
    return g_texts[index].text;
}

void draw_host_reset_text(void)
{
    g_text_count = 0;
}

static u32 g_grey_fills[256];
static u32 g_mono_calls;
static u32 g_shade_calls;
static u8  g_last_shade;
static u32 g_poly_calls, g_line_calls;
static i32 g_line_width;
static u8  g_line_rgb[3];
static i32 g_box[4];
static wp_bool g_have_box;

u32 draw_host_shade_calls(void) { return g_shade_calls; }
u8  draw_host_last_shade(void) { return g_last_shade; }

wp_status plat_fill_shade(plat_gc *gc, i32 x, i32 y, i32 w, i32 h, u8 percent)
{
    WP_UNUSED(gc); WP_UNUSED(x); WP_UNUSED(y); WP_UNUSED(w); WP_UNUSED(h);
    g_shade_calls++;
    g_last_shade = percent;
    return WP_OK;
}

u32 draw_host_grey_fills(u8 level)
{
    return g_grey_fills[level];
}

u32 draw_host_mono_calls(void)
{
    return g_mono_calls;
}

void draw_host_reset_fills(void)
{
    u32 i;
    for (i = 0; i < 256; i++) g_grey_fills[i] = 0;
    g_mono_calls = 0;
    g_shade_calls = 0;
    g_poly_calls = g_line_calls = 0;
    g_line_width = 0;
    g_have_box = WP_FALSE;
}

static void take_points(const i32 *xy, u16 n)
{
    u16 i;
    for (i = 0; i < n; i++) {
        i32 x = xy[i * 2], y = xy[i * 2 + 1];
        if (!g_have_box) { g_box[0] = g_box[2] = x; g_box[1] = g_box[3] = y; g_have_box = WP_TRUE; }
        if (x < g_box[0]) g_box[0] = x;
        if (y < g_box[1]) g_box[1] = y;
        if (x > g_box[2]) g_box[2] = x;
        if (y > g_box[3]) g_box[3] = y;
    }
}

wp_status plat_fill_polygon(plat_gc *gc, const i32 *xy, u16 n, u8 r, u8 g, u8 b)
{
    WP_UNUSED(gc); WP_UNUSED(r); WP_UNUSED(g); WP_UNUSED(b);
    g_poly_calls++;
    take_points(xy, n);
    return WP_OK;
}

wp_status plat_draw_polyline(plat_gc *gc, const i32 *xy, u16 n, i32 width, u8 r, u8 g, u8 b)
{
    WP_UNUSED(gc);
    g_line_calls++;
    g_line_width = width;
    g_line_rgb[0] = r; g_line_rgb[1] = g; g_line_rgb[2] = b;
    take_points(xy, n);
    return WP_OK;
}

u32 draw_host_polygon_calls(void) { return g_poly_calls; }
u32 draw_host_polyline_calls(void) { return g_line_calls; }
i32 draw_host_last_line_width(void) { return g_line_width; }
void draw_host_last_line_rgb(u8 *rgb3) { rgb3[0] = g_line_rgb[0]; rgb3[1] = g_line_rgb[1]; rgb3[2] = g_line_rgb[2]; }

wp_bool draw_host_vector_box(i32 *x0, i32 *y0, i32 *x1, i32 *y1)
{
    if (!g_have_box) return WP_FALSE;
    *x0 = g_box[0]; *y0 = g_box[1]; *x1 = g_box[2]; *y1 = g_box[3];
    return WP_TRUE;
}

wp_status plat_draw_mono(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                         const u16 *rows, u32 row_words, u8 r, u8 g, u8 b)
{
    g_mono_calls++;
    return plat_mono_by_runs(gc, x, y, w, h, rows, row_words, r, g, b);
}

wp_status plat_fill_rect(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                          u8 r, u8 g, u8 b)
{
    WP_UNUSED(gc); WP_UNUSED(x); WP_UNUSED(y); WP_UNUSED(w); WP_UNUSED(h);
    if (r == g && g == b) g_grey_fills[r]++;
    return WP_OK;
}

void plat_gc_flush(plat_gc *gc)
{
    WP_UNUSED(gc);
}

/* The host's own character set, for input that is not UTF-8 -- a data
   file from a vintage machine, say -- is Latin-1: each byte its own
   code point. (Text the host draws is already UTF-8; nothing calls the
   other direction here.) */
wp_status plat_native_to_utf8(const u8 *native, u32 len,
                              u8 *out, u32 out_cap, u32 *out_len)
{
    u32 i, w = 0;

    if (native == NULL || out == NULL) return WP_ERR;
    for (i = 0; i < len; i++) {
        u8 enc[4];
        u32 n = utf8_encode(native[i], enc);
        if (w + n > out_cap) break;
        memcpy(out + w, enc, n);
        w += n;
    }
    if (out_len != NULL) *out_len = w;
    return WP_OK;
}
