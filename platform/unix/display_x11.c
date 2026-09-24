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
 * Full screen by default: a borderless (override-redirect) window over the
 * whole screen, the game centred on black at OPENUA_SCALE (1..4; default 2,
 * less if the screen is too small), with the keyboard and pointer grabbed
 * and - on a PseudoColor screen - its colormap installed, since no window
 * manager looks after it. OPENUA_WINDOW=1 gives a managed window instead.
 * Best on an 8-bit PseudoColor server (Xatw's default depth): a quarter of
 * the data of 32 bpp, and palette changes cost nothing.
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
#include "unix_vbl.h"

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
static int            s_full;		/* full screen (the default) */
static int            s_ox, s_oy;	/* game area's origin in the window */
static int            s_winw, s_winh;	/* the window's size */
static int            s_bg = -1;	/* PseudoColor: the black index the surround uses */
static dsp_backend_t  s_x11_backend;	/* below */

static unsigned char *s_chunky;		/* the engine's surface */
static unsigned char *s_frame;		/* the last presented frame (snapshot) */
static unsigned char *s_shadow;		/* what the window last received */
static unsigned char  s_rowforce[200];	/* rows to resend though unchanged */
static int            s_pending;	/* s_frame not yet sent */
static unsigned long  s_pending_tick;	/* the tick it was presented in */
static dsp_surface_t  s_surface;

static char          *s_img_data;	/* scaled (and/or converted) image */
static XImage        *s_img;
static int            s_bpp;		/* bytes per pixel of s_img */
static unsigned long  s_pixel[256];	/* TrueColor: index -> pixel value */
static dsp_color_t    s_pal[256];	/* the logical palette */

Display *x11_display(void) { return s_dpy; }
void     x11_origin(int *x, int *y) { *x = s_ox; *y = s_oy; }
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
	s_frame  = calloc(GW * GH, 1);
	s_shadow = calloc(GW * GH, 1);
	if (s_chunky == NULL || s_frame == NULL || s_shadow == NULL)
		return 1;

	memset(&wa, 0, sizeof wa);
	/* Full screen by default: a borderless window over the whole screen,
	 * the game centred on black. OPENUA_WINDOW=1 for a managed window. */
	e = getenv("OPENUA_WINDOW");
	s_full = !(e != NULL && *e == '1');
	if (s_full) {
		s_ox = (sw - GW * s_scale) / 2;
		s_oy = (sh - GH * s_scale) / 2;
	}
	s_winw = s_full ? sw : GW * s_scale;
	s_winh = s_full ? sh : GH * s_scale;
	/* the surround's colour: black. On a PseudoColor screen the window
	 * shows through OUR colormap, where no entry is black for sure - the
	 * palette changes pick one (surround_black) */
	wa.background_pixel = s_pseudo ? 0 : XBlackPixel(s_dpy, scr);
	wa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask
	              | ButtonPressMask | ButtonReleaseMask | PointerMotionMask
	              | StructureNotifyMask;
	mask = CWBackPixel | CWEventMask;
	if (s_full) {
		wa.override_redirect = True;	/* no window manager frame */
		mask |= CWOverrideRedirect;
	}
	if (s_pseudo) {
		/* a private colormap: entry i is palette index i */
		s_cmap = XCreateColormap(s_dpy, XRootWindow(s_dpy, scr), s_vis, AllocAll);
		wa.colormap = s_cmap;
		mask |= CWColormap;
	}
	s_win = XCreateWindow(s_dpy, XRootWindow(s_dpy, scr), 0, 0,
	                      s_winw, s_winh, 0, s_depth, InputOutput,
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
	/* no GraphicsExpose/NoExpose per XCopyArea: put_rect copies rows */
	{
		XGCValues gv;
		gv.graphics_exposures = False;
		s_gc = XCreateGC(s_dpy, s_win, GCGraphicsExposures, &gv);
	}
	hide_pointer();

	/* the image the presents go through: 8-bit indices at scale 1 on a
	 * PseudoColor display are the chunky bytes themselves; anything else
	 * is expanded/converted into s_img_data first */
	if (s_pseudo)
		s_bpp = 1;
	else
		s_bpp = (s_depth > 16) ? 4 : 2;
	if (s_pseudo && s_scale == 1)
		s_img_data = (char *)s_frame;	/* put_rect points it at its source */
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
	if (s_full) {
		/* no window manager looks after this window: give it the
		 * keyboard, keep the pointer in it, and install its colours */
		if (s_pseudo)
			XInstallColormap(s_dpy, s_cmap);
		XSetInputFocus(s_dpy, s_win, RevertToParent, CurrentTime);
		XGrabKeyboard(s_dpy, s_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
		XGrabPointer(s_dpy, s_win, True,
		             ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		             GrabModeAsync, GrabModeAsync, s_win, None, CurrentTime);
		XSync(s_dpy, False);
	}

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
	if (s_img_data != (char *)s_frame && s_img_data != (char *)s_shadow)
		free(s_img_data);
	s_img_data = NULL;
	XCloseDisplay(s_dpy);
	s_dpy = NULL;
	free(s_chunky);
	free(s_frame);
	free(s_shadow);
	s_chunky = s_frame = s_shadow = NULL;
}

static dsp_surface_t *x11_surface(void)
{
	return &s_surface;
}

/* Convert the chunky rect into the image: each game pixel becomes S
 * image pixels across, in the window's pixel format, on the FIRST image
 * row of its S. The other S-1 rows are copied on the server (put_rect). */
static void convert_rect(const unsigned char *pix, int x, int y, int w, int h)
{
	int r, c, k, S = s_scale, iw = GW * S;

	for (r = y; r < y + h; r++) {
		const unsigned char *src = pix + r * GW + x;
		if (s_bpp == 1) {
			unsigned char *d = (unsigned char *)s_img_data + (long)r * S * iw + x * S;
			for (c = 0; c < w; c++)
				for (k = 0; k < S; k++)
					*d++ = src[c];
		} else if (s_bpp == 2) {
			unsigned short *d = (unsigned short *)s_img_data + (long)r * S * iw + x * S;
			for (c = 0; c < w; c++) {
				unsigned short p = (unsigned short)s_pixel[src[c]];
				for (k = 0; k < S; k++)
					*d++ = p;
			}
		} else {
			unsigned long *d = (unsigned long *)s_img_data + (long)r * S * iw + x * S;
			for (c = 0; c < w; c++) {
				unsigned long p = s_pixel[src[c]];
				for (k = 0; k < S; k++)
					*d++ = p;
			}
		}
	}
}

/*
 * Put a rect of game pixels in the window. Scaled, each game row goes over
 * the wire ONCE, one window row tall, and XCopyArea duplicates it down the
 * other S-1 window rows on the server - where a card with a 2D engine
 * (the ATW800/2 under Xatw) does it without the CPU: half the data sent
 * and converted at 2x. (A row whose copy source is covered by another
 * window is fixed by the Expose that follows.)
 */
static void put_rect(const unsigned char *pix, int x, int y, int w, int h)
{
	int r, k, S = s_scale;

	if (S == 1) {
		if (s_pseudo)
			s_img->data = (char *)pix;	/* the indices are the image */
		else
			convert_rect(pix, x, y, w, h);
		XPutImage(s_dpy, s_win, s_gc, s_img, x, y, s_ox + x, s_oy + y, w, h);
		return;
	}
	convert_rect(pix, x, y, w, h);
	for (r = y; r < y + h; r++) {
		XPutImage(s_dpy, s_win, s_gc, s_img, x * S, r * S,
		          s_ox + x * S, s_oy + r * S, w * S, 1);
		for (k = 1; k < S; k++)
			XCopyArea(s_dpy, s_win, s_win, s_gc, s_ox + x * S, s_oy + r * S,
			          w * S, 1, s_ox + x * S, s_oy + r * S + k);
	}
}

/*
 * Send the presented frame: only the runs of rows that differ from what
 * the window shows, or that a palette change marked (the RTG/Nova row
 * diff: on the TT every row crosses the network).
 */
static void flush(void)
{
	short y, y0;

#define ROW_CHANGED(y) (s_rowforce[y] || memcmp(s_frame + (long)(y) * GW, \
                                               s_shadow + (long)(y) * GW, GW) != 0)
	s_pending = 0;
	for (y = 0; y < GH; ) {
		if (!ROW_CHANGED(y)) {
			y++;
			continue;
		}
		y0 = y;
		do
			s_rowforce[y++] = 0;
		while (y < GH && ROW_CHANGED(y));
		put_rect(s_frame, 0, y0, GW, y - y0);
		memcpy(s_shadow + (long)y0 * GW, s_frame + (long)y0 * GW,
		       (size_t)(y - y0) * GW);
	}
#undef ROW_CHANGED
	XFlush(s_dpy);
}

/*
 * A present takes a snapshot; the window gets it at the next tick. The
 * engine draws a picture, presents it and THEN sets its palette - on an
 * 8-bit screen the palette write is instant, but on a TrueColor one each
 * step was a full frame of converted pixels crossing the bus, so every
 * intro screen showed in the old colours first, then repainted. Sent a
 * tick later the frame goes out once, in the colours it was meant for.
 * The snapshot keeps it the frame that was presented, whatever the
 * engine draws next.
 */
void x11_flush_due(void)
{
	if (s_dpy != NULL && s_pending && unix_ticks() != s_pending_tick)
		flush();
}

static void mark_presented(void)
{
	unsigned long now = unix_ticks();

	if (s_pending && now != s_pending_tick)
		flush();		/* the frame owed from an earlier tick */
	s_pending = 1;
	s_pending_tick = now;
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
	for (r = 0; r < h; r++)
		memcpy(s_frame + (long)(y + r) * GW + x,
		       s_chunky + (long)(y + r) * GW + x, (size_t)w);
	mark_presented();
}

static void x11_present(void)
{
	if (s_dpy == NULL)
		return;
	memcpy(s_frame, s_chunky, GW * GH);
	mark_presented();
}

/* Redraw everything the window last received (an Expose). */
void x11_repaint(void)
{
	if (s_dpy == NULL)
		return;
	put_rect(s_shadow, 0, 0, GW, GH);
	XFlush(s_dpy);
}

/*
 * PseudoColor, full screen: keep the surround black. Its colour is a
 * colormap entry like any other and the engine's palette changes all of
 * them (255, for one, is its magenta "transparent" key), so after each
 * change make sure the window background names an entry that is black,
 * and repaint the surround if it had to move.
 */
static void clear_strip(int x, int y, int w, int h)
{
	if (w > 0 && h > 0)
		XClearArea(s_dpy, s_win, x, y, (unsigned)w, (unsigned)h, False);
}

static void surround_black(void)
{
	int i;

	if (!s_full || (s_bg >= 0 && s_pal[s_bg].r == 0 && s_pal[s_bg].g == 0 && s_pal[s_bg].b == 0))
		return;
	for (i = 255; i >= 0; i--)
		if (s_pal[i].r == 0 && s_pal[i].g == 0 && s_pal[i].b == 0)
			break;
	if (i < 0 || i == s_bg)
		return;
	s_bg = i;
	XSetWindowBackground(s_dpy, s_win, (unsigned long)i);
	/* the four strips round the game area (a 0 width or height would
	 * mean "to the edge" to XClearArea: skip empty strips) */
	clear_strip(0, 0, s_winw, s_oy);
	clear_strip(0, s_oy + GH * s_scale, s_winw, s_winh - s_oy - GH * s_scale);
	clear_strip(0, s_oy, s_ox, GH * s_scale);
	clear_strip(s_ox + GW * s_scale, s_oy, s_winw - s_ox - GW * s_scale, GH * s_scale);
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
		for (i = 0; i < count; i++)
			s_pal[first + i] = colors[i];
		surround_black();
		XFlush(s_dpy);
	} else {
		/* TrueColor: the window holds colours, so a changed entry means
		 * resending the rows on screen that use it - only those, and
		 * nothing when the entries did not actually change (the engine
		 * re-installs the same range repeatedly). A pending frame is
		 * converted with the new colours when it goes out. */
		unsigned char chg[256];
		int any = 0;
		long p;

		memset(chg, 0, sizeof chg);
		for (i = 0; i < count; i++) {
			dsp_color_t *o = &s_pal[first + i];
			if (o->r != colors[i].r || o->g != colors[i].g || o->b != colors[i].b) {
				*o = colors[i];
				s_pixel[first + i] = truecolor_pixel(&colors[i]);
				chg[first + i] = 1;
				any = 1;
			}
		}
		if (!any)
			return;
		for (p = 0; p < (long)GW * GH; p++)
			if (chg[s_shadow[p]]) {
				s_rowforce[p / GW] = 1;
				p = (p / GW + 1) * GW - 1;	/* next row */
			}
		if (!s_pending) {
			s_pending = 1;
			s_pending_tick = unix_ticks();
		}
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
