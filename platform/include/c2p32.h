/*
 * c2p32.h — the shared 32-pixel chunky-to-planar bit-matrix transpose.
 *
 * 32 chunky bytes in (8 big-endian longwords, pixel 0 = top byte of c[0]),
 * 8 planar longwords out: out[p] = plane p, bit 31 = pixel 0. Five
 * delta-swap stages, ~4 ops/pixel — the masked-swap network brute-force
 * solved and host-verified against the naive scatter (tests/
 * test_c2p_amiga.py exercises it through the Amiga backend).
 *
 * Consumers arrange the output for their hardware: the Amiga scatters
 * out[p] into 8 SEPARATE planes (c2p_amiga.c); the TT interleaves the
 * high/low words per 16-pixel group (display_tt.c). Header-only so each
 * machine build compiles exactly what it links.
 */

#ifndef PLATFORM_C2P32_H
#define PLATFORM_C2P32_H

typedef unsigned long c2p_u32;

/* Delta-swap: exchange the bit-field (a & mask) with the field of `b` that
 * sits `shift` bits higher. */
#define C2P_DSWAP(a, b, shift, mask) do {                      \
	c2p_u32 t_ = ((b) >> (shift) ^ (a)) & (mask);          \
	(a) ^= t_;                                             \
	(b) ^= t_ << (shift);                                  \
} while (0)

static void c2p_transpose32(const c2p_u32 c[8], c2p_u32 out[8])
{
	c2p_u32 a0 = c[0], a1 = c[1], a2 = c[2], a3 = c[3];
	c2p_u32 a4 = c[4], a5 = c[5], a6 = c[6], a7 = c[7];

	/* Byte-lane sort (16- then 8-shift): register position k ends up
	 * holding pixels {r, r+8, r+16, r+24} in the order 0,4,1,5,2,6,3,7. */
	C2P_DSWAP(a0, a4, 16, 0x0000FFFFUL);
	C2P_DSWAP(a1, a5, 16, 0x0000FFFFUL);
	C2P_DSWAP(a2, a6, 16, 0x0000FFFFUL);
	C2P_DSWAP(a3, a7, 16, 0x0000FFFFUL);

	C2P_DSWAP(a0, a2, 8, 0x00FF00FFUL);
	C2P_DSWAP(a1, a3, 8, 0x00FF00FFUL);
	C2P_DSWAP(a4, a6, 8, 0x00FF00FFUL);
	C2P_DSWAP(a5, a7, 8, 0x00FF00FFUL);

	/* In-byte transpose (4/2/1): each shift-s stage pairs the registers
	 * whose pixels differ by s (see the solve note in the header). */
	C2P_DSWAP(a0, a1, 4, 0x0F0F0F0FUL);
	C2P_DSWAP(a2, a3, 4, 0x0F0F0F0FUL);
	C2P_DSWAP(a4, a5, 4, 0x0F0F0F0FUL);
	C2P_DSWAP(a6, a7, 4, 0x0F0F0F0FUL);

	C2P_DSWAP(a0, a4, 2, 0x33333333UL);
	C2P_DSWAP(a2, a6, 2, 0x33333333UL);
	C2P_DSWAP(a1, a5, 2, 0x33333333UL);
	C2P_DSWAP(a3, a7, 2, 0x33333333UL);

	C2P_DSWAP(a0, a2, 1, 0x55555555UL);
	C2P_DSWAP(a4, a6, 1, 0x55555555UL);
	C2P_DSWAP(a1, a3, 1, 0x55555555UL);
	C2P_DSWAP(a5, a7, 1, 0x55555555UL);

	/* Register -> plane assignment falls out of the network. */
	out[7] = a0; out[3] = a1; out[6] = a2; out[2] = a3;
	out[5] = a4; out[1] = a5; out[4] = a6; out[0] = a7;
}

/* Load 32 chunky bytes as 8 big-endian longwords (portable: the shifts give
 * the same lane order on a little-endian test host). */
static void c2p_load32(const unsigned char *src, c2p_u32 c[8])
{
	short k;

	for (k = 0; k < 8; k++) {
		c[k] = ((c2p_u32)src[0] << 24) | ((c2p_u32)src[1] << 16)
		     | ((c2p_u32)src[2] << 8)  |  (c2p_u32)src[3];
		src += 4;
	}
}

#endif /* PLATFORM_C2P32_H */
