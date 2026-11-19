/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* The host runner for tests/conformance.c, plus the edge cases only the
   host backend can be driven through without a user: dialog Cancel. */
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "window_host.h"
#include "conformance.h"

static u32 g_fails;

static void report(const char *what)
{
    printf("  FAIL %s\n", what);
    g_fails++;
}

static u32 dialog_cancel(void)
{
    static const u8 *const with_cancel[] = { (const u8 *)"Save", (const u8 *)"Cancel" };
    static const u32 with_cancel_len[] = { 4, 6 };
    static const u8 *const plain[] = { (const u8 *)"Left", (const u8 *)"Right" };
    static const u32 plain_len[] = { 4, 5 };
    static const u8 *const one[] = { (const u8 *)"OK" };
    static const u32 one_len[] = { 2 };
    wp_bool cancelled;
    u32 index, fails = 0;

#define CHECK(cond, what) do { if (!(cond)) { report(what); fails++; } } while (0)
    window_host_next_choice(1);
    cancelled = WP_FALSE;
    CHECK(plat_dialog_choice(NULL, (const u8 *)"T", 1, with_cancel, with_cancel_len, 2, 0,
                             &cancelled, &index) == WP_OK && cancelled,
          "dialog: the caller's own Cancel answers cancelled");
    window_host_next_choice(WINDOW_HOST_CANCEL);
    cancelled = WP_FALSE;
    CHECK(plat_dialog_choice(NULL, (const u8 *)"T", 1, plain, plain_len, 2, 0,
                             &cancelled, &index) == WP_OK && cancelled,
          "dialog: the backend's Cancel answers cancelled");
    window_host_next_choice(1);
    cancelled = WP_TRUE;
    CHECK(plat_dialog_choice(NULL, (const u8 *)"T", 1, plain, plain_len, 2, 0,
                             &cancelled, &index) == WP_OK && !cancelled && index == 1,
          "dialog: a choice is not a cancel");
    window_host_next_choice(WINDOW_HOST_CANCEL);
    cancelled = WP_TRUE;
    CHECK(plat_dialog_choice(NULL, (const u8 *)"T", 1, one, one_len, 1, 0,
                             &cancelled, &index) == WP_OK && !cancelled,
          "dialog: a choice of one has no Cancel");
#undef CHECK
    return fails;
}

int main(void)
{
    (void)rp_conformance_files("", report);
    (void)rp_conformance_clipboard(report);
    (void)dialog_cancel();
    if (g_fails != 0) {
        printf("conformance: %lu failed\n", (unsigned long)g_fails);
        return 1;
    }
    printf("conformance: host passes\n");
    return 0;
}
