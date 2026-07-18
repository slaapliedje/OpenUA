/* asl_amiga.c — the Amiga asl.library FileRequester frontend for uainst
 * (task #24). Compiled ONLY into the Amiga build (uainst_amiga); the
 * portable core in main.c stays free of Amiga headers.
 *
 * When uainst is launched with no ZIP argument (double-clicked, or run
 * bare from a Shell), main() calls uainst_gui_pick(): it pops the two
 * standard ASL requesters — "pick the module ZIP", then "pick the
 * destination drawer" — and hands the joined paths back. The progress
 * output still goes to the launching Shell's console, exactly as the
 * command-line path; this only replaces the two typed-in path prompts.
 *
 * asl.library v37 (Workbench 2.0) is the floor; if it will not open we
 * return 0 and main() falls back to the console prompt, so a 1.3 box or
 * a redirected run is never left without a path.
 */

#ifdef __amigaos__

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <libraries/asl.h>
#include <proto/exec.h>
#include <proto/asl.h>
#include <proto/dos.h>

#include <stddef.h>
#include <string.h>

struct Library *AslBase;               /* opened/closed here; inline/asl.h uses it */

/* --- stack floor -----------------------------------------------------------
 *
 * A Shell- or Workbench-launched program inherits a small stack (~4 KB
 * default). That is far too small for uainst: miniz's extract path puts a
 * ~12 KB tinfl_decompressor on the stack (mz_zip_reader_extract_to_mem_no_alloc)
 * and asl.library renders its requesters on the calling task's stack too.
 * Either one overflows 4 KB — the ZIP extract hung, and the ASL requester
 * crashed with a #80000003 / #80000004 (return smashed into a string constant).
 *
 * The `unsigned long __stack` cookie is INERT with this toolchain's ncrt0
 * (-noixemul) — it references no such symbol, so the engine's copy in
 * sys_amiga.c is equally a no-op; frua only survives because its per-frame use
 * stays under 4 KB. uainst does not, so it runs the whole job on a large
 * AllocMem'd stack via StackSwap.
 *
 * StackSwap moves the stack pointer out from under the compiler, so nothing
 * the trampoline touches between the two swaps may be an sp-relative local
 * (this TU builds with -fomit-frame-pointer). Everything it uses is therefore
 * static (absolute-addressed); the worker's args and result live in statics.
 */
#define UAINST_STACK (256UL * 1024)

static struct StackSwapStruct s_sss;
static int (*s_fn)(int, char **);
static int   s_argc;
static char **s_argv;
static int   s_result;

/* StackSwap moves sp; the epilogue must restore it frame-relative (via a5),
 * not sp-relative, or it returns through the wrong stack — so keep a frame
 * pointer here even though the TU is built -fomit-frame-pointer. */
static void __attribute__((noinline, optimize("no-omit-frame-pointer")))
uainst_swap_trampoline(void)
{
	StackSwap(&s_sss);
	s_result = s_fn(s_argc, s_argv);        /* runs on the big stack */
	StackSwap(&s_sss);
}

/* Run fn(argc,argv) on a 256 KB stack. Called by main() on the Amiga. */
__attribute__((optimize("no-omit-frame-pointer")))
int uainst_run_big_stack(int (*fn)(int, char **), int argc, char **argv)
{
	UBYTE *mem = AllocMem(UAINST_STACK, MEMF_ANY);
	if (mem == NULL)
		return fn(argc, argv);          /* no RAM: fall back, may be tight */
	s_fn = fn;
	s_argc = argc;
	s_argv = argv;
	s_sss.stk_Lower   = (APTR)mem;
	s_sss.stk_Upper   = (ULONG)mem + UAINST_STACK;
	s_sss.stk_Pointer = (APTR)(mem + UAINST_STACK);
	uainst_swap_trampoline();
	FreeMem(mem, UAINST_STACK);
	return s_result;
}

/* Fill zip/dest from two ASL requesters. Returns 1 iff a ZIP was chosen
 * (dest is only overwritten when the user also picks a drawer — otherwise
 * main()'s default is left intact). Returns 0 if asl.library is absent or
 * the user cancels the ZIP requester. */
int uainst_gui_pick(char *zip, size_t zipcap, char *dest, size_t destcap)
{
	struct FileRequester *fr;
	int got = 0;

	AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 37);
	if (AslBase == NULL)
		return 0;		/* pre-2.0: caller uses the console prompt */

	/* 1) the module ZIP (miniz reads ZIP only, so filter to #?.zip) */
	fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
		ASLFR_TitleText,      (ULONG)"Select fan-module ZIP",
		ASLFR_PositiveText,   (ULONG)"Install",
		ASLFR_InitialPattern, (ULONG)"#?.zip",
		ASLFR_DoPatterns,     TRUE,
		TAG_DONE);
	if (fr != NULL) {
		if (AslRequest(fr, NULL) && fr->fr_File != NULL
		    && fr->fr_File[0] != '\0') {
			char tmp[512];
			size_t dl = (fr->fr_Drawer != NULL)
				? strlen((char *)fr->fr_Drawer) : 0;
			if (dl < sizeof tmp) {
				if (dl)
					memcpy(tmp, fr->fr_Drawer, dl);
				tmp[dl] = '\0';
				/* AddPart inserts the right ':' or '/' join */
				if (AddPart((STRPTR)tmp, fr->fr_File,
					    (ULONG)sizeof tmp)) {
					strncpy(zip, tmp, zipcap - 1);
					zip[zipcap - 1] = '\0';
					got = 1;
				}
			}
		}
		FreeAslRequest(fr);
	}

	/* 2) destination drawer — only asked once a ZIP is in hand */
	if (got) {
		fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
			ASLFR_TitleText,    (ULONG)"Install the design into which drawer?",
			ASLFR_PositiveText, (ULONG)"Install here",
			ASLFR_DrawersOnly,  TRUE,
			TAG_DONE);
		if (fr != NULL) {
			if (AslRequest(fr, NULL) && fr->fr_Drawer != NULL
			    && fr->fr_Drawer[0] != '\0') {
				strncpy(dest, (char *)fr->fr_Drawer, destcap - 1);
				dest[destcap - 1] = '\0';
			}
			FreeAslRequest(fr);
		}
	}

	CloseLibrary(AslBase);
	AslBase = NULL;
	return got;
}

#endif /* __amigaos__ */
