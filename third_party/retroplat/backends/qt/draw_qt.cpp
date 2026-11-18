/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <QString>
#include <QColor>
#include <QPen>
#include <QPolygon>

#include "metrics_qt_internal.h"

extern "C" {
#include "platform.h"
}

extern "C" {

/* Coordinates are PIXELS, as on every other backend here -- the caller
   converts from twips with the same ratio it measured with.
   
   y is the TOP of the line, not the baseline. Qt's drawText(x, y, ...) takes
   a baseline, so the ascent is added here rather than made the caller's
   problem: every other backend in this library takes a top, and a seam whose
   y means something different on one platform is a trap rather than an
   optimisation. */
wp_status plat_draw_text(plat_gc *gc, plat_font *f, i32 x, i32 y,
                         const u8 *utf8, u32 len, u8 r, u8 g, u8 b)
{
    qt_gc   *qgc = (qt_gc *)gc;
    qt_font *qf  = (qt_font *)f;

    if (qgc == NULL || qgc->p == NULL || qf == NULL || utf8 == NULL)
        return WP_ERR;

    QString s = QString::fromUtf8((const char *)utf8, (int)len);
    qgc->p->save();
    qgc->p->setFont(*qf->font);
    qgc->p->setPen(QColor((int)r, (int)g, (int)b));
    qgc->p->drawText((int)x, (int)y + qf->fm->ascent(), s);
    qgc->p->restore();
    return WP_OK;
}

wp_status plat_fill_rect(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                         u8 r, u8 g, u8 b)
{
    qt_gc *qgc = (qt_gc *)gc;
    if (qgc == NULL || qgc->p == NULL) return WP_ERR;
    qgc->p->fillRect((int)x, (int)y, (int)w, (int)h,
                     QColor((int)r, (int)g, (int)b));
    return WP_OK;
}

/* Vector drawing (platform.h): Qt's own, antialiasing left as the
   painter has it. Round joints and ends, as the other backends draw. */
wp_status plat_fill_polygon(plat_gc *gc, const i32 *xy, u16 n, u8 r, u8 g, u8 b)
{
    qt_gc *qgc = (qt_gc *)gc;
    if (qgc == NULL || qgc->p == NULL) return WP_ERR;
    if (n < 3) return WP_OK;
    QPolygon poly((int)n);
    for (u16 i = 0; i < n; i++) poly.setPoint((int)i, (int)xy[i * 2], (int)xy[i * 2 + 1]);
    qgc->p->save();
    qgc->p->setPen(Qt::NoPen);
    qgc->p->setBrush(QColor((int)r, (int)g, (int)b));
    qgc->p->drawPolygon(poly, Qt::OddEvenFill);
    qgc->p->restore();
    return WP_OK;
}

wp_status plat_draw_polyline(plat_gc *gc, const i32 *xy, u16 n, i32 width,
                             u8 r, u8 g, u8 b)
{
    qt_gc *qgc = (qt_gc *)gc;
    if (qgc == NULL || qgc->p == NULL) return WP_ERR;
    if (n < 2) return WP_OK;
    QPolygon poly((int)n);
    for (u16 i = 0; i < n; i++) poly.setPoint((int)i, (int)xy[i * 2], (int)xy[i * 2 + 1]);
    qgc->p->save();
    qgc->p->setPen(QPen(QColor((int)r, (int)g, (int)b), width > 1 ? (int)width : 0,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    qgc->p->drawPolyline(poly);
    qgc->p->restore();
    return WP_OK;
}

/* A colour screen shows a real grey: the nearest to the coverage. */
wp_status plat_fill_shade(plat_gc *gc, i32 x, i32 y, i32 w, i32 h, u8 percent)
{
    int level = 255 - ((int)(percent > 100 ? 100 : percent) * 255) / 100;
    if (percent == 0) return WP_OK;
    return plat_fill_rect(gc, x, y, w, h, (u8)level, (u8)level, (u8)level);
}

/* Qt paints into whatever surface the QPainter was opened on, and that
   surface is presented by whoever owns it -- a widget after paintEvent, or
   a caller saving a QImage. There is nothing to flush. */
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

}  /* extern "C" */
