/*
 * Chunky-to-planar for the AGA display backend (ADR-0012) — portable C.
 * Scatters an 8bpp chunky buffer into 8 SEPARATE bitplanes (the layout
 * Denise/Lisa DMA reads; the Falcon c2p's word-interleaved output is not
 * usable here — see display_aga.c).
 *
 * Shape: for every 8-pixel group, bit p of each chunky pixel gathers into one
 * byte of plane p, MSB = leftmost pixel. `w` must be a multiple of 8 (320 is).
 *
 * Implementation: the classic masked-swap bit-matrix transpose, 32 pixels per
 * step. The 32 chunky bytes are loaded as 8 longwords (an 8x32 bit matrix)
 * and transposed with five delta-swap stages (16/8/4/2/1) into 8 longwords of
 * planar data, one per plane — ~4 ops/pixel instead of the ~25 of the naive
 * per-pixel scatter this replaced. That naive version cost ~1 SECOND per
 * 320x200 frame on a 14MHz 68EC020, which made every modal loop pass (each
 * ends in a jt1134 full present) take seconds — the "slow mouse"/"laggy keys"
 * bring-up bug. Verified bit-identical to the naive scatter by the host-side
 * equivalence test (tests/test_c2p_amiga.py drives it).
 *
 * A hand-scheduled asm version (true Kalms) is a further ~2-3x if real
 * hardware wants it; this C version is 68000-clean for the ECS ladder.
 */

#include "c2p32.h"             /* the shared 32-pixel transpose network */
#include "c2p_amiga.h"        /* public prototypes; must agree with below */

void c2p_amiga(const unsigned char *chunky, unsigned char *const planes[8],
               short w, short h, short plane_pitch);
void c2p_amiga_rect(const unsigned char *chunky, short chunky_pitch,
                    unsigned char *const planes[8], short plane_pitch,
                    short x0, short y0, short w, short h);
/* N-plane forms (the ECS backend uses 5): scatter only the low `nplanes`
 * bitplanes. The transpose still computes all 8 outputs (cheap), but pixels
 * remapped to 0..2^nplanes-1 carry zero in the high planes, so those are
 * simply not written. */
void c2p_amiga_n(const unsigned char *chunky, unsigned char *const planes[],
                 short w, short h, short plane_pitch, short nplanes);
void c2p_amiga_n_rect(const unsigned char *chunky, short chunky_pitch,
                      unsigned char *const planes[], short plane_pitch,
                      short x0, short y0, short w, short h, short nplanes);

/* ---- the table converter -------------------------------------------------
 *
 * One 8-pixel group at a time: for pixel k of the group (k = 0 leftmost) and
 * chunky value v, s_tab[v][k] holds the eight bytes that pixel contributes,
 * one per plane — byte p is (0x80 >> k) if bit p of v is set. OR the eight
 * entries together and byte p IS the group's byte of plane p. The entry is
 * kept as two 32-bit halves so that the ORs are longword operations; it is
 * built and read through its bytes, so the host's byte order does not matter.
 *
 * What it is for: finishing a span that is not a multiple of 32. That tail
 * used to go through a per-pixel, per-plane bit loop, and the walk viewport
 * is 88 pixels wide — two transposes and 24 pixels of tail on every row.
 * Measured with the tail on the table instead: the viewport present fell
 * from 24 ticks to 20 on an A500 build and from 5 to 2 on an A1200 build.
 *
 * It is NOT a replacement for the transpose, even on a plain 68000 where the
 * transpose's 32-bit shifts are loops. Tried: a full 5-plane frame through
 * the table cost 1.28x the transposes (GCC's indexing of the 8-byte entries
 * is the expensive part).
 *
 * 16 KB, filled on first use. */
typedef unsigned int c2p_t32;
typedef char c2p_t32_is_32_bits[sizeof(c2p_t32) == 4 ? 1 : -1];

typedef union c2p_tab_entry {
	c2p_t32       l[2];         /* planes 0..3, planes 4..7 */
	unsigned char b[8];
} c2p_tab_entry;

/* [value][position], so one scaled index (value * 64) reaches all eight of a
 * value's entries with a small constant displacement — which is the only
 * kind of displacement a 68000's indexed addressing has. */
static c2p_tab_entry s_tab[256][8];
static char          s_tab_ready;

static void tab_build(void)
{
	short k, v, p;

	for (v = 0; v < 256; v++)
		for (k = 0; k < 8; k++)
			for (p = 0; p < 8; p++)
				s_tab[v][k].b[p] = (v & (1 << p))
				    ? (unsigned char)(0x80u >> k) : 0;
	s_tab_ready = 1;
}

/* Entry for pixel i of the group at s. */
#define C2P_E(s, i) \
	((const c2p_tab_entry *)(const void *) \
	 ((const char *)s_tab + ((unsigned short)(s)[i] << 6)) + (i))

/* Convert `groups` 8-pixel groups of one row. `off` is the first group's byte
 * offset within each plane. Three bodies, so that the 5-plane case (ECS)
 * carries one longword and one byte rather than two longwords. */
static void tab_row(const unsigned char *src, unsigned char *const planes[],
                    long off, short groups, short nplanes)
{
	short k;

	if (!s_tab_ready)
		tab_build();
	if (nplanes == 5) {
		unsigned char *p0 = planes[0] + off, *p1 = planes[1] + off;
		unsigned char *p2 = planes[2] + off, *p3 = planes[3] + off;
		unsigned char *p4 = planes[4] + off;

		for (; groups > 0; groups--, src += 8) {
			c2p_tab_entry e;
			const c2p_tab_entry *t;
			unsigned char hi;

			t = C2P_E(src, 0); e.l[0]  = t->l[0]; hi  = t->b[4];
			t = C2P_E(src, 1); e.l[0] |= t->l[0]; hi |= t->b[4];
			t = C2P_E(src, 2); e.l[0] |= t->l[0]; hi |= t->b[4];
			t = C2P_E(src, 3); e.l[0] |= t->l[0]; hi |= t->b[4];
			t = C2P_E(src, 4); e.l[0] |= t->l[0]; hi |= t->b[4];
			t = C2P_E(src, 5); e.l[0] |= t->l[0]; hi |= t->b[4];
			t = C2P_E(src, 6); e.l[0] |= t->l[0]; hi |= t->b[4];
			t = C2P_E(src, 7); e.l[0] |= t->l[0]; hi |= t->b[4];
			*p0++ = e.b[0]; *p1++ = e.b[1];
			*p2++ = e.b[2]; *p3++ = e.b[3];
			*p4++ = hi;
		}
		return;
	}
	if (nplanes <= 4) {
		for (; groups > 0; groups--, src += 8, off++) {
			c2p_tab_entry e;

			e.l[0] = C2P_E(src, 0)->l[0] | C2P_E(src, 1)->l[0]
			       | C2P_E(src, 2)->l[0] | C2P_E(src, 3)->l[0]
			       | C2P_E(src, 4)->l[0] | C2P_E(src, 5)->l[0]
			       | C2P_E(src, 6)->l[0] | C2P_E(src, 7)->l[0];
			for (k = 0; k < nplanes; k++)
				planes[k][off] = e.b[k];
		}
		return;
	}
	for (; groups > 0; groups--, src += 8, off++) {
		const c2p_tab_entry *t;
		c2p_tab_entry e;

		t = C2P_E(src, 0); e.l[0]  = t->l[0]; e.l[1]  = t->l[1];
		t = C2P_E(src, 1); e.l[0] |= t->l[0]; e.l[1] |= t->l[1];
		t = C2P_E(src, 2); e.l[0] |= t->l[0]; e.l[1] |= t->l[1];
		t = C2P_E(src, 3); e.l[0] |= t->l[0]; e.l[1] |= t->l[1];
		t = C2P_E(src, 4); e.l[0] |= t->l[0]; e.l[1] |= t->l[1];
		t = C2P_E(src, 5); e.l[0] |= t->l[0]; e.l[1] |= t->l[1];
		t = C2P_E(src, 6); e.l[0] |= t->l[0]; e.l[1] |= t->l[1];
		t = C2P_E(src, 7); e.l[0] |= t->l[0]; e.l[1] |= t->l[1];
		for (k = 0; k < nplanes; k++)
			planes[k][off] = e.b[k];
	}
}

/* -DC2P_AMIGA_TABLE=1 sends the whole span through the table. It is slower
 * (see above); the switch exists so the host test can drive the table over
 * full frames as well as tails. */
#ifndef C2P_AMIGA_TABLE
# define C2P_AMIGA_TABLE 0
#endif

/* Rect form: convert only the given cell. `x0` and `w` must be multiples of 8
 * (the byte granularity of a bitplane row); the caller aligns. `chunky_pitch`
 * is the chunky buffer's full row width. Runs 32-pixel transposes over the
 * aligned span, and the table for whatever is left over. */
static void scatter_rect(const unsigned char *chunky, short chunky_pitch,
                         unsigned char *const planes[], short plane_pitch,
                         short x0, short y0, short w, short h, short nplanes)
{
	short y, k;
	long  x;

	(void)k;
	for (y = 0; y < h; y++) {
		const unsigned char *src =
		    chunky + (long)(y0 + y) * chunky_pitch + x0;
		long rowoff = (long)(y0 + y) * plane_pitch + (x0 >> 3);
		long bytecol = 0;

		x = 0;
		for (; !C2P_AMIGA_TABLE && x + 32 <= w; x += 32) {
			c2p_u32 c[8], o[8];

			c2p_load32(src + x, c);
			c2p_transpose32(c, o);
			for (k = 0; k < nplanes; k++) {
				unsigned char *d = planes[k] + rowoff + bytecol;
				c2p_u32 v = o[k];

				d[0] = (unsigned char)(v >> 24);
				d[1] = (unsigned char)(v >> 16);
				d[2] = (unsigned char)(v >> 8);
				d[3] = (unsigned char)v;
			}
			bytecol += 4;
		}
		if (x < w)
			tab_row(src + x, planes, rowoff + (x >> 3),
			        (short)((w - x) >> 3), nplanes);
	}
}

void c2p_amiga_rect(const unsigned char *chunky, short chunky_pitch,
                    unsigned char *const planes[8], short plane_pitch,
                    short x0, short y0, short w, short h)
{
	scatter_rect(chunky, chunky_pitch, planes, plane_pitch, x0, y0, w, h, 8);
}

void c2p_amiga(const unsigned char *chunky, unsigned char *const planes[8],
               short w, short h, short plane_pitch)
{
	scatter_rect(chunky, w, planes, plane_pitch, 0, 0, w, h, 8);
}

void c2p_amiga_n_rect(const unsigned char *chunky, short chunky_pitch,
                      unsigned char *const planes[], short plane_pitch,
                      short x0, short y0, short w, short h, short nplanes)
{
	scatter_rect(chunky, chunky_pitch, planes, plane_pitch,
	             x0, y0, w, h, nplanes);
}

void c2p_amiga_n(const unsigned char *chunky, unsigned char *const planes[],
                 short w, short h, short plane_pitch, short nplanes)
{
	scatter_rect(chunky, w, planes, plane_pitch, 0, 0, w, h, nplanes);
}
