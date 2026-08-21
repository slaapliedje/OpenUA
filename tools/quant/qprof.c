/* qprof — phase-attribute quant_banded on the HOST (the ECS re-band).
 *
 * The emulator run costs five minutes per iteration and the first two phase
 * dumps were instrument bugs (a close outside its open's branch; a close 90
 * lines before its open). The quantiser is pure C over byte buffers, so the
 * brackets can be validated HERE, where the check "phases sum to the total" is
 * a one-second run. Shares on x86 are not 68000 cycle-exact, but with the
 * multiplies already gone the remaining work is compares/adds/loads, which
 * scale comparably — good enough to rank phases and to sanity-check brackets.
 *
 * Usage: qprof <frame.frm> <clut.clt> [ncol] [nbands] [reps]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static long prof_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)(ts.tv_sec * 1000000L + ts.tv_nsec / 1000L);   /* us */
}
#define QUANT_PROF
#define QUANT_PROF_T() prof_now()
#include "quantize.h"

#define W 320
#define H 200
static unsigned char chunky[W * H], clut[768];
static unsigned char pal[QUANT_MAX_BANDS * 32 * 3], rem[QUANT_MAX_BANDS * 256];

int main(int argc, char **argv)
{
	short ncol = (short)(argc > 3 ? atoi(argv[3]) : 32);
	short nb   = (short)(argc > 4 ? atoi(argv[4]) : 25);
	int   reps = argc > 5 ? atoi(argv[5]) : 200, r;
	FILE *f;
	long t0, tot;

	if (argc < 3) { fprintf(stderr, "usage: %s frm clt [ncol] [nbands] [reps]\n", argv[0]); return 1; }
	if (!(f = fopen(argv[1], "rb")) || fread(chunky, 1, W * H, f) != W * H) { perror(argv[1]); return 1; }
	fclose(f);
	if (!(f = fopen(argv[2], "rb")) || fread(clut, 1, 768, f) != 768) { perror(argv[2]); return 1; }
	fclose(f);

	t0 = prof_now();
	for (r = 0; r < reps; r++)
		quant_banded(chunky, W, H, clut, nb, ncol, 4, pal, rem);
	tot = prof_now() - t0;

	printf("total %ld us for %d reps of a %d-band cut\n", tot, reps, nb);
	printf("  hist    %10ld  %5.1f%%\n", quant_ph_hist,  100.0*quant_ph_hist/tot);
	printf("  keep    %10ld  %5.1f%%\n", quant_ph_keep,  100.0*quant_ph_keep/tot);
	printf("  cut     %10ld  %5.1f%%\n", quant_ph_cut,   100.0*quant_ph_cut/tot);
	printf("  remap   %10ld  %5.1f%%\n", quant_ph_remap, 100.0*quant_ph_remap/tot);
	printf("  buckets %10ld  %5.1f%%\n", quant_ph_buck,  100.0*quant_ph_buck/tot);
	{
		long sum = quant_ph_hist + quant_ph_keep + quant_ph_cut
		         + quant_ph_remap + quant_ph_buck;
		printf("  SUM     %10ld  %5.1f%%   (over 100%% = a bracket bug)\n",
		       sum, 100.0*sum/tot);
	}
	return 0;
}
