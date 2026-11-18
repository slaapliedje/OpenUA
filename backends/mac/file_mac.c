/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <Files.h>
#include "platform.h"

/* Real classic Mac File Manager calls (FSOpen/FSRead/FSWrite/FSClose/
   Create/FSDelete), not a stdio wrapper -- same discipline
   file_amiga.c/file_atari.c both follow: verify against the real
   target API rather than trust a libc that happens to work.

   `native_path` is a plain C string; the File Manager wants a Pascal
   string (length byte + bytes, no NUL). vRefNum 0 means "the default
   volume," but a fully-qualified colon path like "Unix:name" resolves
   to the named volume regardless of vRefNum -- confirmed empirically
   under BasiliskII during M12 Phase 0 (host/tools/mac/launcher.c and
   smoke.c both rely on exactly this).

   SetFPos's posMode maps onto plat_seek_whence directly: fsFromStart
   (absolute), fsFromMark (relative to the current position, exactly
   SEEK_CUR's semantics), fsFromLEOF (relative to end-of-file). */

static void c_to_pstr(const char *c, unsigned char *p)
{
    u32 len = 0;

    while (c[len] != '\0' && len < 255) len++;
    p[0] = (unsigned char)len;
    {
        u32 i;
        for (i = 0; i < len; i++) p[i + 1] = (unsigned char)c[i];
    }
}

wp_status plat_file_open(const char *native_path, plat_file_mode mode,
                          plat_file **out)
{
    unsigned char pname[256];
    short refNum;
    OSErr err;

    if (native_path == NULL || out == NULL) return WP_ERR;
    c_to_pstr(native_path, pname);

    switch (mode) {
    case PLAT_FILE_READ:
        err = FSOpen(pname, 0, &refNum);
        if (err != noErr) return WP_ERR;
        break;

    case PLAT_FILE_WRITE:
        err = Create(pname, 0, 'RWP1', 'TEXT');
        if (err != noErr && err != dupFNErr) return WP_ERR;
        err = FSOpen(pname, 0, &refNum);
        if (err != noErr) return WP_ERR;
        if (SetEOF(refNum, 0) != noErr) { FSClose(refNum); return WP_ERR; }
        break;

    case PLAT_FILE_APPEND:
        err = Create(pname, 0, 'RWP1', 'TEXT');
        if (err != noErr && err != dupFNErr) return WP_ERR;
        err = FSOpen(pname, 0, &refNum);
        if (err != noErr) return WP_ERR;
        if (SetFPos(refNum, fsFromLEOF, 0) != noErr) {
            FSClose(refNum);
            return WP_ERR;
        }
        break;

    default:
        return WP_ERR;
    }

    *out = (plat_file *)(long)refNum;
    return WP_OK;
}

wp_status plat_file_read(plat_file *f, u8 *buf, u32 cap, u32 *out_len)
{
    long count = (long)cap;
    OSErr err;

    if (f == NULL || buf == NULL) return WP_ERR;
    err = FSRead((short)(long)f, &count, buf);
    /* eofErr means the read ran into (or started at) end-of-file --
       count still holds however many bytes were actually transferred
       (possibly 0), which is a clean EOF, not a failure -- matching
       file_amiga.c's own "0 bytes, no error" treatment of Read(). */
    if (err != noErr && err != eofErr) return WP_ERR;
    if (out_len) *out_len = (u32)count;
    return WP_OK;
}

wp_status plat_file_write(plat_file *f, const u8 *buf, u32 len)
{
    long count = (long)len;

    if (f == NULL) return WP_ERR;
    if (FSWrite((short)(long)f, &count, buf) != noErr) return WP_ERR;
    return (count == (long)len) ? WP_OK : WP_ERR;
}

wp_status plat_file_seek(plat_file *f, i32 offset, plat_seek_whence whence)
{
    short mode;

    if (f == NULL) return WP_ERR;
    switch (whence) {
    case PLAT_SEEK_SET: mode = fsFromStart; break;
    case PLAT_SEEK_CUR: mode = fsFromMark;  break;
    case PLAT_SEEK_END: mode = fsFromLEOF;  break;
    default: return WP_ERR;
    }
    return (SetFPos((short)(long)f, mode, (long)offset) == noErr) ? WP_OK : WP_ERR;
}

wp_status plat_file_tell(plat_file *f, u32 *pos_out)
{
    long pos;

    if (f == NULL || pos_out == NULL) return WP_ERR;
    if (GetFPos((short)(long)f, &pos) != noErr) return WP_ERR;
    *pos_out = (u32)pos;
    return WP_OK;
}

wp_status plat_file_size(plat_file *f, u32 *size_out)
{
    long eof;

    if (f == NULL || size_out == NULL) return WP_ERR;
    if (GetEOF((short)(long)f, &eof) != noErr) return WP_ERR;
    *size_out = (u32)eof;
    return WP_OK;
}

void plat_file_close(plat_file *f)
{
    if (f != NULL) FSClose((short)(long)f);
}

wp_status plat_file_delete(const char *native_path)
{
    unsigned char pname[256];

    if (native_path == NULL) return WP_ERR;
    c_to_pstr(native_path, pname);
    return (FSDelete(pname, 0) == noErr) ? WP_OK : WP_ERR;
}

wp_status plat_file_exists(const char *native_path, wp_bool *exists_out)
{
    unsigned char pname[256];
    short refNum;

    if (exists_out == NULL) return WP_ERR;
    if (native_path == NULL) return WP_ERR;
    c_to_pstr(native_path, pname);

    if (FSOpen(pname, 0, &refNum) == noErr) {
        FSClose(refNum);
        *exists_out = WP_TRUE;
    } else {
        *exists_out = WP_FALSE;
    }
    return WP_OK;
}
