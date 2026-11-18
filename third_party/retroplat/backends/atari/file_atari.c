/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdio.h>
#include "platform.h"

/* mintlib's stdio works transparently over GEMDOS on plain TOS -- no raw
   Fopen/Fread/Fwrite/Fseek GEMDOS trap calls needed (confirmed during
   T4.1/T4.2 toolchain research; see docs/atari-platform.md). plat_file
   is just a FILE* in disguise. */

wp_status plat_file_open(const char *native_path, plat_file_mode mode,
                          plat_file **out)
{
    const char *fmode;
    FILE *f;

    switch (mode) {
    case PLAT_FILE_READ:   fmode = "rb"; break;
    case PLAT_FILE_WRITE:  fmode = "wb"; break;
    case PLAT_FILE_APPEND: fmode = "ab"; break;
    default: return WP_ERR;
    }

    f = fopen(native_path, fmode);
    if (f == NULL) return WP_ERR;
    *out = (plat_file *)f;
    return WP_OK;
}

wp_status plat_file_read(plat_file *f, u8 *buf, u32 cap, u32 *out_len)
{
    size_t n;

    if (f == NULL || buf == NULL) return WP_ERR;
    n = fread(buf, 1, (size_t)cap, (FILE *)f);
    if (out_len) *out_len = (u32)n;
    if (n == 0 && !feof((FILE *)f)) return WP_ERR;
    return WP_OK;
}

wp_status plat_file_write(plat_file *f, const u8 *buf, u32 len)
{
    size_t n;

    if (f == NULL) return WP_ERR;
    n = fwrite(buf, 1, (size_t)len, (FILE *)f);
    return (n == (size_t)len) ? WP_OK : WP_ERR;
}

wp_status plat_file_seek(plat_file *f, i32 offset, plat_seek_whence whence)
{
    int origin;

    if (f == NULL) return WP_ERR;
    switch (whence) {
    case PLAT_SEEK_SET: origin = SEEK_SET; break;
    case PLAT_SEEK_CUR: origin = SEEK_CUR; break;
    case PLAT_SEEK_END: origin = SEEK_END; break;
    default: return WP_ERR;
    }
    return (fseek((FILE *)f, (long)offset, origin) == 0) ? WP_OK : WP_ERR;
}

wp_status plat_file_tell(plat_file *f, u32 *pos_out)
{
    long p;

    if (f == NULL || pos_out == NULL) return WP_ERR;
    p = ftell((FILE *)f);
    if (p < 0) return WP_ERR;
    *pos_out = (u32)p;
    return WP_OK;
}

wp_status plat_file_size(plat_file *f, u32 *size_out)
{
    long cur, end;

    if (f == NULL || size_out == NULL) return WP_ERR;
    cur = ftell((FILE *)f);
    if (cur < 0) return WP_ERR;
    if (fseek((FILE *)f, 0, SEEK_END) != 0) return WP_ERR;
    end = ftell((FILE *)f);
    if (fseek((FILE *)f, cur, SEEK_SET) != 0) return WP_ERR;
    if (end < 0) return WP_ERR;
    *size_out = (u32)end;
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
    FILE *f;

    if (exists_out == NULL) return WP_ERR;
    f = fopen(native_path, "rb");
    if (f != NULL) {
        fclose(f);
        *exists_out = WP_TRUE;
    } else {
        *exists_out = WP_FALSE;
    }
    return WP_OK;
}
