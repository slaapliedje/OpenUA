/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include "platform.h"

/* Polygons and wide lines for backends that lack them (platform.h,
   "Vector drawing"). Integer arithmetic throughout: no target here has a
   floating-point unit to spare. */

u8 plat_grey_percent(u8 r, u8 g, u8 b)
{
    u8 hi = r, lo = r;
    i32 lum, pct;

    if (g > hi) hi = g;
    if (b > hi) hi = b;
    if (g < lo) lo = g;
    if (b < lo) lo = b;
    if (hi - lo > 24) return 0;                       /* a colour */
    lum = ((i32)r * 30 + (i32)g * 59 + (i32)b * 11) / 100;
    if (lum >= 240 || lum < 16) return 0;             /* white or black */
    pct = ((255 - lum) * 100 + 127) / 255;
    if (pct < 1) pct = 1;
    if (pct > 99) pct = 99;
    return (u8)pct;
}

u8 plat_luma_percent(u8 r, u8 g, u8 b)
{
    i32 lum = ((i32)r * 30 + (i32)g * 59 + (i32)b * 11) / 100, pct;

    if (lum >= 240 || lum < 16) return 0;             /* white or black */
    pct = ((255 - lum) * 100 + 127) / 255;
    if (pct < 1) pct = 1;
    if (pct > 99) pct = 99;
    return (u8)pct;
}

/* Even-odd, sampled at each pixel row's centre: the crossings of the
   row with every edge, sorted, filled in pairs. Edges are walked in
   doubled y so that "the row's centre" is an integer. */
wp_status plat_polygon_by_spans(plat_gc *gc, const i32 *xy, u16 n, u8 r, u8 g, u8 b, u8 shade)
{
    i32 ymin, ymax, y;
    i32 *xs;
    u16 i;
    u8 grey = shade;

    if (n < 3) return WP_OK;
    ymin = ymax = xy[1];
    for (i = 1; i < n; i++) {
        if (xy[i * 2 + 1] < ymin) ymin = xy[i * 2 + 1];
        if (xy[i * 2 + 1] > ymax) ymax = xy[i * 2 + 1];
    }
    xs = (i32 *)mem_alloc((u32)n * (u32)sizeof(i32));
    if (xs == NULL) return WP_NOMEM;

    for (y = ymin; y < ymax; y++) {
        i32 yc = y * 2 + 1;
        u16 k = 0, a, c;

        for (i = 0; i < n; i++) {
            u16 j = (u16)(i + 1 == n ? 0 : i + 1);
            i32 x0 = xy[i * 2], y0 = xy[i * 2 + 1] * 2;
            i32 x1 = xy[j * 2], y1 = xy[j * 2 + 1] * 2;
            if (y0 == y1) continue;
            if (y0 > y1) { i32 t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
            if (yc < y0 || yc >= y1) continue;
            xs[k++] = x0 + ((x1 - x0) * (yc - y0) + (y1 - y0) / 2) / (y1 - y0);
        }
        for (a = 1; a < k; a++) {   /* insertion sort: a handful each row */
            i32 v = xs[a];
            c = a;
            while (c > 0 && xs[c - 1] > v) { xs[c] = xs[c - 1]; c--; }
            xs[c] = v;
        }
        for (a = 0; a + 1 < k; a += 2) {
            if (xs[a + 1] <= xs[a]) continue;
            if (grey != 0) (void)plat_fill_shade(gc, xs[a], y, xs[a + 1] - xs[a], 1, grey);
            else (void)plat_fill_rect(gc, xs[a], y, xs[a + 1] - xs[a], 1, r, g, b);
        }
    }
    mem_free(xs);
    return WP_OK;
}

static i32 isqrt(i32 v)
{
    i32 r = 0, bit = 1L << 30;

    if (v <= 0) return 0;
    while (bit > v) bit >>= 2;
    while (bit != 0) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* a / b to the nearest, b > 0, either sign of a. */
static i32 rdiv(i32 a, i32 b)
{
    return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
}

/* Each segment a quadrilateral `width` across, and an octagon at every
   joint and end, so corners and ends come out round. */
wp_status plat_polyline_by_polygons(plat_gc *gc, const i32 *xy, u16 n, i32 width,
                                    u8 r, u8 g, u8 b)
{
    i32 q[16];
    i32 h1 = width / 2, h2 = width - width / 2;
    u16 i;

    if (n < 2) return WP_OK;
    if (width < 1) width = 1;
    for (i = 0; i + 1 < n; i++) {
        i32 ax = xy[i * 2], ay = xy[i * 2 + 1], bx = xy[i * 2 + 2], by = xy[i * 2 + 3];
        i32 dx = bx - ax, dy = by - ay, len, px1, py1, px2, py2;

        while (dx > 16000 || dx < -16000 || dy > 16000 || dy < -16000) { dx /= 2; dy /= 2; }
        len = isqrt(dx * dx + dy * dy);
        if (len == 0) continue;
        /* The perpendicular, h1 one side and h2 the other. */
        px1 = rdiv(-dy * h1, len); py1 = rdiv(dx * h1, len);
        px2 = rdiv(dy * h2, len);  py2 = rdiv(-dx * h2, len);
        q[0] = ax + px1; q[1] = ay + py1;
        q[2] = bx + px1; q[3] = by + py1;
        q[4] = bx + px2; q[5] = by + py2;
        q[6] = ax + px2; q[7] = ay + py2;
        (void)plat_fill_polygon(gc, q, 4, r, g, b);
    }
    if (width >= 3) {
        i32 rad = width / 2, k = (width / 2 * 106 + 128) / 256;   /* tan 22.5 */
        for (i = 0; i < n; i++) {
            i32 x = xy[i * 2], y = xy[i * 2 + 1];
            q[0] = x + rad; q[1] = y - k;    q[2] = x + rad; q[3] = y + k;
            q[4] = x + k;   q[5] = y + rad;  q[6] = x - k;   q[7] = y + rad;
            q[8] = x - rad; q[9] = y + k;    q[10] = x - rad; q[11] = y - k;
            q[12] = x - k;  q[13] = y - rad; q[14] = x + k;  q[15] = y - rad;
            (void)plat_fill_polygon(gc, q, 8, r, g, b);
        }
    }
    return WP_OK;
}
