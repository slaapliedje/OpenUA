/*
 * Mac Printing Manager shim — see printing.h.
 *
 * PrOpenDoc opens the VDI workstation (v_opnwk on PR_VDI_DEVICE),
 * attaches the GDOS fonts (vst_load_fonts — MANDATORY: a printer
 * driver like FX80.SYS has no built-in face, and text on a face-less
 * workstation rasterizes to a silent blank page), and picks the
 * monospace face for the engine's 7px print cell. Page control maps
 * PrOpenPage -> v_clrwk and PrClosePage -> v_updwk; PrCloseDoc ->
 * v_clswk.
 *
 * Text arrives through the DrawChar hook (pr_port_capture): the
 * engine's jt433 walks GetPen / DrawChar / MoveTo(+7) across the
 * printing port, and the hook folds that stream back into line runs —
 * one v_gtext per contiguous run instead of one per character — keyed
 * on "the pen sits exactly where the last capture left it".
 *
 * Errors follow the Mac convention: they latch into PrError(), and
 * every entry is safe to call with the job in any state — jt428's
 * cancel path (PrClose + SetPort restore without an open doc) must
 * not trip anything.
 */

#include <stddef.h>             /* NULL */
#include <string.h>             /* memset */

#include "printing.h"
#include "plat_vdi.h"

#ifndef PR_VDI_DEVICE
#define PR_VDI_DEVICE 31        /* META.SYS until the FX80 wedge is solved */
#endif

#define PR_CELL_W    7          /* the Mac print cell jt433 advances by */
#define PR_LINE_MAX  120

static short   pr_err;          /* PrError() */
static int     pr_mgr_open;     /* PrOpen() succeeded */
static short   pr_handle;       /* VDI workstation; 0 = no open doc */
static TPrPort pr_the_port;     /* the printing GrafPort */
static short   pr_face;         /* the VDI font id text flushes with */
static short   pr_dev_w, pr_dev_h;

/* The pending line run. */
static char  pr_buf[PR_LINE_MAX + 1];
static short pr_len;
static short pr_x0, pr_y0;      /* where the run starts */
static short pr_next_h;         /* where the pen must be to extend it */

static void pr_flush(void)
{
	if (pr_len == 0 || pr_handle == 0)
		return;
	pr_buf[pr_len] = 0;
	plat_vdi_text(pr_handle, pr_x0, pr_y0, pr_buf);
	pr_len = 0;
}

void PrOpen(void)
{
	/* resNotFound (-192) — the Mac's "Printing Manager resources
	 * missing" answer — when no GDOS is resident. */
	pr_mgr_open = plat_vdi_gdos_present();
	pr_err = (short)(pr_mgr_open ? 0 : -192);
}

void PrClose(void)
{
	if (pr_handle != 0)             /* abandoned doc: close it out */
		PrCloseDoc(&pr_the_port);
	pr_mgr_open = 0;
}

short PrError(void)
{
	return pr_err;
}

Boolean PrValidate(TPPrint prec)
{
	(void)prec;
	return 0;                       /* record unchanged */
}

Boolean PrStlDialog(TPPrint prec)
{
	(void)prec;
	return (Boolean)(pr_mgr_open && pr_err == 0);   /* fixed setup */
}

Boolean PrJobDialog(TPPrint prec)
{
	(void)prec;
	return (Boolean)(pr_mgr_open && pr_err == 0);
}

TPPrPort PrOpenDoc(TPPrint prec, TPrPort *port, void *ioBuf)
{
	short i, n, id;
	char  name[33];

	(void)prec;
	(void)ioBuf;
	if (port == NULL)
		port = &pr_the_port;

	memset(port, 0, sizeof *port);
	pr_len = 0;

	pr_handle = plat_vdi_open(PR_VDI_DEVICE, &pr_dev_w, &pr_dev_h);
	if (pr_handle == 0) {
		pr_err = -192;
		return port;
	}
#if PR_VDI_DEVICE == 31
	/* Deterministic host-side artifact for the metafile channel. */
	plat_vdi_meta_filename(pr_handle, "C:\\FRUAPRN.GEM");
#endif

	/* Attach the GDOS fonts and pick the monospace face — the print
	 * stream is the engine's fixed 7px cell, so Monospace 821 (the
	 * staged Bitstream face) stands in for the Mac's "Moebius". Any
	 * enumerated face beats face 0 (FX80.SYS has no built-in font). */
	n = plat_vdi_load_fonts(pr_handle);
	pr_face = 0;
	for (i = 1; i <= n; i++) {
		id = plat_vdi_font_name(pr_handle, i, name);
		if (pr_face == 0)
			pr_face = id;           /* first face = fallback */
		if (name[0] == 'M' && name[1] == 'o' && name[2] == 'n') {
			pr_face = id;           /* "Monospace ..." */
			break;
		}
	}
	if (pr_face != 0)
		(void)plat_vdi_font(pr_handle, pr_face);
	(void)plat_vdi_point(pr_handle, 7, NULL);   /* the 7pt print cell */

	/* A believable GrafPort: the page as portRect, pen homed. The
	 * engine reads/writes pnLoc via GetPen/MoveTo and sets txFont /
	 * txSize; text style is applied at flush time. */
	port->gPort.portRect.top    = 0;
	port->gPort.portRect.left   = 0;
	port->gPort.portRect.bottom = pr_dev_h;
	port->gPort.portRect.right  = pr_dev_w;
	port->gPort.txSize = 7;

	pr_err = 0;
	return port;
}

void PrCloseDoc(TPPrPort pp)
{
	(void)pp;
	pr_flush();
	if (pr_handle != 0) {
		plat_vdi_close(pr_handle);
		pr_handle = 0;
	}
}

void PrOpenPage(TPPrPort pp, void *pageFrame)
{
	(void)pp;
	(void)pageFrame;
	pr_flush();
	if (pr_handle != 0)
		plat_vdi_clear(pr_handle);
}

void PrClosePage(TPPrPort pp)
{
	(void)pp;
	pr_flush();
	if (pr_handle != 0)
		plat_vdi_update(pr_handle);     /* emit the page */
}

int pr_port_capture(GrafPtr port, short ch)
{
	if (pr_handle == 0 || port != &pr_the_port.gPort)
		return 0;

	/* The pen moved since the last capture? start a new run. */
	if (pr_len != 0 && (port->pnLoc.v != pr_y0
	                 || port->pnLoc.h != pr_next_h))
		pr_flush();
	if (pr_len == 0) {
		pr_x0 = port->pnLoc.h;
		pr_y0 = port->pnLoc.v;
	}
	pr_buf[pr_len++] = (char)ch;
	if (pr_len >= PR_LINE_MAX)
		pr_flush();

	/* Advance by the print cell — jt433 re-MoveTos to the same spot. */
	port->pnLoc.h = (short)(port->pnLoc.h + PR_CELL_W);
	pr_next_h = port->pnLoc.h;
	return 1;
}
