"""c2p_amiga transpose correctness: the masked-swap bit-matrix transpose in
platform/amiga/c2p_amiga.c must be bit-identical to the naive per-pixel
scatter it replaced (the naive form is the reference inside the harness),
for the full frame AND for an irregular tiling of aligned rects.

The C harness compiles with the HOST compiler — the transpose is portable C
by design (68000-clean for the ECS ladder), so host-green means Amiga-green
modulo compiler bugs (see the Bebbo -fbbb note in toolchain/m68k-amigaos.mk).
"""
import os
import shutil
import subprocess

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HARNESS = r"""
#include <stdio.h>
#include <string.h>

void c2p_amiga(const unsigned char *chunky, unsigned char *const planes[8],
               short w, short h, short plane_pitch);
void c2p_amiga_rect(const unsigned char *chunky, short chunky_pitch,
                    unsigned char *const planes[8], short plane_pitch,
                    short x0, short y0, short w, short h);

#define W 320
#define H 200
#define PITCH (W/8)

static void naive(const unsigned char *chunky, unsigned char *const planes[8],
                  short w, short h, short plane_pitch)
{
	short y, k; long x;
	for (y = 0; y < h; y++) {
		const unsigned char *src = chunky + (long)y * w;
		long rowoff = (long)y * plane_pitch;
		for (x = 0; x < w; x += 8) {
			unsigned char pb[8] = {0};
			for (k = 0; k < 8; k++) {
				unsigned char px = src[x+k], bit = 0x80u >> k;
				short p;
				for (p = 0; p < 8; p++)
					if (px & (1u<<p)) pb[p] |= bit;
			}
			for (k = 0; k < 8; k++)
				planes[k][rowoff + (x>>3)] = pb[k];
		}
	}
}

int main(void)
{
	static unsigned char chunky[W*H];
	static unsigned char ref[8][PITCH*H], fast[8][PITCH*H], part[8][PITCH*H];
	unsigned char *rp[8], *fp[8], *pp[8];
	int i, p;
	unsigned s = 12345;

	for (i = 0; i < W*H; i++) { s = s*1103515245u+12345u; chunky[i] = (unsigned char)(s>>16); }
	for (p = 0; p < 8; p++) { rp[p]=ref[p]; fp[p]=fast[p]; pp[p]=part[p]; }

	naive(chunky, rp, W, H, PITCH);
	c2p_amiga(chunky, fp, W, H, PITCH);
	for (p = 0; p < 8; p++)
		if (memcmp(ref[p], fast[p], PITCH*H)) { printf("FULL MISMATCH plane %d\n", p); return 1; }

	c2p_amiga_rect(chunky, W, pp, PITCH, 0,   0,   64,  50);
	c2p_amiga_rect(chunky, W, pp, PITCH, 64,  0,   256, 50);
	c2p_amiga_rect(chunky, W, pp, PITCH, 0,   50,  320, 3);
	c2p_amiga_rect(chunky, W, pp, PITCH, 0,   53,  8,   147);
	c2p_amiga_rect(chunky, W, pp, PITCH, 8,   53,  16,  147);
	c2p_amiga_rect(chunky, W, pp, PITCH, 24,  53,  296, 147);
	for (p = 0; p < 8; p++)
		if (memcmp(ref[p], part[p], PITCH*H)) { printf("RECT MISMATCH plane %d\n", p); return 1; }
	printf("OK\n");
	return 0;
}
"""


@pytest.mark.skipif(shutil.which("cc") is None, reason="no host C compiler")
def test_c2p_transpose_matches_naive_scatter(tmp_path):
    harness = tmp_path / "harness.c"
    harness.write_text(HARNESS)
    exe = tmp_path / "c2p_test"
    subprocess.run(
        ["cc", "-O2", "-o", str(exe), str(harness),
         os.path.join(REPO, "platform", "amiga", "c2p_amiga.c"),
         "-I", os.path.join(REPO, "platform", "include")],
        check=True, capture_output=True, text=True)
    out = subprocess.run([str(exe)], check=True,
                         capture_output=True, text=True).stdout
    assert "OK" in out
