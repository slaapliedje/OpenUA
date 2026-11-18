/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <exec/types.h>
#include <libraries/iffparse.h>
#include <datatypes/textclass.h>
#include <proto/iffparse.h>
#include "platform.h"

/* Real Amiga clipboard: a typed IFF stream over clipboard.device (unit 0),
   written/read through iffparse.library -- NOT a port of the Atari
   backend's SCRAP.TXT file convention (see docs/amiga-platform.md). Plain
   text on the Amiga clipboard is a FORM FTXT containing one CHRS chunk
   (the same shape any real Amiga text editor reads/writes), so this
   round-trips with other real Amiga software, not just itself.

   iff_Stream is documented (libraries/iffparse.h) as "a value used by the
   client's read/write/seek functions... could even be a pointer or a
   BPTR" -- InitIFFasClip()'s convention is to stash the ClipboardHandle*
   there before calling it, which wires up iffparse's stream hooks to
   read/write clipboard.device directly. */

static wp_status open_clip_iff(struct IFFHandle **iff_out,
                                struct ClipboardHandle **clip_out)
{
    struct IFFHandle *iff;
    struct ClipboardHandle *clip;

    iff = AllocIFF();
    if (iff == NULL) return WP_ERR;

    clip = OpenClipboard(0);
    if (clip == NULL) { FreeIFF(iff); return WP_ERR; }

    iff->iff_Stream = (ULONG)clip;
    InitIFFasClip(iff);

    *iff_out = iff;
    *clip_out = clip;
    return WP_OK;
}

static void close_clip_iff(struct IFFHandle *iff, struct ClipboardHandle *clip)
{
    CloseClipboard(clip);
    FreeIFF(iff);
}

/* Shared by plat_clipboard_get_text and plat_clipboard_has_text: opens the
   clipboard for reading and scans for a FORM FTXT / CHRS chunk. On success
   (WP_OK), the IFF stream is left open and positioned at the CHRS chunk
   (CurrentChunk(iff) valid, ready for ReadChunkBytes) -- caller is
   responsible for CloseIFF(iff) either way. */
static wp_status find_ftxt_chrs(struct IFFHandle *iff)
{
    if (OpenIFF(iff, IFFF_READ) != 0) return WP_ERR;
    if (StopChunk(iff, ID_FTXT, ID_CHRS) != 0) { CloseIFF(iff); return WP_ERR; }
    if (ParseIFF(iff, IFFPARSE_SCAN) != 0) { CloseIFF(iff); return WP_ERR; }
    return WP_OK;
}

wp_status plat_clipboard_set_text(const u8 *utf8, u32 len)
{
    u8 native[1024];
    u32 native_len, unmapped;
    wp_status st;
    struct IFFHandle *iff;
    struct ClipboardHandle *clip;
    wp_status result = WP_ERR;

    st = plat_utf8_to_native(utf8, len, native, (u32)sizeof(native), &native_len, &unmapped);
    if (st != WP_OK) return st;

    if (open_clip_iff(&iff, &clip) != WP_OK) return WP_ERR;

    if (OpenIFF(iff, IFFF_WRITE) == 0) {
        if (PushChunk(iff, ID_FTXT, ID_FORM, IFFSIZE_UNKNOWN) == 0) {
            if (PushChunk(iff, 0, ID_CHRS, IFFSIZE_UNKNOWN) == 0) {
                if (WriteChunkBytes(iff, native, (LONG)native_len) == (LONG)native_len) {
                    result = WP_OK;
                }
                if (PopChunk(iff) != 0) result = WP_ERR;
            }
            if (PopChunk(iff) != 0) result = WP_ERR;
        }
        CloseIFF(iff);
    }

    close_clip_iff(iff, clip);
    return result;
}

wp_status plat_clipboard_get_text(u8 *out, u32 out_cap, u32 *out_len)
{
    struct IFFHandle *iff;
    struct ClipboardHandle *clip;
    struct ContextNode *cn;
    u8 native[1024];
    LONG want, got;
    wp_status st = WP_ERR;

    if (out_len) *out_len = 0;
    if (open_clip_iff(&iff, &clip) != WP_OK) return WP_ERR;

    if (find_ftxt_chrs(iff) == WP_OK) {
        cn = CurrentChunk(iff);
        want = (cn != NULL) ? cn->cn_Size : 0;
        if (want < 0) want = 0;
        if ((u32)want > sizeof(native)) want = (LONG)sizeof(native);

        got = ReadChunkBytes(iff, native, want);
        if (got >= 0) {
            st = plat_native_to_utf8(native, (u32)got, out, out_cap, out_len);
        }
        CloseIFF(iff);
    }

    close_clip_iff(iff, clip);
    return st;
}

wp_bool plat_clipboard_has_text(void)
{
    struct IFFHandle *iff;
    struct ClipboardHandle *clip;
    wp_bool has;

    if (open_clip_iff(&iff, &clip) != WP_OK) return WP_FALSE;

    has = (find_ftxt_chrs(iff) == WP_OK) ? WP_TRUE : WP_FALSE;
    if (has) CloseIFF(iff);

    close_clip_iff(iff, clip);
    return has;
}
