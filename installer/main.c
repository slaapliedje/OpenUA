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
#include "../src/convert/xmi2slb.h"
#include "rsrc_from_dos.h"
#include "modcore.h"
#include <dirent.h>
#include <unistd.h>

#ifdef __MINT__
#define SEP '\\'
#else
#define SEP '/'
#endif

/* The native file picker, when uainst is run with no ZIP argument: the ASL
 * requesters on the Amiga (installer/asl_amiga.c), the AES file selector on
 * the Atari (installer/fsel_atari.c). Same contract on both — returns 1 if a
 * ZIP was chosen, leaves `dest` alone unless a folder was picked too, and
 * returns 0 when the windowing system is unavailable so the console prompt
 * below still gets a chance. UAINST_GUI is defined by the Makefile for the
 * builds that link one; the host build has neither. */
#ifdef UAINST_GUI
int uainst_gui_pick(char *zip, size_t zipcap, char *dest, size_t destcap);
#endif
#ifdef __amigaos__
/* installer/asl_amiga.c — runs fn(argc,argv) on a large StackSwap'd stack;
 * the Shell/Workbench default stack is too small for miniz + asl.library. */
int uainst_run_big_stack(int (*fn)(int, char **), int argc, char **argv);
#endif

#define SCRATCH_CAP (512L * 1024)
#define MAXPATH 512

static unsigned char *g_scratch;
static mc_plan *g_plan;

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

/* Join a directory and a leaf name. Inserts SEP unless the directory already
 * ends in SEP or a ':' volume/drive marker — so an Amiga volume root ("DH0:")
 * or Atari drive ("C:") yields "DH0:leaf", not the malformed "DH0:/leaf".
 * A dir of "" or "." means the current directory and yields the bare leaf:
 * AmigaDOS has no "." cwd notation, so "./leaf" would fail there (this is why
 * an empty ASL drawer selection must not become "./..."). */
static void path_join(char *out, size_t cap, const char *dir, const char *leaf)
{
	size_t dl = strlen(dir);
	if (dl == 0 || (dl == 1 && dir[0] == '.'))
		snprintf(out, cap, "%s", leaf);
	else if (dir[dl - 1] == SEP || dir[dl - 1] == ':')
		snprintf(out, cap, "%s%s", dir, leaf);
	else
		snprintf(out, cap, "%s%c%s", dir, SEP, leaf);
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

/* A planned destination ("GAME001.DAT", "SAVE/SAVGAMA.CSV") under `dir`. */
static void dest_path(char *out, size_t cap, const char *dir, const char *dest)
{
	char sub[MAXPATH];
	const char *slash = strchr(dest, '/');

	if (!slash) {
		path_join(out, cap, dir, dest);
		return;
	}
	snprintf(sub, sizeof sub, "%.*s", (int)(slash - dest), dest);
	path_join(out, cap, dir, sub);
	snprintf(sub, sizeof sub, "%s", out);
	path_join(out, cap, sub, slash + 1);
}

/* Delete a folder and everything in it (one level of subfolders, which is
 * all a design has). A missing folder is fine. */
static void remove_tree(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	char path[MAXPATH];

	if (!d)
		return;
	while ((e = readdir(d)) != 0) {
		DIR *sub;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		path_join(path, sizeof path, dir, e->d_name);
		if ((sub = opendir(path)) != 0) {
			struct dirent *f;
			char inner[MAXPATH];

			while ((f = readdir(sub)) != 0) {
				if (!strcmp(f->d_name, ".") || !strcmp(f->d_name, ".."))
					continue;
				path_join(inner, sizeof inner, path, f->d_name);
				remove(inner);
			}
			closedir(sub);
			rmdir(path);
		} else {
			remove(path);
		}
	}
	closedir(d);
	rmdir(dir);
}

/* The folder in `parent` whose name matches `name` regardless of case, or
 * 0. (AmigaDOS and GEMDOS ignore case anyway; the host test build does not.) */
static int find_ci(const char *parent, const char *name, char *out, size_t cap)
{
	DIR *d = opendir(parent[0] ? parent : ".");
	struct dirent *e;
	int hit = 0;

	if (!d)
		return 0;
	while (!hit && (e = readdir(d)) != 0) {
		size_t k;

		for (k = 0; name[k] && toupper((unsigned char)e->d_name[k])
		                       == toupper((unsigned char)name[k]); k++)
			;
		if (name[k] == 0 && e->d_name[k] == 0) {
			path_join(out, cap, parent, e->d_name);
			hit = 1;
		}
	}
	closedir(d);
	return hit;
}

static int copy_file(const char *from, const char *to)
{
	long n = 0;
	unsigned char *b = read_file(from, &n);
	int ok = b != 0 && write_file(to, b, n);

	free(b);
	return ok;
}

/* Move the finished <DESIGN>.NEW over <DESIGN>.DSN, carrying the player's
 * saved games across. Renames the folder when the file system can; moves
 * the files one by one when it cannot (old GEMDOS will not rename one). */
static int finish_install(const char *work, const char *final_dir)
{
	char old[MAXPATH], from[MAXPATH], to[MAXPATH], parent[MAXPATH];
	const char *leaf = basename_of(final_dir);
	DIR *d;
	struct dirent *e;

	snprintf(parent, sizeof parent, "%.*s", (int)(leaf - final_dir), final_dir);
	if (parent[0] && (parent[strlen(parent) - 1] == SEP))
		parent[strlen(parent) - 1] = 0;
	if (find_ci(parent, leaf, old, sizeof old)) {
		char save[MAXPATH], wsave[MAXPATH];

		path_join(save, sizeof save, old, "SAVE");
		path_join(wsave, sizeof wsave, work, "SAVE");
		if ((d = opendir(save)) != 0) {
			while ((e = readdir(d)) != 0) {
				char up[64];
				size_t k;

				if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
					continue;
				for (k = 0; e->d_name[k] && k < sizeof up - 1; k++)
					up[k] = (char)toupper((unsigned char)e->d_name[k]);
				up[k] = 0;
				path_join(from, sizeof from, save, e->d_name);
				path_join(to, sizeof to, wsave, up);
				(void)copy_file(from, to);
			}
			closedir(d);
		}
		remove_tree(old);
		printf("Replaced the earlier install; saved games kept.\n");
	}
	if (rename(work, final_dir) == 0)
		return 1;
	if (mkdir(final_dir, 0755) != 0)
		return 0;
	path_join(to, sizeof to, final_dir, "SAVE");
	(void)mkdir(to, 0755);
	if ((d = opendir(work)) == 0)
		return 0;
	while ((e = readdir(d)) != 0) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")
		 || !strcmp(e->d_name, "SAVE"))
			continue;
		path_join(from, sizeof from, work, e->d_name);
		path_join(to, sizeof to, final_dir, e->d_name);
		if (rename(from, to) != 0 && !copy_file(from, to)) {
			closedir(d);
			return 0;
		}
	}
	closedir(d);
	{
		char wsave[MAXPATH], fsave[MAXPATH];

		path_join(wsave, sizeof wsave, work, "SAVE");
		path_join(fsave, sizeof fsave, final_dir, "SAVE");
		if ((d = opendir(wsave)) != 0) {
			while ((e = readdir(d)) != 0) {
				if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
					continue;
				path_join(from, sizeof from, wsave, e->d_name);
				path_join(to, sizeof to, fsave, e->d_name);
				if (rename(from, to) != 0)
					(void)copy_file(from, to);
			}
			closedir(d);
		}
	}
	remove_tree(work);
	return 1;
}


static int installer_main(int argc, char **argv)
{
	char zip_path[MAXPATH] = "", dest_dir[MAXPATH] = ".";
	char dsn_dir[MAXPATH], dsn_name[16], path[MAXPATH], work_dir[MAXPATH];
	char err[160];
	mz_zip_archive za;
	mz_uint i;
	int extracted = 0, converted = 0, monoed = 0, failed = 0;

	/* unbuffered: progress lines must appear as they happen on the
	 * Atari/Amiga consoles (and under emulator/pipe capture) */
	setvbuf(stdout, NULL, _IONBF, 0);

	/* uainst --plan ZIP: what installing it would do, machine-readable
	 * (the test suite holds it to tools/modman). Nothing is written. */
	if (argc == 3 && strcmp(argv[1], "--plan") == 0) {
		unsigned char head[64];
		size_t got = 0;
		FILE *f = fopen(argv[2], "rb");
		int kind;

		g_plan = malloc(sizeof *g_plan);
		if (f) {
			got = fread(head, 1, sizeof head, f);
			fclose(f);
		}
		kind = mc_kind(head, (unsigned long)got);
		if (!f || !g_plan || kind != MC_ZIP) {
			printf("%s\n", f ? mc_kind_text(kind) : "cannot open");
			return 1;
		}
		memset(&za, 0, sizeof za);
		if (!mz_zip_reader_init_file(&za, argv[2], 0)
		 || !mc_plan_zip(&za, basename_of(argv[2]), g_plan, err, sizeof err)) {
			printf("%s\n", err);
			return 1;
		}
		printf("%s\t%s\n", g_plan->design, g_plan->title);
		for (i = 0; i < (mz_uint)g_plan->nfiles; i++)
			printf("F %s\n", g_plan->file[i].dest);
		for (i = 0; i < (mz_uint)g_plan->nskip; i++) {
			char nm[MAXPATH], *c;

			mz_zip_reader_get_filename(&za, g_plan->skip[i].index, nm, sizeof nm);
			for (c = nm; *c; c++)
				if (*c == '\\')
					*c = '/';
			printf("S %s\n", nm);
		}
		mz_zip_reader_end(&za);
		return 0;
	}

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
#ifdef UAINST_GUI
		/* No ZIP argument: pop the native picker — ASL requesters on
		 * the Amiga (task #24), the AES file selector on the Atari.
		 * If the windowing system will not answer, fall through to
		 * the console prompt below. */
		if (zip_path[0] == 0)
			(void)uainst_gui_pick(zip_path, sizeof zip_path,
					      dest_dir, sizeof dest_dir);
#endif
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
		printf("       uainst CKIT.EXE [destination-dir]   "
		       "(build frua.rsc from a DOS install)\n");
		return 1;
	}

	/* ★ CKIT.EXE instead of a ZIP: build frua.rsc from the DOS release,
	 * here, on this machine (ADR-0017). Dispatching on the extension is
	 * deliberate — it means dragging CKIT.EXE onto UAINST.TTP just works,
	 * exactly as dragging a module ZIP does, and the GUI picker needs no
	 * new mode. Nothing else in the fan corpus is a .EXE, so there is no
	 * ambiguity to resolve. */
	if (ends_with_ci(zip_path, ".exe")) {
		char out[MAXPATH], msg[256];
		int rc;

		path_join(out, sizeof out, dest_dir, "frua.rsc");
		printf("Building frua.rsc from %s\n", zip_path);
		printf("(verifying - the DOS executable is 574 KB, this takes "
		       "a moment on a 68000)\n");
		rc = uainst_rsrc_from_dos(zip_path, out, msg, sizeof msg);
		printf("%s\n", msg);
		if (rc == UAINST_RSRC_WRONGBUILD)
			printf("Only the v1.2 CKIT.EXE (GOG/Steam, and the "
			       "1993 retail disks) matches the string map.\n");
		return rc == UAINST_RSRC_OK ? 0 : 1;
	}

	g_scratch = malloc(SCRATCH_CAP);
	g_plan = malloc(sizeof *g_plan);
	if (!g_scratch || !g_plan) {
		printf("out of memory\n");
		return 1;
	}

	/* What the file really is: half of one downloaded test set was saved
	 * web error pages renamed .zip, and a StuffIt or LHA file is no zip. */
	{
		unsigned char head[64];
		size_t got = 0;
		FILE *f = fopen(zip_path, "rb");
		int kind;

		if (f) {
			got = fread(head, 1, sizeof head, f);
			fclose(f);
		}
		if (!f) {
			printf("cannot open %s\n", zip_path);
			return 1;
		}
		kind = mc_kind(head, (unsigned long)got);
		if (kind != MC_ZIP) {
			printf("%s is %s.\n", basename_of(zip_path), mc_kind_text(kind));
			return 1;
		}
	}

	memset(&za, 0, sizeof za);
	if (!mz_zip_reader_init_file(&za, zip_path, 0)) {
		printf("cannot open ZIP: %s\n", zip_path);
		return 1;
	}
	if (!mc_plan_zip(&za, basename_of(zip_path), g_plan, err, sizeof err)) {
		printf("%s: %s\n", basename_of(zip_path), err);
		mz_zip_reader_end(&za);
		return 1;
	}
	snprintf(dsn_name, sizeof dsn_name, "%s.DSN", g_plan->design);
	path_join(dsn_dir, sizeof dsn_dir, dest_dir, dsn_name);
	{
		char tmp_name[16];

		snprintf(tmp_name, sizeof tmp_name, "%s.NEW", g_plan->design);
		path_join(work_dir, sizeof work_dir, dest_dir, tmp_name);
	}
	printf("Installing %s\n", g_plan->title[0] ? g_plan->title : basename_of(zip_path));
	printf("     from %s\n     into %s\n\n", basename_of(zip_path), dsn_dir);

	/* Unpack into <DESIGN>.NEW first; the design folder itself is only
	 * touched once everything is in, so a failure leaves the old install
	 * (or nothing) behind, never half of one. */
	remove_tree(work_dir);
	if (mkdir(work_dir, 0755) != 0) {
		printf("cannot create %s\n", work_dir);
		mz_zip_reader_end(&za);
		return 1;
	}
	path_join(path, sizeof path, work_dir, "SAVE");
	(void)mkdir(path, 0755);
	for (i = 0; i < (mz_uint)g_plan->nfiles; i++) {
		unsigned long len = 0;
		unsigned char *buf = mc_extract(&za, g_plan->file[i].index, &len,
		                                err, sizeof err);

		dest_path(path, sizeof path, work_dir, g_plan->file[i].dest);
		if (!buf || !write_file(path, buf, (long)len)) {
			printf("  FAILED: %s\n", buf ? g_plan->file[i].dest : err);
			free(buf);
			mz_zip_reader_end(&za);
			remove_tree(work_dir);
			printf("\nNothing was installed.\n");
			return 1;
		}
		free(buf);
		extracted++;
	}
	for (i = 0; i < (mz_uint)g_plan->nskip; i++) {
		char nm[MAXPATH];

		mz_zip_reader_get_filename(&za, g_plan->skip[i].index, nm, sizeof nm);
		printf("  left out: %s (%s)\n", nm, mc_skip_text(g_plan->skip[i].why));
	}
	mz_zip_reader_end(&za);
	printf("unpacked %d file%s\n\n", extracted, extracted == 1 ? "" : "s");

	/* convert every DOS art file just written */
	for (i = 0; i < (mz_uint)g_plan->nfiles; i++) {
		const char *base = g_plan->file[i].dest;
		int r;

		if (!ends_with_ci(base, ".tlb") || strchr(base, '/'))
			continue;
		path_join(path, sizeof path, work_dir, base);
		printf("  %s...", base);
		fflush(stdout);
		r = convert_one(path);
		if (r >= 1) {
			converted++;
			printf(" colour%s\n", r == 2 ? " + mono" : "");
		} else if (r == 0) {
			printf(" already Mac art, kept\n");
		} else {
			failed++;
			printf(" FAILED\n");
		}
		if (r == 2)
			monoed++;
	}
	/* THE MODULE'S OWN MUSIC. A DOS fan module ships its soundtrack as .XMI
	 * and the engine resolves MUSIC.SLB design-first (ADR-0011 applied to
	 * music), so the bank belongs in the design folder. Until this pass the
	 * conversion was host-side Python only, and a module installed here
	 * played the BASE game's music — working audio, wrong soundtrack.
	 * Same core the PC tool mirrors (src/convert/xmi2slb.c, byte-identical
	 * by test); driver preference Tandy > PC > Roland > AdLib > DQK, and a
	 * partial set converts what exists (the rest play silence). */
	{
		/* best[drv][q-1] = basename of that driver's song q, or empty */
		static char best[XMI2SLB_NDRV][3][MAXPATH];
		int have[XMI2SLB_NDRV] = { 0, 0, 0, 0, 0 };
		int drv, q, chosen = -1;

		for (i = 0; i < (mz_uint)g_plan->nfiles; i++) {
			const char *base = g_plan->file[i].dest;

			if (strchr(base, '/') || !xmi2slb_classify(base, &drv, &q))
				continue;
			if (q < 1 || q > 3)		/* only Q1..Q3 map to Mac slots */
				continue;
			strcpy(best[drv][q - 1], base);
			have[drv] = 1;
		}
		for (drv = 0; drv < XMI2SLB_NDRV; drv++)
			if (have[drv]) { chosen = drv; break; }

		if (chosen >= 0) {
			static const char *drvname[XMI2SLB_NDRV] =
				{ "Tandy", "PC speaker", "Roland", "AdLib", "DQK" };
			const unsigned char *xmi[3] = { 0, 0, 0 };
			unsigned char *own[3] = { 0, 0, 0 };
			long len[3] = { 0, 0, 0 }, out, cap = 64L * 1024;
			unsigned char *bank = malloc(cap);
			int nsongs = 0;

			for (q = 0; q < 3; q++) {
				if (best[chosen][q][0] == 0)
					continue;
				path_join(path, sizeof path, work_dir, best[chosen][q]);
				own[q] = read_file(path, &len[q]);
				if (own[q]) { xmi[q] = own[q]; nsongs++; }
			}
			printf("  music: %s arrangement, %d of 3 song%s...",
			       drvname[chosen], nsongs, nsongs == 1 ? "" : "s");
			fflush(stdout);
			out = bank ? xmi2slb_bank(xmi, len, bank, cap, g_scratch, SCRATCH_CAP)
				   : XMI2SLB_ERR_SPACE;
			if (out > 0) {
				FILE *f;
				path_join(path, sizeof path, work_dir, "MUSIC.SLB");
				f = fopen(path, "wb");
				if (f && fwrite(bank, 1, out, f) == (size_t)out) {
					printf(" MUSIC.SLB (%ld bytes)%s\n", out,
					       nsongs < 3 ? " - partial set, the rest play silence" : "");
				} else {
					printf(" FAILED to write MUSIC.SLB\n");
					failed++;
				}
				if (f)
					fclose(f);
			} else {
				printf(" FAILED (convert error %ld)\n", out);
				failed++;
			}
			for (q = 0; q < 3; q++)
				free(own[q]);
			free(bank);
		}
	}

	/* Everything is in: put it in place. The player's saved games come
	 * across from the old install, over any the module ships. */
	/* (An art or music file that would not convert is reported above but
	 * does not stop the install: the engine converts art again when it is
	 * first shown, and plays the base music without a bank.) */
	if (!finish_install(work_dir, dsn_dir)) {
		printf("\nCould not move the new install into %s.\n", dsn_dir);
		return 1;
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

int main(int argc, char **argv)
{
#ifdef __amigaos__
	/* The whole job (ASL requesters + miniz + artconv) needs far more stack
	 * than a Shell/Workbench launch grants; run it on a big StackSwap'd
	 * stack (installer/asl_amiga.c). Atari and host have ample stacks. */
	return uainst_run_big_stack(installer_main, argc, argv);
#else
	return installer_main(argc, argv);
#endif
}
