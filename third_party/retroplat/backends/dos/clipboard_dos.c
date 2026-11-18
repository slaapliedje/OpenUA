/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "platform.h"

/* Plain DOS is single-tasking with no OS-level clipboard service to
   integrate with (unlike GEM's/AmigaOS's/the Toolbox's own real
   cross-app clipboards) -- but platform.h's clipboard interface never
   mandates cross-app sharing, only persistent storage during a
   session, so an in-process buffer is a fully conformant
   implementation here, not a shortfall. Mirrors platform/host/
   clipboard_host.c's own identical in-process-buffer shape exactly. */

#define DOS_CLIPBOARD_CAP 4096

static u8      g_buf[DOS_CLIPBOARD_CAP];
static u32     g_len = 0;
static wp_bool g_has = WP_FALSE;

wp_status plat_clipboard_set_text(const u8 *utf8, u32 len)
{
    u32 i;

    if (len > DOS_CLIPBOARD_CAP) return WP_RANGE;
    for (i = 0; i < len; i++) g_buf[i] = utf8[i];
    g_len = len;
    g_has = WP_TRUE;
    return WP_OK;
}

wp_status plat_clipboard_get_text(u8 *out, u32 out_cap, u32 *out_len)
{
    u32 n, i;

    if (!g_has) {
        if (out_len) *out_len = 0;
        return WP_ERR;
    }
    n = (g_len < out_cap) ? g_len : out_cap;
    for (i = 0; i < n; i++) out[i] = g_buf[i];
    if (out_len) *out_len = n;
    return WP_OK;
}

wp_bool plat_clipboard_has_text(void)
{
    return g_has;
}
