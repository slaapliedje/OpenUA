/* artconv — FRUA art-container converter core, DOS `HLIB` <-> Mac `GLIB`.
 *
 * C port of tools/art_convert.py (the byte-exact-tested REFERENCE — change
 * the Python first, then mirror here; tests/test_artconv_c.py asserts the
 * two produce identical bytes over the corpus and synthetic containers).
 *
 * Dependency-free C99 integer code, 68000-safe (no unaligned loads), no
 * allocation — the caller supplies a scratch arena. Shared by:
 *   - the engine (on-load conversion of DOS fan-module art),
 *   - the module-installer utility (ZIP -> convert in place),
 *   - the Hatari benchmark PRG (src/convert/bench.c).
 */
#ifndef ARTCONV_H
#define ARTCONV_H

enum {
	ARTCONV_ERR_BAD = -1,		/* not an HLIB/GLIB container / truncated */
	ARTCONV_ERR_UNSUPPORTED = -2,	/* entry uses an unconvertible codec/flag */
	ARTCONV_ERR_SPACE = -3		/* dst or scratch buffer too small */
};

/* Mono-synthesis stream modes (MONO_FAMILIES in the Python). */
enum {
	ARTCONV_MONO_PLANAR = 0,	/* 0x90 plain / 0x91 keep-mask+data */
	ARTCONV_MONO_PACK = 1		/* 0x92 row-aligned PackBits */
};

/* Convert a whole container to the OTHER format (HLIB -> GLIB or back).
 * Returns the output length, or a negative ARTCONV_ERR_*.
 * dst must not overlap src. Worst observed growth is a handful of bytes
 * (method-23 re-encode); dstcap = srclen + 1024 is comfortable.
 * scratch needs 2 * (max entry pixel area) bytes + slack; 160 KB covers
 * every file in the fan corpus (largest entry is a 320x200 picture). */
long artconv_convert(const unsigned char *src, long srclen,
		     unsigned char *dst, long dstcap,
		     unsigned char *scratch, long scratchcap);

/* Per-family mono scale/mode from the DOS art filename (basename is
 * extracted internally; case-insensitive). Returns 1 and fills
 * num/den/mode when the stem has a mono family, 0 when it has none
 * (FRAME/TITLE/...: colour-only, no .tlb synthesis). */
int artconv_mono_family(const char *filename, int *num, int *den, int *mode);

/* Synthesize a 1-bit mono GLIB from a colour GLIB (convert first!).
 * Returns the output length or a negative ARTCONV_ERR_*.
 * dstcap = srclen is comfortable (mono output is smaller: 1bpp + RLE);
 * scratch as for artconv_convert. */
long artconv_mono_synth(const unsigned char *glib, long len,
			unsigned char *dst, long dstcap,
			unsigned char *scratch, long scratchcap,
			int num, int den, int mode);

#endif /* ARTCONV_H */
