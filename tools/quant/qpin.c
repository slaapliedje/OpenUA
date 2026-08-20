/* qpin — price a PINNED palette across a run of captured frames (#139).
 *
 * The walk's re-bands are not driven by qd_set_palette: during play the CLUT
 * barely moves, and the trigger is the new-ink overflow path (st_patch_new_ink
 * declining past INK_MAX, whereupon the whole frame is re-quantised). So the
 * question "can the dungeon keep ONE palette, the way Curse of the Azure Bonds
 * keeps one for its corridor" is really: how much error does a frame pick up
 * when it is forced through a palette that was cut for a DIFFERENT frame?
 *
 * ★ THE PIN IS KEYED ON THE CLUT, AND IT HAS TO BE. The first cut of this tool
 * pinned every frame to frame 0's palette and reported a 25x error, which is
 * not a fidelity result at all — the walk's CLUT MOVES (a captured HEIRS drive
 * cycles 18 distinct CLUTs over 76 re-bands), so that number is one colour
 * space scored against another's palette. A pin can only mean "reuse the
 * palette we already cut FOR THIS CLUT". Every variant below therefore compares
 * within one colour space, which is the only comparison that means anything.
 *
 * Three variants per frame:
 *
 *   A  fresh   re-cut every frame — what ships today, and the floor.
 *   B  cache   the palette cut on the FIRST frame carrying this CLUT. This is
 *              exactly what a palette cache would serve: same colour space,
 *              different CONTENT, so the cut is not re-derived from the pixels
 *              actually on screen. B - A is the price of the cache.
 *   C  union   one cut over ALL frames carrying this CLUT. The best fixed
 *              palette that colour space admits — the authored-palette model,
 *              and the bound on how well a cache could ever be primed.
 *
 * ★ THE PIN'S REMAP IS NEAREST, NOT THE MEDIAN-CUT BOX. Under a pin there is no
 * box to fall into — the palette was cut from other pixels — so every index
 * takes the nearest entry, weighted 2:5:1 like the shipping remap (b7d628ba).
 * Measuring a pin with box assignment would price a bug, not a policy.
 *
 * Usage: qpin <dir> <first> <last> [vx vy vw vh]
 *        (frames are <dir>/qNN.frm + <dir>/qNN.clt, as FRUA_QDUMP writes them)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quantize.h"

#define W      320
#define H      200
#define NBANDS 25                       /* ST_NBANDS — raster resolution      */
#define RPB    (H / NBANDS)             /* ST_RPB                             */
#define NCOL   16                       /* ST_NCOL                            */
#define BITS   4                        /* ST_BITS                            */
#define MAXGRP 3                        /* ST_MAXGRP                          */
#define MAXF   100

static unsigned char *frm[MAXF];
static unsigned char  clt[MAXF][768];
static short          nf;
static short          fid[MAXF];

static short vx = 24, vy = 24, vw = 88, vh = 88;   /* the live viewport rect  */
static short gb[MAXGRP + 1];                       /* group band boundaries   */
static short ngrp;

/* Per-group palettes for each variant. */
static unsigned char pal_fresh[MAXGRP][NCOL * 3], rem_fresh[MAXGRP][256];
static unsigned char rem_scratch[256];

static long d2w(const unsigned char *a, const unsigned char *b)
{
	long dr = (long)a[0] - b[0], dg = (long)a[1] - b[1], db = (long)a[2] - b[2];
	return 2L * dr * dr + 5L * dg * dg + db * db;
}

/* Unweighted squared error, which is what every other #139 number is quoted in. */
static long perr(const unsigned char *want, const unsigned char *got)
{
	long dr = (long)want[0] - got[0];
	long dg = (long)want[1] - got[1];
	long db = (long)want[2] - got[2];
	return dr * dr + dg * dg + db * db;
}

/* Nearest-entry remap of the whole 256-index space against one palette. */
static void nearest_remap(const unsigned char *pal, const unsigned char *clut,
                          unsigned char *rem)
{
	short i, j;

	for (i = 0; i < 256; i++) {
		const unsigned char *c = clut + (long)i * 3;
		long  bd = 0x7FFFFFFFL;
		short bj = 0;

		for (j = 0; j < NCOL; j++) {
			long d = d2w(c, pal + (long)j * 3);
			if (d < bd) { bd = d; bj = j; }
		}
		rem[i] = (unsigned char)bj;
	}
}

static int load_frame(const char *dir, short n)
{
	char p[512];
	FILE *f;

	sprintf(p, "%s/q%02d.frm", dir, n);
	if (!(f = fopen(p, "rb"))) return 0;
	frm[nf] = malloc((size_t)W * H);
	if (fread(frm[nf], 1, (size_t)W * H, f) != (size_t)W * H) { fclose(f); return 0; }
	fclose(f);
	sprintf(p, "%s/q%02d.clt", dir, n);
	if (!(f = fopen(p, "rb"))) return 0;
	if (fread(clt[nf], 1, 768, f) != 768) { fclose(f); return 0; }
	fclose(f);
	fid[nf] = n;
	nf++;
	return 1;
}

/* st_group_layout, offline: boundaries on the viewport's own edges. */
static void group_layout(void)
{
	short t = (short)(vy / RPB);
	short b = (short)((vy + vh + RPB - 1) / RPB);

	if (t <= 0 || b >= NBANDS || b <= t) { ngrp = 1; gb[0] = 0; gb[1] = NBANDS; return; }
	ngrp = 3; gb[0] = 0; gb[1] = t; gb[2] = b; gb[3] = NBANDS;
}

/* Mean squared error of one frame under a set of per-group remaps+palettes,
 * split into "inside the viewport rect" and "everything else". */
static void score(short i, unsigned char pal[MAXGRP][NCOL * 3],
                  unsigned char rem[MAXGRP][256],
                  double *in, double *out)
{
	long ein = 0, eout = 0, nin = 0, nout = 0;
	short g, x, y;

	for (g = 0; g < ngrp; g++)
		for (y = (short)(gb[g] * RPB); y < (short)(gb[g + 1] * RPB); y++)
			for (x = 0; x < W; x++) {
				unsigned char ix = frm[i][(long)y * W + x];
				long e = perr(clt[i] + (long)ix * 3,
				              pal[g] + (long)rem[g][ix] * 3);

				if (x >= vx && x < vx + vw && y >= vy && y < vy + vh) {
					ein += e; nin++;
				} else { eout += e; nout++; }
			}
	*in  = nin  ? (double)ein  / (double)nin  : 0.0;
	*out = nout ? (double)eout / (double)nout : 0.0;
}

int main(int argc, char **argv)
{
	const char *dir;
	short first, last, i, g, j, k;
	long  moved = 0, moved_used = 0;
	unsigned char used_any[256];
	unsigned char *uni;
	double a_in = 0, a_out = 0, b_in = 0, b_out = 0, c_in = 0, c_out = 0;
	double cw_max = 0;
	short cw_max_f = -1;

	if (argc < 4) {
		fprintf(stderr, "usage: %s <dir> <first> <last> [vx vy vw vh]\n", argv[0]);
		return 1;
	}
	dir = argv[1]; first = (short)atoi(argv[2]); last = (short)atoi(argv[3]);
	if (argc >= 8) {
		vx = (short)atoi(argv[4]); vy = (short)atoi(argv[5]);
		vw = (short)atoi(argv[6]); vh = (short)atoi(argv[7]);
	}
	for (i = first; i <= last && nf < MAXF; i++)
		if (!load_frame(dir, i))
			fprintf(stderr, "skip q%02d (missing/short)\n", i);
	if (nf < 2) { fprintf(stderr, "need at least 2 frames\n"); return 1; }
	group_layout();
	printf("%d frames, viewport %d,%d %dx%d, %d group(s) at bands",
	       nf, vx, vy, vw, vh, ngrp);
	for (g = 0; g <= ngrp; g++) printf(" %d", gb[g]);
	printf("\n");

	/* --- is the run pinnable at all? ------------------------------------- */
	memset(used_any, 0, 256);
	for (i = 0; i < nf; i++) {
		long n;
		for (n = 0; n < (long)W * H; n++) used_any[frm[i][n]] = 1;
	}
	for (i = 1; i < nf; i++)
		for (j = 0; j < 256; j++)
			if (memcmp(clt[0] + j * 3, clt[i] + j * 3, 3) != 0) {
				moved++;
				if (used_any[j]) moved_used++;
			}
	printf("CLUT drift vs frame 0: %ld entry-frames moved, %ld of them USED\n",
	       moved, moved_used);
	if (moved_used)
		printf("  ! the colour space MOVES under used indices — a pin would be\n"
		       "    measured across two colour spaces; treat C as approximate\n");

	/* --- CLUT NOVELTY: would a small cache have answered this re-band? -----
	 *
	 * Every capture here IS a re-band, so the run doubles as a census of what
	 * drives them. The backend remembers exactly ONE previous CLUT
	 * (s_clut_banded) and skips the re-band when it matches, which handles a
	 * repeat but not an ALTERNATION: walk -> event picture -> walk re-cuts the
	 * walk's palette from scratch even though it cut that identical CLUT two
	 * re-bands ago. Count how many of these re-bands landed on a CLUT already
	 * seen, and how deep back it was — the depth is the cache size a fix needs. */
	{
		short novel = 0, repeat = 0, maxdepth = 0;

		for (i = 0; i < nf; i++) {
			short hit = -1;
			for (j = (short)(i - 1); j >= 0; j--)
				if (memcmp(clt[i], clt[j], 768) == 0) { hit = j; break; }
			if (hit < 0) { novel++; continue; }
			repeat++;
			/* depth = distinct CLUTs between the hit and here */
			{
				short d = 0, k2;
				for (k2 = (short)(hit + 1); k2 < i; k2++) {
					short seen = 0, m2;
					for (m2 = (short)(hit + 1); m2 < k2; m2++)
						if (memcmp(clt[k2], clt[m2], 768) == 0) { seen = 1; break; }
					if (!seen && memcmp(clt[k2], clt[i], 768) != 0) d++;
				}
				if (d > maxdepth) maxdepth = d;
			}
		}
		printf("CLUT novelty over the run: %d novel, %d REPEATS of a CLUT already cut\n",
		       novel, repeat);
		if (repeat)
			printf("  a %d-entry palette cache would have answered every repeat\n",
			       maxdepth + 1);
	}

	/* --- group frames by CLUT: each class is one colour space ------------- */
	{
		static short cls[MAXF];              /* frame -> class (first member) */
		static short nclass;
		static unsigned char pal_cache[MAXF][MAXGRP][NCOL * 3];
		static unsigned char rem_cache[MAXF][MAXGRP][256];
		static unsigned char pal_un[MAXF][MAXGRP][NCOL * 3];
		static unsigned char rem_un[MAXF][MAXGRP][256];
		double ra_in = 0, ra_out = 0, rb_in = 0, rb_out = 0, rc_in = 0, rc_out = 0;
		short  nrep = 0;

		for (i = 0; i < nf; i++) {
			cls[i] = i;
			for (j = 0; j < i; j++)
				if (memcmp(clt[i], clt[j], 768) == 0) { cls[i] = j; break; }
			if (cls[i] == i) nclass++;
		}

		/* B: each class's palette is the cut taken on its FIRST frame. */
		for (i = 0; i < nf; i++) {
			if (cls[i] != i) continue;
			for (g = 0; g < ngrp; g++) {
				short y0 = (short)(gb[g] * RPB), y1 = (short)(gb[g + 1] * RPB);
				quant_banded(frm[i] + (long)y0 * W, W, (short)(y1 - y0), clt[i],
				             1, NCOL, BITS, pal_cache[i][g], rem_scratch);
				nearest_remap(pal_cache[i][g], clt[i], rem_cache[i][g]);
			}
		}

		/* C: each class's palette is one cut over every frame in the class. */
		uni = malloc((size_t)W * H * (size_t)nf);
		if (!uni) { fprintf(stderr, "oom\n"); return 1; }
		for (i = 0; i < nf; i++) {
			if (cls[i] != i) continue;
			for (g = 0; g < ngrp; g++) {
				short y0 = (short)(gb[g] * RPB), y1 = (short)(gb[g + 1] * RPB);
				long  n = 0;

				for (j = 0; j < nf; j++) {
					if (cls[j] != i) continue;
					for (k = y0; k < y1; k++) {
						memcpy(uni + n, frm[j] + (long)k * W, W);
						n += W;
					}
				}
				quant_banded(uni, W, (short)(n / W), clt[i], 1, NCOL, BITS,
				             pal_un[i][g], rem_scratch);
				nearest_remap(pal_un[i][g], clt[i], rem_un[i][g]);
			}
		}
		free(uni);

		printf("%d distinct CLUTs across %d re-bands\n", nclass, nf);

		/* ★ HOW MUCH OF THE KEY IS DEAD WEIGHT? The backend keys the cache on
		 * all 768 CLUT bytes, so two frames whose palettes differ only in
		 * entries NOTHING ON SCREEN USES count as different colour spaces.
		 * #139 saw exactly that once — consecutive walk frames differing in
		 * entries 253 and 254, neither of them in the viewport, forcing a full
		 * re-band. Re-class on the USED subset and print both counts: the gap
		 * is what a narrower key would buy. */
		{
			short nc2 = 0;
			static short cls2[MAXF];

			for (i = 0; i < nf; i++) {
				cls2[i] = i;
				for (j = 0; j < i; j++) {
					short same = 1, u;
					for (u = 0; u < 256 && same; u++)
						if (used_any[u]
						 && memcmp(clt[i] + u * 3, clt[j] + u * 3, 3) != 0)
							same = 0;
					if (same) { cls2[i] = j; break; }
				}
				if (cls2[i] == i) nc2++;
			}
			printf("  keyed on the USED subset instead: %d distinct (%d fewer)\n",
			       nc2, nclass - nc2);
		}

		/* ★ SIZE THE CACHE FROM THE SEQUENCE, NOT FROM THE CLASS COUNT. 18
		 * distinct CLUTs does not mean 18 entries are needed: what matters is
		 * how far apart a CLUT's uses are. Simulate an LRU of each size over
		 * the re-band ORDER and print the hit rate — that curve is the only
		 * thing that says where the knee is, and a cache one entry short of
		 * the alternation period hits ZERO. */
		{
			short k;

			printf("LRU hit rate by cache size (over the re-band order):\n   ");
			for (k = 1; k <= 16; k++) {
				short lru[17], n = 0, i2, hit = 0;

				for (i2 = 0; i2 < nf; i2++) {
					short c = cls[i2], j2, at = -1;

					for (j2 = 0; j2 < n; j2++)
						if (lru[j2] == c) { at = j2; break; }
					if (at >= 0) {
						hit++;
						for (; at > 0; at--) lru[at] = lru[at - 1];
						lru[0] = c;
					} else {
						if (n < k) n++;
						for (j2 = (short)(n - 1); j2 > 0; j2--) lru[j2] = lru[j2 - 1];
						lru[0] = c;
					}
				}
				printf(" %d:%d%%", k, (int)(100L * hit / nf));
				if (k % 8 == 0) printf("\n   ");
			}
			printf("\n");
		}
		printf("\n  frame  cls        A fresh          B cache         C union\n");
		printf("                   vp     rest      vp     rest      vp     rest\n");
		for (i = 0; i < nf; i++) {
			double ai, ao, bi_, bo, ci, co;
			short  c0 = cls[i];

			for (g = 0; g < ngrp; g++) {
				short y0 = (short)(gb[g] * RPB), y1 = (short)(gb[g + 1] * RPB);
				quant_banded(frm[i] + (long)y0 * W, W, (short)(y1 - y0), clt[i],
				             1, NCOL, BITS, pal_fresh[g], rem_fresh[g]);
			}
			score(i, pal_fresh, rem_fresh, &ai, &ao);
			score(i, pal_cache[c0], rem_cache[c0], &bi_, &bo);
			score(i, pal_un[c0],    rem_un[c0],    &ci, &co);
			printf("  q%02d  %s%3d  %8.1f %8.1f  %8.1f %8.1f  %8.1f %8.1f\n",
			       fid[i], c0 == i ? " " : "*", fid[c0],
			       ai, ao, bi_, bo, ci, co);
			a_in += ai; a_out += ao; b_in += bi_; b_out += bo;
			c_in += ci; c_out += co;
			if (c0 != i) {
				nrep++;
				ra_in += ai; ra_out += ao; rb_in += bi_; rb_out += bo;
				rc_in += ci; rc_out += co;
				if (ci - ai > cw_max) { cw_max = ci - ai; cw_max_f = fid[i]; }
			}
		}
		printf("  ------------------------------------------------------------\n");
		printf("  all %d    %8.1f %8.1f  %8.1f %8.1f  %8.1f %8.1f\n", nf,
		       a_in / nf, a_out / nf, b_in / nf, b_out / nf, c_in / nf, c_out / nf);
		/* ★ THE MEAN OVER ALL FRAMES FLATTERS THE CACHE. A class's FIRST frame
		 * has B == C == A by construction (it is the frame the cut came from),
		 * and those are exactly the re-bands a cache still has to run. Only the
		 * REPEATS are served from the cache, so they are the ones whose error
		 * the cache is responsible for — quote this line, not the one above. */
		if (nrep) {
			printf("  REPEATS ONLY (%d frames — the ones a cache would serve)\n", nrep);
			printf("        %8.1f %8.1f  %8.1f %8.1f  %8.1f %8.1f\n",
			       ra_in / nrep, ra_out / nrep, rb_in / nrep, rb_out / nrep,
			       rc_in / nrep, rc_out / nrep);
			printf("\nviewport, repeats: cache %+.1f%%, union %+.1f%% vs re-cutting\n",
			       100.0 * (rb_in - ra_in) / ra_in, 100.0 * (rc_in - ra_in) / ra_in);
			printf("rest,     repeats: cache %+.1f%%, union %+.1f%%\n",
			       100.0 * (rb_out - ra_out) / ra_out,
			       100.0 * (rc_out - ra_out) / ra_out);
			if (cw_max_f >= 0)
				printf("worst repeat under the union cut: q%02d, viewport +%.1f MSE\n",
				       cw_max_f, cw_max);
		}
	}
	return 0;
}
