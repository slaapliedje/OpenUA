/* gem_atari.c — the GEM front-end for instdisk (INSTDISK.PRG).
 *
 * The .TTP prompts on the console; this gives the .PRG a window with a
 * progress bar, the file selector for the destination folder, and alerts for
 * the disk swaps — the same four calls the console path makes, behind
 * gem_*(). Raw AES/VDI bindings, no library: the same `trap #2` shape as
 * installer/fsel_atari.c, whose comment explains why the registers are loaded
 * INSIDE the asm template (the obvious spelling miscompiles once inlined).
 *
 * No redraw handling on purpose: a TOS-launched installer is the only thing
 * on the desktop, and form_alert restores the screen beneath itself. */
#if !defined(__amigaos__) && defined(INSTDISK_GUI)

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <mint/osbind.h>
#include <sys/stat.h>
#include <unistd.h>

/* --- AES ---------------------------------------------------------------- */
static short aes_contrl[5];
static short aes_global[16];
static short aes_intin[16];
static short aes_intout[16];
static long  aes_addrin[8];
static long  aes_addrout[8];
static void *const aes_pb[6] = {
	aes_contrl, aes_global, aes_intin, aes_intout, aes_addrin, aes_addrout
};

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

static void aes(short op, short nintin, short nintout, short naddrin, short naddrout)
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
#define AES_FORM_DIAL     51
#define AES_FORM_ALERT    52
#define AES_GRAF_HANDLE   77
#define AES_GRAF_MOUSE    78
#define AES_FSEL_INPUT    90
#define AES_FSEL_EXINPUT  91
#define AES_WIND_CREATE  100
#define AES_WIND_OPEN    101
#define AES_WIND_CLOSE   102
#define AES_WIND_DELETE  103
#define AES_WIND_GET     104
#define AES_WIND_SET     105
#define AES_WIND_UPDATE  107

/* --- VDI ---------------------------------------------------------------- */
static short vdi_contrl[12];
static short vdi_intin[128];
static short vdi_ptsin[128];
static short vdi_intout[128];
static short vdi_ptsout[128];
static void *const vdi_pb[5] = {
	vdi_contrl, vdi_intin, vdi_ptsin, vdi_intout, vdi_ptsout
};

static void vdi_trap(void)
{
	void *const pb = (void *)vdi_pb;

	__asm__ volatile ("move.l %0,%%d1\n\t"
	                  "move.w #115,%%d0\n\t"
	                  "trap   #2"
	                  :
	                  : "a" (pb)
	                  : "d0", "d1", "d2", "a0", "a1", "a2", "cc", "memory");
}

static short s_vh;                      /* virtual workstation handle */
static short s_wchar = 8, s_hchar = 16; /* system font cell */
static short s_win = -1;                /* window handle */
static short s_wx, s_wy, s_ww, s_wh;    /* work area */
static short s_aes_version;
static char  s_title[64];

static void vdi(short op, short sub, short nptsin, short nintin)
{
	vdi_contrl[0] = op;
	vdi_contrl[1] = nptsin;
	vdi_contrl[3] = nintin;
	vdi_contrl[5] = sub;
	vdi_contrl[6] = s_vh;
	vdi_trap();
}

static void v_bar(short x1, short y1, short x2, short y2, short color)
{
	/* colour 0 through a SOLID fill came out green on the Falcon (the
	 * autostarted desktop palette); the hollow interior is the GEM idiom
	 * for "clear to background" and is white everywhere. */
	vdi_intin[0] = color;  vdi(25, 0, 0, 1);          /* vsf_color */
	vdi_intin[0] = color ? 1 : 0; vdi(23, 0, 0, 1);   /* solid / hollow */
	vdi_intin[0] = 0;      vdi(104, 0, 0, 1);         /* vsf_perimeter off */
	vdi_ptsin[0] = x1; vdi_ptsin[1] = y1;
	vdi_ptsin[2] = x2; vdi_ptsin[3] = y2;
	vdi(11, 1, 2, 0);                                 /* v_bar (GDP 1) */
}

static void v_gtext(short x, short y, const char *s)
{
	short n = (short)strlen(s), i;

	if (n > 120)
		n = 120;
	for (i = 0; i < n; i++)
		vdi_intin[i] = (unsigned char)s[i];
	vdi_ptsin[0] = x; vdi_ptsin[1] = y;
	vdi(8, 0, 1, n);                                  /* v_gtext */
}

static void redraw_frame(void)
{
	/* white work area, then the title line and the bar trough */
	v_bar(s_wx, s_wy, (short)(s_wx + s_ww - 1), (short)(s_wy + s_wh - 1), 0);
}

/* 0 if the AES is not there (caller falls back to the console). */
int gem_init(const char *title)
{
	short desk[4];
	short w, h;

	aes(AES_APPL_INIT, 0, 1, 0, 0);
	if (aes_intout[0] < 0)
		return 0;
	s_aes_version = aes_global[0];
	snprintf(s_title, sizeof s_title, " %s ", title);

	aes(AES_GRAF_HANDLE, 0, 5, 0, 0);
	s_wchar = aes_intout[1]; s_hchar = aes_intout[2];
	{
		short i;
		vdi_contrl[0] = 100; vdi_contrl[1] = 0; vdi_contrl[3] = 11;
		vdi_contrl[6] = aes_intout[0];
		vdi_intin[0] = aes_intout[0];           /* phys handle */
		for (i = 1; i < 10; i++) vdi_intin[i] = 1;
		vdi_intin[10] = 2;                      /* RC coordinates */
		vdi_trap();                             /* v_opnvwk */
		s_vh = vdi_contrl[6];
	}
	vdi_intin[0] = 1; vdi(32, 0, 0, 1);             /* vswr_mode replace */
	vdi_intin[0] = 0; vdi_intin[1] = 5; vdi(39, 0, 0, 2);  /* vst_alignment left/top */
	vdi_intin[0] = 1; vdi(22, 0, 0, 1);             /* vst_color black */

	aes_intin[0] = 0; aes_intin[1] = 4;             /* desktop work area */
	aes(AES_WIND_GET, 2, 5, 0, 0);
	desk[0] = aes_intout[1]; desk[1] = aes_intout[2];
	desk[2] = aes_intout[3]; desk[3] = aes_intout[4];

	w = (short)(desk[2] - 2 * s_wchar);
	if (w > 52 * s_wchar) w = (short)(52 * s_wchar);
	h = (short)(7 * s_hchar);
	aes_intin[0] = 1;                               /* kind: NAME */
	aes_intin[1] = (short)(desk[0] + (desk[2] - w) / 2);
	aes_intin[2] = (short)(desk[1] + (desk[3] - h) / 2);
	aes_intin[3] = w; aes_intin[4] = h;
	aes(AES_WIND_CREATE, 5, 1, 0, 0);
	s_win = aes_intout[0];
	if (s_win < 0)
		return 1;                               /* no window: alerts still work */
	aes_intin[0] = s_win; aes_intin[1] = 2;         /* WF_NAME */
	aes_intin[2] = (short)((long)(unsigned long)(void *)s_title >> 16);
	aes_intin[3] = (short)((long)(unsigned long)(void *)s_title & 0xFFFF);
	aes_intin[4] = 0; aes_intin[5] = 0;
	aes(AES_WIND_SET, 6, 1, 0, 0);
	aes_intin[0] = s_win;
	aes_intin[1] = (short)(desk[0] + (desk[2] - w) / 2);
	aes_intin[2] = (short)(desk[1] + (desk[3] - h) / 2);
	aes_intin[3] = w; aes_intin[4] = h;
	aes(AES_WIND_OPEN, 5, 1, 0, 0);
	aes_intin[0] = s_win; aes_intin[1] = 4;         /* WF_WORKXYWH */
	aes(AES_WIND_GET, 2, 5, 0, 0);
	s_wx = aes_intout[1]; s_wy = aes_intout[2];
	s_ww = aes_intout[3]; s_wh = aes_intout[4];
	aes_intin[0] = 0; aes_addrin[0] = 0L;           /* arrow pointer */
	aes(AES_GRAF_MOUSE, 1, 1, 1, 0);
	/* An autostarted program can inherit console residue on the desktop;
	 * FMD_FINISH over the desktop's work area asks the desktop to repaint
	 * everything outside our window. */
	aes_intin[0] = 3;                               /* FMD_FINISH */
	aes_intin[1] = 0; aes_intin[2] = 0; aes_intin[3] = 0; aes_intin[4] = 0;
	aes_intin[5] = desk[0]; aes_intin[6] = desk[1];
	aes_intin[7] = desk[2]; aes_intin[8] = desk[3];
	aes(AES_FORM_DIAL, 9, 1, 0, 0);
	redraw_frame();
	return 1;
}

void gem_exit(void)
{
	if (s_win >= 0) {
		aes_intin[0] = s_win; aes(AES_WIND_CLOSE, 1, 1, 0, 0);
		aes_intin[0] = s_win; aes(AES_WIND_DELETE, 1, 1, 0, 0);
		s_win = -1;
	}
	vdi_contrl[0] = 101; vdi_contrl[1] = 0; vdi_contrl[3] = 0;
	vdi_contrl[6] = s_vh; vdi_trap();                /* v_clsvwk */
	aes(AES_APPL_EXIT, 0, 1, 0, 0);
}

/* form_alert. `text` uses '|' for line breaks (max 5 lines x ~30 chars),
 * `buttons` likewise ("OK|Cancel"). Returns the 1-based button pressed. */
int gem_alert(int icon, const char *text, const char *buttons)
{
	static char buf[256];

	snprintf(buf, sizeof buf, "[%d][%s][%s]", icon, text, buttons);
	aes_intin[0] = 1;                               /* default button */
	aes_addrin[0] = (long)buf;
	aes(AES_FORM_ALERT, 1, 1, 1, 0);
	return aes_intout[0];
}

/* Two text lines and a bar 0..100 inside the window. */
void gem_progress(const char *line1, const char *line2, int pct)
{
	short x0 = (short)(s_wx + s_wchar), y0 = (short)(s_wy + s_hchar / 2);
	short bx0 = x0, bx1 = (short)(s_wx + s_ww - s_wchar);
	short by0 = (short)(y0 + 3 * s_hchar), by1 = (short)(by0 + s_hchar);
	short fill;

	if (s_win < 0)
		return;
	aes_intin[0] = 1; aes(AES_WIND_UPDATE, 1, 1, 0, 0);   /* BEG_UPDATE */
	v_bar(x0, y0, (short)(s_wx + s_ww - 1), (short)(by0 - 2), 0);
	if (line1) v_gtext(x0, y0, line1);
	if (line2) v_gtext(x0, (short)(y0 + s_hchar + s_hchar / 4), line2);
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	fill = (short)(bx0 + (long)(bx1 - bx0) * pct / 100);
	v_bar(bx0, by0, bx1, by1, 1);                         /* black frame */
	v_bar((short)(bx0 + 1), (short)(by0 + 1), (short)(bx1 - 1), (short)(by1 - 1), 0);
	if (fill > bx0 + 1)
		v_bar((short)(bx0 + 1), (short)(by0 + 1), (short)(fill - 1), (short)(by1 - 1), 1);
	aes_intin[0] = 0; aes(AES_WIND_UPDATE, 1, 1, 0, 0);   /* END_UPDATE */
}

/* The selector as a folder picker: navigate into the folder and press OK;
 * the file name is ignored. `seed` is the starting path ("C:\\OPENUA").
 * Returns 1 with dest filled, 0 on cancel. */
int gem_pick_folder(char *dest, size_t cap, const char *seed)
{
	static char path[128], name[128];
	const char *slash;
	size_t dirlen;
	struct stat st;

	/* fsel_exinput VALIDATES its starting path: a folder that does not
	 * exist yet gets "cannot find the folder" and a fall-back to the
	 * floppy — which an unattended OK then picks as the destination.
	 * So create the default first, and if its drive is not there, start
	 * at the root of the current drive. */
	(void)mkdir(seed, 0755);
	if (stat(seed, &st) == 0)
		snprintf(path, sizeof path, "%s\\*.*", seed);
	else
		snprintf(path, sizeof path, "%c:\\*.*", 'A' + Dgetdrv());
	name[0] = '\0';
	aes_intin[0] = 1; aes(AES_WIND_UPDATE, 1, 1, 0, 0);
	aes_addrin[0] = (long)path;
	aes_addrin[1] = (long)name;
	if (s_aes_version >= 0x0140) {
		aes_addrin[2] = (long)"Open the install folder, then OK";
		aes(AES_FSEL_EXINPUT, 0, 2, 3, 0);
	} else {
		aes(AES_FSEL_INPUT, 0, 2, 2, 0);
	}
	aes_intin[0] = 0; aes(AES_WIND_UPDATE, 1, 1, 0, 0);
	if (aes_intout[0] == 0 || aes_intout[1] == 0)
		return 0;
	slash = strrchr(path, '\\');
	dirlen = slash ? (size_t)(slash - path) : 0;
	if (dirlen == 2 && path[1] == ':')
		dirlen = 3;                             /* keep "C:\" */
	if (dirlen == 0 || dirlen >= cap)
		return 0;
	memcpy(dest, path, dirlen);
	dest[dirlen] = '\0';
	return 1;
}

#endif /* !__amigaos__ && INSTDISK_GUI */
