/* instdisk — copy a multi-disk OpenUA data set onto a hard disk / CF / SD.
 *
 *   instdisk [destination] [source]
 *
 * The game data does not fit on floppies — a minimum playable install is about
 * 7.4 MB — so tools/mkdatadisks.sh spreads it over a numbered set of .ST/.ADF
 * images and this walks the user through feeding them in.
 *
 * ★ MANIFEST-DRIVEN, NOT DIRECTORY-WALKING. Every disk carries DISK.LST:
 *
 *     <disk-number> <total-disks> <set-name>
 *     GEO001.DAT                       ; one relative path per line, '/' as
 *     HEIRS.DSN/GEO005.DAT             ; the separator whatever the host uses
 *
 * opendir/readdir exist on both toolchains but their behaviour across a FLOPPY
 * CHANGE is exactly the thing that would be least testable here, and a stale
 * directory cache would silently copy the previous disk's files again. Reading
 * an explicit list means a wrong or unchanged disk is DETECTED (the header's
 * disk number will not match) instead of quietly producing a half-installed
 * directory that looks fine.
 *
 * ★ THE AMIGA READS THE DRIVE, NOT THE VOLUME. The first real A1200 run
 * (2026-08-22) looped forever at disk 2: with no source argument the manifest
 * path was relative to the CURRENT DIRECTORY, and on AmigaDOS that is a lock
 * on the VOLUME "OpenUA-Data-1" — so after the swap the OS put up "Please
 * insert volume OpenUA-Data-1", the user obliged, disk 1's manifest was read,
 * and "I need disk 2" followed, endlessly. The source must name the DEVICE
 * (DF0:), which always means "whatever is in the drive". On the Amiga the
 * device of the current directory is resolved through the DosList at startup
 * and used from then on; and since the OS notices a disk change by itself,
 * the swap prompt POLLS for the expected disk instead of waiting on RETURN
 * (RETURN still works).
 *
 * After the data set, the ENGINE is installed too: the engine disks carry
 * ENGINE.LST (same header line, then "<src> <dest> [a]" — `a` appends, which
 * is how the two ECS halves become one frua). A disk without ENGINE.LST is
 * the compressed Mega ST disk, whose README says what to do instead.
 *
 * Portable C99 over stdio + mkdir, like uainst: builds as an Atari .TTP with
 * m68k-atari-mint and for the Amiga with Bebbo.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __MINT__
#define SEP '\\'
#else
#define SEP '/'
#endif

#define MAXPATH 256
#define COPYBUF 16384

#ifdef __amigaos__
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include "drawer_icon.h"

/* Write a drawer icon so the OpenUA drawer shows on Workbench. The Installer
 * script route makes one via (makedir (infos)); the plain instdisk route (a
 * machine with no AmigaOS Installer, e.g. WB 3.1) did not — reported on the
 * A500, 2026-08-22. dest is "DH0:OpenUA"; its icon is the sibling file
 * "DH0:OpenUA.info". Skip if one already exists. */
static void amiga_write_drawer_icon(const char *dest)
{
	char ipath[MAXPATH];
	BPTR fh;
	size_t n = strlen(dest);
	if (n + 6 >= sizeof ipath)
		return;
	memcpy(ipath, dest, n);
	memcpy(ipath + n, ".info", 6);
	fh = Open((CONST_STRPTR)ipath, MODE_OLDFILE);	/* already there? */
	if (fh) { Close(fh); return; }
	fh = Open((CONST_STRPTR)ipath, MODE_NEWFILE);
	if (!fh)
		return;
	Write(fh, (APTR)g_drawer_info, (LONG)sizeof g_drawer_info);
	Close(fh);
}

/* The DEVICE name ("DF0:") of the current directory's volume: the volume
 * lock's handler task is the same MsgPort the device entry carries, so walk
 * the device list for it. Falls back to DF0: if anything is missing. */
static void amiga_source_device(char *out, size_t cap)
{
	BPTR lock = Lock((CONST_STRPTR) "", ACCESS_READ);
	struct FileLock *fl;
	struct DosList  *dl;
	int found = 0;

	snprintf(out, cap, "DF0:");
	if (!lock)
		return;
	fl = (struct FileLock *)BADDR(lock);
	dl = LockDosList(LDF_DEVICES | LDF_READ);
	while (!found && (dl = NextDosEntry(dl, LDF_DEVICES)) != NULL) {
		if (dl->dol_Task == fl->fl_Task && dl->dol_Name) {
			const unsigned char *bn = (const unsigned char *)BADDR(dl->dol_Name);
			size_t n = bn[0];

			if (n > 0 && n + 2 <= cap) {
				memcpy(out, bn + 1, n);
				out[n] = ':';
				out[n + 1] = 0;
				found = 1;
			}
		}
	}
	UnLockDosList(LDF_DEVICES | LDF_READ);
	UnLock(lock);
}

/* One second of waiting for either a keypress or a disk change. Returns 1 if
 * a line was typed (consumed), 0 on timeout. */
static int amiga_wait_key_or_tick(void)
{
	BPTR in = Input();

	if (in && WaitForChar(in, 2000000L)) {	/* 2 s between polls */
		int c;
		while ((c = getchar()) != '\n' && c != EOF)
			;
		return 1;
	}
	if (!in)
		Delay(100);
	return 0;
}
#endif

#ifdef INSTDISK_GUI
/* installer/gem_atari.c — INSTDISK.PRG: selector, alerts, progress window */
int  gem_init(const char *title);
void gem_exit(void);
int  gem_alert(int icon, const char *text, const char *buttons);
void gem_progress(const char *line1, const char *line2, int pct);
int  gem_pick_folder(char *dest, size_t cap, const char *seed);
static int g_gui;                       /* 1 once gem_init succeeded */
/* The console lines are the .TTP's user interface; in the GEM build they
 * would be painted by TOS's VT52 straight over the desktop (a green band
 * under the window, seen in the first drive). Silence them once the window
 * is up — the inner printf is not re-expanded, so this is safe. */
#define printf(...) ((g_gui) ? 0 : printf(__VA_ARGS__))
#endif

static void chomp(char *s)
{
	size_t n = strlen(s);
	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
		s[--n] = 0;
}

/* Join dir + leaf with the host separator; leaf may carry '/' components,
 * which are translated on the way in. */
static void path_join(char *out, size_t cap, const char *dir, const char *leaf)
{
	size_t n;
	char  *p;

	if (dir && dir[0] && strcmp(dir, ".") != 0) {
		snprintf(out, cap, "%s", dir);
		n = strlen(out);
		if (n && out[n - 1] != SEP && out[n - 1] != '/'
		      && out[n - 1] != ':' && n + 1 < cap) {
			out[n++] = SEP;
			out[n] = 0;
		}
		snprintf(out + n, cap - n, "%s", leaf);
	} else {
		snprintf(out, cap, "%s", leaf);
	}
	for (p = out; *p; p++)
		if (*p == '/')
			*p = SEP;
}

/* mkdir every component of the path except the last (the file itself). */
static void make_parents(const char *path)
{
	char tmp[MAXPATH];
	char *p;

	snprintf(tmp, sizeof tmp, "%s", path);
	for (p = tmp; *p; p++) {
		if ((*p == SEP || *p == '/') && p != tmp) {
			char save = *p;
			*p = 0;
			/* A bare drive prefix ("C:", "DH0:") is not a directory
			 * and mkdir on it fails harmlessly; the guard keeps the
			 * console quiet rather than being load-bearing. */
			if (strlen(tmp) > 0 && tmp[strlen(tmp) - 1] != ':')
				(void)mkdir(tmp, 0755);
			*p = save;
		}
	}
}

static int copy_one(const char *src, const char *dst, long *bytes_out,
                    int append)
{
	FILE *in, *out;
	char *buf;
	size_t n;
	long   total = 0;

	in = fopen(src, "rb");
	if (!in)
		return -1;
	make_parents(dst);
	out = fopen(dst, append ? "ab" : "wb");
	if (!out) {
		fclose(in);
		return -2;
	}
	buf = malloc(COPYBUF);
	if (!buf) {
		fclose(in); fclose(out);
		return -3;
	}
	while ((n = fread(buf, 1, COPYBUF, in)) > 0) {
		if (fwrite(buf, 1, n, out) != n) {
			free(buf); fclose(in); fclose(out);
			return -4;
		}
		total += (long)n;
	}
	free(buf);
	fclose(in);
	if (fclose(out) != 0)
		return -5;
	*bytes_out = total;
	return 0;
}

/* Open the manifest on the currently-inserted disk and read its header. */
static FILE *open_manifest(const char *src, const char *lst, int *disk,
                           int *total, char *name, size_t namecap)
{
	char  path[MAXPATH], line[MAXPATH];
	FILE *f;

	path_join(path, sizeof path, src, lst);
	f = fopen(path, "r");
	if (!f)
		return NULL;
	if (!fgets(line, sizeof line, f)) {
		fclose(f);
		return NULL;
	}
	chomp(line);
	*disk = *total = 0;
	name[0] = 0;
	if (sscanf(line, "%d %d %63[^\n]", disk, total, name) < 2) {
		fclose(f);
		return NULL;
	}
	(void)namecap;
	return f;
}

/* Returns 0 on EOF: a closed stdin (a pipe, a redirected console) must end
 * the install, not re-prompt forever — the host harness found it spinning. */
static int wait_return(void)
{
	int c;
	while ((c = getchar()) != '\n' && c != EOF)
		;
	return c != EOF;
}

/* Wait for the disk whose manifest `lst` carries number `want` to be in the
 * drive. Amiga: poll every second (the OS sees the change), RETURN also
 * re-checks. Atari: RETURN re-checks. Returns when it is there. */
static void wait_for_disk(const char *src, const char *lst, int want,
                          const char *what)
{
	char name[64];
	int  disk, total;

	for (;;) {
		FILE *m;

#ifdef INSTDISK_GUI
		if (g_gui) {
			char t[96];

			snprintf(t, sizeof t, "Insert %s %d,|then press OK.", what, want);
			if (gem_alert(1, t, "OK|Cancel") != 1) {
				gem_alert(1, "Install cancelled.", "OK");
				gem_exit();
				exit(1);
			}
		} else
#endif
		{
#ifdef __amigaos__
			(void)amiga_wait_key_or_tick();
#else
			if (!wait_return()) {
				printf("\nno input - giving up.\n");
				exit(1);
			}
#endif
		}
		m = open_manifest(src, lst, &disk, &total, name, sizeof name);
		if (m) {
			fclose(m);
			if (disk == want)
				return;
			printf("\nThat is %s %d of %d - I need %s %d.\n",
			       what, disk, total, what, want);
#ifdef INSTDISK_GUI
			if (g_gui) {
				char t[96];

				snprintf(t, sizeof t, "That is %s %d of %d.|I need %s %d.",
				         what, disk, total, what, want);
				gem_alert(1, t, "OK");
			}
#endif
		}
#ifndef __amigaos__
#ifdef INSTDISK_GUI
		if (!g_gui)
#endif
		printf("Insert %s %d and press RETURN: ", what, want);
#endif
	}
}

int main(int argc, char **argv)
{
	char dest[MAXPATH] = "", src[MAXPATH] = "";
	char name[64], line[MAXPATH];
	char spath[MAXPATH], dpath[MAXPATH];
	int  disk = 0, total = 0, expect = 1;
	long files = 0, bytes = 0;
	int  failed = 0;

	setvbuf(stdout, NULL, _IONBF, 0);
#ifdef INSTDISK_GUI
	g_gui = gem_init("OpenUA installer");
#endif
	printf("instdisk - OpenUA data installer\n\n");

	if (argc >= 2)
		snprintf(dest, sizeof dest, "%s", argv[1]);
	if (argc >= 3)
		snprintf(src, sizeof src, "%s", argv[2]);
#ifdef __amigaos__
	/* ★ A500 (2026-08-22): every poll of an EMPTY drive raised the system
	 * "Please insert a disk in DF0" requester — the installer looked as
	 * if it were nagging once a second. pr_WindowPtr = -1 turns the DOS
	 * requesters off for this process; a missing disk then comes back as
	 * a plain open failure, which the poll already treats as "not yet". */
	{
		struct Process *pr = (struct Process *)FindTask(NULL);
		pr->pr_WindowPtr = (APTR)-1L;
	}
	if (src[0] == 0) {
		amiga_source_device(src, sizeof src);
		printf("Reading disks from %s\n", src);
	}
#endif

#ifdef INSTDISK_GUI
	if (dest[0] == 0 && g_gui) {
		gem_alert(1, "OpenUA installer.|Pick the folder to install|"
		             "into in the next dialog|(it is created if missing).", "OK");
		if (!gem_pick_folder(dest, sizeof dest, "C:\\OPENUA")) {
			gem_alert(1, "No folder chosen - nothing done.", "OK");
			gem_exit();
			return 1;
		}
		(void)mkdir(dest, 0755);
		gem_progress("Destination:", dest, 0);
	}
#endif
	if (dest[0] == 0) {
		/* RETURN on an empty line takes the default: this prompt is what
		 * the Workbench Install icon falls back to on a machine with no
		 * AmigaOS Installer (WB 3.1 never shipped one), and the person
		 * double-clicking an icon should not have to know path syntax. */
#ifdef __amigaos__
		static const char def_dest[] = "DH0:OpenUA";
#else
		static const char def_dest[] = "C:\\OPENUA";
#endif
		printf("Install the game WHERE? (created if missing)\n");
		printf("Destination [%s]: ", def_dest);
		if (!fgets(dest, sizeof dest, stdin)) {
			printf("\nno destination given - nothing done.\n");
			return 1;
		}
		chomp(dest);
		if (dest[0] == 0)
			snprintf(dest, sizeof dest, "%s", def_dest);
		printf("-> %s\n", dest);
	}

	for (;;) {
		FILE *m = open_manifest(src, "DISK.LST", &disk, &total, name,
		                        sizeof name);

		if (!m) {
			/* Started from the ENGINE disk (the Workbench icon's
			 * fallback path)? Say so rather than "no DISK.LST". */
			FILE *e = open_manifest(src, "ENGINE.LST", &disk, &total,
			                        name, sizeof name);
			if (e) {
				fclose(e);
				printf("\nThis is the OpenUA engine disk - the engine"
				       " is installed AFTER the data.\n");
			} else {
				printf("\nNo DISK.LST found%s%s.\n",
				       src[0] ? " in " : "", src[0] ? src : "");
			}
#ifdef INSTDISK_GUI
			if (g_gui) {
				wait_for_disk(src, "DISK.LST", expect, "data disk");
				continue;
			}
#endif
#ifdef __amigaos__
			printf("Insert data disk %d (or press RETURN / Ctrl-C): ",
			       expect);
#else
			printf("Insert data disk %d and press RETURN"
			       " (or Ctrl-C to stop): ", expect);
#endif
			wait_for_disk(src, "DISK.LST", expect, "disk");
			continue;
		}
		if (disk != expect) {
			fclose(m);
			printf("\nThat is disk %d of %d - I need disk %d.\n",
			       disk, total, expect);
#ifdef __amigaos__
			printf("Insert disk %d: ", expect);
#else
			printf("Insert disk %d and press RETURN: ", expect);
#endif
			wait_for_disk(src, "DISK.LST", expect, "disk");
			continue;
		}

		if (expect == 1)
			printf("%s: %d disk(s) -> %s\n\n",
			       name[0] ? name : "OpenUA data", total, dest);
		printf("Disk %d of %d:\n", disk, total);
#ifdef INSTDISK_GUI
		{
			/* count the disk's files for the bar, then rewind */
			long pos = ftell(m);
			int  cnt = 0, idx = 0;
			char dl[48];

			while (fgets(line, sizeof line, m)) {
				chomp(line);
				if (line[0] && line[0] != ';') cnt++;
			}
			fseek(m, pos, SEEK_SET);
			snprintf(dl, sizeof dl, "Data disk %d of %d", disk, total);
#define GUI_TICK(f) do { if (g_gui) { idx++; \
			gem_progress(dl, (f), cnt ? (int)(100L * idx / cnt) : 100); } } while (0)
#else
#define GUI_TICK(f) do { } while (0)
#endif

		while (fgets(line, sizeof line, m)) {
			long n = 0;
			int  rc;

			chomp(line);
			if (line[0] == 0 || line[0] == ';')
				continue;
			path_join(spath, sizeof spath, src, line);
			path_join(dpath, sizeof dpath, dest, line);
			GUI_TICK(line);
			rc = copy_one(spath, dpath, &n, 0);
			if (rc != 0) {
				printf("  FAILED (%d) %s\n", rc, line);
				failed++;
			} else {
				printf("  %s\n", line);
				files++;
				bytes += n;
			}
		}
		fclose(m);
#ifdef INSTDISK_GUI
		}
#undef GUI_TICK
#endif

		if (disk >= total)
			break;
		expect = disk + 1;
#ifdef INSTDISK_GUI
		if (!g_gui)
#endif
		{
#ifdef __amigaos__
		printf("\nRemove disk %d, insert disk %d of %d: ",
		       disk, expect, total);
#else
		printf("\nRemove disk %d, insert disk %d of %d,"
		       " then press RETURN: ", disk, expect, total);
#endif
		}
		wait_for_disk(src, "DISK.LST", expect, "data disk");
	}

	printf("\n%ld file(s), %ld byte(s) copied to %s\n", files, bytes, dest);
	if (failed) {
		printf("%d file(s) FAILED - the install is incomplete.\n",
		       failed);
#ifdef INSTDISK_GUI
		if (g_gui) {
			gem_alert(1, "Some files FAILED to copy.|The install is incomplete.", "OK");
			gem_exit();
		}
#endif
		return 1;
	}

	/* --- the engine -------------------------------------------------- */
	printf("\nNow the engine. Insert the OpenUA ENGINE disk\n"
	       "(the one with frua / FRUA.PRG on it)");
#ifdef __amigaos__
	printf(", or press RETURN to skip: ");
#else
	printf(" and press RETURN\n(or type s and RETURN to skip): ");
#endif
	expect = 1;
	{
		long efiles = 0;
		int  skipped = 0, first = 1;

		for (;;) {
			FILE *m;
			int   rc;

#ifdef __amigaos__
			/* poll for the engine disk; a typed line means skip. After
			 * wait_for_disk has already confirmed a later disk, go
			 * straight to its manifest. */
			if (first) {
				for (;;) {
					if (amiga_wait_key_or_tick()) {
						skipped = 1;
						break;
					}
					m = open_manifest(src, "ENGINE.LST", &disk,
					                  &total, name, sizeof name);
					if (m)
						break;
				}
				if (skipped)
					break;
			} else {
				m = open_manifest(src, "ENGINE.LST", &disk, &total,
				                  name, sizeof name);
				if (!m)
					continue;
			}
#else
#ifdef INSTDISK_GUI
			if (g_gui && first) {
				if (gem_alert(1, "Data installed.|Now insert the OpenUA|"
				                 "ENGINE disk (FRUA.PRG on it).", "OK|Skip") != 1) {
					skipped = 1;
					break;
				}
			} else
#endif
			if (first) {
				int c = getchar();

				if (c == 's' || c == 'S') {
					skipped = 1;
					wait_return();
					break;
				}
				while (c != '\n' && c != EOF)
					c = getchar();
			}
			m = open_manifest(src, "ENGINE.LST", &disk, &total, name,
			                  sizeof name);
			if (!m) {
#ifdef INSTDISK_GUI
				if (g_gui) {
					if (gem_alert(1, "No ENGINE.LST on that disk.|"
					                 "(The 720K Mega ST disk holds|FRUA.ZIP: unzip it on a PC.)",
					              "Retry|Skip") != 1) {
						skipped = 1;
						break;
					}
					continue;
				}
#endif
				printf("\nNo ENGINE.LST on that disk. If it is the"
				       " compressed Mega ST disk,\nits README says how"
				       " to unzip FRUA.PRG on a PC.\n"
				       "Insert an engine disk and press RETURN (s to"
				       " skip): ");
				continue;
			}
#endif
			if (disk != expect) {
				fclose(m);
				printf("\nThat is engine disk %d of %d - I need %d.\n",
				       disk, total, expect);
#ifdef __amigaos__
				printf("Insert engine disk %d: ", expect);
#else
				printf("Insert engine disk %d and press RETURN: ", expect);
#endif
				wait_for_disk(src, "ENGINE.LST", expect, "engine disk");
				m = open_manifest(src, "ENGINE.LST", &disk, &total,
				                  name, sizeof name);
				if (!m)
					continue;
			}
			first = 0;
			printf("\n%s, disk %d of %d:\n", name[0] ? name : "engine",
			       disk, total);
#ifdef INSTDISK_GUI
			if (g_gui)
				gem_progress("Engine", name, 50);
#endif
			while (fgets(line, sizeof line, m)) {
				char sl[MAXPATH], dl_[MAXPATH], mode[8];
				long n = 0;
				int  k;

				chomp(line);
				if (line[0] == 0 || line[0] == ';')
					continue;
				mode[0] = 0;
				k = sscanf(line, "%255s %255s %7s", sl, dl_, mode);
				if (k < 2)
					continue;
				path_join(spath, sizeof spath, src, sl);
				path_join(dpath, sizeof dpath, dest, dl_);
				rc = copy_one(spath, dpath, &n, mode[0] == 'a');
				if (rc != 0) {
					printf("  FAILED (%d) %s\n", rc, sl);
					failed++;
				} else {
					printf("  %s%s\n", sl,
					       mode[0] == 'a' ? " (joined)" : "");
					efiles++;
					bytes += n;
				}
			}
			fclose(m);
			if (disk >= total)
				break;
			expect = disk + 1;
#ifdef __amigaos__
			printf("\nInsert engine disk %d of %d: ", expect, total);
#else
			printf("\nInsert engine disk %d of %d, then press RETURN: ",
			       expect, total);
#endif
			wait_for_disk(src, "ENGINE.LST", expect, "engine disk");
		}
		if (skipped)
			printf("\nEngine skipped. Copy the engine (FRUA.PRG / frua)"
			       " into %s yourself.\n", dest);
		else if (failed)
			printf("\n%d engine file(s) FAILED.\n", failed);
		else
			printf("\nEngine installed (%ld file(s)).\n", efiles);

#ifdef __amigaos__
		/* Convert the DOS art we just installed. The Amiga engine cannot
		 * convert at runtime (ADR-0015), so uaconv (installed alongside the
		 * engine) does it once, here. It reads its own stdin for the
		 * delete-originals prompt; SystemTags inherits our console. */
		if (!skipped && !failed) {
			char up[MAXPATH], cmd[MAXPATH * 2 + 8];
			BPTR l;
			path_join(up, sizeof up, dest, "uaconv");
			l = Lock((CONST_STRPTR)up, ACCESS_READ);
			if (l) {
				UnLock(l);
				snprintf(cmd, sizeof cmd, "%s %s", up, dest);
				printf("\nConverting the art for the Amiga (one time)...\n");
				SystemTags((CONST_STRPTR)cmd, TAG_DONE);
			} else {
				printf("\nNOTE: uaconv not found in %s — run it there to"
				       " convert the art before playing.\n", dest);
			}
		}
#endif
#ifdef INSTDISK_GUI
		if (g_gui) {
			char t[128];

			gem_progress("Done.", dest, 100);
			if (failed)
				snprintf(t, sizeof t, "%d file(s) FAILED.|The install is incomplete.", failed);
			else if (skipped)
				snprintf(t, sizeof t, "Data installed in|%s|Copy FRUA.PRG there yourself.", dest);
			else
				snprintf(t, sizeof t, "OpenUA is installed in|%s|Run FRUA.PRG from there.", dest);
			gem_alert(1, t, "OK");
			gem_exit();
		}
#endif
	}

#ifdef __amigaos__
	if (!failed)
		amiga_write_drawer_icon(dest);
#endif
	if (failed)
		return 1;
	printf("\nDone. Run the game from %s.\n", dest);
	return 0;
}
