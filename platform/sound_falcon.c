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
#include <stdint.h>               /* uintptr_t */
#include <string.h>

#include "plat_sound.h"
#include "display.h"           /* dsp_vdo_cookie: Falcon-vs-TT gate */

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

static int  ring_start(void);
static void (* volatile g_vbl_hook)(void);

/* The ring's sample rate is a RUNTIME value: one binary serves the Falcon
 * CODEC (24585 Hz via Devconnect) and the TT's STE-compatible DMA sound
 * (25033 Hz, rate code 2 at 0xFF8921). Both sit within ~2% of the Mac's
 * 22254.5 Hz; the voice-rate rescale in synth_render keeps pitch exact
 * either way. Set by plat_sound_init before the ring starts. */
static long g_synth_hz = 24585L;

/* The engine's sound task runs here, in supervisor mode. plat_ticks() checks
 * this so it reads _hz_200 directly instead of trapping through Supexec — see
 * platform/input.c. */
extern volatile int g_plat_in_super;

/* Which DMA-sound flavour this machine has. Falcon = the CODEC XBIOS
 * (Devconnect/Setbuffer); TT = the STE-compatible DMA registers banged
 * directly (no XBIOS sound API on TOS 3.x). Anything else runs silent. */
#define SND_NONE    0
#define SND_FALCON  1
#define SND_STE     2
static int g_snd_kind = SND_NONE;

int plat_sound_init(void)
{
	long r, vdo = dsp_vdo_cookie() >> 16;

	if (vdo == 3) {
		g_snd_kind = SND_FALCON;
		g_synth_hz = 24585L;
	} else if (vdo == 2) {
		g_snd_kind = SND_STE;           /* TT: STE-compatible DMA sound */
		g_synth_hz = 25033L;            /* rate code 2 at 0xFF8921 */
	} else {
		return -1;                      /* ST/unknown: silent */
	}

	if (g_snd_kind == SND_FALCON) {
		r = Locksnd();
		if (r != 1)
			return -1;
	}
	g_locked = 1;
	/* Bring the synth's DMA loop up NOW, from normal context. Everything the
	 * engine's sound task later does at interrupt time (start a song, kill a
	 * sound, mix an effect) then reduces to memory writes — no XBIOS call is
	 * ever made from the vblank. An idle loop renders silence. */
	if (ring_start() != 0) {
		g_locked = 0;                   /* no ring, no backend */
		if (g_snd_kind == SND_FALCON)
			Unlocksnd();
		return -1;
	}
	return 0;
}

/* The engine's Mac VBL task (the sound sequencer). Registered through the
 * Sound Manager shim's VInstall; run from our vblank, after the refill. */
void plat_sound_set_vbl_hook(void (*fn)(void))
{
	g_vbl_hook = fn;
}

/* plat_sound_shutdown lives at the end of the file — it tears down the synth
 * ring and the VBL slot, which are declared below. */

/* ==========================================================================
 * The four-tone synth — FRUA's music.
 *
 * The Mac Sound Driver rendered ftMode in software; we are that driver now. A
 * ring buffer in ST-RAM loops forever under the DMA (SB_PLA_RPT) and the VBL
 * renders ahead of the play pointer. Programming the DMA happens ONCE, from
 * normal context (a song always starts from the engine's main loop); after
 * that the VBL only writes samples and reads the DMA address counter, so no
 * XBIOS call is ever made at interrupt time.
 *
 * ONE DELIBERATE DIVERGENCE, at the driver level (not the lift): the Mac's
 * .Sound driver has a single channel, so an effect's KillIO (L7ee0) cancelled
 * the music and it did not come back. Here the effect is MIXED INTO the loop
 * as an extra voice and the music plays on. The engine is unchanged — L7ee0
 * still issues its KillIO — this is only what our "driver" does with it.
 * ========================================================================== */

#define SYNTH_CLK       CLK25K          /* Falcon: 24585 Hz, closest to the Mac */
#define MAC_SYNTH_HZ    22255L          /* 22254.5454, what the Fixed rates mean */
#define RING_SAMPLES    2048L           /* ~83 ms; the VBL renders ~410/frame     */
#define SYNTH_HZ        g_synth_hz      /* runtime rate — declared near the top */

/* FTSoundRec field offsets (see plat_sound.h). */
#define FT_RATE(v)      (2 + (v) * 8)
#define FT_WAVE(v)      (34 + (v) * 4)

static char                 *g_ring;            /* ST-RAM; the DMA loops on it  */
static volatile long         g_ring_w;          /* next sample the VBL renders  */
static volatile int          g_ring_live;       /* the loop is programmed + running */
static const unsigned char * volatile g_ft_rec; /* the LIVE record, or NULL     */
static unsigned long         g_ft_phase[4];

/* An effect mixed into the loop while the synth owns the DMA. */
static volatile long         g_sfx_len, g_sfx_pos;
static signed char           g_sfx_buf[24576];

/* A square-wave tone (swMode): phase-accumulated, counted down in samples. */
static unsigned long         g_tone_phase, g_tone_inc;
static volatile long         g_tone_left;
static short                 g_tone_amp;

static unsigned long rd_be32_p(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16)
	     | ((unsigned long)p[2] << 8)  |  (unsigned long)p[3];
}

/* Where the DMA is reading, as an index into the ring. The Falcon exposes the
 * running playback address in three registers; it moves while we read it, so
 * re-read until two passes agree. */
static unsigned long dma_addr(void)
{
	return ((unsigned long)*(volatile unsigned char *)0xFFFF8909UL << 16)
	     | ((unsigned long)*(volatile unsigned char *)0xFFFF890BUL << 8)
	     |  (unsigned long)*(volatile unsigned char *)0xFFFF890DUL;
}

static long ring_play_index(void)
{
	unsigned long base = (unsigned long)(uintptr_t)g_ring;
	unsigned long a, b;
	int           tries;

	a = dma_addr();
	for (tries = 0; tries < 4; tries++) {
		b = dma_addr();
		if (a == b)
			break;
		a = b;
	}
	if (a < base || a >= base + (unsigned long)RING_SAMPLES)
		return 0;                       /* not in our ring — treat as 0 */
	return (long)(a - base);
}

/* Render `n` samples of (4 wavetable voices + square tone + effect) into dst. */
static void synth_render(char *dst, long n)
{
	const unsigned char *rec = (const unsigned char *)g_ft_rec;
	const unsigned char *wave[4];
	unsigned long        inc[4];
	int                  v, voiced = 0;
	long                 i;

	for (v = 0; v < 4; v++) {
		unsigned long rate;

		inc[v]  = 0;
		wave[v] = NULL;
		if (rec == NULL)
			continue;
		rate    = rd_be32_p(rec + FT_RATE(v));
		wave[v] = (const unsigned char *)(uintptr_t)rd_be32_p(rec + FT_WAVE(v));
		if (rate == 0 || wave[v] == NULL)
			continue;
		/* The Fixed rate steps the wave at the MAC's sample rate; we clock the
		 * CODEC at SYNTH_HZ, so rescale the step or every note is transposed. */
		inc[v] = (unsigned long)(((unsigned long long)rate
		                          * (unsigned long long)MAC_SYNTH_HZ)
		                         / (unsigned long long)SYNTH_HZ);
		if (inc[v] != 0)
			voiced = 1;
	}

	for (i = 0; i < n; i++) {
		long acc = 0;

		if (voiced) {
			for (v = 0; v < 4; v++) {
				if (inc[v] == 0)
					continue;
				g_ft_phase[v] += inc[v];
				acc += (long)wave[v][(g_ft_phase[v] >> 16) & 0xff] - 128;
			}
			acc >>= 2;              /* 4 voices summed -> back into 8-bit */
		}
		if (g_tone_left > 0) {          /* swMode square wave */
			g_tone_phase += g_tone_inc;
			acc += ((g_tone_phase & 0x80000000UL) ? g_tone_amp : -g_tone_amp) >> 1;
			g_tone_left--;
		}
		if (g_sfx_pos < g_sfx_len)      /* the effect rides on top */
			acc += g_sfx_buf[g_sfx_pos++];

		if (acc > 127)
			acc = 127;
		else if (acc < -128)
			acc = -128;
		dst[i] = (char)acc;
	}
}

void plat_sound_vbl(void)
{
	void (*hook)(void);
	long play, lead, todo;

	if (!g_ring_live)
		return;

	play = ring_play_index();
	lead = g_ring_w - play;
	if (lead < 0)
		lead += RING_SAMPLES;
	todo = (RING_SAMPLES / 2) - lead;       /* stay half a ring ahead */

	while (todo > 0) {
		long chunk = RING_SAMPLES - g_ring_w;

		if (chunk > todo)
			chunk = todo;
		synth_render(g_ring + g_ring_w, chunk);
		g_ring_w += chunk;
		if (g_ring_w >= RING_SAMPLES)
			g_ring_w = 0;
		todo -= chunk;
	}

	/* Then run the engine's sound task — the Mac VBL task that drives the
	 * sequencer. Refill FIRST so a slow sequencer pass can never starve the
	 * DMA. It only touches memory (the ring is already programmed), so there
	 * is no XBIOS call from interrupt context. */
	hook = g_vbl_hook;
	if (hook != NULL) {
		g_plat_in_super = 1;
		hook();
		g_plat_in_super = 0;
	}
}

/* Our own VBL slot: the display's VBL only exists when it triple-buffers, and
 * music must not depend on that. Same mechanism as display_videl.c —
 * _vblqueue @ 0x456, _nvbls @ 0x454. */
extern void snd_vbl_trampoline(void);
static long g_snd_vbl_slot = -1;

static long snd_vbl_install_super(void)
{
	long  *queue = *(long **)0x456UL;
	short  nvbls = *(short *)0x454UL;
	short  i;

	for (i = 0; i < nvbls; i++) {
		if (queue[i] == 0) {
			queue[i] = (long)(uintptr_t)snd_vbl_trampoline;
			return i;
		}
	}
	return -1;
}

static long snd_vbl_remove_super(void)
{
	long *queue = *(long **)0x456UL;

	if (g_snd_vbl_slot >= 0)
		queue[g_snd_vbl_slot] = 0;
	return 0;
}

/* --- the TT's STE-compatible DMA sound, banged directly -------------------
 * TOS 3.x has no XBIOS sound API, but the hardware speaks the same ring
 * model: frame base/end registers, a repeat mode that reloads them forever,
 * and a LIVE address counter at the same 0xFF8909/0B/0D bytes dma_addr()
 * already reads on the Falcon. All registers are supervisor-only; the two
 * pokes below run under Supexec (init/shutdown — task context). */

#define STE_CTRL   (*(volatile unsigned char *)0xFFFF8901UL)  /* b0 play, b1 loop */
#define STE_BASE_H (*(volatile unsigned char *)0xFFFF8903UL)
#define STE_BASE_M (*(volatile unsigned char *)0xFFFF8905UL)
#define STE_BASE_L (*(volatile unsigned char *)0xFFFF8907UL)
#define STE_END_H  (*(volatile unsigned char *)0xFFFF890FUL)
#define STE_END_M  (*(volatile unsigned char *)0xFFFF8911UL)
#define STE_END_L  (*(volatile unsigned char *)0xFFFF8913UL)
#define STE_MODE   (*(volatile unsigned char *)0xFFFF8921UL)  /* b7 mono, b0-1 rate */

static char *g_ste_ring;                /* Supexec argument channel */

static long ste_ring_start_super(void)
{
	unsigned long base = (unsigned long)(uintptr_t)g_ste_ring;
	unsigned long end  = base + RING_SAMPLES;

	STE_CTRL   = 0;                     /* stop while reprogramming */
	STE_MODE   = 0x82;                  /* mono, 25033 Hz */
	STE_BASE_H = (unsigned char)(base >> 16);
	STE_BASE_M = (unsigned char)(base >> 8);
	STE_BASE_L = (unsigned char)base;
	STE_END_H  = (unsigned char)(end >> 16);
	STE_END_M  = (unsigned char)(end >> 8);
	STE_END_L  = (unsigned char)end;
	STE_CTRL   = 0x03;                  /* play + repeat: loop forever */
	return 0;
}

static long ste_ring_stop_super(void)
{
	STE_CTRL = 0;
	return 0;
}

/* Program the DMA to loop over the ring forever and hook the vblank that keeps
 * it fed. Called ONCE, from normal context (plat_sound_init). */
static int ring_start(void)
{
	int v;

	if (g_ring_live)
		return 0;
	if (g_ring == NULL) {
		g_ring = (char *)Mxalloc(RING_SAMPLES, 0);      /* 0 = ST-RAM */
		if (g_ring == NULL)
			return -1;
	}
	memset(g_ring, 0, (size_t)RING_SAMPLES);
	g_ring_w = 0;
	for (v = 0; v < 4; v++)
		g_ft_phase[v] = 0;

	if (g_snd_kind == SND_FALCON) {
		Buffoper(0);
		Setmode(MODE_MONO);
		Settracks(0, 0);
		Setbuffer(SR_PLAY, g_ring, g_ring + RING_SAMPLES);
		Devconnect(DMAPLAY, DAC, CLK25M, SYNTH_CLK, 1);
		Buffoper(SB_PLA_ENA | SB_PLA_RPT);      /* loop forever */
	} else {                                        /* SND_STE (TT) */
		g_ste_ring = g_ring;
		Supexec(ste_ring_start_super);
	}
	g_ring_live = 1;

	if (g_snd_vbl_slot < 0)
		g_snd_vbl_slot = Supexec(snd_vbl_install_super);
	return 0;
}

int plat_sound_synth_start(const void *ftsoundrec)
{
	if (!g_locked || ftsoundrec == NULL)
		return -1;
	/* The record is LIVE — the sequencer rewrites its rate fields while it
	 * plays — so keep the pointer, never a copy. */
	g_ft_rec = (const unsigned char *)ftsoundrec;
	return ring_start();                    /* already up unless Mxalloc failed */
}

void plat_sound_synth_stop(void)
{
	/* Silence the voices but keep the loop running: stopping the DMA would
	 * need XBIOS, and jt1151 can reach here from the sequencer. A synth with
	 * no record renders silence. */
	g_ft_rec = NULL;
}

void plat_sound_tone(int count, int amp, int duration_ticks)
{
	if (!g_ring_live || count <= 0) {
		g_tone_left = 0;
		return;
	}
	/* swMode: frequency = 783360 / count. */
	g_tone_inc  = (unsigned long)(((unsigned long long)783360UL << 32)
	                              / ((unsigned long long)count * (unsigned long long)SYNTH_HZ));
	g_tone_amp  = (short)(amp & 0xff);
	g_tone_left = (long)duration_ticks * SYNTH_HZ / 60L;
	if (g_tone_left > SYNTH_HZ)             /* the engine passes 2500 "forever" */
		g_tone_left = SYNTH_HZ / 4;
}

int plat_sound_play_mono8(const signed char *samples, long count, int rate_hz)
{
	int           clk, out_hz;
	unsigned long step, pos;
	long          out_count, i;

	if (!g_locked || samples == NULL || count <= 0 || rate_hz <= 0)
		return -1;

	/* When the synth owns the DMA, an effect becomes another voice in the loop
	 * (see the divergence note above) — resample it to the loop's rate and let
	 * the VBL mix it in, rather than reprogramming the DMA out from under the
	 * music. */
	if (g_ring_live) {
		unsigned long sstep = ((unsigned long)rate_hz << 16) / (unsigned long)SYNTH_HZ;
		unsigned long spos  = 0;
		long          n     = (long)(((unsigned long)count
		                              * (unsigned long)SYNTH_HZ)
		                             / (unsigned long)rate_hz);

		if (n > (long)sizeof g_sfx_buf)
			n = (long)sizeof g_sfx_buf;
		for (i = 0; i < n; i++) {
			long idx  = (long)(spos >> 16);
			long frac = (long)(spos & 0xffffUL);
			long s0, s1;

			if (idx >= count - 1) {
				s0 = s1 = samples[count - 1];
			} else {
				s0 = samples[idx];
				s1 = samples[idx + 1];
			}
			g_sfx_buf[i] = (signed char)(s0 + (((s1 - s0) * frac) >> 16));
			spos += sstep;
		}
		g_sfx_pos = 0;
		g_sfx_len = n;                  /* the VBL picks it up on the next pass */
		return 0;
	}

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
	if (!g_locked)
		return;
	if (g_ring_live) {
		/* L7ee0's KillIO. With the synth looping, this cancels the EFFECT —
		 * killing the DMA would stop the music and could not be restarted
		 * from interrupt context. */
		g_sfx_len = 0;
		g_sfx_pos = 0;
		return;
	}
	Buffoper(0);
}

int plat_sound_playing(void)
{
	/* "Is the EFFECT still going?" — L7ee0 spins on this BEFORE its KillIO, so
	 * a stubbed "never busy" made each effect cut off the one still playing;
	 * the Mac waits for it to finish.
	 *
	 * With the synth looping, the DMA never stops, so the play-enable bit says
	 * nothing about the effect — track the mixed-in effect instead. Otherwise
	 * read the DMA state back with Buffoper(-1); the hardware clears the
	 * play-enable bit itself when a one-shot buffer runs out. */
	if (!g_locked)
		return 0;
	if (g_ring_live)
		return (g_sfx_pos < g_sfx_len) ? 1 : 0;
	return (Buffoper(-1) & SB_PLA_ENA) ? 1 : 0;
}

void plat_sound_shutdown(void)
{
	if (!g_locked)
		return;
	if (g_snd_vbl_slot >= 0) {              /* unhook before freeing the ring */
		Supexec(snd_vbl_remove_super);
		g_snd_vbl_slot = -1;
	}
	g_ring_live = 0;
	g_ft_rec    = NULL;
	if (g_snd_kind == SND_FALCON) {
		Buffoper(0);
		Unlocksnd();
	} else if (g_snd_kind == SND_STE) {
		Supexec(ste_ring_stop_super);
	}
	g_locked = 0;
	if (g_buf != NULL) {
		Mfree(g_buf);
		g_buf      = NULL;
		g_buf_size = 0;
	}
	if (g_ring != NULL) {
		Mfree(g_ring);
		g_ring = NULL;
	}
}
