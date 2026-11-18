/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdio.h>
#include "platform.h"

wp_status plat_file_open(const char *native_path, plat_file_mode mode,
                          plat_file **out)
{
    const char *fmode;
    FILE *fp;

    switch (mode) {
        case PLAT_FILE_READ:   fmode = "rb"; break;
        case PLAT_FILE_WRITE:  fmode = "wb"; break;
        case PLAT_FILE_APPEND: fmode = "ab"; break;
        default: return WP_ERR;
    }

    fp = fopen(native_path, fmode);
    if (fp == NULL) return WP_ERR;

    *out = (plat_file *)fp;
    return WP_OK;
}

wp_status plat_file_read(plat_file *f, u8 *buf, u32 cap, u32 *out_len)
{
    size_t n = fread(buf, 1, (size_t)cap, (FILE *)f);
    if (n < (size_t)cap && ferror((FILE *)f)) return WP_ERR;
    if (out_len) *out_len = (u32)n;
    return WP_OK;
}

wp_status plat_file_write(plat_file *f, const u8 *buf, u32 len)
{
    size_t n = fwrite(buf, 1, (size_t)len, (FILE *)f);
    return (n == (size_t)len) ? WP_OK : WP_ERR;
}

wp_status plat_file_seek(plat_file *f, i32 offset, plat_seek_whence whence)
{
    int w;

    switch (whence) {
        case PLAT_SEEK_SET: w = SEEK_SET; break;
        case PLAT_SEEK_CUR: w = SEEK_CUR; break;
        case PLAT_SEEK_END: w = SEEK_END; break;
        default: return WP_ERR;
    }
    return (fseek((FILE *)f, (long)offset, w) == 0) ? WP_OK : WP_ERR;
}

wp_status plat_file_tell(plat_file *f, u32 *pos_out)
{
    long pos = ftell((FILE *)f);
    if (pos < 0) return WP_ERR;
    if (pos_out) *pos_out = (u32)pos;
    return WP_OK;
}

wp_status plat_file_size(plat_file *f, u32 *size_out)
{
    FILE *fp = (FILE *)f;
    long cur, end;

    cur = ftell(fp);
    if (cur < 0) return WP_ERR;
    if (fseek(fp, 0, SEEK_END) != 0) return WP_ERR;
    end = ftell(fp);
    if (end < 0) return WP_ERR;
    if (fseek(fp, cur, SEEK_SET) != 0) return WP_ERR;

    if (size_out) *size_out = (u32)end;
    return WP_OK;
}

void plat_file_close(plat_file *f)
{
    if (f != NULL) fclose((FILE *)f);
}

wp_status plat_file_delete(const char *native_path)
{
    return (remove(native_path) == 0) ? WP_OK : WP_ERR;
}

wp_status plat_file_exists(const char *native_path, wp_bool *exists_out)
{
    FILE *fp = fopen(native_path, "rb");

    if (fp != NULL) {
        fclose(fp);
        if (exists_out) *exists_out = WP_TRUE;
    } else {
        if (exists_out) *exists_out = WP_FALSE;
    }
    return WP_OK;
}
