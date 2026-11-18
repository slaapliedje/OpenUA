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
#include "modinst.h"

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

#define MAXPATH 512

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

static void print_line(void *ctx, const char *line)
{
	(void)ctx;
	printf("%s\n", line);
}


static int installer_main(int argc, char **argv)
{
	char zip_path[MAXPATH] = "", dest_dir[MAXPATH] = ".";
	char err[160];
	mz_zip_archive za;
	mz_uint i;

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

	{
		int rc = mi_install(zip_path, dest_dir, print_line, 0, 0, 0);
#ifdef __MINT__
		printf("\n-- press RETURN to exit --\n");
		getchar();
#endif
		return rc;
	}
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
