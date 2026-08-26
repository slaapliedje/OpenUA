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
#include <stdint.h>             /* uintptr_t (LUT probe address arithmetic) */
#include <string.h>             /* strncmp (video.cfg key parse) */
#include <mint/osbind.h>        /* Getrez, Logbase, Physbase */
#include "display.h"
#include "dbglog.h"
#include "planar.h"           /* planar_dirty_rows (announced-row narrowing) */

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

/* Hardware LUT state — declared here (not down with nova_lut_bind) because
 * nova_shutdown, further up the file, restores the desktop palette from it. */
static volatile unsigned short *s_lut;          /* card hardware LUT (256 x 565) */
static unsigned short           s_lut_want[256];/* what each slot SHOULD hold     */
static unsigned char            s_lut_set[256]; /* ...and which we have set       */
static unsigned short           s_lut_save[256];/* the DESKTOP's palette, for exit */
static short                    s_lut_saved;

/* ATW800/2 blitter present offload. The Seurat 2D engine copies CARD
 * memory to card memory (regs at VidMem top - 0x700 = LUT + 0x900, model
 * measured against xVDI 20260730 on the hatari-et4000 emulation:
 * word regs src.l/dst.l/sstride/dstride/width-bytes/rows/cmd; cmd 0x0003
 * copy, 0x0005 single-row fill, poll the cmd word to 0). It cannot read
 * Atari RAM, so the CPU still crosses the VME bus once per doubled pixel
 * — but the 2x row DUPLICATION moves to the card: the CPU writes each
 * doubled row once (not twice) and one blit per run copies the even rows
 * to the odd rows, halving the present's CPU bus traffic. The init clear
 * (512,000 CPU byte writes) becomes one fill blit from a zero row parked
 * just past the visible screen. Gated on the LUT bind (same xVDI-cookie
 * hardware as the LUT — a classic card bus-errors up here) and on
 * video.cfg `novablit=on` (OPT-IN); a bounded poll falls back to CPU
 * presents for the session if the engine ever fails to go idle.
 *
 * ⚠ DEFAULT OFF (2026-08-25): the register model above was derived from
 * tracing xVDI against OUR OWN Hatari emulation of the card — circular
 * evidence — and the first run on the real Mega STe ATW800/2 FAILED:
 * title pixels crammed into the top rows, then a black menu the mouse
 * cursor "paints" back in (rect presents are CPU and fine; every FULL
 * present went through the blit and drew nothing/garbage). The real
 * Seurat evidently disagrees with the emulation-derived model (register
 * semantics, completion, or per-mode pitch). Re-enable only after the
 * model is validated ON HARDWARE. */
static volatile unsigned short *s_blit;         /* word regs at LUT + 0x900 */
static short                    s_use_blit;
static short                    s_blit_cfg;     /* video.cfg novablit=on   */

static int nova_blit_wait(void)
{
	long n = 500000L;

	while (s_blit[8] != 0)
		if (--n <= 0) {
			s_use_blit = 0;
			dbg_log("nova: blitter never went idle - CPU presents from here");
			return 0;
		}
	return 1;
}

/* Program one operation and wait it out. src/dst are CARD offsets. */
static int nova_blit_op(unsigned short cmd, long src, long dst,
                        short sstride, short dstride, short wbytes, short rows)
{
	if (!nova_blit_wait())
		return 0;
	s_blit[0] = (unsigned short)((unsigned long)src >> 16);
	s_blit[1] = (unsigned short)src;
	s_blit[2] = (unsigned short)((unsigned long)dst >> 16);
	s_blit[3] = (unsigned short)dst;
	s_blit[4] = (unsigned short)sstride;
	s_blit[5] = (unsigned short)dstride;
	s_blit[6] = (unsigned short)wbytes;
	s_blit[7] = (unsigned short)rows;
	s_blit[8] = cmd;
	return nova_blit_wait();
}

/* Row-diffed full present (the ST backend's pass-1, ported). A full
 * qd_present used to rewrite all 512,000 card bytes over the VME bus with
 * per-byte writes — which is why TEXT was slow on the card while normal ST
 * mode was fine: every glyph update triggers a full present, and the whole
 * screen crossed the bus each time. The shadow mirrors what the card was
 * last given; a full present writes only the rows whose surface bytes
 * changed, and the comparisons run in fast local RAM. `novadiff=off` in
 * video.cfg restores the rewrite-everything behaviour for A/B. */
static unsigned char s_shadow[(long)NOVA_SURF_W * NOVA_CONTENT_H];
static short         s_row_diff = 1;

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
static void nova_lut_bind(void);        /* hardware LUT (RGB565) — defined below */
static void nova_res_restore(void);     /* undo the auto mode switch — below     */
static void nova_lut_assert(void);      /* re-stamp our palette onto the card    */

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

	/* Bind the card's hardware LUT (index == slot, RGB565) now that s_vram is
	 * known — the palette path prefers it over VDI pens, which cannot reach
	 * slot 15 on a 256-colour device. Verified by read-back; falls back silently. */
	nova_lut_bind();

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

	/* Clear the whole card framebuffer to index 0. With the blitter armed
	 * this is one fill blit from a zero row parked just past the visible
	 * screen (xVDI's own desktop-fill idiom: cmd 0x0005, source stride 0)
	 * instead of 512,000 CPU byte writes over the VME bus. */
	n = (long)s_pitch * s_cardh;
	{
		int cleared = 0;

		if (s_use_blit) {
			for (i = 0; i < s_pitch; i++)
				s_vram[n + i] = 0;
			cleared = nova_blit_op(0x0005, n, 0, 0,
			                       (short)s_pitch, (short)s_pitch,
			                       (short)s_cardh);
		}
		if (!cleared)
			for (i = 0; i < n; i++) s_vram[i] = 0;
	}
	memset(s_shadow, 0, sizeof s_shadow);   /* shadow == cleared card */

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

	/* Put the DESKTOP's video mode back before its colours: if we switched
	 * the card to 640x400 for the game, the driver's own p_chres returns it
	 * to whatever the user booted in. */
	nova_res_restore();

	/* Hand the desktop its colours back. AFTER closing the workstation, so a
	 * re-emit on the way out cannot land on top of the restore — and the restore
	 * is the last word either way. Without this the card keeps the game palette
	 * and TOS comes back with black icons / black window contents. */
	if (s_lut != NULL && s_lut_saved) {
		short i;
		for (i = 0; i < 256; i++)
			s_lut[i] = s_lut_save[i];
	}
}

static dsp_surface_t *nova_surface(void) { return &s_surf; }

/* 2x-scale the 320x200 content to fill the 640x400 card: each engine pixel
 * becomes a 2x2 block, so the whole card is covered with no surround. (640x400
 * = exactly 2x 320x200 — the "doubling".) The per-row loop still lets a padded
 * card pitch be a one-constant change; a later pass hands this to the card's 2D
 * blitter. TODO: only the top NOVA_CONTENT_H rows carry the FRUA screen. */
/* Stamp one doubled span onto the card and mirror it into the shadow. The
 * write side is the VME bus, where every access is a full bus transaction:
 * the old per-pixel byte stores cost four transactions per source pixel.
 * A doubled pixel is two identical bytes = one word, and two source pixels
 * make an aligned long, so the steady state is one LONG write per two
 * pixels per row — a quarter of the bus traffic. d0 is always word-aligned
 * (x*2 is even; the card base and pitch are even) and long-aligned when x
 * is even, which the head/tail words below arrange. */
static void nova_stamp_worker(short x, short y, short w, int both)
{
	const unsigned char *src = s_chunky + (long)y * NOVA_SURF_W + x;
	unsigned char       *d0  = s_vram + (long)(y * 2) * s_pitch + (long)x * 2;
	unsigned char       *d1  = d0 + s_pitch;
	short n = w;

	memcpy(s_shadow + (long)y * NOVA_SURF_W + x, src, (size_t)w);

	if ((x & 1) && n > 0) {                 /* head word: align to a long */
		unsigned short vv = (unsigned short)(*src * 0x0101u);
		*(unsigned short *)(void *)d0 = vv;
		if (both) *(unsigned short *)(void *)d1 = vv;
		src++; d0 += 2; d1 += 2; n--;
	}
	while (n >= 2) {
		unsigned long vv = ((unsigned long)src[0] * 0x01010000UL)
		                 | ((unsigned long)src[1] * 0x0101UL);
		*(unsigned long *)(void *)d0 = vv;
		if (both) *(unsigned long *)(void *)d1 = vv;
		src += 2; d0 += 4; d1 += 4; n -= 2;
	}
	if (n > 0) {                            /* tail word */
		unsigned short vv = (unsigned short)(*src * 0x0101u);
		*(unsigned short *)(void *)d0 = vv;
		if (both) *(unsigned short *)(void *)d1 = vv;
	}
}

static void nova_stamp_span(short x, short y, short w)
{
	nova_stamp_worker(x, y, w, 1);
}

static void nova_present_rect(short x, short y, short w, short h)
{
	short row;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > NOVA_SURF_W)    w = NOVA_SURF_W - x;
	if (y + h > NOVA_CONTENT_H) h = NOVA_CONTENT_H - y;
	if (w <= 0 || h <= 0) return;
	for (row = 0; row < h; row++)
		nova_stamp_span(x, (short)(y + row), w);
}

static void nova_present(void)
{
	/* Re-assert the palette once per full present. The engine writes the UI
	 * colours once and expects them to stay; anything else on the machine that
	 * re-emits the card's LUT (the VDI keeps its own CLUT shadow up to date and
	 * we can no longer mirror slot 15 to it — pen 1 addresses slot 255, not 15)
	 * would otherwise leave the hotkey letters wrong until the next palette
	 * install. 256 word writes against a 512,000-byte present is free. */
	nova_lut_assert();
	if (!s_row_diff) {
		nova_present_rect(0, 0, NOVA_SURF_W, NOVA_CONTENT_H);
		return;
	}
	/* Row-diffed: write only the rows whose bytes changed since the card
	 * was last given them. The compares run in local RAM; an unchanged
	 * screen costs zero VME traffic. Rect presents stamp through the same
	 * shadow, so a row they already delivered compares clean here.
	 *
	 * Narrowed to the ANNOUNCED rows (the TT's model, #63): a text update
	 * announces its ~16 rows, and scanning the other 184 every present is
	 * what kept card text slow even after the byte-writes fix — the engine
	 * presents per burst, so the whole-screen memcmp dominated. When the
	 * shim says "scan everything" (a palette change, boot), every row is
	 * checked exactly as before. */
	{
		const unsigned char *drows;
		int   all = planar_dirty_rows(&drows);
		short y;

		for (y = 0; y < NOVA_CONTENT_H; ) {
			const unsigned char *src = s_chunky + (long)y * NOVA_SURF_W;
			short y0;

			if ((!all && !drows[y])
			    || memcmp(s_shadow + (long)y * NOVA_SURF_W, src,
			              NOVA_SURF_W) == 0) {
				y++;
				continue;
			}
			/* A run of changed rows. With the blitter armed the CPU
			 * writes each doubled row ONCE (the even card row) and a
			 * single card-side blit copies the run's even rows down
			 * to the odd rows — half the CPU bus traffic. */
			y0 = y;
			do {
				nova_stamp_worker(0, y, NOVA_SURF_W, !s_use_blit);
				y++;
				src = s_chunky + (long)y * NOVA_SURF_W;
			} while (y < NOVA_CONTENT_H
			         && (all || drows[y])
			         && memcmp(s_shadow + (long)y * NOVA_SURF_W, src,
			                   NOVA_SURF_W) != 0);
			if (s_use_blit
			    && !nova_blit_op(0x0003,
			                     (long)(y0 * 2) * s_pitch,
			                     (long)(y0 * 2 + 1) * s_pitch,
			                     (short)(2 * s_pitch), (short)(2 * s_pitch),
			                     (short)(2 * NOVA_SURF_W), (short)(y - y0))) {
				short r;
				/* Blitter fell over mid-session: the odd rows of this
				 * run were never written — restamp it by CPU. */
				for (r = y0; r < y; r++)
					nova_stamp_span(0, r, NOVA_SURF_W);
			}
		}
	}
}

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

/* --- the slot-15 (UI white) problem, and the video.cfg knob that settles it ---
 *
 * MEASURED ON THE CARD: framebuffer slot 15 (the UI white — the hotkey letter on
 * an UNPRESSED button) is unreachable through vs_color. nova_hw_inverse is the
 * standard 16-COLOUR VDI pen->register table, in which register 15 is reached by
 * PEN 1; but pen 1 is VDI's BLACK, and on a 256-COLOUR device black is index
 * ncolors-1 = 255. Every white we wrote landed on slot 255 — leaving the letter
 * wrong AND clobbering a live art colour. Slot 11 (pen 14) works, which is why
 * PRESSED buttons showed their cyan correctly. PALTEST had validated pens 2/3/6
 * (-> slots 1/2/3) and never pen 1, so the gap survived.
 *
 * The fix is to stop using pens for this at all — see the hardware LUT below.
 * Slot 15 is simply never sent to vs_color now, which also ends the slot-255
 * corruption. docs/nova-palette.md has the full account.
 */

/* --- read back the card's HARDWARE LUT (ATW800/2 Programmer's Manual) --------
 *
 * The manual documents the FPGA LUT as memory-mapped and READ/WRITE:
 *     lut = screen_adr + A_LUT     (A_LUT = 0xFF000, span 0xFF000..0xFF1FF)
 *     lut += 1MB (2MB card) | 3MB (4MB card)
 * 0x200 bytes / 256 entries = 2 bytes per entry ("the 16bit (?) LUT"). The
 * documented absolute addresses (Mega ST 0xDFF000, Mega STE 0xBFF000, TT
 * 0xFEDFF000) are exactly base + 0xFF000 + 1MB, so they are the 2MB case.
 *
 * Reading it turns two open questions into measurements instead of guesses:
 *   1. WHERE our white actually landed. We write slot 15 via VDI pen 1 and it
 *      never shows; if LUT[255] holds the white and LUT[15] does not, then pen 1
 *      is the 256-colour BLACK pen (index ncolors-1) and slot 15 is simply
 *      unreachable through vs_color — which is the whole bug.
 *   2. The ENTRY FORMAT. Slot 11 (cyan 0x67FFFF) demonstrably works, so its
 *      entry is a known-RGB sample: comparing it against the raw word decodes
 *      the encoding (the manual only documents the 15bpp PIXEL layout, and
 *      assuming the LUT matches it would be a guess).
 *
 * Both answers are needed for the real fix — writing the LUT directly, the way
 * the TT/AGA backends own their hardware palette (hw_palette), which also drops
 * the VDI pen table and its unreachable slots entirely. Read-only for now.
 * Done under Supexec: the card lives in VME space. */
/* MEASURED ON THE CARD (2MB ATW800/2, ADDR jumper at 0x(FE)A00000):
 *
 *   Logbase()            = 0xFEA00000      (card video base)
 *   LUT                  = base + 0x1FF000 = 0xFEBFF000   <- reads fine
 *   base + 0x3FF000      = 0xFEDFF000                     <- BUS ERROR (2 bombs)
 *
 * The manual's absolute addresses (TT 0xFEDFF000) assume VidMem at 0x(FE)C00000;
 * with the ADDR jumper at 0x(FE)A00000 the LUT moves with it, so ALWAYS derive it
 * from Logbase() and never probe a second candidate blindly — the 4MB offset is
 * unmapped on a 2MB card and bombs. A 4MB card is opt-in via video.cfg.
 *
 * ENTRY FORMAT = RGB565, big-endian word, decoded from the read-back against
 * known colours (all canonical 565 values):
 *   slot 7  = 0xAD55 -> (172,170,172)  the alternating-bit grey
 *   slot 254= 0xFFE0 -> (255,255,0)    yellow
 *   slot 15 = 0xF81F -> (255,0,255)    magenta
 *   slot 255= 0xFFFF -> (255,255,255)  WHITE  <- where our UI white actually went
 *
 * That last row is the bug: writing framebuffer slot 15 through vs_color PEN 1
 * put the white on slot 255 (the 256-colour VDI black pen is index ncolors-1),
 * so the hotkey letter stayed magenta/dark AND a real art colour (255) got
 * clobbered. Writing this LUT directly makes every slot 0..255 reachable and
 * ends the pen-table indirection for good. */
#define NOVA_LUT_OFF_2MB  0x1FF000L
#define NOVA_LUT_OFF_4MB  0x3FF000L


/* Stamp every slot we own onto the card. Called after a palette batch's vs_color
 * calls (which can make the VDI re-emit its table over ours) and once per full
 * present, so our colours are the last word. 256 word writes is nothing next to
 * the 512,000-byte present. */
static void nova_lut_assert(void)
{
	short i;

	if (s_lut == NULL)
		return;

	/* Unconditional re-stamp. The VDI DOES overwrite this table in normal
	 * operation — measured on the card, slot 15 was found holding near-whites,
	 * a grey, the VDI magenta default and finally 0x0000 black — so a clobber is
	 * the expected steady state, not an error worth logging. Restoring is the
	 * whole mechanism, and it is cheaper than testing first. */
	for (i = 0; i < 256; i++)
		if (s_lut_set[i])
			s_lut[i] = s_lut_want[i];
}

static unsigned short nova_rgb565(unsigned char r, unsigned char g,
                                  unsigned char b)
{
	return (unsigned short)(((unsigned)(r >> 3) << 11)
	                      | ((unsigned)(g >> 2) << 5)
	                      |  (unsigned)(b >> 3));
}

/* The memory-mapped LUT is an ATW800/2 FPGA feature, NOT an ET4000 one — a
 * classic Nova/ET4000 card's DAC is I/O-port-only and its 1MB memory window
 * ends well before base+0x1FF000, so the probe write below BUS-ERRORS there
 * (found by running the classic NOVA-VDI driver on the hatari-et4000
 * emulated card). The ATW800/2 always ships with xVDI, so its cookie is the
 * gate; anything without it stays on vs_color. */
static long xvdi_cookie_super(void)
{
	long *jar = *(long **)0x5A0UL;          /* protected low RAM */

	if (jar == NULL)
		return 0;
	for (; jar[0] != 0; jar += 2)
		if (jar[0] == 0x78564449L)      /* 'xVDI' */
			return jar[1];          /* -> the driver's XCB block */
	return 0;
}

static long nova_cookie_super(void)
{
	long *jar = *(long **)0x5A0UL;

	if (jar == NULL)
		return 0;
	for (; jar[0] != 0; jar += 2)
		if (jar[0] == 0x4E4F5641L)      /* 'NOVA' (classic drivers) */
			return jar[1];
	return 0;
}

static long bootdev_super(void)
{
	return *(volatile unsigned short *)0x446UL;   /* _bootdev */
}

/* Bind + VERIFY (write / read back / restore). The probe only runs on an
 * ATW800/2 (xVDI cookie — see above); there the offset comes from Logbase(),
 * which the present already writes every frame from user mode, so the window
 * is known-good. `novalut=off` disables the direct path (back to vs_color
 * only); `novalut=on` forces the probe without the cookie; `novalut=4mb`
 * selects the 4MB offset (and implies the probe). */
static void nova_lut_bind(void)
{
	volatile unsigned short *lut;
	unsigned short           save, got;
	long                     off = NOVA_LUT_OFF_2MB;
	char                     buf[128];
	short                    fh;
	long                     n;
	int                      i;
	short                    force = 0;

	if (s_vram == NULL)
		return;
	fh = (short)Fopen("video.cfg", 0);
	if (fh >= 0) {
		n = Fread(fh, (long)sizeof buf - 1, buf);
		Fclose(fh);
		if (n > 0) {
			buf[n] = '\0';
			for (i = 0; buf[i] != '\0'; i++)
				if (buf[i] >= 'A' && buf[i] <= 'Z')
					buf[i] = (char)(buf[i] + 32);
			if (strstr(buf, "novadiff=off") != NULL) {
				s_row_diff = 0;
				dbg_log("nova: row-diff present disabled (video.cfg)");
			}
			if (strstr(buf, "novalut=off") != NULL) {
				dbg_log("nova: direct LUT disabled (video.cfg)");
				return;
			}
			if (strstr(buf, "novablit=on") != NULL) {
				s_blit_cfg = 1;
				dbg_log("nova: blitter presents ENABLED (video.cfg)");
			}
			if (strstr(buf, "novalut=on") != NULL)
				force = 1;
			if (strstr(buf, "novalut=4mb") != NULL) {
				off   = NOVA_LUT_OFF_4MB;
				force = 1;
			}
		}
	}

	if (!force && !Supexec(xvdi_cookie_super)) {
		dbg_log("nova: no xVDI cookie - classic card, vs_color path");
		return;
	}

	lut  = (volatile unsigned short *)(void *)
	       ((char *)s_vram + off);
	save = lut[255];
	lut[255] = 0x1234;
	got      = lut[255];
	lut[255] = save;
	if (got != 0x1234) {
		dbg_log("nova: LUT read-back failed - staying on vs_color");
		return;
	}
	s_lut = lut;

	/* Snapshot the DESKTOP's palette before we touch a single entry, so exiting
	 * can put it back. We now own this table outright and re-stamp it every
	 * present, so by exit the card holds the GAME's colours; closing the VDI
	 * workstation does not undo that, and the desktop returns with black icons
	 * and black window contents (reported on hardware, same symptom the ST/STE
	 * backend had for the same reason — see nova_shutdown). */
	{
		short i;
		for (i = 0; i < 256; i++)
			s_lut_save[i] = lut[i];
		s_lut_saved = 1;
	}
	dbg_log_num("nova: hardware LUT bound (RGB565) at ", (long)(uintptr_t)lut);

	/* Same FPGA, same gate: the 2D engine's registers sit at LUT + 0x900
	 * (VidMem top - 0x700) whatever the memory size. */
	if (s_blit_cfg) {
		s_blit = (volatile unsigned short *)(void *)((char *)lut + 0x900);
		s_use_blit = 1;
		dbg_log("nova: blitter presents armed (ATW800/2)");
	}
}

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
		if (pt_n < 400) {
			pt_n++;
			dbg_file_num("paltrace: first*1000+count = ",
			             (long)first * 1000L + count);
			/* what this call puts on the UI range 0..15 (packed RGB) */
			/* BIT-packed, not decimal-packed: r/g/b are 0..255, so the
			 * old idx*1e6 + r*1e4 + g*1e2 + b spilled r>=100 into the
			 * index field and produced impossible indices. Shift instead —
			 * idx<16 so the whole value stays well inside a signed long. */
			/* Only the two UI TEXT slots the black-hotkey report is about
			 * (11 = cyan accelerator, 15 = white accelerator). Logging all
			 * idx<16 burned the line budget during boot and never reached the
			 * char-gen page — the very install that "unlocks" the colour. Now
			 * every writer to 11/15 is visible for the whole session, so the
			 * LAST one before the menu paints is identifiable. */
			for (i = 0; i < count; i++) {
				short idx = (short)(first + i);
				if (idx == 11 || idx == 15) {
					dbg_file_num("paltrace:   idx<<24|rgb  = ",
					             ((long)idx << 24)
					             | ((long)c[i].r << 16)
					             | ((long)c[i].g << 8) | c[i].b);
					/* ...and the PEN it is routed to. The engine writes the
					 * correct cyan/white to 11/15 at the right time (proven on
					 * the ST backend), so if the card still shows black the
					 * suspect is this pen mapping — nova_hw_inverse, the one
					 * thing here that can only be validated on the card. If a
					 * LATER write of the SAME rgb to the SAME pen does show up
					 * (the char-gen page), the values are fine and the mapping
					 * / write-timing is the bug. */
					dbg_file_num("paltrace:     -> vs_color pen ",
					             (long)((idx >= 0 && idx < 16)
					                    ? nova_hw_inverse[idx] : idx));
				}
			}
		}
	}
#endif
	for (i = 0; i < count; i++) {
		short idx = (short)(first + i);
		short pen = (idx >= 0 && idx < 16)
		          ? (short)nova_hw_inverse[idx] : idx;
		/* Remember what this slot SHOULD be, then tell VDI. The LUT write comes
		 * afterwards, in a second pass — see nova_lut_assert(). */
		if (s_lut != NULL) {
			s_lut_want[idx] = nova_rgb565(c[i].r, c[i].g, c[i].b);
			s_lut_set[idx]  = 1;
		}

		/* Keep VDI's own colour table in step (its CLUT shadow is what a later
		 * VDI/AES redraw would re-emit) — EXCEPT slot 15. Its pen 1 does not
		 * address slot 15 on a 256-colour device; it addresses slot 255, so the
		 * write both misses AND overwrites a live art colour with the UI white.
		 * Skipped unconditionally: harmful when the LUT is bound, and merely
		 * useless when it is not. */
		if (idx != 15)
			nova_vs_color(pen, c[i].r, c[i].g, c[i].b);
	}

	/* ...and only NOW stamp the hardware LUT.
	 *
	 * FIELD REPORT: with the LUT write done inline, the hotkey letters came up
	 * WHITE and turned black a moment later. The cause is ordering inside this
	 * very loop: we wrote LUT[15] at i=15, then kept calling vs_color for
	 * indices 16..32, and each of those makes the VDI re-emit its own colour
	 * table — in which slot 15 is black — reverting us microseconds later. The
	 * manual's "CLUT shadow ... needs to be kept up-to-date by the VDI" is the
	 * same mechanism seen from the driver's side.
	 *
	 * So the LUT is asserted LAST, after every vs_color in the batch, and it
	 * re-asserts EVERY slot we have ever set — not just this range — so a VDI
	 * re-emit triggered by an unrelated range cannot leave an older slot wrong. */
	nova_lut_assert();
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

/* --------------------------------------------- automatic card-mode switching
 * Run the game at the card's native 640x400x256 and hand the desktop back
 * whatever mode it booted in (user request). The sanctioned interface is the
 * driver's XCB block, published through the 'xVDI' cookie (ATW800/2) or the
 * 'NOVA' cookie (classic cards): XCB+4 is the current index into the .BIB
 * resolution file and XCB+8 is p_chres(RESOLUTION *, ULONG fll_ofst=0) — the
 * very call XVDIMENU / MENU.PRG make (ATW800/2 Programmer's Manual pp.14-16).
 * RESOLUTION entries are 86 bytes; we match on the colour mode byte (+35,
 * 2 = 256 colours) and the HDI/VDI display-size words (+60/+68). The .BIB is
 * read from the BOOT drive's AUTO folder (the game's cwd is the data drive).
 * video.cfg `novares=off` opts out. */
#define XCB_RES_IDX	4
#define XCB_P_CHRES	8
#define RES_SIZE	86
#define RES_MODE_OFF	35
#define RES_HDI_OFF	60
#define RES_VDI_OFF	68
#define RES_MODE_256	2

static unsigned char s_res_old[RES_SIZE];       /* entry to restore at exit */
static long          s_res_xcb;                 /* XCB while switched       */
static short         s_res_switched;
static short         s_res_new_h;               /* height of the mode we set */

static unsigned short res_word(const unsigned char *r, int off)
{
	return (unsigned short)((r[off] << 8) | r[off + 1]);
}

static void nova_chres(long xcb, unsigned char *res)
{
	long fn = *(volatile long *)(uintptr_t)(xcb + XCB_P_CHRES);

	if (fn == 0)
		return;
	/* Register ABI, NOT C: XVDIMENU calls p_chres with the RESOLUTION in A0
	 * and fll_ofst in D0 (measured: `moveq #0,d0; lea res,a0; jsr ([xcb+8])`).
	 * A stack call leaves A0 as caller garbage and the card gets a junk mode. */
	{
		register long d0 __asm__("d0") = 0;             /* fll_ofst */
		register long a0 __asm__("a0") = (long)(uintptr_t)res;
		register long a1 __asm__("a1") = fn;
		__asm__ volatile ("jsr (%%a1)"
		                  : "+d"(d0), "+a"(a0), "+a"(a1)
		                  :
		                  : "d1", "d2", "a2", "memory", "cc");
	}
}

static int nova_res_cfg_off(void)
{
	char buf[128];
	short fh;
	long  n;
	int   i;

	fh = (short)Fopen("video.cfg", 0);
	if (fh < 0)
		return 0;
	n = Fread(fh, (long)sizeof buf - 1, buf);
	Fclose(fh);
	if (n <= 0)
		return 0;
	buf[n] = '\0';
	for (i = 0; buf[i] != '\0'; i++)
		if (buf[i] >= 'A' && buf[i] <= 'Z')
			buf[i] = (char)(buf[i] + 32);
	return strstr(buf, "novares=off") != NULL;
}

/* Try to switch the card to 640x400x256 (640x480x256 as fallback). Returns 1
 * when p_chres was called — the caller re-queries the screen either way. */
static int nova_res_switch(void)
{
	static unsigned char bib[40 * RES_SIZE];
	char  path[24];
	short fh;
	long  xcb, n, nres, i;
	long  pick = -1, cur = -1;
	unsigned char idx;

	if (nova_res_cfg_off())
		return 0;

	xcb = Supexec(xvdi_cookie_super);
	if (xcb != 0) {
		strcpy(path, "C:\\AUTO\\XVDI.BIB");
	} else {
		xcb = Supexec(nova_cookie_super);
		if (xcb == 0)
			return 0;
		strcpy(path, "C:\\AUTO\\STA_VDI.BIB");
	}
	path[0] = (char)('A' + Supexec(bootdev_super));

	fh = (short)Fopen(path, 0);
	if (fh < 0) {
		dbg_log("nova: no .BIB on the boot drive - keeping desktop mode");
		return 0;
	}
	n = Fread(fh, (long)sizeof bib, bib);
	Fclose(fh);
	nres = n / RES_SIZE;
	if (nres <= 0)
		return 0;

	/* the mode to play in: 640x400x256, else 640x480x256 */
	for (i = 0; i < nres; i++)
		if (bib[i * RES_SIZE + RES_MODE_OFF] == RES_MODE_256
		    && res_word(bib + i * RES_SIZE, RES_HDI_OFF) == 640) {
			if (res_word(bib + i * RES_SIZE, RES_VDI_OFF) == 400) {
				pick = i;
				break;
			}
			if (pick < 0 && res_word(bib + i * RES_SIZE, RES_VDI_OFF) == 480)
				pick = i;
		}
	if (pick < 0) {
		dbg_log("nova: no 640x256c entry in the .BIB - keeping desktop mode");
		return 0;
	}

	/* the mode to come back to: the XCB's current index — verified against
	 * the live screen size, with a scan fallback (the menu can re-sort). */
	idx = *(volatile unsigned char *)(uintptr_t)(xcb + XCB_RES_IDX);
	if (idx < nres
	    && res_word(bib + (long)idx * RES_SIZE, RES_HDI_OFF) == s_cardw
	    && res_word(bib + (long)idx * RES_SIZE, RES_VDI_OFF) == s_cardh)
		cur = idx;
	else
		for (i = 0; i < nres; i++)
			if (res_word(bib + i * RES_SIZE, RES_HDI_OFF) == s_cardw
			    && res_word(bib + i * RES_SIZE, RES_VDI_OFF) == s_cardh) {
				cur = i;
				break;
			}
	if (cur < 0) {
		dbg_log("nova: current mode not in the .BIB - keeping desktop mode");
		return 0;
	}
	if (cur == pick)
		return 0;               /* already exactly where we want to be */

	memcpy(s_res_old, bib + cur * RES_SIZE, RES_SIZE);
	s_res_new_h = (short)res_word(bib + pick * RES_SIZE, RES_VDI_OFF);
	dbg_log_num("nova: switching card mode for the game, .BIB entry = ", pick);
	nova_chres(xcb, bib + pick * RES_SIZE);
	s_res_xcb = xcb;
	s_res_switched = 1;
	return 1;
}

static void nova_res_restore(void)
{
	if (!s_res_switched)
		return;
	s_res_switched = 0;
	dbg_log("nova: restoring the desktop's card mode");
	nova_chres(s_res_xcb, s_res_old);
}

const dsp_backend_t *dsp_backend_nova(void)
{
	short work_out[57];
	short planes;

	/* Belt and braces with dsp_detect()'s cache: nova_open_ws does appl_init +
	 * v_opnvwk unconditionally and overwrites s_handle, so a second probe used
	 * to leak the first workstation AND its AES slot. On success this function
	 * returns with the workstation deliberately still open, so "already open"
	 * is the normal steady state — reuse it rather than stacking another. */
	if (s_handle != 0)
		return &nova_backend;

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

	/* Not the game's native 640x400x256 (a big desktop, a 640x480 boot mode,
	 * or a TrueColor desktop where planes > 8)? Ask the driver to switch.
	 * vq_extnd would still answer with the PRE-switch caps (the VDI's device
	 * info is not rebuilt at runtime), so on success the truth is the mode we
	 * just set: 640 wide, mode-2 = 8 planes, height from the .BIB entry. The
	 * TT-Low guard below is untouched — a TT shifter mode has no xVDI/NOVA
	 * cookie, so the switch can never fire there. */
	if (!(planes == 8 && s_cardw == 640 && s_cardh == 400)
	    && nova_res_switch()) {
		s_cardw = 640;
		s_cardh = s_res_new_h;
		planes  = 8;
		dbg_log_num("nova: card switched for the game, h = ", s_cardh);
	}

	/* TT guard, load-bearing now that the TT probes Nova too: TT-Low is ALSO
	 * 8 planes / 256 colours, but it is the TT's own 320x480 PLANAR shifter
	 * mode — binding to it would write chunky bytes over interleaved
	 * bitplanes. The card's smallest mode is 640 wide, so width tells them
	 * apart. (The LUT read-back is no guard here: on a TT screen the probe
	 * address lands in plain RAM and reads back fine.) */
	if (planes == 8 && s_cardw < 640) {
		dbg_log("nova: 8bpp but <640 wide = TT-Low shifter, not a card");
		nova_close_ws();
		return NULL;
	}

	if (planes != 8) {              /* not a 256-colour chunky screen */
		/* The card IS present — vq_extnd answered — but the desktop is in
		 * a mode we cannot render into: our present writes raw 8-bit
		 * INDICES, which only mean anything on an 8bpp LUT screen. At 16bpp+
		 * the framebuffer holds real pixels and there is no palette to
		 * index. Say so explicitly (field report: "detects the GPU only in
		 * 8bpp" looked like a probe failure — it is a depth mismatch). */
		if (planes > 8)
			dbg_log_num("nova: card found but not in 8bpp - set the "
			            "desktop to 256 colours to use it; planes = ",
			            planes);
		else
			dbg_log("nova: not 8bpp - handing back to the ST backend");
		nova_close_ws();
		return NULL;
	}
	return &nova_backend;
}

#endif /* FRUA_NOVA */
