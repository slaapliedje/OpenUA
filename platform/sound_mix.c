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
static volatile int          g_ft_mute;         /* MIDI carries the music: the
                                                 * four voices stay silent     */
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
	const unsigned char *rec = g_ft_mute ? NULL : (const unsigned char *)g_ft_rec;
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
	const unsigned char *rec = g_ft_mute ? NULL : (const unsigned char *)g_ft_rec;
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

/* Stereo: the same voices, tone and effect as mix_render, as interleaved
 * left/right frames. The Mac synth is mono; here voice 0 leans left and
 * voice 1 right (3:1), voices 2 and 3 sit in the middle, and the tone and
 * the effect are centred. The weights on each side add up to the mono
 * mix's, so the music alone still cannot clip, and the pan is adds and
 * shifts (multiplies per sample cost a 68030 too much). Inactive voices
 * read a flat wave rather than being compacted out: they have places. */
static const unsigned char k_mid = 128;

void mix_render_stereo(char *dst, long n)
{
	const unsigned char *rec = g_ft_mute ? NULL : (const unsigned char *)g_ft_rec;
	const unsigned char *w0, *w1, *w2, *w3, *wv[4];
	unsigned long iv[4], pv[4], p0, p1, p2, p3, i0, i1, i2, i3;
	int on[4], v, fx;
	long i;

	for (v = 0; v < 4; v++) {
		unsigned long rate = 0, inc = 0;
		const unsigned char *w = NULL;

		if (rec != NULL) {
			rate = rd_be32_p(rec + FT_RATE(v));
			w    = (const unsigned char *)(uintptr_t)rd_be32_p(rec + FT_WAVE(v));
		}
		if (rate != 0 && w != NULL)
			inc = (unsigned long)(((unsigned long long)rate
			                       * (unsigned long long)MAC_SYNTH_HZ)
			                      / (unsigned long long)SYNTH_HZ);
		on[v] = inc != 0;
		wv[v] = on[v] ? w : &k_mid;
		iv[v] = inc;
		pv[v] = on[v] ? g_ft_phase[v] : 0;
	}
	w0 = wv[0]; w1 = wv[1]; w2 = wv[2]; w3 = wv[3];
	p0 = pv[0]; p1 = pv[1]; p2 = pv[2]; p3 = pv[3];
	i0 = iv[0]; i1 = iv[1]; i2 = iv[2]; i3 = iv[3];
	fx = g_tone_left > 0 || g_sfx_pos < g_sfx_len;

	for (i = 0; i < n; i++) {
		long a, b, m, l, r;

		p0 += i0; p1 += i1; p2 += i2; p3 += i3;
		a = (long)w0[(p0 >> 16) & 0xff] - 128;
		b = (long)w1[(p1 >> 16) & 0xff] - 128;
		m = ((long)w2[(p2 >> 16) & 0xff] + (long)w3[(p3 >> 16) & 0xff] - 256) << 1;
		l = (a + (a << 1) + b + m) >> 3;
		r = (a + b + (b << 1) + m) >> 3;
		if (fx) {
			long c = 0;

			if (g_tone_left > 0) {
				g_tone_phase += g_tone_inc;
				c += ((g_tone_phase & 0x80000000UL)
				      ? g_tone_amp : -g_tone_amp) >> 1;
				g_tone_left--;
			}
			if (g_sfx_pos < g_sfx_len)
				c += g_sfx_buf[g_sfx_pos++];
			l += c;
			r += c;
			l = l > 127 ? 127 : l < -128 ? -128 : l;
			r = r > 127 ? 127 : r < -128 ? -128 : r;
		}
		dst[2 * i] = (char)l;
		dst[2 * i + 1] = (char)r;
	}
	if (on[0]) g_ft_phase[0] = p0;
	if (on[1]) g_ft_phase[1] = p1;
	if (on[2]) g_ft_phase[2] = p2;
	if (on[3]) g_ft_phase[3] = p3;
}

void mix_reset_phases(void)
{
	int v;

	for (v = 0; v < 4; v++)
		g_ft_phase[v] = 0;
}

/* The MIDI player (midi_seq.c) takes the music over: the engine keeps
 * writing its live record, nothing of it is rendered. Tone and effects are
 * unaffected. */
void mix_music_mute(int mute)
{
	g_ft_mute = mute;
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

/* --- the high-pass filter (see sound_mix.h) --------------------------------
 *
 * Each stage is y[n] = a * (y[n-1] + x[n] - x[n-1]) with a = 1 - 2^-k, so
 * the multiply is a shift: the corner is fs / (2 pi 2^k), the power of two
 * nearest the frequency asked for (at 25033 Hz: k = 6 is 62 Hz, 7 is 31 Hz).
 * A multiplying version cost three times the whole mixer on the TT. The
 * samples are scaled up by 2^8 to keep the shifts' rounding out of the way. */
static int  g_hp_k;                     /* 0 = off */
static long g_hp[2][4];                 /* per channel: x1, y1, x2, y2 */

void mix_set_highpass(int hz)
{
	long f;
	int c, j;

	g_hp_k = 0;
	for (c = 0; c < 2; c++)
		for (j = 0; j < 4; j++)
			g_hp[c][j] = 0;
	if (hz <= 0)
		return;
	/* the k whose corner mix_hz * 113 / (710 << k) is nearest hz, i.e.
	 * the first whose corner falls below hz * sqrt(2) */
	for (g_hp_k = 1; g_hp_k < 12; g_hp_k++) {
		f = (mix_hz * 113L / 710) >> g_hp_k;
		if (f * 1000 < hz * 1414L)
			break;
	}
}

/* n samples, every step-th from buf, through channel c's filter */
static void highpass(char *buf, long n, int step, long *st)
{
	int  k = g_hp_k;
	long x1 = st[0], y1 = st[1], x2 = st[2], y2 = st[3];
	long i, x, v;

	for (i = 0; i < n; i++, buf += step) {
		x  = (long)(signed char)*buf << 8;
		v  = y1 + x - x1;
		y1 = v - (v >> k);
		x1 = x;
		v  = y2 + y1 - x2;
		y2 = v - (v >> k);
		x2 = y1;
		v  = y2 >> 8;
		*buf = (char)(v > 127 ? 127 : v < -128 ? -128 : v);
	}
	st[0] = x1; st[1] = y1; st[2] = x2; st[3] = y2;
}

void mix_highpass(char *buf, long n)
{
	if (g_hp_k != 0)
		highpass(buf, n, 1, g_hp[0]);
}

void mix_highpass_stereo(char *buf, long n)
{
	if (g_hp_k != 0) {
		highpass(buf, n, 2, g_hp[0]);
		highpass(buf + 1, n, 2, g_hp[1]);
	}
}
