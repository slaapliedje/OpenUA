/* modcore — what a module archive holds, decided once for every front end.
 *
 * The rules the on-machine module manager and uainst share: which archive
 * layout a fan module uses, which files the engine reads and what they are
 * called on the card, and reading the members — including PKZIP 1.x
 * "imploded" ones, which miniz cannot. tools/modman/core.py is the reference
 * these are held to (tests/test_installer.py).
 *
 * Portable C99, no platform headers: built for the Atari (mint), the Amiga
 * (Bebbo) and the host test suite. Include miniz.c (header-only) first.
 */
#ifndef MODCORE_H
#define MODCORE_H

/* What a downloaded or picked file really is. */
enum { MC_ZIP, MC_WEBPAGE, MC_STUFFIT, MC_LHA, MC_UNKNOWN };
int mc_kind(const unsigned char *head, unsigned long n);
const char *mc_kind_text(int kind);

/* A file name as it goes on the card: upper-case 8.3, Mac long art names in
 * the 8.3 spelling the engine also probes (ADR-0013). 0 when there is none. */
int mc_dos83(const char *name, char out[13]);

/* Unpack zip method 6. Returns `outlen`, or a negative value when the data
 * is damaged. `flags` is the member's general-purpose bit flag. */
long mc_explode(const unsigned char *in, unsigned long inlen,
                unsigned char *out, unsigned long outlen, unsigned flags);

#define MC_MAXFILES 1024
#define MC_DEST     18          /* "SAVE/" + 8.3 + NUL */

enum { MC_SKIP_OUTSIDE = 1, MC_SKIP_SUBFOLDER, MC_SKIP_PROGRAM, MC_SKIP_NO83,
       MC_SKIP_SAME83 };
const char *mc_skip_text(int why);

typedef struct mc_file {
	unsigned short index;       /* the archive member                        */
	char           dest[MC_DEST]; /* "GAME001.DAT", "SAVE/SAVGAMA.CSV"       */
} mc_file;

typedef struct mc_skip {
	unsigned short index;
	unsigned char  why;
} mc_skip;

typedef struct mc_plan {
	char     design[9];         /* "CURSE": the folder is CURSE.DSN          */
	char     title[32];         /* from GAME001.DAT                          */
	int      nfiles, nskip;
	int      pc_art, mac_art;   /* any .TLB / any .CTL                       */
	mc_file  file[MC_MAXFILES];
	mc_skip  skip[MC_MAXFILES];
} mc_plan;

/* Find the design in an open archive and decide every member's fate.
 * `archive_name` names a loose design. Returns 1, or 0 with `err` set. */
int mc_plan_zip(void *mz_zip_archive_ptr, const char *archive_name,
                mc_plan *p, char *err, unsigned errcap);

/* One member, unpacked into a malloc'd buffer (free() it). 0 on failure,
 * with `err` set; an imploded member is checked against its CRC. */
unsigned char *mc_extract(void *mz_zip_archive_ptr, unsigned index,
                          unsigned long *len, char *err, unsigned errcap);

#endif
