/*
 * monobench — host micro-benchmark for the FRUA mono (BWMODE) present pack.
 *
 * The mono ST-High B&W path packs a 480x300 chunky surface to 1bpp in
 * platform/display_sthigh.c `hi_blit_rows` — 8 chunky bytes (each mapped to a
 * 0/1 ink bit via g_dsp_ink[]) into one screen byte, for every packed row of
 * every present. It is the mono render hotspot.
 *
 * This benchmark compares two byte-identical pack forms and confirms they
 * produce the same output:
 *   - FIXED-SHIFT   the old form: (ink[s0]<<7)|(ink[s1]<<6)|...|ink[s7]
 *   - ACCUMULATE    the current form: b=(b<<1)|ink[sN], MSB-first
 * On the 68000 the fixed-shift form emits `lsl.b #7/#6/#5/#4` — a shift-by-N
 * costs 6+2N cycles (up to 20) — while ACCUMULATE emits `add.l d0,d0` (constant
 * 8-cycle <<1) and folds the LUT load into `or.b (a1,d.l),d0`: ~43 vs 54 insns
 * and 0 vs 5 shifts in the inner loop (m68k-atari-mint-gcc -O2). Host timing
 * cannot see that (x86 shifts are 1 cycle), so this file's VALUE is the
 * byte-identical proof; the win is on the target and shown in the disassembly.
 *
 * It also times the full-surface present diff (memcmp of 300x480) — cheap on
 * the host (SIMD memcmp) but disproportionately costly on a 68000 (word loop),
 * which is why the *next* mono win is dirty-row plumbing, not the pack.
 *
 * Build/run (host):  cc -O2 -o /tmp/monobench tools/monobench.c && /tmp/monobench
 * Inspect target codegen:
 *   m68k-atari-mint-gcc -m68000 -O2 -S -o- platform/display_sthigh.c | less
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SURF_W 480
#define SURF_H 300

static unsigned char ink[256];
static unsigned char chunky[SURF_W * SURF_H];
static unsigned char shadow[SURF_W * SURF_H];
static unsigned char out_fixed[SURF_W / 8 * SURF_H];
static unsigned char out_accum[SURF_W / 8 * SURF_H];

static void pack_fixed(const unsigned char *s, unsigned char *d, int nbytes)
{
    int i;
    for (i = 0; i < nbytes; i++) {
        *d++ = (unsigned char)((ink[s[0]] << 7) | (ink[s[1]] << 6)
             | (ink[s[2]] << 5) | (ink[s[3]] << 4) | (ink[s[4]] << 3)
             | (ink[s[5]] << 2) | (ink[s[6]] << 1) | ink[s[7]]);
        s += 8;
    }
}

static void pack_accum(const unsigned char *s, unsigned char *d, int nbytes)
{
    int i;
    for (i = 0; i < nbytes; i++) {
        unsigned b = ink[s[0]];
        b = (b << 1) | ink[s[1]]; b = (b << 1) | ink[s[2]];
        b = (b << 1) | ink[s[3]]; b = (b << 1) | ink[s[4]];
        b = (b << 1) | ink[s[5]]; b = (b << 1) | ink[s[6]];
        b = (b << 1) | ink[s[7]];
        *d++ = (unsigned char)b;
        s += 8;
    }
}

static int present_diff_rows(void)   /* the full-surface diff sthigh_present does */
{
    int y, changed = 0;
    for (y = 0; y < SURF_H; y++)
        if (memcmp(chunky + (long)y * SURF_W, shadow + (long)y * SURF_W, SURF_W))
            changed++;
    return changed;
}

static double now(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

int main(void)
{
    int i, iter;
    const int N = 4000, ROWBYTES = SURF_W / 8;
    double t0, t;

    for (i = 0; i < 256; i++) ink[i] = (i & 1);              /* half the palette dark */
    for (i = 0; i < SURF_W * SURF_H; i++) chunky[i] = (unsigned char)(i * 7 + (i >> 4));

    /* correctness: the two pack forms are byte-identical */
    for (i = 0; i < SURF_H; i++) pack_fixed(chunky + (long)i * SURF_W, out_fixed + (long)i * ROWBYTES, ROWBYTES);
    for (i = 0; i < SURF_H; i++) pack_accum(chunky + (long)i * SURF_W, out_accum + (long)i * ROWBYTES, ROWBYTES);
    printf("pack ACCUMULATE == FIXED-SHIFT : %s\n",
           memcmp(out_fixed, out_accum, sizeof out_fixed) == 0 ? "YES (byte-identical)" : "NO -- BUG");

    /* host timing (relative only; the real win is on the 68000 -- see header) */
    t0 = now();
    for (iter = 0; iter < N; iter++)
        for (i = 0; i < SURF_H; i++) pack_fixed(chunky + (long)i * SURF_W, out_fixed + (long)i * ROWBYTES, ROWBYTES);
    t = now() - t0;
    printf("pack full surface (fixed)      : %.4f ms/frame (host)\n", t / N * 1000);

    t0 = now();
    for (iter = 0; iter < N; iter++)
        for (i = 0; i < SURF_H; i++) pack_accum(chunky + (long)i * SURF_W, out_accum + (long)i * ROWBYTES, ROWBYTES);
    t = now() - t0;
    printf("pack full surface (accumulate) : %.4f ms/frame (host)\n", t / N * 1000);

    /* full-surface diff cost (cheap here; word-loop-expensive on 68000) */
    memcpy(shadow, chunky, sizeof chunky);
    t0 = now();
    for (iter = 0; iter < N; iter++) (void)present_diff_rows();
    t = now() - t0;
    printf("full-surface diff (clean)      : %.4f ms/frame (host; 68000 has no SIMD memcmp)\n", t / N * 1000);
    return 0;
}
