/*
 * novapaltest — standalone Nova/NVDI/xVDI palette-mapping diagnostic.
 *
 * NO FRUA engine, NO game data, NO dependencies — a self-contained Atari TOS
 * program (built -m68000, runs on any ST/STe/TT/Falcon + an 8bpp graphics
 * card). Freely distributable: it contains none of the game.
 *
 * It opens the CURRENT VDI screen (the desktop), and if that screen is an
 * 8-bit paletted graphics card (planes == 8, e.g. an ATW800/2 running xVDI at
 * 640x400x256) it draws a diagnostic straight into the card framebuffer, waits
 * for a key, and exits. On any other screen it prints a one-line message and
 * exits without touching video.
 *
 * The diagnostic answers one question: for a framebuffer BYTE value V, what
 * colour does the card show? It sets a KNOWN palette and paints:
 *
 *   - TOP STRIP: 16 wide vertical bars, bar k filled with the byte value k
 *     (k = 0..15). Their palette is a fixed, maximally-distinct 16-colour key
 *     (see PAL16 below). If value k shows key colour k, the low indices are
 *     identity. If they are shifted or some are reserved (black/white), the
 *     bars make it obvious.
 *   - BELOW: a smooth hue wheel over values 16..255 (red->yellow->green->cyan
 *     ->blue->magenta->red). Confirms the mid/high range is identity (a swap
 *     reverses it; a permutation scrambles it).
 *
 * Build:  m68k-atari-mint-gcc -m68000 -O2 -o novapaltest.prg tools/novapaltest.c
 * Run:    double-click novapaltest.prg from the card's GEM desktop; photograph;
 *         press any key to quit.
 */

#include <mint/osbind.h>

/* ---- known 16-colour key for the low-index strip (0..15) ---------------- */
static const unsigned char PAL16[16][3] = {
	{  0,  0,  0},  /*  0 black   */
	{255,255,255},  /*  1 white   */
	{255,  0,  0},  /*  2 red     */
	{  0,255,  0},  /*  3 green   */
	{  0,  0,255},  /*  4 blue    */
	{255,255,  0},  /*  5 yellow  */
	{  0,255,255},  /*  6 cyan    */
	{255,  0,255},  /*  7 magenta */
	{128,128,128},  /*  8 grey    */
	{255,128,  0},  /*  9 orange  */
	{128,  0,255},  /* 10 violet  */
	{128, 64,  0},  /* 11 brown   */
	{255,128,192},  /* 12 pink    */
	{128,255,  0},  /* 13 lime    */
	{  0,128,128},  /* 14 teal    */
	{ 64, 64, 64},  /* 15 dkgrey  */
};

/* ---- minimal AES + VDI via trap #2 (no gembind dependency) --------------- */
static short contrl[12], intin[128], ptsin[128], intout[128], ptsout[128];
static long  vdipb[5];
static short ac[5], ag[16], ai[16], ao[16];
static long  aai[4], aao[4], apb[6];

static void vdi(void)
{
	register long d0 __asm__("d0") = 0x73;
	register long d1 __asm__("d1");
	vdipb[0]=(long)contrl; vdipb[1]=(long)intin; vdipb[2]=(long)ptsin;
	vdipb[3]=(long)intout; vdipb[4]=(long)ptsout;
	d1 = (long)vdipb;
	__asm__ volatile ("trap #2" : "+d"(d0),"+d"(d1) :: "d2","a0","a1","a2","memory","cc");
	(void)d0; (void)d1;
}

static void aes(short op, short nout)
{
	register long d0 __asm__("d0") = 0xC8;
	register long d1 __asm__("d1");
	ac[0]=op; ac[1]=0; ac[2]=nout; ac[3]=0; ac[4]=0;
	apb[0]=(long)ac; apb[1]=(long)ag; apb[2]=(long)ai;
	apb[3]=(long)ao; apb[4]=(long)aai; apb[5]=(long)aao;
	d1 = (long)apb;
	__asm__ volatile ("trap #2" : "+d"(d0),"+d"(d1) :: "d2","a0","a1","a2","memory","cc");
	(void)d0; (void)d1;
}

static void vs_color(short h, short idx, short r, short g, short b)
{
	intin[0]=idx;
	intin[1]=(short)(((long)r*1000)/255);
	intin[2]=(short)(((long)g*1000)/255);
	intin[3]=(short)(((long)b*1000)/255);
	contrl[0]=14; contrl[1]=0; contrl[3]=4; contrl[6]=h;
	vdi();
}

static void msg(const char *s) { Cconws(s); }

int main(void)
{
	short phys, planes, w, h;
	long  base, pitch;
	unsigned char *vram;
	short i, x, y;
	short strip;

	aes(10, 1);                             /* appl_init */
	if (ao[0] < 0) { msg("novapaltest: no AES.\r\n"); return 1; }
	aes(77, 5);                             /* graf_handle */
	phys = ao[0];

	/* vq_extnd(mode 0) -> size/colours; (mode 1) -> planes, on the LIVE screen */
	contrl[0]=102; contrl[1]=0; contrl[3]=1; contrl[6]=phys; intin[0]=0; vdi();
	w = intout[0] + 1;
	h = intout[1] + 1;
	contrl[0]=102; contrl[1]=0; contrl[3]=1; contrl[6]=phys; intin[0]=1; vdi();
	planes = intout[4];

	if (planes != 8) {
		msg("novapaltest: this screen is not 8bpp (need a 256-colour\r\n");
		msg("graphics-card mode, e.g. xVDI 640x400x256). Nothing drawn.\r\n");
		aes(19, 1);                     /* appl_exit */
		return 0;
	}

	base  = (long)Logbase();
	pitch = w;                              /* linear chunky: pitch == width */
	vram  = (unsigned char *)base;

	/* palette: 0..15 = the distinct key; 16..255 = a smooth hue wheel */
	for (i = 0; i < 16; i++)
		vs_color(phys, i, PAL16[i][0], PAL16[i][1], PAL16[i][2]);
	for (i = 16; i < 256; i++) {
		long  hh = (long)(i - 16) * 6 * 256 / 240;   /* 0..1535 across 16..255 */
		short seg = (short)(hh >> 8), f = (short)(hh & 255);
		short r=0,g=0,b=0;
		switch (seg) {
		case 0: r=255;   g=f;     b=0;    break;
		case 1: r=255-f; g=255;   b=0;    break;
		case 2: r=0;     g=255;   b=f;    break;
		case 3: r=0;     g=255-f; b=255;  break;
		case 4: r=f;     g=0;     b=255;  break;
		default:r=255;   g=0;     b=255-f;break;
		}
		vs_color(phys, i, r, g, b);
	}

	/* draw. top strip = 16 index bars (value k), below = hue wheel (16..255) */
	strip = (short)(h / 4);
	for (y = 0; y < h; y++) {
		unsigned char *row = vram + (long)y * pitch;
		if (y < strip) {
			for (x = 0; x < w; x++)
				row[x] = (unsigned char)((long)x * 16 / w);      /* 0..15 */
		} else {
			for (x = 0; x < w; x++)
				row[x] = (unsigned char)(16 + (long)x * 240 / w); /* 16..255 */
		}
	}

	Cconin();                               /* wait for a key */

	aes(19, 1);                             /* appl_exit */
	return 0;
}
