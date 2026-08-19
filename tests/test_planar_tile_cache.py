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
static unsigned char  rm_row[256];        /* the band's remap row */

static int bad;

/* the indices tile t actually draws, as the cache wants them */
static short used_of(int t, unsigned char *out)
{
    unsigned char seen[256];
    short n = 0;
    int i;

    memset(seen, 0, sizeof seen);
    for (i = 0; i < W * H; i++) {
        unsigned char v = bodies[t][i];
        if (!lut[v] || seen[v]) continue;
        seen[v] = 1;
        if (n < PT_MAX_USED) out[n] = v;
        n++;
    }
    return (n > PT_MAX_USED) ? 0 : n;      /* 0 = fall back to whole row */
}

/* compose chunky -> slot through the remap, the way the engine does */
static void build_lut2(unsigned short *l2)
{
    int i;
    for (i = 0; i < 256; i++)
        l2[i] = lut[i] ? (unsigned short)(0x100u | rm_row[lut[i] & 0xff]) : 0;
}

static void check(pt_cache_t *c, short slot, short idx, int t)
{
    unsigned short *got = pt_cache_get(c, slot, idx, rm_row);
    unsigned short l2[256];

    if (!got) return;                      /* a miss is legal */
    build_lut2(l2);
    planar_tile_build(fresh, bodies[t], W, H, l2);
    if (memcmp(got, fresh, sizeof fresh) != 0) {
        if (++bad <= 3) printf("STALE HIT slot=%d idx=%d\n", slot, idx);
    }
}

static unsigned short *put(pt_cache_t *c, short slot, short idx, int t)
{
    unsigned char used[PT_MAX_USED];
    short n = used_of(t, used);
    unsigned short *e = pt_cache_reserve(c, slot, idx, W, H, rm_row, used, n);
    unsigned short l2[256];

    if (e) { build_lut2(l2); planar_tile_build(e, bodies[t], W, H, l2); }
    return e;
}

int main(void)
{
    pt_cache_t c;
    int t, round, i;

    pt_cache_init(&c, pool, POOL_WORDS, ents, NTILE);
    for (i = 0; i < 256; i++) {
        unsigned char k = rnd();
        lut[i] = (k < 64) ? 0 : (unsigned short)(0x100u | k);
        rm_row[i] = (unsigned char)(rnd() & 15);
    }
    for (t = 0; t < NTILE; t++)
        for (i = 0; i < W * H; i++)
            bodies[t][i] = (unsigned char)(rnd() % 40);   /* few distinct */

    for (t = 0; t < NTILE; t++)
        if (!put(&c, (short)(t % 3), (short)t, t)) { printf("UNEXPECTED FULL\n"); return 1; }
    if (c.miss != NTILE) { printf("miss=%lu\n", c.miss); return 1; }

    for (round = 0; round < 3; round++)
        for (t = 0; t < NTILE; t++)
            check(&c, (short)(t % 3), (short)t, t);
    if (c.hit == 0) { printf("no hits\n"); return 1; }

    /* ★ THE NARROWING. Move a remap entry NO tile draws: every entry must still
     * hit. Whole-row fingerprinting would invalidate all of them here. */
    {
        unsigned long hit0 = c.hit;
        int unused_i = -1;

        for (i = 0; i < 256 && unused_i < 0; i++) {
            int t2, drawn = 0;
            for (t2 = 0; t2 < NTILE && !drawn; t2++) {
                int j;
                for (j = 0; j < W * H; j++)
                    if (bodies[t2][j] == i && lut[i]) { drawn = 1; break; }
            }
            if (!drawn) unused_i = i;
        }
        if (unused_i < 0) { printf("no unused index to perturb\n"); return 1; }
        rm_row[unused_i] = (unsigned char)((rm_row[unused_i] + 7) & 15);
        for (t = 0; t < NTILE; t++)
            if (!pt_cache_get(&c, (short)(t % 3), (short)t, rm_row)) {
                printf("UNUSED-ENTRY CHANGE INVALIDATED t=%d\n", t);
                return 1;
            }
        if (c.hit != hit0 + NTILE) { printf("hit accounting off\n"); return 1; }
    }

    /* ...and a remap entry a tile DOES draw must still invalidate it. */
    {
        unsigned char used[PT_MAX_USED];
        short n = used_of(0, used);

        if (n <= 0) { printf("tile 0 has no compact index set\n"); return 1; }
        rm_row[used[0]] = (unsigned char)((rm_row[used[0]] + 9) & 15);
        if (pt_cache_get(&c, 0, 0, rm_row)) {
            printf("USED-ENTRY CHANGE DID NOT INVALIDATE\n");
            return 1;
        }
        if (!put(&c, 0, 0, 0)) { printf("rebuild failed\n"); return 1; }
        check(&c, 0, 0, 0);
    }

    /* ★ THE FALLBACK ARM. A tile with more than PT_MAX_USED distinct indices
     * cannot store a compact list, so nused becomes 0 and the fingerprint goes
     * back to the whole row — coarser, never wrong. Exercise it: it must hit
     * while nothing moves, and must invalidate when ANY row entry moves. */
    {
        unsigned char used[PT_MAX_USED];
        short n;

        for (i = 0; i < W * H; i++)
            bodies[2][i] = (unsigned char)rnd();       /* ~all 256 values */
        n = used_of(2, used);
        if (n != 0) { printf("expected the whole-row fallback, got n=%d\n", n); return 1; }
        if (!put(&c, 2, 2, 2)) { printf("fallback reserve failed\n"); return 1; }
        check(&c, 2, 2, 2);
        if (!pt_cache_get(&c, 2, 2, rm_row)) { printf("fallback entry did not hit\n"); return 1; }
        {
            /* any entry at all must now invalidate it */
            int k = 0;
            rm_row[k] = (unsigned char)((rm_row[k] + 5) & 15);
            if (pt_cache_get(&c, 2, 2, rm_row)) {
                printf("FALLBACK DID NOT INVALIDATE ON A ROW CHANGE\n");
                return 1;
            }
        }
    }

    /* a wall-set change: same idx, different art -> only a flush saves it */
    {
        for (i = 0; i < W * H; i++) bodies[1][i] = (unsigned char)(rnd() % 40);
        pt_cache_flush(&c);
        if (pt_cache_get(&c, 1, 1, rm_row)) { printf("FLUSH FAILED\n"); return 1; }
        if (c.pool_used != 0) { printf("flush left pool\n"); return 1; }
    }

    /* ★ A CALLER THAT DOES NOT CLAMP. used_of() already returns 0 past
     * PT_MAX_USED, so the guard inside pt_set_used is only reachable from a
     * malformed caller — and without it the copy walks off uidx[] and smashes
     * the entry table. Call reserve directly with an oversized count and
     * require the fallback. */
    {
        pt_cache_t g;
        static unsigned short gpool[PT_WORDS_FOR(W, H) * 2];
        static pt_entry_t     ge[2];
        unsigned char used[256];
        int k;

        for (k = 0; k < 256; k++) used[k] = (unsigned char)k;
        pt_cache_init(&g, gpool, PT_WORDS_FOR(W, H) * 2, ge, 2);
        if (!pt_cache_reserve(&g, 0, 0, W, H, rm_row, used, 200)) {
            printf("oversized reserve failed\n"); return 1;
        }
        if (ge[0].nused != 0) {
            printf("OVERSIZED nused NOT CLAMPED: %d\n", ge[0].nused);
            return 1;
        }
        if (ge[1].slot != -1) { printf("entry table corrupted\n"); return 1; }
    }

    /* exhaustion is refused, not overrun */
    {
        pt_cache_t s;
        static unsigned short tiny[PT_WORDS_FOR(W, H)];
        static pt_entry_t     te[2];
        unsigned char used[PT_MAX_USED];
        short n = used_of(0, used);

        pt_cache_init(&s, tiny, PT_WORDS_FOR(W, H), te, 2);
        if (!pt_cache_reserve(&s, 0, 0, W, H, rm_row, used, n)) { printf("first failed\n"); return 1; }
        if ( pt_cache_reserve(&s, 0, 1, W, H, rm_row, used, n)) { printf("OVERRAN POOL\n"); return 1; }
        if (s.full != 1) { printf("full=%lu\n", s.full); return 1; }
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
