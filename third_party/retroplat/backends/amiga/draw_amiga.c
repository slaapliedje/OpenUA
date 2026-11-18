/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>

#include <graphics/rastport.h>
#include <graphics/text.h>
#include <graphics/gfx.h>
#include <graphics/view.h>
#include <intuition/screens.h>
#include <utility/tagitem.h>
#include <proto/graphics.h>

#include "platform.h"
#include "utf8.h"
#include "metrics_amiga_internal.h"

/* T9.1a implemented only the encoding edge (plat_utf8_to_native/
   plat_native_to_utf8) -- clipboard_amiga.c needed it to link at all, so
   it couldn't wait for T9.2's real glyph-draw work. T9.2 (this revisit)
   adds plat_draw_text/plat_fill_rect/plat_gc_flush below, RastPort-based
   exactly like draw_atari.c is VDI-based, completing the pair per the
   M9 plan's architecture table.

   Unlike the Atari backend's ASCII-only '?' fallback for every codepoint
   above 0x7F (a documented, deliberate gap -- see docs/atari-platform.md),
   Amiga's native charset genuinely is ISO-8859-1 (Latin-1) from
   Kickstart 2.0's topaz.font onward -- this project's Amiga floor is
   already 2.04+ for MUI (see docs/amiga-platform.md), so codepoints
   0x80-0xFF map identically to native bytes 0x80-0xFF, no lookup table
   needed. Only codepoints above 0xFF (genuinely outside Latin-1) fall
   back to '?'. */

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

        if (cp <= 0xFF) {
            out[written++] = (u8)cp;
        } else {
            u8 f = utf8_ascii_fallback(cp);   /* the nearest plain character, if any */
            out[written++] = f != 0 ? f : (u8)'?';
            if (f == 0) unmapped++;
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

    for (i = 0; i < len; i++) {
        u8 tmp[4];
        u32 n = utf8_encode((u32)native[i], tmp);
        u32 j;

        if (written + n > out_cap) break;
        for (j = 0; j < n; j++) out[written++] = tmp[j];
    }

    if (out_len) *out_len = written;
    return WP_OK;
}

/* r,g,b -> a real MUI pen index. Pens are screen-relative palette slots,
   not direct RGB, so (like Atari's rgb_to_vdi_color choosing between the
   low-res palette's two fixed G_BLACK/G_WHITE entries) this maps to
   whichever of the window's own DrawInfo TEXTPEN/BACKGROUNDPEN pens is
   closer by the same luminance threshold -- callers (app_shell.c) have
   only ever asked for black text on the theme's normal background so
   far, same scope note draw_atari.c's own version carries. */
/*
   Pure white is the theme's SHINEPEN -- paper is white, as in every
   Amiga word processor, where the Workbench's grey BACKGROUNDPEN made
   the page grey -- and pure black its TEXTPEN. Anything else, on a
   screen of 16 colors or more under graphics.library V39+, is the
   nearest pen the screen already has (FindColor: nothing allocated,
   nothing to release), which is what lets a grey be grey. Shallower
   screens, and Kickstart 2.x, choose among the theme's own pens by
   brightness: the dark ones the text pen, the middle the background
   grey, the light ones white -- which is the Workbench's own 3D look
   when the face is a mid grey, its highlight white and its shadow
   dark. */
static UBYTE pen_by_brightness(struct DrawInfo *dri, u32 luminance)
{
    if (luminance < 400) return (UBYTE)dri->dri_Pens[TEXTPEN];
    if (luminance <= 650) return (UBYTE)dri->dri_Pens[BACKGROUNDPEN];
    return (UBYTE)dri->dri_Pens[SHINEPEN];
}

static UBYTE rgb_to_pen_on(const amiga_gc *agc, u8 r, u8 g, u8 b)
{
    struct DrawInfo *dri = agc->dri;
    u32 luminance = (u32)r + (u32)g + (u32)b;

    if (luminance == 765) return (UBYTE)dri->dri_Pens[SHINEPEN];
    if (luminance != 0 && agc->screen != NULL &&
        agc->screen->BitMap.Depth >= 4 && GfxBase->LibNode.lib_Version >= 39) {
        LONG pen = FindColor(agc->screen->ViewPort.ColorMap,
                             (ULONG)r * 0x01010101UL, (ULONG)g * 0x01010101UL,
                             (ULONG)b * 0x01010101UL, -1);
        if (pen >= 0) return (UBYTE)pen;
    }
    return pen_by_brightness(dri, luminance);
}

/* Text: dark text in the text pen, light text (white lettering on a dark
   bar) in white. */
static UBYTE rgb_to_pen(struct DrawInfo *dri, u8 r, u8 g, u8 b)
{
    u32 luminance = (u32)r + (u32)g + (u32)b;
    return (UBYTE)dri->dri_Pens[(luminance < 384) ? TEXTPEN : SHINEPEN];
}

wp_status plat_draw_text(plat_gc *gc, plat_font *f, i32 x, i32 y,
                          const u8 *utf8, u32 len, u8 r, u8 g, u8 b)
{
    amiga_gc *agc = (amiga_gc *)gc;
    amiga_font *af = (amiga_font *)f;
    char buf[256];
    u32 n, out_len, unmapped;
    wp_status st;

    if (agc == NULL || af == NULL || af->tf == NULL) return WP_ERR;

    n = (len < sizeof(buf)) ? len : (u32)sizeof(buf);
    st = plat_utf8_to_native(utf8, n, (u8 *)buf, (u32)sizeof(buf), &out_len, &unmapped);
    if (st != WP_OK) return st;
    if (out_len == 0) return WP_OK;

    /* Font attributes are re-selected here (unlike draw_atari.c, which
       leaves this to the caller's own VDI-attribute state) because a
       RastPort's current font is per-RastPort state that app_shell.c's
       caller doesn't otherwise track between lines of potentially
       different styles -- see paint_document's cached_style pattern,
       mirrored by app_shell.c's own painting loop for this backend. */
    SetFont(agc->rp, af->tf);
    SetAPen(agc->rp, rgb_to_pen(agc->dri, r, g, b));
    SetBPen(agc->rp, (UBYTE)agc->dri->dri_Pens[BACKGROUNDPEN]);
    /* JAM1: the glyphs only. JAM2 filled each character's background,
       punching holes in a paragraph's shading; MUI clears the object's
       background before every full redraw, so nothing is left behind. */
    SetDrMd(agc->rp, JAM1);
    Move(agc->rp, (LONG)x, (LONG)y);
    Text(agc->rp, (STRPTR)buf, (ULONG)out_len);
    return WP_OK;
}

wp_status plat_fill_rect(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                          u8 r, u8 g, u8 b)
{
    amiga_gc *agc = (amiga_gc *)gc;

    if (agc == NULL) return WP_ERR;

    SetAPen(agc->rp, rgb_to_pen_on(agc, r, g, b));
    RectFill(agc->rp, (LONG)x, (LONG)y, (LONG)(x + w - 1), (LONG)(y + h - 1));
    return WP_OK;
}

/* Shading as an area pattern in the text pen over what is there (JAM1):
   Workbench's four pens have no light grey, and the nearest pen to one
   is white. Four rows of dots, about 1/8, 1/4, 1/2 and 3/4 coverage. */
static UWORD g_shade_pat[4][4] = {
    { 0x8888, 0x0000, 0x2222, 0x0000 },
    { 0x8888, 0x2222, 0x8888, 0x2222 },
    { 0x5555, 0xAAAA, 0x5555, 0xAAAA },
    { 0x7777, 0xDDDD, 0x7777, 0xDDDD }
};

wp_status plat_fill_shade(plat_gc *gc, i32 x, i32 y, i32 w, i32 h, u8 percent)
{
    amiga_gc *agc = (amiga_gc *)gc;
    u32 k;

    if (agc == NULL) return WP_ERR;
    if (w <= 0 || h <= 0 || percent == 0) return WP_OK;
    k = percent < 19 ? 0u : percent < 38 ? 1u : percent < 63 ? 2u : 3u;
    SetAPen(agc->rp, rgb_to_pen_on(agc, 0, 0, 0));
    SetDrMd(agc->rp, JAM1);
    agc->rp->AreaPtrn = g_shade_pat[k];
    agc->rp->AreaPtSz = 2;            /* 2^2 = 4 rows */
    RectFill(agc->rp, (LONG)x, (LONG)y, (LONG)(x + w - 1), (LONG)(y + h - 1));
    agc->rp->AreaPtrn = NULL;
    agc->rp->AreaPtSz = 0;
    return WP_OK;
}

/* Vector drawing (platform.h). Area fills need an AreaInfo and a TmpRas
   the size of the screen in chip memory; spans through RectFill need
   neither, and the shading pattern lines up from row to row because it
   follows y. graphics.library has no wide lines: those go as polygons. */
wp_status plat_fill_polygon(plat_gc *gc, const i32 *xy, u16 n, u8 r, u8 g, u8 b)
{
    amiga_gc *agc = (amiga_gc *)gc;
    /* Workbench's four pens (or two): every mid colour a pattern by its
       brightness; a screen of 16 or more finds a real pen for a colour. */
    wp_bool few = (wp_bool)(agc == NULL || agc->screen == NULL || agc->screen->BitMap.Depth < 4 ||
                            GfxBase->LibNode.lib_Version < 39);
    return plat_polygon_by_spans(gc, xy, n, r, g, b,
                                 few ? plat_luma_percent(r, g, b) : plat_grey_percent(r, g, b));
}

wp_status plat_draw_polyline(plat_gc *gc, const i32 *xy, u16 n, i32 width,
                             u8 r, u8 g, u8 b)
{
    amiga_gc *agc = (amiga_gc *)gc;
    u16 i;

    if (agc == NULL) return WP_ERR;
    if (n < 2) return WP_OK;
    if (width > 1) return plat_polyline_by_polygons(gc, xy, n, width, r, g, b);
    SetAPen(agc->rp, rgb_to_pen_on(agc, r, g, b));
    SetDrMd(agc->rp, JAM1);
    Move(agc->rp, (LONG)xy[0], (LONG)xy[1]);
    for (i = 1; i < n; i++) Draw(agc->rp, (LONG)xy[i * 2], (LONG)xy[i * 2 + 1]);
    return WP_OK;
}

/* No blit worth having here: plat_draw_mono run by run. */
wp_status plat_draw_mono(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                         const u16 *rows, u32 row_words, u8 r, u8 g, u8 b)
{
    return plat_mono_by_runs(gc, x, y, w, h, rows, row_words, r, g, b);
}

void plat_gc_flush(plat_gc *gc)
{
    WP_UNUSED(gc);
}

/* M15 T15.6b: real graphics.library indexed-bitmap blit. WritePixelArray8
   writes raw screen PEN NUMBERS, not RGB or local palette indices (it does
   no color translation of its own) -- so the image's own <=16-entry
   palette must first be resolved into real pens on the REAL, runtime-
   queried screen (agc->screen, never fixed at compile time -- M9's own
   finding, see amiga_screen_color_count()) via ObtainBestPen, and every
   packed-nibble pixel remapped through that table before the call.
   WritePixelArray8 also requires a temporary scratch RastPort matching the
   destination's own depth (a real, mandatory parameter, not optional) --
   built once here via AllocBitMap/InitRastPort and freed afterward.
   Nearest-neighbor samples from src_width_px/src_height_px into the
   requested w x h box -- in practice these are always equal in this
   milestone (display_width_twips is derived directly from width_px with
   no additional scaling), but a caller passing a slightly different w/h
   after twips<->px rounding still gets a correct, if approximate, result,
   the same allowance plat_measure_text's own callers already have. */
wp_status plat_draw_image(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                           const u8 *pixels, i32 src_width_px, i32 src_height_px,
                           const u8 palette[][3], u8 palette_count)
{
    amiga_gc *agc = (amiga_gc *)gc;
    struct ColorMap *cm;
    LONG pen_map[16];
    UBYTE *array;
    struct RastPort temprp;
    struct BitMap *temp_bm;
    ULONG depth;
    i32 out_x, out_y;
    u8 i;
    wp_status result = WP_OK;

    if (agc == NULL || agc->screen == NULL || pixels == NULL) return WP_ERR;
    if (w <= 0 || h <= 0 || src_width_px <= 0 || src_height_px <= 0) return WP_ERR;
    if (palette_count < 1 || palette_count > 16) return WP_ERR;

    cm = agc->screen->ViewPort.ColorMap;

    for (i = 0; i < palette_count; i++) {
        ULONG r32 = ((ULONG)palette[i][0] << 24) | ((ULONG)palette[i][0] << 16) |
                    ((ULONG)palette[i][0] << 8) | (ULONG)palette[i][0];
        ULONG g32 = ((ULONG)palette[i][1] << 24) | ((ULONG)palette[i][1] << 16) |
                    ((ULONG)palette[i][1] << 8) | (ULONG)palette[i][1];
        ULONG b32 = ((ULONG)palette[i][2] << 24) | ((ULONG)palette[i][2] << 16) |
                    ((ULONG)palette[i][2] << 8) | (ULONG)palette[i][2];

        pen_map[i] = ObtainBestPen(cm, r32, g32, b32, TAG_DONE);
        if (pen_map[i] < 0) {
            u8 j;
            for (j = 0; j < i; j++) ReleasePen(cm, pen_map[j]);
            return WP_ERR;
        }
    }

    array = (UBYTE *)mem_alloc((u32)w * (u32)h);
    if (array == NULL) {
        for (i = 0; i < palette_count; i++) ReleasePen(cm, pen_map[i]);
        return WP_NOMEM;
    }

    for (out_y = 0; out_y < h; out_y++) {
        i32 src_y = (out_y * src_height_px) / h;
        u32 src_row_bytes = ((u32)src_width_px + 1u) / 2u;
        const u8 *src_row = pixels + (u32)src_y * src_row_bytes;

        for (out_x = 0; out_x < w; out_x++) {
            i32 src_x = (out_x * src_width_px) / w;
            u8 byte = src_row[src_x / 2];
            u8 nibble = (u8)((src_x % 2 == 0) ? (byte >> 4) : (byte & 0x0F));

            if (nibble >= palette_count) nibble = 0;
            array[out_y * w + out_x] = (UBYTE)pen_map[nibble];
        }
    }

    depth = GetBitMapAttr(agc->rp->BitMap, BMA_DEPTH);
    temp_bm = AllocBitMap((ULONG)w, 1, depth, 0, agc->rp->BitMap);
    if (temp_bm == NULL) {
        mem_free(array);
        for (i = 0; i < palette_count; i++) ReleasePen(cm, pen_map[i]);
        return WP_NOMEM;
    }
    InitRastPort(&temprp);
    temprp.BitMap = temp_bm;

    /* WritePixelArray8 does NOT return a status code -- per the Amiga ROM
       Kernel Reference Manual it returns the actual pixel count copied,
       which should equal (xstop-xstart+1)*(ystop-ystart+1) == w*h on a
       full success. A prior version of this code checked "!= 0", which
       misread every successful call (any nonzero pixel count) as a
       failure. */
    if ((u32)WritePixelArray8(agc->rp, (ULONG)x, (ULONG)y, (ULONG)(x + w - 1),
                               (ULONG)(y + h - 1), array, &temprp) !=
        (u32)w * (u32)h) {
        result = WP_ERR;
    }

    FreeBitMap(temp_bm);
    mem_free(array);
    for (i = 0; i < palette_count; i++) ReleasePen(cm, pen_map[i]);

    return result;
}
