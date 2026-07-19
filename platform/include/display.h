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

	/* How many presents seed every visible page with the current frame.
	 * 1 = single-buffered (present writes the visible screen directly);
	 * 2 = page-flipped (present targets a back page, so a full frame
	 * must be presented twice for both pages to carry it — the videl
	 * pattern; its triple-buffer spare is refreshed by present_rect's
	 * hole update, so 2 still suffices). The engine's full-recompose
	 * sites present exactly this many times; the second present used to
	 * be unconditional and cost single-buffered backends a full no-op
	 * screen diff per recompose (#151). */
	short pages;
} dsp_backend_t;

/* Probe the host machine and return the best available backend, or NULL. */
const dsp_backend_t *dsp_detect(void);

/* Native-planar support (ADR-0016, approach B). The active bitplane backend's
 * fixed per-band chunky-index -> palette-slot remap, so engine planar writers
 * convert wall/UI pixels to the SAME slots the backend's c2p uses (one shared
 * per-scene palette, the invariant that lets planar and chunky regions coexist).
 * Returns the remap base (`*nbands` rows of 256 bytes each; band = y*nbands/h)
 * and fills *nbands / *screen_h, or NULL on backends without a fixed-palette
 * planar path (or before the first palette is installed). */
const unsigned char *dsp_planar_remap(short *nbands, short *screen_h);

/* Native-planar dungeon viewport (ADR-0016 B2). A bitplane backend renders the
 * first-person viewport as a SEPARATELY-composited planar region instead of
 * letting the (churning) viewport dirty the shared 8bpp surface's rows — so the
 * static roster/HUD sharing those scanlines stops being re-converted every step.
 *
 *   dsp_viewport_scratch(&pitch) — returns a chunky scratch buffer to render the
 *     viewport into using ABSOLUTE screen coords (so existing clip/placement math
 *     is unchanged), and sets *pitch. Returns NULL on backends that keep the
 *     chunky c2p path (Falcon/TT, and Amiga until its own B2) — the engine then
 *     renders straight into the shared surface exactly as before.
 *   dsp_viewport_commit(x,y,w,h) — after rendering, hand back the rect (absolute
 *     coords) to convert to planes; the next present composites it into the hole.
 *
 * Implemented via the planar_viewport_register() hook (planar.h); the entry
 * points themselves live in the shared planar module so both build trees link. */
unsigned char *dsp_viewport_scratch(short *pitch);
void           dsp_viewport_commit(short x, short y, short w, short h);

/* Atari builds: the _VDO cookie value (video hardware id in the high word:
 * 0 ST, 1 STE, 2 TT, 3 VIDEL; 0 when no jar). Cached after the first call.
 * Other machines' backends do not define it. */
long dsp_vdo_cookie(void);

/* --- VBL mouse-cursor service ------------------------------------------------
 *
 * When plat_cursor_active() is non-zero the backend draws the mouse pointer on
 * every vertical blank, on the displayed buffer, from a sprite pushed by the
 * Toolbox shim — independent of whatever input loop a screen is sitting in, the
 * way the Mac OS draws its pointer. The shim then skips its own software cursor.
 * Backends that can't do this (no VBL flip installed) return 0 and the shim
 * composites the cursor into the chunky surface as before. */
int  plat_cursor_active(void);

/* Push a 16x16 cursor sprite: `rgb565` is 16*16 RGB565 words (row-major),
 * `mask` is 16 rows with bit 15 = column 0 (a set bit = opaque pixel). The
 * hotspot is the pointer's active pixel within the 16x16 cell. */
void plat_cursor_set_sprite(const unsigned short *rgb565,
                            const unsigned short *mask,
                            short hotx, short hoty);

/* Show/hide nesting result (1 = visible). */
void plat_cursor_show(int visible);

/* Hide the cursor until the next mouse movement, then auto-restore — the
 * faithful ObscureCursor semantics, handled in the VBL so it works on every
 * screen (the line editor obscures the pointer during name entry). */
void plat_cursor_obscure(void);

#endif /* PLATFORM_DISPLAY_H */
