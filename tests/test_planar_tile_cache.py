"""planar_tile_cache.h — the cache must never hand back pixels that disagree with
a fresh conversion, and it must survive the two events that can invalidate an entry.

A cache that returns a hit is asserting "these planes are what planar_tile_build
would produce right now". Two things can falsify that:

  * a RE-BAND, which rebuilds every band palette, so a tile converted earlier may
    be stale even though every band agreed when it was built (the measured
    agreement is spatial, not temporal) — modelled here as a new epoch;
  * a WALL-SET change, where the same idx means different ART and no epoch can
    express it — the caller must flush.

So this drives the cache through both and compares every hit against a freshly
built tile, byte for byte. Host-compiled like the other c2p/quantize tests.
"""
import os
import shutil
import subprocess

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "planar_tile_cache.h"

#define W 24
#define H 12
#define NTILE 40
#define POOL_WORDS (PT_WORDS_FOR(W, H) * NTILE)

static unsigned s_rng = 4242u;
static unsigned char rnd(void) { s_rng = s_rng*1103515245u + 12345u; return (unsigned char)(s_rng >> 16); }

static unsigned short pool[POOL_WORDS];
static pt_entry_t     ents[NTILE];
static unsigned char  bodies[NTILE][W * H];
static unsigned short lut[256];
static unsigned short fresh[PT_WORDS_FOR(W, H)];

static int bad;

/* every hit must equal a fresh conversion of the same body with the same lut */
static void check(pt_cache_t *c, short slot, short idx, int t, unsigned long ep)
{
	unsigned short *got = pt_cache_get(c, slot, idx, ep);

	if (!got) return;                       /* a miss is legal, staleness included */
	planar_tile_build(fresh, bodies[t], W, H, lut);
	if (memcmp(got, fresh, sizeof fresh) != 0) {
		if (++bad <= 3)
			printf("STALE HIT slot=%d idx=%d epoch=%lu\n",
			       slot, idx, ep);
	}
}

static void fill_lut(void)
{
	int i;
	for (i = 0; i < 256; i++) {
		unsigned char k = rnd();
		lut[i] = (k < 64) ? 0 : (unsigned short)(0x100u | (k & 15));
	}
}

int main(void)
{
	pt_cache_t c;
	int t, round;
	unsigned long epoch = 1;

	pt_cache_init(&c, pool, POOL_WORDS, ents, NTILE);
	fill_lut();
	for (t = 0; t < NTILE; t++) {
		int i;
		for (i = 0; i < W * H; i++) bodies[t][i] = rnd();
	}

	/* warm: every tile converted once */
	for (t = 0; t < NTILE; t++) {
		unsigned short *e = pt_cache_reserve(&c, (short)(t % 3),
		                                     (short)t, W, H, epoch);
		if (!e) { printf("UNEXPECTED FULL at %d\n", t); return 1; }
		planar_tile_build(e, bodies[t], W, H, lut);
	}
	if (c.miss != NTILE) { printf("miss=%lu want %d\n", c.miss, NTILE); return 1; }

	/* steady state: all hits, all correct */
	for (round = 0; round < 3; round++)
		for (t = 0; t < NTILE; t++)
			check(&c, (short)(t % 3), (short)t, t, epoch);
	if (c.hit == 0) { printf("no hits at all\n"); return 1; }

	/* a RE-BAND: new epoch + a different remap. Every entry must miss, and the
	 * rebuild must reuse its slab rather than exhausting the pool. */
	{
		long used_before = c.pool_used;
		unsigned long reb_before;

		epoch++;
		fill_lut();
		for (t = 0; t < NTILE; t++)
			if (pt_cache_get(&c, (short)(t % 3), (short)t, epoch)) {
				printf("STALE ENTRY SURVIVED A RE-BAND t=%d\n", t);
				return 1;
			}
		reb_before = c.rebuilt;
		for (t = 0; t < NTILE; t++) {
			unsigned short *e = pt_cache_reserve(&c, (short)(t % 3),
			                                     (short)t, W, H, epoch);
			if (!e) { printf("FULL on rebuild at %d\n", t); return 1; }
			planar_tile_build(e, bodies[t], W, H, lut);
		}
		if (c.pool_used != used_before) {
			printf("pool GREW on rebuild: %ld -> %ld\n",
			       used_before, c.pool_used);
			return 1;
		}
		if (c.rebuilt - reb_before != NTILE) {
			printf("rebuilt=%lu want %d\n", c.rebuilt - reb_before, NTILE);
			return 1;
		}
		for (t = 0; t < NTILE; t++)
			check(&c, (short)(t % 3), (short)t, t, epoch);
	}

	/* a WALL-SET change: same idx, different art. Without a flush the cache
	 * would hand back the old tile, so prove the flush is what saves it. */
	{
		int i;
		for (i = 0; i < W * H; i++) bodies[0][i] = (unsigned char)(rnd() ^ 0x5a);
		if (!pt_cache_get(&c, 0, 0, epoch)) {
			printf("expected a hit before the flush\n"); return 1;
		}
		pt_cache_flush(&c);
		if (pt_cache_get(&c, 0, 0, epoch)) {
			printf("FLUSH DID NOT DROP THE ENTRY\n"); return 1;
		}
		if (c.pool_used != 0) { printf("flush left pool_used=%ld\n", c.pool_used); return 1; }
	}

	/* pool exhaustion must be refused, not overrun */
	{
		pt_cache_t s;
		static unsigned short tiny[PT_WORDS_FOR(W, H)];
		static pt_entry_t     te[2];
		unsigned short *a, *b;

		pt_cache_init(&s, tiny, PT_WORDS_FOR(W, H), te, 2);
		a = pt_cache_reserve(&s, 0, 0, W, H, 1);
		b = pt_cache_reserve(&s, 0, 1, W, H, 1);
		if (!a) { printf("first reserve failed\n"); return 1; }
		if (b)  { printf("OVERRAN THE POOL\n"); return 1; }
		if (s.full != 1) { printf("full=%lu want 1\n", s.full); return 1; }
	}

	printf("stale_hits=%d hits=%lu miss=%lu rebuilt=%lu full=%lu\n",
	       bad, c.hit, c.miss, c.rebuilt, c.full);
	return bad ? 1 : 0;
}
"""


@pytest.mark.skipif(shutil.which("cc") is None, reason="no host C compiler")
def test_cache_never_serves_stale_pixels(tmp_path):
    src = tmp_path / "h.c"
    src.write_text(HARNESS)
    exe = tmp_path / "h"
    subprocess.run(
        # the header also defines planar_tile_blit, which this test does not
        # exercise (test_planar_tile.py does); -Werror would reject it as unused.
        ["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-function", "-O2",
         "-I", os.path.join(REPO, "platform/include"),
         str(src), "-o", str(exe)],
        check=True,
    )
    out = subprocess.run([str(exe)], capture_output=True, text=True)
    assert out.returncode == 0, out.stdout + out.stderr
    assert "stale_hits=0" in out.stdout, out.stdout
