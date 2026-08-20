/* #139 STEP 1 — is a STABLE palette affordable, and which flavour do we need?
 *
 * The ST re-quantises whenever a substantial palette load arrives, and the cut
 * is CONTENT-weighted, so the same CLUT with different content yields a
 * different palette. That churn is what forces the whole-frame rebuild, what
 * makes the viewport's baked planes go stale, and what keeps the chunky pass
 * alive (it exists only to feed the quantiser now — the planes are the output).
 *
 * A stable palette removes all three. The question is what to derive it FROM:
 *
 *   A  per-frame   quantise this frame's content        (what ships; the baseline)
 *   B  CLUT-only   quantise all 256 CLUT entries equally — trivial to implement,
 *                  but spends slots on colours no screen ever shows
 *   C  union       quantise the union of every frame sharing this CLUT, weighted
 *                  by summed populations — models deriving the palette from the
 *                  ART (everything the scene CAN show) rather than one frame
 *
 * C is what an art-derived palette would produce. If C is close to A, a stable
 * palette is nearly free and the work is worth doing; if it is far off, the
 * fidelity price has to be argued before any engine change.
 *
 * Error is every pixel's true CLUT colour vs the colour it renders as, the same
 * metric as qfid/qpre, so all three tools are comparable.
 *
 * Usage:  qstable <ncol> <frame.bin> [more frames ...] -- <clut.bin>
 *         (all frames must share the one CLUT; that is the case being modelled)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quantize.h"

#define W 320
#define H 200
#define MAXF 32

static unsigned char frames[MAXF][W * H], clut[768];
static unsigned char bpal[QUANT_MAX_BANDS * 32 * 3];
static unsigned char brem[QUANT_MAX_BANDS * 256];
static unsigned char synth[W * H];

static void load(const char *p, unsigned char *d, long n)
{
	FILE *f = fopen(p, "rb");
	if (!f || fread(d, 1, (size_t)n, f) != (size_t)n) { perror(p); exit(1); }
	fclose(f);
}

/* MSE of `frame` rendered through the palette+remap currently in bpal/brem */
static double mse(const unsigned char *frame, short nc)
{
	long err = 0, n;

	for (n = 0; n < (long)W * H; n++) {
		unsigned char ix = frame[n];
		const unsigned char *g = bpal + (long)brem[ix] * 3;
		const unsigned char *w = clut + (long)ix * 3;
		long dr = (long)w[0]-g[0], dg = (long)w[1]-g[1], db = (long)w[2]-g[2];

		err += dr*dr + dg*dg + db*db;
	}
	(void)nc;
	return (double)err / (double)(W * H);
}

int main(int argc, char **argv)
{
	short nc = 16, nf = 0, i;
	const char *cl = NULL;
	double a_tot = 0, b_tot = 0, c_tot = 0;

	if (argc > 1) nc = (short)atoi(argv[1]);
	for (i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "--")) { cl = argv[++i]; continue; }
		if (nf < MAXF) load(argv[i], frames[nf++], (long)W * H);
	}
	if (!cl || !nf) { fprintf(stderr, "usage: qstable <ncol> f1 [f2..] -- clut.bin\n"); return 1; }
	load(cl, clut, 768);

	/* ---- A: per-frame (the shipping behaviour) ---- */
	for (i = 0; i < nf; i++) {
		quant_banded(frames[i], W, H, clut, 1, nc, 4, bpal, brem);
		a_tot += mse(frames[i], nc);
	}

	/* ---- B: CLUT-only. One pixel per CLUT entry, so every entry is present
	 * exactly once and the cut sees no populations at all. ---- */
	memset(synth, 0, sizeof synth);
	for (i = 0; i < 256; i++)
		synth[i] = (unsigned char)i;
	quant_banded(synth, W, H, clut, 1, nc, 4, bpal, brem);
	for (i = 0; i < nf; i++)
		b_tot += mse(frames[i], nc);

	/* ---- C: union of all frames, populations summed. Concatenating the frames
	 * is not possible in one 320x200 buffer, so build a synthetic surface whose
	 * per-index population is the TOTAL across frames, scaled to fit. ---- */
	{
		long pop[256]; long total = 0, n; short k; long w = 0;

		for (k = 0; k < 256; k++) pop[k] = 0;
		for (i = 0; i < nf; i++)
			for (n = 0; n < (long)W * H; n++) pop[frames[i][n]]++;
		for (k = 0; k < 256; k++) total += pop[k];
		memset(synth, 0, sizeof synth);
		for (k = 0; k < 256 && w < (long)W * H; k++) {
			long want = pop[k] * ((long)W * H) / total;

			if (pop[k] && !want) want = 1;      /* never drop a used colour */
			while (want-- > 0 && w < (long)W * H) synth[w++] = (unsigned char)k;
		}
		quant_banded(synth, W, H, clut, 1, nc, 4, bpal, brem);
		for (i = 0; i < nf; i++)
			c_tot += mse(frames[i], nc);
	}

	printf("frames=%d ncol=%d\n", nf, nc);
	printf("  A per-frame (ships)   %8.1f\n", a_tot / nf);
	printf("  B CLUT-only           %8.1f   (%+.0f%%)\n", b_tot / nf,
	       100.0 * (b_tot - a_tot) / a_tot);
	printf("  C union-of-content    %8.1f   (%+.0f%%)\n", c_tot / nf,
	       100.0 * (c_tot - a_tot) / a_tot);
	return 0;
}
