/* fsel_atari.c — the GEM/AES file-selector frontend for uainst.
 *
 * The Atari counterpart of installer/asl_amiga.c, and the reason it exists:
 * the installer already shipped on both machines, but only the Amiga build
 * had a native picker. Launched with no ZIP argument, the Amiga popped two
 * ASL requesters while the Atari dropped to a typed console prompt — you had
 * to know the path. This closes that gap with the AES file selector, so
 * double-clicking UAINST.PRG on either machine gives you the same "point at
 * the ZIP, point at the folder" flow.
 *
 * No GEM headers ship with the m68k-atari-mint toolchain, so the AES binding
 * is hand-rolled exactly like platform/vdi.c does for the VDI: a parameter
 * block of six pointers, its address in d1, the magic in d0, `trap #2`.
 * AES uses 0xC8 (200) where the VDI uses 0x73.
 *
 * contrl layout (words):
 *   [0] opcode  [1] #intin  [2] #intout  [3] #addrin  [4] #addrout
 *
 * ★ THIS MUST BE A .PRG, NOT A .TTP. The desktop launches .TOS/.TTP
 * programs as TOS applications — it tears down the GEM screen first, so the
 * selector would draw over a text mode with no mouse. A .PRG is launched as
 * a GEM application with the AES alive underneath it, which is what the
 * selector needs. The .TTP is still built and shipped for drag-and-drop and
 * for the argument form; see the Makefile.
 */

#if !defined(__amigaos__) && !defined(UAINST_HOST)

#include <stddef.h>
#include <string.h>
#include <mint/osbind.h>        /* Dgetdrv / Dgetpath */

/* --- raw AES binding -------------------------------------------------- */

static short aes_contrl[5];
static short aes_global[16];
static short aes_intin[16];
static short aes_intout[16];
static long  aes_addrin[8];
static long  aes_addrout[8];

static void *const aes_pb[6] = {
	aes_contrl, aes_global, aes_intin, aes_intout, aes_addrin, aes_addrout
};

/* The trap. d1 = &parameter block, d0 = 0xC8 (200), `trap #2`.
 *
 * ★ THE REGISTERS ARE LOADED INSIDE THE TEMPLATE, DELIBERATELY. The obvious
 * spelling — the one platform/vdi.c uses —
 *
 *     register long        d0 asm("d0") = 0xC8L;
 *     register void *const d1 asm("d1") = (void *)aes_pb;
 *     __asm__ volatile ("trap #2" : "+r"(d0) : "r"(d1) : ...);
 *
 * MISCOMPILES HERE, and does it silently. A local `register ... asm("d1")`
 * only binds the register where the constraint also demands it, and "r" is
 * satisfied by any general register — so once this function was inlined into
 * uainst_gui_pick, GCC
 *
 *     lea _aes_pb,%a6
 *     trap #2
 *
 * put the block in a6 and left d1 holding whatever was there. Every AES call
 * from that function raised "Illegal AES function call". The out-of-line copy
 * (fsel_ask) got `move.l #_aes_pb,%d1` and worked, which is what made it look
 * like a bad opcode rather than a bad binding — and adding a printf between
 * the calls "fixed" it, because the call clobbered the register and forced a
 * reload.
 *
 * The "a" constraint pins the operand to an ADDRESS register, so it cannot
 * collide with the d0/d1 clobbers, and the two loads are then unambiguous.
 * Nothing here reads the trap's return: every AES result comes back in
 * intout[]/global[]. */
static void aes_trap(void)
{
	void *const pb = (void *)aes_pb;

	__asm__ volatile ("move.l %0,%%d1\n\t"
	                  "move.w #200,%%d0\n\t"
	                  "trap   #2"
	                  :
	                  : "a" (pb)
	                  : "d0", "d1", "d2", "a0", "a1", "a2", "cc", "memory");
}

static void aes(short op, short nintin, short nintout,
                short naddrin, short naddrout)
{
	aes_contrl[0] = op;
	aes_contrl[1] = nintin;
	aes_contrl[2] = nintout;
	aes_contrl[3] = naddrin;
	aes_contrl[4] = naddrout;
	aes_trap();
}

#define AES_APPL_INIT     10
#define AES_APPL_EXIT     19
#define AES_GRAF_MOUSE    78
#define AES_FSEL_INPUT    90
#define AES_FSEL_EXINPUT  91
#define AES_WIND_UPDATE  107

#define MOUSE_ARROW        0
#define BEG_UPDATE         1
#define END_UPDATE         0

/* --- the selector ------------------------------------------------------ */

/* AES path buffers are a fixed 128 bytes by convention; the selector writes
 * back into them, so they must be that big whatever we put in. */
#define FSEL_PATH 128
#define FSEL_NAME 128

static short s_aes_version;

/* "C:\GAMES\*.ZIP" from the current drive and directory. */
static void fsel_seed_path(char *path, const char *pattern)
{
	char cwd[96];
	short drv = (short)Dgetdrv();
	size_t n;

	cwd[0] = '\0';
	Dgetpath(cwd, (short)(drv + 1));

	path[0] = (char)('A' + drv);
	path[1] = ':';
	path[2] = '\0';
	if (cwd[0] != '\0')
		strncat(path, cwd, FSEL_PATH - 8);
	n = strlen(path);
	if (n == 0 || path[n - 1] != '\\') {
		path[n++] = '\\';
		path[n] = '\0';
	}
	strncat(path, pattern, FSEL_PATH - strlen(path) - 1);
}

/* Join the selector's directory (which comes back carrying the wildcard)
 * with the chosen filename. Returns 0 if the result will not fit. */
static int fsel_join(char *out, size_t cap, const char *path, const char *name)
{
	const char *slash = strrchr(path, '\\');
	size_t dirlen = (slash != NULL) ? (size_t)(slash - path + 1) : 0;

	if (dirlen + strlen(name) >= cap)
		return 0;
	memcpy(out, path, dirlen);
	out[dirlen] = '\0';
	strcat(out, name);
	return 1;
}

/* One selector round. Returns 1 iff the user pressed OK. */
static int fsel_ask(char *path, char *name, const char *label)
{
	aes_addrin[0] = (long)path;
	aes_addrin[1] = (long)name;

	/* fsel_exinput (the one with a title bar) arrived with AES 1.40 —
	 * TOS 1.4. The Falcon and TT are far past that, but a 68000 build
	 * also lands on TOS 1.0/1.2 STs, so fall back rather than trap into
	 * an opcode that release does not implement. */
	if (s_aes_version >= 0x0140) {
		aes_addrin[2] = (long)label;
		aes(AES_FSEL_EXINPUT, 0, 2, 3, 0);
	} else {
		aes(AES_FSEL_INPUT, 0, 2, 2, 0);
	}

	/* intout[0] = call ok, intout[1] = 1 for OK / 0 for Cancel */
	return (aes_intout[0] != 0 && aes_intout[1] != 0);
}

/* Fill zip/dest from two selector rounds. Same contract as the Amiga's
 * uainst_gui_pick: returns 1 iff a ZIP was chosen, and only overwrites dest
 * when the user actually picked a folder, so main()'s default survives a
 * cancel. Returns 0 if the AES is not there, and main() prompts instead. */
int uainst_gui_pick(char *zip, size_t zipcap, char *dest, size_t destcap)
{
	char path[FSEL_PATH], name[FSEL_NAME];
	int got = 0;

	aes(AES_APPL_INIT, 0, 1, 0, 0);
	if (aes_intout[0] < 0)
		return 0;               /* no AES: caller falls back to stdin */
	s_aes_version = aes_global[0];

	/* The selector wants the screen semaphore and a visible pointer —
	 * a TOS-launched parent may well have left the arrow hidden. */
	aes_intin[0] = BEG_UPDATE;
	aes(AES_WIND_UPDATE, 1, 1, 0, 0);
	aes_intin[0] = MOUSE_ARROW;
	aes_addrin[0] = 0L;
	aes(AES_GRAF_MOUSE, 1, 1, 1, 0);

	/* 1) the module ZIP. miniz reads ZIP only, so filter to *.ZIP. */
	fsel_seed_path(path, "*.ZIP");
	name[0] = '\0';
	if (fsel_ask(path, name, "Select fan-module ZIP") && name[0] != '\0')
		got = fsel_join(zip, zipcap, path, name);

	/* 2) the destination folder. GEM has no folders-only selector, so the
	 * idiom is the ordinary one with the FILENAME IGNORED: navigate into
	 * the target folder and press OK. An empty name is therefore success
	 * here, not a cancel — which is the opposite of the round above. */
	if (got) {
		fsel_seed_path(path, "*.*");
		name[0] = '\0';
		if (fsel_ask(path, name, "Open the install folder, then OK")) {
			const char *slash = strrchr(path, '\\');
			size_t dirlen = (slash != NULL)
				? (size_t)(slash - path) : 0;
			/* keep the trailing separator only for a drive root
			 * ("C:\"), which is not a legal path without it */
			if (dirlen == 2 && path[1] == ':')
				dirlen = 3;
			if (dirlen > 0 && dirlen < destcap) {
				memcpy(dest, path, dirlen);
				dest[dirlen] = '\0';
			}
		}
	}

	aes_intin[0] = END_UPDATE;
	aes(AES_WIND_UPDATE, 1, 1, 0, 0);
	aes(AES_APPL_EXIT, 0, 1, 0, 0);
	return got;
}

#endif /* !__amigaos__ && !UAINST_HOST */
