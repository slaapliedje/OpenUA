/*
 * FRUA — Atari Falcon030 / TT030 port.
 *
 * QuickDraw / display-HAL diagnostic. Brings up the VIDEL backend, binds
 * QuickDraw to the back buffer via qd_attach_screen, then exercises the
 * shim's drawing primitives on the screen port: a full-screen erase, a
 * filled rect, an outlined rect inside it, a corner-to-corner line, two
 * ovals, a 16x16 CopyBits gradient, a ClipRect-narrowed PaintRect, a
 * thick PenSize stroke, a patXor LineTo over a filled background, a
 * PenPat striped LineTo, an RGBForeColor red PaintRect resolved
 * through the cached palette, a DrawString of "HELLO" through the
 * embedded 8x8 fallback font, and finally a Mac-style window painted
 * by NewCWindow / ShowWindow — title bar with the title centred, a
 * close box on the left, a black frame, and a white content area.
 * Each stage logs to C:\DEMO.LOG via dbg_log() so
 * the trail survives a crash and can be read on the host.
 *
 * Reverts to the real engine bootstrap once the path is verified.
 */

#include <mint/osbind.h>
#include <stddef.h>             /* NULL */

#include "display.h"
#include "dbglog.h"
#include "quickdraw.h"
#include "windows.h"

/* A 16x16 source bitmap for the CopyBits demo — a diagonal gradient that
 * shows the blit is reading rows top-to-bottom and pixels left-to-right. */
static unsigned char g_blit_src[16 * 16];

int main(void)
{
	const dsp_backend_t *dsp;
	dsp_surface_t *surf;
	RGBColor       pal[256];
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
	 * index 0 is black, index 255 is white, the middle is colour. The
	 * Mac RGBColor is 16-bit per channel, so the 8-bit ramp is left-
	 * shifted into the top byte; the shim downsamples back to 8-bit
	 * inside qd_set_palette before handing off to the HAL. */
	for (i = 0; i < 256; i++) {
		pal[i].red   = (unsigned short)((i & 0xE0)        << 8);
		pal[i].green = (unsigned short)(((i << 3) & 0xE0) << 8);
		pal[i].blue  = (unsigned short)(((i << 6) & 0xC0) << 8);
	}
	qd_set_palette(pal, 0, 256);
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

	/* PenMode demo: paint a small white strip, then XOR a horizontal
	 * line over it — fgColor 255 ^ 255 flips the pixels to 0, so the
	 * line shows up as black on white, the rubber-band selection idiom. */
	SetRect(&r, 50, 290, 250, 310);
	PaintRect(&r);
	PenMode(patXor);
	MoveTo(60, 300);
	LineTo(240, 300);
	PenMode(patCopy);
	dbg_log("main: penmode ok");

	/* PenPat demo: a vertical-stripe pattern (0xAA = 10101010) on a fresh
	 * white background; patCopy paints fg at set bits and bk at clear bits,
	 * so the line alternates 255 / 0 every column. */
	{
		Pattern stripe = {{0xAA, 0xAA, 0xAA, 0xAA,
		                   0xAA, 0xAA, 0xAA, 0xAA}};
		Pattern solid  = {{0xFF, 0xFF, 0xFF, 0xFF,
		                   0xFF, 0xFF, 0xFF, 0xFF}};

		SetRect(&r, 50, 330, 250, 350);
		PaintRect(&r);
		PenPat(&stripe);
		MoveTo(60, 340);
		LineTo(240, 340);
		PenPat(&solid);
	}
	dbg_log("main: penpat  ok");

	/* RGBForeColor demo: request pure red; the nearest entry in our 332
	 * ramp is index 224 (top three R bits set, G/B bits clear — pal[224]
	 * is exactly (224, 0, 0)). PaintRect into a clean area on the right
	 * then restore white before any later drawing. */
	{
		RGBColor red   = { 0xFFFF, 0x0000, 0x0000 };
		RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };

		SetRect(&r, 270, 290, 310, 310);
		RGBForeColor(&red);
		PaintRect(&r);
		RGBForeColor(&white);
	}
	dbg_log("main: rgbfg   ok");

	/* Text demo: render "HELLO" through the 8x8 fallback font. pnLoc.v
	 * is the baseline; with ascent 7 the glyph body sits at y =
	 * baseline-6 .. baseline. Five chars at 8 px = 40 px wide.
	 *
	 * Pen, fg and bk get reset first — the prior demos left the pen in
	 * an arbitrary state. */
	PenSize(1, 1);
	PenMode(patCopy);
	PenPat(&(Pattern){{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}});
	MoveTo(10, 380);
	DrawString((ConstStr255Param)"\005HELLO");
	dbg_log("main: text    ok");

	/* Window demo: bring up a colour window with a close box. NewCWindow
	 * runs ShowWindow because visible=1, and ShowWindow paints the frame
	 * + title bar into the screen port — overdrawing whatever sat under
	 * the window bounds. */
	{
		Rect win_bounds;

		SetRect(&win_bounds, 60, 60, 220, 200);
		NewCWindow(NULL, &win_bounds, (ConstStr255Param)"\003WIN",
		           1, 0, (WindowPtr)-1L, 1, 0);
	}
	dbg_log("main: window  ok");

	dsp->present();
	dbg_log("main: present ok");

	Crawcin();                              /* hold until a keypress */
	dbg_log("main: hold ok");

	dsp->shutdown();
	dbg_log("main: shutdown ok");
	return 0;
}
