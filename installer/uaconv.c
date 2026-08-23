/* uaconv — OpenUA bulk DOS-art converter (Amiga). OPTIONAL since ADR-0019.
 *
 * The engine converts each DOS `HLIB` .tlb library to the Mac `GLIB` .ctl it
 * loads on FIRST TOUCH, once ever (ADR-0019 re-enabled that on the Amiga;
 * ADR-0015 had barred it). So nothing needs this tool. What it offers: do
 * ALL of the conversion up front in one pass, and reclaim the ~5 MB the
 * now-redundant .tlb originals occupy (`-d`, or answer the prompt). It scans
 * the OpenUA folder and its .DSN design sub-folders.
 *
 *   uaconv DH0:OpenUA          convert everything, ask about deleting
 *   uaconv -d DH0:OpenUA       convert and delete the .tlb without asking
 *
 * Portable C over stdio + the artconv core (src/convert/artconv.c). The only
 * platform split is the directory scan: dos.library Examine/ExNext on the
 * Amiga, POSIX readdir on the host (so the convert path is host-testable).
 * Colour conversion only — no mono synthesis (that is the ST 1-bit build's
 * install-time job, irrelevant to ECS/AGA).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/convert/artconv.h"

#define MAXPATH   256
#define MAX_TLB   512
#define SCRATCH_CAP (512L * 1024)

static unsigned char *g_scratch;
static char  g_tlb[MAX_TLB][MAXPATH];   /* converted .tlb, for the delete pass */
static int   g_ntlb;

#ifdef __amigaos__
int uainst_run_big_stack(int (*fn)(int, char **), int argc, char **argv);
#endif

/* ---- small helpers (mirrors installer/main.c) --------------------------- */

static const char *basename_of(const char *p)
{
	const char *b = p;
	for (; *p; p++)
		if (*p == '/' || *p == ':' || *p == '\\')
			b = p + 1;
	return b;
}

static int ext_is_tlb(const char *name)
{
	size_t n = strlen(name);
	if (n < 4 || name[n - 4] != '.')
		return 0;
	return (name[n - 3] == 't' || name[n - 3] == 'T')
	    && (name[n - 2] == 'l' || name[n - 2] == 'L')
	    && (name[n - 1] == 'b' || name[n - 1] == 'B');
}

static int ext_is_dsn(const char *name)
{
	size_t n = strlen(name);
	if (n < 4 || name[n - 4] != '.')
		return 0;
	return (name[n - 3] == 'd' || name[n - 3] == 'D')
	    && (name[n - 2] == 's' || name[n - 2] == 'S')
	    && (name[n - 1] == 'n' || name[n - 1] == 'N');
}

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

/* Join a dir and a leaf with the right AmigaDOS/host separator. A dir that
 * already ends in ':' or '/' takes no extra separator (volume roots). */
static void path_join(char *out, size_t cap, const char *dir, const char *leaf)
{
	size_t dl = strlen(dir);
	char sep = '/';
	if (dl == 0) { snprintf(out, cap, "%s", leaf); return; }
	if (dir[dl - 1] == ':' || dir[dl - 1] == '/')
		sep = 0;
	if (sep)
		snprintf(out, cap, "%s/%s", dir, leaf);
	else
		snprintf(out, cap, "%s%s", dir, leaf);
}

/* ---- the conversion ----------------------------------------------------- */

/* Convert one .tlb in place: write the .ctl twin. Returns 1 converted,
 * 0 skipped (not HLIB), negative on error. Records the path for the delete
 * pass on success. */
static int convert_one(const char *tlb_path)
{
	unsigned char *src, *dst;
	long n = 0, r, cap;
	char ctl_path[MAXPATH];
	size_t plen = strlen(tlb_path);

	src = read_file(tlb_path, &n);
	if (!src)
		return -1;
	if (n < 4 || memcmp(src, "HLIB", 4) != 0) {
		free(src);
		return 0;			/* already Mac art, or not art */
	}
	cap = n + 4096;
	dst = malloc(cap);
	if (!dst) { free(src); return -1; }

	r = artconv_convert(src, n, dst, cap, g_scratch, SCRATCH_CAP);
	if (r < 0) {
		printf("  SKIP %s (convert error %ld)\n", basename_of(tlb_path), r);
		free(src); free(dst);
		return -2;
	}
	if (plen >= sizeof ctl_path) { free(src); free(dst); return -1; }
	memcpy(ctl_path, tlb_path, plen + 1);
	memcpy(ctl_path + plen - 3, "ctl", 3);		/* keep the 8.3 stem */

	if (!write_file(ctl_path, dst, r)) {
		printf("  FAILED to write %s\n", basename_of(ctl_path));
		free(src); free(dst);
		return -1;
	}
	free(src);
	free(dst);
	if (g_ntlb < MAX_TLB)
		memcpy(g_tlb[g_ntlb++], tlb_path, plen + 1);
	return 1;
}

/* ---- directory scan (platform split) ------------------------------------ */
/* scan_files: convert every .tlb directly in `dir`.
 * for_each_dsn: call `fn(subdir_path)` for every *.DSN sub-folder of `dir`. */

#ifdef __amigaos__
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>

static void scan_files(const char *dir)
{
	BPTR lock = Lock((CONST_STRPTR)dir, ACCESS_READ);
	struct FileInfoBlock *fib;
	if (!lock)
		return;
	fib = (struct FileInfoBlock *)AllocMem(sizeof *fib, MEMF_CLEAR);
	if (fib && Examine(lock, fib)) {
		while (ExNext(lock, fib)) {
			char full[MAXPATH];
			if (fib->fib_DirEntryType > 0)      /* a directory */
				continue;
			if (!ext_is_tlb(fib->fib_FileName))
				continue;
			path_join(full, sizeof full, dir, fib->fib_FileName);
			{
				int rc = convert_one(full);
				if (rc == 1)
					printf("  %s\n", fib->fib_FileName);
			}
		}
	}
	if (fib)
		FreeMem(fib, sizeof *fib);
	UnLock(lock);
}

static void for_each_dsn(const char *dir, void (*fn)(const char *))
{
	BPTR lock = Lock((CONST_STRPTR)dir, ACCESS_READ);
	struct FileInfoBlock *fib;
	if (!lock)
		return;
	fib = (struct FileInfoBlock *)AllocMem(sizeof *fib, MEMF_CLEAR);
	if (fib && Examine(lock, fib)) {
		while (ExNext(lock, fib)) {
			char full[MAXPATH];
			if (fib->fib_DirEntryType <= 0)     /* not a directory */
				continue;
			if (!ext_is_dsn(fib->fib_FileName))
				continue;
			path_join(full, sizeof full, dir, fib->fib_FileName);
			fn(full);
		}
	}
	if (fib)
		FreeMem(fib, sizeof *fib);
	UnLock(lock);
}

static int prompt_yes(const char *q)
{
	int c;
	printf("%s (y/N): ", q);
	fflush(stdout);
	c = getchar();
	while (getchar() != '\n' && !feof(stdin))
		;
	return (c == 'y' || c == 'Y');
}

#else /* host */
#include <dirent.h>
#include <sys/stat.h>

static void scan_files(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	if (!d)
		return;
	while ((e = readdir(d)) != NULL) {
		char full[MAXPATH];
		struct stat st;
		if (!ext_is_tlb(e->d_name))
			continue;
		path_join(full, sizeof full, dir, e->d_name);
		if (stat(full, &st) == 0 && (st.st_mode & S_IFDIR))
			continue;
		if (convert_one(full) == 1)
			printf("  %s\n", e->d_name);
	}
	closedir(d);
}

static void for_each_dsn(const char *dir, void (*fn)(const char *))
{
	DIR *d = opendir(dir);
	struct dirent *e;
	if (!d)
		return;
	while ((e = readdir(d)) != NULL) {
		char full[MAXPATH];
		struct stat st;
		if (!ext_is_dsn(e->d_name))
			continue;
		path_join(full, sizeof full, dir, e->d_name);
		if (stat(full, &st) == 0 && (st.st_mode & S_IFDIR))
			fn(full);
	}
	closedir(d);
}

static int prompt_yes(const char *q)
{
	int c;
	printf("%s (y/N): ", q);
	fflush(stdout);
	c = getchar();
	while (c != '\n' && c != EOF && getchar() != '\n')
		;
	return (c == 'y' || c == 'Y');
}
#endif

/* ---- driver ------------------------------------------------------------- */

static int uaconv_main(int argc, char **argv)
{
	const char *dir = "";
	int i, del = 0;

	/* uaconv [-d] <dir> : -d deletes the .tlb originals without asking
	 * (for the AmigaOS Installer route, which has no interactive stdin;
	 * the plain instdisk/Shell route asks). */
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && (argv[i][1] == 'd' || argv[i][1] == 'D'))
			del = 1;
		else
			dir = argv[i];
	}

	g_scratch = malloc(SCRATCH_CAP);
	if (!g_scratch) {
		printf("uaconv: out of memory\n");
		return 1;
	}
	printf("uaconv - converting OpenUA art in %s\n\n",
	       dir[0] ? dir : "the current directory");

	scan_files(dir);                        /* root libraries */
	for_each_dsn(dir, scan_files);          /* per-design libraries */

	free(g_scratch);
	g_scratch = NULL;

	if (g_ntlb == 0) {
		printf("\nNothing to convert (no DOS .tlb art found).\n");
		return 0;
	}
	printf("\nConverted %d file(s) to .ctl.\n", g_ntlb);

	/* Offer to reclaim the space: the engine reads the .ctl now, the .tlb
	 * are dead weight. -d deletes unprompted; otherwise ask (keep on EOF). */
	if (del || prompt_yes("Delete the original .tlb files")) {
		int removed = 0;
		for (i = 0; i < g_ntlb; i++)
			if (remove(g_tlb[i]) == 0)
				removed++;
		printf("Removed %d original(s).\n", removed);
	} else {
		printf("Kept the .tlb originals.\n");
	}
	printf("\nDone. The Amiga art is ready.\n");
	return 0;
}

int main(int argc, char **argv)
{
#ifdef __amigaos__
	/* artconv wants far more stack than a Shell/WB launch grants; run on a
	 * big StackSwap'd stack (installer/asl_amiga.c, shared with uainst). */
	return uainst_run_big_stack(uaconv_main, argc, argv);
#else
	return uaconv_main(argc, argv);
#endif
}
