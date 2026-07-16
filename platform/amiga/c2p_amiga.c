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
#include "c2p32.h"             /* the shared 32-pixel transpose network */

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

/* Rect form: convert only the given cell. `x0` and `w` must be multiples of 8
 * (the byte granularity of a bitplane row); the caller aligns. `chunky_pitch`
 * is the chunky buffer's full row width. Runs 32-pixel transposes over the
 * aligned span, with a naive 8-pixel tail for spans narrower than 32. */
static void scatter_rect(const unsigned char *chunky, short chunky_pitch,
                         unsigned char *const planes[], short plane_pitch,
                         short x0, short y0, short w, short h, short nplanes)
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
			for (k = 0; k < nplanes; k++)
				planes[k][rowoff + (x >> 3)] = pb[k];
		}
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
