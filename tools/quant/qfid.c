/* What does quant_banded ACTUALLY cost us in fidelity, measured the way the
 * engine uses it: indices + a 256-entry CLUT in, per-band palettes + remap
 * out, error against the true CLUT colour of every pixel.
 *
 * This is the engine's own path, not a re-implementation, so the number is
 * comparable to what the ST renders — unlike tools/quantsim.py, which median-
 * cuts the RGB image directly and therefore measures a different pipeline. */
#include <stdio.h>
#include <stdlib.h>
#include "quantize.h"

#define W 320
#define H 200

static unsigned char chunky[W * H], clut[768];
static unsigned char bpal[QUANT_MAX_BANDS * 32 * 3];
static unsigned char brem[QUANT_MAX_BANDS * 256];

static void load(const char *p, unsigned char *d, long n)
{
	FILE *f = fopen(p, "rb");
	if (!f || fread(d, 1, (size_t)n, f) != (size_t)n) { perror(p); exit(1); }
	fclose(f);
}

int main(int argc, char **argv)
{
	short nb = (short)(argc > 1 ? atoi(argv[1]) : 10);
	short nc = (short)(argc > 2 ? atoi(argv[2]) : 16);
	const char *frame = argc > 3 ? argv[3] : "frame.bin";
	const char *cl    = argc > 4 ? argv[4] : "clut.bin";
	long   err = 0;
	short  x, y;

	load(frame, chunky, W * H);
	load(cl, clut, 768);
	quant_banded(chunky, W, H, clut, nb, nc, 4, bpal, brem);

	for (y = 0; y < H; y++) {
		short b = (short)((long)y * nb / H);
		const unsigned char *rem = brem + (long)b * 256;
		const unsigned char *pal = bpal + (long)b * nc * 3;

		for (x = 0; x < W; x++) {
			unsigned char ix = chunky[y * W + x];
			const unsigned char *got = pal + (long)rem[ix] * 3;
			const unsigned char *want = clut + (long)ix * 3;
			long dr = (long)want[0] - got[0];
			long dg = (long)want[1] - got[1];
			long db = (long)want[2] - got[2];

			err += dr * dr + dg * dg + db * db;
		}
	}
	printf("nbands=%2d ncol=%2d   mean sq RGB err = %8.1f\n", nb, nc,
	       (double)err / (double)(W * H));
	return 0;
}
