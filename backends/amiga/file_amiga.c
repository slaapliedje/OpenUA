/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dos.h>
#include "platform.h"

/* Real AmigaDOS calls (Open/Read/Write/Seek/Close via dos.library), not a
   libc stdio wrapper -- -noixemul means no unix-emulation layer is
   assumed present, and this project's own discipline (see
   docs/amiga-platform.md) is to verify against the real target API, not
   just a libc that happens to work. DOSBase is opened by the -noixemul
   crt0 before main() runs (confirmed empirically -- see
   docs/amiga-platform.md's muitest probe), so no explicit
   OpenLibrary("dos.library", ...) is needed here.

   AmigaDOS's Seek() returns the *old* file position on success (not the
   new one, unlike fseek()), and there is no separate "tell" primitive --
   the documented idiom is Seek(file, 0, OFFSET_CURRENT), which returns
   the current position without moving. plat_file_tell and
   plat_file_size below both rely on this. */

wp_status plat_file_open(const char *native_path, plat_file_mode mode,
                          plat_file **out)
{
    BPTR fh;

    switch (mode) {
    case PLAT_FILE_READ:
        fh = Open((CONST_STRPTR)native_path, MODE_OLDFILE);
        break;
    case PLAT_FILE_WRITE:
        fh = Open((CONST_STRPTR)native_path, MODE_NEWFILE);
        break;
    case PLAT_FILE_APPEND:
        fh = Open((CONST_STRPTR)native_path, MODE_READWRITE);
        if (fh == 0) fh = Open((CONST_STRPTR)native_path, MODE_NEWFILE);
        if (fh != 0) Seek(fh, 0, OFFSET_END);
        break;
    default:
        return WP_ERR;
    }

    if (fh == 0) return WP_ERR;
    *out = (plat_file *)fh;
    return WP_OK;
}

wp_status plat_file_read(plat_file *f, u8 *buf, u32 cap, u32 *out_len)
{
    LONG n;

    if (f == NULL || buf == NULL) return WP_ERR;
    n = Read((BPTR)f, buf, (LONG)cap);
    if (n < 0) return WP_ERR;
    if (out_len) *out_len = (u32)n;
    return WP_OK;
}

wp_status plat_file_write(plat_file *f, const u8 *buf, u32 len)
{
    LONG n;

    if (f == NULL) return WP_ERR;
    n = Write((BPTR)f, (APTR)buf, (LONG)len);
    return (n == (LONG)len) ? WP_OK : WP_ERR;
}

wp_status plat_file_seek(plat_file *f, i32 offset, plat_seek_whence whence)
{
    LONG mode;

    if (f == NULL) return WP_ERR;
    switch (whence) {
    case PLAT_SEEK_SET: mode = OFFSET_BEGINNING; break;
    case PLAT_SEEK_CUR: mode = OFFSET_CURRENT; break;
    case PLAT_SEEK_END: mode = OFFSET_END; break;
    default: return WP_ERR;
    }
    return (Seek((BPTR)f, (LONG)offset, mode) != -1) ? WP_OK : WP_ERR;
}

wp_status plat_file_tell(plat_file *f, u32 *pos_out)
{
    LONG p;

    if (f == NULL || pos_out == NULL) return WP_ERR;
    p = Seek((BPTR)f, 0, OFFSET_CURRENT);
    if (p < 0) return WP_ERR;
    *pos_out = (u32)p;
    return WP_OK;
}

wp_status plat_file_size(plat_file *f, u32 *size_out)
{
    LONG cur, end;

    if (f == NULL || size_out == NULL) return WP_ERR;
    cur = Seek((BPTR)f, 0, OFFSET_CURRENT);
    if (cur < 0) return WP_ERR;
    if (Seek((BPTR)f, 0, OFFSET_END) < 0) return WP_ERR;
    end = Seek((BPTR)f, 0, OFFSET_CURRENT);
    if (end < 0) return WP_ERR;
    if (Seek((BPTR)f, cur, OFFSET_BEGINNING) < 0) return WP_ERR;
    *size_out = (u32)end;
    return WP_OK;
}

void plat_file_close(plat_file *f)
{
    if (f != NULL) Close((BPTR)f);
}

wp_status plat_file_delete(const char *native_path)
{
    return DeleteFile((CONST_STRPTR)native_path) ? WP_OK : WP_ERR;
}

wp_status plat_file_exists(const char *native_path, wp_bool *exists_out)
{
    BPTR lock;

    if (exists_out == NULL) return WP_ERR;
    lock = Lock((CONST_STRPTR)native_path, ACCESS_READ);
    if (lock != 0) {
        UnLock(lock);
        *exists_out = WP_TRUE;
    } else {
        *exists_out = WP_FALSE;
    }
    return WP_OK;
}
