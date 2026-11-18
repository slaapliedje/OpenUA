/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <gem.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"

/* Classic GEM clipboard convention (docs/atari-platform.md): entirely
   file-based. scrp_write(dir) tells AES "the clipboard content lives in
   this directory"; the content itself is a plain file literally named
   SCRAP.TXT within it. This app is both producer and consumer in this
   environment, so it always writes to a fixed, known directory (the
   GEMDOS C: root, matching this project's Hatari CI harddrive mount)
   and registers it with scrp_write() so other GEM apps could interop --
   but reads always fall back to that same fixed directory if scrp_read()
   returns something empty, rather than trusting a possibly-stale
   AES-remembered path. */
#define CLIP_DIR  "C:\\"
#define CLIP_FILE "C:\\SCRAP.TXT"

wp_status plat_clipboard_set_text(const u8 *utf8, u32 len)
{
    u8 native[1024];
    u32 native_len, unmapped;
    wp_status st;
    FILE *f;
    size_t written;

    st = plat_utf8_to_native(utf8, len, native, (u32)sizeof(native), &native_len, &unmapped);
    if (st != WP_OK) return st;

    f = fopen(CLIP_FILE, "wb");
    if (f == NULL) return WP_ERR;
    written = fwrite(native, 1, (size_t)native_len, f);
    fclose(f);
    if (written != (size_t)native_len) return WP_ERR;

    scrp_write(CLIP_DIR);
    return WP_OK;
}

wp_status plat_clipboard_get_text(u8 *out, u32 out_cap, u32 *out_len)
{
    char dir[128];
    char path[160];
    FILE *f;
    u8 native[1024];
    size_t native_len;
    wp_status st;

    strcpy(dir, CLIP_DIR);
    scrp_read(dir);
    if (dir[0] == '\0') strcpy(dir, CLIP_DIR);

    strcpy(path, dir);
    strcat(path, "SCRAP.TXT");

    f = fopen(path, "rb");
    if (f == NULL) {
        f = fopen(CLIP_FILE, "rb");
        if (f == NULL) {
            if (out_len) *out_len = 0;
            return WP_ERR;
        }
    }
    native_len = fread(native, 1, sizeof(native), f);
    fclose(f);

    st = plat_native_to_utf8(native, (u32)native_len, out, out_cap, out_len);
    return st;
}

wp_bool plat_clipboard_has_text(void)
{
    FILE *f = fopen(CLIP_FILE, "rb");

    if (f == NULL) return WP_FALSE;
    fclose(f);
    return WP_TRUE;
}
