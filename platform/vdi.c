/*
 * Platform VDI printer-workstation HAL — see plat_vdi.h.
 *
 * The classic GEM VDI calling convention: a parameter block of five
 * pointers {contrl, intin, ptsin, intout, ptsout}, its address in d1,
 * the 0x73 magic in d0, `trap #2`. With a GDOS resident (SpeedoGDOS
 * here) the trap dispatches non-screen device ids to the ASSIGN.SYS
 * driver (21 = FX80.SYS, 31 = META.SYS in the staged install).
 *
 * contrl layout (words):
 *   [0] opcode        [1] #ptsin vertices   [2] #ptsout (out)
 *   [3] #intin words  [4] #intout (out)     [5] sub-opcode
 *   [6] workstation handle
 *
 * One static parameter block serves all calls — the port is single-
 * threaded and VDI is synchronous, exactly like the engine's other
 * HAL surfaces.
 */

#include <stddef.h>             /* NULL */
#include <mint/osbind.h>        /* Dgetdrv / Dsetdrv / Dgetpath / Dsetpath */

#include "plat_vdi.h"

/*
 * GDOS moves the GEMDOS current directory out from under us.
 *
 * Opening a workstation loads the driver, and vst_load_fonts reads the faces
 * named by ASSIGN.SYS — SpeedoGDOS does both by Dsetpath'ing into the font
 * directory (C:\GEMSYS), and it does not put the path back. Every relative
 * open the engine makes afterwards then resolves under GEMSYS\ and fails:
 * "frua.rsc not found", "No GEMDOS dir C:\GEMSYS\heirs.dsn".
 *
 * The HAL owns the trap, so the HAL hides the side effect: bracket the two
 * file-touching GDOS entries with a save/restore of drive + path.
 */
typedef struct { short drv; char path[128]; } vdi_cwd;

static void cwd_save(vdi_cwd *c)
{
	c->drv = (short)Dgetdrv();
	c->path[0] = 0;
	Dgetpath(c->path, (short)(c->drv + 1));
}

static void cwd_restore(const vdi_cwd *c)
{
	Dsetdrv(c->drv);
	if (c->path[0] != 0)
		Dsetpath(c->path);
	else
		Dsetpath("\\");
}

static short contrl[12];
static short intin[260];
static short ptsin[16];
static short intout[260];
static short ptsout[64];

static void *const vdi_pb[5] = { contrl, intin, ptsin, intout, ptsout };

/* Raw trap #2. GEM clobbers the scratch registers; d0 carries the 0x73
 * magic in and (for the GDOS probe) a result out. */
static long vdi_trap(long magic)
{
	register long        d0 asm("d0") = magic;
	register void *const d1 asm("d1") = (void *)vdi_pb;

	__asm__ volatile ("trap #2"
	                  : "+r" (d0)
	                  : "r" (d1)
	                  : "d2", "a0", "a1", "a2", "cc", "memory");
	return d0;
}

static void vdi(short op, short sub, short handle, short nptsin, short nintin)
{
	contrl[0] = op;
	contrl[1] = nptsin;
	contrl[2] = 0;
	contrl[3] = nintin;
	contrl[4] = 0;
	contrl[5] = sub;
	contrl[6] = handle;
	(void)vdi_trap(0x73L);
}

int plat_vdi_gdos_present(void)
{
	/* The documented GDOS probe: trap #2 with d0 = -2. No GDOS: the ROM
	 * handler returns d0 unchanged (-2). Any GDOS: something else (0 for
	 * old GDOS, a version/magic for FSM/Speedo). */
	register long d0 asm("d0") = -2L;

	__asm__ volatile ("trap #2"
	                  : "+r" (d0)
	                  :
	                  : "d1", "d2", "a0", "a1", "a2", "cc", "memory");
	return d0 != -2L;
}

short plat_vdi_open(short device, short *out_w, short *out_h)
{
	vdi_cwd cwd;
	short i;

	for (i = 0; i < 10; i++)
		intin[i] = 1;
	intin[0]  = device;
	intin[10] = 2;                          /* raster coordinates */

	cwd_save(&cwd);                         /* the driver load chdirs */
	vdi(1, 0, 0, 0, 11);                    /* v_opnwk */
	cwd_restore(&cwd);

	if (contrl[6] > 0) {
		if (out_w != NULL)
			*out_w = (short)(intout[0] + 1);   /* work_out[0] */
		if (out_h != NULL)
			*out_h = (short)(intout[1] + 1);   /* work_out[1] */
	}
	return contrl[6];
}

void plat_vdi_close(short handle)
{
	vdi(2, 0, handle, 0, 0);                /* v_clswk */
}

void plat_vdi_clear(short handle)
{
	vdi(3, 0, handle, 0, 0);                /* v_clrwk */
}

void plat_vdi_update(short handle)
{
	vdi(4, 0, handle, 0, 0);                /* v_updwk */
}

void plat_vdi_text(short handle, short x, short y, const char *s)
{
	short n = 0;

	while (s != NULL && s[n] != 0 && n < 256)
		n++;
	{
		short i;
		for (i = 0; i < n; i++)
			intin[i] = (short)(unsigned char)s[i];
	}
	ptsin[0] = x;
	ptsin[1] = y;
	vdi(8, 0, handle, 1, n);                /* v_gtext */
}

short plat_vdi_font(short handle, short id)
{
	intin[0] = id;
	vdi(21, 0, handle, 0, 1);               /* vst_font */
	return intout[0];
}

short plat_vdi_point(short handle, short pt, short *out_cell_h)
{
	intin[0] = pt;
	vdi(107, 0, handle, 0, 1);              /* vst_point */
	if (out_cell_h != NULL)
		*out_cell_h = ptsout[3];        /* cell height, pixels */
	return intout[0];
}

short plat_vdi_load_fonts(short handle)
{
	vdi_cwd cwd;

	intin[0] = 0;
	cwd_save(&cwd);                         /* GDOS reads C:\GEMSYS and stays */
	vdi(119, 0, handle, 0, 1);              /* vst_load_fonts */
	cwd_restore(&cwd);
	return intout[0];                       /* # fonts ADDED by GDOS */
}

short plat_vdi_font_name(short handle, short index, char *name32)
{
	short i;

	intin[0] = index;
	vdi(130, 0, handle, 0, 1);              /* vqt_name */
	if (name32 != NULL) {
		for (i = 0; i < 32; i++)
			name32[i] = (char)intout[1 + i];
		name32[32] = 0;
	}
	return intout[0];                       /* the face's font id */
}

void plat_vdi_meta_filename(short handle, const char *path)
{
	short n = 0;

	while (path != NULL && path[n] != 0 && n < 256)
		n++;
	{
		short i;
		for (i = 0; i < n; i++)
			intin[i] = (short)(unsigned char)path[i];
		intin[n] = 0;
	}
	vdi(5, 100, handle, 0, (short)(n + 1)); /* vm_filename (escape 100) */
}
