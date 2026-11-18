/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include <string.h>
#include "platform.h"
#include "utf8.h"
#include "metrics_atari_internal.h"

/* The Atari ST character set's upper half, as Unicode: 0x80 + i shows
   g_atari_hi[i]; 0 for a code not mapped here (the Hebrew block 0xC2-0xDC
   and a few symbols whose glyphs could not be checked), which leaves its
   character to utf8_ascii_fallback. The Latin-1 half agrees with
   EmuTOS's own latin1-to-atarist table (tools/bug.c, read as a
   specification); the rest is the standard ST set -- Greek, maths, the
   ligatures. */
static const u16 g_atari_hi[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,   /* 80 */
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,   /* 90 */
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x00DF, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,   /* A0 */
    0x00BF, 0,      0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x00E3, 0x00F5, 0x00D8, 0x00F8, 0x0153, 0x0152, 0x00C0, 0x00C3,   /* B0 */
    0x00D5, 0x00A8, 0x00B4, 0x2020, 0x00B6, 0x00A9, 0x00AE, 0x2122,
    0x0133, 0x0132, 0,      0,      0,      0,      0,      0,        /* C0 */
    0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,        /* D0 */
    0,      0,      0,      0,      0,      0x00A7, 0,      0x221E,
    0x03B1, 0x03B2, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,   /* E0 */
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0,      0,      0x2208, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0,      0,      0x00F7, 0x2248,   /* F0 */
    0x00B0, 0x2022, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x00B3, 0x00AF
};

/* Unicode to the Atari set: ASCII as itself, the upper half through
   g_atari_hi above, and a character the set lacks as
   utf8_ascii_fallback's nearest plain one; only one with none becomes
   '?', counted in *unmapped_count. */
wp_status plat_utf8_to_native(const u8 *utf8, u32 len,
                               u8 *out, u32 out_cap, u32 *out_len,
                               u32 *unmapped_count)
{
    u32 pos = 0;
    u32 written = 0;
    u32 unmapped = 0;

    if (utf8 == NULL || out == NULL) return WP_ERR;

    while (pos < len && written < out_cap) {
        u32 cp;
        u32 n = utf8_decode(utf8 + pos, len - pos, &cp);

        if (n == 0) {
            out[written++] = '?';
            unmapped++;
            pos++;
            continue;
        }

        if (cp <= 0x7F) {
            out[written++] = (u8)cp;
        } else {
            u32 k;
            for (k = 0; k < 128 && g_atari_hi[k] != cp; k++) {}
            if (k < 128) {
                out[written++] = (u8)(0x80 + k);   /* the Atari set has it */
            } else {
                u8 f = utf8_ascii_fallback(cp);   /* the nearest plain character, if any */
                out[written++] = f != 0 ? f : (u8)'?';
                if (f == 0) unmapped++;
            }
        }
        pos += n;
    }

    if (out_len) *out_len = written;
    if (unmapped_count) *unmapped_count = unmapped;
    return WP_OK;
}

/* The other way, for keyboard, clipboard and file input: a German or
   French keyboard's accented keys arrive as the set's upper-half codes,
   which g_atari_hi turns into their characters; an unmapped one is '?'. */
wp_status plat_native_to_utf8(const u8 *native, u32 len,
                               u8 *out, u32 out_cap, u32 *out_len)
{
    u32 i;
    u32 written = 0;

    if (native == NULL || out == NULL) return WP_ERR;

    for (i = 0; i < len && written < out_cap; i++) {
        u32 cp = native[i] <= 0x7F ? native[i] : g_atari_hi[native[i] - 0x80];
        u8 enc[4];
        u32 n;
        if (cp == 0) cp = '?';
        n = utf8_encode(cp, enc);
        if (written + n > out_cap) break;
        memcpy(out + written, enc, n);
        written += n;
    }

    if (out_len) *out_len = written;
    return WP_OK;
}

/* r,g,b -> a VDI pen: the nearest in the real palette on a screen of
   16 colors or more, white or black below that (atari_nearest_pen). */
static short rgb_to_vdi_color(u8 r, u8 g, u8 b)
{
    return atari_nearest_pen(r, g, b);
}

wp_status plat_draw_text(plat_gc *gc, plat_font *f, i32 x, i32 y,
                          const u8 *utf8, u32 len, u8 r, u8 g, u8 b)
{
    atari_gc *agc = (atari_gc *)gc;
    const atari_font *af = (const atari_font *)f;
    char buf[256];
    u32 n, out_len, unmapped;
    wp_status st;

    if (agc == NULL || af == NULL) return WP_ERR;

    n = (len < sizeof(buf) - 1) ? len : (u32)(sizeof(buf) - 1);
    st = plat_utf8_to_native(utf8, n, (u8 *)buf, (u32)(sizeof(buf) - 1), &out_len, &unmapped);
    if (st != WP_OK) return st;
    buf[out_len] = '\0';

    /* v_gtext draws with ITS OWN handle's (agc->vdi) current text
       attributes -- a real, distinct handle from g_metrics_vdi, which
       is what plat_measure_text/plat_font_metrics_get select during
       layout (VDI text attributes are per-workstation-handle, not
       global). This was NOT actually reaching a different result in
       practice on the real/EmuTOS environments tested (both handles
       reported the identical selected font, so this alone isn't what
       causes the ST-High rendering issue documented in
       docs/atari-platform.md's own ST-High section) -- but it's still
       the only correct way to guarantee agc->vdi draws at the font
       plat_font_open was actually asked for, rather than relying on
       both handles coincidentally agreeing, so it stays.
       It also keeps a printer alive: under SpeedoGDOS on TOS 1.62,
       2.06 and 3.06, v_gtext on a printer workstation before any
       vst_font bus-errors in the ROM (Hatari, 2026-11-15; vst_font(h, 1)
       first, then either vst_point or vst_height, prints cleanly). */
    atari_select_font(agc->vdi, af);
    vst_color(agc->vdi, rgb_to_vdi_color(r, g, b));
    /* Transparent: in replace mode v_gtext paints each character cell's
       background, which punched white holes in a paragraph's shading.
       Every caller clears behind its text first. */
    vswr_mode(agc->vdi, MD_TRANS);
    v_gtext(agc->vdi, (short)x, (short)y, buf);
    vswr_mode(agc->vdi, MD_REPLACE);
    return WP_OK;
}

wp_status plat_fill_rect(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                          u8 r, u8 g, u8 b)
{
    atari_gc *agc = (atari_gc *)gc;
    short pxy[4];

    if (agc == NULL) return WP_ERR;

    pxy[0] = (short)x;
    pxy[1] = (short)y;
    pxy[2] = (short)(x + w - 1);
    pxy[3] = (short)(y + h - 1);

    vsf_interior(agc->vdi, FIS_SOLID);
    vsf_color(agc->vdi, rgb_to_vdi_color(r, g, b));
    v_bar(agc->vdi, pxy);
    return WP_OK;
}

/* VDI's fill patterns: style 1 to 7 of FIS_PATTERN are dither densities
   from about 1/8 to 7/8, 8 solid -- on a screen or a printer workstation
   alike, so shading prints as it shows. */
wp_status plat_fill_shade(plat_gc *gc, i32 x, i32 y, i32 w, i32 h, u8 percent)
{
    atari_gc *agc = (atari_gc *)gc;
    short pxy[4];
    short style;

    if (agc == NULL) return WP_ERR;
    if (w <= 0 || h <= 0 || percent == 0) return WP_OK;
    style = (short)(((i32)percent * 8 + 50) / 100);
    if (style < 1) style = 1;
    if (style > 8) style = 8;
    pxy[0] = (short)x;
    pxy[1] = (short)y;
    pxy[2] = (short)(x + w - 1);
    pxy[3] = (short)(y + h - 1);
    /* A polygon filled by spans on a printer (polys_by_spans) comes here
       a row at a time, and SpeedoGDOS's FX80 prints nothing for a
       one-row pattern bar (a solid one prints): its patterned bars seem
       to stop short of their last row. Such a row goes two rows tall. */
    if (agc->polys_by_spans && h == 1) pxy[3] = (short)(y + 1);
    vsf_interior(agc->vdi, FIS_PATTERN);
    vsf_style(agc->vdi, style);
    vsf_color(agc->vdi, G_BLACK);
    vswr_mode(agc->vdi, MD_TRANS);
    /* v_bar outlines its fill when the perimeter is on: invisible on a
       solid fill, a rule around every shaded paragraph on a pattern. */
    vsf_perimeter(agc->vdi, 0);
    v_bar(agc->vdi, pxy);
    vsf_perimeter(agc->vdi, 1);
    vswr_mode(agc->vdi, MD_REPLACE);
    vsf_interior(agc->vdi, FIS_SOLID);
    return WP_OK;
}

/* Vector drawing (platform.h). VDI fills a polygon natively; some VDIs
   take no more than 128 points a call, so a longer one goes by spans.
   A mid grey takes the pattern plat_fill_shade would, with the perimeter
   off, so it survives a mono screen and a printer alike. Lines wider
   than a pixel are drawn as polygons: SpeedoGDOS's FX80 driver wrote to
   address 0 on vsl_width 3 under Hatari, and a polygon gives every
   driver the same joints and ends. That driver's v_fillarea writes to
   address 0 too (2026-11-10, likely the same fault): a gc can ask for
   polygons by spans instead (atari_gc.polys_by_spans). */
#define ATARI_POLY_MAX 128
static short g_poly_pxy[ATARI_POLY_MAX * 2];

wp_status plat_fill_polygon(plat_gc *gc, const i32 *xy, u16 n, u8 r, u8 g, u8 b)
{
    atari_gc *agc = (atari_gc *)gc;
    /* Below 16 colours -- mono, Medium, a printer -- every mid colour is
       a pattern by its brightness; above, only a grey. */
    u8 grey = atari_screen_max_colors() < 16 ? plat_luma_percent(r, g, b) : plat_grey_percent(r, g, b);
    u16 i;

    if (agc == NULL) return WP_ERR;
    if (n < 3) return WP_OK;
    if (n > ATARI_POLY_MAX || agc->polys_by_spans) return plat_polygon_by_spans(gc, xy, n, r, g, b, grey);
    for (i = 0; i < n * 2; i++) g_poly_pxy[i] = (short)xy[i];
    if (grey != 0) {
        short style = (short)(((i32)grey * 8 + 50) / 100);
        if (style < 1) style = 1;
        if (style > 8) style = 8;
        vsf_interior(agc->vdi, FIS_PATTERN);
        vsf_style(agc->vdi, style);
        vsf_color(agc->vdi, G_BLACK);
        vswr_mode(agc->vdi, MD_TRANS);
    } else {
        vsf_interior(agc->vdi, FIS_SOLID);
        vsf_color(agc->vdi, rgb_to_vdi_color(r, g, b));
    }
    vsf_perimeter(agc->vdi, 0);
    v_fillarea(agc->vdi, (short)n, g_poly_pxy);
    vsf_perimeter(agc->vdi, 1);
    vswr_mode(agc->vdi, MD_REPLACE);
    vsf_interior(agc->vdi, FIS_SOLID);
    return WP_OK;
}

wp_status plat_draw_polyline(plat_gc *gc, const i32 *xy, u16 n, i32 width,
                             u8 r, u8 g, u8 b)
{
    atari_gc *agc = (atari_gc *)gc;
    u16 at = 0;

    if (agc == NULL) return WP_ERR;
    if (n < 2) return WP_OK;
    if (width > 1) return plat_polyline_by_polygons(gc, xy, n, width, r, g, b);
    vsl_width(agc->vdi, 1);
    vsl_type(agc->vdi, 1);
    vsl_color(agc->vdi, rgb_to_vdi_color(r, g, b));
    /* In pieces of 128 points, each starting where the last ended. */
    while (at + 1 < n) {
        u16 k = (u16)(n - at > ATARI_POLY_MAX ? ATARI_POLY_MAX : n - at), i;
        for (i = 0; i < k * 2; i++) g_poly_pxy[i] = (short)xy[at * 2 + i];
        v_pline(agc->vdi, (short)k, g_poly_pxy);
        at = (u16)(at + k - 1);
    }
    return WP_OK;
}

/* M15 Phase 5b: real ST VDI raster-copy blit. Two hard, ST-specific
   constraints shape this (see docs/atari-platform.md and this
   milestone's own research):

   (1) An MFDB source form in "device-specific" format (fd_stand=0) must
   be genuine word-interleaved ST bitplanes -- NOT the packed-nibble-
   per-byte chunky format the wire protocol/gateway actually produce
   (docs/formats/offload-protocol.md's OFFLOAD_IMAGE section) -- so
   every pixel is unpacked and re-planed into a scratch buffer here.
   Standard ST convention: within each 16-pixel word group, bit 15 is
   the LEFTMOST pixel; plane N holds bit N of each pixel's color index
   (plane 0 = LSB).

   (2) The ST's palette is a small, GLOBAL, hardware-indexed table
   (vs_color reprograms real palette registers by index) -- unlike
   Amiga's per-request ObtainBestPen, which allocates from a shared but
   independent pen pool. Indices 0/1 already carry the G_WHITE/G_BLACK
   this backend's own plat_draw_text/plat_fill_rect use for every other
   on-screen pixel (rgb_to_vdi_color) -- reprogramming them would
   recolor existing text/UI everywhere on screen, not just this image's
   box. So this permanently reserves pens 0/1 and only ever programs
   pens 2..(colors-1) for image content. The real color count (2 mono/
   4 Medium/16 Low) is queried at runtime via atari_screen_max_colors()
   (VDI's own work_out[13], the same call metrics_atari.c already makes
   for DPI) rather than assumed -- do_insert_image already sized its
   OFFLOAD_IMAGE request against this same real budget. A genuinely
   mono (2-color) screen has no spare pen at all once 0/1 are reserved,
   so this honestly fails (WP_ERR) rather than corrupting the two
   existing text colors -- the caller already falls back to a solid
   palette[0] rect in that case, the same allowance a missing/unreadable
   cache file already gets. */
wp_status plat_draw_image(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                           const u8 *pixels, i32 src_width_px, i32 src_height_px,
                           const u8 palette[][3], u8 palette_count)
{
    atari_gc *agc = (atari_gc *)gc;
    i32 colors, nplanes;
    i32 wdwidth;
    u8 pen_for[16];
    u8 i;
    i32 px, py;
    u16 *planebuf;
    u32 planebuf_words, row_words, src_row_bytes;
    MFDB src_mfdb, dst_mfdb;
    short pxy[8];

    if (agc == NULL || pixels == NULL) return WP_ERR;
    if (w <= 0 || h <= 0 || src_width_px <= 0 || src_height_px <= 0) return WP_ERR;
    if (palette_count < 1 || palette_count > 16) return WP_ERR;

    colors = atari_screen_max_colors();
    nplanes = 0;
    while ((1L << nplanes) < colors && nplanes < 8) nplanes++;
    if (nplanes < 2) return WP_ERR; /* mono: no spare pen once 0/1 reserved */

    if ((i32)palette_count > colors - 2) palette_count = (u8)(colors - 2);

    for (i = 0; i < palette_count; i++) {
        short rgb[3];
        rgb[0] = (short)(((i32)palette[i][0] * 1000) / 255);
        rgb[1] = (short)(((i32)palette[i][1] * 1000) / 255);
        rgb[2] = (short)(((i32)palette[i][2] * 1000) / 255);
        vs_color(agc->vdi, (short)(i + 2), rgb);
        pen_for[i] = (u8)(i + 2);
    }
    atari_palette_taken(2, (short)palette_count);

    wdwidth = (src_width_px + 15) / 16;
    row_words = (u32)wdwidth * (u32)nplanes;
    planebuf_words = row_words * (u32)src_height_px;
    planebuf = (u16 *)mem_alloc(planebuf_words * (u32)sizeof(u16));
    if (planebuf == NULL) return WP_NOMEM;
    memset(planebuf, 0, planebuf_words * (u32)sizeof(u16));

    src_row_bytes = ((u32)src_width_px + 1u) / 2u;
    for (py = 0; py < src_height_px; py++) {
        const u8 *src_row = pixels + (u32)py * src_row_bytes;
        u16 *plane_row = planebuf + (u32)py * row_words;

        for (px = 0; px < src_width_px; px++) {
            u8 byte = src_row[px / 2];
            u8 nibble = (u8)((px % 2 == 0) ? (byte >> 4) : (byte & 0x0F));
            u8 pen;
            i32 word_idx = px / 16;
            i32 bit_idx = 15 - (px % 16);
            i32 plane;

            if (nibble >= palette_count) nibble = 0;
            pen = pen_for[nibble];

            for (plane = 0; plane < nplanes; plane++) {
                if (pen & (1 << plane)) {
                    u16 *w_ptr = &plane_row[(u32)word_idx * (u32)nplanes + (u32)plane];
                    *w_ptr = (u16)(*w_ptr | (1u << bit_idx));
                }
            }
        }
    }

    memset(&src_mfdb, 0, sizeof(src_mfdb));
    MFDB_SET_ADDR(src_mfdb, planebuf);
    src_mfdb.fd_w = (short)src_width_px;
    src_mfdb.fd_h = (short)src_height_px;
    src_mfdb.fd_wdwidth = (short)wdwidth;
    src_mfdb.fd_stand = 0;
    src_mfdb.fd_nplanes = (short)nplanes;

    memset(&dst_mfdb, 0, sizeof(dst_mfdb)); /* fd_addr=0 -> real screen, VDI fills in the rest */

    pxy[0] = 0;
    pxy[1] = 0;
    pxy[2] = (short)(src_width_px - 1);
    pxy[3] = (short)(src_height_px - 1);
    pxy[4] = (short)x;
    pxy[5] = (short)y;
    pxy[6] = (short)(x + w - 1);
    pxy[7] = (short)(y + h - 1);

    vro_cpyfm(agc->vdi, S_ONLY, pxy, &src_mfdb, &dst_mfdb);

    mem_free(planebuf);
    return WP_OK;
}

/* One vrt_cpyfm per icon, transparent: set pixels in the pen, clear
   ones untouched. A one-plane MFDB is the same in standard and device
   form, and the rows are already the VDI's words (big-endian, bit 15
   leftmost) on the ST. Clipped by hand to the caller's rectangle --
   raster copies ignore vs_clip. gem4xe's words are not the ST's byte
   order, so it draws run by run instead. */
wp_status plat_draw_mono(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                         const u16 *rows, u32 row_words, u8 r, u8 g, u8 b)
{
#ifdef GEM4XE_APP_GEM_H
    return plat_mono_by_runs(gc, x, y, w, h, rows, row_words, r, g, b);
#else
    atari_gc *agc = (atari_gc *)gc;
    MFDB src, dst;
    short pxy[8];
    short colors[2];
    i32 sx = 0, sy = 0;

    if (agc == NULL || rows == NULL || row_words == 0) return WP_ERR;
    if (w > (i32)(row_words * 16u)) w = (i32)(row_words * 16u);
    if (agc->clip_on) {
        i32 cx1 = agc->clip[0], cy1 = agc->clip[1], cx2 = agc->clip[2], cy2 = agc->clip[3];
        if (x < cx1) { sx = cx1 - x; w -= sx; x = cx1; }
        if (y < cy1) { sy = cy1 - y; h -= sy; y = cy1; }
        if (x + w - 1 > cx2) w = cx2 - x + 1;
        if (y + h - 1 > cy2) h = cy2 - y + 1;
    }
    if (w <= 0 || h <= 0) return WP_OK;

    memset(&src, 0, sizeof(src));
    MFDB_SET_ADDR(src, (void *)rows);
    src.fd_w = (short)(row_words * 16u);
    src.fd_h = (short)(sy + h);
    src.fd_wdwidth = (short)row_words;
    src.fd_stand = 0;
    src.fd_nplanes = 1;
    memset(&dst, 0, sizeof(dst));          /* fd_addr 0: the screen */

    pxy[0] = (short)sx;
    pxy[1] = (short)sy;
    pxy[2] = (short)(sx + w - 1);
    pxy[3] = (short)(sy + h - 1);
    pxy[4] = (short)x;
    pxy[5] = (short)y;
    pxy[6] = (short)(x + w - 1);
    pxy[7] = (short)(y + h - 1);
    colors[0] = rgb_to_vdi_color(r, g, b);
    colors[1] = 0;
    vrt_cpyfm(agc->vdi, MD_TRANS, pxy, &src, &dst, colors);
    return WP_OK;
#endif
}

void plat_gc_flush(plat_gc *gc)
{
    WP_UNUSED(gc);
}
