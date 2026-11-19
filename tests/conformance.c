/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* tests/conformance.c -- platform.h's edge cases, held to on every
   backend. Only platform.h: no libc file calls, nothing native. */
#include "platform.h"
#include "conformance.h"

#define CHECK(cond, what) do { if (!(cond)) { report(what); fails++; } } while (0)

static void path_join(char *out, u32 cap, const char *dir, const char *name)
{
    u32 n = 0;
    while (*dir && n + 1 < cap) out[n++] = *dir++;
    while (*name && n + 1 < cap) out[n++] = *name++;
    out[n] = '\0';
}

u32 rp_conformance_files(const char *dir, rp_conformance_report report)
{
    static const u8 data[5] = { 'h', 'e', 'l', 'l', 'o' };
    char path[160], missing[160];
    plat_file *f = (plat_file *)0;
    u8 buf[16];
    u32 n, pos;
    wp_bool exists;
    wp_status st;
    u32 fails = 0;

    path_join(path, sizeof(path), dir, "RPCONF.TMP");
    path_join(missing, sizeof(missing), dir, "RPNONE.TMP");
    (void)plat_file_delete(path);
    (void)plat_file_delete(missing);

    /* a file that is not there */
    exists = WP_TRUE;
    CHECK(plat_file_exists(missing, &exists) == WP_OK, "exists: a missing file is WP_OK");
    CHECK(exists == WP_FALSE, "exists: a missing file is WP_FALSE");
    CHECK(plat_file_open(missing, PLAT_FILE_READ, &f) != WP_OK, "open: a missing file for reading fails");
    CHECK(plat_file_delete(missing) == WP_ERR, "delete: a missing file is WP_ERR");

    /* five bytes written */
    st = plat_file_open(path, PLAT_FILE_WRITE, &f);
    CHECK(st == WP_OK, "open: create for writing");
    if (st != WP_OK) return fails;
    CHECK(plat_file_write(f, data, 5) == WP_OK, "write: five bytes");
    plat_file_close(f);
    CHECK(plat_file_exists(path, &exists) == WP_OK && exists, "exists: the file written");

    /* read back: cap 0, the bytes, the end */
    st = plat_file_open(path, PLAT_FILE_READ, &f);
    CHECK(st == WP_OK, "open: for reading");
    if (st == WP_OK) {
        n = 99;
        CHECK(plat_file_read(f, buf, 0, &n) == WP_OK, "read: cap 0 is WP_OK");
        CHECK(n == 0, "read: cap 0 gives 0 bytes");
        n = 99;
        CHECK(plat_file_read(f, (u8 *)0, 0, &n) == WP_OK && n == 0, "read: cap 0 with no buffer is WP_OK, 0");
        CHECK(plat_file_tell(f, &pos) == WP_OK && pos == 0, "read: cap 0 does not move the position");
        CHECK(plat_file_size(f, &pos) == WP_OK && pos == 5, "size: five");
        n = 0;
        CHECK(plat_file_read(f, buf, sizeof(buf), &n) == WP_OK, "read: past the end is WP_OK");
        CHECK(n == 5 && buf[0] == 'h' && buf[4] == 'o', "read: the five bytes");
        n = 99;
        CHECK(plat_file_read(f, buf, sizeof(buf), &n) == WP_OK, "read: at the end is WP_OK");
        CHECK(n == 0, "read: at the end gives 0 bytes");
        n = 99;
        CHECK(plat_file_read(f, buf, 0, &n) == WP_OK && n == 0, "read: cap 0 at the end is WP_OK, 0");
        CHECK(plat_file_seek(f, 1, PLAT_SEEK_SET) == WP_OK, "seek: to 1");
        n = 0;
        CHECK(plat_file_read(f, buf, 2, &n) == WP_OK && n == 2 && buf[0] == 'e', "read: two from 1");
        plat_file_close(f);
    }

    /* append, then delete */
    st = plat_file_open(path, PLAT_FILE_APPEND, &f);
    CHECK(st == WP_OK, "open: for appending");
    if (st == WP_OK) {
        CHECK(plat_file_write(f, data, 2) == WP_OK, "write: two appended");
        plat_file_close(f);
        if (plat_file_open(path, PLAT_FILE_READ, &f) == WP_OK) {
            CHECK(plat_file_size(f, &pos) == WP_OK && pos == 7, "append: seven in all");
            plat_file_close(f);
        }
    }
    CHECK(plat_file_delete(path) == WP_OK, "delete: the file");
    CHECK(plat_file_exists(path, &exists) == WP_OK && !exists, "delete: it is gone");
    CHECK(plat_file_delete(path) == WP_ERR, "delete: twice is WP_ERR");
    return fails;
}

u32 rp_conformance_clipboard(rp_conformance_report report)
{
    u8 buf[16];
    u32 n, fails = 0;

    CHECK(plat_clipboard_set_text((const u8 *)"clip", 4) == WP_OK, "clipboard: set");
    CHECK(plat_clipboard_has_text(), "clipboard: has text");
    n = 0;
    CHECK(plat_clipboard_get_text(buf, sizeof(buf), &n) == WP_OK && n == 4 && buf[0] == 'c',
          "clipboard: get it back");
    return fails;
}
