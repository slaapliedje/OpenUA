/* uainst — the OpenUA fan-module installer.
 *
 * Takes a fan module's ZIP, extracts it into a design folder, and
 * converts the DOS art with the same byte-exact core the engine uses
 * (src/convert/artconv.c):
 *
 *   - every HLIB *.TLB gains its GLIB *.ctl twin (8.3 names, ADR-0013);
 *   - the *.TLB itself is REPLACED by the synthesized 1-bit mono GLIB
 *     where the family has one (ADR-0014: mono is install-time only —
 *     41-53 s per wall master on an 8 MHz 68000 belongs here, not at
 *     area entry);
 *   - design data (*.DAT etc.) passes through untouched (byte-identical
 *     between the DOS and Mac releases).
 *
 * Usage:
 *   uainst <module.zip> [destination-dir]
 * With no arguments it reads UAINST.INF (line 1 = zip, line 2 = dest)
 * or prompts on the console. The design lands in
 * <dest>/<ZIPNAME>.DSN/ — or in the folder the ZIP itself names if it
 * already carries a *.DSN/ directory component.
 *
 * Portable C99 over stdio + malloc: builds as an Atari .TTP with the
 * m68k-atari-mint cross toolchain (`make installer`), as a host CLI for
 * the test suite, and (untested until the Bebbo toolchain lands,
 * ADR-0012) for the Amiga. ZIP reading is vendored public-domain miniz
 * v1.14 (installer/miniz.c, Rich Geldreich — see its unlicense block).
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MINIZ_HEADER_FILE_ONLY
#include "miniz.c"

#include "../src/convert/artconv.h"

#ifdef __MINT__
#define SEP '\\'
#else
#define SEP '/'
#endif

#define SCRATCH_CAP (512L * 1024)
#define MAXPATH 512

static unsigned char *g_scratch;

/* --- small path helpers --------------------------------------------------- */

static const char *basename_of(const char *p)
{
	const char *b = p, *q;
	for (q = p; *q; q++)
		if (*q == '/' || *q == '\\' || *q == ':')
			b = q + 1;
	return b;
}

static int ends_with_ci(const char *s, const char *ext)
{
	size_t n = strlen(s), m = strlen(ext);
	size_t i;
	if (n < m)
		return 0;
	for (i = 0; i < m; i++)
		if (tolower((unsigned char)s[n - m + i])
		    != tolower((unsigned char)ext[i]))
			return 0;
	return 1;
}

static void chomp(char *s)
{
	size_t n = strlen(s);
	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r'
		     || s[n - 1] == ' '))
		s[--n] = 0;
}

/* --- the art conversion pass ---------------------------------------------- */

static unsigned char *read_file(const char *path, long *len)
{
	FILE *f = fopen(path, "rb");
	unsigned char *buf;
	long n;
	if (!f)
		return 0;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(n ? n : 1);
	if (!buf || fread(buf, 1, n, f) != (size_t)n) {
		fclose(f);
		free(buf);
		return 0;
	}
	fclose(f);
	*len = n;
	return buf;
}

static int write_file(const char *path, const unsigned char *buf, long len)
{
	FILE *f = fopen(path, "wb");
	int ok;
	if (!f)
		return 0;
	ok = (fwrite(buf, 1, len, f) == (size_t)len);
	fclose(f);
	return ok;
}

/* Convert one extracted .TLB in place: write the .ctl twin, then replace
 * the .TLB with the mono synthesis. Returns 0 skip, 1 colour-only,
 * 2 colour+mono, negative on error. */
static int convert_one(const char *tlb_path)
{
	unsigned char *src, *dst;
	long n, r, cap;
	char ctl_path[MAXPATH];
	size_t plen = strlen(tlb_path);
	int num, den, mode, result = 1;

	src = read_file(tlb_path, &n);
	if (!src)
		return -1;
	if (n < 4 || memcmp(src, "HLIB", 4) != 0) {
		free(src);
		return 0;			/* already Mac art (or not art) */
	}
	cap = n + 4096;
	dst = malloc(cap);
	if (!dst) {
		free(src);
		return -1;
	}
	r = artconv_convert(src, n, dst, cap, g_scratch, SCRATCH_CAP);
	if (r < 0) {
		printf("  SKIP %s (convert error %ld)\n",
		       basename_of(tlb_path), r);
		free(src);
		free(dst);
		return -2;
	}
	memcpy(ctl_path, tlb_path, plen + 1);
	memcpy(ctl_path + plen - 3, "ctl", 3);	/* 8.3 stem kept (ADR-0013) */
	if (!write_file(ctl_path, dst, r)) {
		free(src);
		free(dst);
		return -1;
	}

	if (artconv_mono_family(basename_of(tlb_path), &num, &den, &mode)) {
		long m = artconv_mono_synth(dst, r, src, n, g_scratch,
					    SCRATCH_CAP, num, den, mode);
		/* mono output re-uses the src buffer: always smaller
		 * (1bpp + RLE) than the 8bpp colour original */
		if (m > 0 && write_file(tlb_path, src, m))
			result = 2;
		else
			printf("  NOTE %s: mono synthesis failed (%ld); "
			       ".TLB left as DOS art\n",
			       basename_of(tlb_path), m);
	}
	free(src);
	free(dst);
	return result;
}

/* --- install -------------------------------------------------------------- */

static void design_name_from_zip(const char *zip, char *out, size_t cap)
{
	const char *b = basename_of(zip);
	size_t i, n = 0;
	for (i = 0; b[i] && b[i] != '.' && n + 6 < cap && n < 8; i++) {
		char c = b[i];
		if (c >= 'a' && c <= 'z')
			c -= 32;
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
		    || c == '_' || c == '-')
			out[n++] = c;
	}
	if (n == 0)
		out[n++] = 'M';
	memcpy(out + n, ".DSN", 5);
}

int main(int argc, char **argv)
{
	char zip_path[MAXPATH] = "", dest_dir[MAXPATH] = ".";
	char dsn_dir[MAXPATH], dsn_name[16], path[MAXPATH];
	mz_zip_archive za;
	mz_uint i, nfiles;
	int extracted = 0, converted = 0, monoed = 0, failed = 0;

	/* unbuffered: progress lines must appear as they happen on the
	 * Atari/Amiga consoles (and under emulator/pipe capture) */
	setvbuf(stdout, NULL, _IONBF, 0);

	printf("uainst - OpenUA fan-module installer\n\n");

	if (argc >= 2) {
		strncpy(zip_path, argv[1], sizeof zip_path - 1);
		if (argc >= 3)
			strncpy(dest_dir, argv[2], sizeof dest_dir - 1);
	} else {
		FILE *inf = fopen("UAINST.INF", "r");
		if (inf) {
			if (fgets(zip_path, sizeof zip_path, inf))
				chomp(zip_path);
			if (fgets(dest_dir, sizeof dest_dir, inf))
				chomp(dest_dir);
			fclose(inf);
			if (dest_dir[0] == 0)
				strcpy(dest_dir, ".");
		}
		if (zip_path[0] == 0) {
			printf("Module ZIP file: ");
			fflush(stdout);
			if (!fgets(zip_path, sizeof zip_path, stdin))
				return 1;
			chomp(zip_path);
			printf("Install into folder [.]: ");
			fflush(stdout);
			if (fgets(dest_dir, sizeof dest_dir, stdin)) {
				chomp(dest_dir);
				if (dest_dir[0] == 0)
					strcpy(dest_dir, ".");
			}
		}
	}
	if (zip_path[0] == 0) {
		printf("usage: uainst <module.zip> [destination-dir]\n");
		return 1;
	}

	g_scratch = malloc(SCRATCH_CAP);
	if (!g_scratch) {
		printf("out of memory\n");
		return 1;
	}

	memset(&za, 0, sizeof za);
	if (!mz_zip_reader_init_file(&za, zip_path, 0)) {
		printf("cannot open ZIP: %s\n", zip_path);
		return 1;
	}
	nfiles = mz_zip_reader_get_num_files(&za);

	/* the design folder: honour a *.DSN/ component inside the ZIP,
	 * else name it after the ZIP */
	dsn_name[0] = 0;
	for (i = 0; i < nfiles && dsn_name[0] == 0; i++) {
		char nm[MAXPATH];
		char *seg, *next;
		mz_zip_reader_get_filename(&za, i, nm, sizeof nm);
		for (seg = nm; seg; seg = next) {
			next = strpbrk(seg, "/\\");
			if (next)
				*next++ = 0;
			if (next && ends_with_ci(seg, ".dsn")
			    && strlen(seg) < sizeof dsn_name) {
				strcpy(dsn_name, seg);
				break;
			}
		}
	}
	if (dsn_name[0] == 0)
		design_name_from_zip(zip_path, dsn_name, sizeof dsn_name);

	snprintf(dsn_dir, sizeof dsn_dir, "%s%c%s", dest_dir, SEP, dsn_name);
#ifdef __MINT__
	(void)mkdir(dsn_dir, 0755);
#else
	(void)mkdir(dsn_dir, 0755);
#endif
	printf("Installing %s\n     into %s\n\n", basename_of(zip_path),
	       dsn_dir);

	/* extract, FLATTENED: module folders are flat, and fan ZIPs vary
	 * between no folder, MOD/, and MOD.DSN/ layouts */
	for (i = 0; i < nfiles; i++) {
		char nm[MAXPATH];
		const char *base;
		mz_zip_reader_get_filename(&za, i, nm, sizeof nm);
		if (mz_zip_reader_is_file_a_directory(&za, i))
			continue;
		base = basename_of(nm);
		if (base[0] == 0 || strstr(nm, ".."))
			continue;
		snprintf(path, sizeof path, "%s%c%s", dsn_dir, SEP, base);
		if (mz_zip_reader_extract_to_file(&za, i, path, 0)) {
			extracted++;
		} else {
			printf("  FAILED to extract %s\n", base);
			failed++;
		}
	}
	mz_zip_reader_end(&za);
	printf("extracted %d file%s\n\n", extracted,
	       extracted == 1 ? "" : "s");

	/* convert every DOS art file in the new folder. A second pass over
	 * the ZIP listing rather than a directory scan: portable, and
	 * exactly the set just written. */
	{
		memset(&za, 0, sizeof za);
		if (!mz_zip_reader_init_file(&za, zip_path, 0)) {
			printf("cannot re-open ZIP\n");
			return 1;
		}
		nfiles = mz_zip_reader_get_num_files(&za);
		for (i = 0; i < nfiles; i++) {
			char nm[MAXPATH];
			const char *base;
			int r;
			mz_zip_reader_get_filename(&za, i, nm, sizeof nm);
			if (mz_zip_reader_is_file_a_directory(&za, i))
				continue;
			base = basename_of(nm);
			if (!ends_with_ci(base, ".tlb"))
				continue;
			snprintf(path, sizeof path, "%s%c%s",
				 dsn_dir, SEP, base);
			printf("  %s...", base);
			fflush(stdout);
			r = convert_one(path);
			if (r >= 1) {
				converted++;
				printf(" colour%s\n",
				       r == 2 ? " + mono" : "");
			} else if (r == 0) {
				printf(" already Mac art, kept\n");
			} else {
				failed++;
				printf(" FAILED\n");
			}
			if (r == 2)
				monoed++;
		}
		mz_zip_reader_end(&za);
	}

	printf("\nDone: %d extracted, %d art file%s converted "
	       "(%d with mono), %d failure%s.\n",
	       extracted, converted, converted == 1 ? "" : "s",
	       monoed, failed, failed == 1 ? "" : "s");
	printf("The module is ready: pick \"%s\" with SELECT A DESIGN.\n",
	       dsn_name);
#ifdef __MINT__
	printf("\n-- press RETURN to exit --\n");
	getchar();
#endif
	return failed ? 1 : 0;
}
