/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include "platform.h"
#include "window_host.h"

/* No real GUI at this stage -- enough surface to link and let a test
   harness exercise the seam. plat_window/plat_gc are opaque handles per
   platform.h; this struct is private to this backend. */
typedef struct {
    i32 w;
    i32 h;
} host_window;

wp_status plat_window_create(const u8 *title_utf8, u32 title_len,
                              i32 w, i32 h, plat_window **out)
{
    host_window *win;

    WP_UNUSED(title_utf8);
    WP_UNUSED(title_len);

    win = (host_window *)mem_alloc((u32)sizeof(host_window));
    if (win == NULL) return WP_NOMEM;
    win->w = w;
    win->h = h;
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
    /* No real drawing surface: the window handle itself doubles as a
       non-null opaque gc handle for draw_host.c's no-op functions. */
    *out = (plat_gc *)w;
    return WP_OK;
}

wp_status plat_dialog_message(plat_window *parent, plat_dialog_kind kind,
                               const u8 *utf8_msg, u32 len,
                               plat_dialog_result *out)
{
    WP_UNUSED(parent);
    WP_UNUSED(kind);
    WP_UNUSED(utf8_msg);
    WP_UNUSED(len);
    if (out) *out = PLAT_DR_OK;
    return WP_OK;
}

/* window_host.h: the next choice dialogs' answers, in order (-1 = the
   default), queued by window_host_next_choice. */
#define HOST_CHOICE_QUEUE 16
static i32 g_choice_queue[HOST_CHOICE_QUEUE];
static u32 g_choice_head = 0, g_choice_count = 0;

/* window_host.h: the last choice dialog's title, NUL-terminated. */
static char g_last_title[128];

wp_status plat_dialog_choice(plat_window *parent,
                              const u8 *title_utf8, u32 title_len,
                              const u8 * const *option_labels_utf8,
                              const u32 *option_label_lens,
                              u32 option_count, u32 default_index,
                              wp_bool *out_cancelled, u32 *out_index)
{
    /* No real UI at this stage -- always "picks" the default, never
       cancels. Enough to let platform/common code (and host tests
       exercising it) link and run without a real GUI, same reasoning
       as plat_dialog_message above. */
    u32 i;

    WP_UNUSED(parent);
    for (i = 0; i < title_len && i + 1 < (u32)sizeof(g_last_title); i++) {
        g_last_title[i] = (char)title_utf8[i];
    }
    g_last_title[i] = '\0';
    WP_UNUSED(option_labels_utf8);
    WP_UNUSED(option_label_lens);
    if (option_count == 0) return WP_RANGE;
    if (out_cancelled) *out_cancelled = WP_FALSE;
    if (out_index) *out_index = (default_index < option_count) ? default_index : 0;
    if (g_choice_count > 0) {           /* a test said what to pick, once */
        i32 pick = g_choice_queue[g_choice_head];
        g_choice_head = (g_choice_head + 1) % HOST_CHOICE_QUEUE;
        g_choice_count--;
        if (pick >= 0 && out_index && (u32)pick < option_count) *out_index = (u32)pick;
    }
    return WP_OK;
}

void window_host_next_choice(i32 index)
{
    if (g_choice_count >= HOST_CHOICE_QUEUE) return;
    g_choice_queue[(g_choice_head + g_choice_count) % HOST_CHOICE_QUEUE] = index;
    g_choice_count++;
}

const char *window_host_last_title(void)
{
    return g_last_title;
}
