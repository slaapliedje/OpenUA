"""c2p4st.h — the nibble-input 4-plane ST c2p (the ST backend's hot loop) must
be bit-identical to the naive per-pixel scatter for random chunky data and a
random 256->16 LUT. Host-compiled like the other c2p/quantize tests."""
import os
import shutil
import subprocess

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "c2p4st.h"

static unsigned s_rng = 77771u;
static unsigned char rnd(void) { s_rng = s_rng*1103515245u + 12345u; return (unsigned char)(s_rng >> 16); }

/* Naive reference: remap, then scatter each pixel's 4 plane bits into the
 * ST-low interleaved word layout (d[p] = plane p px 0-15, d[p+4] = px 16-31,
 * bit 15 = leftmost). */
static void naive(const unsigned char *src, const unsigned char *lut,
                  unsigned short *d)
{
	int i, p;
	for (i = 0; i < 8; i++) d[i] = 0;
	for (i = 0; i < 32; i++) {
		unsigned char v = lut[src[i]];
		int grp = (i >> 4) * 4;          /* 0 or 4 */
		int bit = 15 - (i & 15);
		for (p = 0; p < 4; p++)
			if (v & (1 << p))
				d[grp + p] |= (unsigned short)(1 << bit);
	}
}

int main(void)
{
	unsigned char src[32], lut[256];
	unsigned short ref[8], fast[8];
	int trial, i;

	for (trial = 0; trial < 2000; trial++) {
		for (i = 0; i < 256; i++) lut[i] = (unsigned char)(rnd() & 15);
		for (i = 0; i < 32; i++)  src[i] = rnd();
		naive(src, lut, ref);
		c2p4st_32(src, lut, fast);
		if (memcmp(ref, fast, sizeof ref)) {
			printf("MISMATCH trial %d\n", trial);
			for (i = 0; i < 8; i++)
				printf("  d[%d]: ref %04x fast %04x\n", i, ref[i], fast[i]);
			return 1;
		}
	}
	printf("OK 2000 trials\n");
	return 0;
}
"""


@pytest.mark.skipif(shutil.which("cc") is None, reason="no host C compiler")
def test_c2p4st_matches_naive(tmp_path):
	harness = tmp_path / "h.c"
	harness.write_text(HARNESS)
	exe = tmp_path / "t"
	subprocess.run(
		["cc", "-O2", "-Wall", "-o", str(exe), str(harness),
		 "-I", os.path.join(REPO, "platform", "include")],
		check=True, capture_output=True, text=True)
	out = subprocess.run([str(exe)], check=True,
	                     capture_output=True, text=True).stdout
	assert "OK" in out, out
