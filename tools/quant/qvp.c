/* qvp — price a DEDICATED viewport palette against the shipping whole-frame cut.
 *
 * Curse of the Azure Bonds (the one SSI Gold Box that shipped on the ST) draws
 * its first-person corridor in EIGHT colours out of a fixed sixteen and never
 * changes them; the other eight are the chrome. We cut all sixteen over the
 * WHOLE frame, so the roster text, the stone border and the command bar compete
 * with the 3D view for the same slots. Two questions follow, and this tool
 * answers both:
 *
 *   1. how much of the viewport's error is the chrome stealing slots?
 *      -> compare "whole-frame cut, error inside the rect" against
 *         "rect-only cut" at the same ncol.
 *   2. can 8 dedicated slots carry the viewport?
 *      -> rect-only at 8, with and without the phase-LUT dither the backend
 *         can actually afford (the same model as qdither: N remap tables
 *         selected by pixel position, never a per-pixel search).
 *
 * Error is reported separately inside and outside the rect so a gain in one is
 * never hidden by the other. PPMs are written for every variant because MSE
 * always punishes dither — the eye is the judge for those.
 *
 * Usage: qvp <frame.frm> <clut.clt> [x y w h] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quantize.h"

#define W 320
#define H 200
#define MAXPH 4

static unsigned char chunky[W * H], clut[768];
static unsigned char rect[W * H];                       /* the cropped rect   */
static unsigned char bpal[QUANT_MAX_BANDS * 32 * 3];
static unsigned char brem[QUANT_MAX_BANDS * 256];
static unsigned char plut[MAXPH][256];                  /* dither phase LUTs  */

static short rx = 24, ry = 24, rw = 88, rh = 88;         /* the live viewport */
static long  g_floor = 512;      /* quant_snap's own rounding noise — see qdither */

static void load(const char *p, unsigned char *d, long n)
{
	FILE *f = fopen(p, "rb");
	if (!f || fread(d, 1, (size_t)n, f) != (size_t)n) { perror(p); exit(1); }
	fclose(f);
}

static int in_rect(short x, short y)
{
	return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static long perr(unsigned char ix, const unsigned char *got)
{
	const unsigned char *want = clut + (long)ix * 3;
	long dr = (long)want[0] - got[0];
	long dg = (long)want[1] - got[1];
	long db = (long)want[2] - got[2];
	return dr * dr + dg * dg + db * db;
}

static long d2(const unsigned char *a, const unsigned char *b)
{
	long dr = (long)a[0] - b[0], dg = (long)a[1] - b[1], db = (long)a[2] - b[2];
	return 2L * dr * dr + 5L * dg * dg + db * db;
}

/* Crop the rect out of the frame into `rect`, compactly. */
static void crop(void)
{
	short y;

	for (y = 0; y < rh; y++)
		memcpy(rect + (long)y * rw, chunky + (long)(ry + y) * W + rx,
		       (size_t)rw);
}

/* Whole-frame cut at ncol: error inside and outside the rect. */
static void whole_frame(short ncol, double *in, double *out)
{
	long ein = 0, eout = 0, nin = 0, nout = 0;
	short x, y;

	quant_banded(chunky, W, H, clut, 1, ncol, 4, bpal, brem);
	for (y = 0; y < H; y++)
		for (x = 0; x < W; x++) {
			unsigned char ix = chunky[y * W + x];
			long e = perr(ix, bpal + (long)brem[ix] * 3);

			if (in_rect(x, y)) { ein += e; nin++; }
			else               { eout += e; nout++; }
		}
	*in  = nin  ? (double)ein  / (double)nin  : 0.0;
	*out = nout ? (double)eout / (double)nout : 0.0;
}



/* The other half of the split: the chrome has to live in the slots the
 * viewport does not take. Pack every pixel outside the rect into a compact
 * strip and cut THAT alone — the palette is what matters, the packing only has
 * to preserve the population. */
static long pack_chrome(void)
{
	long n = 0;
	short x, y;

	for (y = 0; y < H; y++)
		for (x = 0; x < W; x++)
			if (!in_rect(x, y))
				rect[n++] = chunky[(long)y * W + x];
	return n;
}

/* THE REMAP IS NOT NEAREST-COLOUR. quant_banded assigns each present index to
 * the median-cut BOX it fell in, and each absent one through a coarse 4x8x4
 * bucket table. Neither is the nearest entry of the palette it just built, so
 * this measures the same palette under three remaps to separate "the palette is
 * too small" from "the labelling is wrong" — they need different fixes, and the
 * second is far cheaper. */
static void remap_compare(const unsigned char *src, short w, short h,
                          short ncol, const char *what)
{
	long ebox = 0, ergb = 0, ewei = 0;
	long n = (long)w * h;
	unsigned char nr[256], nw[256];
	short i, k, x, y;

	for (i = 0; i < 256; i++) {
		const unsigned char *c = clut + (long)i * 3;
		long br = 0x7FFFFFFFL, bw = 0x7FFFFFFFL;
		short jr = 0, jw = 0;

		for (k = 0; k < ncol; k++) {
			const unsigned char *p = bpal + (long)k * 3;
			long dr = (long)c[0] - p[0], dg = (long)c[1] - p[1];
			long db = (long)c[2] - p[2];
			long d  = dr * dr + dg * dg + db * db;
			long dw = 2L * dr * dr + 5L * dg * dg + db * db;

			if (d  < br) { br = d;  jr = k; }
			if (dw < bw) { bw = dw; jw = k; }
		}
		nr[i] = (unsigned char)jr;
		nw[i] = (unsigned char)jw;
	}
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			unsigned char ix = src[(long)y * w + x];

			ebox += perr(ix, bpal + (long)brem[ix] * 3);
			ergb += perr(ix, bpal + (long)nr[ix] * 3);
			ewei += perr(ix, bpal + (long)nw[ix] * 3);
		}
	printf("  %-22s ncol=%2d  box %8.1f   nearest-rgb %8.1f   nearest-2:5:1 %8.1f\n",
	       what, ncol, (double)ebox / n, (double)ergb / n, (double)ewei / n);
}

/* Build `nph` phase LUTs over the palette quant_banded just produced. */
static void build_dither(short nph, short ncol)
{
	short i, p;

	for (i = 0; i < 256; i++) {
		const unsigned char *c = clut + (long)i * 3;
		short i0 = -1, i1 = -1, k;
		long  d0 = 0x7FFFFFFFL, d1 = 0x7FFFFFFFL, num = 0, den = 0;

		for (k = 0; k < ncol; k++) {
			long d = d2(c, bpal + (long)k * 3);

			if (d < d0)      { i1 = i0; d1 = d0; i0 = k; d0 = d; }
			else if (d < d1) { i1 = k;  d1 = d; }
		}
		if (nph == 1 || i1 < 0 || d0 <= g_floor) {
			for (p = 0; p < nph; p++) plut[p][i] = (unsigned char)i0;
			continue;
		}
		for (k = 0; k < 3; k++) {
			long a = bpal[(long)i0 * 3 + k], b = bpal[(long)i1 * 3 + k];

			num += ((long)c[k] - a) * (b - a);
			den += (b - a) * (b - a);
		}
		{
			long t = den ? (num * 256L) / den : 0;

			if (t < 0)   t = 0;
			if (t > 256) t = 256;
			for (p = 0; p < nph; p++) {
				long thr = (2 * p + 1) * 256L / (2 * nph);

				plut[p][i] = (unsigned char)(t > thr ? i1 : i0);
			}
		}
	}
}

/* Render the cropped rect through the current palette + phase LUTs. */
static double render(const char *ppm, short nph, int checker)
{
	FILE *f = ppm ? fopen(ppm, "wb") : NULL;
	long e = 0;
	short x, y;

	if (f) fprintf(f, "P6\n%d %d\n255\n", rw, rh);
	for (y = 0; y < rh; y++)
		for (x = 0; x < rw; x++) {
			unsigned char ix = rect[(long)y * rw + x];
			short ph = nph == 1 ? 0
			         : nph == 2 ? (short)(checker ? ((x ^ y) & 1) : (y & 1))
			         :            (short)(((y & 1) << 1) | (x & 1));
			const unsigned char *got = bpal + (long)plut[ph][ix] * 3;

			if (f) fputc(got[0], f), fputc(got[1], f), fputc(got[2], f);
			e += perr(ix, got);
		}
	if (f) fclose(f);
	return (double)e / (double)((long)rw * rh);
}

/* Cut the rect alone at ncol, then report it flat and dithered. */
static void rect_only(short ncol, const char *tag)
{
	char path[256];

	crop();
	quant_banded(rect, rw, rh, clut, 1, ncol, 4, bpal, brem);
	build_dither(1, ncol);
	sprintf(path, "%s_none.ppm", tag);
	printf("  rect-only ncol=%2d  flat   %8.1f\n", ncol, render(path, 1, 0));
	build_dither(2, ncol);
	sprintf(path, "%s_row2.ppm", tag);
	printf("                     row2   %8.1f\n", render(path, 2, 0));
	sprintf(path, "%s_check2.ppm", tag);
	printf("                     check2 %8.1f\n", render(path, 2, 1));
	build_dither(4, ncol);
	sprintf(path, "%s_bayer4.ppm", tag);
	printf("                     bayer4 %8.1f\n", render(path, 4, 1));
}


/* THE IMPLEMENTABLE SHAPE. The ST has one hardware palette, but it already has
 * a Timer B raster split (ST_NBANDS), so the rows the viewport occupies can
 * carry their OWN sixteen. That is not the same as a viewport-only cut: those
 * rows are full width, so they also hold the roster panel beside the viewport.
 * This measures the real thing — cut rows [ry, ry+rh) at ncol, then report the
 * error inside the viewport rect and in the rest of the band separately. */
static void row_band(short ncol)
{
	long ein = 0, eout = 0, nin = 0, nout = 0;
	unsigned char nr[256];
	short i, k, x, y;

	memcpy(rect, chunky + (long)ry * W, (size_t)((long)rh * W));
	quant_banded(rect, W, rh, clut, 1, ncol, 4, bpal, brem);
	for (i = 0; i < 256; i++) {
		const unsigned char *c = clut + (long)i * 3;
		long br = 0x7FFFFFFFL; short jr = 0;

		for (k = 0; k < ncol; k++) {
			const unsigned char *pp = bpal + (long)k * 3;
			long dr = (long)c[0]-pp[0], dg = (long)c[1]-pp[1];
			long db = (long)c[2]-pp[2];
			long d = dr*dr + dg*dg + db*db;

			if (d < br) { br = d; jr = k; }
		}
		nr[i] = (unsigned char)jr;
	}
	{
		FILE *f = fopen("band_near.ppm", "wb");

		fprintf(f, "P6\n%d %d\n255\n", rw, rh);
		for (y = 0; y < rh; y++)
			for (x = 0; x < W; x++) {
				unsigned char ix = rect[(long)y * W + x];
				long e = perr(ix, bpal + (long)nr[ix] * 3);

				if (in_rect((short)x, (short)(ry + y))) {
					ein += e; nin++;
					fwrite(bpal + (long)nr[ix] * 3, 1, 3, f);
				} else { eout += e; nout++; }
			}
		fclose(f);
	}
	printf("  %-22s ncol=%2d  rect %8.1f   rest of band %8.1f\n",
	       "row band (nearest)", ncol, (double)ein / nin, (double)eout / nout);
}

/* Distinct source indices inside the rect — the ceiling on exact preservation. */
static short rect_indices(void)
{
	unsigned char seen[256];
	short x, y, n = 0;

	memset(seen, 0, sizeof seen);
	for (y = 0; y < rh; y++)
		for (x = 0; x < rw; x++)
			seen[chunky[(long)(ry + y) * W + rx + x]] = 1;
	for (x = 0; x < 256; x++)
		n = (short)(n + seen[x]);
	return n;
}

int main(int argc, char **argv)
{
	const char *frame = argc > 1 ? argv[1] : "frame.frm";
	const char *cl    = argc > 2 ? argv[2] : "clut.clt";
	double in16, out16, in8, out8;

	if (argc > 6) {
		rx = (short)atoi(argv[3]); ry = (short)atoi(argv[4]);
		rw = (short)atoi(argv[5]); rh = (short)atoi(argv[6]);
	}
	load(frame, chunky, W * H);
	load(cl, clut, 768);

	printf("%s  rect %dx%d at (%d,%d), %d distinct source indices\n",
	       frame, rw, rh, rx, ry, rect_indices());

	whole_frame(16, &in16, &out16);
	printf("  SHIPPING  whole-frame ncol=16 : rect %8.1f   outside %8.1f\n",
	       in16, out16);
	whole_frame(8, &in8, &out8);
	printf("            whole-frame ncol= 8 : rect %8.1f   outside %8.1f\n",
	       in8, out8);
	/* Same palette, three remaps — is it the palette or the labelling? */
	quant_banded(chunky, W, H, clut, 1, 16, 4, bpal, brem);
	remap_compare(chunky, W, H, 16, "whole frame");
	{	/* and where does that land INSIDE the viewport, which is the
		 * half the player is looking at? */
		long ebox = 0, ergb = 0, n = 0;
		short i, k, x, y;
		unsigned char nr[256];

		for (i = 0; i < 256; i++) {
			const unsigned char *c = clut + (long)i * 3;
			long br = 0x7FFFFFFFL; short jr = 0;

			for (k = 0; k < 16; k++) {
				const unsigned char *pp = bpal + (long)k * 3;
				long dr = (long)c[0]-pp[0], dg = (long)c[1]-pp[1];
				long db = (long)c[2]-pp[2];
				long d = dr*dr + dg*dg + db*db;

				if (d < br) { br = d; jr = k; }
			}
			nr[i] = (unsigned char)jr;
		}
		{	/* PPMs of the rect as each remap would render it */
			FILE *fb = fopen("ship_box.ppm", "wb");
			FILE *fn = fopen("ship_near.ppm", "wb");

			fprintf(fb, "P6\n%d %d\n255\n", rw, rh);
			fprintf(fn, "P6\n%d %d\n255\n", rw, rh);
			for (y = ry; y < ry + rh; y++)
				for (x = rx; x < rx + rw; x++) {
					unsigned char ix = chunky[(long)y * W + x];
					const unsigned char *gb = bpal + (long)brem[ix] * 3;
					const unsigned char *gn = bpal + (long)nr[ix] * 3;

					fwrite(gb, 1, 3, fb);
					fwrite(gn, 1, 3, fn);
					ebox += perr(ix, gb);
					ergb += perr(ix, gn);
					n++;
				}
			fclose(fb); fclose(fn);
		}
		printf("  %-22s ncol=16  box %8.1f   nearest-rgb %8.1f\n",
		       "  ...inside the rect", (double)ebox/n, (double)ergb/n);
	}
	crop();
	quant_banded(rect, rw, rh, clut, 1, 16, 4, bpal, brem);
	remap_compare(rect, rw, rh, 16, "rect only");
	quant_banded(rect, rw, rh, clut, 1, 8, 4, bpal, brem);
	remap_compare(rect, rw, rh, 8, "rect only");

	{
		long n = pack_chrome();
		short cw = (short)(n > W ? W : n);
		short ch = (short)(n / (cw ? cw : 1));

		quant_banded(rect, cw, ch, clut, 1, 8, 4, bpal, brem);
		remap_compare(rect, cw, ch, 8, "chrome only (outside)");
	}

	row_band(16);
	row_band(8);

	rect_only(16, "vp16");
	rect_only(8,  "vp8");
	return 0;
}
