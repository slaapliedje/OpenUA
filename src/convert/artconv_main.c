/* Host-side CLI over the artconv core, for the byte-exactness tests
 * (tests/test_artconv_c.py): the C core's output must equal the Python
 * reference's for every corpus and synthetic container.
 *
 *   artconv conv <in> <out>            convert HLIB <-> GLIB (auto-detect)
 *   artconv mono <in> <out> <dosname>  synthesize the 1-bit GLIB; the mono
 *                                      family is derived from <dosname>
 *
 * Builds with the host compiler (no cross toolchain needed):
 *   cc -O2 -o artconv src/convert/artconv.c src/convert/artconv_main.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "artconv.h"

#ifndef SCRATCH_CAP
#define SCRATCH_CAP (512L * 1024)
#endif

static unsigned char *read_all(const char *path, long *len)
{
	FILE *f = fopen(path, "rb");
	unsigned char *buf;
	long n;
	if (!f)
		return 0;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(n ? n : 1);
	if (!buf || fread(buf, 1, n, f) != (size_t)n) {
		fclose(f);
		free(buf);
		return 0;
	}
	fclose(f);
	*len = n;
	return buf;
}

int main(int argc, char **argv)
{
	unsigned char *src, *dst, *scratch;
	long n, cap, r;
	FILE *f;

	if (argc < 4) {
		fprintf(stderr, "usage: %s conv <in> <out> | %s mono <in> <out> <dosname>\n",
			argv[0], argv[0]);
		return 2;
	}
	src = read_all(argv[2], &n);
	if (!src) {
		fprintf(stderr, "cannot read %s\n", argv[2]);
		return 1;
	}
	cap = n + 65536;
	dst = malloc(cap);
	scratch = malloc(SCRATCH_CAP);
	if (!dst || !scratch) {
		fprintf(stderr, "out of memory\n");
		return 1;
	}

	if (!strcmp(argv[1], "conv")) {
		r = artconv_convert(src, n, dst, cap, scratch, SCRATCH_CAP);
	} else if (!strcmp(argv[1], "mono")) {
		int num, den, mode;
		if (argc < 5 || !artconv_mono_family(argv[4], &num, &den, &mode)) {
			fprintf(stderr, "no mono family for %s\n",
				argc < 5 ? "(missing dosname)" : argv[4]);
			return 3;
		}
		r = artconv_mono_synth(src, n, dst, cap, scratch, SCRATCH_CAP,
				       num, den, mode);
	} else {
		fprintf(stderr, "unknown mode %s\n", argv[1]);
		return 2;
	}

	if (r < 0) {
		fprintf(stderr, "%s: artconv error %ld\n", argv[2], r);
		return 4;
	}
	f = fopen(argv[3], "wb");
	if (!f || fwrite(dst, 1, r, f) != (size_t)r) {
		fprintf(stderr, "cannot write %s\n", argv[3]);
		return 1;
	}
	fclose(f);
	printf("%s -> %s (%ld bytes)\n", argv[2], argv[3], r);
	return 0;
}
