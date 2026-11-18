/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef RP_QT_METRICS_INTERNAL_H
#define RP_QT_METRICS_INTERNAL_H

#include <QFont>
#include <QFontMetrics>
#include <QPainter>

extern "C" {
#include "types.h"
}

/* Private to backends/qt -- shared between metrics_qt.cpp (which owns the
   twips<->points conversion and the QFont) and draw_qt.cpp (the plat_gc
   implementation, painting through a QPainter). The same shared-internal-
   header pattern every other backend in this library uses.

   Qt sizes fonts in POINTS and a twip is 1/20 point, so the conversion is
   exact in both directions -- the same property the Mac backend gets from
   QuickDraw's 72 dpi and the GTK one got from Pango. All three are built on
   the printer's point rather than on a screen pixel.

   THIS IS THE ONLY C++ IN THE LIBRARY. The seam is C, so every plat_*
   definition here carries C linkage; nothing else about the build changes,
   and no class declares a signal or a slot, so there is no moc step. */

#define QT_TWIPS_PER_POINT 20

typedef struct {
    QFont        *font;
    QFontMetrics *fm;
    i32           size_twips;
} qt_font;

/* plat_gc for this backend: the QPainter of whatever is being painted -- a
   widget's during paintEvent, or one on a QImage for an off-screen render.
   Callers construct one directly, as they do on the Amiga, Mac and GTK
   backends, because the shell already owns its window. */
typedef struct {
    QPainter *p;
} qt_gc;

#endif /* RP_QT_METRICS_INTERNAL_H */
