/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <windows.h>

#include "metrics_win32_internal.h"
#include "platform.h"

/* A REAL clipboard, unlike the host backend's in-process buffer -- text put
   here is readable by Notepad and everything else on the desktop. That is
   the same choice the Atari, Amiga and Mac backends make (GEM's scrap
   directory, the Amiga clipboard.device, the Mac scrap manager); the host
   one is a test fixture and borrowing it on a real platform would quietly
   turn cut-and-paste into a private variable.

   CF_UNICODETEXT rather than CF_TEXT: this seam's strings are UTF-8, and
   going through UTF-16 means the round trip loses nothing. Windows
   synthesises CF_TEXT from it automatically for older programs. */

wp_status plat_clipboard_set_text(const u8 *utf8, u32 len)
{
    HGLOBAL  h;
    WCHAR   *dst;
    int      n;

    if (utf8 == NULL) return WP_ERR;

    n = MultiByteToWideChar(CP_UTF8, 0, (const char *)utf8, (int)len,
                            NULL, 0);
    if (n < 0) return WP_ERR;

    h = GlobalAlloc(GMEM_MOVEABLE, ((SIZE_T)n + 1) * sizeof(WCHAR));
    if (h == NULL) return WP_NOMEM;
    dst = (WCHAR *)GlobalLock(h);
    if (dst == NULL) { GlobalFree(h); return WP_ERR; }
    if (len > 0)
        MultiByteToWideChar(CP_UTF8, 0, (const char *)utf8, (int)len, dst, n);
    dst[n] = 0;
    GlobalUnlock(h);

    if (!OpenClipboard(NULL)) { GlobalFree(h); return WP_ERR; }
    EmptyClipboard();
    if (SetClipboardData(CF_UNICODETEXT, h) == NULL) {
        CloseClipboard();
        /* Ownership only transfers on success, so on failure the block is
           still ours to release. */
        GlobalFree(h);
        return WP_ERR;
    }
    CloseClipboard();
    return WP_OK;
}

wp_status plat_clipboard_get_text(u8 *out, u32 out_cap, u32 *out_len)
{
    HANDLE  h;
    WCHAR  *src;
    int     n;

    if (out == NULL) return WP_ERR;
    if (out_len != NULL) *out_len = 0;

    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return WP_ERR;
    if (!OpenClipboard(NULL)) return WP_ERR;

    h = GetClipboardData(CF_UNICODETEXT);
    if (h == NULL) { CloseClipboard(); return WP_ERR; }
    src = (WCHAR *)GlobalLock(h);
    if (src == NULL) { CloseClipboard(); return WP_ERR; }

    /* -1 counts the run up to and including its terminator; the length
       written back excludes it, because this seam's strings carry a
       length rather than a null. */
    n = WideCharToMultiByte(CP_UTF8, 0, src, -1, (char *)out, (int)out_cap,
                            NULL, NULL);
    GlobalUnlock(h);
    CloseClipboard();

    if (n <= 0) return WP_RANGE;
    if (out_len != NULL) *out_len = (u32)(n - 1);
    return WP_OK;
}

wp_bool plat_clipboard_has_text(void)
{
    return IsClipboardFormatAvailable(CF_UNICODETEXT) ? WP_TRUE : WP_FALSE;
}
