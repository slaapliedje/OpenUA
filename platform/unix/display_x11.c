/*
 * X11 display backend — AMIX (Amiga UNIX) and Atari System V (the ATW800/2
 * under X11R6.3), or any X server the program can reach over TCP.
 *
 * The engine draws into a 320x200 chunky 8-bit surface, which is what an
 * X window with a PseudoColor visual is: a private 256-entry colormap is
 * the palette, XPutImage the present. On such a display the pixels hold the
 * index and the colormap is the hardware CLUT, so palette changes are free
 * (hw_palette) and ride with the next present (palette_with_present), as on
 * AGA. On a TrueColor display (a PC's X server, over the network) the
 * pixels hold colours: the present converts through a pixel table and a
 * palette change re-presents everything.
 *
 * The window is the game scaled by OPENUA_SCALE (1..4; default 2, or 1 if
 * the screen is too small). A full present is row-diffed against a shadow
 * of what the window last received (the RTG/Nova design): only the runs of
 * rows that changed go over the wire, which on the TT is the network.
 *
 * The X pointer is hidden over the window: the engine draws its own (the
 * shim's software cursor, plat_cursor_active() = 0), at the position
 * input_x11.c reports.
 *
 * Only Xlib structures whose fields are all 4 bytes wide cross into the
 * AMIX Xlib (XEvent, XImage, XColor, XSizeHints, XWMHints,
 * XSetWindowAttributes): see
 * toolchain/sysv4-cc.
 */
#ifdef FRUA_UNIX

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "dbglog.h"
#include "x11_unix.h"

#define GW  320			/* the game's surface */
#define GH  200

static Display       *s_dpy;
static Window         s_win;
static GC             s_gc;
static Visual        *s_vis;
static Colormap       s_cmap;
static int            s_depth;
static int            s_pseudo;		/* 8-bit PseudoColor: pixel = index */
static int            s_scale = 2;
static dsp_backend_t  s_x11_backend;	/* below */

static unsigned char *s_chunky;		/* the engine's surface */
static unsigned char *s_shadow;		/* what the window last received */
static dsp_surface_t  s_surface;

static char          *s_img_data;	/* scaled (and/or converted) image */
static XImage        *s_img;
static int            s_bpp;		/* bytes per pixel of s_img */
static unsigned long  s_pixel[256];	/* TrueColor: index -> pixel value */
static dsp_color_t    s_pal[256];	/* TrueColor: the logical palette */

Display *x11_display(void) { return s_dpy; }
Window   x11_window(void)  { return s_win; }
int      x11_scale(void)   { return s_scale; }

/* bits a mask is shifted left by, and how many of them there are */
static void mask_shift(unsigned long m, int *shift, int *bits)
{
	*shift = 0; *bits = 0;
	if (m == 0)
		return;
	while (!(m & 1)) { m >>= 1; (*shift)++; }
	while (m & 1)    { m >>= 1; (*bits)++; }
}

static unsigned long truecolor_pixel(const dsp_color_t *c)
{
	int rs, rb, gs, gb, bs, bb;

	mask_shift(s_vis->red_mask, &rs, &rb);
	mask_shift(s_vis->green_mask, &gs, &gb);
	mask_shift(s_vis->blue_mask, &bs, &bb);
	return ((unsigned long)(c->r >> (8 - rb)) << rs)
	     | ((unsigned long)(c->g >> (8 - gb)) << gs)
	     | ((unsigned long)(c->b >> (8 - bb)) << bs);
}

/* An invisible pointer for the window: the engine draws its own. */
static void hide_pointer(void)
{
	static char zero[8];
	Pixmap p = XCreateBitmapFromData(s_dpy, s_win, zero, 8, 8);
	XColor black;
	Cursor c;

	memset(&black, 0, sizeof black);
	c = XCreatePixmapCursor(s_dpy, p, p, &black, &black, 0, 0);
	XDefineCursor(s_dpy, s_win, c);
	XFreePixmap(s_dpy, p);
}

static int x11_init(short want_w, short want_h)
{
	const char *e;
	int scr, sw, sh;
	XSetWindowAttributes wa;
	XSizeHints hints;
	XWMHints wmh;
	unsigned long mask;

	(void)want_w; (void)want_h;
	s_dpy = XOpenDisplay(getenv("DISPLAY"));
	if (s_dpy == NULL) {
		dbg_log("x11: cannot open the display (DISPLAY by address, "
		        "e.g. 192.168.1.2:0)");
		return 1;
	}
	scr     = XDefaultScreen(s_dpy);
	s_vis   = XDefaultVisual(s_dpy, scr);
	s_depth = XDefaultDepth(s_dpy, scr);
	sw      = XDisplayWidth(s_dpy, scr);
	sh      = XDisplayHeight(s_dpy, scr);
	s_pseudo = (s_vis->class == PseudoColor && s_depth == 8);
	if (!s_pseudo && s_vis->class != TrueColor) {
		dbg_log("x11: need an 8-bit PseudoColor or a TrueColor visual");
		return 1;
	}
	e = getenv("OPENUA_SCALE");
	if (e != NULL && *e >= '1' && *e <= '4')
		s_scale = *e - '0';
	while (s_scale > 1 && (GW * s_scale > sw || GH * s_scale > sh))
		s_scale--;
	dbg_log_num("x11: depth = ", s_depth);
	dbg_log_num("x11: scale = ", s_scale);

	s_chunky = calloc(GW * GH, 1);
	s_shadow = calloc(GW * GH, 1);
	if (s_chunky == NULL || s_shadow == NULL)
		return 1;

	memset(&wa, 0, sizeof wa);
	wa.background_pixel = XBlackPixel(s_dpy, scr);
	wa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask
	              | ButtonPressMask | ButtonReleaseMask | PointerMotionMask
	              | StructureNotifyMask;
	mask = CWBackPixel | CWEventMask;
	if (s_pseudo) {
		/* a private colormap: entry i is palette index i */
		s_cmap = XCreateColormap(s_dpy, XRootWindow(s_dpy, scr), s_vis, AllocAll);
		wa.colormap = s_cmap;
		mask |= CWColormap;
	}
	s_win = XCreateWindow(s_dpy, XRootWindow(s_dpy, scr), 0, 0,
	                      GW * s_scale, GH * s_scale, 0, s_depth, InputOutput,
	                      s_vis, mask, &wa);
	memset(&hints, 0, sizeof hints);
	hints.flags = PMinSize | PMaxSize;
	hints.min_width = hints.max_width = GW * s_scale;
	hints.min_height = hints.max_height = GH * s_scale;
	XSetNormalHints(s_dpy, s_win, &hints);
	/* without an input hint olvwm never gives the window the keyboard */
	memset(&wmh, 0, sizeof wmh);
	wmh.flags = InputHint | StateHint;
	wmh.input = True;
	wmh.initial_state = NormalState;
	XSetWMHints(s_dpy, s_win, &wmh);
	XStoreName(s_dpy, s_win, "OpenUA");
	s_gc = XCreateGC(s_dpy, s_win, 0, NULL);
	hide_pointer();

	/* the image the presents go through: 8-bit indices at scale 1 on a
	 * PseudoColor display are the chunky bytes themselves; anything else
	 * is expanded/converted into s_img_data first */
	if (s_pseudo)
		s_bpp = 1;
	else
		s_bpp = (s_depth > 16) ? 4 : 2;
	if (s_pseudo && s_scale == 1)
		s_img_data = (char *)s_chunky;
	else
		s_img_data = malloc((size_t)GW * s_scale * GH * s_scale * s_bpp);
	if (s_img_data == NULL)
		return 1;
	s_img = XCreateImage(s_dpy, s_vis, s_depth, ZPixmap, 0, s_img_data,
	                     GW * s_scale, GH * s_scale, s_bpp * 8,
	                     GW * s_scale * s_bpp);
	if (s_img == NULL)
		return 1;
	s_img->byte_order = MSBFirst;	/* ours: Xlib swaps for the server */

	/* PseudoColor: the window holds indices and the colormap is the
	 * hardware CLUT, so a palette change keeps the pixels valid and rides
	 * with the next present (as on AGA). TrueColor: the defaults (0). */
	s_x11_backend.hw_palette = (short)s_pseudo;
	s_x11_backend.palette_with_present = (short)s_pseudo;

	XMapWindow(s_dpy, s_win);
	XSync(s_dpy, False);

	s_surface.width  = GW;
	s_surface.height = GH;
	s_surface.pitch  = GW;
	s_surface.pixels = s_chunky;
	return 0;
}

static void x11_shutdown(void)
{
	if (s_dpy == NULL)
		return;
	if (s_img != NULL) {
		s_img->data = NULL;	/* the buffer is freed below, not by X */
		XDestroyImage(s_img);
		s_img = NULL;
	}
	if (s_img_data != (char *)s_chunky)
		free(s_img_data);
	s_img_data = NULL;
	XCloseDisplay(s_dpy);
	s_dpy = NULL;
	free(s_chunky);
	free(s_shadow);
	s_chunky = s_shadow = NULL;
}

static dsp_surface_t *x11_surface(void)
{
	return &s_surface;
}

/* Expand/convert the chunky rect into the image (scale, pixel format). */
static void convert_rect(int x, int y, int w, int h)
{
	int r, c, k, S = s_scale, iw = GW * S;

	for (r = y; r < y + h; r++) {
		const unsigned char *src = s_chunky + r * GW + x;
		if (s_bpp == 1) {
			unsigned char *d = (unsigned char *)s_img_data + (long)r * S * iw + x * S;
			for (c = 0; c < w; c++)
				for (k = 0; k < S; k++)
					*d++ = src[c];
			d = (unsigned char *)s_img_data + (long)r * S * iw + x * S;
			for (k = 1; k < S; k++)
				memcpy(d + (long)k * iw, d, (size_t)w * S);
		} else if (s_bpp == 2) {
			unsigned short *d = (unsigned short *)s_img_data + (long)r * S * iw + x * S;
			unsigned short *d0 = d;
			for (c = 0; c < w; c++) {
				unsigned short p = (unsigned short)s_pixel[src[c]];
				for (k = 0; k < S; k++)
					*d++ = p;
			}
			for (k = 1; k < S; k++)
				memcpy(d0 + (long)k * iw, d0, (size_t)w * S * 2);
		} else {
			unsigned long *d = (unsigned long *)s_img_data + (long)r * S * iw + x * S;
			unsigned long *d0 = d;
			for (c = 0; c < w; c++) {
				unsigned long p = s_pixel[src[c]];
				for (k = 0; k < S; k++)
					*d++ = p;
			}
			for (k = 1; k < S; k++)
				memcpy(d0 + (long)k * iw, d0, (size_t)w * S * 4);
		}
	}
}

static void put_rect(int x, int y, int w, int h)
{
	if (!(s_pseudo && s_scale == 1))
		convert_rect(x, y, w, h);
	XPutImage(s_dpy, s_win, s_gc, s_img, x * s_scale, y * s_scale,
	          x * s_scale, y * s_scale, w * s_scale, h * s_scale);
}

static void x11_present_rect(short x, short y, short w, short h)
{
	short r;

	if (s_dpy == NULL)
		return;
	if (x < 0) { w = (short)(w + x); x = 0; }
	if (y < 0) { h = (short)(h + y); y = 0; }
	if (x + w > GW) w = (short)(GW - x);
	if (y + h > GH) h = (short)(GH - y);
	if (w <= 0 || h <= 0)
		return;
	put_rect(x, y, w, h);
	for (r = 0; r < h; r++)
		memcpy(s_shadow + (long)(y + r) * GW + x,
		       s_chunky + (long)(y + r) * GW + x, (size_t)w);
	XFlush(s_dpy);
}

/* Row-diffed full present: only the runs of rows that changed. */
static void x11_present(void)
{
	short y, y0;

	if (s_dpy == NULL)
		return;
	for (y = 0; y < GH; ) {
		if (memcmp(s_chunky + (long)y * GW, s_shadow + (long)y * GW, GW) == 0) {
			y++;
			continue;
		}
		y0 = y;
		do
			y++;
		while (y < GH && memcmp(s_chunky + (long)y * GW,
		                        s_shadow + (long)y * GW, GW) != 0);
		put_rect(0, y0, GW, y - y0);
		memcpy(s_shadow + (long)y0 * GW, s_chunky + (long)y0 * GW,
		       (size_t)(y - y0) * GW);
	}
	XFlush(s_dpy);
}

/* Redraw everything the window last received (an Expose). */
void x11_repaint(void)
{
	if (s_dpy == NULL)
		return;
	/* the shadow is what the window showed: present it, not the surface
	 * the engine may be halfway through drawing */
	unsigned char *keep = s_chunky;
	s_chunky = s_shadow;
	put_rect(0, 0, GW, GH);
	s_chunky = keep;
	XFlush(s_dpy);
}

static void x11_set_palette(const dsp_color_t *colors, short first, short count)
{
	short i;

	if (s_dpy == NULL || count <= 0 || first < 0 || first >= 256)
		return;
	if (first + count > 256)
		count = (short)(256 - first);
	if (s_pseudo) {
		XColor xc[256];
		for (i = 0; i < count; i++) {
			xc[i].pixel = (unsigned long)(first + i);
			xc[i].red   = (unsigned short)(colors[i].r * 257);
			xc[i].green = (unsigned short)(colors[i].g * 257);
			xc[i].blue  = (unsigned short)(colors[i].b * 257);
			xc[i].flags = DoRed | DoGreen | DoBlue;
		}
		XStoreColors(s_dpy, s_cmap, xc, count);
		XFlush(s_dpy);
	} else {
		/* TrueColor: new pixel values; hw_palette = 0, so the shim marks
		 * the frame dirty and the next present re-converts it. The shadow
		 * is cleared so the row diff sees every row as changed. */
		for (i = 0; i < count; i++) {
			s_pal[first + i] = colors[i];
			s_pixel[first + i] = truecolor_pixel(&colors[i]);
		}
		memset(s_shadow, 0xff, GW * GH);
	}
}

static dsp_backend_t s_x11_backend = {
	"x11",
	x11_init,
	x11_shutdown,
	x11_surface,
	x11_present,
	x11_present_rect,
	x11_set_palette,
	1,		/* pages: one window, presented directly */
	0, 0, 0		/* hw_palette etc.: x11_init sets them for PseudoColor */
};

const dsp_backend_t *dsp_detect(void)
{
	return &s_x11_backend;
}

/* The engine's pointer is the shim's software composite. */
int  plat_cursor_active(void) { return 0; }
void plat_cursor_set_sprite(const unsigned short *rgb565, const unsigned short *mask,
                            short hotx, short hoty)
{ (void)rgb565; (void)mask; (void)hotx; (void)hoty; }
void plat_cursor_show(int visible) { (void)visible; }
void plat_cursor_obscure(void) { }

long dsp_vdo_cookie(void) { return 0; }

/* No planar colour bands on a chunky display: callers check for NULL. */
const unsigned char *dsp_planar_remap(short *nbands, short *screen_h)
{
	if (nbands)   *nbands = 0;
	if (screen_h) *screen_h = 0;
	return NULL;
}

#endif /* FRUA_UNIX */
