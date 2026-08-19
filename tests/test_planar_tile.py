"""planar_tile.h — a tile converted to planes+mask and blitted word-wise must land
EXACTLY the pixels the per-pixel masked blit would have landed.

That equivalence is the whole basis of #144's tile cache: it replaces l309c_tile's
per-pixel scatter (a 255 key plus per-set magenta keys, one store per surviving
pixel) with a word-wise plane merge. If the two disagree anywhere the cache silently
draws wrong walls, which no screenshot check would reliably catch.

The reference here is deliberately dumb: for every pixel, decide transparency and
slot from the same LUT, then set/clear the four plane bits at that exact screen
position. Random tiles, random LUTs (including transparent entries), and — the case
that actually breaks shifted planar blitters — every sub-word x offset 0..15, plus
clip rectangles that cut the tile on all four sides.

Host-compiled like the c2p and quantize tests.
"""
import os
import shutil
import subprocess

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "planar_tile.h"

#define W_MAX 40
#define H_MAX 24
#define PAGE_W 320
#define LINE_BYTES (PAGE_W / 2)          /* ST-Low: 4 planes, 8 bytes / 16px */
#define PAGE_H 64

static unsigned s_rng = 20260818u;
static unsigned char rnd(void)
{
	s_rng = s_rng * 1103515245u + 12345u;
	return (unsigned char)(s_rng >> 16);
}

static unsigned char page_a[LINE_BYTES * PAGE_H];
static unsigned char page_b[LINE_BYTES * PAGE_H];

/* Naive: per pixel, exactly what the chunky blit decides, scattered into planes. */
static void naive_blit(unsigned char *page, const unsigned char *body,
                       short w, short h, const unsigned short *lut,
                       short x, short y,
                       short cl, short cr, short ct, short cb)
{
	short r, c, p;

	for (r = 0; r < h; r++) {
		short dy = (short)(y + r);
		if (dy < ct || dy >= cb) continue;
		for (c = 0; c < w; c++) {
			short dx = (short)(x + c);
			unsigned short t = lut[body[(long)r * w + c]];
			unsigned char *grp;
			unsigned short bit;

			if (!t) continue;                    /* transparent */
			if (dx < cl || dx >= cr) continue;   /* clipped */
			grp = page + (long)dy * LINE_BYTES + (long)(dx >> 4) * 8;
			bit = (unsigned short)(0x8000u >> (dx & 15));
			for (p = 0; p < 4; p++) {
				unsigned short *pl = (unsigned short *)(grp + p * 2);
				if (t & (1u << p)) *pl |= bit;
				else               *pl &= (unsigned short)~bit;
			}
		}
	}
}

int main(void)
{
	unsigned char body[W_MAX * H_MAX];
	unsigned short lut[256];
	unsigned short tile[PT_WORDS_FOR(W_MAX, H_MAX)];
	int trial, bad = 0, blitted = 0;

	for (trial = 0; trial < 400; trial++) {
		short w = (short)(8 + (rnd() % (W_MAX - 8 + 1)));
		short h = (short)(1 + (rnd() % H_MAX));
		short x = (short)(rnd() % 200);
		short y = (short)(rnd() % 32);
		short cl, cr, ct, cb;
		int i;

		/* every sub-word offset gets exercised */
		x = (short)((x & ~15) | (trial & 15));

		for (i = 0; i < 256; i++) {
			unsigned char k = rnd();
			/* ~25% transparent, else a random 4-bit slot */
			lut[i] = (k < 64) ? 0
			                  : (unsigned short)(0x100u | (k & 15));
		}
		for (i = 0; i < w * h; i++)
			body[i] = rnd();

		cl = (short)(rnd() % 64);
		cr = (short)(cl + 16 + (rnd() % 200));
		ct = (short)(rnd() % 8);
		cb = (short)(ct + 1 + (rnd() % 40));
		if (cr > PAGE_W) cr = PAGE_W;
		if (cb > PAGE_H) cb = PAGE_H;

        /* identical, non-zero starting contents: the merge must PRESERVE
         * whatever the mask does not cover */
		for (i = 0; i < (int)sizeof page_a; i++)
			page_a[i] = page_b[i] = rnd();

		planar_tile_build(tile, body, w, h, lut);
		planar_tile_blit(page_a, LINE_BYTES, tile, w, h, x, y,
		                 cl, cr, ct, cb);
		naive_blit(page_b, body, w, h, lut, x, y, cl, cr, ct, cb);
		blitted++;

		if (memcmp(page_a, page_b, sizeof page_a) != 0) {
			if (++bad <= 3) {
				int j;
				for (j = 0; j < (int)sizeof page_a; j++)
					if (page_a[j] != page_b[j]) {
						printf("MISMATCH trial=%d w=%d h=%d x=%d y=%d "
						       "clip=%d,%d,%d,%d byte=%d got=%02x want=%02x\n",
						       trial, w, h, x, y, cl, cr, ct, cb,
						       j, page_a[j], page_b[j]);
						break;
					}
			}
		}
	}
	printf("trials=%d mismatches=%d\n", blitted, bad);
	return bad ? 1 : 0;
}
"""


@pytest.mark.skipif(shutil.which("cc") is None, reason="no host C compiler")
def test_planar_tile_matches_naive(tmp_path):
    src = tmp_path / "h.c"
    src.write_text(HARNESS)
    exe = tmp_path / "h"
    subprocess.run(
        ["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-O2",
         "-I", os.path.join(REPO, "platform/include"),
         str(src), "-o", str(exe)],
        check=True,
    )
    out = subprocess.run([str(exe)], capture_output=True, text=True)
    assert out.returncode == 0, out.stdout + out.stderr
    assert "mismatches=0" in out.stdout, out.stdout
