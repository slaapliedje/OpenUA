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

#include "display.h"

void c2p_amiga(const unsigned char *chunky, unsigned char *const planes[8],
               short w, short h, short plane_pitch);
void c2p_amiga_rect(const unsigned char *chunky, short chunky_pitch,
                    unsigned char *const planes[8], short plane_pitch,
                    short x0, short y0, short w, short h);

typedef unsigned long u32;

/* Delta-swap: exchange the bit-field (a & mask) with the field of `b` that
 * sits `shift` bits higher. The workhorse of the transpose. */
#define DSWAP(a, b, shift, mask) do {                          \
	u32 t_ = ((b) >> (shift) ^ (a)) & (mask);              \
	(a) ^= t_;                                             \
	(b) ^= t_ << (shift);                                  \
} while (0)

/* Transpose 32 chunky pixels (8 longwords, byte-major: pixel 0 is the top
 * byte of c[0]) into 8 planar longwords, out[p] = plane p, bit 31 = pixel 0.
 *
 * Stage view: the 16- and 8-shift stages sort the BYTE lanes (which pixel
 * group a register holds), the 4/2/1 stages transpose the bits inside each
 * byte lane (which PLANE a register holds). The pairings below were
 * validated against the naive scatter on the host — see the header note. */
static void transpose32(u32 c[8], u32 out[8])
{
	u32 a0 = c[0], a1 = c[1], a2 = c[2], a3 = c[3];
	u32 a4 = c[4], a5 = c[5], a6 = c[6], a7 = c[7];

	/* Byte-lane sort (16- then 8-shift swaps): after these, register
	 * position k holds pixels {r, r+8, r+16, r+24} for r in the order
	 * 0,4,1,5,2,6,3,7 — each lane one 8-pixel group. */
	DSWAP(a0, a4, 16, 0x0000FFFFUL);
	DSWAP(a1, a5, 16, 0x0000FFFFUL);
	DSWAP(a2, a6, 16, 0x0000FFFFUL);
	DSWAP(a3, a7, 16, 0x0000FFFFUL);

	DSWAP(a0, a2, 8, 0x00FF00FFUL);
	DSWAP(a1, a3, 8, 0x00FF00FFUL);
	DSWAP(a4, a6, 8, 0x00FF00FFUL);
	DSWAP(a5, a7, 8, 0x00FF00FFUL);

	/* In-byte transpose (4/2/1 delta swaps): pairings solved from the
	 * pixel order above — each shift-s stage pairs the registers whose
	 * pixels differ by s (scratchpad solve3.py, brute-force verified). */
	DSWAP(a0, a1, 4, 0x0F0F0F0FUL);
	DSWAP(a2, a3, 4, 0x0F0F0F0FUL);
	DSWAP(a4, a5, 4, 0x0F0F0F0FUL);
	DSWAP(a6, a7, 4, 0x0F0F0F0FUL);

	DSWAP(a0, a4, 2, 0x33333333UL);
	DSWAP(a2, a6, 2, 0x33333333UL);
	DSWAP(a1, a5, 2, 0x33333333UL);
	DSWAP(a3, a7, 2, 0x33333333UL);

	DSWAP(a0, a2, 1, 0x55555555UL);
	DSWAP(a4, a6, 1, 0x55555555UL);
	DSWAP(a1, a3, 1, 0x55555555UL);
	DSWAP(a5, a7, 1, 0x55555555UL);

	/* Register -> plane assignment falls out of the network. */
	out[7] = a0; out[3] = a1; out[6] = a2; out[2] = a3;
	out[5] = a4; out[1] = a5; out[4] = a6; out[0] = a7;
}

/* Load 32 chunky bytes as 8 big-endian longwords. On the 68k a plain long
 * read IS big-endian; on a little-endian host (the test harness) the shifts
 * below produce the same lane order, so the function is portable. */
static void load32(const unsigned char *src, u32 c[8])
{
	short k;

	for (k = 0; k < 8; k++) {
		c[k] = ((u32)src[0] << 24) | ((u32)src[1] << 16)
		     | ((u32)src[2] << 8)  |  (u32)src[3];
		src += 4;
	}
}

/* Rect form: convert only the given cell. `x0` and `w` must be multiples of 8
 * (the byte granularity of a bitplane row); the caller aligns. `chunky_pitch`
 * is the chunky buffer's full row width. Runs 32-pixel transposes over the
 * aligned span, with a naive 8-pixel tail for spans narrower than 32. */
void c2p_amiga_rect(const unsigned char *chunky, short chunky_pitch,
                    unsigned char *const planes[8], short plane_pitch,
                    short x0, short y0, short w, short h)
{
	short y, k;
	long  x;

	for (y = 0; y < h; y++) {
		const unsigned char *src =
		    chunky + (long)(y0 + y) * chunky_pitch + x0;
		long rowoff = (long)(y0 + y) * plane_pitch + (x0 >> 3);
		long bytecol = 0;

		x = 0;
		for (; x + 32 <= w; x += 32) {
			u32 c[8], o[8];

			load32(src + x, c);
			transpose32(c, o);
			for (k = 0; k < 8; k++) {
				unsigned char *d = planes[k] + rowoff + bytecol;
				u32 v = o[k];

				d[0] = (unsigned char)(v >> 24);
				d[1] = (unsigned char)(v >> 16);
				d[2] = (unsigned char)(v >> 8);
				d[3] = (unsigned char)v;
			}
			bytecol += 4;
		}
		/* tail: spans not a multiple of 32 finish 8 pixels at a time */
		for (; x < w; x += 8) {
			unsigned char pb[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

			for (k = 0; k < 8; k++) {
				unsigned char px  = src[x + k];
				unsigned char bit = (unsigned char)(0x80u >> k);
				short p;

				for (p = 0; p < 8; p++)
					if (px & (1u << p))
						pb[p] |= bit;
			}
			for (k = 0; k < 8; k++)
				planes[k][rowoff + (x >> 3)] = pb[k];
		}
	}
}

void c2p_amiga(const unsigned char *chunky, unsigned char *const planes[8],
               short w, short h, short plane_pitch)
{
	c2p_amiga_rect(chunky, w, planes, plane_pitch, 0, 0, w, h);
}
