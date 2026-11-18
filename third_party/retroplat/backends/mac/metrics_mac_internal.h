/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_MAC_METRICS_INTERNAL_H
#define WP_MAC_METRICS_INTERNAL_H

#include "types.h"
#include "papermetrics.h"

/* Private to platform/mac -- shared between metrics_mac.c (owns the
   twips<->pixel conversion and the font/measuring-port implementation),
   draw_mac.c (the plat_gc implementation, drawing in pixels through a
   real window's port), and app_shell.c (which lays out text in twips
   via the engine and must convert to pixels with the SAME ratio before
   drawing, so line breaks measured by the engine land exactly where
   they're drawn -- same invariant platform/atari/metrics_atari_internal.h
   and platform/amiga/metrics_amiga_internal.h each document for their
   own target).

   Unlike Atari (real per-monitor DPI from v_opnvwk) or Amiga (no
   physical-DPI primitive below AmigaOS 3.9, so a per-mode tick ratio is
   queried instead), classic Mac OS needs neither: QuickDraw's whole
   coordinate system is built on a 72-DPI convention by design -- the
   original 1984 Macintosh's own 9" screen was exactly 72 pixels per
   inch, deliberately chosen so "one point" (1/72 inch, the same unit
   printer output uses) equals exactly one screen pixel, matching
   TextSize()'s own units (points) directly. This is architectural, not
   approximate -- every later Mac model in this project's target range
   keeps the same logical 72-DPI calibration regardless of a given
   monitor's real physical dot pitch. Twips are 1/20 point, so the
   conversion is an exact `/20` and `*20`, no rounding surprises. */

i32 mac_twips_to_px(i32 twips);
i32 mac_px_to_twips(i32 px);

/* plat_font for this backend: a real QuickDraw font family ID (resolved
   from a name via GetFNum, or systemFont/0 when the engine's own
   always-empty font name -- see linebreak.c's measure_and_height
   comment -- is passed) plus the point size and Style bits TextFont/
   TextSize/TextFace need before any measurement or drawing call. */
typedef struct {
    short fontNum;
    short size;
    short style;
    plat_paper_face paper;   /* how it measures with paper metrics on */
} mac_font;

/* plat_gc for this backend: the destination WindowPtr whose port
   drawing happens through (app_shell.c's own g_win during a real
   updateEvt repaint). Callers construct one directly -- there is no
   plat_window_create/plat_window_get_gc implementation for Mac, same
   reasoning platform/atari/metrics_atari_internal.h and platform/amiga/
   metrics_amiga_internal.h each give: app_shell.c already owns its
   window through direct Toolbox calls. */
typedef struct {
    WindowPtr window;
} mac_gc;

#endif /* WP_MAC_METRICS_INTERNAL_H */
