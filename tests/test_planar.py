"""Native-planar substrate (ADR-0016 phase 1): platform/planar.c.

chunky_to_planar_piece() must convert an 8bpp indexed piece into MSB-first
bitplanes + a 1-bit transparency mask, applying a 256->N remap once; and
planar_blit_cpu() must cookie-cut a piece into a planar destination, leaving
dst untouched where the piece is transparent, with clipping.

The C harness compiles with the HOST compiler — planar.c is portable,
68000-clean C by design (same policy as tests/test_c2p_amiga.py), so
host-green means ST/Amiga-green modulo compiler bugs. The reference here is a
naive per-pixel converter/reader, and a round-trip that reconstructs each
pixel's index from the planes.
"""
import os
import shutil
import subprocess

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "planar.h"

/* ---- test scene: a 13x7 piece (odd width forces word padding) ---- */
#define W 13
#define H 7
#define NP 5                     /* 5 planes = ECS */
#define TRANSIDX 255             /* the global transparency key */

int main(void)
{
	static unsigned char chunky[W * H];
	unsigned char remap[256], trans[256];
	static unsigned char planebuf[PLANAR_STRIDE(W) * H * NP];
	static unsigned char maskbuf[PLANAR_STRIDE(W) * H];
	planar_piece_t pc;
	int x, y, p, fails = 0;

	/* remap: identity low 5 bits (index -> 0..31). trans: only 255. */
	for (x = 0; x < 256; x++) { remap[x] = (unsigned char)(x & 31); trans[x] = 0; }
	trans[TRANSIDX] = 1;

	/* fill: a gradient, with a transparent hole in the middle column */
	for (y = 0; y < H; y++)
		for (x = 0; x < W; x++)
			chunky[y * W + x] =
			    (x == 6) ? TRANSIDX : (unsigned char)((x * 3 + y * 7) & 0xff);

	pc.w = W; pc.h = H; pc.nplanes = NP;
	pc.planes = planebuf; pc.mask = maskbuf;
	chunky_to_planar_piece(chunky, W, W, H, remap, trans, &pc);

	if (pc.stride != PLANAR_STRIDE(W)) {
		printf("FAIL stride %d != %d\n", pc.stride, PLANAR_STRIDE(W));
		fails++;
	}

	/* (A) convert correctness: reconstruct index + mask from the planes. */
	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) {
			int byte = x >> 3, bit = 0x80 >> (x & 7);
			long roff = (long)y * pc.stride;
			int mbit = (pc.mask[roff + byte] & bit) ? 1 : 0;
			int want_trans = (chunky[y * W + x] == TRANSIDX);
			int idx = 0;

			if (mbit == want_trans) {
				printf("FAIL mask (%d,%d) got %d\n", x, y, mbit);
				fails++;
			}
			if (want_trans) continue;
			for (p = 0; p < NP; p++)
				if (pc.planes[(long)p * pc.stride * H + roff + byte] & bit)
					idx |= (1 << p);
			if (idx != (remap[chunky[y * W + x]])) {
				printf("FAIL idx (%d,%d) got %d want %d\n",
				       x, y, idx, remap[chunky[y * W + x]]);
				fails++;
			}
		}
	}

	/* padding bits (x = W..stride*8-1) must be zero in planes AND mask */
	for (y = 0; y < H; y++) {
		for (x = W; x < pc.stride * 8; x++) {
			int byte = x >> 3, bit = 0x80 >> (x & 7);
			long roff = (long)y * pc.stride;
			if (pc.mask[roff + byte] & bit) { printf("FAIL pad mask\n"); fails++; }
			for (p = 0; p < NP; p++)
				if (pc.planes[(long)p * pc.stride * H + roff + byte] & bit) {
					printf("FAIL pad plane\n"); fails++;
				}
		}
	}

	/* (B) masked blit round-trip: paint a nonzero background, blit the piece
	 * at an UNALIGNED x, then read back — opaque pixels take the piece index,
	 * transparent pixels keep the background. */
	{
		#define DW 40
		#define DH 20
		#define DSTRIDE (DW / 8)
		#define BX 5      /* unaligned on purpose (5 & 7 != 0) */
		#define BY 3
		static unsigned char dp[NP][DSTRIDE * DH];
		unsigned char *planes[NP];
		int bg = 21;      /* background index (0..31) */

		for (p = 0; p < NP; p++) {
			planes[p] = dp[p];
			/* seed every dst pixel to bg */
			for (y = 0; y < DH; y++)
				for (x = 0; x < DW; x++) {
					int byte = x >> 3, bit = 0x80 >> (x & 7);
					if ((bg >> p) & 1) dp[p][y * DSTRIDE + byte] |= bit;
					else               dp[p][y * DSTRIDE + byte] &= ~bit;
				}
		}

		planar_blit_cpu(&pc, planes, DSTRIDE, DW, DH, BX, BY);

		for (y = 0; y < DH; y++) {
			for (x = 0; x < DW; x++) {
				int byte = x >> 3, bit = 0x80 >> (x & 7);
				int idx = 0, inpiece, px = x - BX, py = y - BY, want;
				for (p = 0; p < NP; p++)
					if (dp[p][y * DSTRIDE + byte] & bit) idx |= (1 << p);
				inpiece = (px >= 0 && px < W && py >= 0 && py < H);
				if (inpiece && chunky[py * W + px] != TRANSIDX)
					want = remap[chunky[py * W + px]];
				else
					want = bg;
				if (idx != want) {
					printf("FAIL blit (%d,%d) got %d want %d\n",
					       x, y, idx, want);
					fails++;
				}
			}
		}
	}

	if (fails == 0) printf("OK\n");
	return fails ? 1 : 0;
}
"""


@pytest.mark.skipif(shutil.which("cc") is None and shutil.which("gcc") is None,
                    reason="no host C compiler")
def test_planar_convert_and_blit(tmp_path):
    cc = shutil.which("cc") or shutil.which("gcc")
    src = tmp_path / "h.c"
    src.write_text(HARNESS)
    exe = tmp_path / "h"
    subprocess.run(
        [cc, "-O2", "-Wall", "-Wextra", "-Werror",
         "-I", os.path.join(REPO, "platform", "include"),
         str(src), os.path.join(REPO, "platform", "planar.c"),
         "-o", str(exe)],
        check=True)
    out = subprocess.run([str(exe)], capture_output=True, text=True)
    assert out.returncode == 0, out.stdout + out.stderr
    assert out.stdout.strip().endswith("OK"), out.stdout
