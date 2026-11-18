/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "metrics_qt_internal.h"

extern "C" {
#include "platform.h"
#include "mem_host.h"
}

/* No plat_window implementation, for the same reason the Amiga, Mac and GTK
   backends have none: a Qt application owns its own QMainWindow, menu bar
   and central widget through direct calls, and builds a qt_gc from the
   QPainter it opens in paintEvent. A placeholder is kept only so a caller
   wanting the generic shape has something non-NULL to hold. */
typedef struct { int dummy; } qt_window;

extern "C" {

wp_status plat_window_create(const u8 *title_utf8, u32 title_len,
                             i32 w, i32 h, plat_window **out)
{
    (void)title_utf8; (void)title_len; (void)w; (void)h;
    if (out == NULL) return WP_ERR;
    qt_window *win = (qt_window *)mem_alloc((u32)sizeof(qt_window));
    if (win == NULL) return WP_NOMEM;
    *out = (plat_window *)win;
    return WP_OK;
}

void plat_window_destroy(plat_window *w) { mem_free(w); }

wp_status plat_window_get_gc(plat_window *w, plat_gc **out)
{
    (void)w;
    if (out == NULL) return WP_ERR;
    /* There is no painter without a paintEvent to open one in. */
    *out = NULL;
    return WP_ERR;
}

}  /* extern "C" */
