"""planar_put_stlow / planar_fill_stlow / planar_glyph_stlow / planar_c2p_span_stlow — the draw-time
plane-store primitives (ADR-0016 draw-time present model) must set exactly the
addressed pixels' slot bits in ST-Low interleaved planes and leave every other
pixel untouched, for random pixels, random rects, clipped rects, and 1bpp glyphs
in both opaque (srcCopy) and transparent (srcOr) modes. Host-compiled like the
other c2p/planar tests; verified against an independent bit decoder."""
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
	/* planar_put_amiga: separate-plane layout — exactly one pixel changes. */
	for (trial = 0; trial < 3000; trial++) {
		unsigned char ascr[NP * (W / 8) * H];
		int px = rnd() % W, py = rnd() % H, p;
		unsigned char sl = (unsigned char)(rnd() & 15);
		memset(ascr, 0, sizeof ascr);
		planar_put_amiga(ascr, W / 8, (long)(W / 8) * H, NP,
		                 (short)px, (short)py, sl);
		for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
			unsigned char got = 0, exp;
			for (p = 0; p < NP; p++)
				if (ascr[(long)p * (W / 8) * H + (long)y * (W / 8)
				         + (x >> 3)] & (0x80 >> (x & 7)))
					got |= (unsigned char)(1 << p);
			exp = (x == px && y == py) ? sl : 0;
			if (got != exp) {
				printf("AMIGA MISMATCH t=%d at(%d,%d) got %d exp %d\n",
				       trial, x, y, got, exp);
				return 1;
			}
		}
	}

	/* planar_glyph_stlow: a 1bpp glyph over a bg fill. Set bits -> fg; clear
	 * bits -> bg when opaque, else the bg fill shows through. Clipping (negative
	 * origin, oversize glyph) must never touch a pixel outside the screen. */
	for (trial = 0; trial < 4000; trial++) {
		unsigned char bg  = (unsigned char)(rnd() & 15);
		unsigned char fg  = (unsigned char)(rnd() & 15);
		unsigned char sfil = (unsigned char)(rnd() & 15);   /* prior surface fill */
		int opaque = rnd() & 1;
		int gw = 1 + rnd() % 20, gh = 1 + rnd() % 12;       /* glyph up to 20x12 */
		int gstride = (gw + 7) / 8;
		int gx = (int)(signed char)rnd() % W;               /* may be negative   */
		int gy = (int)(signed char)rnd() % H;
		unsigned char gbits[3 * 12];                        /* gstride<=3, gh<=12 */
		int r, c;
		for (r = 0; r < gstride * gh; r++)
			gbits[r] = rnd();
		memset(scr, 0, sizeof scr);
		planar_fill_stlow(scr, LB, NP, W, H, 0, 0, W, H, sfil);
		planar_glyph_stlow(scr, LB, NP, W, H, gbits, (short)gstride,
		                   (short)gx, (short)gy, (short)gw, (short)gh,
		                   fg, bg, (short)opaque);
		for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
			unsigned char exp = sfil;
			r = y - gy; c = x - gx;
			if (r >= 0 && r < gh && c >= 0 && c < gw) {
				int bit = gbits[r * gstride + (c >> 3)] & (0x80 >> (c & 7));
				if (bit)          exp = fg;
				else if (opaque)  exp = bg;
			}
			if (slot_at(scr, x, y) != exp) {
				printf("GLYPH MISMATCH t=%d g(%d,%d,%d,%d) op=%d at(%d,%d) got %d exp %d\n",
				       trial, gx, gy, gw, gh, opaque, x, y, slot_at(scr, x, y), exp);
				return 1;
			}
		}
	}
	/* planar_c2p_span_stlow: a chunky row through a random LUT must produce
	 * EXACTLY what a per-pixel planar_put_stlow loop produces, for arbitrary
	 * (unaligned, sub-group, multi-group) spans — and must not disturb one
	 * pixel outside [x0, x1). Random background first so "untouched" is a
	 * real assertion and not "still zero". */
	for (trial = 0; trial < 3000; trial++) {
		unsigned char chunky[W], lut[256], ref[LB * H];
		int x0 = rnd() % W, x1 = rnd() % W, y = rnd() % H;
		int i;

		if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
		for (i = 0; i < W; i++)   chunky[i] = rnd();
		for (i = 0; i < 256; i++) lut[i] = (unsigned char)(rnd() & 15);
		for (i = 0; i < (int)sizeof scr; i++) scr[i] = rnd();
		memcpy(ref, scr, sizeof ref);
		/* reference: the per-pixel form this replaces */
		for (i = x0; i < x1; i++)
			planar_put_stlow(ref, LB, NP, (short)i, (short)y,
			                 lut[chunky[i]]);
		planar_c2p_span_stlow(scr, LB, NP, (short)y, (short)x0, (short)x1,
		                      chunky, lut);
		if (memcmp(scr, ref, sizeof ref) != 0) {
			for (y = 0; y < H; y++) for (x = 0; x < W; x++)
				if (slot_at(scr, x, y) != slot_at(ref, x, y)) {
					printf("C2P MISMATCH t=%d span[%d,%d) y=%d at(%d,%d) got %d exp %d\n",
					       trial, x0, x1, y, x, y,
					       slot_at(scr, x, y), slot_at(ref, x, y));
					return 1;
				}
			printf("C2P MISMATCH t=%d span[%d,%d) (bytes differ outside decoded pixels)\n",
			       trial, x0, x1);
			return 1;
		}
	}

	/* planar_span_amiga / planar_c2p_span_amiga: the separate-plane siblings.
	 * Same contract as the ST forms — must equal a per-pixel planar_put_amiga
	 * reference for arbitrary spans and disturb nothing outside [x0, x1).
	 * Randomised background so "untouched" is a real assertion. */
	for (trial = 0; trial < 3000; trial++) {
		unsigned char ascr[NP * (W / 8) * H], aref[NP * (W / 8) * H];
		unsigned char chunky[W], lut[256];
		int x0 = rnd() % W, x1 = rnd() % W, y = rnd() % H;
		unsigned char sl = (unsigned char)(rnd() & 15);
		int i, solid = (trial & 1);

		if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
		for (i = 0; i < W; i++)   chunky[i] = rnd();
		for (i = 0; i < 256; i++) lut[i] = (unsigned char)(rnd() & 15);
		for (i = 0; i < (int)sizeof ascr; i++) ascr[i] = rnd();
		memcpy(aref, ascr, sizeof aref);
		for (i = x0; i < x1; i++)
			planar_put_amiga(aref, W / 8, (long)(W / 8) * H, NP,
			                 (short)i, (short)y,
			                 solid ? sl : lut[chunky[i]]);
		if (solid)
			planar_span_amiga(ascr, W / 8, (long)(W / 8) * H, NP,
			                  (short)y, (short)x0, (short)x1, sl);
		else
			planar_c2p_span_amiga(ascr, W / 8, (long)(W / 8) * H, NP,
			                      (short)y, (short)x0, (short)x1,
			                      chunky, lut);
		if (memcmp(ascr, aref, sizeof aref) != 0) {
			printf("AMIGA SPAN MISMATCH t=%d %s span[%d,%d) y=%d\n",
			       trial, solid ? "solid" : "c2p", x0, x1, y);
			return 1;
		}
	}

	/* #129: the same Amiga spans at 5 and 8 PLANES. The block above runs at
	 * NP=4, which never exercised the plane counts the Amiga actually ships
	 * (ECS 5, AGA 8) — and the 32-pixel transpose fast path stores per plane,
	 * so an 8-plane bug would have been invisible. Buffers are sized for 8. */
	for (trial = 0; trial < 4000; trial++) {
		unsigned char ascr[8 * (W / 8) * H], aref[8 * (W / 8) * H];
		unsigned char chunky[W], lut[256];
		int np = (trial % 3 == 0) ? 4 : ((trial % 3 == 1) ? 5 : 8);
		int x0 = rnd() % W, x1 = rnd() % W, y = rnd() % H;
		unsigned char sl = (unsigned char)(rnd() & 0xFF);
		int i, solid = (trial & 1);
		long pb = (long)(W / 8) * H;

		if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
		for (i = 0; i < W; i++)   chunky[i] = rnd();
		for (i = 0; i < 256; i++) lut[i] = rnd();
		for (i = 0; i < (int)sizeof ascr; i++) ascr[i] = rnd();
		memcpy(aref, ascr, sizeof aref);
		for (i = x0; i < x1; i++)
			planar_put_amiga(aref, W / 8, pb, (short)np,
			                 (short)i, (short)y,
			                 solid ? sl : lut[chunky[i]]);
		if (solid)
			planar_span_amiga(ascr, W / 8, pb, (short)np,
			                  (short)y, (short)x0, (short)x1, sl);
		else
			planar_c2p_span_amiga(ascr, W / 8, pb, (short)np,
			                      (short)y, (short)x0, (short)x1,
			                      chunky, lut);
		if (memcmp(ascr, aref, sizeof aref) != 0) {
			printf("AMIGA %dP MISMATCH t=%d %s span[%d,%d) y=%d\n",
			       np, trial, solid ? "solid" : "c2p", x0, x1, y);
			return 1;
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
		 "-I", os.path.join(REPO, "platform", "include"),
		 "-I", os.path.join(REPO, "third_party", "c2p-68k", "include")],
		check=True, capture_output=True, text=True)
	out = subprocess.run([str(exe)], check=True,
	                     capture_output=True, text=True).stdout
	assert "OK" in out, out
