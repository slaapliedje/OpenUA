/*
 * Nova / graphics-card discovery probe — see nova_probe.h.
 *
 * Everything here is gated on FRUA_NOVAPROBE so the shipping binary links an
 * empty object. The probe is deliberately self-contained: it talks to the VDI
 * through a raw trap #2 (no AES, no gembind dependency) and to the machine
 * through XBIOS/Supexec, so it runs whether or not GEM is up.
 *
 * What it records, and why a Nova-card backend author needs it:
 *   - The whole cookie jar. 'NOVA' / 'EdDI' / 'fVDI' / 'NVDI' prove the card
 *     + driver are present and give the EdDI entry point; '_VDO' stays 1 (STE)
 *     with a card fitted, which is exactly why dsp_detect can't rely on it.
 *   - v_opnwk() device caps (Compendium pp.522-524): work_out[0]/[1] = screen
 *     size-1, work_out[13] = #colours (256 => the engine's native depth).
 *   - vq_extnd(mode 1) (pp.550-551): work_out[4] = PLANES (8 => 256-colour
 *     chunky), work_out[5] = LUT flag. planes + colours together settle
 *     "is this an 8bpp paletted screen we can render into directly".
 *   - Physbase()/Logbase() and _v_bas_ad: the framebuffer base once the card
 *     is the active screen — the address the fast path writes chunky bytes to.
 *
 * The raw work_out arrays are dumped in full even where a field's meaning is
 * already known, so an index I mis-remember can still be decoded later against
 * the Compendium from the actual values.
 */

#ifdef FRUA_NOVAPROBE

#include <mint/osbind.h>        /* Getrez, Physbase, Logbase, Supexec, Fcreate... */

/* ------------------------------------------------------------------ output */

static char  g_out[16384];
static long  g_len;

static void emit(const char *s)
{
	while (*s && g_len < (long)sizeof(g_out) - 1)
		g_out[g_len++] = *s++;
}

static void emit_nl(void) { emit("\r\n"); }

static void emit_udec(unsigned long v)
{
	char tmp[12];
	short n = 0;
	if (v == 0) { emit("0"); return; }
	while (v && n < 11) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
	while (n) { char c[2]; c[0] = tmp[--n]; c[1] = 0; emit(c); }
}

static void emit_dec(long v)
{
	if (v < 0) { emit("-"); emit_udec((unsigned long)(-v)); }
	else emit_udec((unsigned long)v);
}

static void emit_hex(unsigned long v, short digits)
{
	static const char hx[] = "0123456789ABCDEF";
	char c[2];
	short i;
	emit("$");
	for (i = digits - 1; i >= 0; i--) {
		c[0] = hx[(v >> (i * 4)) & 0xF];
		c[1] = 0;
		emit(c);
	}
}

/* A cookie value printed as its 4-char tag, e.g. 0x4E4F5641 -> "NOVA". */
static void emit_tag(unsigned long v)
{
	char c[2];
	short i;
	c[1] = 0;
	for (i = 3; i >= 0; i--) {
		unsigned char ch = (unsigned char)(v >> (i * 8));
		c[0] = (ch >= 32 && ch < 127) ? (char)ch : '.';
		emit(c);
	}
}

/* ------------------------------------------------ supervisor-mode captures */

static long  g_cookie[64 * 2];
static short g_ncookie;
static long  g_v_bas_ad;        /* *(long*)0x44E — VT52/Line-A screen base   */
static short g_sshiftmd;        /* *(short*)0x44C — shifter mode             */

static long capture_super(void)
{
	long *jar = *(long **)0x5A0UL;
	short n = 0;

	if (jar) {
		for (; jar[0] != 0 && n < 63; jar += 2) {
			g_cookie[n * 2]     = jar[0];
			g_cookie[n * 2 + 1] = jar[1];
			n++;
		}
		/* record the terminator's second long too (jar count/slots) */
		g_cookie[n * 2]     = jar[0];
		g_cookie[n * 2 + 1] = jar[1];
	}
	g_ncookie   = n;
	g_v_bas_ad  = *(long *)0x44EUL;
	g_sshiftmd  = *(short *)0x44CUL;
	return 0;
}

/* ------------------------------------------------------- raw VDI (trap #2) */

static short contrl[12];
static short intin[128];
static short ptsin[128];
static short intout[128];
static short ptsout[128];
static long  vdipb[5];

static void vdi(void)
{
	register long d0 __asm__("d0") = 0x73;
	register long d1 __asm__("d1");

	vdipb[0] = (long)contrl;
	vdipb[1] = (long)intin;
	vdipb[2] = (long)ptsin;
	vdipb[3] = (long)intout;
	vdipb[4] = (long)ptsout;
	d1 = (long)vdipb;

	__asm__ volatile ("trap #2"
		: "+d"(d0), "+d"(d1)
		:
		: "d2", "a0", "a1", "a2", "memory", "cc");
	(void)d0; (void)d1;
}

/* ------------------------------------------------------- minimal AES (trap #2)
 *
 * The screen's physical workstation is owned by the AES; the correct way to
 * inquire it is graf_handle() + v_opnvwk() on that handle, NOT a second
 * v_opnwk() (which conflicts with the AES-owned open and can block). */
static short aes_control[5];
static short aes_global[16];
static short aes_intin[16];
static short aes_intout[16];
static long  aes_addrin[4];
static long  aes_addrout[4];
static long  aespb[6];

static void aes(short op, short n_intout)
{
	register long d0 __asm__("d0") = 0xC8;
	register long d1 __asm__("d1");

	aes_control[0] = op;
	aes_control[1] = 0;             /* n_intin  */
	aes_control[2] = n_intout;
	aes_control[3] = 0;             /* n_addrin */
	aes_control[4] = 0;             /* n_addrout*/
	aespb[0] = (long)aes_control; aespb[1] = (long)aes_global;
	aespb[2] = (long)aes_intin;   aespb[3] = (long)aes_intout;
	aespb[4] = (long)aes_addrin;  aespb[5] = (long)aes_addrout;
	d1 = (long)aespb;

	__asm__ volatile ("trap #2"
		: "+d"(d0), "+d"(d1)
		:
		: "d2", "a0", "a1", "a2", "memory", "cc");
	(void)d0; (void)d1;
}

static short g_aes_ok;
static short g_phys;             /* AES physical screen handle (the live screen) */

/* Open a virtual screen workstation via the AES physical handle. Returns the
 * virtual handle (0 = fail) and fills work_out[57]. */
static short ws_open(short *work_out)
{
	short i, phys;

	aes(10, 1);                     /* appl_init  -> ap_id in intout[0] */
	if (aes_intout[0] < 0)          /* no AES available */
		return 0;
	g_aes_ok = 1;

	aes(77, 5);                     /* graf_handle -> phys handle in [0] */
	phys = aes_intout[0];
	g_phys = phys;                  /* the AES-open screen ws = the LIVE screen */

	intin[0] = (short)(Getrez() + 2);
	for (i = 1; i < 10; i++)
		intin[i] = 1;
	intin[10] = 2;                  /* raster coordinates */

	contrl[0] = 100;                /* v_opnvwk */
	contrl[1] = 0;
	contrl[3] = 11;
	contrl[6] = phys;
	vdi();

	for (i = 0; i < 45; i++) work_out[i]      = intout[i];
	for (i = 0; i < 12; i++) work_out[45 + i] = ptsout[i];
	return contrl[6];
}

static void ws_extnd(short handle, short mode, short *work_out)
{
	short i;
	contrl[0] = 102;                /* vq_extnd */
	contrl[1] = 0;
	contrl[3] = 1;
	contrl[6] = handle;
	intin[0]  = mode;
	vdi();
	for (i = 0; i < 45; i++) work_out[i]      = intout[i];
	for (i = 0; i < 12; i++) work_out[45 + i] = ptsout[i];
}

static void ws_close(short handle)
{
	if (handle) {
		contrl[0] = 101;        /* v_clsvwk */
		contrl[1] = 0;
		contrl[3] = 0;
		contrl[6] = handle;
		vdi();
	}
	if (g_aes_ok) {
		aes(19, 1);             /* appl_exit */
		g_aes_ok = 0;
	}
}

/* Write the whole accumulated buffer to NOVA.LOG (truncate + rewrite). Called
 * as a checkpoint after each phase, so if a later VDI call hangs the machine,
 * the cookie jar + framebuffer base — the data a backend actually needs — are
 * already on disk, and the last line names the step that never returned. */
static void flush_log(void)
{
	long fd = Fcreate("NOVA.LOG", 0);
	if (fd >= 0) {
		Fwrite((short)fd, g_len, g_out);
		Fclose((short)fd);
	}
}

/* Dump a full 57-word work_out block, 8 per line. */
static void emit_workout(const short *w)
{
	short i;
	for (i = 0; i < 57; i++) {
		emit("  [");
		if (i < 10) emit(" ");
		emit_dec(i);
		emit("]=");
		emit_dec(w[i]);
		if ((i % 8) == 7) emit_nl();
	}
	emit_nl();
}

/* ----------------------------------------------------------------- driver */

void nova_probe_dump(void)
{
	short work_out[57];
	short handle;
	short i;
	short have_gfx = 0;

	g_len = 0;

	emit("OpenUA Nova / graphics-card probe"); emit_nl();
	emit("================================="); emit_nl();
	emit_nl();
	emit("phase: start"); emit_nl();
	flush_log();

	/* -- cookie jar + framebuffer base (never blocks) -------------------- */
	Supexec(capture_super);

	emit("_sshiftmd (0x44C) = "); emit_dec(g_sshiftmd); emit_nl();
	emit("_v_bas_ad (0x44E) = "); emit_hex((unsigned long)g_v_bas_ad, 8); emit_nl();
	emit("Getrez()          = "); emit_dec(Getrez()); emit_nl();
	emit("Logbase()         = "); emit_hex((unsigned long)Logbase(), 8); emit_nl();
	emit("Physbase()        = "); emit_hex((unsigned long)Physbase(), 8); emit_nl();
	emit_nl();

	emit("Cookie jar ("); emit_dec(g_ncookie); emit(" cookies):"); emit_nl();
	for (i = 0; i < g_ncookie; i++) {
		emit("  ");
		emit_tag((unsigned long)g_cookie[i * 2]);
		emit("  tag="); emit_hex((unsigned long)g_cookie[i * 2], 8);
		emit("  val="); emit_hex((unsigned long)g_cookie[i * 2 + 1], 8);
		/* annotate the ones a card backend cares about */
		switch ((unsigned long)g_cookie[i * 2]) {
		case 0x4E4F5641UL: emit("   <- NOVA graphics card"); have_gfx = 1; break;
		case 0x45644449UL: emit("   <- EdDI (screen-driver entry / vq_scrninfo)"); have_gfx = 1; break;
		case 0x66564449UL: emit("   <- fVDI"); have_gfx = 1; break;
		case 0x4E564449UL: emit("   <- NVDI"); have_gfx = 1; break;
		case 0x5F56444FUL: emit("   <- _VDO (video hw id, hi word)"); break;
		case 0x5F4D4348UL: emit("   <- _MCH (machine id)"); break;
		}
		emit_nl();
	}
	emit_nl();
	emit(have_gfx
	     ? "(a NOVA/EdDI/NVDI/fVDI cookie is present)"
	     : "(no NOVA/EdDI/NVDI/fVDI cookie — a card driver like xVDI may use a"
	       " DIFFERENT cookie; the VDI query below is what actually settles it)");
	emit_nl();
	emit("phase: cookies+base captured (this is the essential data)"); emit_nl();
	flush_log();

	/* -- VDI screen caps -------------------------------------------------
	 * The DECISIVE query. Trust neither Getrez() nor a specific cookie: a
	 * graphics card (xVDI/Nova/NVDI) at, say, 640x400x256 makes Getrez()
	 * report 2 (ST-High), so BOTH the engine's dsp_detect (Getrez()==2 ->
	 * ST-High mono backend) AND a v_opnvwk(Getrez()+2) land on the 640x400x2
	 * COMPAT mode instead of the real 256-colour card screen.
	 *
	 * The card's REAL screen is the physical workstation the AES already
	 * has open for the desktop. graf_handle() returns its handle, and
	 * vq_extnd() on THAT handle reports the card's true planes/colours — no
	 * new v_opnvwk, no device-id guess. This runs whenever the AES is up
	 * (appl_init succeeds); on a plain machine that just reports the ST
	 * screen (a fine control). Both are dumped so the discrepancy is on
	 * record. */
	emit("phase: opening AES + querying the LIVE screen..."); emit_nl();
	flush_log();

	handle = ws_open(work_out);     /* appl_init + graf_handle + v_opnvwk */
	if (!g_aes_ok) {
		emit("appl_init failed -> no AES/desktop. Boot into the card's GEM"); emit_nl();
		emit("desktop (run xVDI), then run this from the desktop."); emit_nl();
		emit("phase: done (no AES)"); emit_nl();
		flush_log();
		return;
	}

	emit("graf_handle (LIVE physical screen) = "); emit_dec(g_phys); emit_nl();

	/* (A) authoritative: query the AES physical handle directly */
	ws_extnd(g_phys, 0, work_out);
	emit("== LIVE SCREEN via vq_extnd(graf_handle) — the REAL card mode =="); emit_nl();
	emit("  width   = "); emit_dec(work_out[0] + 1); emit(" px"); emit_nl();
	emit("  height  = "); emit_dec(work_out[1] + 1); emit(" px"); emit_nl();
	emit("  colours = "); emit_dec(work_out[13]); emit_nl();
	emit("  full mode-0 work_out[57]:"); emit_nl();
	emit_workout(work_out);
	ws_extnd(g_phys, 1, work_out);
	emit("  planes(work_out[4]) = "); emit_dec(work_out[4]);
	emit(", lut(work_out[5]) = "); emit_dec(work_out[5]); emit_nl();
	emit("  => 2^planes = "); emit_dec(1L << work_out[4]);
	emit(" (planes==8 & colours==256 => 8bpp chunky card)"); emit_nl();
	emit("  full mode-1 work_out[57]:"); emit_nl();
	emit_workout(work_out);
	emit("phase: live-screen query done"); emit_nl();
	flush_log();

	/* (B) for the record: the device-id path the engine currently uses */
	emit("== v_opnvwk(Getrez()+2) — the DEVICE-ID path (may report ST-compat) =="); emit_nl();
	emit("  handle = "); emit_dec(handle); emit_nl();
	if (handle != 0) {
		ws_extnd(handle, 0, work_out);
		emit("  width  = "); emit_dec(work_out[0] + 1);
		emit(", height = "); emit_dec(work_out[1] + 1);
		emit(", colours = "); emit_dec(work_out[13]); emit_nl();
		ws_extnd(handle, 1, work_out);
		emit("  planes  = "); emit_dec(work_out[4]); emit_nl();
	}

	ws_close(handle);               /* v_clsvwk (if open) + appl_exit */

	emit("phase: done"); emit_nl();
	emit("-- end of probe --"); emit_nl();
	flush_log();
}

#else  /* !FRUA_NOVAPROBE — empty object in the shipping build */
typedef int nova_probe_translation_unit_not_empty;
#endif
