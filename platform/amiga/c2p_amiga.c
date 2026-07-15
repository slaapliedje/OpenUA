/*
 * Chunky-to-planar for the AGA display backend (ADR-0012) — portable C
 * reference. Scatters an 8bpp chunky buffer into 8 SEPARATE bitplanes (the
 * layout Denise/Lisa DMA reads; the Falcon c2p's word-interleaved output is
 * not usable here — see display_aga.c).
 *
 * Shape: for every 8-pixel group, bit p of each chunky pixel gathers into one
 * byte of plane p, MSB = leftmost pixel. `w` must be a multiple of 8 (320 is).
 *
 * First cut, same trajectory as the Falcon backend (portable C LUT blit
 * first, hand asm later): the classic Kalms-style merged bit-transpose is a
 * later optimisation once the port is alive under amiberry.
 */

#include "display.h"

void c2p_amiga(const unsigned char *chunky, unsigned char *const planes[8],
               short w, short h, short plane_pitch);

void c2p_amiga(const unsigned char *chunky, unsigned char *const planes[8],
               short w, short h, short plane_pitch)
{
	short y, k;
	long  x;

	for (y = 0; y < h; y++) {
		const unsigned char *src = chunky + (long)y * w;
		long rowoff = (long)y * plane_pitch;

		for (x = 0; x < w; x += 8) {
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
