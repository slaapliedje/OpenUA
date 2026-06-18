/*
 * Falcon VIDEL display backend (ADR-0005) — 16bpp TrueColor LUT path.
 *
 * The engine draws into a chunky 8-bit surface (unchanged). Instead of the
 * old chunky-to-planar (C2P) conversion onto an 8-plane 256-colour screen —
 * which shuffles bits across 8 interleaved planes and cost a full pass plus a
 * Vsync per present — this backend runs the VIDEL in 16bpp TrueColor and
 * present() is a tight 8->16 LUT blit: each chunky byte indexes a 256-entry
 * RGB565 lookup table, written as one word to the screen. No plane shuffling,
 * no palette hardware in the hot path (the LUT is rebuilt only on a palette
 * change), and the flip latches at the next vblank without the per-present
 * Vsync stall that caused the input lag.
 *
 * Double-buffered in ST-RAM; VsetScreen latches physbase at the next vblank,
 * so present() returns immediately and the beam still only scans a complete
 * buffer. The 8-bit surface is sized to the VIDEL mode (V_X_MAX x V_Y_MAX);
 * the game uses its top 320x200, the rest is the letterbox surround.
 *
 * First cut: portable C LUT blit. The 68030 asm inner loop (longword reads,
 * 4 px/iteration) is a follow-up behind a self-test gate, like the old C2P.
 */

#include <mint/osbind.h>
#include <mint/falcon.h>
#include <mint/linea.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "display.h"
#include "dbglog.h"
#include "input.h"              /* plat_mouse_pos — the VBL cursor reads it */

static dsp_surface_t  g_surface;          /* the 8-bit chunky buffer the game draws */

/* Double-buffered 16bpp screen in ST-RAM. present() always writes the back
 * (off-screen) buffer, then points the VIDEL base at it; the latch happens at
 * the next vblank, so the beam never scans a half-blitted frame. */
static void          *g_screen_raw[3];    /* the Mxalloc'd blocks             */
static unsigned short *g_screen[3];        /* 256-byte-aligned 16bpp screens   */
static short          g_nbuf;             /* 2 (no VBL) or 3 (VBL triple-buf)  */
/* Triple-buffer page flip driven by the VBL handler. present() blits its
 * private g_draw buffer and PUBLISHES it in g_next (a plain aligned-word
 * write, atomic on the 68k); the VBL handler shows the latest published
 * frame by latching the VIDEL base, sets g_disp, and clears g_next. Single
 * producer (present) / single consumer (the VBL) — no locks. */
static volatile short g_disp;             /* index the VIDEL is showing       */
static volatile short g_next = -1;        /* published frame, -1 = none new    */
static short          g_draw = 1;         /* present's private write buffer    */
static int            g_vbl_installed;
static long           g_vbl_slot = -1;
static short          g_scr_words;        /* 16bpp screen rowbytes, in words  */
static long           g_screen_bytes;     /* size of one 16bpp buffer         */
static short          g_save_mode;
static void          *g_save_log;
static void          *g_save_phys;
static long           g_save_palette[256];

/* 8-bit palette index -> RGB565 word. Rebuilt by videl_set_palette. */
static unsigned short g_lut[256];

/* Hand-asm 8->16 LUT blit (c2p.S) + the self-test gate. count is a multiple
 * of 4 and src must be 4-aligned; videl_lut_blit only takes the asm path when
 * both hold, else the portable C loop. */
extern void lutblit_span(const unsigned char *src, unsigned short *dst,
                         const unsigned short *lut, long count);
static int g_lut_use_asm = 0;

/* --- VBL-driven page flip --- */

extern void vbl_trampoline(void);          /* c2p.S: saves regs, calls below  */

/* VBL mouse cursor (a platform service: drawn on the displayed buffer every
 * vblank, so a live pointer appears on every screen regardless of its input
 * loop). The sprite is a 16x16 RGB565 image + a 1-bit-per-pixel mask pushed by
 * the shim; the erase re-blits the cursor rect from the authoritative chunky
 * surface through the LUT, which composes correctly with the full-frame page
 * flip (each displayed buffer is a fresh repaint of that same surface). */
static unsigned short g_cur_rgb[16 * 16];
static unsigned short g_cur_mask[16];        /* row r, bit 15 = column 0       */
static short          g_cur_hotx, g_cur_hoty;
static volatile short g_cur_have;            /* a valid sprite was pushed      */
static volatile short g_cur_show = 1;        /* HideCursor/ShowCursor result   */
static volatile short g_cur_obscured;        /* hide until the next mouse move  */
static short          g_cur_px = -1, g_cur_py;   /* top-left of the drawn cell  */
static short          g_cur_drawn;           /* cursor currently on the buffer  */
static short          g_cur_omx = -32000, g_cur_omy = -32000;  /* obscure ref   */
static void vbl_cursor_update(short flipped);

/* Called from vbl_trampoline at every vertical blank (supervisor). Latch the
 * VIDEL video base to the latest published frame. The Falcon video base is
 * three bytes: 0xFFFF8201 (bits 23-16), 0xFFFF8203 (15-8), 0xFFFF820D (7-0);
 * latched for the next frame, so writing them in the blank is tear-free. */
void vbl_flip_handler(void)
{
	short n = g_next;
	short flipped = 0;
	if (n >= 0) {
		unsigned long a = (unsigned long)(uintptr_t)g_screen[n];
		*(volatile unsigned char *)0xFFFF8201UL = (unsigned char)((a >> 16) & 0xff);
		*(volatile unsigned char *)0xFFFF8203UL = (unsigned char)((a >> 8) & 0xff);
		*(volatile unsigned char *)0xFFFF820DUL = (unsigned char)(a & 0xff);
		g_disp = n;
		g_next = -1;
		flipped = 1;
	}
	vbl_cursor_update(flipped);
}

/* Supexec'd: add / remove the trampoline in the OS VBL queue (protected low
 * memory: _vblqueue @ 0x456, _nvbls @ 0x454). */
static long vbl_install_super(void)
{
	long *queue = *(long **)0x456UL;
	short nvbls = *(short *)0x454UL;
	short i;
	for (i = 0; i < nvbls; i++)
		if (queue[i] == 0) {
			queue[i] = (long)(uintptr_t)vbl_trampoline;
			g_vbl_slot = i;
			return 1;
		}
	return 0;
}

static long vbl_remove_super(void)
{
	if (g_vbl_slot >= 0) {
		long *queue = *(long **)0x456UL;
		queue[g_vbl_slot] = 0;
		g_vbl_slot = -1;
	}
	return 0;
}

static int videl_init(short want_w, short want_h)
{
	short w, h, newmode;
	long  bytes, raw;
	short b;

	(void)want_w;
	(void)want_h;

	g_save_mode = VsetMode(-1);
	g_save_log  = (void *)Logbase();
	g_save_phys = (void *)Physbase();
	VgetRGB(0, 256, g_save_palette);

	/* 16bpp TrueColor, same geometry as the 8bpp path: 320x240 on VGA
	 * (VGA + double-line), 320x200 on an RGB/TV monitor. BPS16 swaps the
	 * 256-colour planar mode for a 1-word-per-pixel chunky screen, so no
	 * c2p. (If a monitor needs COL80 for 320-wide 16bpp, that is the knob
	 * to flip — the logged width below tells.) */
	if (g_save_mode & VGA)
		newmode = (short)(VGA | VERTFLAG | BPS16);   /* 320x240 16bpp */
	else
		newmode = (short)(BPS16);                    /* 320x200 16bpp */
	dbg_log_num("  videl_init: old mode = ", g_save_mode);
	dbg_log_num("  videl_init: new mode = ", newmode);

	VsetMode(newmode);
	linea0();
	w = (short)V_X_MAX;
	bytes = VgetSize(newmode);                   /* 16bpp: W*H*2 bytes */
	h = (short)(bytes / ((long)w * 2));
	g_scr_words = (short)(bytes / h / 2);        /* rowbytes / 2 = words/row */
	dbg_log_num("  videl_init: width    = ", w);
	dbg_log_num("  videl_init: height   = ", h);
	dbg_log_num("  videl_init: bytes    = ", bytes);
	dbg_log_num("  videl_init: words/row= ", g_scr_words);

	/* The 8-bit chunky surface the game renders into (unchanged shape). */
	g_surface.pixels = malloc((size_t)((long)w * h));
	if (g_surface.pixels == NULL) {
		dbg_log("  videl_init: surface malloc FAILED");
		VsetMode(g_save_mode);
		return -1;
	}
	g_surface.width  = w;
	g_surface.height = h;
	g_surface.pitch  = w;
	memset(g_surface.pixels, 0, (size_t)((long)w * h));

	/* 16bpp ST-RAM screens (the VIDEL DMAs them, so ST-RAM). Try for three —
	 * the VBL-driven triple-buffer needs a spare so present() never blocks —
	 * but require only two: the third is optional and falls back to the
	 * direct (no-Vsync) flip if it or the VBL install fails. */
	g_screen_bytes = bytes;
	g_nbuf = 0;
	for (b = 0; b < 3; b++) {
		raw = Mxalloc(bytes + 256, 0);           /* 0 = ST-RAM */
		if (raw <= 0) {
			if (b < 2) {                     /* need at least two */
				dbg_log("  videl_init: Mxalloc FAILED");
				while (b-- > 0) Mfree(g_screen_raw[b]);
				free(g_surface.pixels);
				g_surface.pixels = NULL;
				VsetMode(g_save_mode);
				return -1;
			}
			break;                           /* two is fine; no third */
		}
		g_screen_raw[b] = (void *)raw;
		g_screen[b] = (unsigned short *)((raw + 255) & ~255L);
		memset(g_screen[b], 0, (size_t)bytes);   /* black, incl. letterbox */
		g_nbuf++;
	}
	g_disp = 0;
	g_draw = 1;
	g_next = -1;
	VsetScreen(g_screen[0], g_screen[0], -1, -1);

	/* Install the VBL page-flip handler (only worthwhile with the spare
	 * third buffer). On failure, present() uses the direct no-Vsync flip. */
	g_vbl_installed = 0;
	if (g_nbuf >= 3 && Supexec(vbl_install_super) != 0)
		g_vbl_installed = 1;
	dbg_log_num("  videl_init: buffers  = ", g_nbuf);
	dbg_log_num("  videl_init: vbl flip = ", g_vbl_installed);

	/* Black the VIDEL overscan border: even in TrueColor the border colour is
	 * driven by Falcon palette register 0 (the desktop left it light). The
	 * engine no longer uses the hardware palette — pixels go through the LUT —
	 * so index 0 is free to pin black. */
	{
		long black = 0;
		VsetRGB(0, 1, &black);
	}

	/* Seed the LUT from the desktop palette so an early present isn't pure
	 * black; the engine reinstalls its own CLUT (clut 129) on boot. */
	{
		short i;
		for (i = 0; i < 256; i++) {
			unsigned long e = (unsigned long)g_save_palette[i];
			unsigned short r = (unsigned short)((e >> 16) & 0xff);
			unsigned short gg = (unsigned short)((e >> 8) & 0xff);
			unsigned short bb = (unsigned short)(e & 0xff);
			g_lut[i] = (unsigned short)(((r & 0xf8) << 8)
			                          | ((gg & 0xfc) << 3)
			                          | (bb >> 3));
		}
	}

	/* Self-test the hand-asm LUT blit against the C reference on a fixed
	 * pattern (4-aligned, count a multiple of 4); only enable it if they
	 * agree word-for-word, so a bug in the asm degrades to the C loop
	 * rather than corrupting the screen. */
	{
		static unsigned char  tin[8] = { 0, 1, 2, 255, 128, 64, 32, 16 };
		unsigned short        ta[8], tc[8];
		short i, ok = 1;
		lutblit_span(tin, ta, g_lut, 8);
		for (i = 0; i < 8; i++)
			tc[i] = g_lut[tin[i]];
		for (i = 0; i < 8; i++)
			if (ta[i] != tc[i]) ok = 0;
		g_lut_use_asm = ok;
		dbg_log_num("  videl_init: lutblit asm ok = ", ok);
	}

	dbg_log("  videl_init: done (16bpp LUT)");
	return 0;
}

static void videl_shutdown(void)
{
	short b;
	if (g_vbl_installed) {                      /* unhook before freeing pages */
		Supexec(vbl_remove_super);
		g_vbl_installed = 0;
	}
	VsetMode(g_save_mode);
	VsetScreen(g_save_log, g_save_phys, -1, -1);
	VsetRGB(0, 256, g_save_palette);
	for (b = 0; b < 3; b++)
		if (g_screen_raw[b] != NULL) {
			Mfree(g_screen_raw[b]);
			g_screen_raw[b] = NULL;
		}
	free(g_surface.pixels);
	g_surface.pixels = NULL;
}

static dsp_surface_t *videl_surface(void)
{
	return &g_surface;
}

/* 8->16 LUT blit: chunky rows [y0,y1), columns [x0,x1) -> the 16bpp buffer
 * `dst`. One word per pixel via the RGB565 LUT. The C reference; a 68030 asm
 * inner loop (longword reads, 4 px/iter) is the follow-up. */
static void videl_lut_blit(unsigned short *dst, short y0, short y1,
                           short x0, short x1)
{
	short y, x, n = (short)(x1 - x0);
	const unsigned char *src = g_surface.pixels
	                         + (long)y0 * g_surface.pitch + x0;
	unsigned short      *row = dst + (long)y0 * g_scr_words + x0;
	/* The asm reads longwords, so it needs a 4-aligned start and a count
	 * that is a multiple of 4. The surface base is malloc-aligned and the
	 * pitch is a multiple of 4, so (x0 & 3)==0 makes every row start aligned. */
	int use_asm = g_lut_use_asm && ((x0 & 3) == 0) && ((n & 3) == 0);

	for (y = y0; y < y1; y++) {
		if (use_asm)
			lutblit_span(src, row, g_lut, (long)n);
		else
			for (x = 0; x < n; x++)
				row[x] = g_lut[src[x]];
		src += g_surface.pitch;
		row += g_scr_words;
	}
}

/* --- VBL cursor draw / erase (defined here so videl_lut_blit is in scope) --- */

/* Erase: re-blit the cursor's 16x16 cell from the chunky surface through the
 * LUT, clipped to the screen. The chunky surface is the authoritative frame, so
 * this restores the exact background under where the cursor was. */
static void cur_erase(unsigned short *scr, short px, short py)
{
	short x0 = px, y0 = py, x1 = (short)(px + 16), y1 = (short)(py + 16);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > g_surface.width)  x1 = g_surface.width;
	if (y1 > g_surface.height) y1 = g_surface.height;
	if (x0 < x1 && y0 < y1)
		videl_lut_blit(scr, y0, y1, x0, x1);
}

/* Draw the 16x16 RGB565 sprite at (px,py), masked + clipped. */
static void cur_draw(unsigned short *scr, short px, short py)
{
	short r, c;
	for (r = 0; r < 16; r++) {
		short          dy  = (short)(py + r);
		unsigned short m   = g_cur_mask[r];
		unsigned short    *row;
		const unsigned short *src;
		if (dy < 0 || dy >= g_surface.height || m == 0)
			continue;
		row = scr + (long)dy * g_scr_words;
		src = &g_cur_rgb[r * 16];
		for (c = 0; c < 16; c++) {
			short dx = (short)(px + c);
			if (!(m & (unsigned short)(0x8000u >> c)))
				continue;
			if (dx < 0 || dx >= g_surface.width)
				continue;
			row[dx] = src[c];
		}
	}
}

/* Called from vbl_flip_handler every vblank (interrupt / supervisor). `flipped`
 * means a fresh frame was just latched onto g_disp: that buffer is a full
 * repaint with no cursor, so just draw — the previous buffer's stale cursor is
 * wiped by its next present(). When not flipped the same buffer stays shown, so
 * erase-then-draw as the pointer moves. Keep it short: a couple of 16x16 spans. */
static void vbl_cursor_update(short flipped)
{
	unsigned short *scr = g_screen[g_disp];
	short mx, my, px, py, vis;

	plat_mouse_pos(&mx, &my);
	if (g_cur_obscured && (mx != g_cur_omx || my != g_cur_omy))
		g_cur_obscured = 0;            /* ObscureCursor restores on first move */
	vis = g_cur_have && g_cur_show && !g_cur_obscured;
	px  = (short)(mx - g_cur_hotx);
	py  = (short)(my - g_cur_hoty);

	if (flipped) {
		if (vis) { cur_draw(scr, px, py); g_cur_px = px; g_cur_py = py; g_cur_drawn = 1; }
		else     { g_cur_drawn = 0; }
		return;
	}
	if (g_cur_drawn && (!vis || px != g_cur_px || py != g_cur_py)) {
		cur_erase(scr, g_cur_px, g_cur_py);
		g_cur_drawn = 0;
	}
	if (vis && !g_cur_drawn) {
		cur_draw(scr, px, py);
		g_cur_px = px; g_cur_py = py; g_cur_drawn = 1;
	}
}

int plat_cursor_active(void)
{
	return g_vbl_installed;            /* the VBL flip handler draws the cursor */
}

void plat_cursor_set_sprite(const unsigned short *rgb565,
                            const unsigned short *mask,
                            short hotx, short hoty)
{
	short i;
	if (rgb565 == NULL || mask == NULL)
		return;
	g_cur_have = 0;                    /* disable draw while the sprite updates */
	for (i = 0; i < 16 * 16; i++)
		g_cur_rgb[i] = rgb565[i];
	for (i = 0; i < 16; i++)
		g_cur_mask[i] = mask[i];
	g_cur_hotx = hotx;
	g_cur_hoty = hoty;
	g_cur_have = 1;
}

void plat_cursor_show(int visible)
{
	g_cur_show = (short)(visible ? 1 : 0);
}

void plat_cursor_obscure(void)
{
	plat_mouse_pos(&g_cur_omx, &g_cur_omy);   /* arm: clear on the next move */
	g_cur_obscured = 1;
}

/* Flip: point the VIDEL base at the just-blitted back buffer. VsetScreen
 * latches physbase at the next vblank, so this returns immediately (no Vsync
 * stall) and the beam still only scans a complete buffer. */
static void videl_flip(unsigned short *back)
{
	VsetScreen(back, back, -1, -1);
}

static void videl_present(void)
{
	videl_lut_blit(g_screen[g_draw], 0, g_surface.height, 0, g_surface.width);

	if (g_vbl_installed) {
		/* Publish g_draw for the VBL handler, then take a fresh private
		 * buffer — the one that is neither displayed nor just-published (with
		 * three buffers there is exactly one). present() never blocks; the
		 * handler shows the latest published frame, tear-free. */
		short d, k;
		g_next = g_draw;                 /* atomic aligned-word publish */
		d = g_disp;                      /* snapshot */
		for (k = 0; k < 3; k++)
			if (k != d && k != g_draw) { g_draw = k; break; }
	} else {
		/* No VBL handler: direct flip (VsetScreen latches at the next vblank,
		 * no Vsync). Cycle the two buffers. */
		videl_flip(g_screen[g_draw]);
		g_disp = g_draw;
		g_draw = (short)(1 - g_draw);
	}
}

/* The LUT blit is fast, so a partial present buys little and is wrong under
 * triple-buffering (the three pages diverge); just do a full present. */
static void videl_present_rect(short x, short y, short w, short h)
{
	(void)x; (void)y; (void)w; (void)h;
	videl_present();
}

/* Rebuild the 8->RGB565 LUT from the engine's palette. The 8-bit RGB
 * components are truncated to 5/6/5 bits. */
static void videl_set_palette(const dsp_color_t *colors, short first,
                              short count)
{
	short i;

	if (first < 0) first = 0;
	if (first + count > 256) count = (short)(256 - first);
	for (i = 0; i < count; i++) {
		unsigned short r = colors[i].r;
		unsigned short g = colors[i].g;
		unsigned short b = colors[i].b;
		g_lut[first + i] = (unsigned short)(((r & 0xf8) << 8)
		                                  | ((g & 0xfc) << 3)
		                                  | (b >> 3));
	}
}

static const dsp_backend_t videl_backend = {
	"VIDEL (Falcon 16bpp)",
	videl_init,
	videl_shutdown,
	videl_surface,
	videl_present,
	videl_present_rect,
	videl_set_palette,
};

const dsp_backend_t *dsp_detect(void)
{
	/* TODO: probe the _VDO cookie to tell Falcon (VIDEL) from TT
	 * (TT-shifter) and pick the matching backend. Only VIDEL today. */
	return &videl_backend;
}
