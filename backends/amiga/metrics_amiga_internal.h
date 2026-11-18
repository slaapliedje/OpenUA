/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_AMIGA_METRICS_INTERNAL_H
#define WP_AMIGA_METRICS_INTERNAL_H

#include <graphics/text.h>
#include <graphics/rastport.h>
#include <intuition/screens.h>
#include "types.h"
#include "papermetrics.h"

/* Private to platform/amiga -- shared between metrics_amiga.c (owns the
   twips<->pixel conversion and the lazily-queried real screen DPI plus a
   metrics-only RastPort), draw_amiga.c (the plat_gc implementation,
   drawing in pixels through the window's own RastPort), and app_shell.c
   (which lays out text in twips via the engine and must convert to
   pixels with the SAME ratio before drawing, so line breaks measured by
   the engine land exactly where they're drawn -- same invariant
   platform/atari/metrics_atari_internal.h documents for its own target).

   Real per-axis DPI, unlike Atari's v_opnvwk (which reports true device
   pixel size in microns straight from VDI), has no equivalent "physical
   size" primitive in stock AmigaOS graphics.library below V45's
   GetDiskFontCtrl(DFCTRL_XDPI/YDPI) -- and even that queries a font-
   engine PREFERENCE, not real hardware, and needs diskfont.library V45
   (AmigaOS 3.9), far above this project's Kickstart 2.04+ floor. So
   metrics_amiga.c pins Y at 72 DPI -- not an arbitrary guess, but
   AmigaOS's own documented font-engine convention (diskfonttag.h's
   DFCTRL_YDPI comment: "Default is 72 dpi"; ta_YSize has always meant
   "pixels at 72dpi" by the same convention Mac/PostScript point sizing
   used) -- and derives X from a REAL, queried, per-mode value:
   DisplayInfo.Resolution (graphics/displayinfo.h), the actual
   ticks-per-pixel ratio for the current public screen's video mode
   (GetVPModeID + FindDisplayInfo + GetDisplayInfoData(DTAG_DISP), all
   real V36+ graphics.library calls -- within this project's floor).
   This is real, mode-derived data, not hardcoded -- see
   docs/amiga-platform.md for why a true physical DPI isn't obtainable
   here the way it is on the ST. */

i32 amiga_twips_to_px_x(i32 twips);
i32 amiga_twips_to_px_y(i32 twips);
i32 amiga_px_to_twips_x(i32 px);
i32 amiga_px_to_twips_y(i32 px);

/* plat_font for this backend: a real TextFont opened via OpenDiskFont
   (which searches ROM-resident AND disk fonts, and is available since
   Kickstart 1.2 -- no need to also call the narrower OpenFont). owns_font
   is WP_FALSE for the GfxBase->DefaultFont fallback (used only if
   OpenDiskFont itself fails, which shouldn't happen for topaz.font --
   see metrics_amiga.c) since that font is never ours to CloseFont. */
typedef struct {
    struct TextFont *tf;
    wp_bool owns_font;
    plat_paper_face paper;   /* how it measures with paper metrics on */
} amiga_font;

/* plat_gc for this backend: the destination RastPort (the editor object's
   own _rp(obj) during a real MUIM_Draw, or the metrics-only RastPort
   during a selfcheck) plus the DrawInfo needed to resolve MUI's
   TEXTPEN/BACKGROUNDPEN from an (r,g,b) triple -- pens are screen-
   relative indices, not direct RGB, so draw_amiga.c needs this pointer
   rather than a global. Callers (app_shell.c) construct one directly --
   there is no plat_window_create/plat_window_get_gc implementation for
   Amiga, same reasoning platform/atari/metrics_atari_internal.h gives:
   app_shell.c already owns its window through direct MUI calls. */
typedef struct {
    struct RastPort *rp;
    struct DrawInfo *dri;
    struct Screen   *screen; /* M15: the real, runtime-queried screen this
                                 RastPort belongs to -- plat_draw_image needs
                                 its ViewPort.ColorMap for ObtainBestPen (an
                                 indexed image's palette must be resolved
                                 against the REAL screen depth, which is
                                 never fixed at compile time -- see
                                 amiga_screen_color_count()'s own comment).
                                 NULL for the metrics-only RastPort (never
                                 draws an image, only measures text). */
} amiga_gc;

#endif /* WP_AMIGA_METRICS_INTERNAL_H */
