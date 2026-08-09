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

	/* #99: 1 = a palette change is applied by HARDWARE and does NOT
	 * invalidate pixels already converted to the screen.
	 *
	 * The shim's qd_set_palette used to call qd_touch_all() unconditionally,
	 * which forces the next present to re-convert the whole frame. On most
	 * backends that is right, because the on-screen bytes encode the COLOUR
	 * and not the index:
	 *   - VIDEL blits the 8bpp surface through a LUT into a 16bpp screen, so
	 *     new palette entries mean new screen words;
	 *   - the ST/STe and Amiga ECS backends QUANTISE a 256-index surface down
	 *     to 16/32 slots, so a new palette changes the index->slot remap and
	 *     every plane bit is potentially stale.
	 * The TT is neither. TT-low is 8 planes = 256 colours, tt_c2p_span
	 * transposes the raw chunky index with no remap, and tt_set_palette gives
	 * CLUT entry i the colour of index i — so plane value == chunky index ==
	 * CLUT slot, and EsetPalette alone makes the change visible. This is the
	 * same identity that made the AGA port (#86) short.
	 *
	 * MEASURED (#99, TT, 480 presents in play): the palette site accounted for
	 * 521 touch_all calls — more than one per present — and was single-handedly
	 * why the dirty-row present never fired. 456 of 480 presents were full.
	 *
	 * Conservative by construction: every backend initialiser is a positional
	 * literal that stops before this field, so they all get 0 = "a palette
	 * change DOES invalidate my pixels", which is the old behaviour. Only a
	 * backend that has proven the identity should set it. AGA looks eligible on
	 * the same argument but has NOT been measured or verified here, so it is
	 * deliberately left at 0. */
	short hw_palette;

	/* #63(Amiga): 1 = a palette change on this backend needs NO chunky row
	 * rescan, because the backend invalidates its own converted pixels.
	 *
	 * Distinct from hw_palette, and the two are independent. hw_palette says
	 * "my pixels are still valid" (the TT identity). This says "my pixels may
	 * well be stale, but marking rows dirty is not what fixes them" — the
	 * quantising backends re-derive their remap and re-render off their OWN
	 * dirty flag, while the shim's blanket row mark only makes the next present
	 * re-read all 200 rows of a chunky surface a palette write did not touch.
	 * The present itself is still requested (qd_touch_present_only); only the
	 * row set is left alone.
	 *
	 * MEASURED (Amiga ECS, FRUA_AMIGAPROF, a HEIRS walk): qd_set_palette fired
	 * 636 blanket marks across 644 full presents, making every one of them scan
	 * all 200 rows — 128,800 row compares to convert 782 rows (0.6%). ECS
	 * qualifies because both of its palette paths bypass the row scan anyway: a
	 * substantial write sets s_dirty and ecs_present takes the
	 * ecs_reband/ecs_render branch, and a small write changes nothing the row
	 * scan could have found.
	 *
	 * Conservative by construction, exactly like hw_palette: it sits last, so
	 * every positional initialiser that stops earlier gets 0 = the historical
	 * blanket. The ST/STe backend is eligible by the same argument (its reband
	 * sets s_force_full, which also bypasses pass 1) but is shipping and
	 * measured, so it is deliberately left alone. */
	short palette_self_invalidates;
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

/* Native-planar draw-time plane stores (ADR-0016 B4). A backend that renders the
 * frame by writing planes AT DRAW TIME (the FRUA_PLANAR draw-time model, in place
 * of painting a chunky surface and batch-c2p'ing it each present) fills this out;
 * the converted Toolbox/engine writers (DrawChar first) stamp their pixels straight
 * into `planes` through the per-band remap, IN PARALLEL with their existing chunky
 * store while the writers are converted one at a time. `remap` is `nbands` rows of
 * 256 bytes; screen line y uses row remap + (y*nbands/h)*256 — the SAME map the c2p
 * uses, so a converted writer produces byte-identical planes (see docs/planar-plan.md).
 *
 * A converted writer maps its store to screen coords through the CHUNKY WRITE ADDRESS,
 * not port coords: screen offset = p - `chunky`, giving (off%chunky_pitch, off/chunky_pitch).
 * That is coordinate-correct for any pixmap aliasing the screen (window or direct) AND
 * naturally excludes OFFSCREEN pixmaps (whose bytes lie outside [chunky, chunky+pitch*h)),
 * which must not touch the plane buffer. `cov` marks which screen pixels a writer owns,
 * so the present can bridge the rest from the c2p (the immediate-c2p bridge). */
struct dsp_planar_dt {
	unsigned char       *planes;      /* live interleaved plane buffer       */
	const unsigned char *remap;       /* nbands * 256 index -> palette slot   */
	unsigned char       *cov;         /* w*h coverage: 1 where a writer wrote */
	unsigned char       *idx;         /* w*h: the chunky index each writer laid */
	short               *rowcov;      /* h entries: covered-pixel count per row —
	                                   * rowcov[y]==w means every pixel was
	                                   * stamped this epoch; with idx==chunky
	                                   * over the row, s_dt is authoritative and
	                                   * the present skips its conversion */
	const unsigned char *chunky;      /* the on-screen 8bpp surface base      */
	short                chunky_pitch;/* its bytes/row (== w on ST)           */
	short                line_bytes;  /* ST: bytes/scanline (interleaved);
	                                   * Amiga: bytes/row of ONE plane (pitch) */
	long                 plane_bytes; /* Amiga: bytes per plane (pitch*h);
	                                   * unused on ST (interleaved layout)    */
	short                w, h;        /* screen dims                          */
	short                nplanes;
	short                nbands;
};
typedef struct dsp_planar_dt dsp_planar_dt_t;

/* Returns 1 and fills *dt when the active backend is in draw-time plane mode and a
 * palette exists; 0 otherwise (the default chunky+c2p path, Falcon/TT, and any
 * backend before its first palette). A converted writer gates its plane store on
 * this and leaves its chunky store unchanged during the transition. Dispatches
 * through planar_draw_target_register() (planar.h) so both build trees link. */
int dsp_planar_draw_target(dsp_planar_dt_t *dt);

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
