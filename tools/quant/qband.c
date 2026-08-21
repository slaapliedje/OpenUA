/* qband — is a PER-BAND cut worth what it costs? (the ECS boot)
 *
 * The Amiga ECS backend runs 25 copper bands and cuts a palette for EACH of
 * them on every re-band. Measured on a live boot: 16.8 s per re-band, 86% of
 * all present time, because the median cut's cost is essentially FIXED per call
 * (it reduces a colour SET, not pixels) — the ST pays ~0.7 s for one cut over
 * 32,000 sampled pixels and ECS pays ~0.67 s for one over 2,560.
 *
 * The ST retired the same expense in #139 by cutting per GROUP and replicating,
 * on the reasoning that the band count is RASTER RESOLUTION, not palette count.
 * Before doing that to ECS, this asks what the 25 cuts actually buy:
 *
 *   A  per-band   25 independent cuts (what ECS ships)
 *   B  one cut    a single whole-frame cut replicated to all 25 bands
 *
 * If B's error is close to A's, the 25 cuts are buying nothing on this content
 * and collapsing them is free. If B is much worse, the bands genuinely differ
 * and the fix has to be content-based GROUPS rather than a single cut.
 *
 * Usage: qband <frame.frm> <clut.clt> [ncol] [nbands]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quantize.h"

#define W 320
#define H 200

static unsigned char chunky[W * H], clut[768];
static unsigned char pal_a[QUANT_MAX_BANDS * 32 * 3], rem_a[QUANT_MAX_BANDS * 256];
static unsigned char pal_b[32 * 3], rem_b[256];

static double score(const unsigned char *pal, const unsigned char *rem,
                    short nb, int per_band)
{
	long e = 0;
	short x, y;

	for (y = 0; y < H; y++) {
		short b = (short)((long)y * nb / H);
		const unsigned char *p = per_band ? pal + (long)b * 32 * 3 : pal;
		const unsigned char *r = per_band ? rem + (long)b * 256    : rem;

		for (x = 0; x < W; x++) {
			unsigned char ix = chunky[(long)y * W + x];
			const unsigned char *want = clut + (long)ix * 3;
			const unsigned char *got  = p + (long)r[ix] * 3;
			long dr = (long)want[0] - got[0];
			long dg = (long)want[1] - got[1];
			long db = (long)want[2] - got[2];

			e += dr * dr + dg * dg + db * db;
		}
	}
	return (double)e / ((double)W * H);
}

int main(int argc, char **argv)
{
	short ncol = (short)(argc > 3 ? atoi(argv[3]) : 32);
	short nb   = (short)(argc > 4 ? atoi(argv[4]) : 25);
	FILE *f;
	short b, i, j, diff_tot = 0, maxdiff = 0;
	double ea, eb;

	if (argc < 3) { fprintf(stderr, "usage: %s <frm> <clt> [ncol] [nbands]\n", argv[0]); return 1; }
	if (!(f = fopen(argv[1], "rb")) || fread(chunky, 1, W * H, f) != W * H) { perror(argv[1]); return 1; }
	fclose(f);
	if (!(f = fopen(argv[2], "rb")) || fread(clut, 1, 768, f) != 768) { perror(argv[2]); return 1; }
	fclose(f);

	quant_banded(chunky, W, H, clut, nb, ncol, 4, pal_a, rem_a);   /* A */
	quant_banded(chunky, W, H, clut, 1,  ncol, 4, pal_b, rem_b);   /* B */
	ea = score(pal_a, rem_a, nb, 1);
	eb = score(pal_b, rem_b, nb, 0);

	/* How far do the per-band palettes actually diverge from the single cut?
	 * Count entries that differ by more than a 4-bit quantisation step, which
	 * is the smallest difference the hardware can even show. */
	for (b = 0; b < nb; b++) {
		short d = 0;
		for (i = 0; i < ncol; i++) {
			const unsigned char *pa = pal_a + ((long)b * 32 + i) * 3;
			short best = 3 * 256;
			for (j = 0; j < ncol; j++) {
				const unsigned char *pb = pal_b + (long)j * 3;
				short s = (short)(abs(pa[0]-pb[0]) + abs(pa[1]-pb[1]) + abs(pa[2]-pb[2]));
				if (s < best) best = s;
			}
			if (best > 16) d++;      /* > one 4-bit step on one gun */
		}
		diff_tot += d;
		if (d > maxdiff) maxdiff = d;
	}

	printf("%-28s  per-band %8.1f   one-cut %8.1f   %+6.1f%%   bands' entries not in the single cut: mean %.1f max %d of %d\n",
	       argv[1], ea, eb, 100.0 * (eb - ea) / ea,
	       (double)diff_tot / nb, maxdiff, ncol);
	return 0;
}
