/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PLAT_PAPERMETRICS_H
#define PLAT_PAPERMETRICS_H

#include "types.h"
#include "platform.h"

/* Paper metrics: text measured as it will print, whatever the screen's
   fonts are. A screen's bitmap fonts are seldom true point sizes -- the
   ST's GEM sizes its faces to the system font, the Amiga's to Topaz --
   so a layout measured with them holds more on a page than paper will,
   and prints too small through a page language. With paper metrics on
   (plat_metrics_set_paper), a backend measures every face as the nearest
   of the twelve standard PostScript text faces -- Times, Helvetica or
   Courier, each roman, italic, bold and bold italic -- by their published
   widths (Adobe's Core 14 metrics), and gives a line 1.2 em; its own
   fonts only draw. The screen then shows the printed layout, its words
   at their printed places.

   For backends: keep a plat_paper_face beside each font
   (plat_paper_face_init in plat_font_open), and when plat_metrics_paper()
   answer plat_measure_text and plat_font_metrics_get from it. A backend
   measuring in character cells (DOS's text screen) ignores the mode. */

#define PLAT_PAPER_FACES 12

typedef struct {
    const u16 *w;      /* advance widths, 1/1000 em, slot = codepoint - 0x20 (0x80-0x9F: Windows-1252's) */
    u16 ascent;        /* 1/1000 em, the line gap's half included */
    u16 descent;
} plat_paper_table;

extern const plat_paper_table g_plat_paper_faces[PLAT_PAPER_FACES];

typedef struct {
    u8  face;          /* index into g_plat_paper_faces */
    i32 size_twips;
} plat_paper_face;

/* The paper face for a font as plat_font_open was asked for it: its
   family by name (Times, Helvetica, Courier and their look-alikes), else
   by the PLAT_FAMILY_* in style_flags, else Times; bold and italic from
   style_flags. */
void plat_paper_face_init(plat_paper_face *pf, const u8 *name, u32 name_len,
                          i32 size_twips, u16 style_flags);

wp_status plat_paper_measure(const plat_paper_face *pf, const u8 *utf8, u32 len, i32 *width_twips_out);
void      plat_paper_metrics(const plat_paper_face *pf, plat_font_metrics *out);

#endif /* PLAT_PAPERMETRICS_H */
