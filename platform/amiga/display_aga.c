/*
 * Amiga AGA display backend (ADR-0012) — direct chipset.
 *
 * The engine renders into one 8-bit paletted CHUNKY back buffer (dsp_surface_t)
 * and calls present(). This backend owns the AGA video hardware: 8 bitplanes in
 * CHIP RAM, a hand-built copper list, and a plane-pointer flip. present()
 * converts the chunky buffer to 8 separate bitplanes (c2p_amiga) and repoints
 * the copper's BPLxPT block at the freshly-filled set. This is the faithful
 * analog of the VIDEL backend, which likewise bangs the Falcon's video
 * registers rather than going through the OS. Nothing above platform/ knows
 * bitplanes exist.
 *
 * The 256-entry palette lives IN the copper list (8 AGA banks x 32 COLORxx
 * writes, high+low nibble passes via BPLCON3 LOCT), so set_palette() is a
 * table patch the next frame displays — palette animation (the fireplace
 * colour cycle) costs nothing here, not even the re-present the 16bpp Falcon
 * path needs.
 *
 * Status: complete direct-AGA implementation, UNVERIFIED on amiberry/real
 * hardware. Known bring-up simplifications, all flagged inline: pointer/
 * palette patches are not VBL-synchronised (a one-frame glitch under heavy
 * churn, never a tear lock), and the display is NTSC-timed 200 lines inside
 * a PAL-tolerant window.
 */

#include "display.h"
#include "dbglog.h"

#ifdef FRUA_AMIGA

#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <proto/exec.h>
#include <proto/graphics.h>

/* The custom chip block. Addressed directly (volatile pointer) rather than
 * through amiga.lib's `custom` symbol, so no extra link dependency. */
#define CUSTOM ((volatile struct Custom *)0xDFF000)

/* The engine's fixed play resolution (see the screen-320x200 note): 320x200,
 * 8 bitplanes = 256 colours, lores. On PAL the 200 lines sit in a 256-line
 * frame's top; the remainder is border. */
#define AGA_W      320
#define AGA_H      200
#define AGA_DEPTH  8
#define AGA_PITCH  (AGA_W / 8)          /* bytes per bitplane row */

/* The per-plane chunky->planar (platform/amiga/c2p_amiga.c): the Falcon c2p's
 * word-interleaved output cannot feed Denise/Lisa, which fetches each plane
 * from its own BPLxPT. */
extern void c2p_amiga(const unsigned char *chunky, unsigned char *const planes[8],
                      short w, short h, short plane_pitch);

/* --- backend state ------------------------------------------------------- */

struct GfxBase *GfxBase;                        /* opened here, v39+ (KS 3.0) */

static unsigned char *s_chunky;                 /* the engine's 8bpp buffer   */
static unsigned char *s_planes[2];              /* double-buffered plane sets */
static int            s_front;                  /* which set is on screen     */
static dsp_surface_t  s_surface;
static struct View   *s_oldview;                /* to restore on shutdown     */

/* CHIP-RAM size of one 8-bitplane frame (8 separate contiguous planes). */
#define FRAME_BYTES ((ULONG)AGA_PITCH * AGA_H * AGA_DEPTH)

/* --- the copper list ------------------------------------------------------
 *
 * Layout (every entry is a MOVE = 2 words; WAIT ends the list):
 *   prologue: FMODE/BPLCONx/BPLxMOD/DIWSTRT/DIWSTOP/DDFSTRT/DDFSTOP
 *   plane block: 8 x { BPLxPTH, hi ; BPLxPTL, lo }         (patched per flip)
 *   palette block: 8 banks x { BPLCON3=bank ; 32 x COLORxx(high) ;
 *                              BPLCON3=bank|LOCT ; 32 x COLORxx(low) }
 *   WAIT 0xFFFF,0xFFFE
 *
 * s_cop_bpl points at the first plane-pointer OPERAND word; s_cop_pal[i]
 * points at colour i's high/low operand words, so both patch in place. */

#define COP_PROLOGUE_MOVES  12
#define COP_PLANE_MOVES     (8 * 2)
#define COP_PAL_MOVES       (8 * (1 + 32 + 1 + 32))
#define COP_WORDS           ((COP_PROLOGUE_MOVES + COP_PLANE_MOVES \
                              + COP_PAL_MOVES) * 2 + 2)

static UWORD *s_cop;                    /* the copper list, CHIP RAM         */
static UWORD *s_cop_bpl;                /* first BPLxPTH operand word        */
static UWORD *s_cop_pal_hi[256];        /* colour i's high-nibble operand    */
static UWORD *s_cop_pal_lo[256];        /* colour i's low-nibble operand     */

/* Custom-register byte offsets (the copper MOVE target field). */
#define R_BPLCON0  0x100
#define R_BPLCON1  0x102
#define R_BPLCON2  0x104
#define R_BPLCON3  0x106
#define R_BPL1MOD  0x108
#define R_BPL2MOD  0x10A
#define R_BPLCON4  0x10C
#define R_DIWSTRT  0x08E
#define R_DIWSTOP  0x090
#define R_DDFSTRT  0x092
#define R_DDFSTOP  0x094
#define R_FMODE    0x1FC
#define R_BPL1PTH  0x0E0                /* +4 per plane                      */
#define R_COLOR00  0x180

/* BPLCON3: bits 15-13 = palette BANK, bit 9 = LOCT (low-nibble pass). */
#define BPLCON3_BANK(b)  ((UWORD)((b) << 13))
#define BPLCON3_LOCT     ((UWORD)0x0200)

static UWORD *cop_move(UWORD *cl, UWORD reg, UWORD val)
{
	*cl++ = reg;
	*cl++ = val;
	return cl;
}

static void cop_build(void)
{
	UWORD *cl = s_cop;
	short p, bank, c;

	/* FMODE 0 = 16-bit fetches, the classic DDF timing below stays valid.
	 * (64-bit fetch would need DDFSTRT/STOP retimed; a later optimisation.) */
	cl = cop_move(cl, R_FMODE,   0x0000);
	/* BPLCON0: COLOR on (bit 9) + 8 bitplanes = BPU2-0 zero with BPU3
	 * (bit 4) set — the AGA 8-plane encoding. */
	cl = cop_move(cl, R_BPLCON0, 0x0210);
	cl = cop_move(cl, R_BPLCON1, 0x0000);
	cl = cop_move(cl, R_BPLCON2, 0x0000);
	cl = cop_move(cl, R_BPLCON3, 0x0000);
	cl = cop_move(cl, R_BPLCON4, 0x0011);
	/* 8 separate contiguous planes: no modulo on either field. */
	cl = cop_move(cl, R_BPL1MOD, 0x0000);
	cl = cop_move(cl, R_BPL2MOD, 0x0000);
	/* Standard lores window: top-left 0x2C,0x81; 200 lines -> bottom 0xF4. */
	cl = cop_move(cl, R_DIWSTRT, 0x2C81);
	cl = cop_move(cl, R_DIWSTOP, 0xF4C1);
	cl = cop_move(cl, R_DDFSTRT, 0x0038);
	cl = cop_move(cl, R_DDFSTOP, 0x00D0);

	/* Plane pointers (patched by cop_point_planes on every flip). */
	s_cop_bpl = cl + 1;                 /* first operand word */
	for (p = 0; p < 8; p++) {
		cl = cop_move(cl, (UWORD)(R_BPL1PTH + p * 4),     0);  /* hi */
		cl = cop_move(cl, (UWORD)(R_BPL1PTH + p * 4 + 2), 0);  /* lo */
	}

	/* Palette: 8 AGA banks; per bank one high-nibble pass then one
	 * low-nibble pass (LOCT). Colours start black; set_palette patches. */
	for (bank = 0; bank < 8; bank++) {
		cl = cop_move(cl, R_BPLCON3, BPLCON3_BANK(bank));
		for (c = 0; c < 32; c++) {
			s_cop_pal_hi[bank * 32 + c] = cl + 1;
			cl = cop_move(cl, (UWORD)(R_COLOR00 + c * 2), 0);
		}
		cl = cop_move(cl, R_BPLCON3, (UWORD)(BPLCON3_BANK(bank) | BPLCON3_LOCT));
		for (c = 0; c < 32; c++) {
			s_cop_pal_lo[bank * 32 + c] = cl + 1;
			cl = cop_move(cl, (UWORD)(R_COLOR00 + c * 2), 0);
		}
	}

	/* End of list: WAIT for a line that never comes. */
	*cl++ = 0xFFFF;
	*cl++ = 0xFFFE;
}

/* Patch the copper's plane-pointer block at the given plane set.
 * NOT VBL-synchronised: the copper re-reads the list each frame, so a patch
 * racing the fetch shows at worst one frame with a mismatched hi/lo pair.
 * Bring-up simplification; a double copper list swap fixes it later. */
static void cop_point_planes(unsigned char *set)
{
	short p;

	for (p = 0; p < 8; p++) {
		ULONG addr = (ULONG)(set + (ULONG)p * AGA_PITCH * AGA_H);
		s_cop_bpl[p * 4]     = (UWORD)(addr >> 16);
		s_cop_bpl[p * 4 + 2] = (UWORD)(addr & 0xFFFF);
	}
}

/* --- backend entry points ------------------------------------------------ */

static void aga_shutdown_partial(void);

static int aga_init(short want_w, short want_h)
{
	(void)want_w; (void)want_h;    /* fixed 320x200 like the VIDEL backend */

	/* Kickstart 3.0+ (v39) — the OS level every AGA machine ships. */
	GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
	if (GfxBase == NULL) {
		dbg_log("aga: graphics.library v39 open failed");
		return 1;
	}
	/* Real AGA — an ECS machine under KS3.x must not get 8 planes (the ECS
	 * answer is a future 32-colour backend). Accept ANY AA chip bit:
	 * KS3.2 under amiberry reports the A1200's chipset as AA_MLISA (16)
	 * with HR_AGNUS|HR_DENISE, NOT AA_LISA — seen live, ChipRevBits0=19. */
	if (!(GfxBase->ChipRevBits0
	      & (GFXF_AA_ALICE | GFXF_AA_LISA | GFXF_AA_MLISA))) {
		dbg_log_num("aga: no AA chipset; ChipRevBits0 = ",
		            (long)GfxBase->ChipRevBits0);
		aga_shutdown_partial();
		return 1;
	}

	/* The chunky back buffer can live in FAST RAM (CPU-only). */
	s_chunky = AllocMem((ULONG)AGA_W * AGA_H, MEMF_ANY | MEMF_CLEAR);
	/* The bitplanes MUST be CHIP RAM (Denise/Lisa DMA reads them). Two
	 * frames for a tear-free flip; the copper list rides in chip too. */
	s_planes[0] = AllocMem(FRAME_BYTES, MEMF_CHIP | MEMF_CLEAR);
	s_planes[1] = AllocMem(FRAME_BYTES, MEMF_CHIP | MEMF_CLEAR);
	s_cop       = AllocMem(COP_WORDS * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);
	if (s_chunky == NULL || s_planes[0] == NULL
	    || s_planes[1] == NULL || s_cop == NULL) {
		dbg_log("aga: AllocMem failed (chip for planes/copper?)");
		aga_shutdown_partial();
		return 1;
	}
	s_front = 0;

	s_surface.width  = AGA_W;
	s_surface.height = AGA_H;
	s_surface.pitch  = AGA_W;      /* chunky: 1 byte/pixel, tightly packed */
	s_surface.pixels = s_chunky;

	cop_build();
	cop_point_planes(s_planes[0]);

	/* Take the display: park the OS view, hand the copper ours. */
	s_oldview = GfxBase->ActiView;
	LoadView(NULL);
	WaitTOF();
	WaitTOF();
	CUSTOM->cop1lc  = (ULONG)s_cop;
	CUSTOM->copjmp1 = 0;                        /* strobe: restart at cop1lc */
	CUSTOM->dmacon  = (UWORD)(DMAF_SETCLR | DMAF_MASTER
	                          | DMAF_RASTER | DMAF_COPPER);
	return 0;
}

static void aga_shutdown_partial(void)
{
	if (s_cop)       { FreeMem(s_cop, COP_WORDS * sizeof(UWORD)); s_cop = NULL; }
	if (s_planes[0]) { FreeMem(s_planes[0], FRAME_BYTES); s_planes[0] = NULL; }
	if (s_planes[1]) { FreeMem(s_planes[1], FRAME_BYTES); s_planes[1] = NULL; }
	if (s_chunky)    { FreeMem(s_chunky, (ULONG)AGA_W * AGA_H); s_chunky = NULL; }
	if (GfxBase)     { CloseLibrary((struct Library *)GfxBase); GfxBase = NULL; }
}

static void aga_shutdown(void)
{
	if (GfxBase != NULL && s_oldview != NULL) {
		/* Give the display back: the OS view first, then its copper —
		 * the classic takeover epilogue. (RethinkDisplay would be the
		 * belt-and-braces extra, but it lives in intuition.library,
		 * which this backend never opens.) */
		LoadView(s_oldview);
		WaitTOF();
		WaitTOF();
		CUSTOM->cop1lc  = (ULONG)GfxBase->copinit;
		CUSTOM->copjmp1 = 0;
	}
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

	for (p = 0; p < 8; p++)
		planes[p] = back + (ULONG)p * AGA_PITCH * AGA_H;
	c2p_amiga(s_chunky, planes, AGA_W, AGA_H, AGA_PITCH);

	cop_point_planes(back);
	s_front ^= 1;
}

static void aga_present_rect(short x, short y, short w, short h)
{
	/* The c2p converts the whole frame; a partial-rect c2p is a later
	 * optimisation. */
	(void)x; (void)y; (void)w; (void)h;
	aga_present();
}

static void aga_set_palette(const dsp_color_t *colors, short first, short count)
{
	short i;

	if (s_cop == NULL)
		return;
	for (i = 0; i < count; i++) {
		short idx = (short)(first + i);
		const dsp_color_t *c;

		if (idx < 0 || idx > 255)
			continue;
		c = &colors[i];
		/* AGA 24-bit colour as two 12-bit register passes: the high
		 * nibbles land in the normal write, the low nibbles in the LOCT
		 * pass. The copper replays both every frame. */
		*s_cop_pal_hi[idx] = (UWORD)(((c->r & 0xF0) << 4)
		                            | (c->g & 0xF0)
		                            | ((c->b & 0xF0) >> 4));
		*s_cop_pal_lo[idx] = (UWORD)(((c->r & 0x0F) << 8)
		                            | ((c->g & 0x0F) << 4)
		                            | (c->b & 0x0F));
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
	/* aga_init itself refuses to run without KS3.0 + Lisa, so claiming the
	 * backend here is safe; an ECS fallback backend slots in later. */
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
