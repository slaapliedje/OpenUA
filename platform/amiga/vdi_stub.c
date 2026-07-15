/*
 * VDI HAL — Amiga stub (ADR-0012).
 *
 * The GDOS printing backend (compat/printing.c) drives the Falcon's VDI
 * (platform/vdi.c) for printer output. AmigaOS has no VDI; its native answer
 * would be printer.device or a datatypes render — a later, separate backend.
 * Until then this stub reports "no GDOS present", which the printing shim
 * already handles faithfully: PrOpen fails cleanly, PrError propagates, and
 * the engine's own error path (the L4806 SysBeep, the alert) fires exactly
 * as the Mac does with no printer selected.
 */

#include <stddef.h>             /* NULL */

#include "plat_vdi.h"

#ifdef FRUA_AMIGA

int plat_vdi_gdos_present(void)
{
	return 0;                       /* no printing backend on this machine */
}

short plat_vdi_open(short device, short *out_w, short *out_h)
{
	(void)device;
	if (out_w != NULL) *out_w = 0;
	if (out_h != NULL) *out_h = 0;
	return 0;                       /* 0 = open failed */
}

void plat_vdi_close(short handle)          { (void)handle; }
void plat_vdi_clear(short handle)          { (void)handle; }
void plat_vdi_update(short handle)         { (void)handle; }

void plat_vdi_text(short handle, short x, short y, const char *s)
{
	(void)handle; (void)x; (void)y; (void)s;
}

short plat_vdi_font(short handle, short id)
{
	(void)handle; (void)id;
	return 0;
}

short plat_vdi_point(short handle, short pt, short *out_cell_h)
{
	(void)handle; (void)pt;
	if (out_cell_h != NULL) *out_cell_h = 0;
	return 0;
}

short plat_vdi_load_fonts(short handle)
{
	(void)handle;
	return 0;
}

short plat_vdi_font_name(short handle, short index, char *name32)
{
	(void)handle; (void)index;
	if (name32 != NULL) name32[0] = '\0';
	return 0;
}

void plat_vdi_meta_filename(short handle, const char *path)
{
	(void)handle; (void)path;
}

#endif /* FRUA_AMIGA */
