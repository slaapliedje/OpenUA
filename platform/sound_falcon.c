/*
 * Falcon DMA-sound backend for the Sound Manager shim. See sound.h.
 *
 * Wiring:
 *   plat_sound_init      -> Locksnd
 *   plat_sound_play_...  -> Setbuffer + Setmode + Settracks +
 *                           Devconnect + Soundcmd + Buffoper
 *   plat_sound_stop      -> Buffoper(0)
 *   plat_sound_shutdown  -> Buffoper(0) + Unlocksnd
 *
 * Falcon DMA reads from ST-RAM (chip RAM), so plat_sound_play_mono8
 * allocates its working buffer via Mxalloc(0) (0 = ST-RAM) and grows
 * it on demand. The Mac unsigned-8-bit → Falcon signed-8-bit
 * conversion lives in the Sound Manager shim; this backend just plays
 * whatever signed bytes the caller hands over.
 *
 * Sample-rate matching: the Falcon CODEC supports a small set of
 * fixed rates (8195 / 9834 / 12292 / 16390 / 19668 / 24585 / 32780 /
 * 49170 Hz), and none of the Mac's rates is among them (FRUA's effects
 * are 22254.5454/n — 7417 and 11127 Hz). pick_clk() picks the closest
 * clockable rate and plat_sound_play_mono8 RESAMPLES the wave to it, so
 * the effect keeps the Mac's pitch instead of being transposed by the
 * ratio between the two rates.
 */

#include <mint/falcon.h>
#include <mint/osbind.h>          /* Mxalloc, Mfree */
#include <stddef.h>
#include <string.h>

#include "plat_sound.h"

static char *g_buf      = NULL;
static long  g_buf_size = 0;
static int   g_locked   = 0;

static const struct {
	int hz;
	int clk;
} k_clks[] = {
	{  8195, CLK8K  },
	{  9834, CLK10K },
	{ 12292, CLK12K },
	{ 16390, CLK16K },
	{ 19668, CLK20K },
	{ 24585, CLK25K },
	{ 32780, CLK33K },
	{ 49170, CLK50K },
};

/* Nearest supported CODEC rate to `hz`; *out_hz gets the rate actually clocked. */
static int pick_clk(int hz, int *out_hz)
{
	int best  = k_clks[0].clk;
	int besth = k_clks[0].hz;
	long bd   = (long)hz - k_clks[0].hz;
	unsigned k;

	if (bd < 0)
		bd = -bd;
	for (k = 1; k < sizeof k_clks / sizeof k_clks[0]; k++) {
		long d = (long)hz - k_clks[k].hz;

		if (d < 0)
			d = -d;
		if (d < bd) {
			bd    = d;
			best  = k_clks[k].clk;
			besth = k_clks[k].hz;
		}
	}
	if (out_hz != NULL)
		*out_hz = besth;
	return best;
}

int plat_sound_init(void)
{
	long r = Locksnd();

	if (r != 1)
		return -1;
	g_locked = 1;
	return 0;
}

void plat_sound_shutdown(void)
{
	if (!g_locked)
		return;
	Buffoper(0);
	Unlocksnd();
	g_locked = 0;
	if (g_buf != NULL) {
		Mfree(g_buf);
		g_buf      = NULL;
		g_buf_size = 0;
	}
}

int plat_sound_play_mono8(const signed char *samples, long count, int rate_hz)
{
	int           clk, out_hz;
	unsigned long step, pos;
	long          out_count, i;

	if (!g_locked || samples == NULL || count <= 0 || rate_hz <= 0)
		return -1;

	/* The CODEC only clocks the eight fixed rates in k_clks, and FRUA's
	 * effects are sampled at 22254.5454/n (7417 / 11127 Hz) — none of which
	 * the Falcon can clock. Playing the samples out at the nearest rate would
	 * transpose them (7417 -> 8195 is a semitone sharp), so RESAMPLE to the
	 * rate we can actually clock and keep the Mac's pitch. Linear
	 * interpolation, 16.16 fixed point — no FPU (the default build is
	 * -msoft-float). */
	clk       = pick_clk(rate_hz, &out_hz);
	step      = ((unsigned long)rate_hz << 16) / (unsigned long)out_hz;
	out_count = (long)(((unsigned long)count * (unsigned long)out_hz)
	                   / (unsigned long)rate_hz);
	if (out_count <= 0)
		return -1;
	out_count &= ~1L;                       /* DMA wants an even byte count */
	if (out_count <= 0)
		return -1;

	if (g_buf_size < out_count) {
		if (g_buf != NULL)
			Mfree(g_buf);
		g_buf = (char *)Mxalloc(out_count, 0);  /* 0 = ST-RAM */
		if (g_buf == NULL) {
			g_buf_size = 0;
			return -1;
		}
		g_buf_size = out_count;
	}

	pos = 0;
	for (i = 0; i < out_count; i++) {
		long idx  = (long)(pos >> 16);
		long frac = (long)(pos & 0xffffUL);
		long s0, s1;

		if (idx >= count - 1) {
			s0 = s1 = samples[count - 1];
		} else {
			s0 = samples[idx];
			s1 = samples[idx + 1];
		}
		g_buf[i] = (char)(s0 + (((s1 - s0) * frac) >> 16));
		pos += step;
	}

	Buffoper(0);
	Setmode(MODE_MONO);
	Settracks(0, 0);
	Setbuffer(SR_PLAY, g_buf, g_buf + out_count);
	Devconnect(DMAPLAY, DAC, CLK25M, clk, 1);
	Buffoper(SB_PLA_ENA);
	return 0;
}

void plat_sound_stop(void)
{
	if (g_locked)
		Buffoper(0);
}

int plat_sound_playing(void)
{
	/* Buffoper(-1) reads the DMA state back without changing it; the
	 * play-enable bit clears itself when the buffer runs out (we never set
	 * repeat). The engine's sfx leaf (L7ee0) spins on this BEFORE its KillIO,
	 * so a stubbed "never busy" made each effect cut off the one still
	 * playing — the Mac waits for it to finish. */
	if (!g_locked)
		return 0;
	return (Buffoper(-1) & SB_PLA_ENA) ? 1 : 0;
}
