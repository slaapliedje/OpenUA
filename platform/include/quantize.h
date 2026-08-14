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
 * Reduce a `ncolors`-entry CLUT (`clut`, ncolors*3 bytes RGB, ncolors <= 256)
 * to at most `n` colours (n <= QUANT_MAX_N) snapped to `bits`/gun.
 *   out_pal[n*3]     — the reduced palette, snapped 8-bit RGB (tail zeroed)
 *   remap[ncolors]   — original index -> reduced index (0 .. return-1)
 * Returns the actual colour count (< n when the CLUT has fewer distinct
 * colours than the budget). The banded quantiser feeds it the COMPACT list of
 * colours actually used in a band, so ncolors is usually well under 256.
 */
static short quant_reduce_n(const unsigned char *clut, short ncolors,
                            short n, short bits,
                            unsigned char *out_pal, unsigned char *remap)
{
	unsigned char idx[256];
	short bstart[QUANT_MAX_N], blen[QUANT_MAX_N];
	short nbox = 1, i, j;

	if (ncolors < 1)
		ncolors = 1;
	if (ncolors > 256)
		ncolors = 256;
	if (n < 1)
		n = 1;
	if (n > QUANT_MAX_N)
		n = QUANT_MAX_N;

	for (i = 0; i < ncolors; i++)
		idx[i] = (unsigned char)i;
	bstart[0] = 0;
	blen[0] = ncolors;

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

/* Global reduce over a full 256-entry CLUT — the nbands==1 case. Convenience
 * wrapper; a backend that only bands never calls it, hence `unused`. */
static __attribute__((unused)) short
quant_reduce(const unsigned char *clut, short n, short bits,
             unsigned char *out_pal, unsigned char *remap)
{
	return quant_reduce_n(clut, 256, n, bits, out_pal, remap);
}

#define QUANT_MAX_BANDS 40

/* How many of a band's most POPULOUS colours are reproduced EXACTLY, before
 * the median cut divides what is left. See quant_banded. Bounded by ncol/2 so
 * the cut always keeps at least half the budget. */
#define QUANT_KEEP      6
/* A colour must cover at least this percent of the band to earn an exact slot
 * — that is what makes the reservation track flat AREAS (panels, chrome,
 * backdrops) rather than merely frequent ones. */
#define QUANT_KEEP_PCT  3

/*
 * Per-horizontal-band quantiser. Split the WxH chunky frame into `nbands`
 * equal horizontal strips; for each strip reduce the CLUT colours that
 * ACTUALLY appear in it to `ncol` (snapped `bits`/gun) and build that band's
 * 256->ncol remap. Each region gets a palette suited to its own content — the
 * win a single global reduce can't give (the granite chrome stops starving the
 * viewport).
 *
 * EXACT-PRESERVATION. Before cutting, each band reserves up to QUANT_KEEP
 * slots for the colours covering the most of it (>= QUANT_KEEP_PCT each) and
 * reproduces them EXACTLY. Two things fall out, and they are why this is not
 * merely an optimisation:
 *
 *   - Fidelity. The median cut is PRESENCE-weighted (each used colour counts
 *     once, however many pixels it covers — the population isn't known at
 *     set_palette time), so a flat panel filling half the band was averaged
 *     into a box with its neighbours and came back a different colour.
 *     Measured on captured HEIRS frames at the ST's 16-colour budget, mean
 *     squared RGB error: walk view 446 -> 207, BigPic 828 -> 429, the title
 *     screen 1713 -> 466.
 *   - SEAMS. A flat colour spanning several bands is now reproduced exactly in
 *     every one of them, so it cannot come back as a different shade either
 *     side of a band boundary. That striping (#40) is why per-band was turned
 *     off in ADR-0016 B1 in favour of one global palette; on the same frames
 *     it costs 685 seam pixels with a plain per-band cut and 100 with this.
 *
 *   band_pal[nbands*ncol*3] — per-band snapped palettes
 *   band_remap[nbands*256]  — per-band remap LUTs. Colours ABSENT from a band
 *                             at build time still get a mapping — the reduced
 *                             entry nearest in weighted RGB — because content
 *                             drawn after the reduce (the composited cursor, a
 *                             changed 3D view) must not fall to black. This
 *                             fallback used to bucket by LUMINANCE, which cost
 *                             it hue and turned stone walls cyan; see the
 *                             comment at the bucket table below.
 * nbands==1 reproduces the global reduce (over just the used colours).
 */
static void quant_banded(const unsigned char *chunky, short w, short h,
                         const unsigned char *clut,
                         short nbands, short ncol, short bits,
                         unsigned char *band_pal, unsigned char *band_remap)
{
	unsigned short cnt[256];         /* this band's population histogram   */
	unsigned char  kept[256];        /* 0 = no, else exact slot + 1        */
	unsigned char cclut[256 * 3];
	unsigned char cremap[256];
	short idxlist[256];
	short b, i, x, y, m;

	if (nbands < 1)
		nbands = 1;
	if (nbands > QUANT_MAX_BANDS)
		nbands = QUANT_MAX_BANDS;

	for (b = 0; b < nbands; b++) {
		unsigned char *bpal = band_pal + (long)b * ncol * 3;
		unsigned char *brem = band_remap + (long)b * 256;
		short y0 = (short)((long)b * h / nbands);
		short y1 = (short)((long)(b + 1) * h / nbands);
		short nkeep = 0, keepmax, n, ntot;
		long  total = 0;

		/* Population histogram for this band, EVERY OTHER row — bands are
		 * many rows tall, so alternate rows see the band's content, and a
		 * colour living only on skipped rows degrades to the RGB fallback
		 * below. Halves the biggest fixed cost of a re-band (the
		 * 64000-pixel scan) on the 8MHz targets. */
		for (i = 0; i < 256; i++) {
			cnt[i] = 0;
			kept[i] = 0;
		}
		for (y = y0; y < y1; y += 2) {
			const unsigned char *row = chunky + (long)y * w;

			for (x = 0; x < w; x++)
				cnt[row[x]]++;
			total += w;
		}

		/* Reserve exact slots for the most populous colours. Repeated
		 * max-scans (at most QUANT_KEEP passes over 256) rather than a
		 * sort — the budget is tiny and this stays 68000-cheap.
		 *
		 * Two CLUT indices can SNAP to the same hardware colour; the
		 * second must share the first's slot instead of burning one. */
		keepmax = (short)(ncol / 2);
		if (keepmax > QUANT_KEEP)
			keepmax = QUANT_KEEP;
		while (nkeep < keepmax) {
			short bi = -1;
			unsigned short bc = 0;
			unsigned char sr, sg, sb;
			short j, dup = -1;

			for (i = 0; i < 256; i++)
				if (!kept[i] && cnt[i] > bc) {
					bc = cnt[i];
					bi = i;
				}
			if (bi < 0 || (long)bc * 100 < total * QUANT_KEEP_PCT)
				break;
			sr = quant_snap(clut[bi * 3 + 0], bits);
			sg = quant_snap(clut[bi * 3 + 1], bits);
			sb = quant_snap(clut[bi * 3 + 2], bits);
			for (j = 0; j < nkeep; j++)
				if (bpal[j * 3 + 0] == sr && bpal[j * 3 + 1] == sg
				 && bpal[j * 3 + 2] == sb) {
					dup = j;
					break;
				}
			if (dup >= 0) {
				kept[bi] = (unsigned char)(dup + 1);
				continue;         /* shares a slot, costs none */
			}
			bpal[nkeep * 3 + 0] = sr;
			bpal[nkeep * 3 + 1] = sg;
			bpal[nkeep * 3 + 2] = sb;
			kept[bi] = (unsigned char)(nkeep + 1);
			nkeep++;
		}

		/* Everything else present goes to the median cut, which divides
		 * the slots the reservation left. */
		m = 0;
		for (i = 0; i < 256; i++) {
			if (cnt[i] && !kept[i]) {
				idxlist[m] = i;
				cclut[m * 3 + 0] = clut[i * 3 + 0];
				cclut[m * 3 + 1] = clut[i * 3 + 1];
				cclut[m * 3 + 2] = clut[i * 3 + 2];
				m++;
			}
		}
		if (m == 0 && nkeep == 0) {         /* empty band -> all black */
			for (i = 0; i < ncol * 3; i++)
				bpal[i] = 0;
			for (i = 0; i < 256; i++)
				brem[i] = 0;
			continue;
		}
		n = 0;
		if (m > 0 && ncol - nkeep > 0)
			n = quant_reduce_n(cclut, m, (short)(ncol - nkeep), bits,
			                   bpal + nkeep * 3, cremap);
		ntot = (short)(nkeep + n);          /* live entries in this band */
		for (i = ntot * 3; i < ncol * 3; i++)
			bpal[i] = 0;

		/* Absent-colour fallback via a 4x8x4 RGB BUCKET table.
		 *
		 * ★ THIS USED TO BUCKET BY LUMINANCE ALONE, AND THAT IS A COLOUR
		 * BUG, not just an approximation. Luma throws away hue, so any two
		 * colours of equal brightness are interchangeable to it — and FRUA
		 * has a textbook collision: the party roster's cyan (0,200,200) and
		 * mid-grey dungeon stone (150,150,150) BOTH have luma 150. A wall
		 * shade that misses the presence histogram therefore falls back to
		 * whichever entry is nearest in brightness, and when the roster cyan
		 * is the nearest, stone walls render CYAN. It only happens sometimes
		 * because the histogram below samples every OTHER row, so whether a
		 * given wall index is "present" depends on which rows it landed on.
		 *
		 * Bucketing in 3-D instead keeps hue. Cost stays bounded the same
		 * way: n*128 scalar distance evaluations per band (vs n*32 before,
		 * and vs the n*256 per-colour linear search whose ~1.3M cycles per
		 * re-band at 8MHz was the original objection). Green gets twice the
		 * resolution of red/blue — it carries most of the luminance. */
		{
			unsigned char btab[4 * 8 * 4];
			short bk, j;

			for (bk = 0; bk < 4 * 8 * 4; bk++) {
				/* bucket centre: r = bk>>5, g = (bk>>2)&7, b = bk&3 */
				short cr = (short)(((bk >> 5) & 3) * 64 + 32);
				short cg = (short)(((bk >> 2) & 7) * 32 + 16);
				short cb = (short)((bk & 3) * 64 + 32);
				long  bestd = 0x7FFFFFFFL;
				short bestj = 0;

				for (j = 0; j < ntot; j++) {
					short dr = (short)(bpal[j * 3 + 0] - cr);
					short dg = (short)(bpal[j * 3 + 1] - cg);
					short db = (short)(bpal[j * 3 + 2] - cb);
					/* 2:5:1 weights — the same perceptual
					 * weighting the luma version used, now
					 * applied per channel instead of collapsed */
					long  d  = 2L * dr * dr + 5L * dg * dg
					         + 1L * db * db;

					if (d < bestd) {
						bestd = d;
						bestj = j;
					}
				}
				btab[bk] = (unsigned char)bestj;
			}
			for (i = 0; i < 256; i++)
				brem[i] = btab[(((clut[i * 3 + 0] >> 6) & 3) << 5)
				             | (((clut[i * 3 + 1] >> 5) & 7) << 2)
				             |  ((clut[i * 3 + 2] >> 6) & 3)];
		}
		/* Present colours override the fallback: the reserved ones map to
		 * their exact slot, the rest to their cut slot (offset past the
		 * reservation). */
		for (i = 0; i < 256; i++)
			if (kept[i])
				brem[i] = (unsigned char)(kept[i] - 1);
		for (i = 0; i < m; i++)
			brem[idxlist[i]] = (unsigned char)(nkeep + cremap[i]);
	}
}

#endif /* PLATFORM_QUANTIZE_H */
