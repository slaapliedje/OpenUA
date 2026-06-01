/*
 * Falcon VIDEL display backend (ADR-0005).
 *
 * Implements the display HAL (platform/include/display.h) on the Atari
 * Falcon030's VIDEL video. The engine draws into a chunky 8-bit surface;
 * the Falcon's 256-colour mode is 8 interleaved bitplanes, so present()
 * does a chunky-to-planar conversion onto the screen.
 *
 * init() switches the VIDEL to a native 256-colour mode (the desktop
 * mode with STMODES cleared and 8 planes / VERTFLAG set) and sizes the
 * planar screen from VgetSize().
 *
 * First cut — single-buffered, with a naive (correct but slow) c2p. A fast
 * c2p and double-buffering are follow-ups.
 */

#include <mint/osbind.h>
#include <mint/falcon.h>
#include <mint/linea.h>
#include <stdlib.h>

#include "display.h"
#include "dbglog.h"

static dsp_surface_t  g_surface;
static void          *g_screen_raw;     /* the Mxalloc'd block            */
static unsigned char *g_screen;         /* 256-byte-aligned planar screen */
static short          g_save_mode;
static void          *g_save_log;
static void          *g_save_phys;
static long           g_save_palette[256];   /* desktop palette, restored on exit */

/* c2p group converters (defined below) + the self-test gate. */
extern void c2p_group_asm(const unsigned long *src, unsigned short *dst);
static void c2p_group_c(const unsigned long *sl, unsigned short *pw);
static int  g_c2p_use_asm = 0;

static int videl_init(short want_w, short want_h)
{
	short w, h, newmode;
	long  bytes, raw;

	(void)want_w;
	(void)want_h;

	/* Save the current mode, screen base, and palette for shutdown. */
	g_save_mode = VsetMode(-1);
	g_save_log  = (void *)Logbase();
	g_save_phys = (void *)Physbase();
	VgetRGB(0, 256, g_save_palette);

	/* 256 colours needs a VIDEL-native mode: clear STMODES — the
	 * ST-shifter compatibility flag, which caps at 16 colours — and set
	 * 8 planes. VERTFLAG selects the 400-line variant. Switch the mode
	 * first, then size the screen from VgetSize(): line-A's V_Y_MAX
	 * reports 200 for this mode and can't be trusted for the height. */
	newmode = (short)((g_save_mode & ~(STMODES | 7)) | BPS8 | VERTFLAG);
	dbg_log_num("  videl_init: old mode = ", g_save_mode);
	dbg_log_num("  videl_init: new mode = ", newmode);

	VsetMode(newmode);
	linea0();
	w = (short)V_X_MAX;
	bytes = VgetSize(newmode);                   /* 8bpp planar: W*H bytes */
	h = (short)(bytes / w);
	dbg_log_num("  videl_init: width    = ", w);
	dbg_log_num("  videl_init: height   = ", h);
	dbg_log_num("  videl_init: bytes    = ", bytes);
	dbg_log_num("  videl_init: V_Y_MAX  = ", V_Y_MAX);

	g_surface.pixels = malloc((size_t)bytes);
	if (g_surface.pixels == NULL) {
		dbg_log("  videl_init: malloc FAILED");
		VsetMode(g_save_mode);
		return -1;
	}
	g_surface.width  = w;
	g_surface.height = h;
	g_surface.pitch  = w;

	raw = Mxalloc(bytes + 256, 0);               /* 0 = ST-RAM */
	if (raw <= 0) {
		dbg_log("  videl_init: Mxalloc FAILED");
		free(g_surface.pixels);
		g_surface.pixels = NULL;
		VsetMode(g_save_mode);
		return -1;
	}
	g_screen_raw = (void *)raw;
	g_screen = (unsigned char *)((raw + 255) & ~255L);

	VsetScreen(g_screen, g_screen, -1, -1);      /* point the VIDEL at it */

	/* Self-test the hand-asm c2p against the C reference on a fixed
	 * pattern; only enable it if they agree byte-for-byte (else a bug in
	 * the asm degrades to the C path instead of corrupting the screen). */
	{
		static const unsigned long t_in[4] = {
			0x12345678UL, 0x9ABCDEF0UL, 0x0F1E2D3CUL, 0x4B5A6978UL
		};
		unsigned short a[8], c[8];
		short i, ok = 1;
		c2p_group_asm(t_in, a);
		c2p_group_c(t_in, c);
		for (i = 0; i < 8; i++)
			if (a[i] != c[i]) ok = 0;
		g_c2p_use_asm = ok;
		dbg_log_num("  videl_init: c2p asm ok = ", ok);
	}

	dbg_log("  videl_init: done");
	return 0;
}

static void videl_shutdown(void)
{
	VsetMode(g_save_mode);                     /* restore the desktop mode  */
	VsetScreen(g_save_log, g_save_phys, -1, -1);
	VsetRGB(0, 256, g_save_palette);           /* restore the desktop palette */
	if (g_screen_raw != NULL) {
		Mfree(g_screen_raw);
		g_screen_raw = NULL;
	}
	free(g_surface.pixels);
	g_surface.pixels = NULL;
}

static dsp_surface_t *videl_surface(void)
{
	return &g_surface;
}

/*
 * 8x8 bit-matrix transpose, 32-bit native (Hacker's Delight
 * transpose8rS32), operating in place on the two longs (x = source bytes
 * 0..3, y = bytes 4..7, each byte = one pixel, pixel 0 in the MSB). After
 * it, x/y hold the transposed bytes: byte i gathers bit i of the 8 source
 * pixels — i.e. all eight plane rows at once. The 030 has no 64-bit ALU,
 * so this 32-bit path avoids the emulated long-long the first cut used,
 * and taking longs in/out (vs byte arrays) drops 16 byte loads/stores per
 * 8 px — that I/O, not the math, was the cost.
 */
#define C2P_TR8(x, y) do {                                            \
	unsigned long t_;                                                \
	t_ = ((x) ^ ((x) >> 7))  & 0x00AA00AAUL; (x) ^= t_ ^ (t_ << 7);  \
	t_ = ((y) ^ ((y) >> 7))  & 0x00AA00AAUL; (y) ^= t_ ^ (t_ << 7);  \
	t_ = ((x) ^ ((x) >> 14)) & 0x0000CCCCUL; (x) ^= t_ ^ (t_ << 14); \
	t_ = ((y) ^ ((y) >> 14)) & 0x0000CCCCUL; (y) ^= t_ ^ (t_ << 14); \
	t_ = ((x) & 0xF0F0F0F0UL) | (((y) >> 4) & 0x0F0F0F0FUL);          \
	(y) = (((x) << 4) & 0xF0F0F0F0UL) | ((y) & 0x0F0F0F0FUL);         \
	(x) = t_;                                                        \
} while (0)

/* One 16-pixel group, chunky (4 longs) -> 8 plane words. The C reference;
 * the hand-asm c2p_group_asm is a faithful translation, self-tested
 * against this at init. */
static void c2p_group_c(const unsigned long *sl, unsigned short *pw)
{
	unsigned long hx = sl[0], hy = sl[1];   /* pixels 0..7  */
	unsigned long lx = sl[2], ly = sl[3];   /* pixels 8..15 */

	C2P_TR8(hx, hy);
	C2P_TR8(lx, ly);
	pw[0] = (unsigned short)(((hy & 0xff) << 8) | (ly & 0xff));
	pw[1] = (unsigned short)((((hy >> 8) & 0xff) << 8) | ((ly >> 8) & 0xff));
	pw[2] = (unsigned short)((((hy >> 16) & 0xff) << 8) | ((ly >> 16) & 0xff));
	pw[3] = (unsigned short)((((hy >> 24) & 0xff) << 8) | ((ly >> 24) & 0xff));
	pw[4] = (unsigned short)(((hx & 0xff) << 8) | (lx & 0xff));
	pw[5] = (unsigned short)((((hx >> 8) & 0xff) << 8) | ((lx >> 8) & 0xff));
	pw[6] = (unsigned short)((((hx >> 16) & 0xff) << 8) | ((lx >> 16) & 0xff));
	pw[7] = (unsigned short)((((hx >> 24) & 0xff) << 8) | ((lx >> 24) & 0xff));
}

/*
 * Chunky 8-bit -> Falcon 8-plane interleaved over the 16-pixel-group
 * columns [g0,g1) of rows [y0,y1). The surface is 4-aligned (malloc base,
 * even pitch, 16-px groups) so the group reads four aligned longs. Uses
 * the asm group converter when the self-test passed, else the C one.
 */
static void videl_c2p_rows(short y0, short y1, short g0, short g1)
{
	short w = g_surface.width;
	const unsigned char *src = g_surface.pixels + (long)y0 * g_surface.pitch;
	unsigned char       *row = g_screen + (long)y0 * w;
	short y, g;

	for (y = y0; y < y1; y++) {
		const unsigned long *sl = (const unsigned long *)(src + g0 * 16);
		unsigned short      *pw = (unsigned short *)(row + g0 * 16);

		if (g_c2p_use_asm)
			for (g = g0; g < g1; g++, sl += 4, pw += 8)
				c2p_group_asm(sl, pw);
		else
			for (g = g0; g < g1; g++, sl += 4, pw += 8)
				c2p_group_c(sl, pw);

		src += g_surface.pitch;
		row += w;                        /* 8bpp planar rowbytes == width */
	}
}

static void videl_present(void)
{
	Vsync();
	videl_c2p_rows(0, g_surface.height, 0, g_surface.width / 16);
}

/* Convert only the dirty rect, snapped out to 16-pixel group columns and
 * clamped to the surface — the static parts of the screen are left as-is. */
static void videl_present_rect(short x, short y, short w, short h)
{
	short gmax = g_surface.width / 16;
	short g0 = (short)(x / 16);
	short g1 = (short)((x + w + 15) / 16);
	short y1 = (short)(y + h);

	if (y < 0) y = 0;
	if (y1 > g_surface.height) y1 = g_surface.height;
	if (g0 < 0) g0 = 0;
	if (g1 > gmax) g1 = gmax;
	if (y1 <= y || g1 <= g0)
		return;
	Vsync();
	videl_c2p_rows(y, y1, g0, g1);
}

static void videl_set_palette(const dsp_color_t *colors, short first,
                              short count)
{
	long  entry[256];
	short i;

	if (count > 256)
		count = 256;
	for (i = 0; i < count; i++)
		entry[i] = ((long)colors[i].r << 16)
		         | ((long)colors[i].g << 8)
		         |  (long)colors[i].b;
	VsetRGB(first, count, entry);
}

static const dsp_backend_t videl_backend = {
	"VIDEL (Falcon)",
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
	 * (TT-shifter). Until the TT backend exists, return VIDEL. */
	return &videl_backend;
}
