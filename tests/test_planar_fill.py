"""planar_put_stlow / planar_fill_stlow — the draw-time plane-store primitives
(ADR-0016 draw-time present model) must set exactly the addressed pixels' slot
bits in ST-Low interleaved planes and leave every other pixel untouched, for
random pixels, random rects, and clipped rects. Host-compiled like the other
c2p/planar tests; verified against an independent bit decoder."""
import os
import shutil
import subprocess

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "planar.h"

#define W  64
#define H  16
#define NP 4
#define LB (W / 16 * NP * 2)        /* 32 bytes/line */

static unsigned s_rng = 12345u;
static unsigned char rnd(void) { s_rng = s_rng*1103515245u + 12345u; return (unsigned char)(s_rng >> 16); }

/* Independent decoder: read pixel (x,y)'s slot back out of the interleaved
 * planes (plane p's word for group g at y*LB + g*NP*2 + p*2, MSB = leftmost). */
static unsigned char slot_at(const unsigned char *d, int x, int y)
{
	int g = x >> 4, bit = x & 15, byte = bit >> 3, p;
	unsigned char mask = (unsigned char)(0x80u >> (bit & 7)), s = 0;
	const unsigned char *grp = d + (long)y * LB + (long)g * NP * 2;
	for (p = 0; p < NP; p++)
		if (grp[p * 2 + byte] & mask)
			s |= (unsigned char)(1 << p);
	return s;
}

int main(void)
{
	unsigned char scr[LB * H];
	int trial, x, y;

	/* planar_put_stlow: exactly one pixel changes, to `slot`. */
	for (trial = 0; trial < 5000; trial++) {
		int px = rnd() % W, py = rnd() % H;
		unsigned char sl = (unsigned char)(rnd() & 15);
		memset(scr, 0, sizeof scr);
		planar_put_stlow(scr, LB, NP, (short)px, (short)py, sl);
		for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
			unsigned char exp = (x == px && y == py) ? sl : 0;
			if (slot_at(scr, x, y) != exp) {
				printf("PUT MISMATCH t=%d put(%d,%d)=%d at(%d,%d) got %d exp %d\n",
				       trial, px, py, sl, x, y, slot_at(scr, x, y), exp);
				return 1;
			}
		}
	}

	/* planar_fill_stlow: a fg rect over a bg fill; clipping (negative origin,
	 * oversize extent) must never touch a pixel outside [rx,rx+rw)x[ry,ry+rh). */
	for (trial = 0; trial < 3000; trial++) {
		unsigned char bg = (unsigned char)(rnd() & 15);
		unsigned char fg = (unsigned char)(rnd() & 15);
		int rx = (int)(signed char)rnd() % W;   /* may be negative -> clip */
		int ry = (int)(signed char)rnd() % H;
		int rw = rnd() % (W + 8), rh = rnd() % (H + 4);
		memset(scr, 0, sizeof scr);
		planar_fill_stlow(scr, LB, NP, W, H, 0, 0, W, H, bg);
		planar_fill_stlow(scr, LB, NP, W, H, (short)rx, (short)ry,
		                  (short)rw, (short)rh, fg);
		for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
			int inside = (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
			unsigned char exp = inside ? fg : bg;
			if (slot_at(scr, x, y) != exp) {
				printf("FILL MISMATCH t=%d rect(%d,%d,%d,%d) at(%d,%d) got %d exp %d\n",
				       trial, rx, ry, rw, rh, x, y, slot_at(scr, x, y), exp);
				return 1;
			}
		}
	}
	printf("OK\n");
	return 0;
}
"""


@pytest.mark.skipif(shutil.which("cc") is None, reason="no host C compiler")
def test_planar_fill_matches_decoder(tmp_path):
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
