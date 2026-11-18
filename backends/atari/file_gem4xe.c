/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* file_gem4xe.c -- plat_file on GEMDOS, for gem4xe.
 *
 * The ST's half of this seam (file_atari.c) is stdio: mintlib's works
 * transparently over GEMDOS on plain TOS, so there was never a reason to
 * reach past it.  gem4xe has no stdio to reach past, and that is a
 * DELIBERATE absence rather than a gap: Calypsi's C library carries the
 * Apache-2.0 NuttX string and stream code, which is incompatible with the
 * GPLv2 that gem4xe inherits from EmuTOS, so gem4xe links none of it and
 * writes the handful of ISO C functions it needs itself
 * (gem4xe/docs/licence.md).  A program that wants a file there calls
 * GEMDOS, which is what stdio would have been doing anyway.
 *
 * So this is the same seam with two fewer layers under it, and it is why
 * the seam exists: a shell that reads its tables through plat_file_read
 * needs no #ifdef of its own to run on both GEMs.
 *
 * GEMDOS answers with a WORD handle and a handle is not a pointer -- 0 is
 * a perfectly good one, and plat_file_open reports failure by not
 * producing a pointer at all.  Hence the table: a plat_file * here is a
 * slot in it.  Four is more than any shell in this tree has open at once
 * (one design, or one sheet being written), and running out returns
 * WP_ERR like any other open failure rather than growing a heap for it. */

#include <gem.h>

/* This file is the gem4xe half of a two-GEM backend, and the directory it
 * sits in is compiled whole by both targets -- RetroWP's ST build globs it,
 * and so does retroplat's own seam gate.  Neither can be asked to know
 * which of file_atari.c and file_gem4xe.c belongs to it, so the file says
 * so itself: outside gem4xe there is nothing here but the declaration
 * below, which exists only so the translation unit is not empty (C89
 * 3.7: a translation unit shall contain at least one declaration).
 *
 * The guard is gem4xe's own include-guard rather than a build flag, for
 * the same reason gemcompat.h uses it: what actually differs is which
 * gem.h the include path found. */
#ifdef GEM4XE_APP_GEM_H

#include "platform.h"

#define MAXOPEN 4

static struct {
    WORD used;
    WORD h;
} g_slot[MAXOPEN];

static plat_file *slot_new(WORD h)
{
    int i;

    for (i = 0; i < MAXOPEN; i++) {
        if (!g_slot[i].used) {
            g_slot[i].used = 1;
            g_slot[i].h = h;
            return (plat_file *)&g_slot[i];
        }
    }
    (void)Fclose(h);
    return 0;
}

static WORD slot_h(plat_file *f)
{
    return f ? ((WORD *)f)[1] : -1;
}

wp_status plat_file_open(const char *native_path, plat_file_mode mode,
                          plat_file **out)
{
    LONG r;

    if (native_path == 0 || out == 0) return WP_ERR;
    switch (mode) {
    case PLAT_FILE_READ:
        r = Fopen(native_path, 0);           /* read only */
        break;
    case PLAT_FILE_WRITE:
        /* GEMDOS has no "create or truncate" mode: Fcreate IS that, and
           it is a different call rather than a flag on Fopen. */
        r = Fcreate(native_path, 0);
        break;
    case PLAT_FILE_APPEND:
        r = Fopen(native_path, 2);           /* read/write */
        if (r < 0) r = Fcreate(native_path, 0);
        else (void)Fseek(0L, (WORD)r, 2);    /* 2: from the end */
        break;
    default:
        return WP_ERR;
    }
    if (r < 0) return WP_ERR;
    *out = slot_new((WORD)r);
    return *out ? WP_OK : WP_ERR;
}

wp_status plat_file_read(plat_file *f, u8 *buf, u32 cap, u32 *out_len)
{
    LONG n;

    if (f == 0 || buf == 0) return WP_ERR;
    n = Fread(slot_h(f), (LONG)cap, buf);
    if (n < 0) { if (out_len) *out_len = 0; return WP_ERR; }
    if (out_len) *out_len = (u32)n;
    return WP_OK;
}

wp_status plat_file_write(plat_file *f, const u8 *buf, u32 len)
{
    LONG n;

    if (f == 0) return WP_ERR;
    if (len == 0) return WP_OK;
    n = Fwrite(slot_h(f), (LONG)len, buf);
    return (n == (LONG)len) ? WP_OK : WP_ERR;
}

wp_status plat_file_seek(plat_file *f, i32 offset, plat_seek_whence whence)
{
    WORD mode;

    if (f == 0) return WP_ERR;
    switch (whence) {
    case PLAT_SEEK_SET: mode = 0; break;
    case PLAT_SEEK_CUR: mode = 1; break;
    case PLAT_SEEK_END: mode = 2; break;
    default: return WP_ERR;
    }
    return (Fseek((LONG)offset, slot_h(f), mode) >= 0) ? WP_OK : WP_ERR;
}

wp_status plat_file_tell(plat_file *f, u32 *pos_out)
{
    LONG p;

    if (f == 0 || pos_out == 0) return WP_ERR;
    p = Fseek(0L, slot_h(f), 1);             /* 1: from here, moving none */
    if (p < 0) return WP_ERR;
    *pos_out = (u32)p;
    return WP_OK;
}

wp_status plat_file_size(plat_file *f, u32 *size_out)
{
    LONG cur, end;
    WORD h;

    if (f == 0 || size_out == 0) return WP_ERR;
    h = slot_h(f);
    cur = Fseek(0L, h, 1);
    if (cur < 0) return WP_ERR;
    end = Fseek(0L, h, 2);
    if (end < 0) return WP_ERR;
    if (Fseek(cur, h, 0) < 0) return WP_ERR;
    *size_out = (u32)end;
    return WP_OK;
}

void plat_file_close(plat_file *f)
{
    if (f == 0) return;
    (void)Fclose(slot_h(f));
    ((WORD *)f)[0] = 0;
}

wp_status plat_file_delete(const char *native_path)
{
    return (Fdelete(native_path) >= 0) ? WP_OK : WP_ERR;
}

wp_status plat_file_exists(const char *native_path, wp_bool *exists_out)
{
    LONG r;

    if (exists_out == 0) return WP_ERR;
    r = Fopen(native_path, 0);
    if (r >= 0) {
        (void)Fclose((WORD)r);
        *exists_out = WP_TRUE;
    } else {
        *exists_out = WP_FALSE;
    }
    return WP_OK;
}

#else  /* not gem4xe -- file_atari.c serves this seam instead */

typedef int rp_file_gem4xe_not_this_target;

#endif
