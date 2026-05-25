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
 * 49170 Hz). pick_clk() returns the prescaler value whose Hz is
 * closest to the request — Mac 22.05 kHz lands on 19668 (CLK20K),
 * 11.025 kHz on 12292 (CLK12K).
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

static int pick_clk(int hz)
{
	int best  = k_clks[0].clk;
	long bd   = (long)hz - k_clks[0].hz;
	unsigned k;

	if (bd < 0)
		bd = -bd;
	for (k = 1; k < sizeof k_clks / sizeof k_clks[0]; k++) {
		long d = (long)hz - k_clks[k].hz;

		if (d < 0)
			d = -d;
		if (d < bd) {
			bd   = d;
			best = k_clks[k].clk;
		}
	}
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
	int clk;

	if (!g_locked || samples == NULL || count <= 0)
		return -1;

	if (g_buf_size < count) {
		if (g_buf != NULL)
			Mfree(g_buf);
		g_buf = (char *)Mxalloc(count, 0);      /* 0 = ST-RAM */
		if (g_buf == NULL) {
			g_buf_size = 0;
			return -1;
		}
		g_buf_size = count;
	}
	memcpy(g_buf, samples, (size_t)count);

	clk = pick_clk(rate_hz);
	Buffoper(0);
	Setmode(MODE_MONO);
	Settracks(0, 0);
	Setbuffer(SR_PLAY, g_buf, g_buf + count);
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
	/* Sndstatus's bit semantics differ across TOS revisions; the
	 * skeleton reports "not playing" so the engine never blocks on
	 * busy-wait. Real status follows when an async path needs it. */
	return 0;
}
