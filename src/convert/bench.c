/* convbench — time the artconv core on target hardware (Hatari).
 *
 * Enumerates every *.TLB in the current GEMDOS directory (stage a module's
 * DOS art there), converts each HLIB -> GLIB and synthesizes the mono
 * .tlb, timing both with the 200 Hz system tick (hz200, 0x4ba). hz200
 * advances with EMULATED cycles, so the numbers are valid even under
 * Hatari --fast-forward.
 *
 * Build (68000 worst case / 68020-60 Falcon):
 *   m68k-atari-mint-gcc -m68000    -msoft-float -std=gnu99 -O2 \
 *       -fomit-frame-pointer -o convb000.prg src/convert/bench.c src/convert/artconv.c
 *   m68k-atari-mint-gcc -m68020-60 -msoft-float -std=gnu99 -O2 \
 *       -fomit-frame-pointer -o convb020.prg src/convert/bench.c src/convert/artconv.c
 *
 * Run: hatari --machine st ... --auto 'C:\CONVB000.PRG' -d <dir-with-TLBs>
 * The report lands on the console (--conout 2). This is the measurement
 * behind the in-engine-conversion decision on task #23.
 */
#include <osbind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "artconv.h"

#define DST_CAP     (400L * 1024)
#define SCRATCH_CAP (512L * 1024)

struct dta {
	char reserved[21];
	char attr;
	unsigned short time, date;
	long size;
	char name[14];
};

static long read_hz200(void)
{
	long ssp = Super(0L);
	long v = *(volatile long *)0x4baL;
	Super((void *)ssp);
	return v;
}

/* Repetitions per file so small files still span several 5 ms ticks. */
static int reps_for(long size)
{
	if (size < 4096)
		return 20;
	if (size < 32768)
		return 5;
	return 2;
}

int main(void)
{
	struct dta d;
	unsigned char *src, *dst, *mono, *scratch;
	long total_conv = 0, total_mono = 0, total_bytes = 0;
	long max_conv = 0, max_mono = 0;
	char max_conv_name[14] = "", max_mono_name[14] = "";
	int nfiles = 0, nmono = 0, nskip = 0, err;

	dst = malloc(DST_CAP);
	mono = malloc(DST_CAP);
	scratch = malloc(SCRATCH_CAP);
	if (!dst || !mono || !scratch) {
		printf("CONVBENCH: out of memory for work buffers\n");
		return 1;
	}

	printf("CONVBENCH: start (hz200 tick = 5 ms)\n");
	printf("%-13s %7s %8s %8s\n", "file", "bytes", "conv-ms", "mono-ms");

	Fsetdta((void *)&d);
	err = Fsfirst("*.TLB", 0);
	while (err == 0) {
		long fh, n, r, t0, t1, conv_ms, mono_ms = -1;
		int reps, i, num, den, mode;

		src = malloc(d.size);
		if (!src) {
			printf("%-13s %7ld SKIP (no memory)\n", d.name, d.size);
			err = Fsnext();
			continue;
		}
		fh = Fopen(d.name, 0);
		if (fh < 0) {
			printf("%-13s open failed (%ld)\n", d.name, fh);
			free(src);
			err = Fsnext();
			continue;
		}
		n = Fread((int)fh, d.size, src);
		Fclose((int)fh);
		if (n != d.size || n < 4 || memcmp(src, "HLIB", 4)) {
			free(src);
			err = Fsnext();
			continue;	/* not DOS art (or short read) */
		}

		reps = reps_for(n);
		t0 = read_hz200();
		for (i = 0; i < reps; i++)
			r = artconv_convert(src, n, dst, DST_CAP,
					    scratch, SCRATCH_CAP);
		t1 = read_hz200();
		if (r < 0) {
			printf("%-13s %7ld SKIP (conv err %ld)\n",
			       d.name, n, r);
			nskip++;
			free(src);
			err = Fsnext();
			continue;
		}
		conv_ms = (t1 - t0) * 5 / reps;

		if (artconv_mono_family(d.name, &num, &den, &mode)) {
			long glen = r, m;
			t0 = read_hz200();
			for (i = 0; i < reps; i++)
				m = artconv_mono_synth(dst, glen, mono, DST_CAP,
						       scratch, SCRATCH_CAP,
						       num, den, mode);
			t1 = read_hz200();
			if (m < 0) {
				printf("%-13s mono SKIP (err %ld)\n", d.name, m);
				nskip++;
			} else {
				mono_ms = (t1 - t0) * 5 / reps;
				total_mono += mono_ms;
				nmono++;
				if (mono_ms > max_mono) {
					max_mono = mono_ms;
					strcpy(max_mono_name, d.name);
				}
			}
		}

		printf("%-13s %7ld %8ld %8ld\n", d.name, n, conv_ms, mono_ms);
		total_conv += conv_ms;
		total_bytes += n;
		nfiles++;
		if (conv_ms > max_conv) {
			max_conv = conv_ms;
			strcpy(max_conv_name, d.name);
		}
		free(src);
		err = Fsnext();
	}

	printf("---\n");
	printf("CONVBENCH files=%d monofiles=%d skips=%d bytes=%ld\n",
	       nfiles, nmono, nskip, total_bytes);
	printf("CONVBENCH conv total=%ld ms  worst=%ld ms (%s)\n",
	       total_conv, max_conv, max_conv_name);
	printf("CONVBENCH mono total=%ld ms  worst=%ld ms (%s)\n",
	       total_mono, max_mono, max_mono_name);
	printf("CONVBENCH whole-module=%ld ms\n", total_conv + total_mono);
	printf("CONVBENCH DONE\n");
	return 0;
}
