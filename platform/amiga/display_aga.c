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
 * The mouse pointer is hardware sprites 0+1 (attached, 15 colours), fed by
 * the input VBL — it tracks the mouse at video rate with no c2p cost at all
 * (the software fallback repainted a full frame per move: the "slow mouse").
 *
 * Status: VERIFIED on amiberry (boots to the main menu, 7108aa1). Known
 * bring-up simplifications, all flagged inline: pointer/palette patches are
 * not VBL-synchronised (a one-frame glitch under heavy churn, never a tear
 * lock), and the display is NTSC-timed 200 lines inside a PAL-tolerant
 * window.
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
extern void c2p_amiga_rect(const unsigned char *chunky, short chunky_pitch,
                           unsigned char *const planes[8], short plane_pitch,
                           short x0, short y0, short w, short h);

#include "input.h"               /* plat_mouse_pos — the VBL cursor reads it */

/* --- backend state ------------------------------------------------------- */

struct GfxBase *GfxBase;                        /* opened here, v39+ (KS 3.0) */

static unsigned char *s_chunky;                 /* the engine's 8bpp buffer   */
static unsigned char *s_planes[2];              /* double-buffered plane sets */
static int            s_front;                  /* which set is on screen     */
static dsp_surface_t  s_surface;
static struct View   *s_oldview;                /* to restore on shutdown     */

/* CHIP-RAM size of one 8-bitplane frame (8 separate contiguous planes). */
#define FRAME_BYTES ((ULONG)AGA_PITCH * AGA_H * AGA_DEPTH)

/* --- hardware-sprite cursor state -----------------------------------------
 * The mouse pointer is sprites 0+1 ATTACHED (4-bit pixels = 15 colours +
 * transparent — enough for the colour pointer the shim can push). Image and
 * control words live in CHIP RAM; the position words are rewritten every
 * frame by the input VBL server (amiga_display_vbl_cursor), so the pointer
 * tracks the mouse at video rate no matter what the engine is doing — the
 * whole reason it exists (the software fallback repainted through a
 * full-frame c2p per move). */

/* One 16px sprite: POS/CTL + 16 lines x 2 data words + 2 terminator words. */
#define SPR_WORDS  (2 + 16 * 2 + 2)

static UWORD *s_spr[2];                 /* attached cursor pair, CHIP        */
static UWORD *s_spr_null;               /* the parked/hidden sprite, CHIP    */
static short  s_cur_hotx, s_cur_hoty;
static volatile short s_cur_ready;      /* sprite image pushed at least once */
static volatile short s_cur_visible;
static volatile short s_cur_obscured;   /* ObscureCursor: until mouse moves  */
static short  s_cur_last_h, s_cur_last_v;   /* VBL-only: obscure release     */
static short  s_cur_shown;              /* VBL-only: pointers at cursor?     */

/* The cursor's claim on palette bank 0xF0: pixel value v displays
 * COLOR(240+v). Kept here to re-assert after every game set_palette — the
 * game CLUT nominally owns all 256 entries, so art pixels using 241..255
 * would show cursor colours instead; nothing in the shipped designs has
 * surfaced yet, and the alternative (no hardware pointer) is worse. */
static UWORD  s_cur_pal_hi[16], s_cur_pal_lo[16];
static unsigned short s_cur_pal_rgb[16];    /* RGB565 -> value map           */
static short  s_cur_pal_n;              /* values 1..n in use                */

/* --- the copper list ------------------------------------------------------
 *
 * Layout (every entry is a MOVE = 2 words; WAIT ends the list):
 *   prologue: FMODE/BPLCONx/BPLxMOD/DIWSTRT/DIWSTOP/DDFSTRT/DDFSTOP
 *   plane block: 8 x { BPLxPTH, hi ; BPLxPTL, lo }         (patched per flip)
 *   sprite block: 8 x { SPRxPTH, hi ; SPRxPTL, lo }        (patched: cursor)
 *   palette block: 8 banks x { BPLCON3=bank ; 32 x COLORxx(high) ;
 *                              BPLCON3=bank|LOCT ; 32 x COLORxx(low) }
 *   WAIT 0xFFFF,0xFFFE
 *
 * The sprite block sits BEFORE the palette block: the copper restarts at the
 * top of every frame and must have re-pointed the sprite channels before
 * their control-word DMA slots (~line 25) — the 500-odd palette moves would
 * push them past that.
 *
 * s_cop_bpl points at the first plane-pointer OPERAND word; s_cop_pal[i]
 * points at colour i's high/low operand words, so both patch in place. */

#define COP_PROLOGUE_MOVES  12
#define COP_PLANE_MOVES     (8 * 2)
#define COP_SPR_MOVES       (8 * 2)
#define COP_PAL_MOVES       (8 * (1 + 32 + 1 + 32))
#define COP_WORDS           ((COP_PROLOGUE_MOVES + COP_PLANE_MOVES \
                              + COP_SPR_MOVES + COP_PAL_MOVES) * 2 + 2)

static UWORD *s_cop;                    /* the copper list, CHIP RAM         */
static UWORD *s_cop_bpl;                /* first BPLxPTH operand word        */
static UWORD *s_cop_spr[8];             /* SPRxPTH operand word per channel  */
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
#define R_SPR0PTH  0x120                /* +4 per sprite channel             */
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
	/* BPLCON2 = 0x0024: PF2P = PF1P = 4 — ALL sprites in front of the
	 * playfield. Zero here would bury the sprite-0 mouse pointer UNDER
	 * the picture. */
	cl = cop_move(cl, R_BPLCON2, 0x0024);
	cl = cop_move(cl, R_BPLCON3, 0x0000);
	/* BPLCON4 = 0x00FF: BPLAM (high byte) untouched; ESPRM = OSPRM = 0xF
	 * puts the sprite colours in palette bank 0xF0 — an attached-pair
	 * cursor pixel value v reads COLOR(240+v). The cursor palette writer
	 * below owns those entries (see cursor_pal_write). */
	cl = cop_move(cl, R_BPLCON4, 0x00FF);
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

	/* Sprite pointers: sprite DMA consumes them every frame, so the copper
	 * must rewrite all 8 each frame. 0/1 = the attached cursor pair (or the
	 * null sprite while hidden); 2-7 always the null sprite. Patched by
	 * cop_point_sprite. */
	for (p = 0; p < 8; p++) {
		s_cop_spr[p] = cl + 1;
		cl = cop_move(cl, (UWORD)(R_SPR0PTH + p * 4),     0);  /* hi */
		cl = cop_move(cl, (UWORD)(R_SPR0PTH + p * 4 + 2), 0);  /* lo */
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

static void cop_point_sprite(short chan, UWORD *spr)
{
	ULONG addr = (ULONG)spr;

	s_cop_spr[chan][0] = (UWORD)(addr >> 16);
	s_cop_spr[chan][2] = (UWORD)(addr & 0xFFFF);
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
	s_spr[0]    = AllocMem(SPR_WORDS * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);
	s_spr[1]    = AllocMem(SPR_WORDS * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);
	s_spr_null  = AllocMem(4 * sizeof(UWORD),         MEMF_CHIP | MEMF_CLEAR);
	if (s_chunky == NULL || s_planes[0] == NULL
	    || s_planes[1] == NULL || s_cop == NULL
	    || s_spr[0] == NULL || s_spr[1] == NULL || s_spr_null == NULL) {
		dbg_log("aga: AllocMem failed (chip for planes/copper?)");
		aga_shutdown_partial();
		return 1;
	}
	s_front = 0;
	s_cur_ready = 0;
	s_cur_shown = 0;

	s_surface.width  = AGA_W;
	s_surface.height = AGA_H;
	s_surface.pitch  = AGA_W;      /* chunky: 1 byte/pixel, tightly packed */
	s_surface.pixels = s_chunky;

	cop_build();
	cop_point_planes(s_planes[0]);
	{
		short s;
		for (s = 0; s < 8; s++)
			cop_point_sprite(s, s_spr_null);
	}

	/* Take the display: park the OS view, hand the copper ours. */
	s_oldview = GfxBase->ActiView;
	LoadView(NULL);
	WaitTOF();
	WaitTOF();
	{
		/* Park the hardware sprite pointers too before enabling their
		 * DMA — the copper only rewrites them at the NEXT frame top,
		 * and one frame of garbage-pointer sprites is visible. */
		short s;
		for (s = 0; s < 8; s++)
			CUSTOM->sprpt[s] = (APTR)s_spr_null;
	}
	CUSTOM->cop1lc  = (ULONG)s_cop;
	CUSTOM->copjmp1 = 0;                        /* strobe: restart at cop1lc */
	CUSTOM->dmacon  = (UWORD)(DMAF_SETCLR | DMAF_MASTER
	                          | DMAF_RASTER | DMAF_COPPER | DMAF_SPRITE);
	return 0;
}

static void aga_shutdown_partial(void)
{
	s_cur_ready = 0;    /* the input VBL server must stop touching sprites */
	if (s_spr[0])    { FreeMem(s_spr[0], SPR_WORDS * sizeof(UWORD)); s_spr[0] = NULL; }
	if (s_spr[1])    { FreeMem(s_spr[1], SPR_WORDS * sizeof(UWORD)); s_spr[1] = NULL; }
	if (s_spr_null)  { FreeMem(s_spr_null, 4 * sizeof(UWORD)); s_spr_null = NULL; }
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

#ifdef FRUA_KBTRACE
static long s_kbt_full, s_kbt_rect;
#endif

static void aga_present(void)
{
	unsigned char *back = s_planes[s_front ^ 1];
	unsigned char *planes[8];
	short p;

#ifdef FRUA_KBTRACE
	if (++s_kbt_full <= 30 || (s_kbt_full % 200) == 0)
		dbg_file_num("kbt: full present #", s_kbt_full);
#endif

	for (p = 0; p < 8; p++)
		planes[p] = back + (ULONG)p * AGA_PITCH * AGA_H;
	c2p_amiga(s_chunky, planes, AGA_W, AGA_H, AGA_PITCH);

	cop_point_planes(back);
	s_front ^= 1;
}

static void aga_present_rect(short x, short y, short w, short h)
{
	/* Partial update: convert just the touched cell straight into the
	 * DISPLAYED plane set — no flip. The front buffer stays a complete
	 * current frame (full presents rewrite the back buffer entirely), and
	 * a small unsynchronised write risks at most one frame of shear
	 * inside the cell. This mattered: the full-frame c2p made every
	 * 16x16 pane repaint cost a 64000-pixel conversion. */
	unsigned char *front = s_planes[s_front];
	unsigned char *planes[8];
	short p, x1;

#ifdef FRUA_KBTRACE
	if (++s_kbt_rect <= 30 || (s_kbt_rect % 200) == 0) {
		dbg_file_num("kbt: rect present #", s_kbt_rect);
		dbg_file_num("kbt: rect y=", y);
	}
#endif

	if (x < 0) { w = (short)(w + x); x = 0; }
	if (y < 0) { h = (short)(h + y); y = 0; }
	if (x + w > AGA_W) w = (short)(AGA_W - x);
	if (y + h > AGA_H) h = (short)(AGA_H - y);
	if (w <= 0 || h <= 0)
		return;

	x1 = (short)((x + w + 7) & ~7);         /* byte-align the cell */
	x  = (short)(x & ~7);

	for (p = 0; p < 8; p++)
		planes[p] = front + (ULONG)p * AGA_PITCH * AGA_H;
	c2p_amiga_rect(s_chunky, AGA_W, planes, AGA_PITCH,
	               x, y, (short)(x1 - x), h);
}

static void cursor_pal_reassert(void)
{
	short v;

	for (v = 1; v <= s_cur_pal_n; v++) {
		*s_cop_pal_hi[240 + v] = s_cur_pal_hi[v];
		*s_cop_pal_lo[240 + v] = s_cur_pal_lo[v];
	}
}

static void aga_set_palette(const dsp_color_t *colors, short first, short count)
{
	short i;

#ifdef FRUA_KBTRACE
	{ extern long g_kbt_setpal; g_kbt_setpal++; }
#endif
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
	/* The sprite cursor's bank-0xF0 entries win over game writes. */
	if (s_cur_ready)
		cursor_pal_reassert();
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
 * The pointer is hardware sprites 0+1 attached. The shim pushes a 16x16
 * RGB565 sprite + mask; each distinct opaque colour gets a pixel value
 * 1..15 whose palette entry (240+v) is written into the copper's palette
 * table. Position tracking runs in the input backend's VERTB server via
 * amiga_display_vbl_cursor() below. With this active the shim never
 * composites a software cursor — no c2p per mouse move at all. */

int plat_cursor_active(void)
{
	return s_spr[0] != NULL;           /* the display takeover succeeded */
}

void plat_cursor_set_sprite(const unsigned short *rgb565,
                            const unsigned short *mask, short hotx, short hoty)
{
	short r, c, v;

	if (s_spr[0] == NULL)
		return;

	s_cur_pal_n = 0;
	for (r = 0; r < 16; r++) {
		UWORD p0 = 0, p1 = 0, p2 = 0, p3 = 0;   /* spr0 A/B, spr1 A/B */

		for (c = 0; c < 16; c++) {
			UWORD bit = (UWORD)(0x8000u >> c);
			unsigned short rgb;

			if (!(mask[r] & bit))
				continue;               /* transparent */
			rgb = rgb565[r * 16 + c];
			for (v = 1; v <= s_cur_pal_n; v++)
				if (s_cur_pal_rgb[v] == rgb)
					break;
			if (v > s_cur_pal_n) {
				if (s_cur_pal_n < 15) {
					/* New colour: widen RGB565 channels to 8
					 * bits (replicating top bits) and stage the
					 * two AGA passes for COLOR(240+v). */
					unsigned char r8 = (unsigned char)(((rgb >> 11) << 3) | ((rgb >> 13) & 7));
					unsigned char g8 = (unsigned char)((((rgb >> 5) & 0x3F) << 2) | ((rgb >> 9) & 3));
					unsigned char b8 = (unsigned char)(((rgb & 0x1F) << 3) | ((rgb >> 2) & 7));

					s_cur_pal_n = v;
					s_cur_pal_rgb[v] = rgb;
					s_cur_pal_hi[v] = (UWORD)(((r8 & 0xF0) << 4)
					                         | (g8 & 0xF0)
					                         | ((b8 & 0xF0) >> 4));
					s_cur_pal_lo[v] = (UWORD)(((r8 & 0x0F) << 8)
					                         | ((g8 & 0x0F) << 4)
					                         | (b8 & 0x0F));
				} else {
					v = s_cur_pal_n;    /* out of values: reuse */
				}
			}
			if (v & 1) p0 |= bit;
			if (v & 2) p1 |= bit;
			if (v & 4) p2 |= bit;
			if (v & 8) p3 |= bit;
		}
		s_spr[0][2 + r * 2]     = p0;
		s_spr[0][2 + r * 2 + 1] = p1;
		s_spr[1][2 + r * 2]     = p2;
		s_spr[1][2 + r * 2 + 1] = p3;
	}
	cursor_pal_reassert();

	s_cur_hotx  = hotx;
	s_cur_hoty  = hoty;
	s_cur_ready = 1;                    /* the VBL may drive it now */
}

void plat_cursor_show(int visible)
{
	s_cur_visible = (short)(visible != 0);
}

void plat_cursor_obscure(void)
{
	s_cur_obscured = 1;                 /* until the next mouse movement */
}

/* Called from the input backend's VERTB server, after it has integrated the
 * frame's mouse delta (platform-internal; see input_amiga.c). Rewrites the
 * sprite POS/CTL words and the copper's sprite-0/1 pointers — during the
 * vertical blank, so before this frame's sprite control-word DMA. */
void amiga_display_vbl_cursor(void)
{
	short h, v, hs, vs, ve;
	UWORD pos, ctl;

	if (!s_cur_ready)
		return;

	plat_mouse_pos(&h, &v);
	if (s_cur_obscured && (h != s_cur_last_h || v != s_cur_last_v))
		s_cur_obscured = 0;
	s_cur_last_h = h;
	s_cur_last_v = v;

	if (!s_cur_visible || s_cur_obscured) {
		if (s_cur_shown) {
			cop_point_sprite(0, s_spr_null);
			cop_point_sprite(1, s_spr_null);
			s_cur_shown = 0;
		}
		return;
	}

	/* Screen (0,0) sits at raster (V=0x2C, H=0x81) — the DIW top-left.
	 * Sprite HSTART is in lores pixels but leads the display by one, so
	 * 0x80 aligns the sprite's first column with pixel 0. */
	hs = (short)(0x80 + h - s_cur_hotx);
	vs = (short)(0x2C + v - s_cur_hoty);
	ve = (short)(vs + 16);

	pos = (UWORD)(((vs & 0xFF) << 8) | ((hs >> 1) & 0xFF));
	ctl = (UWORD)(((ve & 0xFF) << 8)
	              | (((vs >> 8) & 1) << 2)
	              | (((ve >> 8) & 1) << 1)
	              | (hs & 1));
	s_spr[0][0] = pos;
	s_spr[0][1] = ctl;
	s_spr[1][0] = pos;
	s_spr[1][1] = (UWORD)(ctl | 0x0080);        /* ATTACH on the odd sprite */

	if (!s_cur_shown) {
		cop_point_sprite(0, s_spr[0]);
		cop_point_sprite(1, s_spr[1]);
		s_cur_shown = 1;
	}
}

#endif /* FRUA_AMIGA */
