/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include <i86.h>
#include "platform.h"
#include "utf8.h"
#include "metrics_dos_internal.h"

/* No real DOS-code-page (CP437) mapping table yet -- ASCII (0x00-0x7F)
   passes through unchanged, and every other codepoint becomes a single
   '?' fallback byte, counted in *unmapped_count. The same honest,
   already-accepted "ASCII-first, extended-range best-effort" scope
   platform/atari/draw_atari.c's own identical comment documents for
   its own high-ASCII gap -- a real CP437 table is a documented future
   improvement here too, not a silent one. */
/* Code page 437's upper half, 0x80-0xFF, as Unicode: the PC's accented
   letters, box-drawing characters and symbols. */
static const u16 g_cp437_hi[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
};

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
            for (k = 0; k < 128 && g_cp437_hi[k] != cp; k++) {}
            if (k < 128) {
                out[written++] = (u8)(0x80 + k);
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
        u8 seq[4];
        u32 n, k;

        if (native[i] <= 0x7F) {
            out[written++] = native[i];
            continue;
        }
        n = utf8_encode(g_cp437_hi[native[i] - 0x80], seq);
        if (written + n > out_cap) break;
        for (k = 0; k < n; k++) out[written++] = seq[k];
    }

    if (out_len) *out_len = written;
    return WP_OK;
}

/* r,g,b -> nearest of the 16 real, standard CGA/EGA/VGA text-mode
   colors (squared RGB distance) -- unlike the monochrome ST/Mac
   backends' own luminance-threshold approximations, DOS text mode
   genuinely has this many real colors available, so a real nearest-
   match is both possible and worthwhile here. */
static const u8 dos_palette[16][3] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
    {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
    {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
    {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF}
};

static u8 rgb_to_dos_color(u8 r, u8 g, u8 b)
{
    u32 best_dist = 0xFFFFFFFFul;
    u8 best = 0;
    u8 i;

    for (i = 0; i < 16; i++) {
        i32 dr = (i32)r - (i32)dos_palette[i][0];
        i32 dg = (i32)g - (i32)dos_palette[i][1];
        i32 db = (i32)b - (i32)dos_palette[i][2];
        u32 dist = (u32)(dr * dr + dg * dg + db * db);

        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

static unsigned char __far *dos_video_mem(void)
{
    return (unsigned char __far *)MK_FP(0xB800, 0);
}

static void dos_put_cell(i32 col, i32 row, u8 ch, u8 attr)
{
    unsigned char __far *vmem = dos_video_mem();
    i32 offset;

    if (col < 0 || col >= DOS_SCREEN_COLS || row < 0 || row >= DOS_SCREEN_ROWS) return;
    offset = (row * DOS_SCREEN_COLS + col) * 2;
    vmem[offset] = ch;
    vmem[offset + 1] = attr;
}

/* Reads back a cell's CURRENT attribute byte -- plat_draw_text uses
   just the background nibble from this (see its own comment) so text
   drawn over an already-painted background (plat_fill_rect's own
   status-line bar, a highlighted line) doesn't stomp it to black. */
static u8 dos_get_cell_attr(i32 col, i32 row)
{
    unsigned char __far *vmem = dos_video_mem();
    i32 offset;

    if (col < 0 || col >= DOS_SCREEN_COLS || row < 0 || row >= DOS_SCREEN_ROWS) return 0x00;
    offset = (row * DOS_SCREEN_COLS + col) * 2;
    return vmem[offset + 1];
}

/* gc is unused -- there is exactly one screen and one fixed B800
   segment on this backend, no per-window/per-context state to carry
   (see platform/dos/app_shell_internal.h's own note on plat_window_
   create). */
wp_status plat_draw_text(plat_gc *gc, plat_font *f, i32 x, i32 y,
                          const u8 *utf8, u32 len, u8 r, u8 g, u8 b)
{
    dos_font *df = (dos_font *)f;
    u8 native[256];
    u32 out_len, unmapped;
    wp_status st;
    u8 fg;
    u8 attr;
    u32 i;

    WP_UNUSED(gc);
    if (df == NULL || utf8 == NULL) return WP_ERR;

    st = plat_utf8_to_native(utf8, len, native, (u32)sizeof(native), &out_len, &unmapped);
    if (st != WP_OK) return st;

    fg = rgb_to_dos_color(r, g, b);
    if (df->bold) fg |= 0x08;  /* intensity bit -- the standard DOS-text-mode "bold" convention */

    /* Preserves whatever background is already at each cell (a plain
       black document-body cell, or a status-line bar's own fill --
       see dos_get_cell_attr's own comment) instead of forcing black --
       a real bug found and fixed during Phase 5 visual verification:
       status-line text was rendering as solid black-on-black (its own
       PREVIOUSLY-drawn light-gray fill silently overwritten by a hard-
       coded black background here) and so was completely invisible. */
    for (i = 0; i < out_len; i++) {
        i32 col = x + (i32)i;
        u8 existing_bg = (u8)(dos_get_cell_attr(col, y) & 0xF0);

        attr = (u8)(existing_bg | (fg & 0x0F));
        dos_put_cell(col, y, native[i], attr);
    }
    return WP_OK;
}

/* Shading in text mode: a blue background. The screen's text is light
   grey, and a grey background of any useful shade would hide it; text
   drawn after keeps the background (plat_draw_text). */
wp_status plat_fill_shade(plat_gc *gc, i32 x, i32 y, i32 w, i32 h, u8 percent)
{
    i32 row, col;

    WP_UNUSED(gc);
    if (percent == 0) return WP_OK;
    for (row = y; row < y + h; row++) {
        for (col = x; col < x + w; col++) dos_put_cell(col, row, ' ', (u8)(0x01 << 4));
    }
    return WP_OK;
}

/* Vector drawing (platform.h): text mode has nothing to draw a line
   with; RetroWP shows a drawing as a labelled placeholder, as it does a
   picture. */
wp_status plat_fill_polygon(plat_gc *gc, const i32 *xy, u16 n, u8 r, u8 g, u8 b)
{
    WP_UNUSED(gc); WP_UNUSED(xy); WP_UNUSED(n); WP_UNUSED(r); WP_UNUSED(g); WP_UNUSED(b);
    return WP_OK;
}

wp_status plat_draw_polyline(plat_gc *gc, const i32 *xy, u16 n, i32 width,
                             u8 r, u8 g, u8 b)
{
    WP_UNUSED(gc); WP_UNUSED(xy); WP_UNUSED(n); WP_UNUSED(width);
    WP_UNUSED(r); WP_UNUSED(g); WP_UNUSED(b);
    return WP_OK;
}

wp_status plat_fill_rect(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                          u8 r, u8 g, u8 b)
{
    u8 bg = rgb_to_dos_color(r, g, b);
    u8 attr = (u8)(bg << 4);  /* black foreground on the requested background */
    i32 row, col;

    WP_UNUSED(gc);

    for (row = y; row < y + h; row++) {
        for (col = x; col < x + w; col++) {
            dos_put_cell(col, row, ' ', attr);
        }
    }
    return WP_OK;
}

/* Direct video-memory writes are visible the instant they happen --
   nothing to flush. */
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

/* There is exactly one screen and no window manager -- plat_window_
   create just returns a placeholder handle so callers have something
   non-NULL to hold and eventually destroy, mirroring every other
   backend's plat_window contract without pretending DOS has real
   multi-window state to track. Deliberately does NOT set video mode
   itself (ignoring the requested title/w/h, which have no real
   DOS-text-mode equivalent either): a real bug, found directly during
   Phase 2 visual verification, not assumed -- BIOS INT 10h's video-
   mode-set service always blanks the display as a side effect, even
   when re-requesting the mode already active, so a plat_window_create
   call made AFTER app_shell.c's own startup set_text_mode() (e.g. this
   backend's render_fixture_paragraph, which creates its own window
   partway through a render) was wiping out everything drawn so far --
   the title bar included. Video mode is set exactly once, at startup,
   by whoever owns that sequence; plat_window_create must never repeat
   it. */
typedef struct {
    int dummy;
} dos_window;

wp_status plat_window_create(const u8 *title_utf8, u32 title_len,
                              i32 w, i32 h, plat_window **out)
{
    dos_window *win;

    WP_UNUSED(title_utf8);
    WP_UNUSED(title_len);
    WP_UNUSED(w);
    WP_UNUSED(h);

    if (out == NULL) return WP_ERR;

    win = (dos_window *)mem_alloc((u32)sizeof(dos_window));
    if (win == NULL) return WP_NOMEM;

    *out = (plat_window *)win;
    return WP_OK;
}

void plat_window_destroy(plat_window *w)
{
    mem_free(w);
}

wp_status plat_window_get_gc(plat_window *w, plat_gc **out)
{
    if (w == NULL || out == NULL) return WP_ERR;
    *out = (plat_gc *)w;  /* no real per-context state needed -- see above */
    return WP_OK;
}
