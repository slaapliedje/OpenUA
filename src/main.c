/*
 * FRUA — Atari Falcon030 / TT030 port.
 *
 * QuickDraw / display-HAL diagnostic. Brings up the VIDEL backend, binds
 * QuickDraw to the back buffer via qd_attach_screen, then exercises the
 * shim's drawing primitives on the screen port: a full-screen erase, a
 * filled rect, an outlined rect inside it, a corner-to-corner line, two
 * ovals, a 16x16 CopyBits gradient, a ClipRect-narrowed PaintRect, and a
 * thick PenSize stroke. Each stage logs to C:\DEMO.LOG via dbg_log() so
 * the trail survives a crash and can be read on the host.
 *
 * Reverts to the real engine bootstrap once the path is verified.
 */

#include <mint/osbind.h>
#include <stddef.h>             /* NULL */

#include "display.h"
#include "dbglog.h"
#include "quickdraw.h"

/* A 16x16 source bitmap for the CopyBits demo — a diagonal gradient that
 * shows the blit is reading rows top-to-bottom and pixels left-to-right. */
static unsigned char g_blit_src[16 * 16];

int main(void)
{
	const dsp_backend_t *dsp;
	dsp_surface_t *surf;
	dsp_color_t    pal[256];
	BitMap         src_bm;
	GrafPtr        port;
	Rect           r, srcR, dstR;
	int            i, j;

	dbg_log("main: entered");

	dsp = dsp_detect();
	dbg_log("main: backend =");
	dbg_log(dsp->name);

	if (dsp->init(320, 240) != 0) {
		dbg_log("main: init returned failure");
		return 1;
	}
	dbg_log("main: init ok");

	surf = dsp->surface();
	dbg_log("main: surface ok");

	/* A 332 RGB ramp so the index-as-colour scheme makes sense visually:
	 * index 0 is black, index 255 is white, the middle is colour. */
	for (i = 0; i < 256; i++) {
		pal[i].r = (unsigned char)(i & 0xE0);
		pal[i].g = (unsigned char)((i << 3) & 0xE0);
		pal[i].b = (unsigned char)((i << 6) & 0xC0);
	}
	dsp->set_palette(pal, 0, 256);
	dbg_log("main: palette ok");

	qd_attach_screen(surf->pixels, surf->pitch, surf->width, surf->height);
	dbg_log("main: screen port attached");

	SetRect(&r, 0, 0, surf->width, surf->height);
	EraseRect(&r);                          /* fill background (index 0)   */
	dbg_log("main: erase ok");

	SetRect(&r, 40, 40, surf->width - 40, surf->height - 40);
	PaintRect(&r);                          /* fill foreground (index 255) */
	dbg_log("main: paint ok");

	SetRect(&r, 100, 100, 200, 200);
	FrameRect(&r);                          /* outline inside the paint    */
	dbg_log("main: frame ok");

	MoveTo(0, 0);
	LineTo(surf->width - 1, surf->height - 1);
	dbg_log("main: line  ok");

	SetRect(&r, 220, 50, 310, 110);
	PaintOval(&r);                          /* filled oval to the right    */
	dbg_log("main: paintoval ok");

	SetRect(&r, 220, 130, 310, 190);
	FrameOval(&r);                          /* outlined oval below it      */
	dbg_log("main: frameoval ok");

	/* CopyBits demo: 16x16 source with (row<<4 | col) values blitted onto
	 * the screen. Reading sample pixels back confirms the row/col addressing. */
	for (j = 0; j < 16; j++)
		for (i = 0; i < 16; i++)
			g_blit_src[j * 16 + i] = (unsigned char)((j << 4) | i);
	src_bm.baseAddr = (Ptr)g_blit_src;
	src_bm.rowBytes = 16;
	SetRect(&src_bm.bounds, 0, 0, 16, 16);
	SetRect(&srcR, 0, 0, 16, 16);
	SetRect(&dstR, 10, 220, 26, 236);
	GetPort(&port);
	CopyBits(&src_bm, (const BitMap *)*((CGrafPtr)port)->portPixMap,
	         &srcR, &dstR, srcCopy, NULL);
	dbg_log("main: copybits ok");

	/* ClipRect demo: clip to a small box well below the other shapes,
	 * then PaintRect over the whole screen — only the clipped area
	 * receives pixels. */
	SetRect(&r, 50, 380, 100, 395);
	ClipRect(&r);
	SetRect(&r, 0, 0, surf->width, surf->height);
	PaintRect(&r);
	dbg_log("main: cliprect ok");
	SetRect(&r, 0, 0, surf->width, surf->height);
	ClipRect(&r);                           /* reset clip to full screen */

	/* PenSize demo: a 3x3 pen sweeps a thick horizontal band below the
	 * PaintRect, then resets to the default. */
	PenSize(3, 3);
	MoveTo(150, 365);
	LineTo(200, 365);
	PenSize(1, 1);
	dbg_log("main: pensize ok");

	dsp->present();
	dbg_log("main: present ok");

	Crawcin();                              /* hold until a keypress */
	dbg_log("main: hold ok");

	dsp->shutdown();
	dbg_log("main: shutdown ok");
	return 0;
}
