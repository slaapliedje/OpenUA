/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "platform.h"

/* Not a real OS clipboard -- an in-process buffer, sufficient for host
   tests that only need cut/paste to round-trip within the same run. */

#define HOST_CLIPBOARD_CAP 4096

static u8      g_buf[HOST_CLIPBOARD_CAP];
static u32     g_len = 0;
static wp_bool g_has = WP_FALSE;

wp_status plat_clipboard_set_text(const u8 *utf8, u32 len)
{
    u32 i;

    if (len > HOST_CLIPBOARD_CAP) return WP_RANGE;
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
