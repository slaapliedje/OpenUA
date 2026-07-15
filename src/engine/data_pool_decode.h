/*
 * data_pool_decode.h — pure THINK C DATA / DREL decode.
 *
 * Dependency-free (no Mac shim, no globals) so it is shared by the
 * engine's runtime replay (data_pool_replay.c) AND exercised on the
 * host (tests/test_data_pool_decode.py) against the tools/datapool.py
 * reference. Mirrors that Python exactly:
 *   - dp_expand():   expand the compressed DATA image with the ZERO
 *                    run-length table (== datapool.expand_data).
 *   - dp_drel_word(): split one DREL word into (base, even offset)
 *                    (== datapool._split_reloc_word).
 */

#ifndef ENGINE_DATA_POOL_DECODE_H
#define ENGINE_DATA_POOL_DECODE_H

/*
 * Expand THINK C's compressed DATA using the ZERO run-length table:
 * copy each 16-bit word verbatim; a 0x0000 word is followed by `n`
 * extra zero bytes, where `n` is the next big-endian short pulled from
 * ZERO (so a zero word expands to n+2 zeros). If `dst` is NULL this
 * only measures. Returns the expanded length, or -1 if ZERO runs out
 * mid-expansion.
 */
static long dp_expand(const unsigned char *data, long dlen,
                      const unsigned char *zero, long zlen,
                      unsigned char *dst)
{
	long di = 0, zi = 0, out = 0;

	while (di + 2 <= dlen) {
		unsigned char b0 = data[di], b1 = data[di + 1];

		di += 2;
		if (dst) {
			dst[out] = b0;
			dst[out + 1] = b1;
		}
		out += 2;
		if (b0 == 0 && b1 == 0) {
			long n, k;

			if (zi + 2 > zlen)
				return -1;
			n = ((long)zero[zi] << 8) | zero[zi + 1];
			zi += 2;
			if (dst)
				for (k = 0; k < n; k++)
					dst[out + k] = 0;
			out += n;
		}
	}
	return out;
}

/*
 * Decode one big-endian DREL word: bit 0 selects the base (1 = A4/STRS
 * pool, 0 = A5 world), bits 15..1 are an even signed offset from that
 * base. Writes the offset to *off; returns 1 for A4, 0 for A5.
 */
static int dp_drel_word(unsigned short w, short *off)
{
	*off = (short)(w & 0xFFFE);
	return (int)(w & 1);
}

#endif /* ENGINE_DATA_POOL_DECODE_H */
