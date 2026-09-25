/*
 * The software mixer behind the DMA-sound backends (see sound_mix.h): the
 * four-tone synth that is FRUA's music, the swMode tone and one effect
 * mixed on top, rendered as signed 8-bit mono at mix_hz. Moved here from
 * sound_falcon.c unchanged so that the Unix backend (/dev/audio) renders
 * with the same code; the backends keep only how samples reach the
 * hardware.
 *
 * ONE DELIBERATE DIVERGENCE, at the driver level (not the lift): the Mac's
 * .Sound driver has a single channel, so an effect's KillIO (L7ee0) cancelled
 * the music and it did not come back. Here the effect is MIXED IN as an
 * extra voice and the music plays on. The engine is unchanged - L7ee0 still
 * issues its KillIO - this is only what our "driver" does with it.
 */

#include <stddef.h>
#include <stdint.h>               /* uintptr_t */

#include "sound_mix.h"
#ifdef FRUA_SNDPROF
#include "dbglog.h"
#endif

#define MAC_SYNTH_HZ    22255L          /* 22254.5454, what the Fixed rates mean */
#define SYNTH_HZ        mix_hz

long mix_hz = 24585L;

/* FTSoundRec field offsets (see plat_sound.h). */
#define FT_RATE(v)      (2 + (v) * 8)
#define FT_WAVE(v)      (34 + (v) * 4)

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

#ifdef FRUA_SNDPROF
/* #63 follow-up. PC sampling put plat_sound_vbl at ~56% of the ST/STe play
 * loop and FRUA_NOSOUND priced the whole subsystem at ~70% of wall — but
 * neither says WHICH state that time was spent in. The synth renders a fixed
 * ~410 samples every vblank whether or not anything is audible, so "the sound
 * costs 70%" is compatible with two very different fixes:
 *
 *   idle-dominated  -> gate the mixer when nothing sounds. Free, no fidelity
 *                      cost, and the loop below is pure waste.
 *   voiced-dominated-> the four-tone synth itself is the cost. Only a cheaper
 *                      inner loop or a lower sample rate helps, and the rate
 *                      is an audible trade.
 *
 * Bucket the samples by the state they were rendered in. Accounted per CALL
 * (~50/sec), not per sample, so the instrument cannot distort what it measures. */
static long sp_snd_calls, sp_snd_silent, sp_snd_voiced, sp_snd_tone, sp_snd_sfx;

void plat_sound_prof_dump(void)
{
	dbg_log_num("b63snd: render calls  = ", sp_snd_calls);
	dbg_log_num("b63snd: samples SILENT= ", sp_snd_silent);
	dbg_log_num("b63snd: samples VOICED= ", sp_snd_voiced);
	dbg_log_num("b63snd: samples tone  = ", sp_snd_tone);
	dbg_log_num("b63snd: samples sfx   = ", sp_snd_sfx);
}
#endif

/* Is anything actually going to make noise? The four-tone record is written by
 * the sequencer at interrupt time, so this is the same test synth_render makes
 * — hoisted out so the vblank can decline to render silence sample by sample.
 * See g_quiet_run in plat_sound_vbl for why that mattered so much. */
int mix_audible(void)
{
	const unsigned char *rec = (const unsigned char *)g_ft_rec;
	int v;

	if (g_tone_left > 0 || g_sfx_pos < g_sfx_len)
		return 1;
	if (rec == NULL)
		return 0;
	for (v = 0; v < 4; v++)
		if (rd_be32_p(rec + FT_RATE(v)) != 0 &&
		    rd_be32_p(rec + FT_WAVE(v)) != 0)
			return 1;
	return 0;
}

/* Render `n` samples of (4 wavetable voices + square tone + effect) into dst.
 *
 * Same rewrite as the Amiga backend (19239f7f, where the old shape measured
 * ~770 cycles/sample on the 68000 and starved the VBL): active voices are
 * COMPACTED once per call with phases in locals (no global read-modify-write
 * per sample), wavetable bytes are summed UNSIGNED with the -128-per-voice
 * bias folded into the final >>2, and the steady music case (no tone, no sfx)
 * gets per-voice-count specializations with no clamp — after the bias and
 * >>2 the result provably fits -128..127 (max 4*255-512 = 508 -> 127, min
 * -512 -> -128). Tone/sfx frames keep the general path: same operation
 * order, same single final clamp. Output is byte-identical to the old mixer.
 * The 030's cache made the old shape affordable here, but the rewrite is
 * strictly cheaper and keeps the two backends the same code shape. */
void mix_render(char *dst, long n)
{
	const unsigned char *rec = (const unsigned char *)g_ft_rec;
	const unsigned char *wv[4];
	unsigned long        iv[4], pv[4];
	short                ix[4];
	int                  v, nv = 0;
	long                 i;

	for (v = 0; v < 4; v++) {
		unsigned long rate, inc;
		const unsigned char *w;

		if (rec == NULL)
			continue;
		rate = rd_be32_p(rec + FT_RATE(v));
		w    = (const unsigned char *)(uintptr_t)rd_be32_p(rec + FT_WAVE(v));
		if (rate == 0 || w == NULL)
			continue;
		/* The Fixed rate steps the wave at the MAC's sample rate; we clock the
		 * CODEC at SYNTH_HZ, so rescale the step or every note is transposed. */
		inc = (unsigned long)(((unsigned long long)rate
		                       * (unsigned long long)MAC_SYNTH_HZ)
		                      / (unsigned long long)SYNTH_HZ);
		if (inc == 0)
			continue;
		wv[nv] = w;
		iv[nv] = inc;
		pv[nv] = g_ft_phase[v];
		ix[nv] = (short)v;
		nv++;
	}

#ifdef FRUA_SNDPROF
	sp_snd_calls++;
	if (nv > 0)                     sp_snd_voiced += n;
	if (g_tone_left > 0)            sp_snd_tone   += n;
	if (g_sfx_pos < g_sfx_len)      sp_snd_sfx    += n;
	if (nv == 0 && g_tone_left <= 0 && g_sfx_pos >= g_sfx_len)
		sp_snd_silent += n;     /* rendered, inaudible, paid for anyway */
#endif

	if (g_tone_left <= 0 && g_sfx_pos >= g_sfx_len) {
		/* music only — the case that runs for the length of every tune */
		switch (nv) {
		case 0:
			for (i = 0; i < n; i++)
				dst[i] = 0;
			break;
		case 1: {
			const unsigned char *w0 = wv[0];
			unsigned long p0 = pv[0], i0 = iv[0];

			for (i = 0; i < n; i++) {
				p0 += i0;
				dst[i] = (char)
				    (((long)w0[(p0 >> 16) & 0xff] - 128) >> 2);
			}
			pv[0] = p0;
			break;
		}
		case 2: {
			const unsigned char *w0 = wv[0], *w1 = wv[1];
			unsigned long p0 = pv[0], i0 = iv[0];
			unsigned long p1 = pv[1], i1 = iv[1];

			for (i = 0; i < n; i++) {
				long u;

				p0 += i0; p1 += i1;
				u  = (long)w0[(p0 >> 16) & 0xff]
				   + (long)w1[(p1 >> 16) & 0xff];
				dst[i] = (char)((u - 256) >> 2);
			}
			pv[0] = p0; pv[1] = p1;
			break;
		}
		case 3: {
			const unsigned char *w0 = wv[0], *w1 = wv[1], *w2 = wv[2];
			unsigned long p0 = pv[0], i0 = iv[0];
			unsigned long p1 = pv[1], i1 = iv[1];
			unsigned long p2 = pv[2], i2 = iv[2];

			for (i = 0; i < n; i++) {
				long u;

				p0 += i0; p1 += i1; p2 += i2;
				u  = (long)w0[(p0 >> 16) & 0xff]
				   + (long)w1[(p1 >> 16) & 0xff]
				   + (long)w2[(p2 >> 16) & 0xff];
				dst[i] = (char)((u - 384) >> 2);
			}
			pv[0] = p0; pv[1] = p1; pv[2] = p2;
			break;
		}
		default: {
			const unsigned char *w0 = wv[0], *w1 = wv[1];
			const unsigned char *w2 = wv[2], *w3 = wv[3];
			unsigned long p0 = pv[0], i0 = iv[0];
			unsigned long p1 = pv[1], i1 = iv[1];
			unsigned long p2 = pv[2], i2 = iv[2];
			unsigned long p3 = pv[3], i3 = iv[3];

			for (i = 0; i < n; i++) {
				long u;

				p0 += i0; p1 += i1; p2 += i2; p3 += i3;
				u  = (long)w0[(p0 >> 16) & 0xff]
				   + (long)w1[(p1 >> 16) & 0xff]
				   + (long)w2[(p2 >> 16) & 0xff]
				   + (long)w3[(p3 >> 16) & 0xff];
				dst[i] = (char)((u - 512) >> 2);
			}
			pv[0] = p0; pv[1] = p1; pv[2] = p2; pv[3] = p3;
			break;
		}
		}
	} else {
		/* tone and/or effect present — the general mix, same order and
		 * single final clamp as the original */
		for (i = 0; i < n; i++) {
			long acc = 0;
			int  k;

			for (k = 0; k < nv; k++) {
				pv[k] += iv[k];
				acc += (long)wv[k][(pv[k] >> 16) & 0xff] - 128;
			}
			if (nv)
				acc >>= 2;
			if (g_tone_left > 0) {          /* swMode square wave */
				g_tone_phase += g_tone_inc;
				acc += ((g_tone_phase & 0x80000000UL)
				        ? g_tone_amp : -g_tone_amp) >> 1;
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

	for (v = 0; v < nv; v++)
		g_ft_phase[ix[v]] = pv[v];
}

void mix_reset_phases(void)
{
	int v;

	for (v = 0; v < 4; v++)
		g_ft_phase[v] = 0;
}

void mix_set_record(const void *ftsoundrec)
{
	/* The record is LIVE - the sequencer rewrites its rate fields while it
	 * plays - so keep the pointer, never a copy. */
	g_ft_rec = (const unsigned char *)ftsoundrec;
}

void mix_tone_off(void)
{
	g_tone_left = 0;
}

void mix_tone(int count, int amp, int duration_ticks)
{
	if (count <= 0) {
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

/* An effect becomes another voice in the loop (see the divergence note
 * above): resample it to the loop's rate and let the refill mix it in. */
void mix_effect(const signed char *samples, long count, int rate_hz)
{
	long i;

	unsigned long sstep = ((unsigned long)rate_hz << 16) / (unsigned long)SYNTH_HZ;
	unsigned long spos  = 0;
	long          n     = (long)(((unsigned long)count
	                              * (unsigned long)SYNTH_HZ)
	                             / (unsigned long)rate_hz);

	/* Disarm the effect BEFORE overwriting its buffer. The resample
	 * loop below takes tens of milliseconds on a 68000-class machine
	 * (up to 24576 interpolated samples), and the sound VBL keeps
	 * firing throughout: with the old effect still armed it mixed from
	 * a buffer this loop was half-way through rewriting — a burst of
	 * mismatched samples right before the new sound, i.e. the "weird
	 * blips at the beginning" reported on the Mega STe. The Falcon030
	 * fills fast enough that the window almost never hit. len is the
	 * gate the mixer tests, so clearing it first makes the fill safe;
	 * it is re-set (last) once the buffer is complete. */
	g_sfx_len = 0;
	g_sfx_pos = 0;

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
}

void mix_effect_stop(void)
{
	g_sfx_len = 0;
	g_sfx_pos = 0;
}

int mix_effect_playing(void)
{
	return (g_sfx_pos < g_sfx_len) ? 1 : 0;
}
