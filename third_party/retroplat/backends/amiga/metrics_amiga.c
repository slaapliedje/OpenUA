/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include <string.h>

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/text.h>
#include <graphics/rastport.h>
#include <graphics/displayinfo.h>
#include <intuition/screens.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/diskfont.h>
#include <diskfont/diskfont.h>

#include "platform.h"
#include "metrics_amiga_internal.h"

/* T9.2: real graphics.library/diskfont.library metrics, replacing the
   T9.1a WP_UNSUPPORTED stubs this file used to hold (those existed only
   so linebreak.o's wp_paragraph_relayout could link -- see the old
   comment's own explanation, now obsolete: relayout genuinely IS called
   now, by app_shell.c's relayout_document). */

static i32 g_dpi_x = 72;
static i32 g_dpi_y = 72;
static wp_bool g_dpi_queried = WP_FALSE;

/* The current public (Workbench) screen's own default font name, cached
   at first query -- used as the ta_Name for every plat_font_open request
   below (the engine never sets a real font name yet -- see linebreak.c's
   measure_and_height comment, "always requests the platform's default
   font", the same boundary metrics_atari.c respects). Falls back to the
   literal "topaz.font" (guaranteed ROM-resident on every real Amiga)
   if the screen or its Font pointer can't be queried. */
#define WP_DEFAULT_FONT_NAME_CAP 32
static char g_default_font_name[WP_DEFAULT_FONT_NAME_CAP] = "";

static void ensure_metrics_queried(void)
{
    struct Screen *screen;

    if (g_dpi_queried) return;
    g_dpi_queried = WP_TRUE; /* set first: a failed query below must not
                                 retry (and re-lock a screen) on every
                                 single call -- same defensive-caching
                                 shape as metrics_atari.c's g_metrics_vdi. */

    screen = LockPubScreen(NULL);
    if (screen == NULL) return;

    {
        LONG modeid = GetVPModeID(&screen->ViewPort);
        DisplayInfoHandle handle = FindDisplayInfo((ULONG)modeid);

        if (handle != NULL) {
            struct DisplayInfo di;

            memset(&di, 0, sizeof(di));
            if (GetDisplayInfoData(handle, (APTR)&di, (ULONG)sizeof(di),
                                    DTAG_DISP, (ULONG)modeid) != 0
                && di.Resolution.x > 0 && di.Resolution.y > 0) {
                /* DisplayInfo.Resolution is real, mode-derived
                   ticks-per-pixel per axis -- see
                   metrics_amiga_internal.h's own comment for why this,
                   not a hardcoded ratio, is used for X. Fewer ticks per
                   pixel on an axis means each pixel covers LESS physical
                   width there, i.e. a HIGHER dots-per-inch -- so X_DPI
                   scales inversely to X's own tick count, relative to Y
                   pinned at 72. */
                i32 x = (i32)(((i32)72 * (i32)di.Resolution.y) / (i32)di.Resolution.x);
                if (x > 0) g_dpi_x = x;
            }
        }
    }

    if (screen->Font != NULL && screen->Font->ta_Name != NULL) {
        u32 n = (u32)strlen((char *)screen->Font->ta_Name);
        if (n >= (u32)sizeof(g_default_font_name)) n = (u32)sizeof(g_default_font_name) - 1;
        memcpy(g_default_font_name, screen->Font->ta_Name, n);
        g_default_font_name[n] = '\0';
    }

    UnlockPubScreen(NULL, screen);
}

i32 amiga_twips_to_px_x(i32 twips)
{
    ensure_metrics_queried();
    return (twips * g_dpi_x) / 1440;
}

i32 amiga_twips_to_px_y(i32 twips)
{
    ensure_metrics_queried();
    return (twips * g_dpi_y) / 1440;
}

i32 amiga_px_to_twips_x(i32 px)
{
    ensure_metrics_queried();
    return (px * 1440) / g_dpi_x;
}

i32 amiga_px_to_twips_y(i32 px)
{
    ensure_metrics_queried();
    return (px * 1440) / g_dpi_y;
}

/* A lazily-initialized RastPort used ONLY for TextLength() measurement --
   never Text()/RectFill(). Verified on-target under amiberry (see
   docs/amiga-platform.md) that a bare InitRastPort() with no BitMap
   attached is NOT safe here, despite "TextLength only reads the font's
   own advance tables" being the commonly-cited reasoning for a
   bitmap-less measuring RastPort: real repeated redraws produced actual
   heap corruption (NUL bytes overwriting live wp_strpool content,
   visible as garbled glyphs), tracing back to this RastPort's
   rp_BitMap/rp_Layer being NULL/zeroed rather than pointing at anything
   real. A real, minimally-sized (1x1, depth 1) BitMap allocated via
   AllocBitMap -- the same graphics.library primitive any real Amiga
   program uses for an off-screen measuring surface -- removes the whole
   class of "passing graphics.library an incompletely-initialized
   structure" risk, not just the one call path that happened to crash
   first. Never freed: same process-lifetime-held-resource shape
   metrics_atari.c's own g_metrics_vdi already has (v_clsvwk is never
   called there either). */
static struct RastPort g_metrics_rp;
static struct BitMap *g_metrics_bitmap = NULL;
static wp_bool g_metrics_rp_ready = WP_FALSE;

/* Pre-V39 fallback storage for the measuring bitmap -- see
   ensure_metrics_rp. Process-lifetime, never freed, exactly like the
   AllocBitMap path above it. */
static struct BitMap g_metrics_bitmap_v37;

static void ensure_metrics_rp(void)
{
    if (g_metrics_rp_ready) return;
    g_metrics_rp_ready = WP_TRUE; /* set first -- see ensure_metrics_queried's
                                      own comment on why a failed attempt
                                      below must not retry every call */

    /* AllocBitMap is graphics.library V39 (Kickstart 3.0) -- verified
       against the NDK's own graphics_lib.sfd, where it appears after
       the "==version 39" marker, while InitBitMap/AllocRaster sit above
       "==version 36" and so exist on every Amiga.

       Kickstart 2.04/2.05 ship graphics.library V37, where calling
       AllocBitMap jumps past the end of the library's jump table --
       a wild jump, not a failed call, so it faults rather than
       returning NULL. That is the whole reason RetroWP could not run on
       those Kickstarts (T164): the very first paint calls relayout,
       which measures text, which lands here. Two separate lessons in
       one bug -- the failure was an illegal instruction at a garbage
       address, and the guard has to be a VERSION CHECK, because there
       is no return value to test. */
    if (GfxBase != NULL && GfxBase->LibNode.lib_Version >= 39) {
        g_metrics_bitmap = AllocBitMap(1, 1, 1, 0, NULL);
    } else {
        PLANEPTR plane = AllocRaster(1, 1);

        if (plane != NULL) {
            InitBitMap(&g_metrics_bitmap_v37, 1, 1, 1);
            g_metrics_bitmap_v37.Planes[0] = plane;
            g_metrics_bitmap = &g_metrics_bitmap_v37;
        }
    }
    if (g_metrics_bitmap == NULL) return;

    InitRastPort(&g_metrics_rp);
    g_metrics_rp.BitMap = g_metrics_bitmap;
}

static UWORD map_style(u16 style_flags)
{
    UWORD style = FS_NORMAL;

    if (style_flags & PLAT_STYLE_BOLD)      style |= FSF_BOLD;
    if (style_flags & PLAT_STYLE_ITALIC)    style |= FSF_ITALIC;
    if (style_flags & PLAT_STYLE_UNDERLINE) style |= FSF_UNDERLINED;
    /* PLAT_STYLE_STRIKE has no FS_* equivalent, and the document model's
       own SUPER/SUB never reach this seam -- not mapped, the same
       documented gap metrics_atari.c's map_effects already has for VDI's
       TXT_* effects. */
    return style;
}

/* The faces in memory and in FONTS:, one per .font name whatever its
   sizes, read once by AvailFonts the first time anything asks -- a
   floppy-only machine pays for the FONTS: scan only when a document
   names a face or the font menu is opened. Shown without ".font" and
   with a capital ("Times"), which is how a document names them; the
   file name is that plus ".font" again, lower-cased the way the Amiga's
   own fonts are spelled (OpenDiskFont ignores case regardless). */
#define AMIGA_MAX_FACES 32
#define AMIGA_FACE_NAME_CAP 32
static char g_faces[AMIGA_MAX_FACES][AMIGA_FACE_NAME_CAP];
static u32 g_face_count = 0;
static wp_bool g_faces_read = WP_FALSE;

static void read_faces(void)
{
    LONG size = 2048, more;
    UBYTE *buf = NULL;
    struct AvailFontsHeader *afh;
    struct AvailFonts *af;
    UWORD i;

    if (g_faces_read) return;
    g_faces_read = WP_TRUE;

    for (;;) {
        buf = (UBYTE *)mem_alloc((u32)size);
        if (buf == NULL) return;
        more = AvailFonts((struct AvailFontsHeader *)buf, size, AFF_MEMORY | AFF_DISK);
        if (more <= 0) break;
        mem_free(buf);
        size += more;
    }

    afh = (struct AvailFontsHeader *)buf;
    af = (struct AvailFonts *)(afh + 1);
    for (i = 0; i < afh->afh_NumEntries && g_face_count < AMIGA_MAX_FACES; i++) {
        const char *nm = (const char *)af[i].af_Attr.ta_Name;
        char face[AMIGA_FACE_NAME_CAP];
        u32 n = 0, k;
        wp_bool dup = WP_FALSE;

        if (nm == NULL) continue;
        while (nm[n] != '\0' && n + 1 < sizeof(face)) { face[n] = nm[n]; n++; }
        face[n] = '\0';
        if (n > 5 && strcmp(face + n - 5, ".font") == 0) face[n -= 5] = '\0';
        if (n == 0) continue;
        if (face[0] >= 'a' && face[0] <= 'z') face[0] = (char)(face[0] - 32);
        for (k = 0; k < g_face_count; k++) {
            if (strcmp(g_faces[k], face) == 0) { dup = WP_TRUE; break; }
        }
        if (!dup) memcpy(g_faces[g_face_count++], face, n + 1);
    }
    mem_free(buf);
}

static const char *face_name_at(u32 index, void *user)
{
    WP_UNUSED(user);
    return g_faces[index];
}

u32 plat_font_face_count(void)
{
    read_faces();
    return (g_face_count > 1) ? g_face_count : 0u;
}

wp_status plat_font_face_name(u32 index, u8 *utf8_out, u32 cap, u32 *len_out)
{
    u32 n;

    *len_out = 0;
    if (index >= plat_font_face_count()) return WP_RANGE;
    n = (u32)strlen(g_faces[index]);
    if (n > cap) n = cap;
    memcpy(utf8_out, g_faces[index], n);
    *len_out = n;
    return WP_OK;
}

wp_status plat_font_open(const u8 *utf8_font_name, u32 name_len,
                          i32 size_twips, u16 style_flags, plat_font **out)
{
    amiga_font *f;
    struct TextAttr ta;
    i32 px;
    char file[AMIGA_FACE_NAME_CAP + 6];

    ensure_metrics_queried();

    f = (amiga_font *)mem_alloc((u32)sizeof(amiga_font));
    if (f == NULL) return WP_NOMEM;

    px = (size_twips * g_dpi_y) / 1440;
    if (px < 1) px = 1;

    ta.ta_Name = (STRPTR)(g_default_font_name[0] != '\0' ? g_default_font_name : (char *)"topaz.font");
    if (name_len > 0 || (style_flags & PLAT_FAMILY_MASK) != 0) {
        i32 at;
        read_faces();
        at = plat_font_pick(face_name_at, NULL, g_face_count, utf8_font_name, name_len, style_flags);
        if (at >= 0) {
            u32 n = (u32)strlen(g_faces[at]), k;
            for (k = 0; k < n; k++) {
                char c = g_faces[at][k];
                file[k] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
            }
            memcpy(file + n, ".font", 6);
            ta.ta_Name = (STRPTR)file;
        }
    }
    ta.ta_YSize = (UWORD)px;
    ta.ta_Style = (UBYTE)map_style(style_flags);
    ta.ta_Flags = 0;

    f->tf = NULL;
    if (GfxBase->LibNode.lib_Version >= 39) {
        /* The screen's resolution each way (TA_DeviceDPI): an outline
           face (CGTimes and kin through diskfont) is then shaped for the
           screen's pixels -- on a 640x256 screen, whose pixels are twice
           as tall as wide, as wide as its type is, where by height alone
           it came out half as wide. A bitmap face is unaffected. From
           Kickstart 3.0 only: 2.04's and 2.05's diskfont answered the
           tagged request with a face whose words overran a narrow column
           (the layout and help self-checks failed under both). */
        struct TTextAttr tta;
        struct TagItem tags[2];
        tags[0].ti_Tag = TA_DeviceDPI;
        tags[0].ti_Data = ((ULONG)g_dpi_x << 16) | (ULONG)g_dpi_y;
        tags[1].ti_Tag = TAG_DONE;
        tags[1].ti_Data = 0;
        tta.tta_Name = ta.ta_Name;
        tta.tta_YSize = ta.ta_YSize;
        tta.tta_Style = (UBYTE)(ta.ta_Style | FSF_TAGGED);
        tta.tta_Flags = ta.ta_Flags;
        tta.tta_Tags = tags;
        f->tf = OpenDiskFont((struct TextAttr *)&tta);
    }
    if (f->tf == NULL) f->tf = OpenDiskFont(&ta);
    f->owns_font = (wp_bool)(f->tf != NULL ? WP_TRUE : WP_FALSE);

    if (f->tf == NULL) {
        /* OpenDiskFont failing at all would be a genuine surprise for a
           topaz.font request (always ROM-resident on real hardware) --
           fall back to the exec-guaranteed-non-NULL system default
           rather than failing the whole request. Not owned: never
           CloseFont a font this code didn't open. */
        f->tf = GfxBase->DefaultFont;
        f->owns_font = WP_FALSE;
    }
    plat_paper_face_init(&f->paper, utf8_font_name, name_len, size_twips, style_flags);

    *out = (plat_font *)f;
    return WP_OK;
}

void plat_font_close(plat_font *f)
{
    amiga_font *af = (amiga_font *)f;

    if (af == NULL) return;
    if (af->owns_font && af->tf != NULL) CloseFont(af->tf);
    mem_free(af);
}

wp_status plat_font_metrics_get(plat_font *f, plat_font_metrics *out)
{
    amiga_font *af = (amiga_font *)f;

    if (af == NULL || af->tf == NULL || out == NULL) return WP_ERR;
    if (plat_metrics_paper()) { plat_paper_metrics(&af->paper, out); return WP_OK; }

    /* tf_Baseline: distance from the top of the character cell to the
       baseline, in pixels (real TextFont field, graphics/text.h) --
       ascent. tf_YSize - tf_Baseline is the remainder below the
       baseline -- descent. No separate leading field on TextFont, same
       leading=0 choice metrics_atari.c makes for VDI's own font info. */
    out->ascent = amiga_px_to_twips_y((i32)af->tf->tf_Baseline);
    out->descent = amiga_px_to_twips_y((i32)af->tf->tf_YSize - (i32)af->tf->tf_Baseline);
    if (out->descent < 0) out->descent = 0;
    out->leading = 0;
    return WP_OK;
}

wp_status plat_measure_text(plat_font *f, const u8 *utf8, u32 len,
                             i32 *width_twips_out)
{
    amiga_font *af = (amiga_font *)f;
    char buf[256];
    u32 n, out_len, unmapped;
    wp_status st;
    WORD px_width;

    if (af == NULL || af->tf == NULL || width_twips_out == NULL) return WP_ERR;
    if (plat_metrics_paper()) return plat_paper_measure(&af->paper, utf8, len, width_twips_out);

    ensure_metrics_rp();
    if (g_metrics_bitmap == NULL) return WP_ERR;

    n = (len < sizeof(buf)) ? len : (u32)sizeof(buf);
    /* Same real Latin-1 mapping draw_amiga.c's plat_utf8_to_native
       already implements -- reused here so measurement and drawing
       agree on exactly what bytes are being sized/rendered, the same
       consistency metrics_atari.c's own comment calls out. */
    st = plat_utf8_to_native(utf8, n, (u8 *)buf, (u32)sizeof(buf), &out_len, &unmapped);
    if (st != WP_OK) return st;

    if (out_len == 0) {
        *width_twips_out = 0;
        return WP_OK;
    }

    SetFont(&g_metrics_rp, af->tf);
    px_width = TextLength(&g_metrics_rp, (CONST_STRPTR)buf, (ULONG)out_len);

    *width_twips_out = amiga_px_to_twips_x((i32)px_width);
    return WP_OK;
}
