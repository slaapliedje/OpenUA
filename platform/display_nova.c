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
#define NOVA_SURF_W 320
#define NOVA_SURF_H 240

static dsp_surface_t   s_surf;
static unsigned char  *s_chunky;        /* engine renders here (local RAM)      */
static unsigned char  *s_vram;          /* card linear aperture ($FEA00000)     */
static short           s_handle;        /* VDI virtual workstation              */
static short           s_cardw, s_cardh;/* card mode (640x400 confirmed)        */
static long            s_pitch;         /* card bytes/row                        */
static short           s_xoff, s_yoff;  /* where the 320x240 window lands        */
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
	s_xoff  = (short)((s_cardw - NOVA_SURF_W) / 2);
	s_yoff  = (short)((s_cardh - NOVA_SURF_H) / 2);

	/* Engine renders into local RAM, we push to the card (display_rtg model). */
	s_chunky = (unsigned char *)Mxalloc((long)NOVA_SURF_W * NOVA_SURF_H, 0);
	if ((long)s_chunky <= 0)
		s_chunky = (unsigned char *)Malloc((long)NOVA_SURF_W * NOVA_SURF_H);
	if ((long)s_chunky <= 0) { dbg_log("nova: surface alloc failed"); return 1; }

	/* Clear the whole card framebuffer to index 0 (black surround). */
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

/* Copy the chunky surface to the card aperture, row by row (handles a padded
 * card pitch). TODO(NOVA.LOG): if s_pitch==s_w this is one flat copy; the row
 * loop is here so a padded pitch just works once the real value is filled in.
 * A later pass replaces this with a blitter blit. */
static void nova_present_rect(short x, short y, short w, short h)
{
	short row;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > NOVA_SURF_W) w = NOVA_SURF_W - x;
	if (y + h > NOVA_SURF_H) h = NOVA_SURF_H - y;
	if (w <= 0 || h <= 0) return;
	for (row = 0; row < h; row++) {
		const unsigned char *src = s_chunky + (long)(y + row) * NOVA_SURF_W + x;
		unsigned char       *dst = s_vram
		                         + (long)(s_yoff + y + row) * s_pitch
		                         + (s_xoff + x);
		short n = w;
		while (n--) *dst++ = *src++;
	}
}

static void nova_present(void) { nova_present_rect(0, 0, NOVA_SURF_W, NOVA_SURF_H); }

/* Hardware CLUT via VDI. TODO(NOVA.LOG): the card is index==slot (hw_palette),
 * so this is correct; only the 0..1000 scaling is VDI-standard. A faster path
 * writes the card CLUT registers directly once their address is known. */
static void nova_set_palette(const dsp_color_t *c, short first, short count)
{
	short i;
	for (i = 0; i < count; i++) {
		intin[0] = first + i;
		intin[1] = (short)((c[i].r * 1000) / 255);
		intin[2] = (short)((c[i].g * 1000) / 255);
		intin[3] = (short)((c[i].b * 1000) / 255);
		contrl[0] = 14; contrl[1] = 0; contrl[3] = 4; contrl[6] = s_handle;
		vdi();
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
