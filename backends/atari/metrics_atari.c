/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include <string.h>
#include "platform.h"
#include "metrics_atari_internal.h"

/* GDOS (SpeedoGDOS/FontGDOS) is detected at runtime via vq_gdos(), not
   assumed absent -- see docs/atari-platform.md for how this was
   verified against a real SpeedoGDOS 5.7 install under Hatari. When
   present, vst_load_fonts() is called once so vst_point() can hit exact
   requested sizes via the scalable outline renderer; when absent (or
   the load fails), text is the built-in bitmap system font at whatever
   point size vst_point() can actually hit, same as before -- requested
   sizes snap to the nearest size the system font supports. Either way
   this is invisible to callers: plat_font_open/plat_font_metrics_get
   behave the same regardless of which case applies. */

/* A lazily-opened, metrics-only virtual VDI workstation, independent of
   any window's own VDI handle (font metrics can be queried before a
   window exists, or shared across paragraphs/windows). VDI text
   attributes (point size, effects) are workstation-global state, not
   per-call, so every measurement re-applies them before querying --
   see measure_and_height()'s font cache in linebreak.c, which can hand
   us several different plat_font instances that all share this one
   handle. */
static VdiHdl g_metrics_vdi = 0;

/* An outline GDOS -- SpeedoGDOS, FSM GDOS, NVDI: vq_vgdos says '_FSM'.
   Its vst_point gives only the sizes EXTEND.SYS lists (10, 12 and 24 as
   shipped), the nearest below the one asked for, so a 20 pt heading
   printed at 12 pt; vst_arbpt scales a face to any size. Used for the
   printer only: on the screen the snapping is what keeps small text
   legible -- ST Medium's pixels are 372 microns tall, and an honest 8 pt
   face (the body size scaled to the system cell) is unreadable there.
   gemlib has vst_arbpt, but its installed headers do not declare it;
   gem4xe has no outline GDOS. */
/* Measuring a printer rather than the screen (atari_metrics_printer). */
static wp_bool g_measuring_printer = WP_FALSE;
#ifndef GEM4XE_APP_GEM_H
short vst_arbpt(short handle, short point, short *wchar, short *hchar, short *wcell, short *hcell);
static wp_bool g_outline_gdos = WP_FALSE;
#endif
static i32 g_dpi_x = ATARI_ASSUMED_DPI;
static i32 g_dpi_y = ATARI_ASSUMED_DPI;
static i32 g_num_colors = 2; /* conservative (mono) until queried */

/* Real pixel size in microns, kept alongside the derived DPI because
   font selection below compares PHYSICAL cell dimensions and doing that
   from an already-rounded integer DPI throws away the precision that
   makes the comparison meaningful at these tiny sizes. */
static i32 g_px_um_x = 25400L / ATARI_ASSUMED_DPI;
static i32 g_px_um_y = 25400L / ATARI_ASSUMED_DPI;

/* The distinct fonts this workstation can actually produce, discovered
   once by sweeping vst_point (see build_font_table). Six entries is the
   real count on stock TOS/EmuTOS; the cap is generous for a GDOS that
   offers more. */
#define ATARI_MAX_FONT_SIZES 24
typedef struct {
    short pt;      /* the vst_point argument that selects this cell */
    short cell_w;
    short cell_h;
} atari_font_size;

static atari_font_size g_font_sizes[ATARI_MAX_FONT_SIZES];
static short g_font_size_count = 0;

/* The AES's own character cell for this screen mode -- 8x8 in ST Medium,
   8x16 in ST High, straight from graf_handle. This is the font GEM draws
   every menu, title and dialog in, so it is the machine's own answer to
   "what does normal text look like here", and pick_point_size anchors
   the document's default body size to it. */
static short g_sys_cell_w = 0;
static short g_sys_cell_h = 0;

static void build_font_table(void);

/* The faces a resident GDOS loaded for g_metrics_vdi, by vqt_name: its
   index-1 entry is the system font, the rest whatever ASSIGN.SYS (or
   Speedo's font list) gave it. Empty without a GDOS -- the system font
   is then the only face and there is nothing to name. Face ids are the
   same on every handle that loaded the same fonts, which is what lets a
   window's own handle (atari_ensure_gdos_fonts) select one found here. */
#define ATARI_MAX_FACES 32
typedef struct {
    short id;
    char  name[34];    /* vqt_name writes 32 characters and a NUL, and
                          some GDOS a flag byte after */
} atari_face;
static atari_face g_faces[ATARI_MAX_FACES];
static short g_face_count = 0;

static void build_face_table(short total)
{
    short i;

    g_face_count = 0;
    for (i = 1; i <= total && g_face_count < ATARI_MAX_FACES; i++) {
        atari_face *f = &g_faces[g_face_count];
        short n;

        memset(f->name, 0, sizeof(f->name));
        f->id = vqt_name(g_metrics_vdi, i, f->name);
        f->name[32] = '\0';
        for (n = 31; n >= 0 && (f->name[n] == ' ' || f->name[n] == '\0'); n--) f->name[n] = '\0';
        if (f->name[0] != '\0' && f->id > 0) g_face_count++;
    }
}

static wp_status ensure_metrics_vdi(void)
{
    short work_in[11];
    short work_out[57];
    short handle, dummy;
    i32 dpi;

    if (g_metrics_vdi != 0) return WP_OK;

    {
        short box_w, box_h;
        handle = graf_handle(&g_sys_cell_w, &g_sys_cell_h, &box_w, &box_h);
    }
    for (dummy = 0; dummy < 10; dummy++) work_in[dummy] = 1;
    work_in[10] = 2; /* raster coordinates */
    v_opnvwk(work_in, &handle, work_out);
    if (handle == 0) return WP_ERR;

    g_metrics_vdi = handle;

    /* work_out[3]/[4]: pixel width/height in microns (25400 microns per
       inch). Standard v_opnvwk output, present with or without GDOS --
       confirmed on a real EmuTOS boot with no GDOS driver attached.
       Guarded against a 0 or too-large-to-yield-a-nonzero-DPI report,
       which shouldn't happen on any real driver but must never divide
       by zero or silently truncate to a 0 DPI. */
    if (work_out[3] > 0) {
        dpi = 25400L / (i32)work_out[3];
        if (dpi > 0) { g_dpi_x = dpi; g_px_um_x = (i32)work_out[3]; }
    }
    if (work_out[4] > 0) {
        dpi = 25400L / (i32)work_out[4];
        if (dpi > 0) { g_dpi_y = dpi; g_px_um_y = (i32)work_out[4]; }
    }

    /* work_out[13]: number of colors this device supports (standard
       v_opnvwk output; 2/4/16 for ST Mono/Medium/Low -- see
       docs/atari-platform.md). Guarded the same way DPI is: a 0 or
       negative report shouldn't happen on any real driver but must
       never leave g_num_colors at something nonsensical. */
    if (work_out[13] > 0) g_num_colors = (i32)work_out[13];

    /* atari_ensure_gdos_fonts' load, here by hand for its count:
       work_out[10] faces are built in, vst_load_fonts adds the rest. */
    {
        short extra = 0;
        if (vq_gdos() != 0) extra = vst_load_fonts(g_metrics_vdi, 0);
#ifndef GEM4XE_APP_GEM_H
        g_outline_gdos = (wp_bool)(vq_gdos() != 0 && vq_vgdos() == GDOS_FSM);
#endif
        if (extra > 0) build_face_table((short)(work_out[10] + extra));
    }
    build_font_table();

    return WP_OK;
}

/* ------------------------------------------------------------------ */
/* Measuring for a printer                                             */
/* ------------------------------------------------------------------ */

/* Everything above that describes the device being measured, kept aside
   while a printer is measured instead. */
typedef struct {
    VdiHdl vdi;
    i32 dpi_x, dpi_y, num_colors, px_um_x, px_um_y;
    atari_font_size sizes[ATARI_MAX_FONT_SIZES];
    short size_count;
    short sys_cell_w, sys_cell_h;
    atari_face faces[ATARI_MAX_FACES];
    short face_count;
} atari_measure_state;


static void save_state(atari_measure_state *st)
{
    st->vdi = g_metrics_vdi;
    st->dpi_x = g_dpi_x; st->dpi_y = g_dpi_y;
    st->num_colors = g_num_colors;
    st->px_um_x = g_px_um_x; st->px_um_y = g_px_um_y;
    memcpy(st->sizes, g_font_sizes, sizeof(g_font_sizes));
    st->size_count = g_font_size_count;
    st->sys_cell_w = g_sys_cell_w; st->sys_cell_h = g_sys_cell_h;
    memcpy(st->faces, g_faces, sizeof(g_faces));
    st->face_count = g_face_count;
}

static void restore_state(const atari_measure_state *st)
{
    g_metrics_vdi = st->vdi;
    g_dpi_x = st->dpi_x; g_dpi_y = st->dpi_y;
    g_num_colors = st->num_colors;
    g_px_um_x = st->px_um_x; g_px_um_y = st->px_um_y;
    memcpy(g_font_sizes, st->sizes, sizeof(g_font_sizes));
    g_font_size_count = st->size_count;
    g_sys_cell_w = st->sys_cell_w; g_sys_cell_h = st->sys_cell_h;
    memcpy(g_faces, st->faces, sizeof(g_faces));
    g_face_count = st->face_count;
}

static atari_measure_state g_screen_state;

wp_status atari_metrics_printer(VdiHdl printer, const short *work_out, short faces)
{
    if (printer <= 0 || work_out == NULL || work_out[3] <= 0 || work_out[4] <= 0) return WP_ERR;
    if (ensure_metrics_vdi() != WP_OK) return WP_ERR;
    if (!g_measuring_printer) save_state(&g_screen_state);
    g_measuring_printer = WP_TRUE;

    g_metrics_vdi = printer;
    g_px_um_x = (i32)work_out[3];
    g_px_um_y = (i32)work_out[4];
    g_dpi_x = 25400L / g_px_um_x;
    g_dpi_y = 25400L / g_px_um_y;
    if (g_dpi_x < 1) g_dpi_x = 1;
    if (g_dpi_y < 1) g_dpi_y = 1;
    g_num_colors = 2;
    /* No system cell: sizes are real points on paper (target_cell_px). */
    g_sys_cell_w = 0;
    g_sys_cell_h = 0;
    g_face_count = 0;
    if (faces > 1) build_face_table(faces);
    build_font_table();
    return WP_OK;
}

void atari_metrics_screen(void)
{
    if (!g_measuring_printer) return;
    restore_state(&g_screen_state);
    g_measuring_printer = WP_FALSE;
}

/* ------------------------------------------------------------------ */
/* Picking a font by PHYSICAL size, not by VDI's declared point size    */
/* ------------------------------------------------------------------ */

/* Why this exists at all.
 *
 * vst_point()'s point-size -> font mapping is IDENTICAL in every screen
 * mode. Measured directly on EmuTOS, sweeping 1..48pt in both:
 *
 *     ST Medium 640x200          ST High 640x400
 *      1pt -> cell  6x6           1pt -> cell  6x6
 *      9pt -> cell  8x8           9pt -> cell  8x8
 *     10pt -> cell  8x16         10pt -> cell  8x16
 *     16pt -> cell 12x12         16pt -> cell 12x12
 *     18pt -> cell 16x16         18pt -> cell 16x16
 *     20pt -> cell 16x32         20pt -> cell 16x32
 *
 * Byte-for-byte the same table. But the PIXELS are not: Medium reports
 * 169x372 microns (pixels 2.2x taller than wide), High reports 372x372
 * (square). So VDI's declared point sizes are only meaningful on a
 * square-pixel device, and taking them at face value -- which this file
 * did, passing size_twips/20 straight to vst_point -- picks a font by a
 * number that does not describe the screen it is about to be drawn on.
 *
 * Concretely, the bug that prompted this: a 12pt document in Medium got
 * cell 8x16, which at 169x372 microns is 1352 x 5952 -- a glyph cell
 * 4.4x taller than wide, against GEM's own chrome in the same mode,
 * which uses 8x8 (2.2x). Document text rendered at double the height it
 * should be and looked stretched. The identical cell in High is
 * 2976x5952, a perfectly normal 1:2 text cell, which is why this only
 * ever looked wrong in Medium.
 *
 * So: choose by real physical dimensions, using the per-axis micron
 * figures the workstation already reports. Two filters, in order:
 *
 *   1. Reject cells whose PHYSICAL shape is not text-shaped -- height
 *      between 1.6x and 3.0x the width. This is what actually excludes
 *      8x16 in Medium (4.4x) and 12x12 in High (1.0x), without either
 *      mode being special-cased anywhere.
 *   2. Among the survivors take the one whose physical height is
 *      nearest the requested size.
 *
 * The bounds are deliberately wide. They are not trying to express a
 * typographic ideal; they only have to separate "a cell meant for this
 * pixel aspect" from "a cell meant for the other one", and the two
 * groups sit at 2.2x and 4.4x (Medium) or 1.0x and 2.0x (High) -- far
 * apart, with 1.6/3.0 comfortably between them either way. */
#define ATARI_ASPECT_MIN_10  16   /* height >= 1.6 * width  */
#define ATARI_ASPECT_MAX_10  30   /* height <= 3.0 * width  */

static void build_font_table(void)
{
    short pt;
    short charw, charh, cellw, cellh;
    short prev_w = -1, prev_h = -1;

    g_font_size_count = 0;

    /* 1..48pt covers every distinct cell stock TOS can produce with room
       to spare (the largest, 16x32, appears at 20pt). Consecutive
       duplicates are skipped, so the table holds one entry per distinct
       cell together with the LOWEST point size that selects it. */
    for (pt = 1; pt <= 48 && g_font_size_count < ATARI_MAX_FONT_SIZES; pt++) {
        vst_point(g_metrics_vdi, pt, &charw, &charh, &cellw, &cellh);
        if (cellw == prev_w && cellh == prev_h) continue;
        if (cellw <= 0 || cellh <= 0) continue;
        prev_w = cellw;
        prev_h = cellh;
        g_font_sizes[g_font_size_count].pt = pt;
        g_font_sizes[g_font_size_count].cell_w = cellw;
        g_font_sizes[g_font_size_count].cell_h = cellh;
        g_font_size_count++;
    }
}

/* The document's own default body size (docmodel.c: default_cs.size_twips
   = 240, 12pt). Not exported by any engine header, so it is restated
   here as what it is used for: the size that should come out looking
   like the machine's normal text, i.e. the anchor the scale below is
   pinned to. */
#define ATARI_REFERENCE_BODY_TWIPS 240

/* Returns the vst_point argument whose cell best matches size_twips on
   THIS screen.
 *
 * The target is expressed relative to the AES system cell rather than in
 * absolute physical units, and that choice is worth stating plainly
 * because the obvious alternative is wrong in practice.
 *
 * Matching absolute physical height is more "correct" in the abstract: a
 * 12pt line really is 4233 microns, and in Medium the cell nearest that
 * is 12x12. But the ST's whole font inventory is six cells, and pinning
 * 12pt to 12x12 yields 53 columns across a 640-pixel screen in a mode
 * whose native text grid is 80. GEM's own chrome runs at 8x8 there. A
 * word processor that gives you 53 columns on an 80-column screen, in a
 * font a third larger than everything else on the desktop, is not more
 * correct for being arithmetically defensible.
 *
 * So the reference body size is defined to BE the system cell, and other
 * sizes scale from it. 12pt lands on 8x8 in Medium (80 columns) and 8x16
 * in High (80 columns) -- matching what the AES itself uses in each mode,
 * which is the same answer GEM reaches and the reason both look right.
 * Relative sizing survives: 8pt still comes out smaller and 24pt still
 * comes out roughly twice the body size, so the Font Size dialog keeps
 * meaning something.
 *
 * The physical aspect filter still applies on top, and is still what
 * stops Medium from picking a cell built for square pixels -- the
 * original defect. Scaling changed WHICH text-shaped cell gets chosen;
 * it did not reintroduce the possibility of choosing a wrong-shaped one.
 *
 * Falls back to the plain points conversion only if the sweep found
 * nothing, so this can never return something vst_point would reject. */
/* The cell height, in device pixels, a size should come out at:
   scaled from the AES's own system cell (8 in Medium, 16 in High), so
   the body size looks like the machine's normal text; without a usable
   cell, the real physical height (twips -> microns -> pixels, 1440
   twips to 25400 microns, x * 635 / 36). */
static i32 target_cell_px(i32 size_twips)
{
    i32 px;

    if (g_sys_cell_h > 0) px = ((i32)g_sys_cell_h * size_twips) / ATARI_REFERENCE_BODY_TWIPS;
    else px = ((size_twips * 635L) / 36L) / (g_px_um_y > 0 ? g_px_um_y : 1);
    return (px < 1) ? 1 : px;
}

static short pick_point_size(i32 size_twips)
{
    i32 target_px_h;
    i32 best_delta = 0;
    short best_pt = 0;
    short i;
    short pass;

    if (g_font_size_count == 0) {
        short pt = (short)(size_twips / 20);
        return (pt < 1) ? (short)1 : pt;
    }

    target_px_h = target_cell_px(size_twips);

    /* pass 0 applies the aspect filter; pass 1 repeats without it, so a
       device whose every cell fails (some future GDOS, a printer) still
       gets the nearest height rather than nothing at all. */
    for (pass = 0; pass < 2 && best_pt == 0; pass++) {
        for (i = 0; i < g_font_size_count; i++) {
            i32 phys_w = (i32)g_font_sizes[i].cell_w * g_px_um_x;
            i32 phys_h = (i32)g_font_sizes[i].cell_h * g_px_um_y;
            i32 delta;

            if (pass == 0) {
                if (phys_h * 10L < phys_w * ATARI_ASPECT_MIN_10) continue;
                if (phys_h * 10L > phys_w * ATARI_ASPECT_MAX_10) continue;
            }

            delta = (i32)g_font_sizes[i].cell_h - target_px_h;
            if (delta < 0) delta = -delta;

            if (best_pt == 0 || delta < best_delta) {
                best_delta = delta;
                best_pt = g_font_sizes[i].pt;
            }
        }
    }

    return best_pt;
}

void atari_ensure_gdos_fonts(VdiHdl vdi)
{
    /* Ask a resident scalable-font GDOS to load its fonts for this
       workstation. If none is resident, or the call fails, this is a
       no-op: vst_point() continues to snap to the nearest built-in
       bitmap size exactly as it did before GDOS was ever probed for.
       Must be called once per REAL handle that will select fonts --
       registering g_metrics_vdi here does nothing for a window's own
       separate drawing handle, the same "attributes don't cross
       handles" reason atari_select_font (below) has to be called on
       both. */
    if (vq_gdos() != 0) vst_load_fonts(vdi, 0);
}

/* Round to nearest, not toward zero.
 *
 * These conversions are applied per-component and then summed, so a
 * consistent downward bias accumulates into a whole pixel. Found from a
 * screenshot: with the 8x8 font, ascent 6px + descent 1px + leading 1px
 * became 127 + 21 + 21 = 169 twips, and 169 twips back is 7.98 pixels,
 * which truncated to 7. Lines were therefore set a pixel tighter than
 * the font's own cell and every line clipped the tops of the one below.
 * Three truncations of at most one twip each, compounding into a visible
 * rendering fault. Rounding gives 8, which is exactly the cell.
 *
 * Negative inputs round symmetrically rather than toward negative
 * infinity, so a coordinate and its negation stay mirror images. */
static i32 scale_round(i32 v, i32 num, i32 den)
{
    if (den <= 0) return 0;
    if (v >= 0) return (v * num + den / 2) / den;
    return -((((-v) * num) + den / 2) / den);
}

i32 atari_twips_to_px_x(i32 twips)
{
    ensure_metrics_vdi();
    return scale_round(twips, g_dpi_x, 1440);
}

i32 atari_twips_to_px_y(i32 twips)
{
    ensure_metrics_vdi();
    return scale_round(twips, g_dpi_y, 1440);
}

i32 atari_px_to_twips_x(i32 px)
{
    ensure_metrics_vdi();
    return scale_round(px, 1440, g_dpi_x);
}

i32 atari_px_to_twips_y(i32 px)
{
    ensure_metrics_vdi();
    return scale_round(px, 1440, g_dpi_y);
}

i32 atari_screen_max_colors(void)
{
    ensure_metrics_vdi();
    return g_num_colors;
}

/* The palette, in VDI's 0..1000 per gun, read on first use; pens from
   2 up to g_taken_hi are an image's. A few recent answers are kept: a
   ruler bar repaint asks for the same handful of greys hundreds of
   times, and each fresh answer is a scan of up to 256 pens. */
#define ATARI_PAL_MAX 256
static short g_pal[ATARI_PAL_MAX][3];
static short g_pal_count = 0;          /* 0: not read yet */
static short g_taken_hi = 2;
static struct { u8 r, g, b, ok; short pen; } g_pen_cache[8];
static u32 g_pen_cache_next = 0;

void atari_palette_taken(short first, short count)
{
    u32 i;

    if (first + count > g_taken_hi) g_taken_hi = (short)(first + count);
    g_pal_count = 0;
    for (i = 0; i < 8; i++) g_pen_cache[i].ok = 0;
}

short atari_nearest_pen(u8 r, u8 g, u8 b)
{
    i32 tr, tg, tb, best_d = -1;
    short best = G_BLACK, i, n;
    u32 k;

    if (ensure_metrics_vdi() != WP_OK || g_num_colors < 16) {
        return ((u32)r + (u32)g + (u32)b < 384) ? (short)G_BLACK : (short)G_WHITE;
    }
    for (k = 0; k < 8; k++) {
        if (g_pen_cache[k].ok && g_pen_cache[k].r == r && g_pen_cache[k].g == g && g_pen_cache[k].b == b)
            return g_pen_cache[k].pen;
    }
    if (g_pal_count == 0) {
        n = (short)(g_num_colors < ATARI_PAL_MAX ? g_num_colors : ATARI_PAL_MAX);
        for (i = 0; i < n; i++) (void)vq_color(g_metrics_vdi, i, 1, g_pal[i]);
        g_pal_count = n;
    }
    tr = ((i32)r * 1000) / 255;
    tg = ((i32)g * 1000) / 255;
    tb = ((i32)b * 1000) / 255;
    for (i = 0; i < g_pal_count; i++) {
        i32 dr, dg, db, d;
        if (i >= 2 && i < g_taken_hi) continue;
        dr = g_pal[i][0] - tr; dg = g_pal[i][1] - tg; db = g_pal[i][2] - tb;
        d = dr * dr + dg * dg + db * db;
        if (best_d < 0 || d < best_d) { best_d = d; best = i; }
    }
    k = g_pen_cache_next++ & 7u;
    g_pen_cache[k].r = r; g_pen_cache[k].g = g; g_pen_cache[k].b = b;
    g_pen_cache[k].pen = best;
    g_pen_cache[k].ok = 1;
    return best;
}

static short map_effects(u16 style_flags)
{
    short effects = TXT_NORMAL;

    if (style_flags & PLAT_STYLE_BOLD)      effects |= TXT_THICKENED;
    if (style_flags & PLAT_STYLE_ITALIC)    effects |= TXT_SKEWED;
    if (style_flags & PLAT_STYLE_UNDERLINE) effects |= TXT_UNDERLINED;
    /* PLAT_STYLE_STRIKE has no VDI text-effect equivalent, and the
       document model's own SUPER/SUB never reach this seam at all --
       neither is mapped; documented gap in docs/atari-platform.md. */
    return effects;
}

/* Re-applies this font's point size and effects to vdi's own current
   text attributes. Must run on a handle before any of ITS OWN calls
   that depend on current text attributes (vst_point, vqt_extent,
   vqt_fontinfo, v_gtext) -- see metrics_atari_internal.h's own comment
   on why this takes an explicit vdi param rather than assuming
   g_metrics_vdi the way this function used to. */
/* Selects f's face and size on vdi; *cellh_out the cell height it got. */
static void select_face_size(VdiHdl vdi, const atari_font *f, short *cellh_out)
{
    short charw, charh, cellw, cellh;

    vst_font(vdi, f->face_id);
#ifndef GEM4XE_APP_GEM_H
    if (g_outline_gdos && g_measuring_printer && f->face_id != 1)
        (void)vst_arbpt(vdi, f->point_size, &charw, &charh, &cellw, &cellh);
    else
#endif
    vst_point(vdi, f->point_size, &charw, &charh, &cellw, &cellh);
    *cellh_out = cellh;
}

void atari_select_font(VdiHdl vdi, const atari_font *f)
{
    short cellh;

    select_face_size(vdi, f, &cellh);
    vst_effects(vdi, map_effects(f->style_flags));
}

static const char *face_name_at(u32 index, void *user)
{
    WP_UNUSED(user);
    return g_faces[index].name;
}

u32 plat_font_face_count(void)
{
    if (ensure_metrics_vdi() != WP_OK) return 0;
    return (g_face_count > 1) ? (u32)g_face_count : 0u;
}

wp_status plat_font_face_name(u32 index, u8 *utf8_out, u32 cap, u32 *len_out)
{
    u32 n;

    *len_out = 0;
    if (index >= plat_font_face_count()) return WP_RANGE;
    n = (u32)strlen(g_faces[index].name);
    if (n > cap) n = cap;
    memcpy(utf8_out, g_faces[index].name, n);
    *len_out = n;
    return WP_OK;
}

wp_status plat_font_open(const u8 *utf8_font_name, u32 name_len,
                          i32 size_twips, u16 style_flags, plat_font **out)
{
    atari_font *f;
    wp_status st;
    short pt;

    st = ensure_metrics_vdi();
    if (st != WP_OK) return st;

    f = (atari_font *)mem_alloc((u32)sizeof(atari_font));
    if (f == NULL) return WP_NOMEM;

    /* The named face if GDOS has it or one of its family, else the
       system font. Sized by the same pick_point_size as the system
       font: a Speedo outline at that vst_point argument comes out the
       physical height the system cell does, and a bitmap GDOS face
       snaps to its nearest size the way the system font always has. */
    f->face_id = 1;
    if (g_face_count > 1 &&
        (name_len > 0 || (style_flags & PLAT_FAMILY_MASK) != 0)) {
        i32 at = plat_font_pick(face_name_at, NULL, (u32)g_face_count,
                                utf8_font_name, name_len, style_flags);
        if (at >= 0) f->face_id = g_faces[at].id;
    }

    /* NOT size_twips/20. point_size holds the vst_point ARGUMENT that
       yields the right physical cell on this screen, which is not the
       document's point size except by coincidence -- see pick_point_size
       for the measurements. Every consumer (atari_select_font,
       plat_font_metrics_get, draw_atari.c) already treats this field as
       "what to hand vst_point", so nothing downstream changes. */
    pt = pick_point_size(size_twips);
    if (f->face_id != 1) {
        /* A GDOS face is not the system font, and the system font's few
           sizes say nothing about it -- under fVDI they stop at the 8x16
           cell, so 24 pt asked a TrueType face for 10. Its point size is
           the target cell height at the screen's own vertical resolution
           instead: the same physical height the system font would have
           been aimed at, which an outline face hits exactly and a bitmap
           face snaps to its nearest. */
        pt = (short)((target_cell_px(size_twips) * 72L + g_dpi_y / 2) / (g_dpi_y > 0 ? g_dpi_y : 72));
    }
    if (pt < 1) pt = 1;
    f->point_size = pt;
    f->style_flags = style_flags;
    plat_paper_face_init(&f->paper, utf8_font_name, name_len, size_twips, style_flags);

    *out = (plat_font *)f;
    return WP_OK;
}

/* Proves the font actually chosen for body text is shaped like text ON
   THIS SCREEN, which is precisely what was wrong before: in ST Medium a
   12pt document resolved to cell 8x16 = 1352 x 5952 microns, a glyph
   cell 4.4x taller than wide, and nothing anywhere noticed. The existing
   layout selfcheck could not have caught it -- it asserts wrapping is
   internally consistent, and wrapping stayed perfectly consistent while
   every glyph was being drawn at double height.

   Deliberately expressed in MICRONS, not pixels. A pixel-space
   assertion is unfalsifiable here: cell 8x16 is 1:2 in pixels in every
   mode, which reads as correct on paper and is exactly the reasoning
   that let this through. The physical figures are what differ between
   Medium and High, so they are what the check has to use.

   CI boots Medium (--tos-res med, host/tools/atari_ci_lib.sh), so this
   runs in the mode that was broken. */
wp_bool wp_atari_font_selfcheck(void)
{
    static const i32 sizes[] = { 160, 240, 480 };  /* 8pt, 12pt, 24pt */
    u32 i;

    if (ensure_metrics_vdi() != WP_OK) return WP_FALSE;

    /* The sweep must have found something, and every entry must be
       usable -- an empty table silently sends pick_point_size down its
       points-conversion fallback, i.e. straight back to the old bug. */
    if (g_font_size_count <= 0) return WP_FALSE;
    if (g_px_um_x <= 0 || g_px_um_y <= 0) return WP_FALSE;

    for (i = 0; i < (u32)g_font_size_count; i++) {
        if (g_font_sizes[i].cell_w <= 0 || g_font_sizes[i].cell_h <= 0) return WP_FALSE;
        if (g_font_sizes[i].pt <= 0) return WP_FALSE;
    }

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        plat_font *pf;
        atari_font *af;
        short charw, charh, cellw, cellh;
        i32 phys_w, phys_h;

        if (plat_font_open(NULL, 0, sizes[i], 0, &pf) != WP_OK) return WP_FALSE;
        af = (atari_font *)pf;
        vst_font(g_metrics_vdi, af->face_id);
        vst_point(g_metrics_vdi, af->point_size, &charw, &charh, &cellw, &cellh);
        plat_font_close(pf);

        if (cellw <= 0 || cellh <= 0) return WP_FALSE;

        phys_w = (i32)cellw * g_px_um_x;
        phys_h = (i32)cellh * g_px_um_y;

        if (phys_h * 10L < phys_w * ATARI_ASPECT_MIN_10) return WP_FALSE;
        if (phys_h * 10L > phys_w * ATARI_ASPECT_MAX_10) return WP_FALSE;
    }

    return WP_TRUE;
}

void plat_font_close(plat_font *f)
{
    mem_free(f);
}

wp_status plat_font_metrics_get(plat_font *f, plat_font_metrics *out)
{
    atari_font *af = (atari_font *)f;
    short cellh;
    short minade, maxade, maxwidth;
    short distances[10];
    short effects[10];

    if (af == NULL || out == NULL) return WP_ERR;
    if (plat_metrics_paper()) { plat_paper_metrics(&af->paper, out); return WP_OK; }

    /* vst_point selects the point size (required before querying font
       info); its own charw/cellh outputs are no longer used for
       ascent/descent -- vqt_fontinfo's distances[] gives the real
       breakdown instead. distances[1] is descent-to-baseline and
       distances[3] is ascent-to-baseline, both positive magnitudes (not
       signed coordinates) -- verified empirically against a real EmuTOS
       boot, where distances[3] matched vst_point's own charh exactly
       for the built-in bitmap font. See docs/atari-platform.md. */
    select_face_size(g_metrics_vdi, af, &cellh);
    vqt_fontinfo(g_metrics_vdi, &minade, &maxade, distances, &maxwidth, effects);

    out->ascent = atari_px_to_twips_y((i32)distances[3]);
    out->descent = atari_px_to_twips_y((i32)distances[1]);
    if (out->descent < 0) out->descent = 0;

    /* Leading is whatever the font's own CELL has left over above
       ascent+descent, not a hardcoded 0.
     *
     * linebreak.c computes line height as ascent + descent + leading, so
     * returning 0 made consecutive baselines land ascent+descent apart
     * while the glyphs themselves occupy a full cell. For the 8x8 font
     * that is 6+1 = 7 against a cell of 8: every line overlapped the one
     * above by a pixel and the tops of the glyphs were clipped. It went
     * unnoticed while the 8x16 font was being chosen, because there
     * 11+2 = 13 fits inside a 16-pixel cell with room to spare.
     *
     * cellh is the pitch the font was designed to be set at, so the
     * remainder is exactly the leading it expects. */
    {
        i32 gap = (i32)cellh - ((i32)distances[3] + (i32)distances[1]);
        out->leading = (gap > 0) ? atari_px_to_twips_y(gap) : 0;
    }
    return WP_OK;
}

wp_status plat_measure_text(plat_font *f, const u8 *utf8, u32 len,
                             i32 *width_twips_out)
{
    atari_font *af = (atari_font *)f;
    char buf[256];
    u32 n, i;
    short extent[8];
    short min_x, max_x;
    wp_status st;

    if (af == NULL || width_twips_out == NULL) return WP_ERR;
    if (plat_metrics_paper()) return plat_paper_measure(&af->paper, utf8, len, width_twips_out);

    n = (len < sizeof(buf) - 1) ? len : (u32)(sizeof(buf) - 1);
    /* No real Atari-ST-charset mapping table yet (plat_utf8_to_native is
       ASCII-passthrough-plus-'?' -- see draw_atari.c and
       docs/atari-platform.md); reuse it here so measurement and drawing
       agree on what bytes are actually being rendered. */
    {
        u32 out_len = 0;
        u32 unmapped = 0;
        st = plat_utf8_to_native(utf8, n, (u8 *)buf, (u32)(sizeof(buf) - 1), &out_len, &unmapped);
        if (st != WP_OK) return st;
        n = out_len;
    }
    buf[n] = '\0';

    atari_select_font(g_metrics_vdi, af);
    /* Slanting draws a glyph leaning over its neighbour and advances by
       the upright width; vqt_extent counts the lean as well, which left a
       gap after every italic word. Measured upright, the advance is what
       the next word is placed by. */
    if (af->style_flags & PLAT_STYLE_ITALIC) {
        vst_effects(g_metrics_vdi, map_effects((u16)(af->style_flags & ~PLAT_STYLE_ITALIC)));
    }

    if (n == 0) {
        *width_twips_out = 0;
        return WP_OK;
    }

    vqt_extent(g_metrics_vdi, buf, extent);
    min_x = max_x = extent[0];
    for (i = 2; i <= 6; i += 2) {
        if (extent[i] < min_x) min_x = extent[i];
        if (extent[i] > max_x) max_x = extent[i];
    }

    *width_twips_out = atari_px_to_twips_x((i32)(max_x - min_x));
    return WP_OK;
}
