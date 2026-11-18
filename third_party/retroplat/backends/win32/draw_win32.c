/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <windows.h>

#include "metrics_win32_internal.h"
#include "platform.h"
#include "mem_host.h"

/* Coordinates are PIXELS, as on every other backend here -- the caller
   converts from twips with the same ratio it measured with.

   y is the TOP of the line, not the baseline. That is this seam's contract
   everywhere, and GDI is the one backend where it costs nothing to honour:
   TextOut's y already means the top of the cell under the default
   TA_TOP alignment, so unlike Qt and QuickDraw there is no ascent to add.
   SetTextAlign is called anyway rather than assumed, because a DC handed
   in from elsewhere may have been left in TA_BASELINE. */
wp_status plat_draw_text(plat_gc *gc, plat_font *f, i32 x, i32 y,
                         const u8 *utf8, u32 len, u8 r, u8 g, u8 b)
{
    w32_gc   *wgc = (w32_gc *)gc;
    w32_font *wf  = (w32_font *)f;
    WCHAR     stackbuf[512];
    WCHAR    *wide = stackbuf;
    HGDIOBJ   oldfont;
    COLORREF  oldcolor;
    int       oldbk, oldalign, n;

    if (wgc == NULL || wgc->dc == NULL || wf == NULL || utf8 == NULL)
        return WP_ERR;
    if (len == 0) return WP_OK;

    if (len > (u32)(sizeof stackbuf / sizeof stackbuf[0])) {
        wide = (WCHAR *)mem_alloc(len * (u32)sizeof(WCHAR));
        if (wide == NULL) return WP_NOMEM;
    }
    n = rp_w32_widen(utf8, len, wide, (int)len);
    if (n < 0) { if (wide != stackbuf) mem_free(wide); return WP_ERR; }

    oldfont  = SelectObject(wgc->dc, wf->hfont);
    oldcolor = SetTextColor(wgc->dc, RGB(r, g, b));
    /* Transparent, so text over a filled gauge bar does not punch a
       rectangle of background through it. */
    oldbk    = SetBkMode(wgc->dc, TRANSPARENT);
    oldalign = (int)SetTextAlign(wgc->dc, TA_LEFT | TA_TOP);

    (void)TextOutW(wgc->dc, (int)x, (int)y, wide, n);

    SetTextAlign(wgc->dc, (UINT)oldalign);
    SetBkMode(wgc->dc, oldbk);
    SetTextColor(wgc->dc, oldcolor);
    SelectObject(wgc->dc, oldfont);
    if (wide != stackbuf) mem_free(wide);
    return WP_OK;
}

/* A colour screen shows a real grey: the nearest to the coverage. */
wp_status plat_fill_shade(plat_gc *gc, i32 x, i32 y, i32 w, i32 h, u8 percent)
{
    u8 level = (u8)(255 - ((u32)(percent > 100 ? 100 : percent) * 255u) / 100u);

    if (percent == 0) return WP_OK;
    return plat_fill_rect(gc, x, y, w, h, level, level, level);
}

wp_status plat_fill_rect(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                         u8 r, u8 g, u8 b)
{
    w32_gc *wgc = (w32_gc *)gc;
    HBRUSH  brush;
    RECT    rect;

    if (wgc == NULL || wgc->dc == NULL) return WP_ERR;
    if (w <= 0 || h <= 0) return WP_OK;

    /* FillRect's RECT is half-open on the right and bottom -- the same
       convention the caller's (x, y, w, h) already has, so there is no
       off-by-one to negotiate. */
    rect.left   = (LONG)x;
    rect.top    = (LONG)y;
    rect.right  = (LONG)(x + w);
    rect.bottom = (LONG)(y + h);

    brush = CreateSolidBrush(RGB(r, g, b));
    if (brush == NULL) return WP_ERR;
    FillRect(wgc->dc, &rect, brush);
    DeleteObject(brush);
    return WP_OK;
}

/* Vector drawing (platform.h): GDI's own. A cosmetic or geometric pen
   of the width asked, round at its joints and ends -- CreatePen's
   default -- and an alternate-fill polygon with no outline. */
wp_status plat_fill_polygon(plat_gc *gc, const i32 *xy, u16 n, u8 r, u8 g, u8 b)
{
    w32_gc *wgc = (w32_gc *)gc;
    POINT *pts;
    HBRUSH brush, old_brush;
    HGDIOBJ old_pen;
    u16 i;

    if (wgc == NULL || wgc->dc == NULL) return WP_ERR;
    if (n < 3) return WP_OK;
    pts = (POINT *)mem_alloc((u32)n * (u32)sizeof(POINT));
    if (pts == NULL) return WP_NOMEM;
    for (i = 0; i < n; i++) { pts[i].x = (LONG)xy[i * 2]; pts[i].y = (LONG)xy[i * 2 + 1]; }
    brush = CreateSolidBrush(RGB(r, g, b));
    if (brush == NULL) { mem_free(pts); return WP_ERR; }
    old_brush = (HBRUSH)SelectObject(wgc->dc, brush);
    old_pen = SelectObject(wgc->dc, GetStockObject(NULL_PEN));
    SetPolyFillMode(wgc->dc, ALTERNATE);
    Polygon(wgc->dc, pts, (int)n);
    SelectObject(wgc->dc, old_pen);
    SelectObject(wgc->dc, old_brush);
    DeleteObject(brush);
    mem_free(pts);
    return WP_OK;
}

wp_status plat_draw_polyline(plat_gc *gc, const i32 *xy, u16 n, i32 width,
                             u8 r, u8 g, u8 b)
{
    w32_gc *wgc = (w32_gc *)gc;
    POINT *pts;
    HPEN pen;
    HGDIOBJ old_pen;
    u16 i;

    if (wgc == NULL || wgc->dc == NULL) return WP_ERR;
    if (n < 2) return WP_OK;
    pts = (POINT *)mem_alloc((u32)n * (u32)sizeof(POINT));
    if (pts == NULL) return WP_NOMEM;
    for (i = 0; i < n; i++) { pts[i].x = (LONG)xy[i * 2]; pts[i].y = (LONG)xy[i * 2 + 1]; }
    pen = CreatePen(PS_SOLID, width > 1 ? (int)width : 0, RGB(r, g, b));
    if (pen == NULL) { mem_free(pts); return WP_ERR; }
    old_pen = SelectObject(wgc->dc, pen);
    Polyline(wgc->dc, pts, (int)n);
    SelectObject(wgc->dc, old_pen);
    DeleteObject(pen);
    mem_free(pts);
    return WP_OK;
}

/* GDI paints straight into whatever surface the DC names, and that surface
   is presented by whoever owns it -- the window manager after EndPaint, or
   a caller blitting a memory DC. There is nothing to flush. */
/* No blit worth having here: plat_draw_mono run by run. */
wp_status plat_draw_mono(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                         const u16 *rows, u32 row_words, u8 r, u8 g, u8 b)
{
    return plat_mono_by_runs(gc, x, y, w, h, rows, row_words, r, g, b);
}

void plat_gc_flush(plat_gc *gc)
{
    (void)gc;
}
