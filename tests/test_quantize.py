"""quantize.h median-cut palette reducer, exercised through the HOST compiler
(the header is portable, 68000-clean C by design, so host-green == target-green
modulo compiler bugs). Three properties:

  identity   — a CLUT with <= N distinct GRID colours must round-trip with
               ZERO error (every original index remaps to its exact colour);
               this pins the partition/rep logic.
  validity   — every remap index is in range, every box non-empty, every rep
               lands on the hardware grid.
  monotone   — reducing the same random CLUT to more colours cannot INCREASE
               the mean-squared remap error (32 <= 16 <= 8).
"""
import os
import shutil
import subprocess

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HARNESS = r"""
#include <stdio.h>
#include "quantize.h"

/* deterministic LCG so the host run is reproducible */
static unsigned s_rng = 2463534242u;
static unsigned char rnd(void) { s_rng = s_rng*1103515245u + 12345u; return (unsigned char)(s_rng >> 16); }

/* --- identity: <= N distinct grid colours must remap with zero error --- */
static int test_identity(short n, short bits)
{
	unsigned char clut[768], pal[QUANT_MAX_N*3], remap[256];
	short step = 256 >> bits, i, nbox;
	/* build K = n distinct grid-midpoint colours, then fill 256 slots by
	 * repeating them (repeats must never separate). */
	unsigned char base[QUANT_MAX_N*3];
	short K = n;
	for (i = 0; i < K; i++) {
		base[i*3+0] = (rnd()/step)*step + step/2;
		base[i*3+1] = (rnd()/step)*step + step/2;
		base[i*3+2] = (rnd()/step)*step + step/2;
	}
	for (i = 0; i < 256; i++) {
		short k = i % K;
		clut[i*3+0]=base[k*3+0]; clut[i*3+1]=base[k*3+1]; clut[i*3+2]=base[k*3+2];
	}
	nbox = quant_reduce(clut, n, bits, pal, remap);
	for (i = 0; i < 256; i++) {
		unsigned char *want = clut + i*3, *got = pal + remap[i]*3;
		if (got[0]!=want[0] || got[1]!=want[1] || got[2]!=want[2]) {
			printf("IDENTITY MISMATCH n=%d slot=%d want %d,%d,%d got %d,%d,%d\n",
			       n, i, want[0],want[1],want[2], got[0],got[1],got[2]);
			return 1;
		}
	}
	(void)nbox;
	return 0;
}

/* --- validity + return the MSE of a random-CLUT reduction --- */
static long reduce_mse(short n, short bits, int *bad)
{
	unsigned char clut[768], pal[QUANT_MAX_N*3], remap[256];
	short nbox, i;
	long mse = 0;
	for (i = 0; i < 768; i++) clut[i] = rnd();
	nbox = quant_reduce(clut, n, bits, pal, remap);
	if (nbox < 1 || nbox > n) { printf("BAD nbox=%d (n=%d)\n", nbox, n); *bad=1; }
	for (i = 0; i < 256; i++) {
		if (remap[i] >= nbox) { printf("BAD remap[%d]=%d nbox=%d\n", i, remap[i], nbox); *bad=1; }
	}
	for (i = 0; i < nbox*3; i++) {
		short step = 256 >> bits, cell = (pal[i]-step/2);
		if (cell % step != 0) { printf("BAD rep %d not on grid (bits=%d)\n", pal[i], bits); *bad=1; }
	}
	for (i = 0; i < 256; i++) {
		short dr = clut[i*3+0]-pal[remap[i]*3+0];
		short dg = clut[i*3+1]-pal[remap[i]*3+1];
		short db = clut[i*3+2]-pal[remap[i]*3+2];
		mse += (long)dr*dr + (long)dg*dg + (long)db*db;
	}
	return mse;
}

int main(void)
{
	int bad = 0;
	long e8, e16, e32;

	if (test_identity(16, 4)) return 1;
	if (test_identity(32, 4)) return 1;
	if (test_identity(16, 3)) return 1;

	s_rng = 99991u; e8  = reduce_mse(8,  4, &bad);
	s_rng = 99991u; e16 = reduce_mse(16, 4, &bad);
	s_rng = 99991u; e32 = reduce_mse(32, 4, &bad);
	if (bad) return 1;
	if (!(e32 <= e16 && e16 <= e8)) {
		printf("NOT MONOTONE  e8=%ld e16=%ld e32=%ld\n", e8, e16, e32);
		return 1;
	}
	printf("OK  mse(8)=%ld mse(16)=%ld mse(32)=%ld\n", e8, e16, e32);
	return 0;
}
"""


@pytest.mark.skipif(shutil.which("cc") is None, reason="no host C compiler")
def test_quantize_properties(tmp_path):
	harness = tmp_path / "harness.c"
	harness.write_text(HARNESS)
	exe = tmp_path / "quant_test"
	subprocess.run(
		["cc", "-O2", "-Wall", "-o", str(exe), str(harness),
		 "-I", os.path.join(REPO, "platform", "include")],
		check=True, capture_output=True, text=True)
	out = subprocess.run([str(exe)], check=True,
	                     capture_output=True, text=True).stdout
	assert "OK" in out, out
