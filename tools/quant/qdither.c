/* DITHER SPIKE — models what the ST would actually ship, not an idealised
 * dither.
 *
 * The backend maps a pixel with ONE table lookup: remap[chunky]. Any dither we
 * can afford must keep that shape, so it is N remap LUTs selected by pixel
 * position — not a per-pixel search. That bounds what is expressible:
 *
 *   row2    2 LUTs, chosen by y&1              — the LUT pointer is picked
 *                                                ONCE PER ROW, so the inner
 *                                                loop is untouched: free.
 *   check2  2 LUTs, chosen by (x^y)&1          — alternates per PIXEL, so the
 *                                                inner loop must switch tables
 *                                                as it walks: costs.
 *   bayer4  4 LUTs, chosen by (y&1)*2 + (x&1)  — as check2, plus 25/75 mixes.
 *
 * row2 and check2 can only express a 50/50 blend of two palette entries;
 * bayer4 adds quarters. Everything runs over the REAL per-band palettes that
 * quant_banded produces, so the numbers describe the shipping path.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quantize.h"

#define W 320
#define H 200
#define MAXPH 4

static unsigned char chunky[W * H], clut[768];
static unsigned char bpal[QUANT_MAX_BANDS * 32 * 3];
static unsigned char brem[QUANT_MAX_BANDS * 256];
/* phase LUTs: [phase][band][index] */
static unsigned char plut[MAXPH][QUANT_MAX_BANDS][256];

static short g_nb = 10, g_nc = 16;
static long  g_floor = 512;   /* half a 4-bit cell, weighted */

static void load(const char *p, unsigned char *d, long n)
{
	FILE *f = fopen(p, "rb");
	if (!f || fread(d, 1, (size_t)n, f) != (size_t)n) { perror(p); exit(1); }
	fclose(f);
}

static long d2(const unsigned char *a, const unsigned char *b)
{
	long dr = (long)a[0] - b[0], dg = (long)a[1] - b[1], db = (long)a[2] - b[2];
	return 2L * dr * dr + 5L * dg * dg + db * db;
}

/* Build `nph` phase LUTs. For each index, find the two palette entries that
 * bracket its true colour, work out how far between them it sits, and hand
 * each phase whichever entry that fraction calls for. nph == 1 degenerates to
 * the plain nearest-colour remap the backend uses today. */
static void build(short nph)
{
	short b, i, p;

	for (b = 0; b < g_nb; b++) {
		const unsigned char *pal = bpal + (long)b * g_nc * 3;

		for (i = 0; i < 256; i++) {
			const unsigned char *c = clut + (long)i * 3;
			short i0 = -1, i1 = -1, k;
			long  d0 = 0x7FFFFFFFL, d1 = 0x7FFFFFFFL;
			long  num = 0, den = 0;

			for (k = 0; k < g_nc; k++) {
				long d = d2(c, pal + (long)k * 3);

				if (d < d0) { i1 = i0; d1 = d0; i0 = k; d0 = d; }
				else if (d < d1) { i1 = k; d1 = d; }
			}
			/* ★ DEADBAND, not "d0 == 0". quant_snap lands every entry on
			 * a CELL MIDPOINT, so a colour the band reproduces EXACTLY
			 * still sits up to half a cell away — 8 per gun at 4 bits,
			 * which is d2 = 2*64 + 5*64 + 64 = 512. Testing for zero
			 * therefore dithers the flat panels and chrome that
			 * exact-preservation went to the trouble of reserving, and
			 * it shows: the title screen's flat red backdrop came back
			 * cross-hatched. Anything inside the grid's own rounding
			 * noise must stay solid. */
			if (nph == 1 || i1 < 0 || d0 <= g_floor) {
				for (p = 0; p < nph; p++) plut[p][b][i] = (unsigned char)i0;
				continue;
			}
			/* how far from pal[i0] toward pal[i1] does the true colour lie? */
			for (k = 0; k < 3; k++) {
				long a = pal[(long)i0 * 3 + k], bb = pal[(long)i1 * 3 + k];

				num += ((long)c[k] - a) * (bb - a);
				den += (bb - a) * (bb - a);
			}
			{
				long t = den ? (num * 256L) / den : 0;   /* 0..256 */

				if (t < 0) t = 0;
				if (t > 256) t = 256;
				for (p = 0; p < nph; p++) {
					/* phase p covers the band [p/nph, (p+1)/nph) */
					long thr = (2 * p + 1) * 256L / (2 * nph);

					plut[p][b][i] = (unsigned char)(t > thr ? i1 : i0);
				}
			}
		}
	}
}

static long render(const char *path, short nph, int checker)
{
	FILE *f = path ? fopen(path, "wb") : NULL;
	long err = 0;
	short x, y;

	if (f) fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (y = 0; y < H; y++) {
		short b = (short)((long)y * g_nb / H);
		const unsigned char *pal = bpal + (long)b * g_nc * 3;

		for (x = 0; x < W; x++) {
			unsigned char ix = chunky[y * W + x];
			short ph;
			const unsigned char *got, *want = clut + (long)ix * 3;

			if (nph == 1)          ph = 0;
			else if (nph == 2)     ph = (short)(checker ? ((x ^ y) & 1) : (y & 1));
			else                   ph = (short)(((y & 1) << 1) | (x & 1));
			got = pal + (long)plut[ph][b][ix] * 3;
			if (f) fputc(got[0], f), fputc(got[1], f), fputc(got[2], f);
			err += (long)(want[0]-got[0])*(want[0]-got[0])
			     + (long)(want[1]-got[1])*(want[1]-got[1])
			     + (long)(want[2]-got[2])*(want[2]-got[2]);
		}
	}
	if (f) fclose(f);
	return err / (W * H);
}

int main(int argc, char **argv)
{
	const char *frame = argc > 1 ? argv[1] : "frame.bin";
	const char *cl    = argc > 2 ? argv[2] : "clut.bin";
	const char *tag   = argc > 3 ? argv[3] : "wiz";
	char path[256];

	if (argc > 4) g_nc = (short)atoi(argv[4]);
	if (argc > 5) g_nb = (short)atoi(argv[5]);
	if (argc > 6) g_floor = atol(argv[6]);
	load(frame, chunky, W * H);
	load(cl, clut, 768);
	quant_banded(chunky, W, H, clut, g_nb, g_nc, 4, bpal, brem);

	build(1); sprintf(path, "%s_none.ppm", tag);
	printf("  %-8s mse=%6ld\n", "none",   render(path, 1, 0));
	build(2); sprintf(path, "%s_row2.ppm", tag);
	printf("  %-8s mse=%6ld\n", "row2",   render(path, 2, 0));
	build(2); sprintf(path, "%s_check2.ppm", tag);
	printf("  %-8s mse=%6ld\n", "check2", render(path, 2, 1));
	build(4); sprintf(path, "%s_bayer4.ppm", tag);
	printf("  %-8s mse=%6ld\n", "bayer4", render(path, 4, 1));
	return 0;
}
