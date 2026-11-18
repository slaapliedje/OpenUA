/* modinst — install one module archive into an OpenUA folder.
 *
 * The whole of an install, shared by uainst (installer/main.c) and the
 * on-machine module manager: the archive's plan (installer/modcore.c), the
 * files unpacked into <DESIGN>.NEW, DOS art converted to the Mac form the
 * engine reads (with the 1-bit synthesis for the mono families), the module's
 * own music made into MUSIC.SLB, and the folder moved into place with the
 * player's saved games carried across.
 *
 * Progress is reported a line at a time through a callback, so a console
 * front end prints it and a window shows it. Portable C99 over stdio.
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#define MINIZ_HEADER_FILE_ONLY
#include "miniz.c"

#include "../src/convert/artconv.h"
#include "../src/convert/xmi2slb.h"
#include "modcore.h"
#include "modinst.h"

#ifdef __MINT__
#define SEP '\\'
#else
#define SEP '/'
#endif

#define SCRATCH_CAP (512L * 1024)
#define MAXPATH 512

static mi_log  g_log;
static void   *g_log_ctx;

/* Report progress: pieces accumulate until a newline, then go out a line
 * at a time (without the newline). */
static void say(const char *fmt, ...)
{
	static char line[400];
	static size_t used;
	char piece[400];
	const char *p;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(piece, sizeof piece, fmt, ap);
	va_end(ap);
	for (p = piece; *p; p++) {
		if (*p == '\n') {
			line[used] = 0;
			if (g_log)
				g_log(g_log_ctx, line);
			used = 0;
		} else if (used + 1 < sizeof line) {
			line[used++] = *p;
		}
	}
}

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
		say("  SKIP %s (convert error %ld)\n",
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
			say("  NOTE %s: mono synthesis failed (%ld); "
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
		say("Replaced the earlier install; saved games kept.\n");
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



int mi_install(const char *zip_path, const char *dest_dir, mi_log log, void *ctx,
               char *dsn_out, unsigned dsn_cap)
{
	char dsn_dir[MAXPATH], dsn_name[16], path[MAXPATH], work_dir[MAXPATH];
	char err[160];
	mz_zip_archive za;
	mz_uint i;
	int extracted = 0, converted = 0, monoed = 0, failed = 0;

	g_log = log;
	g_log_ctx = ctx;
	if (dsn_cap)
		dsn_out[0] = 0;
	if (!g_scratch)
		g_scratch = malloc(SCRATCH_CAP);
	if (!g_plan)
		g_plan = malloc(sizeof *g_plan);
	if (!g_scratch || !g_plan) {
		say("out of memory\n");
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
			say("cannot open %s\n", zip_path);
			return 1;
		}
		kind = mc_kind(head, (unsigned long)got);
		if (kind != MC_ZIP) {
			say("%s is %s.\n", basename_of(zip_path), mc_kind_text(kind));
			return 1;
		}
	}

	memset(&za, 0, sizeof za);
	if (!mz_zip_reader_init_file(&za, zip_path, 0)) {
		say("cannot open ZIP: %s\n", zip_path);
		return 1;
	}
	if (!mc_plan_zip(&za, basename_of(zip_path), g_plan, err, sizeof err)) {
		say("%s: %s\n", basename_of(zip_path), err);
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
	say("Installing %s\n", g_plan->title[0] ? g_plan->title : basename_of(zip_path));
	say("     from %s\n     into %s\n\n", basename_of(zip_path), dsn_dir);

	/* Unpack into <DESIGN>.NEW first; the design folder itself is only
	 * touched once everything is in, so a failure leaves the old install
	 * (or nothing) behind, never half of one. */
	remove_tree(work_dir);
	if (mkdir(work_dir, 0755) != 0) {
		say("cannot create %s\n", work_dir);
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
			say("  FAILED: %s\n", buf ? g_plan->file[i].dest : err);
			free(buf);
			mz_zip_reader_end(&za);
			remove_tree(work_dir);
			say("\nNothing was installed.\n");
			return 1;
		}
		free(buf);
		extracted++;
	}
	for (i = 0; i < (mz_uint)g_plan->nskip; i++) {
		char nm[MAXPATH];

		mz_zip_reader_get_filename(&za, g_plan->skip[i].index, nm, sizeof nm);
		say("  left out: %s (%s)\n", nm, mc_skip_text(g_plan->skip[i].why));
	}
	mz_zip_reader_end(&za);
	say("unpacked %d file%s\n\n", extracted, extracted == 1 ? "" : "s");

	/* convert every DOS art file just written */
	for (i = 0; i < (mz_uint)g_plan->nfiles; i++) {
		const char *base = g_plan->file[i].dest;
		int r;

		if (!ends_with_ci(base, ".tlb") || strchr(base, '/'))
			continue;
		path_join(path, sizeof path, work_dir, base);
		say("  %s...", base);
				r = convert_one(path);
		if (r >= 1) {
			converted++;
			say(" colour%s\n", r == 2 ? " + mono" : "");
		} else if (r == 0) {
			say(" already Mac art, kept\n");
		} else {
			failed++;
			say(" FAILED\n");
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
			say("  music: %s arrangement, %d of 3 song%s...",
			       drvname[chosen], nsongs, nsongs == 1 ? "" : "s");
						out = bank ? xmi2slb_bank(xmi, len, bank, cap, g_scratch, SCRATCH_CAP)
				   : XMI2SLB_ERR_SPACE;
			if (out > 0) {
				FILE *f;
				path_join(path, sizeof path, work_dir, "MUSIC.SLB");
				f = fopen(path, "wb");
				if (f && fwrite(bank, 1, out, f) == (size_t)out) {
					say(" MUSIC.SLB (%ld bytes)%s\n", out,
					       nsongs < 3 ? " - partial set, the rest play silence" : "");
				} else {
					say(" FAILED to write MUSIC.SLB\n");
					failed++;
				}
				if (f)
					fclose(f);
			} else {
				say(" FAILED (convert error %ld)\n", out);
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
		say("\nCould not move the new install into %s.\n", dsn_dir);
		return 1;
	}

	say("\nDone: %d extracted, %d art file%s converted "
	       "(%d with mono), %d failure%s.\n",
	       extracted, converted, converted == 1 ? "" : "s",
	       monoed, failed, failed == 1 ? "" : "s");
	say("The module is ready: pick \"%s\" with SELECT A DESIGN.\n",
	       dsn_name);
	if (dsn_cap)
		snprintf(dsn_out, dsn_cap, "%s", dsn_name);
	return failed ? 1 : 0;
}
