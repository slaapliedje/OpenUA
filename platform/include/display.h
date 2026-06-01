/*
 * Display hardware abstraction for the FRUA Atari port.
 *
 * One engine-facing surface API; one swappable backend per machine:
 *   - VIDEL       Falcon030 programmable video
 *   - TT-shifter  TT030 video
 *   - VDI         portable GEM fallback (added later)
 *
 * The engine and the Mac Toolbox shim draw into an 8-bit paletted back
 * buffer and call dsp_present() to put it on screen. Nothing above this
 * header knows which machine it is running on.
 */

#ifndef PLATFORM_DISPLAY_H
#define PLATFORM_DISPLAY_H

/* A palette entry, 8 bits per channel. Backends convert to hardware format. */
typedef struct {
	unsigned char r, g, b;
} dsp_color_t;

/* The 8-bit paletted back buffer the engine renders into. */
typedef struct {
	short          width;   /* visible width in pixels        */
	short          height;  /* visible height in pixels       */
	short          pitch;   /* bytes per row (may exceed width)*/
	unsigned char *pixels;  /* top-left origin, one byte/pixel */
} dsp_surface_t;

/* A concrete machine backend. Exactly one is selected at init. */
typedef struct dsp_backend {
	const char *name;

	/* Bring up a video mode at least want_w x want_h, 8-bit paletted.
	 * Returns 0 on success, non-zero on failure. */
	int  (*init)(short want_w, short want_h);

	/* Restore the video mode that was active before init(). */
	void (*shutdown)(void);

	/* The back buffer to render into. Valid until shutdown(). */
	dsp_surface_t *(*surface)(void);

	/* Copy/flip the back buffer to the visible screen. */
	void (*present)(void);

	/* Present only the dirty rect (x,y,w,h). Optional (may be NULL);
	 * lets a backend skip converting the static parts of the screen.
	 * The backend may snap the rect to its conversion granularity. */
	void (*present_rect)(short x, short y, short w, short h);

	/* Load count CLUT entries starting at index first. */
	void (*set_palette)(const dsp_color_t *colors, short first, short count);
} dsp_backend_t;

/* Probe the host machine and return the best available backend, or NULL. */
const dsp_backend_t *dsp_detect(void);

#endif /* PLATFORM_DISPLAY_H */
