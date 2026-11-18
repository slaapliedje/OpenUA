/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include <string.h>
#include <Quickdraw.h>
#include "platform.h"
#include "utf8.h"
#include "metrics_mac_internal.h"

/* Mac Roman's upper half, as Unicode: 0x80 + i shows g_macroman_hi[i]
   (the same table RetroWP's engine reads \\mac RTF with). */
static const u16 g_macroman_hi[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
    0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7
};

/* plat_utf8_to_native/plat_native_to_utf8 were pulled forward to Phase 1
   because the clipboard and file seams needed them to link -- same
   reasoning draw_amiga.c gives for doing the identical pull-forward in
   M9 Phase 1.

   Unlike Amiga's ISO-8859-1 (a 1:1 identity map for 0x80-0xFF), Mac OS
   Roman is its own ordering: g_macroman_hi above, both ways. A
   character it lacks takes utf8_ascii_fallback's nearest plain one, and
   only one with none becomes '?', counted in *unmapped_count. */

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
            for (k = 0; k < 128 && g_macroman_hi[k] != cp; k++) {}
            if (k < 128) {
                out[written++] = (u8)(0x80 + k);   /* the Mac Roman set has it */
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

wp_status plat_native_to_utf8(const u8 *native, u32 len,
                               u8 *out, u32 out_cap, u32 *out_len)
{
    u32 i;
    u32 written = 0;

    if (native == NULL || out == NULL) return WP_ERR;

    for (i = 0; i < len && written < out_cap; i++) {
        u32 cp = native[i] <= 0x7F ? native[i] : g_macroman_hi[native[i] - 0x80];
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


/* -- Glyph draw (Phase 3): real QuickDraw, drawing into whatever real
   window the caller's mac_gc names (app_shell.c's own g_win during a
   real updateEvt repaint). Only ever asked to draw black text on a
   white background so far -- every fixture ROM boots into a 1-bit
   QuickDraw port by default, and color isn't a real feature on any
   backend yet -- so r/g/b are accepted but unused, same simplification
   draw_atari.c's own rgb_to_vdi_color makes explicit for its target. */

wp_status plat_draw_text(plat_gc *gc, plat_font *f, i32 x, i32 y,
                          const u8 *utf8, u32 len, u8 r, u8 g, u8 b)
{
    mac_gc *mgc = (mac_gc *)gc;
    mac_font *mf = (mac_font *)f;
    char buf[256];
    u32 n, out_len, unmapped;
    wp_status st;

    if (mgc == NULL || mf == NULL) return WP_ERR;

    n = (len < (u32)sizeof(buf)) ? len : (u32)sizeof(buf);
    st = plat_utf8_to_native(utf8, n, (u8 *)buf, (u32)sizeof(buf), &out_len, &unmapped);
    if (st != WP_OK) return st;

    SetPort(mgc->window);
    TextFont(mf->fontNum);
    TextSize(mf->size);
    TextFace((short)mf->style);
    MoveTo((short)x, (short)y);
    /* One bit: a light colour is white -- the text clears its pixels
       (srcBic), as white lettering on a dark bar needs -- else black.
       Brightness itself, as the image path below judges it: not
       plat_luma_percent, a fill's shade, which answers 0 for black and
       white alike and so drew all black text white. */
    if ((u32)r + (u32)g + (u32)b >= 384u) TextMode(srcBic);
    DrawText(buf, 0, (short)out_len);
    TextMode(srcOr);
    return WP_OK;
}

/* r,g,b -> nearest 1-bit QuickDraw pattern. PaintRect always fills with
   the port's CURRENT pen pattern (default black, set once by InitGraf
   and never otherwise touched by this file) -- a real bug, not a
   theoretical one, found by testing the help pager on-target: it asks
   for a WHITE background (0xFF,0xFF,0xFF, platform/common/help_view.c)
   to clear its viewport, which this function previously painted BLACK
   regardless, since it ignored r/g/b entirely and always used
   whatever pattern was already current. The main document body never
   surfaced this, since it clears its own background via a direct
   EraseRect call (always white, by QuickDraw's own contract) rather
   than through this seam. The pen is always restored to black
   afterward, since draw_document's own cursor-marker PaintRect call
   (and any future direct PaintRect caller) assumes that default.

   TWO tiers, thresholded at 384 -- deliberately the identical rule
   draw_atari.c's rgb_to_vdi_color uses for the ST's 2-tone palette,
   rather than a Mac-specific variant.

   There WAS a third tier here (qd.ltGray for the 96..224 band), added so
   the status line's and help pager's 0xC0 gray bars would "render
   distinctly from both pure black and pure white". It was removed
   because on a 1-bit screen there is no gray: 0xC0 became a dither, and
   BOTH of this project's only two 0xC0 callers draw BLACK TEXT ON TOP OF
   IT (platform/common/statusline.c:44, whose own comment asks for "a
   legible neutral background for black text", and help_view.c:310's
   selected-link highlight). Small black glyphs over a dither on 1-bit
   are mush -- verified on-target under BasiliskII, where the vi mode
   indicator and the ":"/"/" command line were unreadable without
   zooming to 500%. The tier's stated purpose was in direct conflict with
   its only two real uses.

   Dropping it did cost something, which is now paid for properly rather
   than waved away. The help pager's 0xC0 rect was the ONLY cue for which
   link is selected -- its "> " prefix was drawn on every link, not just
   the selected one, so white-filling that rect would have made selection
   invisible here. (Atari, thresholding at this same 384, had that bug
   all along: selection has never been visible on the ST.) So
   help_view.c now puts the selection in the TEXT -- "> " on the selected
   link, spaces on the rest -- which fixes both backends and leaves this
   seam free to do the simple, correct thing. */
static void select_fill_pattern(u8 r, u8 g, u8 b)
{
    u32 luminance = (u32)r + (u32)g + (u32)b;

    if (luminance < 384u) {
        PenPat(&qd.black);
    } else {
        PenPat(&qd.white);
    }
}

wp_status plat_fill_rect(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                          u8 r, u8 g, u8 b)
{
    mac_gc *mgc = (mac_gc *)gc;
    Rect rect;

    if (mgc == NULL) return WP_ERR;

    SetPort(mgc->window);
    SetRect(&rect, (short)x, (short)y, (short)(x + w), (short)(y + h));
    select_fill_pattern(r, g, b);
    PaintRect(&rect);
    PenPat(&qd.black);
    return WP_OK;
}

/* QuickDraw's own greys: light gray, gray and dark gray patterns, or
   black for the darkest. */
wp_status plat_fill_shade(plat_gc *gc, i32 x, i32 y, i32 w, i32 h, u8 percent)
{
    mac_gc *mgc = (mac_gc *)gc;
    Rect rect;

    if (mgc == NULL) return WP_ERR;
    if (w <= 0 || h <= 0 || percent == 0) return WP_OK;
    SetPort(mgc->window);
    SetRect(&rect, (short)x, (short)y, (short)(x + w), (short)(y + h));
    if (percent < 38) PenPat(&qd.ltGray);
    else if (percent < 63) PenPat(&qd.gray);
    else if (percent < 88) PenPat(&qd.dkGray);
    else PenPat(&qd.black);
    PenMode(patOr);
    PaintRect(&rect);
    PenMode(patCopy);
    PenPat(&qd.black);
    return WP_OK;
}

/* Vector drawing (platform.h): QuickDraw's own polygons, a grey in the
   pattern plat_fill_shade picks. Its pen hangs below and right of the
   line it draws, so lines wider than a pixel go as polygons, centred, as
   on every other machine. */
static void select_vector_pattern(u8 r, u8 g, u8 b)
{
    u8 grey = plat_luma_percent(r, g, b);   /* this backend is one-bit throughout */

    if (grey == 0) { select_fill_pattern(r, g, b); PenMode(patCopy); return; }
    if (grey < 38) PenPat(&qd.ltGray);
    else if (grey < 63) PenPat(&qd.gray);
    else if (grey < 88) PenPat(&qd.dkGray);
    else PenPat(&qd.black);
    PenMode(patOr);
}

wp_status plat_fill_polygon(plat_gc *gc, const i32 *xy, u16 n, u8 r, u8 g, u8 b)
{
    mac_gc *mgc = (mac_gc *)gc;
    PolyHandle poly;
    u16 i;

    if (mgc == NULL) return WP_ERR;
    if (n < 3) return WP_OK;
    SetPort(mgc->window);
    poly = OpenPoly();
    MoveTo((short)xy[0], (short)xy[1]);
    for (i = 1; i < n; i++) LineTo((short)xy[i * 2], (short)xy[i * 2 + 1]);
    LineTo((short)xy[0], (short)xy[1]);
    ClosePoly();
    if (poly == NULL) return WP_NOMEM;
    select_vector_pattern(r, g, b);
    PaintPoly(poly);
    KillPoly(poly);
    PenMode(patCopy);
    PenPat(&qd.black);
    return WP_OK;
}

wp_status plat_draw_polyline(plat_gc *gc, const i32 *xy, u16 n, i32 width,
                             u8 r, u8 g, u8 b)
{
    mac_gc *mgc = (mac_gc *)gc;
    u16 i;

    if (mgc == NULL) return WP_ERR;
    if (n < 2) return WP_OK;
    if (width > 1) return plat_polyline_by_polygons(gc, xy, n, width, r, g, b);
    SetPort(mgc->window);
    PenSize(1, 1);
    select_vector_pattern(r, g, b);
    MoveTo((short)xy[0], (short)xy[1]);
    for (i = 1; i < n; i++) LineTo((short)xy[i * 2], (short)xy[i * 2 + 1]);
    PenMode(patCopy);
    PenPat(&qd.black);
    return WP_OK;
}

/* M15 Phase 5b: real QuickDraw CopyBits blit, dithered down to this
   backend's hard-capped 1-bit tier (a plain NewWindow, not NewCWindow --
   true color is out of scope). Each pixel's palette RGB is reduced to
   black/white via a 4x4 ordered (Bayer) dither -- extending
   select_fill_pattern's own per-solid-fill luminance-threshold trick to
   run per pixel instead of a single flat threshold, so a multi-color
   source image still reads as recognizable shading rather than one flat
   gray. Builds a scratch, manually-populated 1-bit BitMap (mirrors
   metrics_mac.c's own ensure_metrics_port() precedent for constructing
   a BitMap by hand) and blits it via CopyBits -- WindowPtr IS a GrafPtr,
   so &mgc->window->portBits is directly usable as the destination
   BitMap*, no Carbon-only GetPortBitMapForCopyBits needed. */
static const u8 g_mac_bayer4x4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

wp_status plat_draw_image(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                           const u8 *pixels, i32 src_width_px, i32 src_height_px,
                           const u8 palette[][3], u8 palette_count)
{
    mac_gc *mgc = (mac_gc *)gc;
    BitMap src_bm;
    Rect src_rect, dst_rect;
    u32 row_bytes, buf_len, src_row_bytes;
    u8 *bits;
    i32 px, py;

    if (mgc == NULL || mgc->window == NULL || pixels == NULL) return WP_ERR;
    if (w <= 0 || h <= 0 || src_width_px <= 0 || src_height_px <= 0) return WP_ERR;
    if (palette_count < 1 || palette_count > 16) return WP_ERR;

    /* Word-aligned rowBytes -- a real QuickDraw 1-bit BitMap requirement,
       same "round up, no partial words" shape do_insert_image's own
       packed-4-bit row math already follows for a different bit depth. */
    row_bytes = (u32)(((src_width_px + 15) / 16) * 2);
    buf_len = row_bytes * (u32)src_height_px;
    bits = (u8 *)mem_alloc(buf_len);
    if (bits == NULL) return WP_NOMEM;
    memset(bits, 0, buf_len);

    src_row_bytes = ((u32)src_width_px + 1u) / 2u;
    for (py = 0; py < src_height_px; py++) {
        const u8 *src_row = pixels + (u32)py * src_row_bytes;
        u8 *dst_row = bits + (u32)py * row_bytes;

        for (px = 0; px < src_width_px; px++) {
            u8 byte = src_row[px / 2];
            u8 nibble = (u8)((px % 2 == 0) ? (byte >> 4) : (byte & 0x0F));
            u32 luminance_avg, threshold;

            if (nibble >= palette_count) nibble = 0;
            luminance_avg = ((u32)palette[nibble][0] + (u32)palette[nibble][1] +
                              (u32)palette[nibble][2]) / 3u;
            /* Standard 0-15 -> 0-255 Bayer scaling (val*16+8): keeps the
               max threshold at 248, strictly below a pure-white pixel's
               luminance of 255, so solid white regions never misfire
               black at any of the 16 dither cells. */
            threshold = (u32)g_mac_bayer4x4[py & 3][px & 3] * 16u + 8u;

            /* 1-bit QuickDraw convention: a set bit is BLACK. */
            if (luminance_avg <= threshold) {
                dst_row[px / 8] |= (u8)(0x80 >> (px % 8));
            }
        }
    }

    src_bm.baseAddr = (Ptr)bits;
    src_bm.rowBytes = (short)row_bytes;
    SetRect(&src_bm.bounds, 0, 0, (short)src_width_px, (short)src_height_px);

    SetPort(mgc->window);
    SetRect(&src_rect, 0, 0, (short)src_width_px, (short)src_height_px);
    SetRect(&dst_rect, (short)x, (short)y, (short)(x + w), (short)(y + h));

    CopyBits(&src_bm, &mgc->window->portBits, &src_rect, &dst_rect, srcCopy, NULL);

    mem_free(bits);
    return WP_OK;
}

/* One CopyBits per icon from a BitMap over the rows themselves:
   srcOr sets the black pixels, srcBic clears for white -- the 1-bit
   port has nothing between (select_fill_pattern's rule). CopyBits
   honours the window's clip region, so nothing to clip by hand. */
wp_status plat_draw_mono(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                         const u16 *rows, u32 row_words, u8 r, u8 g, u8 b)
{
    mac_gc *mgc = (mac_gc *)gc;
    BitMap src;
    Rect sr, dr;

    if (mgc == NULL || rows == NULL || row_words == 0) return WP_ERR;
    if (w > (i32)(row_words * 16u)) w = (i32)(row_words * 16u);
    if (w <= 0 || h <= 0) return WP_OK;

    src.baseAddr = (Ptr)rows;
    src.rowBytes = (short)(row_words * 2u);
    SetRect(&src.bounds, 0, 0, (short)(row_words * 16u), (short)h);
    SetRect(&sr, 0, 0, (short)w, (short)h);
    SetRect(&dr, (short)x, (short)y, (short)(x + w), (short)(y + h));
    SetPort(mgc->window);
    CopyBits(&src, &mgc->window->portBits, &sr, &dr,
             ((u32)r + (u32)g + (u32)b < 384u) ? srcOr : srcBic, NULL);
    return WP_OK;
}

void plat_gc_flush(plat_gc *gc)
{
    WP_UNUSED(gc);
}
