/* qrender — render a capture through quant_banded, as the backend would.
 *
 * The other tools print numbers; this one prints the picture. It exists
 * because MSE and the eye disagree often enough to matter (dither always
 * loses on MSE, and a remap that wins on MSE can still drop a small saturated
 * detail like a door). Build it against two different quantize.h revisions and
 * you have a before/after that needs no emulator.
 *
 * Usage: qrender <frame.frm> <clut.clt> <out.ppm> [ncol] [nbands] */
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
	const char *frame = argc > 1 ? argv[1] : "frame.frm";
	const char *cl    = argc > 2 ? argv[2] : "clut.clt";
	const char *out   = argc > 3 ? argv[3] : "out.ppm";
	short nc = (short)(argc > 4 ? atoi(argv[4]) : 16);
	short nb = (short)(argc > 5 ? atoi(argv[5]) : 1);
	FILE *f;
	long err = 0;
	short x, y;

	load(frame, chunky, W * H);
	load(cl, clut, 768);
	quant_banded(chunky, W, H, clut, nb, nc, 4, bpal, brem);

	f = fopen(out, "wb");
	if (!f) { perror(out); return 1; }
	fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (y = 0; y < H; y++) {
		short b = (short)((long)y * nb / H);
		const unsigned char *rem = brem + (long)b * 256;
		const unsigned char *pal = bpal + (long)b * nc * 3;

		for (x = 0; x < W; x++) {
			unsigned char ix = chunky[y * W + x];
			const unsigned char *got = pal + (long)rem[ix] * 3;
			const unsigned char *want = clut + (long)ix * 3;

			fwrite(got, 1, 3, f);
			err += (long)(want[0]-got[0]) * (want[0]-got[0])
			     + (long)(want[1]-got[1]) * (want[1]-got[1])
			     + (long)(want[2]-got[2]) * (want[2]-got[2]);
		}
	}
	fclose(f);
	printf("%s -> %s  ncol=%d nbands=%d  mse %.1f\n", frame, out, nc, nb,
	       (double)err / (double)(W * H));
	return 0;
}
