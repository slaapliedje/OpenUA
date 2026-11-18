/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* clipboard_gem4xe.c -- plat_clipboard_* on gem4xe.
 *
 * The convention is classic GEM's and is unchanged from the ST
 * (clipboard_atari.c): the scrap manager keeps a DIRECTORY, and the content
 * is a plain file called SCRAP.TXT inside it, so two programs that have
 * never heard of each other agree on a place rather than on a format.
 * Three things differ from the ST's file.
 *
 * FILES.  gem4xe has no stdio -- deliberately, because Calypsi's comes
 * from Apache NuttX and Apache 2.0 cannot be linked into the GPLv2 binary
 * gem4xe produces (gem4xe/docs/licence.md).  GEMDOS is what stdio would
 * have called anyway, so it is called directly, exactly as file_gem4xe.c
 * does for the plat_file seam.
 *
 * THE PATH BUFFER MUST BE NEAR.  scrp_read writes into the caller's
 * buffer and scrp_write reads out of it, and gem4xe hands the AES an
 * address as sixteen bits in bank $00 (WP_AES_NEAR in gemcompat.h).  A
 * path is also the one string too long for the far-string bounce the ABI
 * uses elsewhere -- gem.h says so where it declares these two, and the
 * AES's own buffer is SC_PATHLEN, 128 bytes, long enough for a full path
 * AND a filename.  So one near buffer of that size serves both calls, and
 * the near pool pays for it once.
 *
 * THE FALLBACK DRIVE IS ASKED FOR, NOT ASSUMED.  The ST's file falls back
 * to a literal "C:\\", which is right for its Hatari harddrive mount and
 * wrong here: gem4xe maps a GEM drive letter onto a SpartaDOS device by
 * position (src/sys/dos.c, dos_cioname -- A: is D1:, so C: is D3:), and on
 * a one-floppy machine, which is what this port has been run on, D3: does
 * not exist.  Dgetdrv answers the drive the program was started from, so
 * that is what the fallback names.
 *
 * BUFFERS ARE STATIC, NOT AUTOMATIC.  An application's near memory on
 * gem4xe -- which is where its stack is, because the 65816 stack is in
 * bank $00 by hardware -- comes out of a small pool.  The ST's version of
 * this file puts a 1 KB scrap buffer on the stack, which is unremarkable
 * on a machine with a 32 KB one and fatal here.  Declared static, the big
 * ones go to far BSS under --data-model=large and cost the pool nothing;
 * only the path buffer above is deliberately near. */

#include <gem.h>

#ifdef GEM4XE_APP_GEM_H

#include <string.h>            /* strcpy, strcat -- declared, not defined:
                                  the kit writes both (lib/clib.c), and an
                                  unprototyped call would be compiled
                                  differently on this toolchain. */
#include "platform.h"
#include "gemcompat.h"

#define SCRAP_MAX   1024
#define SC_PATHLEN  128             /* the AES's own, src/aes/scrap.c */
#define SCRAP_NAME  "SCRAP.TXT"

static u8 g_native[SCRAP_MAX];

/* Near, because its address is what crosses into the AES. */
static WP_AES_NEAR char g_dir[SC_PATHLEN];

/* Far is fine for this one: GEMDOS takes a far pointer (gem.h declares
   Fopen's name `const char FAR *`), and only the AES is restricted. */
static char g_path[SC_PATHLEN + 16];

/* "D:\" for the drive the program was started from. */
static void dir_default(char *out)
{
    WORD d = Dgetdrv();

    if (d < 0 || d > 25) d = 2;     /* 2 = C:, the ST's own fallback */
    out[0] = (char)('A' + d);
    out[1] = ':';
    out[2] = '\\';
    out[3] = '\0';
}

/* The scrap directory the AES is holding, or the default if it is holding
   none.  gem4xe's sc_read hands back an empty string and answers TRUE
   rather than refusing, exactly as TOS does, so the emptiness is the
   answer and not an error to report. */
static void dir_current(char *out)
{
    g_dir[0] = '\0';
    (void)scrp_read(g_dir);

    if (g_dir[0] == '\0') {
        dir_default(out);
        return;
    }
    strcpy(out, g_dir);
}

static void scrap_path(char *out)
{
    dir_current(out);
    strcat(out, SCRAP_NAME);
}

wp_status plat_clipboard_set_text(const u8 *utf8, u32 len)
{
    u32 native_len, unmapped;
    wp_status st;
    LONG h, wrote;

    st = plat_utf8_to_native(utf8, len, g_native, (u32)SCRAP_MAX,
                             &native_len, &unmapped);
    if (st != WP_OK) return st;

    /* Written to the directory this program owns, then registered -- so a
       failed write never leaves the AES pointing at a scrap that is not
       there. */
    dir_default(g_dir);
    strcpy(g_path, g_dir);
    strcat(g_path, SCRAP_NAME);

    h = Fcreate(g_path, 0);
    if (h < 0) return WP_ERR;

    wrote = Fwrite((WORD)h, (LONG)native_len, g_native);
    (void)Fclose((WORD)h);

    if (wrote != (LONG)native_len) return WP_ERR;

    (void)scrp_write(g_dir);
    return WP_OK;
}

wp_status plat_clipboard_get_text(u8 *out, u32 out_cap, u32 *out_len)
{
    LONG h, got;

    scrap_path(g_path);

    h = Fopen(g_path, 0);               /* 0 = read only */
    if (h < 0) {
        /* The AES's directory may be another program's, and stale. This
           program's own scrap is the second place to look. */
        dir_default(g_path);
        strcat(g_path, SCRAP_NAME);
        h = Fopen(g_path, 0);
    }
    if (h < 0) {
        if (out_len) *out_len = 0;
        return WP_ERR;
    }

    got = Fread((WORD)h, (LONG)SCRAP_MAX, g_native);
    (void)Fclose((WORD)h);

    if (got < 0) {
        if (out_len) *out_len = 0;
        return WP_ERR;
    }

    return plat_native_to_utf8(g_native, (u32)got, out, out_cap, out_len);
}

wp_bool plat_clipboard_has_text(void)
{
    LONG h;

    scrap_path(g_path);

    h = Fopen(g_path, 0);
    if (h < 0) {
        dir_default(g_path);
        strcat(g_path, SCRAP_NAME);
        h = Fopen(g_path, 0);
    }
    if (h < 0) return WP_FALSE;

    (void)Fclose((WORD)h);
    return WP_TRUE;
}

#else

typedef int rp_clipboard_gem4xe_not_this_target;

#endif
