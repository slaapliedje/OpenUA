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

/* --- banded beats global when regions are vertically stacked --- */
#define BW 64
#define BH 8
static long band_test(int *bad)
{
	/* CLUT: 30 distinct grid colours. Image: top 4 rows use colours 0..14,
	 * bottom 4 use 15..29 — 30 colours total, but only 15 per band. A global
	 * reduce to 16 must merge; a 2-band reduce to 16 fits each band exactly.
	 *
	 * FIFTEEN per band, not sixteen, and that is the point: slot 0 is
	 * reserved for the hardware border in every band (see quant_banded), so
	 * ncol slots reproduce ncol-1 content colours exactly. None of these
	 * random grid colours is the border colour, so each band pays for it. */
	unsigned char clut[768], chunky[BW * BH];
	unsigned char gpal[16 * 3], grem[256];
	unsigned char bpal[2 * 16 * 3], brem[2 * 256];
	short i, x, y;
	long gmse = 0, bmse = 0;

	for (i = 0; i < 30; i++) {
		clut[i*3+0] = (rnd() / 16) * 16 + 8;
		clut[i*3+1] = (rnd() / 16) * 16 + 8;
		clut[i*3+2] = (rnd() / 16) * 16 + 8;
	}
	for (i = 90; i < 768; i++) clut[i] = 0;
	for (y = 0; y < BH; y++)
		for (x = 0; x < BW; x++)
			chunky[y*BW+x] = (unsigned char)((y < BH/2) ? (x % 15)
			                                           : (15 + (x % 15)));

	quant_banded(chunky, BW, BH, clut, 1, 16, 4, gpal, grem, (unsigned char *)0);   /* global   */
	quant_banded(chunky, BW, BH, clut, 2, 16, 4, bpal, brem, (unsigned char *)0);   /* 2 bands  */
	for (y = 0; y < BH; y++) {
		short bb = (short)((long)y * 2 / BH);       /* banded: 2 bands */
		for (x = 0; x < BW; x++) {
			unsigned char v = chunky[y*BW+x];
			unsigned char *gp = gpal + grem[v]*3;
			unsigned char *bp = bpal + (bb*16 + brem[bb*256 + v])*3;
			short dr,dg,db;
			dr=clut[v*3+0]-gp[0]; dg=clut[v*3+1]-gp[1]; db=clut[v*3+2]-gp[2];
			gmse += (long)dr*dr+(long)dg*dg+(long)db*db;
			dr=clut[v*3+0]-bp[0]; dg=clut[v*3+1]-bp[1]; db=clut[v*3+2]-bp[2];
			bmse += (long)dr*dr+(long)dg*dg+(long)db*db;
		}
	}
	if (bmse != 0) { printf("BANDED not exact: bmse=%ld\n", bmse); *bad=1; }
	if (!(bmse < gmse)) { printf("BANDED not better: b=%ld g=%ld\n", bmse, gmse); *bad=1; }
	return gmse;
}

/* --- absent-colour fallback must keep HUE, not just brightness ------------
 *
 * The live bug this pins: FRUA's party-roster CYAN (0,200,200) and mid-grey
 * dungeon stone (150,150,150) have the SAME luminance under the engine's 2:5:1
 * weights (both 150). While quant_banded bucketed the fallback by luma alone,
 * a stone shade that missed the presence histogram — which samples every OTHER
 * row, so this depends on where the pixels landed — fell back to whichever
 * entry was nearest in brightness. When that was the roster cyan, stone walls
 * rendered CYAN, intermittently.
 *
 * Setup: the band's image contains cyan, grey120, grey180 and black, so those
 * four are the reduced palette. Grey150 is in the CLUT but ABSENT from the
 * image, so it takes the fallback path. Its luma distance to cyan is ZERO and
 * to either grey is 30 — a luma bucket MUST choose cyan here. A hue-aware
 * bucket must choose a grey. */
static int hue_fallback_test(void)
{
	unsigned char clut[768], chunky[BW * BH];
	unsigned char bpal[8 * 3], brem[256];
	short i, x, y;
	unsigned char *got;

	for (i = 0; i < 768; i++) clut[i] = 0;
	/* 0 cyan, 1 grey120, 2 grey180, 3 black, 4 = the ABSENT grey150 */
	clut[0*3+0]=0;   clut[0*3+1]=200; clut[0*3+2]=200;
	clut[1*3+0]=120; clut[1*3+1]=120; clut[1*3+2]=120;
	clut[2*3+0]=180; clut[2*3+1]=180; clut[2*3+2]=180;
	clut[3*3+0]=0;   clut[3*3+1]=0;   clut[3*3+2]=0;
	clut[4*3+0]=150; clut[4*3+1]=150; clut[4*3+2]=150;

	for (y = 0; y < BH; y++)
		for (x = 0; x < BW; x++)
			chunky[y*BW+x] = (unsigned char)(x & 3);   /* 0..3 only */

	quant_banded(chunky, BW, BH, clut, 1, 8, 4, bpal, brem, (unsigned char *)0);

	got = bpal + brem[4]*3;
	/* a grey rep has r == g == b on the snapped grid; cyan does not */
	if (got[0] != got[1] || got[1] != got[2]) {
		printf("HUE FALLBACK: absent grey150 fell back to %d,%d,%d "
		       "(luma-only bucketing picks the cyan)\n",
		       got[0], got[1], got[2]);
		return 1;
	}
	return 0;
}

/* --- a flat AREA colour must survive exactly, and identically in every band --
 *
 * This is the seam guarantee. A flat panel spanning a band boundary rendered as
 * two different shades (#40) is what made per-band palettes unusable and drove
 * ADR-0016 B1 to one global palette. The median cut is PRESENCE-weighted — a
 * colour counts once however many pixels it covers — so a panel filling half a
 * band got averaged into a box with its neighbours and came back a different
 * colour, independently in each band.
 *
 * Setup: index 0 covers ~62% of every row; indices 1..40 are scattered spread
 * colours that give the cut plenty to chew on. quant_banded must reserve index
 * 0 an exact slot in BOTH bands, so it round-trips to its snapped CLUT value
 * and is byte-identical either side of the boundary. */
static int flat_area_test(void)
{
	unsigned char clut[768], chunky[BW * BH];
	unsigned char bpal[2 * 4 * 3], brem[2 * 256];
	unsigned char *g0, *g1, want[3];
	short i, x, y, step = 16;

	for (i = 0; i < 768; i++) clut[i] = 0;
	clut[0] = 200; clut[1] = 100; clut[2] = 50;          /* the panel colour */
	/* Band 0's neighbours spread far ABOVE the panel colour, band 1's far
	 * BELOW, and only FOUR slots are available. A cut with no reservation
	 * must put the panel in a box with several of them, and the box average
	 * lands well outside the panel's grid cell — averaged UP in one band and
	 * DOWN in the other. That is the #40 seam, reproduced. */
	for (i = 1; i <= 40; i++) {
		clut[i*3+0] = (unsigned char)(200 + i);
		clut[i*3+1] = (unsigned char)(100 + i * 3);
		clut[i*3+2] = (unsigned char)( 50 + i * 4);
	}
	for (i = 41; i <= 80; i++) {
		short d = (short)(i - 40);
		clut[i*3+0] = (unsigned char)(200 - d * 4);
		clut[i*3+1] = (unsigned char)(100 - d * 2);
		clut[i*3+2] = (unsigned char)( 50 - d);
	}
	for (y = 0; y < BH; y++)
		for (x = 0; x < BW; x++) {
			short lo = (y < BH/2) ? 1 : 41;
			chunky[y*BW+x] = (unsigned char)((x & 1) ? 0
			                 : (lo + ((x/2 + y*32) % 40)));
		}

	quant_banded(chunky, BW, BH, clut, 2, 4, 4, bpal, brem, (unsigned char *)0);

	g0 = bpal + (0*4 + brem[0*256 + 0])*3;
	g1 = bpal + (1*4 + brem[1*256 + 0])*3;
	want[0] = (unsigned char)((200/step)*step + step/2);
	want[1] = (unsigned char)((100/step)*step + step/2);
	want[2] = (unsigned char)(( 50/step)*step + step/2);
	if (g0[0]!=g1[0] || g0[1]!=g1[1] || g0[2]!=g1[2]) {
		printf("SEAM: the flat panel is %d,%d,%d in band 0 but %d,%d,%d in "
		       "band 1\n", g0[0],g0[1],g0[2], g1[0],g1[1],g1[2]);
		return 1;
	}
	if (g0[0]!=want[0] || g0[1]!=want[1] || g0[2]!=want[2]) {
		printf("FLAT AREA: panel renders as %d,%d,%d, want exact %d,%d,%d\n",
		       g0[0],g0[1],g0[2], want[0],want[1],want[2]);
		return 1;
	}
	return 0;
}

/* --- slot 0 is the hardware BORDER and must be identical in every band -----
 *
 * The ST shows colour register 0 in the border, and no pixel index is involved
 * there — so nothing in a band's content constrains it, and bands that
 * disagree stripe the border across the full width of the display, well
 * outside the 320-pixel image. Measured on the HEIRS entry-event screen as a
 * brown bar over engine rows 40-59 and a dark-green one over 60-79.
 *
 * The setup is what makes this bite: the two bands share NO colours, and
 * neither contains black. The old fix picked the darkest slot out of band 0
 * and applied that one swap to every band, which is only sound if slot k means
 * the same colour everywhere — and pass 2 aligns by NEAREST colour, not
 * identity. With disjoint palettes there is nothing near, so band 1's slot
 * `best` held a content colour and went straight to the border. */
static int border_slot_test(void)
{
	unsigned char clut[768], chunky[BW * BH];
	unsigned char bpal[2 * 8 * 3], brem[2 * 256];
	unsigned char *p0, *p1;
	short i, x, y;

	for (i = 0; i < 768; i++) clut[i] = 0;
	/* band 0: warm browns. band 1: cool greens. No black, no overlap. */
	for (i = 0; i < 8; i++) {
		clut[i*3+0] = (unsigned char)(120 + i * 8);
		clut[i*3+1] = (unsigned char)( 70 + i * 4);
		clut[i*3+2] = (unsigned char)( 40 + i * 2);
	}
	for (i = 8; i < 16; i++) {
		clut[i*3+0] = (unsigned char)( 40 + (i-8) * 2);
		clut[i*3+1] = (unsigned char)(110 + (i-8) * 8);
		clut[i*3+2] = (unsigned char)( 60 + (i-8) * 4);
	}
	for (y = 0; y < BH; y++)
		for (x = 0; x < BW; x++)
			chunky[y*BW+x] = (unsigned char)((y < BH/2) ? (x & 7)
			                                           : (8 + (x & 7)));

	quant_banded(chunky, BW, BH, clut, 2, 8, 4, bpal, brem, (unsigned char *)0);

	p0 = bpal + 0 * 8 * 3;
	p1 = bpal + 1 * 8 * 3;
	if (p0[0] != p1[0] || p0[1] != p1[1] || p0[2] != p1[2]) {
		printf("BORDER: slot 0 is %d,%d,%d in band 0 but %d,%d,%d in band 1"
		       " — the border stripes\n",
		       p0[0],p0[1],p0[2], p1[0],p1[1],p1[2]);
		return 1;
	}
	if (p0[0] != quant_snap(0, 4) || p0[1] != quant_snap(0, 4)
	 || p0[2] != quant_snap(0, 4)) {
		printf("BORDER: slot 0 is %d,%d,%d, want the snapped black %d\n",
		       p0[0],p0[1],p0[2], quant_snap(0, 4));
		return 1;
	}
	/* And nothing may be MAPPED there — the content is disjoint from black,
	 * so a remap landing on slot 0 would render a content colour as border. */
	for (i = 0; i < 16; i++)
		if (brem[(i < 8 ? 0 : 256) + i] == 0) {
			printf("BORDER: content index %d maps to the border slot\n", i);
			return 1;
		}
	return 0;
}

/* --- the dominance trigger: a band goes stale when POPULATION moves ---------
 *
 * Presence is not enough, and this pins the case that proved it. On the credits
 * -> main-menu transition the menu's backdrop index was ALREADY present in
 * every band (a handful of pixels), so a presence test saw no new ink and never
 * re-banded; the menu rendered on the credits screen's palettes. What changed
 * was the index's POPULATION.
 *
 * Frames here are 200x20, one band, sampled every other row => 2000 counted
 * pixels, so 3% (enter) is 60 and 2% (leave) is 40 — fine enough to sit a
 * colour deliberately between the two bars and check the hysteresis.
 *
 * band_used levels: 0 absent, 1 rare, 2 present in quantity, 3 dominant.
 */
#define DW 200
#define DH 20

static void dom_frame(unsigned char *f, unsigned char bg, unsigned char fg,
                      short npx)
{
	short i;

	for (i = 0; i < DW * DH; i++) f[i] = bg;
	for (i = 0; i < npx; i++) f[i] = fg;      /* row 0: a SAMPLED row */
}

static int dominance_test(void)
{
	unsigned char clut[768], f[DW * DH];
	unsigned char pal[16 * 3], rem[256], used[256];
	short i;

	for (i = 0; i < 256; i++) {
		clut[i*3+0] = (unsigned char)(i);
		clut[i*3+1] = (unsigned char)(255 - i);
		clut[i*3+2] = (unsigned char)((i * 7) & 0xff);
	}

	/* A: index 5 is the backdrop; index 9 is PRESENT on 50 of 2000 sampled
	 * pixels — 2.5%, under the 3% bar, exactly the shape that fooled the
	 * presence test. */
	dom_frame(f, 5, 9, 50);
	quant_banded(f, DW, DH, clut, 1, 16, 4, pal, rem, used);
	if (used[5] != 3 || used[9] < 1 || used[9] > 2) {
		printf("DOM: classes wrong, used[5]=%d used[9]=%d "
		       "(want 3 = dominant, 1..2 = present-not-dominant)\n",
		       used[5], used[9]);
		return 1;
	}
	if (quant_band_dominant_moved(f, DW, DH, 1, 0, used)) {
		printf("DOM: a frame is stale against its OWN quant\n");
		return 1;
	}

	/* B: THE BUG. The two swap roles. Every index is still present in both
	 * frames — only the populations moved — so this is precisely what a
	 * presence test cannot see. */
	dom_frame(f, 9, 5, 50);
	if (!quant_band_dominant_moved(f, DW, DH, 1, 0, used)) {
		printf("DOM: a swapped backdrop did not read as stale\n");
		return 1;
	}

	/* C: index 9 at 58/2000 = 2.9%, just under the entry bar. A minor
	 * colour drifting below 3% must not buy a re-band. */
	dom_frame(f, 5, 9, 58);
	if (quant_band_dominant_moved(f, DW, DH, 1, 0, used)) {
		printf("DOM: 2.9%% tripped the 3%% entry bar\n");
		return 1;
	}
	/* ...and at 60/2000 = 3.0% it does. */
	dom_frame(f, 5, 9, 60);
	if (!quant_band_dominant_moved(f, DW, DH, 1, 0, used)) {
		printf("DOM: 3.0%% did not reach the entry bar\n");
		return 1;
	}

	/* HYSTERESIS. Re-quant with index 9 dominant, then shrink it to 2.25% —
	 * below the 3% it entered at but above the 2% it leaves at. Without the
	 * gap a colour parked between the bars re-bands on every frame that
	 * nudges it, and every one of those re-bands is invisible. */
	dom_frame(f, 5, 9, 60);
	quant_banded(f, DW, DH, clut, 1, 16, 4, pal, rem, used);
	if (used[9] != 3) {
		printf("DOM: 3.0%% did not earn the dominant class (got %d)\n",
		       used[9]);
		return 1;
	}
	dom_frame(f, 5, 9, 45);
	if (quant_band_dominant_moved(f, DW, DH, 1, 0, used)) {
		printf("DOM: 2.25%% fell out of the set — no hysteresis\n");
		return 1;
	}
	/* Below the lower bar it really is gone. */
	dom_frame(f, 5, 9, 30);
	if (!quant_band_dominant_moved(f, DW, DH, 1, 0, used)) {
		printf("DOM: 1.5%% stayed in the set — the exit bar never fires\n");
		return 1;
	}
	return 0;
}

int main(void)
{
	int bad = 0;
	long e8, e16, e32, gmse;

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
	s_rng = 4242u; gmse = band_test(&bad);
	if (bad) return 1;
	if (hue_fallback_test()) return 1;
	if (flat_area_test()) return 1;
	if (border_slot_test()) return 1;
	if (dominance_test()) return 1;
	printf("OK  mse(8)=%ld mse(16)=%ld mse(32)=%ld  band-global-mse=%ld\n",
	       e8, e16, e32, gmse);
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
		 "-I", os.path.join(REPO, "platform", "include"),
		 "-I", os.path.join(REPO, "third_party", "c2p-68k", "include")],
		check=True, capture_output=True, text=True)
	out = subprocess.run([str(exe)], check=True,
	                     capture_output=True, text=True).stdout
	assert "OK" in out, out
