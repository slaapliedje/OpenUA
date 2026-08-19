/*
 * planar_tile.h — wall tiles converted ONCE to ST-Low planes + a coverage mask,
 * so a repeat blit is word-wise plane writes instead of a per-pixel scatter.
 *
 * WHY THIS EXISTS (#144). l309c_tile's colour path is a MASKED PER-PIXEL blit: a
 * global 255 key plus per-set magenta keys, one store per surviving pixel. That
 * cannot be stamped into bitplanes cheaply — a masked write is read-modify-write
 * per pixel across four planes, which is the ~1900 cycles/pixel scalar path
 * ADR-0016 exists to avoid, and it is why the viewport still bulk-converts today.
 *
 * The way out is not a faster transpose, it is FEWER of them. Measured over a
 * 137-key walk: 15-28 tile blits per render, but only 48-64 DISTINCT tiles in the
 * whole run (the count plateaus), for 28-34 KB in this form. So convert each tile
 * once and blit the planes thereafter.
 *
 * Storage, per tile: for each row, for each 16-pixel group, five words —
 * four plane words then a MASK word (1 = opaque). Group g of row r is at
 * ((r * w16) + g) * PT_WORDS. That mask is the whole point: it makes the
 * destination merge `dst = (dst & ~m) | (src & m)` a word operation, so
 * transparency costs nothing per pixel.
 *
 * ★ THE LUT IS (chunky -> SLOT), NOT (chunky -> chunky). l309c_tile builds a
 * 256-entry table whose entries are 0 for "drop" and 0x100 | value otherwise;
 * this takes the same shape but the low byte must already be the 4-bit PALETTE
 * SLOT, i.e. composed with the backend's per-band remap. Bands may be composed in
 * any order: the remap was measured identical across every band a tile spans
 * (3,544 straddling blits, 0 disagreements), so one conversion serves all rows.
 * That agreement is SPATIAL. It says nothing about the remap changing over time —
 * a re-band rebuilds the palettes — which is what the epoch on the cache entry is
 * for; see planar_tile_cache.h.
 */
#ifndef PLATFORM_PLANAR_TILE_H
#define PLATFORM_PLANAR_TILE_H

#define PT_PLANES 4
#define PT_WORDS  5                       /* 4 planes + mask, per 16px group */
#define PT_W16(w) (((w) + 15) / 16)
/* words of storage for a w x h tile */
#define PT_WORDS_FOR(w, h) ((long)PT_W16(w) * (h) * PT_WORDS)

/*
 * Convert a chunky tile body (h rows of `w` bytes) into the planes+mask form.
 * `lut` is 256 entries: 0 = transparent, otherwise 0x100 | slot, slot in 0..15.
 * Bit 15 of a plane word is the LEFTMOST pixel, matching c2p4st_32.
 */
static void planar_tile_build(unsigned short *out, const unsigned char *body,
                              short w, short h, const unsigned short *lut)
{
	short w16 = PT_W16(w);
	short r, g, i;

	for (r = 0; r < h; r++) {
		const unsigned char *srow = body + (long)r * w;

		for (g = 0; g < w16; g++) {
			unsigned short *o = out + ((long)r * w16 + g) * PT_WORDS;
			short x0 = (short)(g * 16);

			for (i = 0; i < PT_WORDS; i++)
				o[i] = 0;
			for (i = 0; i < 16; i++) {
				short x = (short)(x0 + i);
				unsigned short t;
				unsigned short bit;
				short p;

				if (x >= w)
					break;             /* ragged last group */
				t = lut[srow[x]];
				if (!t)
					continue;          /* transparent pixel */
				bit = (unsigned short)(0x8000u >> i);
				o[PT_PLANES] |= bit;       /* mask */
				for (p = 0; p < PT_PLANES; p++)
					if (t & (1u << p))
						o[p] |= bit;
			}
		}
	}
}

/*
 * Blit a converted tile to (x, y) on an ST-Low interleaved page, clipped to
 * [clip_l, clip_r) x [clip_t, clip_b). `line_bytes` is the page pitch (160).
 *
 * A 16-pixel group occupies 8 bytes: plane p of group gx is at
 * row + gx * 8 + p * 2. An arbitrary x means the tile lands ACROSS group
 * boundaries, so each destination group takes the tail of one source group and
 * the head of the next — that shift is done here rather than by keeping
 * pre-shifted copies, which would cost 16x the memory (16 shifts x 64 tiles is
 * ~550 KB against 34 KB).
 *
 * Clipping rides on the mask: a clipped-out pixel simply has its mask bit
 * cleared, so edges need no separate path and cannot write outside the rect.
 */
static void planar_tile_blit(unsigned char *page, short line_bytes,
                             const unsigned short *tile, short w, short h,
                             short x, short y,
                             short clip_l, short clip_r,
                             short clip_t, short clip_b)
{
	short w16 = PT_W16(w);
	short sh  = (short)(x & 15);
	short gx0 = (short)(x >> 4);
	short r, d;

	for (r = 0; r < h; r++) {
		short dy = (short)(y + r);
		const unsigned short *srow;
		unsigned char *drow;

		if (dy < clip_t || dy >= clip_b)
			continue;
		srow = tile + (long)r * w16 * PT_WORDS;
		drow = page + (long)dy * line_bytes;

		/* One extra destination group: a shifted tile spills into it. */
		for (d = 0; d <= w16; d++) {
			short gx = (short)(gx0 + d);
			unsigned short pw[PT_PLANES], mw;
			short p, i;

			if (gx < 0 || (short)(gx * 16) >= clip_r)
				continue;
			if ((short)(gx * 16 + 15) < clip_l)
				continue;

			/* Gather this destination group from the two source groups
			 * that straddle it. */
			for (p = 0; p <= PT_PLANES; p++) {
				unsigned short cur =
				    (d < w16) ? srow[(long)d * PT_WORDS + p] : 0;
				unsigned short prv =
				    (d > 0) ? srow[(long)(d - 1) * PT_WORDS + p] : 0;
				unsigned short v;

				if (sh == 0)
					v = cur;           /* shift by 16 is UB */
				else
					v = (unsigned short)((prv << (16 - sh))
					                   | (cur >> sh));
				if (p < PT_PLANES)
					pw[p] = v;
				else
					mw = v;
			}

			/* Fold the horizontal clip into the mask. */
			for (i = 0; i < 16; i++) {
				short px = (short)(gx * 16 + i);

				if (px < clip_l || px >= clip_r)
					mw &= (unsigned short)~(0x8000u >> i);
			}
			if (!mw)
				continue;

			{
				unsigned short *dg =
				    (unsigned short *)(drow + (long)gx * 8);

				for (p = 0; p < PT_PLANES; p++)
					dg[p] = (unsigned short)
					        ((dg[p] & ~mw) | (pw[p] & mw));
			}
		}
	}
}

#endif /* PLATFORM_PLANAR_TILE_H */
