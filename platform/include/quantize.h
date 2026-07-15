/*
 * quantize.h — runtime palette reduction for the native ECS/ST backends.
 *
 * The engine renders into one 256-colour chunky buffer. A native ECS Amiga
 * (32 colours) or Atari ST/STE (16) can't show that many, so whenever the
 * engine changes its palette we must REDUCE the live 256-entry CLUT to the
 * machine's budget, load those N hardware registers, and build a 256->N
 * nearest-colour remap LUT the present path indexes each pixel through before
 * packing bitplanes (c2p32.h takes the remapped indices unchanged).
 *
 * Algorithm: median cut over the 256 CLUT entries (each slot weight 1, so a
 * colour used in K slots carries weight K — a cheap proxy for pixel
 * population, which isn't known at set_palette time). Cut cost is per
 * set_palette (scene change), not per frame. Portable C, 68000-clean by
 * construction — the reduction runs on the target CPU, and the host test
 * (tests/test_quantize.py) exercises this same code.
 *
 * quant_snap() lands each gun on the hardware palette grid at the CELL
 * MIDPOINT, matching tools/palette_preview.py so the C reducer and the host
 * viability preview agree on colours. Reps are emitted as snapped 8-bit RGB;
 * each backend converts to its register format (Amiga LoadRGB32 8-bit, STE
 * 4-bit, ST 3-bit).
 */

#ifndef PLATFORM_QUANTIZE_H
#define PLATFORM_QUANTIZE_H

#define QUANT_MAX_N 64

/* Snap one 8-bit gun value to `bits` per gun, to the midpoint of its cell. */
static unsigned char quant_snap(short v, short bits)
{
	short step = 256 >> bits;
	short s = (v / step) * step + step / 2;
	return (unsigned char)(s > 255 ? 255 : s);
}

/* Partition idx[start .. start+len) about the median of channel `axis`:
 * members with axis value <= threshold move to the front, the rest follow,
 * both sides non-empty (caller guarantees spread >= 1). Returns the left
 * side's length. Counting-based (no comparison sort) — O(len)+O(256). */
static short quant_partition(unsigned char *idx, const unsigned char *clut,
                             short start, short len, short axis)
{
	unsigned short hist[256];
	unsigned char tmp[256];
	short i, lo = 255, hi = 0, t, cum, half, li, ri;

	for (i = 0; i < 256; i++)
		hist[i] = 0;
	for (i = 0; i < len; i++) {
		short v = clut[idx[start + i] * 3 + axis];
		hist[v]++;
		if (v < lo) lo = v;
		if (v > hi) hi = v;
	}
	/* threshold in [lo, hi-1] so both the min- and max-valued members land
	 * on opposite sides — guarantees a genuine split when spread >= 1. */
	half = len >> 1;
	cum = 0;
	for (t = lo; t < hi; t++) {
		cum += hist[t];
		if (cum >= half)
			break;
	}
	if (t >= hi)
		t = hi - 1;

	li = 0;
	for (i = 0; i < len; i++)
		if (clut[idx[start + i] * 3 + axis] <= t)
			tmp[li++] = idx[start + i];
	ri = li;
	for (i = 0; i < len; i++)
		if (clut[idx[start + i] * 3 + axis] > t)
			tmp[ri++] = idx[start + i];
	for (i = 0; i < len; i++)
		idx[start + i] = tmp[i];
	return li;
}

/*
 * Reduce a 256-entry CLUT (`clut`, 256*3 bytes RGB) to at most `n` colours
 * (n <= QUANT_MAX_N) snapped to `bits`/gun.
 *   out_pal[n*3] — the reduced palette, snapped 8-bit RGB (unused tail zeroed)
 *   remap[256]   — original index -> reduced index (0 .. return-1)
 * Returns the actual colour count (< n when the CLUT has fewer distinct
 * colours than the budget).
 */
static short quant_reduce(const unsigned char *clut, short n, short bits,
                          unsigned char *out_pal, unsigned char *remap)
{
	unsigned char idx[256];
	short bstart[QUANT_MAX_N], blen[QUANT_MAX_N];
	short nbox = 1, i, j;

	if (n < 1)
		n = 1;
	if (n > QUANT_MAX_N)
		n = QUANT_MAX_N;

	for (i = 0; i < 256; i++)
		idx[i] = (unsigned char)i;
	bstart[0] = 0;
	blen[0] = 256;

	while (nbox < n) {
		short best = -1, bestspread = 0, bestaxis = 0, b;

		for (b = 0; b < nbox; b++) {
			short lo[3], hi[3], a, s;

			if (blen[b] < 2)
				continue;
			lo[0] = lo[1] = lo[2] = 255;
			hi[0] = hi[1] = hi[2] = 0;
			for (j = 0; j < blen[b]; j++) {
				const unsigned char *c = clut + idx[bstart[b] + j] * 3;

				for (a = 0; a < 3; a++) {
					if (c[a] < lo[a]) lo[a] = c[a];
					if (c[a] > hi[a]) hi[a] = c[a];
				}
			}
			for (a = 0; a < 3; a++) {
				s = hi[a] - lo[a];
				if (s > bestspread) {
					bestspread = s;
					best = b;
					bestaxis = a;
				}
			}
		}
		if (best < 0)
			break;		/* every box uniform — fewer distinct than n */

		{
			short half = quant_partition(idx, clut, bstart[best],
			                             blen[best], bestaxis);

			bstart[nbox] = bstart[best] + half;
			blen[nbox] = blen[best] - half;
			blen[best] = half;
			nbox++;
		}
	}

	for (i = 0; i < nbox; i++) {
		long sr = 0, sg = 0, sb = 0;

		for (j = 0; j < blen[i]; j++) {
			const unsigned char *c = clut + idx[bstart[i] + j] * 3;

			sr += c[0];
			sg += c[1];
			sb += c[2];
		}
		out_pal[i * 3 + 0] = quant_snap((short)(sr / blen[i]), bits);
		out_pal[i * 3 + 1] = quant_snap((short)(sg / blen[i]), bits);
		out_pal[i * 3 + 2] = quant_snap((short)(sb / blen[i]), bits);
		for (j = 0; j < blen[i]; j++)
			remap[idx[bstart[i] + j]] = (unsigned char)i;
	}
	for (i = nbox; i < n; i++)
		out_pal[i * 3 + 0] = out_pal[i * 3 + 1] = out_pal[i * 3 + 2] = 0;

	return nbox;
}

#endif /* PLATFORM_QUANTIZE_H */
