/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* No Scrap.h exists in Retro68's header set at all (unlike Files.h/
   Processes.h, which exist as thin wrappers) -- Memory.h resolves to
   the same master Multiverse.h header and already declares
   ZeroScrap/PutScrap/GetScrap, so it alone is enough. */
#include <Memory.h>
#include <string.h>
#include "platform.h"

/* Real classic Mac Scrap Manager (ZeroScrap/PutScrap/GetScrap), type
   'TEXT' -- the same plain-text scrap type every real Mac app of this
   era reads/writes, so this interoperates with real Mac software, not
   just itself (same bar clipboard_amiga.c's IFF FORM FTXT met for
   Amiga). GetScrap's contract: pass a zero-length Handle and it grows
   the handle to fit and returns the byte count of the requested type
   (or a negative OSErr if that type isn't on the scrap at all). */

#define MAC_CLIP_BUF 1024

wp_status plat_clipboard_set_text(const u8 *utf8, u32 len)
{
    u8 native[MAC_CLIP_BUF];
    u32 native_len, unmapped;
    wp_status st;

    st = plat_utf8_to_native(utf8, len, native, (u32)sizeof(native), &native_len, &unmapped);
    if (st != WP_OK) return st;

    if (ZeroScrap() != noErr) return WP_ERR;
    if (PutScrap((long)native_len, 'TEXT', (Ptr)native) != noErr) return WP_ERR;
    return WP_OK;
}

wp_status plat_clipboard_get_text(u8 *out, u32 out_cap, u32 *out_len)
{
    Handle h;
    long offset = 0;
    long got;
    u8 native[MAC_CLIP_BUF];
    wp_status st;

    if (out_len) *out_len = 0;
    h = NewHandle(0);
    if (h == NULL) return WP_ERR;

    got = GetScrap(h, 'TEXT', &offset);
    if (got < 0) { DisposeHandle(h); return WP_ERR; }
    if ((u32)got > sizeof(native)) got = (long)sizeof(native);

    HLock(h);
    memcpy(native, *h, (size_t)got);
    HUnlock(h);
    DisposeHandle(h);

    st = plat_native_to_utf8(native, (u32)got, out, out_cap, out_len);
    return st;
}

wp_bool plat_clipboard_has_text(void)
{
    Handle h;
    long offset = 0;
    long got;

    h = NewHandle(0);
    if (h == NULL) return WP_FALSE;
    got = GetScrap(h, 'TEXT', &offset);
    DisposeHandle(h);
    return (got >= 0) ? WP_TRUE : WP_FALSE;
}
