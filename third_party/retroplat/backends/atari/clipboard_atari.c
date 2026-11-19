/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <gem.h>
#include <mint/osbind.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"

/* The GEM clipboard (docs/atari-platform.md), by the protocol the Atari
   Compendium sets out (scrp_read, and "The Clipboard" in the AES
   overview): the scrap manager holds a DIRECTORY, and the text is a file
   called SCRAP.TXT in it.

     1. Ask the AES for the directory (scrp_read).
     2. If it has none, or one that is not there, make \CLIPBRD\ on the
        boot drive and tell the AES (scrp_write) -- "C:\CLIPBRD\" on a
        hard disk, "A:\CLIPBRD\" on a floppy-only ST.
     3. Before writing, delete SCRAP.* there: a SCRAP.GEM some other
        program left would otherwise be what the next one pastes.

   This file used to write C:\SCRAP.TXT whatever the AES said, which
   fails outright on the baseline machine (a 1 MB ST with no hard disk,
   so no C:) and overwrote the directory every other program had agreed
   on. Not every program writes the directory with its trailing
   backslash, so one is added when it is missing. */

#define SC_PATHLEN 128     /* the AES's own scrap path buffer */

static long read_bootdev(void)
{
    return (long)*(volatile short *)0x446L;     /* _bootdev */
}

/* "X:\CLIPBRD\" on the boot drive, made if it is not there. */
static void dir_default(char *out)
{
    long d = Supexec(read_bootdev);

    if (d < 0 || d > 25) d = (long)Dgetdrv();
    out[0] = (char)('A' + d);
    strcpy(out + 1, ":\\CLIPBRD");
    (void)Dcreate(out);                  /* already there is fine */
    strcat(out, "\\");
}

static wp_bool dir_exists(const char *dir)
{
    char probe[SC_PATHLEN + 8];
    size_t n = strlen(dir);
    _DTA *old = Fgetdta(), dta;
    wp_bool there;

    if (n == 0 || n >= SC_PATHLEN) return WP_FALSE;
    if (n == 3 && dir[1] == ':') return WP_TRUE;          /* a drive's root */
    memcpy(probe, dir, n);
    if (probe[n - 1] == '\\') n--;
    probe[n] = '\0';
    Fsetdta(&dta);
    there = (wp_bool)(Fsfirst(probe, FA_DIR) == 0 && (dta.dta_attribute & FA_DIR) != 0);
    Fsetdta(old);
    return there;
}

/* The clipboard directory, with its trailing backslash: the AES's, or
   the default made and registered. */
static void clip_dir(char *out)
{
    size_t n;

    out[0] = '\0';
    (void)scrp_read(out);
    out[SC_PATHLEN - 1] = '\0';
    n = strlen(out);
    if (n > 0 && n < SC_PATHLEN - 1 && out[n - 1] != '\\') {
        out[n] = '\\';
        out[n + 1] = '\0';
    }
    if (!dir_exists(out)) {
        dir_default(out);
        (void)scrp_write(out);
    }
}

/* Step 3: SCRAP.* in the directory goes before a new scrap is written. */
static void clear_scraps(const char *dir)
{
    char mask[SC_PATHLEN + 16], victim[SC_PATHLEN + 16];
    _DTA *old = Fgetdta(), dta;
    long r;

    strcpy(mask, dir);
    strcat(mask, "SCRAP.*");
    Fsetdta(&dta);
    for (r = Fsfirst(mask, 0); r == 0; r = Fsnext()) {
        strcpy(victim, dir);
        strcat(victim, dta.dta_name);
        (void)Fdelete(victim);
    }
    Fsetdta(old);
}

wp_status plat_clipboard_set_text(const u8 *utf8, u32 len)
{
    u8 native[1024];
    char dir[SC_PATHLEN], path[SC_PATHLEN + 16];
    u32 native_len, unmapped;
    wp_status st;
    FILE *f;
    size_t written;

    st = plat_utf8_to_native(utf8, len, native, (u32)sizeof(native), &native_len, &unmapped);
    if (st != WP_OK) return st;

    clip_dir(dir);
    clear_scraps(dir);
    strcpy(path, dir);
    strcat(path, "SCRAP.TXT");
    f = fopen(path, "wb");
    if (f == NULL) return WP_ERR;
    written = fwrite(native, 1, (size_t)native_len, f);
    fclose(f);
    if (written != (size_t)native_len) return WP_ERR;
    return WP_OK;
}

wp_status plat_clipboard_get_text(u8 *out, u32 out_cap, u32 *out_len)
{
    char dir[SC_PATHLEN], path[SC_PATHLEN + 16];
    FILE *f;
    u8 native[1024];
    size_t native_len;

    clip_dir(dir);
    strcpy(path, dir);
    strcat(path, "SCRAP.TXT");
    f = fopen(path, "rb");
    if (f == NULL) {
        if (out_len) *out_len = 0;
        return WP_ERR;
    }
    native_len = fread(native, 1, sizeof(native), f);
    fclose(f);
    return plat_native_to_utf8(native, (u32)native_len, out, out_cap, out_len);
}

wp_bool plat_clipboard_has_text(void)
{
    char dir[SC_PATHLEN], path[SC_PATHLEN + 16];
    FILE *f;

    clip_dir(dir);
    strcpy(path, dir);
    strcat(path, "SCRAP.TXT");
    f = fopen(path, "rb");
    if (f == NULL) return WP_FALSE;
    fclose(f);
    return WP_TRUE;
}
