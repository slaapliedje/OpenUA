/*
 * Nova / NVDI graphics-card display backend (ATW800/2 and any Nova-compatible
 * card).  ADR-0005 backend, chunky-native — the engine's happy path.
 *
 * STATUS: PROVISIONAL SCAFFOLD, gated on -DFRUA_NOVA so it is an empty object
 * in every shipping build and is never selected by dsp_detect() unless that
 * flag is set.  It exists so the post-probe session is "fill in four numbers"
 * rather than "start from scratch".  The pieces that need REAL values from a
 * hardware NOVA.LOG (see platform/nova_probe.c, docs/nova-card.md) are marked
 * TODO(NOVA.LOG); everything else is settled.
 *
 * Why a card is the cleanest non-Falcon Atari target: an 8bpp card is chunky
 * with the palette in hardware — the same identity as the TT/AGA ports, so
 * hw_palette = 1 and the #99 dirty-row present skip works immediately.  There
 * is NO chunky->planar conversion here at all (that whole ADR-0016 machine is
 * only for the bitplane machines).  A card build is conceptually display_videl
 * re-pointed at the card's linear aperture, plus display_rtg's "render chunky
 * rows, push to the card" data-flow.
 *
 * Data-flow (matches display_rtg): render the frame into a chunky surface in
 * FAST/ST-RAM and copy it to the card aperture at present.  Do NOT let the
 * 68000 write pixels one at a time across the bus into card VRAM.  A later
 * pass hands the big copies to the card's 2D blitter (130 MB/s on the ATW800/2)
 * through accelerated VDI, off the CPU entirely.
 */

#ifdef FRUA_NOVA

#include <stddef.h>             /* NULL */
#include <mint/osbind.h>        /* Getrez, Logbase, Physbase */
#include "display.h"
#include "dbglog.h"

/* ------------------------------------------------- minimal AES + VDI (trap #2)
 * Self-contained on purpose (a gated scaffold pulls in no shared state). The
 * probe proved this exact open sequence returns correct caps on hardware. */
static short contrl[12], intin[128], ptsin[128], intout[128], ptsout[128];
static long  vdipb[5];
static short aes_control[5], aes_global[16], aes_intin[16], aes_intout[16];
static long  aes_addrin[4], aes_addrout[4], aespb[6];

static void vdi(void)
{
	register long d0 __asm__("d0") = 0x73;
	register long d1 __asm__("d1");
	vdipb[0] = (long)contrl; vdipb[1] = (long)intin; vdipb[2] = (long)ptsin;
	vdipb[3] = (long)intout; vdipb[4] = (long)ptsout;
	d1 = (long)vdipb;
	__asm__ volatile ("trap #2" : "+d"(d0), "+d"(d1) :: "d2","a0","a1","a2","memory","cc");
	(void)d0; (void)d1;
}

static void aes(short op, short n_intout)
{
	register long d0 __asm__("d0") = 0xC8;
	register long d1 __asm__("d1");
	aes_control[0] = op; aes_control[1] = 0; aes_control[2] = n_intout;
	aes_control[3] = 0;  aes_control[4] = 0;
	aespb[0]=(long)aes_control; aespb[1]=(long)aes_global;
	aespb[2]=(long)aes_intin;   aespb[3]=(long)aes_intout;
	aespb[4]=(long)aes_addrin;  aespb[5]=(long)aes_addrout;
	d1 = (long)aespb;
	__asm__ volatile ("trap #2" : "+d"(d0), "+d"(d1) :: "d2","a0","a1","a2","memory","cc");
	(void)d0; (void)d1;
}

/* --------------------------------------------------------------- backend state
 * Confirmed on a real ATW800/2 + xVDI (data/work/ship/NOVA-card.log): the card
 * is 640x400x256 8bpp CHUNKY, aperture at Logbase()=$FEA00000. The engine
 * renders a 320x240 chunky surface in local RAM; present centres it into the
 * card framebuffer (640x400 = 2x 320x200, so a 320x240 window sits centred with
 * a black surround — a later pass 2x-scales to fill the screen). */
#define NOVA_SURF_W    320
#define NOVA_SURF_H    240      /* engine canvas (init asks for 320x240)        */
#define NOVA_CONTENT_H 200      /* the FRUA screen occupies the top 200 rows    */

static dsp_surface_t   s_surf;
static unsigned char  *s_chunky;        /* engine renders here (local RAM)      */
static unsigned char  *s_vram;          /* card linear aperture ($FEA00000)     */
static short           s_handle;        /* VDI virtual workstation              */
static short           s_cardw, s_cardh;/* card mode (640x400 confirmed)        */
static long            s_pitch;         /* card bytes/row                        */
static short           s_aes_ok;
static short           s_phys;          /* AES physical handle = the LIVE screen */

static short nova_open_ws(short *work_out)
{
	short i, phys;
	aes(10, 1);                     /* appl_init  */
	if (aes_intout[0] < 0) return 0;
	s_aes_ok = 1;
	aes(77, 5);                     /* graf_handle */
	phys = aes_intout[0];
	s_phys = phys;
	intin[0] = (short)(Getrez() + 2);
	for (i = 1; i < 10; i++) intin[i] = 1;
	intin[10] = 2;
	contrl[0] = 100; contrl[1] = 0; contrl[3] = 11; contrl[6] = phys;
	vdi();
	for (i = 0; i < 45; i++) work_out[i] = intout[i];
	return contrl[6];
}

static void nova_close_ws(void)
{
	if (s_handle) { contrl[0]=101; contrl[1]=0; contrl[3]=0; contrl[6]=s_handle; vdi(); s_handle=0; }
	if (s_aes_ok) { aes(19, 1); s_aes_ok = 0; }
}

/* Set one card CLUT entry via VDI vs_color (RGB 0..1000). */
static void nova_vs_color(short idx, short r, short g, short b)
{
	intin[0] = idx;
	intin[1] = (short)(((long)r * 1000) / 255);
	intin[2] = (short)(((long)g * 1000) / 255);
	intin[3] = (short)(((long)b * 1000) / 255);
	contrl[0] = 14; contrl[1] = 0; contrl[3] = 4; contrl[6] = s_handle;
	vdi();
}

#ifdef FRUA_NOVA_PALTEST
/* PALETTE DIAGNOSTIC: paint a full-hue-wheel ramp so ONE photo reveals how the
 * card maps a framebuffer BYTE to a colour. Set CLUT entry i to hue(i); fill
 * the screen left->right with value = x*256/width. Then halt so it persists.
 *
 *   identity      -> smooth red -> yellow -> green -> cyan -> blue -> magenta
 *   R<->B swap     -> the cyan band shows GOLD (cyan (0,255,255) -> (255,255,0)),
 *                     i.e. exactly the "cyan title renders gold" symptom
 *   permutation    -> scrambled colours instead of a smooth wheel
 *
 * The four corner blocks are pure R / G / B / white at known indices (0/1/2/3)
 * as an unambiguous channel-order key. */
static void nova_paltest(void)
{
	long x, y;
	short i;

	/* corner-key indices first (overwritten in the wheel below only for i<4,
	 * so set the wheel first, then stamp these). */
	for (i = 0; i < 256; i++) {
		long h = (long)i * 6;           /* 0..1530 across the wheel */
		short seg = (short)(h >> 8), f = (short)(h & 255);
		short r = 0, g = 0, b = 0;
		switch (seg) {
		case 0: r = 255;     g = f;       b = 0;       break; /* red->yellow  */
		case 1: r = 255 - f; g = 255;     b = 0;       break; /* yellow->green*/
		case 2: r = 0;       g = 255;     b = f;       break; /* green->cyan  */
		case 3: r = 0;       g = 255 - f; b = 255;     break; /* cyan->blue   */
		case 4: r = f;       g = 0;       b = 255;     break; /* blue->magenta*/
		default:r = 255;     g = 0;       b = 255 - f; break; /* magenta->red */
		}
		nova_vs_color(i, r, g, b);
	}
	nova_vs_color(0, 255, 0,   0);          /* index 0 = pure RED   */
	nova_vs_color(1, 0,   255, 0);          /* index 1 = pure GREEN */
	nova_vs_color(2, 0,   0,   255);        /* index 2 = pure BLUE  */
	nova_vs_color(3, 255, 255, 255);        /* index 3 = WHITE      */

	for (y = 0; y < s_cardh; y++)
		for (x = 0; x < s_cardw; x++)
			s_vram[y * s_pitch + x] =
				(unsigned char)((x * 256L) / s_cardw);

	/* corner blocks: 40x40 of index 0/1/2/3 at the four corners */
	for (y = 0; y < 40; y++)
		for (x = 0; x < 40; x++) {
			s_vram[y * s_pitch + x]                              = 0;
			s_vram[y * s_pitch + (s_cardw - 1 - x)]              = 1;
			s_vram[(s_cardh - 1 - y) * s_pitch + x]              = 2;
			s_vram[(s_cardh - 1 - y) * s_pitch + (s_cardw-1-x)]  = 3;
		}

	dbg_log("nova: PALTEST drawn — photograph the hue wheel + corners");
	for (;;) { }                            /* halt; reset after photographing */
}
#endif

/* ------------------------------------------------------------------- backend ops
 * The screen is already open (dsp_backend_nova confirmed 8bpp and left the
 * workstation open). init() only allocates the render surface + binds VRAM. */
static int nova_init(short want_w, short want_h)
{
	long i, n;
	(void)want_w; (void)want_h;

	/* Confirmed base = Logbase() (=$FEA00000 on the card). Pitch = card width
	 * for a linear chunky mode; if a future card pads its rows, read the true
	 * bytes/line from EdDI vq_scrninfo — the present already loops per row so
	 * only this constant changes. */
	s_vram  = (unsigned char *)Logbase();
	s_pitch = s_cardw;

	/* Stop dbg_log painting the card screen (Cconws draws into the framebuffer
	 * we now own) — route it to DBG.LOG so the debug trail stops overwriting the
	 * game. This is what let the play loop stay legible on the card. */
	dbg_log_screen_owned();

#ifdef FRUA_NOVA_PALTEST
	nova_paltest();                 /* draws the diagnostic + halts */
#endif

	/* Engine renders into local RAM, we push to the card (display_rtg model). */
	s_chunky = (unsigned char *)Mxalloc((long)NOVA_SURF_W * NOVA_SURF_H, 0);
	if ((long)s_chunky <= 0)
		s_chunky = (unsigned char *)Malloc((long)NOVA_SURF_W * NOVA_SURF_H);
	if ((long)s_chunky <= 0) { dbg_log("nova: surface alloc failed"); return 1; }

	/* Clear the render surface — the engine draws its 320x200 into the top, so
	 * the unused rows 200..239 must be black, not malloc garbage (that garbage
	 * was the static band at the bottom of the first centred render). */
	for (i = 0; i < (long)NOVA_SURF_W * NOVA_SURF_H; i++) s_chunky[i] = 0;

	/* Clear the whole card framebuffer to index 0. */
	n = (long)s_pitch * s_cardh;
	for (i = 0; i < n; i++) s_vram[i] = 0;

	s_surf.width  = NOVA_SURF_W;
	s_surf.height = NOVA_SURF_H;
	s_surf.pitch  = NOVA_SURF_W;
	s_surf.pixels = s_chunky;
	dbg_log_num("nova: up 8bpp chunky, card w = ", s_cardw);
	dbg_log_num("nova:                 card h = ", s_cardh);
	return 0;
}

static void nova_shutdown(void)
{
	nova_close_ws();
}

static dsp_surface_t *nova_surface(void) { return &s_surf; }

/* 2x-scale the 320x200 content to fill the 640x400 card: each engine pixel
 * becomes a 2x2 block, so the whole card is covered with no surround. (640x400
 * = exactly 2x 320x200 — the "doubling".) The per-row loop still lets a padded
 * card pitch be a one-constant change; a later pass hands this to the card's 2D
 * blitter. TODO: only the top NOVA_CONTENT_H rows carry the FRUA screen. */
static void nova_present_rect(short x, short y, short w, short h)
{
	short row;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > NOVA_SURF_W)    w = NOVA_SURF_W - x;
	if (y + h > NOVA_CONTENT_H) h = NOVA_CONTENT_H - y;
	if (w <= 0 || h <= 0) return;
	for (row = 0; row < h; row++) {
		const unsigned char *src = s_chunky + (long)(y + row) * NOVA_SURF_W + x;
		unsigned char       *d0  = s_vram + (long)((y + row) * 2) * s_pitch + (long)x * 2;
		unsigned char       *d1  = d0 + s_pitch;
		short n = w;
		while (n--) {
			unsigned char v = *src++;
			d0[0] = v; d0[1] = v;
			d1[0] = v; d1[1] = v;
			d0 += 2; d1 += 2;
		}
	}
}

static void nova_present(void) { nova_present_rect(0, 0, NOVA_SURF_W, NOVA_CONTENT_H); }

/* VDI reserves the first 16 pens and does NOT map pen p to hardware CLUT slot p
 * for p < 16 — it uses the standard Atari VDI pen->register table, so a
 * vs_color(p, ...) on a low pen lands on a DIFFERENT hardware slot. A framebuffer
 * byte b, however, selects hardware CLUT[b] directly. So to make framebuffer
 * index i (i < 16) show colour C we must set the pen whose hardware slot is i,
 * i.e. vs_color(hw_inverse[i], C). Confirmed on the ATW800/2 by the PALTEST
 * corners (fb 1 showed pen 2's blue, fb 2 pen 3's white, fb 3 pen 6's orange =
 * pen->hw {2->1, 3->2, 6->3}, the standard table). Pens >= 16 are identity, so
 * the churning art/3D indices are unaffected; only the low UI palette moved
 * (the "cyan title renders gold"). */
static const unsigned char nova_hw_inverse[16] =
	{ 0, 2, 3, 6, 4, 7, 5, 8, 9, 10, 11, 14, 12, 15, 13, 1 };

static void nova_set_palette(const dsp_color_t *c, short first, short count)
{
	short i;
#ifdef FRUA_NOVA_PALTRACE
	/* FIELD PROBE: the menu's hotkey letters paint CORRECTLY and then go black
	 * on a redraw. A wrong hw_inverse table cannot do that — it would be wrong
	 * on the first paint too — so the question is what happens to those CLUT
	 * entries BETWEEN the two paints. Log every palette write as
	 * first*1000+count, plus the RGB actually landing on a watched low index,
	 * so a later full 256-entry install (the boot log shows "clut 129
	 * installed, entries = 256" twice) shows up as the thing that overwrites
	 * the UI colours. Bounded so it cannot flood the card's log. */
	{
		static short pt_n;
		if (pt_n < 80) {
			pt_n++;
			dbg_file_num("paltrace: first*1000+count = ",
			             (long)first * 1000L + count);
			/* what this call puts on the UI range 0..15 (packed RGB) */
			/* BIT-packed, not decimal-packed: r/g/b are 0..255, so the
			 * old idx*1e6 + r*1e4 + g*1e2 + b spilled r>=100 into the
			 * index field and produced impossible indices. Shift instead —
			 * idx<16 so the whole value stays well inside a signed long. */
			for (i = 0; i < count; i++) {
				short idx = (short)(first + i);
				if (idx < 16)
					dbg_file_num("paltrace:   idx<<24|rgb  = ",
					             ((long)idx << 24)
					             | ((long)c[i].r << 16)
					             | ((long)c[i].g << 8) | c[i].b);
			}
		}
	}
#endif
	for (i = 0; i < count; i++) {
		short idx = (short)(first + i);
		short pen = (idx >= 0 && idx < 16)
		          ? (short)nova_hw_inverse[idx] : idx;
		nova_vs_color(pen, c[i].r, c[i].g, c[i].b);
	}
}

static const dsp_backend_t nova_backend = {
	"nova (8bpp graphics card)",
	nova_init,
	nova_shutdown,
	nova_surface,
	nova_present,
	nova_present_rect,
	nova_set_palette,
	1,      /* pages: single-buffered for now (present writes VRAM directly) */
	1,      /* hw_palette: index==CLUT slot, palette is hardware (TT/AGA identity) */
};

/* Detection lives HERE (not in init) so dsp_detect() can fall through to the
 * ST/STe backend when there is no card: open the screen workstation, read the
 * plane count, and return the backend only for a 256-colour (8-plane) chunky
 * screen — leaving the workstation OPEN for init()/shutdown(). Anything else
 * (no AES, or a paletted-16/planar ST screen) closes up and returns NULL. */
const dsp_backend_t *dsp_backend_nova(void)
{
	short work_out[57];
	short planes;

	s_handle = nova_open_ws(work_out);
	if (s_handle == 0) { dbg_log("nova: no AES/VDI screen"); nova_close_ws(); return NULL; }

	/* ★ Query the LIVE physical screen (graf_handle), NOT the v_opnvwk we just
	 * opened. A card at 640x400x256 makes Getrez() report 2 (ST-High), so the
	 * v_opnvwk(Getrez()+2) above binds to the ST-compat mode and reports planes
	 * 4/1 — the same trap that makes dsp_detect pick the ST-High backend. The
	 * AES physical handle is the real desktop screen (the card); vq_extnd on it
	 * reports the true depth. (Proven by nova_probe.c: on the card the live
	 * query gives 256/8 while the device-id path gives ST-compat.) The mode-0
	 * caps (size/colours) also come from the physical handle. */
	contrl[0] = 102; contrl[1] = 0; contrl[3] = 1; contrl[6] = s_phys;
	intin[0] = 0; vdi();            /* vq_extnd(mode 0) -> Open-Workstation caps */
	s_cardw = intout[0] + 1;
	s_cardh = intout[1] + 1;

	contrl[0] = 102; contrl[1] = 0; contrl[3] = 1; contrl[6] = s_phys;
	intin[0] = 1; vdi();            /* vq_extnd(mode 1) -> planes at [4] */
	planes = intout[4];
	dbg_log_num("nova: card width  = ", s_cardw);
	dbg_log_num("nova: card height = ", s_cardh);
	dbg_log_num("nova: planes      = ", planes);

	if (planes != 8) {              /* not a 256-colour chunky screen */
		dbg_log("nova: not 8bpp - handing back to the ST backend");
		nova_close_ws();
		return NULL;
	}
	return &nova_backend;
}

#endif /* FRUA_NOVA */
