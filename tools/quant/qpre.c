/* #139 scoping: how much fidelity is an OFFLINE pre-quantiser actually worth,
 * and how much of it is reachable at runtime?
 *
 *   A  quant_banded as it ships          — the cut is PRESENCE-weighted: each
 *                                          used colour counts once however
 *                                          much of the band it covers.
 *   B  population-weighted cut           — same median cut, boxes split and
 *                                          representatives averaged by PIXEL
 *                                          COUNT. Needs the per-band histogram,
 *                                          which quant_banded already builds.
 *   C  B + Floyd-Steinberg               — offline only: error diffusion is a
 *                                          serial pass over pixels and cannot
 *                                          be a table lookup.
 *
 * Error is measured against every pixel's true CLUT colour, so the numbers are
 * comparable to the shipping path (see qfid.c).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quantize.h"

#define W 320
#define H 200
#define MAXC 64

static unsigned char chunky[W * H], clut[768];
static unsigned char bpal[QUANT_MAX_BANDS * MAXC * 3];
static unsigned char brem[QUANT_MAX_BANDS * 256];

static short g_nb = 10, g_nc = 16, g_bits = 4;

static void load(const char *p, unsigned char *d, long n)
{
	FILE *f = fopen(p, "rb");
	if (!f || fread(d, 1, (size_t)n, f) != (size_t)n) { perror(p); exit(1); }
	fclose(f);
}

static long wd2(const short *a, const unsigned char *b)
{
	long dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
	return 2L * dr * dr + 5L * dg * dg + db * db;
}

/* ---- B: population-weighted median cut over one band ------------------- */
struct box { short idx[256]; short n; };

static short wcut(const long *cnt, short ncol, unsigned char *pal)
{
	struct box bx[MAXC];
	short nb = 1, i, k;

	bx[0].n = 0;
	for (i = 0; i < 256; i++)
		if (cnt[i]) bx[0].idx[bx[0].n++] = i;
	if (!bx[0].n) return 0;

	while (nb < ncol) {
		short best = -1, baxis = 0;
		double bestscore = -1;

		for (k = 0; k < nb; k++) {
			short a;
			if (bx[k].n < 2) continue;
			for (a = 0; a < 3; a++) {
				short lo = 255, hi = 0;
				double pop = 0;
				for (i = 0; i < bx[k].n; i++) {
					short v = clut[bx[k].idx[i] * 3 + a];
					if (v < lo) lo = v;
					if (v > hi) hi = v;
					pop += (double)cnt[bx[k].idx[i]];
				}
				/* weighted spread: how much ERROR the box contributes */
				if ((hi - lo) * pop > bestscore) {
					bestscore = (hi - lo) * pop;
					best = k; baxis = a;
				}
			}
		}
		if (best < 0) break;
		{	/* split at the population MEDIAN along baxis */
			short list[256], n = bx[best].n, a = baxis, j;
			double half = 0, run = 0;
			short thr = 0;

			memcpy(list, bx[best].idx, sizeof(short) * n);
			for (i = 0; i < n; i++)          /* insertion sort by axis */
				for (j = i + 1; j < n; j++)
					if (clut[list[j] * 3 + a] < clut[list[i] * 3 + a]) {
						short t = list[i]; list[i] = list[j]; list[j] = t;
					}
			for (i = 0; i < n; i++) half += (double)cnt[list[i]];
			half /= 2;
			for (i = 0; i < n; i++) {
				run += (double)cnt[list[i]];
				if (run >= half) { thr = i; break; }
			}
			if (thr >= n - 1) thr = (short)(n - 2);
			bx[best].n = (short)(thr + 1);
			memcpy(bx[best].idx, list, sizeof(short) * bx[best].n);
			bx[nb].n = (short)(n - thr - 1);
			memcpy(bx[nb].idx, list + thr + 1, sizeof(short) * bx[nb].n);
			nb++;
		}
	}
	for (k = 0; k < nb; k++) {
		double sr = 0, sg = 0, sb = 0, pop = 0;
		for (i = 0; i < bx[k].n; i++) {
			long c = cnt[bx[k].idx[i]];
			sr += (double)clut[bx[k].idx[i]*3+0] * c;
			sg += (double)clut[bx[k].idx[i]*3+1] * c;
			sb += (double)clut[bx[k].idx[i]*3+2] * c;
			pop += (double)c;
		}
		pal[k*3+0] = quant_snap((short)(sr/pop), g_bits);
		pal[k*3+1] = quant_snap((short)(sg/pop), g_bits);
		pal[k*3+2] = quant_snap((short)(sb/pop), g_bits);
	}
	for (k = nb; k < ncol; k++)
		pal[k*3+0] = pal[k*3+1] = pal[k*3+2] = 0;
	return nb;
}

static double run_weighted(int diffuse)
{
	static unsigned char pal[QUANT_MAX_BANDS * MAXC * 3];
	static short err[W + 2][3];
	long total = 0;
	short b, x, y;

	for (b = 0; b < g_nb; b++) {
		long cnt[256];
		short y0 = (short)((long)b * H / g_nb), y1 = (short)((long)(b+1) * H / g_nb);

		for (x = 0; x < 256; x++) cnt[x] = 0;
		for (y = y0; y < y1; y++)
			for (x = 0; x < W; x++) cnt[chunky[y*W+x]]++;
		wcut(cnt, g_nc, pal + (long)b * g_nc * 3);
	}
	memset(err, 0, sizeof err);
	for (y = 0; y < H; y++) {
		short b2 = (short)((long)y * g_nb / H);
		const unsigned char *pal2 = pal + (long)b2 * g_nc * 3;
		short nxt[W + 2][3];

		memset(nxt, 0, sizeof nxt);
		for (x = 0; x < W; x++) {
			const unsigned char *c = clut + (long)chunky[y*W+x] * 3;
			short want[3], k, bk = 0;
			long bd = 0x7FFFFFFFL;

			for (k = 0; k < 3; k++) {
				long v = c[k] + (diffuse ? err[x+1][k] : 0);
				want[k] = (short)(v < 0 ? 0 : v > 255 ? 255 : v);
			}
			for (k = 0; k < g_nc; k++) {
				long d = wd2(want, pal2 + (long)k * 3);
				if (d < bd) { bd = d; bk = k; }
			}
			for (k = 0; k < 3; k++) {
				long e = want[k] - pal2[bk*3+k];
				long tr = (long)c[k] - pal2[bk*3+k];

				total += tr * tr;
				if (diffuse) {           /* Floyd-Steinberg 7/3/5/1 */
					err[x+2][k]  = (short)(err[x+2][k] + e * 7 / 16);
					nxt[x][k]    = (short)(nxt[x][k]   + e * 3 / 16);
					nxt[x+1][k]  = (short)(nxt[x+1][k] + e * 5 / 16);
					nxt[x+2][k]  = (short)(nxt[x+2][k] + e * 1 / 16);
				}
			}
		}
		memcpy(err, nxt, sizeof err);
	}
	return (double)total / (double)(W * H);
}

int main(int argc, char **argv)
{
	const char *frame = argc > 1 ? argv[1] : "frame.bin";
	const char *cl    = argc > 2 ? argv[2] : "clut.bin";
	long err = 0;
	short x, y;

	if (argc > 3) g_nc = (short)atoi(argv[3]);
	if (argc > 4) g_nb = (short)atoi(argv[4]);
	load(frame, chunky, W * H);
	load(cl, clut, 768);

	quant_banded(chunky, W, H, clut, g_nb, g_nc, g_bits, bpal, brem);
	for (y = 0; y < H; y++) {
		short b = (short)((long)y * g_nb / H);
		for (x = 0; x < W; x++) {
			unsigned char ix = chunky[y*W+x];
			const unsigned char *g = bpal + ((long)b * g_nc + brem[(long)b*256+ix]) * 3;
			const unsigned char *w = clut + (long)ix * 3;
			long dr = (long)w[0]-g[0], dg = (long)w[1]-g[1], db = (long)w[2]-g[2];
			err += dr*dr + dg*dg + db*db;
		}
	}
	printf("%-12s ncol=%2d nbands=%2d   A shipping=%7.1f  B pop-weighted=%7.1f"
	       "  C B+diffuse=%7.1f\n", frame, g_nc, g_nb,
	       (double)err / (W*H), run_weighted(0), run_weighted(1));
	return 0;
}
