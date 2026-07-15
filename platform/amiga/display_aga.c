/*
 * Amiga AGA display backend (ADR-0012) — direct chipset.
 *
 * The engine renders into one 8-bit paletted CHUNKY back buffer (dsp_surface_t)
 * and calls present(). This backend owns the AGA video hardware: 8 bitplanes in
 * CHIP RAM, a copper list, and a VBL flip. present() converts the chunky buffer
 * to the 8 bitplanes with the shared c2p (platform/c2p.S) and swaps the display
 * to the freshly-filled set. This is the faithful analog of the VIDEL backend,
 * which likewise bangs the Falcon's video registers rather than going through
 * the OS. Nothing above platform/ knows bitplanes exist.
 *
 * ★ SCAFFOLD STATUS: the structure, palette conversion, and backend wiring are
 * real; the copper/bitplane hardware bring-up bodies marked TODO(hw) need to be
 * written against the NDK and validated on real AGA / amiberry once the Bebbo
 * toolchain is in place. They currently no-op or return failure so the intent
 * is never mistaken for a finished, tested backend. Do NOT ship until the
 * TODO(hw) blocks are done and a frame has been seen on hardware.
 */

#include "display.h"

#ifdef FRUA_AMIGA

#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <proto/exec.h>
#include <proto/graphics.h>

/* The engine's fixed play resolution (see the screen-320x200 note): 320x200,
 * 8 bitplanes = 256 colours, lores. AGA displays this as a standard lores
 * screen; 320x256 PAL leaves a border we letterbox. */
#define AGA_W      320
#define AGA_H      200
#define AGA_DEPTH  8
#define AGA_PITCH  (AGA_W / 8)          /* bytes per bitplane row */

/* ★ c2p LAYOUT MISMATCH — the Amiga needs its OWN chunky->planar.
 *
 * platform/c2p.S exports `c2p_group_asm(const unsigned long *src4,
 * unsigned short *dst8)`: 16 chunky pixels -> 8 plane WORDS laid out
 * word-INTERLEAVED (word0=plane0, word1=plane1, ... word7=plane7, then the next
 * group). That is the Falcon's 8-plane mode format. The Amiga's Denise fetches
 * each bitplane from its OWN BPLxPTH pointer, so it wants either 8 SEPARATE
 * contiguous planes or ROW-interleaved planes (via BPLxMOD) — not word-
 * interleaved. So the Falcon c2p is NOT directly reusable here.
 *
 * TODO(hw): write c2p_amiga(chunky, planes[8], w, h, plane_pitch) that scatters
 * to 8 separate plane pointers (the classic Amiga "c2p 1x1 8bpp" shape — e.g.
 * the Kalms/ Rink routines), or row-interleaved with BPLxMOD. The bit-transpose
 * core of c2p.S is reusable; only the final scatter differs. */
extern void c2p_amiga(const unsigned char *chunky, unsigned char *const planes[8],
                      short w, short h, short plane_pitch);

/* --- backend state ------------------------------------------------------- */

static unsigned char *s_chunky;                 /* the engine's 8bpp buffer   */
static unsigned char *s_planes[2];              /* double-buffered bitplanes  */
static int            s_front;                  /* which s_planes is on screen*/
static dsp_surface_t  s_surface;
static struct View   *s_oldview;                /* to restore on shutdown     */
static ULONG          s_palette[1 + AGA_DEPTH * 0]; /* placeholder; see below */

/* CHIP-RAM size of one 8-bitplane frame. */
#define FRAME_BYTES ((long)AGA_PITCH * AGA_H * AGA_DEPTH)

static void aga_shutdown_partial(void);

static int aga_init(short want_w, short want_h)
{
	(void)want_w; (void)want_h;    /* fixed 320x200 like the VIDEL backend */

	/* The chunky back buffer can live in FAST RAM (CPU-only). */
	s_chunky = AllocMem((ULONG)AGA_W * AGA_H, MEMF_ANY | MEMF_CLEAR);
	if (s_chunky == NULL)
		return 1;

	/* The bitplanes MUST be CHIP RAM (the Denise/Lisa DMA reads them). Two
	 * frames for a tear-free VBL flip. */
	s_planes[0] = AllocMem(FRAME_BYTES, MEMF_CHIP | MEMF_CLEAR);
	s_planes[1] = AllocMem(FRAME_BYTES, MEMF_CHIP | MEMF_CLEAR);
	if (s_planes[0] == NULL || s_planes[1] == NULL) {
		aga_shutdown_partial();
		return 1;
	}
	s_front = 0;

	s_surface.width  = AGA_W;
	s_surface.height = AGA_H;
	s_surface.pitch  = AGA_W;      /* chunky: 1 byte/pixel, tightly packed */
	s_surface.pixels = s_chunky;

	/* TODO(hw): take over the display.
	 *   - s_oldview = GfxBase->ActiView; save it.
	 *   - LoadView(NULL); WaitTOF() x2 to let the OS copper settle.
	 *   - Build a copper list: BPLCON0 = 8-bitplane lores (BPU=0..2 + the AGA
	 *     BPU3 bit in BPLCON0), the 8 BPLxPTH/BPLxPTL pointers into s_planes[0],
	 *     BPL1MOD/BPL2MOD = 0 (tightly packed, no interleave), DIWSTRT/DIWSTOP
	 *     for 320x200, DDFSTRT/DDFSTOP = 0x38/0xD0 lores.
	 *   - Enable AGA (FMODE = 0 for 16-bit fetch is fine at lores), point
	 *     COP1LC at the list, and start copper DMA (DMACON = DMAF_SETCLR |
	 *     DMAF_RASTER | DMAF_COPPER | DMAF_MASTER).
	 * Until this lands, return failure so dsp_detect() can be honest. */
	return 1;   /* TODO(hw): return 0 once the copper/bitplane setup is real */
}

static void aga_shutdown_partial(void)
{
	if (s_planes[0]) { FreeMem(s_planes[0], FRAME_BYTES); s_planes[0] = NULL; }
	if (s_planes[1]) { FreeMem(s_planes[1], FRAME_BYTES); s_planes[1] = NULL; }
	if (s_chunky)    { FreeMem(s_chunky, (ULONG)AGA_W * AGA_H); s_chunky = NULL; }
}

static void aga_shutdown(void)
{
	/* TODO(hw): stop copper/bitplane DMA (DMACON clear RASTER|COPPER),
	 * LoadView(s_oldview); RethinkDisplay() so the OS reclaims the screen. */
	(void)s_oldview;
	aga_shutdown_partial();
}

static dsp_surface_t *aga_surface(void)
{
	return &s_surface;
}

static void aga_present(void)
{
	unsigned char *back = s_planes[s_front ^ 1];
	unsigned char *planes[8];
	short p;

	/* The off-screen frame holds 8 separate planes back-to-back; hand their
	 * addresses to the Amiga c2p to scatter the chunky buffer into them. */
	for (p = 0; p < 8; p++)
		planes[p] = back + (long)p * AGA_PITCH * AGA_H;
	c2p_amiga(s_chunky, planes, AGA_W, AGA_H, AGA_PITCH);   /* TODO(hw): impl */

	/* TODO(hw): on the next VBL, repoint the copper's 8 BPLxPTH/PTL pairs at
	 * `planes[0..7]` (WaitTOF() then patch, or run two alternating copper lists
	 * and swap COP1LC). Then flip the front index. */
	s_front ^= 1;
}

static void aga_present_rect(short x, short y, short w, short h)
{
	/* AGA c2p is cheap enough to convert the whole frame; a partial-rect c2p
	 * is a later optimisation. Fall back to a full present. */
	(void)x; (void)y; (void)w; (void)h;
	aga_present();
}

static void aga_set_palette(const dsp_color_t *colors, short first, short count)
{
	short i;
	(void)s_palette;
	/* TODO(hw): AGA has 8-bit-per-gun colour. Build a LoadRGB32-style table
	 * ({count<<16 | first}, then count * 3 longs of 0xRRRRRRRR/GG/BB) and call
	 * LoadRGB32(&screen->ViewPort, table) — OR, in the direct-copper model,
	 * write the AGA COLORxx registers through the BPLCON3 bank select. For now
	 * just widen each 8-bit channel to the 32-bit gun value the hardware wants,
	 * so the conversion math is in place. */
	for (i = 0; i < count; i++) {
		const dsp_color_t *c = &colors[i];
		ULONG r = ((ULONG)c->r * 0x01010101u);
		ULONG g = ((ULONG)c->g * 0x01010101u);
		ULONG b = ((ULONG)c->b * 0x01010101u);
		(void)first; (void)r; (void)g; (void)b;   /* TODO(hw): store to regs */
	}
}

static const dsp_backend_t aga_backend = {
	"amiga-aga",
	aga_init,
	aga_shutdown,
	aga_surface,
	aga_present,
	aga_present_rect,
	aga_set_palette,
};

const dsp_backend_t *dsp_detect(void)
{
	/* TODO(hw): confirm AGA is present (GfxBase->ChipRevBits0 & GFXF_AA_LISA/
	 * ALICE, or the ECS/AGA flags) before claiming this backend. On a plain
	 * ECS/OCS machine we'd want a 32-colour EHB fallback, not AGA. */
	return &aga_backend;
}

/* --- VBL mouse-cursor service (display.h) --------------------------------
 * The Amiga has hardware sprites; the mouse pointer is a natural fit for
 * sprite 0, updated in the copper/VBL. Until that is wired, report inactive so
 * the Toolbox shim composites the cursor into the chunky surface itself (the
 * same fallback the Atari backends use when no VBL flip is installed). */
int  plat_cursor_active(void) { return 0; }
void plat_cursor_set_sprite(const unsigned short *rgb565,
                            const unsigned short *mask, short hotx, short hoty)
{ (void)rgb565; (void)mask; (void)hotx; (void)hoty; }   /* TODO(hw): sprite 0 */
void plat_cursor_show(int visible)     { (void)visible; }
void plat_cursor_obscure(void)         { }

#endif /* FRUA_AMIGA */
