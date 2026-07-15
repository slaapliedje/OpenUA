/*
 * Amiga Paula audio backend (ADR-0012) — direct chipset.
 *
 * Implements the plat_sound HAL (plat_sound.h) with the SAME architecture as
 * the Falcon backend (sound_falcon.c): ONE looping DMA buffer that a
 * software mixer renders into every vertical blank — four wavetable voices
 * (the Mac four-tone synth = FRUA's music), the swMode square tone, and the
 * current effect mixed on top. Paula's channel 0 loops the ring natively
 * (audio DMA reloads AUDxLC/LEN when the length runs out), so the DMA is
 * programmed ONCE from task context; everything at interrupt time is memory
 * writes, exactly the discipline the Falcon backend established.
 *
 * The Falcon reads its DMA play address to know where to render; Paula's
 * audio pointers are WRITE-ONLY, so the play cursor is MODELED instead: with
 * period 156, one PAL frame consumes EXACTLY 454 samples (156 * 454 = 70824
 * = 227 colour clocks * 312 lines), so the cursor advances a fixed count per
 * VERTB and never drifts — audio DMA and the vertical blank derive from the
 * same crystal. The ring is 8 frames long and the writer keeps half a ring
 * of lead, so a missed VBL is absorbed, not audible.
 *
 * The same deliberate driver-level divergence as the Falcon: the Mac .Sound
 * driver was single-channel, so an effect's KillIO cancelled the music for
 * good. Here the effect is a voice mixed into the loop and the music plays
 * on; the engine (L7ee0) is unchanged.
 *
 * The LED filter: A500-class Amigas gate a ~3.2kHz low-pass with the power
 * LED. FRUA's effects run up to 11kHz and the ring at 22.7kHz — the filter
 * would muffle everything, so init turns it off (LED dim) and shutdown
 * restores it.
 */

#include "plat_sound.h"

#ifdef FRUA_AMIGA

#include <exec/types.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <hardware/dmabits.h>
#include <proto/exec.h>

#define CUSTOM ((volatile struct Custom *)0xDFF000)
#define CIAA   ((volatile struct CIA *)0xBFE001)

/* Set while the VBL runs engine code (the sequencer hook) — the dbg sinks
 * check it and defer to a ring instead of calling dos.library from an
 * interrupt (dbglog_amiga.c). */
extern volatile int g_amiga_in_int;

/* Paula period 156 at the PAL colour clock (3546895 Hz) = 22736.5 Hz, right
 * next to the Mac Sound Driver's 22254.5 Hz; the voice-rate rescale below
 * keeps the pitch exact. 454 samples per PAL frame, exactly. */
#define SYNTH_PER        156
#define SYNTH_HZ         22737L
#define MAC_SYNTH_HZ     22255L         /* what the Fixed rates mean */
#define FRAME_SAMPLES    454L
#define RING_SAMPLES     (FRAME_SAMPLES * 8)    /* 3632 bytes, CHIP */

/* FTSoundRec field offsets (see plat_sound.h). */
#define FT_RATE(v)      (2 + (v) * 8)
#define FT_WAVE(v)      (34 + (v) * 4)

static signed char          *g_ring;            /* CHIP; AUD0 loops on it     */
static volatile long         g_ring_w;          /* next sample to render      */
static volatile long         g_ring_play;       /* modeled DMA play cursor    */
static volatile int          g_ring_live;
static const unsigned char * volatile g_ft_rec; /* the LIVE record, or NULL   */
static unsigned long         g_ft_phase[4];

/* An effect mixed into the loop. */
static volatile long         g_sfx_len, g_sfx_pos;
static signed char           g_sfx_buf[24576];

/* A square-wave tone (swMode). */
static unsigned long         g_tone_phase, g_tone_inc;
static volatile long         g_tone_left;
static short                 g_tone_amp;

static UBYTE                 g_saved_led;       /* filter state to restore    */
static int                   g_inited;

static unsigned long rd_be32_p(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16)
	     | ((unsigned long)p[2] << 8)  |  (unsigned long)p[3];
}

/* Render `n` samples of (4 wavetable voices + square tone + effect) into
 * dst — the same mixer as the Falcon backend, at Paula's ring rate. */
static void synth_render(signed char *dst, long n)
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
		wave[v] = (const unsigned char *)rd_be32_p(rec + FT_WAVE(v));
		if (rate == 0 || wave[v] == NULL)
			continue;
		/* The Fixed rate steps the wave at the MAC's sample rate; the
		 * ring clocks SYNTH_HZ, so rescale or every note transposes. */
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
			acc >>= 2;              /* 4 voices summed -> 8-bit */
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
		dst[i] = (signed char)acc;
	}
}

int plat_sound_init(void)
{
	int v;

	if (g_inited)
		return 0;
	g_ring = AllocMem(RING_SAMPLES, MEMF_CHIP | MEMF_CLEAR);
	if (g_ring == NULL)
		return -1;

	g_ring_w    = RING_SAMPLES / 2;         /* start half a ring ahead */
	g_ring_play = 0;
	for (v = 0; v < 4; v++)
		g_ft_phase[v] = 0;

	/* Program channel 0 once; Paula reloads LC/LEN itself at the end of
	 * the buffer — a hardware ring. Kill any modulation linkage first. */
	CUSTOM->dmacon = DMAF_AUD0;                     /* clear while we set up */
	CUSTOM->adkcon = 0x00FF;                        /* clear all AM/FM links */
	CUSTOM->aud[0].ac_ptr = (UWORD *)g_ring;
	CUSTOM->aud[0].ac_len = (UWORD)(RING_SAMPLES / 2);      /* words */
	CUSTOM->aud[0].ac_per = SYNTH_PER;
	CUSTOM->aud[0].ac_vol = 64;
	CUSTOM->dmacon = (UWORD)(DMAF_SETCLR | DMAF_MASTER | DMAF_AUD0);

	/* LED off = the 3.2kHz low-pass filter off (see the header note). */
	g_saved_led = (UBYTE)(CIAA->ciapra & CIAF_LED);
	CIAA->ciapra |= CIAF_LED;

	g_ring_live = 1;
	g_inited    = 1;
	return 0;
}

void plat_sound_shutdown(void)
{
	if (!g_inited)
		return;
	g_ring_live = 0;
	g_ft_rec    = NULL;
	CUSTOM->dmacon = DMAF_AUD0;
	CUSTOM->aud[0].ac_vol = 0;
	if (g_saved_led == 0)
		CIAA->ciapra &= (UBYTE)~CIAF_LED;       /* filter was on: restore */
	if (g_ring) {
		FreeMem(g_ring, RING_SAMPLES);
		g_ring = NULL;
	}
	g_inited = 0;
}

int plat_sound_synth_start(const void *ftsoundrec)
{
	if (!g_ring_live || ftsoundrec == NULL)
		return -1;
	/* The record is LIVE — the sequencer rewrites its rate fields while it
	 * plays — so keep the pointer, never a copy. */
	g_ft_rec = (const unsigned char *)ftsoundrec;
	return 0;
}

void plat_sound_synth_stop(void)
{
	/* Silence the voices, keep the loop running: a synth with no record
	 * renders silence (reachable from the sequencer at interrupt time). */
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
	unsigned long sstep, spos = 0;
	long          n, i;

	if (!g_ring_live || samples == NULL || count <= 0 || rate_hz <= 0)
		return -1;

	/* The effect becomes a voice in the loop (the driver-level divergence in
	 * the header): resample to the ring rate — linear interpolation, 16.16
	 * fixed point, no FPU — and let the VBL mix it in. */
	sstep = ((unsigned long)rate_hz << 16) / (unsigned long)SYNTH_HZ;
	n     = (long)(((unsigned long)count * (unsigned long)SYNTH_HZ)
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
	g_sfx_len = n;                  /* the VBL picks it up next pass */
	return 0;
}

void plat_sound_stop(void)
{
	/* L7ee0's KillIO: cancels the EFFECT; the loop (and the music) live on —
	 * the same semantics as the Falcon backend. */
	g_sfx_len = 0;
	g_sfx_pos = 0;
}

int plat_sound_playing(void)
{
	/* "Is the EFFECT still going?" — L7ee0 spins on this before its KillIO;
	 * a "never busy" stub made each effect cut off the previous one. */
	return (g_ring_live && g_sfx_pos < g_sfx_len) ? 1 : 0;
}

/* The engine's sound task (the Mac VBL task driving the sequencer). */
static void (* volatile s_vbl_hook)(void);

void plat_sound_set_vbl_hook(void (*fn)(void))
{
	s_vbl_hook = fn;
}

/* Called from the input backend's VERTB server every frame. Advance the
 * modeled play cursor by the frame's exact sample count, render up to half
 * a ring of lead, then run the sequencer — refill FIRST so a slow sequencer
 * pass can never starve the DMA. Memory writes only: interrupt-safe. */
void plat_sound_vbl(void)
{
	void (*hook)(void);
	long lead, todo;

	if (!g_ring_live)
		return;

	g_ring_play += FRAME_SAMPLES;
	if (g_ring_play >= RING_SAMPLES)
		g_ring_play -= RING_SAMPLES;

	lead = g_ring_w - g_ring_play;
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

	hook = s_vbl_hook;
	if (hook != NULL) {
		g_amiga_in_int = 1;     /* dbg sinks defer while engine code runs */
		hook();
		g_amiga_in_int = 0;
	}
}

#endif /* FRUA_AMIGA */
