"""planar_put_stlow (the DrawChar/B4 draw-time plane store) must land the SAME
bytes the backend's batch c2p (c2p4st_32) produces for the same pixels + remap —
that byte-identity is what lets a converted writer stamp planes in parallel with,
and eventually instead of, the chunky+c2p path (ADR-0016 B4). Both encode MSB-first
(c2p4st: bit 15 = leftmost; planar_put_stlow: byte 0 MSB = leftmost), so on the
big-endian 68k they are the same screen image. Here we lay c2p4st_32's words out
big-endian (the 68k memory image) and memcmp against the per-pixel plane store, for
random spans and random 0..15 remaps. Host-compiled like the other c2p/planar tests."""
import os
import shutil
import subprocess

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "c2p4st.h"
#include "planar.h"

#define NP     4
#define GROUPS 4                 /* 64 px = 2 c2p4st_32 calls (32 px each) */
#define W      (GROUPS * 16)     /* 64  */
#define LB     (GROUPS * NP * 2) /* 32 bytes/line, interleaved */

static unsigned s_rng = 987654321u;
static unsigned char rnd(void) { s_rng = s_rng*1103515245u + 12345u; return (unsigned char)(s_rng >> 16); }

int main(void)
{
	unsigned char  lut[256], src[W], a[LB], b[LB];
	unsigned short d[8];
	int trial, i, g;

	for (trial = 0; trial < 5000; trial++) {
		for (i = 0; i < 256; i++) lut[i] = (unsigned char)(rnd() & 15);
		for (i = 0; i < W; i++)   src[i] = rnd();

		/* Path A: the real batch c2p, laid out big-endian = the 68k screen image.
		 * c2p4st_32 converts 32 px -> d[0..3] planes for px 0-15, d[4..7] for
		 * px 16-31; store each plane word high byte then low. */
		memset(a, 0, sizeof a);
		for (g = 0; g < GROUPS / 2; g++) {
			c2p4st_32(src + g * 32, lut, d);
			for (i = 0; i < 8; i++) {
				int gg = g * 2 + (i >= 4 ? 1 : 0);   /* screen 16px group */
				int p  = i & 3;                       /* plane 0..3        */
				unsigned char *dst = a + gg * NP * 2 + p * 2;
				dst[0] = (unsigned char)(d[i] >> 8);
				dst[1] = (unsigned char)(d[i] & 0xff);
			}
		}

		/* Path B: the draw-time plane store, per pixel, slot = lut[src]. */
		memset(b, 0, sizeof b);
		for (i = 0; i < W; i++)
			planar_put_stlow(b, LB, NP, (short)i, 0, lut[src[i]]);

		if (memcmp(a, b, LB) != 0) {
			printf("MISMATCH trial=%d\n", trial);
			for (i = 0; i < LB; i++)
				if (a[i] != b[i])
					printf("  byte %d: c2p %02x planar %02x\n", i, a[i], b[i]);
			return 1;
		}
	}
	printf("OK\n");
	return 0;
}
"""


@pytest.mark.skipif(shutil.which("cc") is None, reason="no host C compiler")
def test_plane_store_agrees_with_c2p(tmp_path):
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
