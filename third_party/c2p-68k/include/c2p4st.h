/*
 * c2p4st.h — nibble-input chunky-to-planar for ST low (4 word-interleaved
 * bitplanes), with the remap LUT folded into the load.
 *
 * The quantized ST backend remaps every pixel to 0..15 before conversion, so
 * the full 8-plane transpose (c2p32.h) wastes most of its work on known-zero
 * bits. Three reductions, each verified by tests/test_c2p4st.py against the
 * naive scatter:
 *
 *  1. The transpose's first two stages (the 16- and 8-shift byte-lane sort)
 *     are pure BYTE PERMUTATIONS — so the load places each (LUT-remapped)
 *     byte directly at its post-stage-2 position, and both stages vanish.
 *     Register k loads pixels {c, c+8, c+16, c+24} with c = 0,4,1,5,2,6,3,7.
 *  2. With every byte's high nibble zero, the 4-shift stage degenerates:
 *     t = ((b>>4)^a) & 0x0F0F0F0F reduces to t = a, so the swap is just
 *     b |= a << 4 (and a becomes dead).
 *  3. Half of the remaining 2- and 1-shift swaps pair all-zero registers —
 *     dropped.
 *
 * Net: 4 merges + 4 delta-swaps instead of 20 delta-swaps plus a separate
 * load pass — roughly half the per-pixel cost of the general path on a 68000,
 * where this is the hot loop under every screen repaint.
 */

#ifndef PLATFORM_C2P4ST_H
#define PLATFORM_C2P4ST_H

#include "c2p32.h"              /* c2p_u32, C2P_DSWAP */

/*
 * Convert 32 chunky pixels (bytes, remapped through `lut` to 0..15) into
 * ST-low interleaved planes: d[0..3] = plane 0..3 words for pixels 0-15,
 * d[4..7] for pixels 16-31. Bit 15 of each word = leftmost pixel.
 */
static void c2p4st_32(const unsigned char *src, const unsigned char *lut,
                      unsigned short *d)
{
	c2p_u32 a0, a1, a2, a3, a4, a5, a6, a7;

	/* Load with the stage-1/2 byte permutation baked in (reduction 1). */
#define C2P4_LD(c) (((c2p_u32)lut[src[(c)]] << 24)      \
	          | ((c2p_u32)lut[src[(c) + 8]] << 16)  \
	          | ((c2p_u32)lut[src[(c) + 16]] << 8)  \
	          |  (c2p_u32)lut[src[(c) + 24]])
	a0 = C2P4_LD(0); a1 = C2P4_LD(4);
	a2 = C2P4_LD(1); a3 = C2P4_LD(5);
	a4 = C2P4_LD(2); a5 = C2P4_LD(6);
	a6 = C2P4_LD(3); a7 = C2P4_LD(7);
#undef C2P4_LD

	/* 4-shift stage as nibble merges (reduction 2). */
	a1 |= a0 << 4;
	a3 |= a2 << 4;
	a5 |= a4 << 4;
	a7 |= a6 << 4;

	/* Surviving 2- and 1-shift swaps (reduction 3). */
	C2P_DSWAP(a1, a5, 2, 0x33333333UL);
	C2P_DSWAP(a3, a7, 2, 0x33333333UL);
	C2P_DSWAP(a1, a3, 1, 0x55555555UL);
	C2P_DSWAP(a5, a7, 1, 0x55555555UL);

	/* out[3]=a1 out[2]=a3 out[1]=a5 out[0]=a7 (planes 3..0). */
	d[0] = (unsigned short)(a7 >> 16);      /* plane 0, pixels 0-15  */
	d[1] = (unsigned short)(a5 >> 16);      /* plane 1               */
	d[2] = (unsigned short)(a3 >> 16);      /* plane 2               */
	d[3] = (unsigned short)(a1 >> 16);      /* plane 3               */
	d[4] = (unsigned short)a7;              /* plane 0, pixels 16-31 */
	d[5] = (unsigned short)a5;
	d[6] = (unsigned short)a3;
	d[7] = (unsigned short)a1;
}

/*
 * FLAT fast path (ADR-0016 B3.2). When all 32 chunky pixels share one value the
 * four plane words are CONSTANT (each plane is all-0 or all-1 for the remapped
 * nibble), so the whole transpose collapses to four constant stores — the bulk
 * of the compute vanishes. This is the overlay-safe core of blitter-accelerated
 * fills: the flat panels/backgrounds that dominate a menu/HUD screen convert at
 * a fraction of the c2p cost, WITHOUT any writer change (a text word over the
 * fill is not flat, so it takes the normal transpose). Output is byte-identical
 * to c2p4st_32 on an all-`v` input — verified in tests/test_c2p4st.py.
 *
 * `v` is the pre-remap chunky value (the caller has already checked flatness).
 */
static void c2p4st_32_flat(unsigned char v, const unsigned char *lut,
                           unsigned short *d)
{
	unsigned char s = lut[v];
	unsigned short p0 = (s & 1) ? 0xFFFFu : 0u;
	unsigned short p1 = (s & 2) ? 0xFFFFu : 0u;
	unsigned short p2 = (s & 4) ? 0xFFFFu : 0u;
	unsigned short p3 = (s & 8) ? 0xFFFFu : 0u;

	d[0] = p0; d[1] = p1; d[2] = p2; d[3] = p3;     /* pixels 0-15  */
	d[4] = p0; d[5] = p1; d[6] = p2; d[7] = p3;     /* pixels 16-31 */
}

/*
 * True iff all `n` bytes at `src` are equal. Early-exits on the first mismatch,
 * so on non-flat data (the dungeon, textured chrome) it costs ~one compare and
 * the caller falls straight through to the transpose — a negligible tax for a
 * large saving on flat spans. `n` is 32 (body) or 16 (tail).
 */
static int c2p4st_is_flat(const unsigned char *src, short n)
{
	unsigned char v = src[0];
	short i;

	for (i = 1; i < n; i++)
		if (src[i] != v)
			return 0;
	return 1;
}

/*
 * Convert EIGHT chunky pixels (bytes, remapped through `lut` to 0..15) into ONE
 * plane BYTE per plane: out[0..3] = plane 0..3, bit 7 = leftmost pixel (src[0]).
 * This is the half-group c2p an unaligned edge column needs — a viewport that
 * does not start on a 16-pixel boundary presents 8-pixel columns, and running
 * them through a scalar per-bit scatter was ~45% of the ST dungeon-walk
 * composite (measured 2026-08-08). It is the same word-parallel butterfly as
 * c2p4st_32, restricted to the 8 pixels that land in each lane's top byte.
 *
 * Derivation: in c2p4st_32 pixels 0..7 occupy the `<<24` slot of lanes a0..a7
 * (the src[c] term, c<8), and c2p4st_32's plane-word for pixels 0-15 is a7>>16
 * (plane 0) etc. — so pixels 0-7 are exactly the HIGH BYTE of that word, a7>>24.
 * Because every remapped nibble is <16 its byte's high nibble is zero, so the
 * 4-shift merge is the clean `b |= a<<4` (reduction 2) and stays within each
 * byte; zeroing the lower three bytes of every lane therefore leaves the top
 * byte's transpose identical to the 32-pixel case. Byte-identical to the naive
 * scatter — verified in tests/test_c2p4st.py.
 */
static void c2p4st_8(const unsigned char *src, const unsigned char *lut,
                     unsigned char out[4])
{
	/* Same lane assignment as c2p4st_32's C2P4_LD, top-byte slot only:
	 * a0..a7 <- pixels {0,4,1,5,2,6,3,7} (c = 0,4,1,5,2,6,3,7). */
	c2p_u32 a0 = (c2p_u32)lut[src[0]] << 24;
	c2p_u32 a1 = (c2p_u32)lut[src[4]] << 24;
	c2p_u32 a2 = (c2p_u32)lut[src[1]] << 24;
	c2p_u32 a3 = (c2p_u32)lut[src[5]] << 24;
	c2p_u32 a4 = (c2p_u32)lut[src[2]] << 24;
	c2p_u32 a5 = (c2p_u32)lut[src[6]] << 24;
	c2p_u32 a6 = (c2p_u32)lut[src[3]] << 24;
	c2p_u32 a7 = (c2p_u32)lut[src[7]] << 24;

	a1 |= a0 << 4;
	a3 |= a2 << 4;
	a5 |= a4 << 4;
	a7 |= a6 << 4;

	C2P_DSWAP(a1, a5, 2, 0x33333333UL);
	C2P_DSWAP(a3, a7, 2, 0x33333333UL);
	C2P_DSWAP(a1, a3, 1, 0x55555555UL);
	C2P_DSWAP(a5, a7, 1, 0x55555555UL);

	out[0] = (unsigned char)(a7 >> 24);     /* plane 0 */
	out[1] = (unsigned char)(a5 >> 24);     /* plane 1 */
	out[2] = (unsigned char)(a3 >> 24);     /* plane 2 */
	out[3] = (unsigned char)(a1 >> 24);     /* plane 3 */
}

#endif /* PLATFORM_C2P4ST_H */
