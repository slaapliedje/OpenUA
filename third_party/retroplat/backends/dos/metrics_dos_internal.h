/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_DOS_METRICS_INTERNAL_H
#define WP_DOS_METRICS_INTERNAL_H

#include "types.h"

/* Private to platform/dos -- shared between metrics_dos.c (owns the
   twips<->cell conversion and plat_font_open's dos_font), draw_dos.c
   (the plat_gc implementation, drawing in cells), and app_shell.c
   (which lays out text in twips via the engine and must convert to
   cells with the SAME ratio before calling plat_draw_text/
   plat_fill_rect, so line breaks measured by the engine land exactly
   where they're drawn -- the same "shared internal header" pattern
   metrics_atari_internal.h/metrics_mac_internal.h each already use for
   their own real per-axis twips<->pixel ratio).

   Unlike those three GUI backends, DOS text mode has no real per-axis
   DPI to query -- there is exactly one fixed BIOS character cell, a
   real, historically-grounded monospace convention (10 characters per
   inch, 6 lines per inch -- the standard DOS/typewriter/line-printer
   convention) rather than an arbitrary made-up ratio. 1 inch = 1440
   twips. */
#define DOS_CELL_WIDTH_TWIPS  144   /* 1440 / 10 cpi */
#define DOS_CELL_HEIGHT_TWIPS 240   /* 1440 / 6 lpi */

#define DOS_SCREEN_COLS 80
#define DOS_SCREEN_ROWS 25

/* Opaque to everyone except metrics_dos.c/draw_dos.c -- plat_font is
   declared `typedef void plat_font` in platform.h. size_twips is
   accepted (the engine's own layout math needs SOME number to divide
   by) but never changes what's actually rendered: plain BIOS text mode
   has exactly one fixed ROM cell, no real size variation -- a
   documented DOS-specific gap, not a silent one. bold is precomputed
   once at open time (mirroring metrics_atari.c's/metrics_mac.c's own
   docmodel.h-aware translation at open time) so draw_dos.c never needs
   to know about wp_style_flags itself. */
typedef struct {
    i32     size_twips;
    wp_bool bold;
} dos_font;

i32 dos_twips_to_col(i32 twips);
i32 dos_twips_to_row(i32 twips);
i32 dos_col_to_twips(i32 col);
i32 dos_row_to_twips(i32 row);

#endif /* WP_DOS_METRICS_INTERNAL_H */
