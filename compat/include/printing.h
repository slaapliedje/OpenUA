/*
 * Mac Printing Manager shim — the Pr* face over the GDOS/VDI printer
 * workstation (platform/plat_vdi.h).
 *
 * The Mac model, which the engine's lifted print chain (jt428 open /
 * jt433 char emit / L4806 page rollover / jt434 close) already speaks:
 * PrOpenDoc returns a PRINTING GRAFPORT; the application SetPorts to it
 * and ordinary QuickDraw text (GetPen / MoveTo / DrawChar) lands on the
 * page. The shim reproduces exactly that: the TPrPort is a real
 * GrafPort, and DrawChar in compat/quickdraw.c routes to the printing
 * module (pr_port_capture) whenever it is the current port.
 *
 * Types mirror the ENGINE's usage, not the full Inside Macintosh
 * surface: this THINK C app allocates the TPrint record itself with
 * NewPtr(120) and passes raw pointers everywhere (never Handles), so
 * the face is pointer-typed throughout.
 *
 * Output device: ASSIGN.SYS id PR_VDI_DEVICE — 21 (FX80.SYS, the Epson
 * dot-matrix on the parallel port). Build with -DPR_VDI_DEVICE=31 to send
 * the job to META.SYS instead, which drops a byte-diffable .GEM metafile at
 * C:\FRUAPRN.GEM — the handy shape for tests.
 */

#ifndef COMPAT_PRINTING_H
#define COMPAT_PRINTING_H

#include "quickdraw.h"

/* The engine allocates this with NewPtr(120) and treats it as opaque. */
typedef struct TPrint {
	unsigned char opaque[120];
} TPrint, *TPPrint;

/* The printing GrafPort. The engine stores the PrOpenDoc return in an
 * A5 slot and SetPorts to it — the struct must BE a GrafPort. */
typedef struct TPrPort {
	GrafPort gPort;
} TPrPort, *TPPrPort;

void     PrOpen(void);
void     PrClose(void);
short    PrError(void);

/* Fixed page setup — the port reimplements dialogs in the shim when a
 * screen needs one (ADR-0006); print setup has nothing to configure
 * yet, so both "dialogs" simply confirm (when PrOpen succeeded). */
Boolean  PrValidate(TPPrint prec);
Boolean  PrStlDialog(TPPrint prec);
Boolean  PrJobDialog(TPPrint prec);

TPPrPort PrOpenDoc(TPPrint prec, TPrPort *port, void *ioBuf);
void     PrCloseDoc(TPPrPort pp);
void     PrOpenPage(TPPrPort pp, void *pageFrame);
void     PrClosePage(TPPrPort pp);

/*
 * The DrawChar hook: returns 1 (and consumes the character, advancing
 * the pen by the 7px Mac print cell) when `port` is the open printing
 * port; 0 lets the caller render to its PixMap as usual. Characters
 * accumulate into a line buffer flushed to v_gtext when the pen jumps.
 */
int pr_port_capture(GrafPtr port, short ch);

#endif /* COMPAT_PRINTING_H */
