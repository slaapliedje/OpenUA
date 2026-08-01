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

static int copy_one(const char *src, const char *dst, long *bytes_out)
{
	FILE *in, *out;
	char *buf;
	size_t n;
	long   total = 0;

	in = fopen(src, "rb");
	if (!in)
		return -1;
	make_parents(dst);
	out = fopen(dst, "wb");
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
static FILE *open_manifest(const char *src, int *disk, int *total, char *name,
                           size_t namecap)
{
	char  path[MAXPATH], line[MAXPATH];
	FILE *f;

	path_join(path, sizeof path, src, "DISK.LST");
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

static void wait_return(void)
{
	int c;
	while ((c = getchar()) != '\n' && c != EOF)
		;
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
	printf("instdisk - OpenUA data installer\n\n");

	if (argc >= 2)
		snprintf(dest, sizeof dest, "%s", argv[1]);
	if (argc >= 3)
		snprintf(src, sizeof src, "%s", argv[2]);

	if (dest[0] == 0) {
		printf("Install the game data WHERE?\n");
		printf("  (e.g. C:\\OPENUA on Atari, DH0:OpenUA on Amiga)\n");
		printf("Destination: ");
		if (!fgets(dest, sizeof dest, stdin)) {
			printf("\nno destination given - nothing done.\n");
			return 1;
		}
		chomp(dest);
		if (dest[0] == 0) {
			printf("no destination given - nothing done.\n");
			return 1;
		}
	}

	for (;;) {
		FILE *m = open_manifest(src, &disk, &total, name, sizeof name);

		if (!m) {
			printf("\nNo DISK.LST found%s%s.\n",
			       src[0] ? " in " : "", src[0] ? src : "");
			printf("Insert data disk %d and press RETURN"
			       " (or Ctrl-C to stop): ", expect);
			wait_return();
			continue;
		}
		if (disk != expect) {
			fclose(m);
			printf("\nThat is disk %d of %d - I need disk %d.\n",
			       disk, total, expect);
			printf("Insert disk %d and press RETURN: ", expect);
			wait_return();
			continue;
		}

		if (expect == 1)
			printf("%s: %d disk(s) -> %s\n\n",
			       name[0] ? name : "OpenUA data", total, dest);
		printf("Disk %d of %d:\n", disk, total);

		while (fgets(line, sizeof line, m)) {
			long n = 0;
			int  rc;

			chomp(line);
			if (line[0] == 0 || line[0] == ';')
				continue;
			path_join(spath, sizeof spath, src, line);
			path_join(dpath, sizeof dpath, dest, line);
			rc = copy_one(spath, dpath, &n);
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

		if (disk >= total)
			break;
		expect = disk + 1;
		printf("\nRemove disk %d, insert disk %d of %d,"
		       " then press RETURN: ", disk, expect, total);
		wait_return();
	}

	printf("\n%ld file(s), %ld byte(s) copied to %s\n", files, bytes, dest);
	if (failed) {
		printf("%d file(s) FAILED - the install is incomplete.\n",
		       failed);
		return 1;
	}
	printf("\nDone. Copy the engine (FRUA.PRG / frua) into that same\n"
	       "directory and run it from there.\n");
	return 0;
}
