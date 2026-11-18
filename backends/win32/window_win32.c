/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <windows.h>

#include "metrics_win32_internal.h"
#include "platform.h"
#include "mem_host.h"

/* No plat_window implementation, for the same reason the Amiga, Mac and Qt
   backends have none: a Win32 application registers its own class, owns
   its HWND and its menu, and builds a w32_gc from the HDC that BeginPaint
   hands it in WM_PAINT. A placeholder is kept only so a caller wanting the
   generic shape has something non-NULL to hold. */
typedef struct { int dummy; } w32_window;

wp_status plat_window_create(const u8 *title_utf8, u32 title_len,
                             i32 w, i32 h, plat_window **out)
{
    w32_window *win;
    (void)title_utf8; (void)title_len; (void)w; (void)h;
    if (out == NULL) return WP_ERR;
    win = (w32_window *)mem_alloc((u32)sizeof(w32_window));
    if (win == NULL) return WP_NOMEM;
    *out = (plat_window *)win;
    return WP_OK;
}

void plat_window_destroy(plat_window *w) { mem_free(w); }

wp_status plat_window_get_gc(plat_window *w, plat_gc **out)
{
    (void)w;
    if (out == NULL) return WP_ERR;
    /* There is no DC without a WM_PAINT to open one in. */
    *out = NULL;
    return WP_ERR;
}
