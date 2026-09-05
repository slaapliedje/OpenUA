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
#include "dbglog.h"

#ifdef FRUA_AMIGA

#include <exec/types.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <hardware/dmabits.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <devices/ahi.h>

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

static unsigned long bard_tod(void);
/* Exact Paula consumption per vblank in 24.8 fixed point, at SYNTH_PER:
 * PAL:  227.5 CCK/line x 312.5 lines / 156 = 455.729 -> x256 = 116667
 * NTSC: 227.5 CCK/line x 262.5 lines / 156 = 382.812 -> x256 =  98000 */
static unsigned long g_play_step = 116667UL;
static unsigned long g_play_acc;
static unsigned long g_play_tod;

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

/* #116: samples of silence already committed to the ring — see the gate in
 * plat_sound_vbl. At >= RING_SAMPLES the whole ring is zeroes and there is
 * nothing left to write. */
static volatile long         g_quiet_run;

static unsigned long rd_be32_p(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16)
	     | ((unsigned long)p[2] << 8)  |  (unsigned long)p[3];
}

/* Is anything actually going to make a sound this frame? Cheap enough to run
 * every vblank: four rate/wave pairs plus two counters. Mirrors the Falcon
 * backend's synth_audible() exactly — same record, same fields. */
static int synth_audible(void)
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

/* Render `n` samples of (4 wavetable voices + square tone + effect) into
 * dst — the same mixer as the Falcon backend, at Paula's ring rate.
 *
 * ★ REWRITTEN FOR THE 68000 — measured at 773 beam lines = 2.47 FRAMES per
 * 454-sample fill (~770 cycles/sample) in its Falcon-identical form, which
 * is the entire "VBL starvation" (39551e5e): the per-sample loop did four
 * read-modify-writes of g_ft_phase[] through absolute addresses, four
 * inc==0 tests, and tone/sfx tests, every sample. The shape now:
 *
 *  - active voices COMPACTED once per call; phases live in locals for the
 *    duration and write back at the end — no global traffic per sample;
 *  - wavetable bytes summed UNSIGNED with the -128-per-voice bias folded
 *    into the final shift: (sum - 128*nv) >> 2 == sum of (w-128) >> 2;
 *  - the steady state (music, no tone, no sfx) gets per-voice-count
 *    specializations with everything register-resident and NO clamp —
 *    after the bias and >>2 the result provably fits -128..127
 *    (max 4*255-512 = 508 -> 127; min -512 -> -128);
 *  - tone/sfx frames take the general path, same order and single final
 *    clamp as before — output is byte-identical to the old mixer. */
static void synth_render(signed char *dst, long n)
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
		w    = (const unsigned char *)rd_be32_p(rec + FT_WAVE(v));
		if (rate == 0 || w == NULL)
			continue;
		/* The Fixed rate steps the wave at the MAC's sample rate; the
		 * ring clocks SYNTH_HZ, so rescale or every note transposes. */
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
				dst[i] = (signed char)
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
				dst[i] = (signed char)((u - 256) >> 2);
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
				dst[i] = (signed char)((u - 384) >> 2);
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
				dst[i] = (signed char)((u - 512) >> 2);
			}
			pv[0] = p0; pv[1] = p1; pv[2] = p2; pv[3] = p3;
			break;
		}
		}
	} else {
		/* Tone and/or effect present — the general mix.
		 *
		 * HEADROOM. The voices alone already fill the sample: four of
		 * them sum to +-512 and `>>2` lands on exactly +-128. Adding a
		 * full-scale effect (+-128) or beep (g_tone_amp is amp & 0xff,
		 * so +-255 before its shift) on TOP of that reached +-256 and
		 * hit the clamp below hard, on every loud sample — heard as
		 * gritty overload whenever an effect played over music, while
		 * music alone (the fast path above) stayed clean. That matched
		 * a capture off real hardware: peaks pinned at the clamp only
		 * while effects were sounding.
		 *
		 * So this path now DUCKS: the voices give up 6 dB while
		 * something rides on top, and the rider is scaled to fit the
		 * budget it just freed. The two common pairings land exactly on
		 * full scale with no clipping at all:
		 *
		 *      voices >>3  +-64  +  effect >>1  +-64  = +-128
		 *      voices >>3  +-64  +  tone   >>2  +-63  = +-127
		 *
		 * Music-only playback is untouched — it never enters this path —
		 * so the level only moves while an effect or beep is actually
		 * sounding, which is the usual game-audio ducking behaviour.
		 * Tone AND effect together can still reach the clamp, but that
		 * combination is rare and the clamp remains as the safety net.
		 *
		 * NOTE this is a deliberate divergence from the Mac original,
		 * which clamped without ducking. Restoring the old behaviour is
		 * a matter of putting these three shifts back to 2 / 1 / 0. */
		for (i = 0; i < n; i++) {
			long acc = 0;
			int  k;

			for (k = 0; k < nv; k++) {
				pv[k] += iv[k];
				acc += (long)wv[k][(pv[k] >> 16) & 0xff] - 128;
			}
			if (nv)
				acc >>= 3;              /* was 2 — 6 dB of headroom */
			if (g_tone_left > 0) {          /* swMode square wave */
				g_tone_phase += g_tone_inc;
				acc += ((g_tone_phase & 0x80000000UL)
				        ? g_tone_amp : -g_tone_amp) >> 2;
				g_tone_left--;
			}
			if (g_sfx_pos < g_sfx_len)      /* the effect rides on top */
				acc += g_sfx_buf[g_sfx_pos++] >> 1;

			if (acc > 127)
				acc = 127;
			else if (acc < -128)
				acc = -128;
			dst[i] = (signed char)acc;
		}
	}

	for (v = 0; v < nv; v++)
		g_ft_phase[ix[v]] = pv[v];
}

/* --- AHI BACKEND -----------------------------------------------------------
 *
 * Paula is 8-bit and hard-pans its channels; ahi.device is the standard Amiga
 * audio API and, on an Apollo/Vampire, its driver reaches the SAGA audio core
 * ("Arne": 16 channels, 8/16-bit, rates to 56 kHz, per-channel panning, 24-bit
 * internal mixing). Arne's REGISTERS ARE NOT PUBLICLY DOCUMENTED — the Apollo
 * wiki publishes the feature list but no register map, and the SAGA register
 * reference has no audio entries at all — so talking to it directly is not
 * possible from published information. AHI is the supported route, and it also
 * works on any stock machine with AHI installed, so this is one backend rather
 * than a Vampire special case.
 *
 * We use AHI's DEVICE api (OpenDevice + CMD_WRITE on an AHIRequest, double
 * buffered via ahir_Link), not the low-level api: it needs no library base, no
 * inlines and no link library — just the vendored devices/ahi.h.
 *
 * The device api wants a PROCESS (WaitIO blocks), while the Paula path renders
 * from the VBL. So when AHI is in use a small process owns rendering: it calls
 * synth_render() straight into its own double buffers and hands them to AHI,
 * and plat_sound_vbl skips the ring refill entirely (it still runs the engine
 * sequencer hook — that is what arms the voices). Paula is left completely
 * alone: no DMA, no ring, no LED filter change.
 *
 * Falls back to Paula whenever ahi.device is missing or refuses to open, which
 * is the common case on a stock Workbench without AHI installed.
 */

#define AHI_BUF_SAMPLES  1024           /* ~45 ms at SYNTH_HZ, x2 buffers */

static volatile int  g_use_ahi;         /* 1 = AHI owns the output */
static volatile int  g_ahi_run;         /* player process: keep going */
static volatile int  g_ahi_state;       /* 0 starting, 1 playing, -1 failed */
static volatile int  g_ahi_done;        /* player has exited */

static void ahi_player(void)
{
	struct MsgPort    *mp   = NULL;
	struct AHIRequest *req[2] = { NULL, NULL };
	struct AHIRequest *link = NULL;
	signed char       *buf[2] = { NULL, NULL };
	BYTE               opened = -1;
	int                cur = 0;

	mp = CreateMsgPort();
	if (mp != NULL)
		req[0] = (struct AHIRequest *)
		         CreateIORequest(mp, sizeof(struct AHIRequest));
	if (req[0] != NULL) {
		req[0]->ahir_Version = 4;
		opened = OpenDevice((CONST_STRPTR)AHINAME, 0,
		                    (struct IORequest *)req[0], 0);
	}
	if (opened == 0) {
		/* the second request is a COPY of the opened one, per the AHI
		 * developer example — do not OpenDevice twice */
		req[1] = AllocMem(sizeof(struct AHIRequest), MEMF_ANY);
		if (req[1] != NULL)
			CopyMem(req[0], req[1], sizeof(struct AHIRequest));
		buf[0] = AllocMem(AHI_BUF_SAMPLES, MEMF_ANY | MEMF_CLEAR);
		buf[1] = AllocMem(AHI_BUF_SAMPLES, MEMF_ANY | MEMF_CLEAR);
	}
	if (opened != 0 || req[1] == NULL || buf[0] == NULL || buf[1] == NULL) {
		g_ahi_state = -1;
		goto cleanup;
	}

	g_ahi_state = 1;
	while (g_ahi_run) {
		struct AHIRequest *r = req[cur];

		synth_render(buf[cur], AHI_BUF_SAMPLES);

		r->ahir_Std.io_Command = CMD_WRITE;
		r->ahir_Std.io_Data    = buf[cur];
		r->ahir_Std.io_Length  = AHI_BUF_SAMPLES;
		r->ahir_Std.io_Offset  = 0;
		r->ahir_Frequency      = SYNTH_HZ;
		r->ahir_Type           = AHIST_M8S;     /* what synth_render makes */
		r->ahir_Volume         = 0x10000;       /* unity */
		r->ahir_Position       = 0x8000;        /* CENTRED — the whole point */
		r->ahir_Link           = link;
		SendIO((struct IORequest *)r);

		/* Wait for the PREVIOUS buffer, so exactly one is always queued
		 * ahead: that is what keeps playback gapless. */
		if (link != NULL)
			WaitIO((struct IORequest *)link);
		link = r;
		cur ^= 1;
	}

	/* only `link` can still be queued: every other request was waited on
	 * inside the loop before being reused */
	if (link != NULL) {
		AbortIO((struct IORequest *)link);
		WaitIO((struct IORequest *)link);
	}

cleanup:
	if (opened == 0)
		CloseDevice((struct IORequest *)req[0]);
	if (buf[1] != NULL) FreeMem(buf[1], AHI_BUF_SAMPLES);
	if (buf[0] != NULL) FreeMem(buf[0], AHI_BUF_SAMPLES);
	if (req[1] != NULL) FreeMem(req[1], sizeof(struct AHIRequest));
	if (req[0] != NULL) DeleteIORequest((struct IORequest *)req[0]);
	if (mp != NULL)     DeleteMsgPort(mp);
	g_ahi_done = 1;
}

/* Start the AHI player. Returns 0 if AHI is driving the output, -1 to fall
 * back to Paula. Runs from task context (plat_sound_init). */
static int ahi_start(void)
{
	int spin;

	g_ahi_run   = 1;
	g_ahi_state = 0;
	g_ahi_done  = 0;

	{
		/* CreateNewProc takes a TagItem ARRAY in this NDK (the varargs
		 * spelling is CreateNewProcTags) */
		struct TagItem tags[5];

		tags[0].ti_Tag = NP_Entry;     tags[0].ti_Data = (ULONG)ahi_player;
		tags[1].ti_Tag = NP_Name;      tags[1].ti_Data = (ULONG)"OpenUA AHI";
		tags[2].ti_Tag = NP_StackSize; tags[2].ti_Data = 16384;
		tags[3].ti_Tag = NP_Priority;  tags[3].ti_Data = 5;
		tags[4].ti_Tag = TAG_DONE;     tags[4].ti_Data = 0;

		if (CreateNewProc(tags) == NULL) {
			g_ahi_run = 0;
			return -1;
		}
	}

	/* the process reports back within a few ticks; Delay() is legal here
	 * (task context) and costs nothing on the failure path */
	for (spin = 0; spin < 50 && g_ahi_state == 0; spin++)
		Delay(1);

	if (g_ahi_state != 1) {
		g_ahi_run = 0;
		for (spin = 0; spin < 50 && !g_ahi_done; spin++)
			Delay(1);
		return -1;
	}
	return 0;
}

static void ahi_stop(void)
{
	int spin;

	if (!g_use_ahi)
		return;
	g_ahi_run = 0;
	for (spin = 0; spin < 100 && !g_ahi_done; spin++)
		Delay(1);
	g_use_ahi = 0;
}

int plat_sound_init(void)
{
	int v;

	if (g_inited)
		return 0;

	for (v = 0; v < 4; v++)
		g_ft_phase[v] = 0;

	/* Prefer AHI: 16-bit-capable, properly stereo, and on an Apollo it
	 * reaches the SAGA audio core. Paula is the fallback when ahi.device
	 * is absent or will not open — the common case on a stock Workbench. */
	if (ahi_start() == 0) {
		g_use_ahi   = 1;
		g_ring_live = 1;
		g_quiet_run = 0;
		g_inited    = 1;
		/* SAY WHICH BACKEND RAN. Both paths now produce CENTRED audio —
		 * AHI by mixing, Paula by driving AUD0+AUD1 — so a listener on
		 * hardware cannot tell them apart, and "the sound is centred
		 * now" would confirm nothing about AHI. This line is the only
		 * thing that separates "AHI opened" from "AHI was absent and
		 * the Paula fix carried it". */
		dbg_log("snd: AHI backend up (ahi.device)");
		return 0;
	}
	dbg_log("snd: no AHI - Paula fallback (AUD0+AUD1 centred)");

	g_ring = AllocMem(RING_SAMPLES, MEMF_CHIP | MEMF_CLEAR);
	if (g_ring == NULL)
		return -1;

	g_ring_w    = RING_SAMPLES / 2;         /* start half a ring ahead */
	g_ring_play = 0;
	g_quiet_run = 0;                        /* #116: render from cold */
	for (v = 0; v < 4; v++)
		g_ft_phase[v] = 0;

	/* Program channels 0 and 1 once; Paula reloads LC/LEN itself at the end
	 * of the buffer — a hardware ring. Kill any modulation linkage first.
	 *
	 * BOTH channels play the SAME ring: Paula hard-pans 0/3 left and 1/2
	 * right, so driving only AUD0 put the entire engine mix in the left
	 * speaker (measured on captured hardware output: 18 dB left of right).
	 * The Mac original is mono, so the faithful rendering is mono CENTRED,
	 * i.e. the same samples in both channels.
	 *
	 * They must be started by ONE dmacon write with identical LC/LEN/PER so
	 * they run in lockstep. Enabling them separately would leave the right
	 * channel at a different ring offset — up to a full ring, 160 ms — which
	 * would be heard as an echo rather than a centred image. */
	CUSTOM->dmacon = (UWORD)(DMAF_AUD0 | DMAF_AUD1);/* clear while we set up */
	CUSTOM->adkcon = 0x00FF;                        /* clear all AM/FM links */
	CUSTOM->aud[0].ac_ptr = (UWORD *)g_ring;
	CUSTOM->aud[0].ac_len = (UWORD)(RING_SAMPLES / 2);      /* words */
	CUSTOM->aud[0].ac_per = SYNTH_PER;
	CUSTOM->aud[0].ac_vol = 64;
	CUSTOM->aud[1].ac_ptr = (UWORD *)g_ring;
	CUSTOM->aud[1].ac_len = (UWORD)(RING_SAMPLES / 2);
	CUSTOM->aud[1].ac_per = SYNTH_PER;
	CUSTOM->aud[1].ac_vol = 64;
	CUSTOM->dmacon = (UWORD)(DMAF_SETCLR | DMAF_MASTER
	                         | DMAF_AUD0 | DMAF_AUD1);

	/* LED off = the 3.2kHz low-pass filter off (see the header note). */
	g_saved_led = (UBYTE)(CIAA->ciapra & CIAF_LED);
	CIAA->ciapra |= CIAF_LED;

	{
		extern struct ExecBase *SysBase;

		if (SysBase->VBlankFrequency == 60)
			g_play_step = 98000UL;  /* NTSC (see the table above) */
	}
	g_play_tod  = bard_tod();
	g_ring_live = 1;
	g_inited    = 1;
	return 0;
}


/* --- the campfire bard (the first-load loading tune) ----------------------
 *
 * Plays while the palette quantiser runs — the "please wait" screen's music.
 * The engine's own audio CANNOT cover this window: its notes come from the
 * sequencer in the MAINLINE, which is busy doing the median cuts, so the VBL
 * ring faithfully renders whatever chord was sounding when the cut began — a
 * 20-second frozen drone ("something struggling to not die", as it was
 * reported from the room it leaked into). So while the bard plays, AUD0 (the
 * engine ring) is muted and AUD1/AUD2 — which the engine never uses; the
 * whole Mac synth is software-mixed onto channel 0 — carry the tune.
 *
 * Entirely VBL-driven and DMA-fed, so it is immune to the very stall it
 * decorates: Paula loops the waveforms by itself; the VBL only retunes
 * ac_per and walks the pluck envelope down ac_vol. Zero mainline work.
 *
 * The instruments are SYNTHESIZED at start (32-byte triangle loop — chip-lute
 * — in CHIP RAM), the melody is an ORIGINAL sixteen bars in D dorian over a
 * fifth drone: nothing shipped, nothing licensed. Sparse on purpose: one
 * melody voice, one drone, plucks left to ring. */
#define BARD_WAVE_LEN 32
static signed char *g_bard_wave;        /* CHIP: one triangle cycle          */
static short        g_bard_on;
static short        g_bard_step;        /* index into the tune               */
static short        g_bard_frames;      /* frames left in the current step   */
static short        g_bard_vol;         /* pluck envelope, 64 -> 0           */
static short        g_bard_atk;         /* attack ramp frames left           */
static short        g_bass_step, g_bass_frames, g_bass_vol, g_bass_atk;
static long         g_bard_calls;       /* bard_vbl invocations this window  */
static long         g_bard_gap[6];      /* arrival-gap histogram, TOD ticks  */
static long         g_bard_gapmax;      /* largest gap seen this window      */
static long         g_bard_front0, g_bard_tail0;  /* chain-bracket snapshots */
static long         g_prof_fill_calls, g_prof_fill_lines;  /* refill cost   */
static long         g_prof_hook_calls, g_prof_hook_lines;  /* sequencer cost*/
/* Handler-cost instrumentation (mainline-read): total beam lines spent inside
 * plat_sound_vbl and the call count, so avg lines/call ~ handler cost. A PAL
 * frame is 313 lines: an average NEAR 313 means the handler costs a whole
 * frame and the starvation is SELF-inflicted. */
static long g_vblprof_lines, g_vblprof_calls;

static unsigned long vbl_beamline(void)
{
	unsigned long vp = *(volatile unsigned long *)0xDFF004UL;

	return (((vp >> 16) & 1UL) << 8) + ((vp >> 8) & 0xFFUL);
}

static unsigned long g_bard_tod;         /* last CIA-A TOD reading            */
static unsigned long g_bard_tod0;        /* window start, for WALL duration   */
static unsigned long bard_tod(void);
static UWORD        g_bard_vol0;        /* AUD0 volume to restore            */

/* Paula period for a BARD_WAVE_LEN loop: 3546895 / (freq * 32), PAL.
 * D dorian: D3 755  E3 672  F3 635  G3 566  A3 504  B3 449  C4 424  D4 377. */
#define BN_D3 755
#define BN_E3 672
#define BN_F3 635
#define BN_G3 566
#define BN_A3 504
#define BN_B3 449
#define BN_C4 424
#define BN_D4 377
#define BN_FS3 599
#define BN_E4 336
#define BN_FS4 300
#define BN_G4 283
#define BN_A4 252
#define BN_REST 0                       /* no retrigger; the last pluck rings */

/* {period, frames} — PAL frames, quarter ~= 33 (~90 bpm, campfire pace). */
static const short g_bard_tune[][2] = {
	/* D mixolydian, ~170 bpm — the somber dorian ballad read as a dirge
	 * ("shouldn't it be a bit more upbeat and adventurous?"). Rising
	 * contours, dotted rhythms, lands on the octave. */
	{ BN_D4, 18 },  { BN_D4, 9 },   { BN_E4, 9 },  { BN_FS4, 18 },
	{ BN_G4, 9 },   { BN_FS4, 9 },  { BN_E4, 18 }, { BN_FS4, 9 },
	{ BN_G4, 9 },   { BN_A4, 27 },  { BN_REST, 9 },
	{ BN_A4, 9 },   { BN_G4, 9 },   { BN_FS4, 9 }, { BN_E4, 9 },
	{ BN_FS4, 18 }, { BN_D4, 18 },  { BN_E4, 9 },  { BN_FS4, 9 },
	{ BN_E4, 9 },   { BN_C4, 9 },   { BN_D4, 27 }, { BN_REST, 9 },
	{ BN_D4, 9 },   { BN_FS4, 9 },  { BN_A4, 18 }, { BN_B3, 9 },
	{ BN_C4, 9 },   { BN_A3, 18 },  { BN_G3, 9 },  { BN_A3, 9 },
	{ BN_D4, 36 },  { BN_REST, 18 },
};
#define BARD_STEPS (short)(sizeof g_bard_tune / sizeof g_bard_tune[0])

/* The ground bass: roots and fifths below the melody, one slow loop.
 * A2 1008  G2 1131  C3 847  D3 755. */
static const short g_bard_bass[][2] = {
	{ 755, 36 }, { 1008, 36 }, { 755, 36 }, { 1131, 36 },
	{ 847, 36 }, { 1008, 36 }, { 755, 36 }, { 755, 36 },
};
#define BARD_BASS_STEPS (short)(sizeof g_bard_bass / sizeof g_bard_bass[0])

void plat_bard_start(void)
{
	short i;

	if (!g_inited || g_bard_on)
		return;
	if (g_use_ahi)
		return;         /* Paula-only tune; AHI owns the audio hardware */
	if (g_bard_wave == NULL) {
		g_bard_wave = AllocMem(BARD_WAVE_LEN, MEMF_CHIP);
		if (g_bard_wave == NULL)
			return;
		/* one triangle cycle, FULL SCALE -120..+120 and back: the first
		 * tavern request. +-60 left 6 dB on the table — Paula's volume
		 * register tops out at 64, so loudness lives in the WAVEFORM.
		 * 240 across 16 samples = 16 per step, peaks at exactly +-120,
		 * safely inside signed char. */
		for (i = 0; i < BARD_WAVE_LEN; i++)
			g_bard_wave[i] = (signed char)
			    (i < 16 ? i * 16 - 120 : 120 - (i - 16) * 16);
	}
	g_bard_vol0 = 64;
	/* mute the frozen engine chord — BOTH ring channels now (AUD1 is the
	 * right half of the centred mix; the bard borrows it below) */
	CUSTOM->aud[0].ac_vol = 0;
	CUSTOM->aud[1].ac_vol = 0;
	/* melody: AUD1.  drone: AUD2, a soft D3 that just loops. */
	CUSTOM->dmacon = (UWORD)(DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3);
	CUSTOM->aud[1].ac_ptr = (UWORD *)g_bard_wave;
	CUSTOM->aud[1].ac_len = BARD_WAVE_LEN / 2;
	CUSTOM->aud[1].ac_per = BN_D4;
	CUSTOM->aud[1].ac_vol = 0;
	CUSTOM->aud[2].ac_ptr = (UWORD *)g_bard_wave;
	CUSTOM->aud[2].ac_len = BARD_WAVE_LEN / 2;
	CUSTOM->aud[2].ac_per = BN_D3;
	CUSTOM->aud[2].ac_vol = 14;             /* drone present, not a pedal */
	CUSTOM->aud[3].ac_ptr = (UWORD *)g_bard_wave;
	CUSTOM->aud[3].ac_len = BARD_WAVE_LEN / 2;
	CUSTOM->aud[3].ac_per = 755;
	CUSTOM->aud[3].ac_vol = 0;
	CUSTOM->dmacon = (UWORD)(DMAF_SETCLR | DMAF_MASTER | DMAF_AUD1
	                         | DMAF_AUD2 | DMAF_AUD3);
	g_bard_tod    = bard_tod();
	g_bard_tod0   = g_bard_tod;
	{
		extern long g_vbl_front_ticks, g_vbl_tail_ticks;

		g_bard_front0 = g_vbl_front_ticks;
		g_bard_tail0  = g_vbl_tail_ticks;
	}
	g_bard_step   = 0;
	g_bass_step   = 0;
	g_bass_frames = 0;
	g_bass_vol    = 0;
	g_bard_frames = 0;
	g_bard_vol    = 0;
	g_bard_on     = 1;
}

void plat_bard_stop(void)
{
	extern void dbg_log_num(const char *, long);

	if (!g_bard_on)
		return;
	/* ★ THE TEMPO COMPLAINT WAS QUANTITATIVE. The user heard the tavern cut
	 * as slow with a tick; the RMS profile showed pluck cycles ~4 s apart
	 * against a table written for ~1 s — so before retuning any music, log
	 * how often this window's bard_vbl ACTUALLY ran. If it is well under
	 * 50/s, the VBL is being starved during the quant and the tune tables
	 * are innocent. */
	dbg_log_num("bard: vbl calls this window = ", g_bard_calls);
	/* ★ WALL TIME, from hardware: every rate claim in this hunt so far divided
	 * by an rl-clock duration, and the rl clock is driven by the very server
	 * being starved. TOD ticks at 50 Hz off the power supply regardless, so
	 * calls vs ticks is the true service ratio, and ticks/50 is the true
	 * quant wall time -- the first uncorrupted duration measured this week. */
	dbg_log_num("bard: window TOD ticks (50Hz)= ",
	            (long)((bard_tod() - g_bard_tod0) & 0xFFFFFFUL));
	dbg_log_num("bard: handler calls          = ", g_vblprof_calls);
	dbg_log_num("bard: handler beam lines     = ", g_vblprof_lines);
	dbg_log_num("bard: refill calls / lines   = ", g_prof_fill_calls);
	dbg_log_num("bard:              lines     = ", g_prof_fill_lines);
	dbg_log_num("bard: hook   calls / lines   = ", g_prof_hook_calls);
	dbg_log_num("bard:              lines     = ", g_prof_hook_lines);
	g_prof_fill_calls = 0; g_prof_fill_lines = 0;
	g_prof_hook_calls = 0; g_prof_hook_lines = 0;
	{
		short i;

		for (i = 0; i < 6; i++) {
			static const char *nm[6] = {
				"bard: gap 0 ticks x ", "bard: gap 1 tick  x ",
				"bard: gap 2 ticks x ", "bard: gap 3 ticks x ",
				"bard: gap 4 ticks x ", "bard: gap >=5     x " };

			dbg_log_num(nm[i], g_bard_gap[i]);
			g_bard_gap[i] = 0;
		}
		dbg_log_num("bard: gap max     = ", g_bard_gapmax);
		g_bard_gapmax = 0;
	}
	{
		extern long g_vbl_front_ticks, g_vbl_tail_ticks;

		dbg_log_num("bard: chain FRONT calls  = ",
		            g_vbl_front_ticks - g_bard_front0);
		dbg_log_num("bard: chain TAIL calls   = ",
		            g_vbl_tail_ticks - g_bard_tail0);
	}
	g_vblprof_calls = 0; g_vblprof_lines = 0;
	g_bard_calls = 0;
	CUSTOM->dmacon = (UWORD)(DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3);
	CUSTOM->aud[2].ac_vol = 0;
	CUSTOM->aud[3].ac_vol = 0;

	/* The bard stole AUD1, so the right half of the centred mix has to be
	 * re-seated. Restart BOTH ring channels from the top of the buffer with
	 * one dmacon write: re-enabling AUD1 alone would leave it at a different
	 * ring offset from the still-running AUD0 and turn the centred image
	 * into an echo. Engine audio was muted throughout the bard, so
	 * restarting AUD0 here is inaudible.
	 *
	 * Restarting the DMA means the play cursor really is back at 0, so the
	 * model is reset to match rather than carried over. */
	CUSTOM->dmacon = (UWORD)(DMAF_AUD0 | DMAF_AUD1);
	CUSTOM->aud[0].ac_ptr = (UWORD *)g_ring;
	CUSTOM->aud[0].ac_len = (UWORD)(RING_SAMPLES / 2);
	CUSTOM->aud[0].ac_per = SYNTH_PER;
	CUSTOM->aud[0].ac_vol = g_bard_vol0;
	CUSTOM->aud[1].ac_ptr = (UWORD *)g_ring;
	CUSTOM->aud[1].ac_len = (UWORD)(RING_SAMPLES / 2);
	CUSTOM->aud[1].ac_per = SYNTH_PER;
	CUSTOM->aud[1].ac_vol = g_bard_vol0;
	CUSTOM->dmacon = (UWORD)(DMAF_SETCLR | DMAF_MASTER
	                         | DMAF_AUD0 | DMAF_AUD1);

	g_ring_play = 0;
	g_play_acc  = 0;
	g_play_tod  = bard_tod();
	g_ring_w    = RING_SAMPLES / 2;         /* half a ring ahead, as init */
	g_quiet_run = 0;
	g_bard_on = 0;
}

/* CIA-A TOD: a 24-bit counter ticking at 50 Hz off the POWER SUPPLY —
 * hardware time, immune to the interrupt starvation measured below. Reading
 * HI latches the value; reading LO releases the latch. */
static unsigned long bard_tod(void)
{
	volatile UBYTE *cia = (volatile UBYTE *)0xBFE001;
	unsigned long hi = cia[0x0A00], mid = cia[0x0900], lo = cia[0x0800];

	return (hi << 16) | (mid << 8) | lo;
}

/* Called from plat_sound_vbl while the bard plays — but NOT once per frame:
 * ★ MEASURED at 15-31 Hz during a quant against the nominal 50 (73 and 725
 * calls in windows expecting ~250 and ~1150). The user heard the result
 * precisely — "it's slow, and there's a tick in it": the tune ran at a third
 * speed off the starved call rate. (The same starvation under-fills the
 * ENGINE's ring during conversions, and it skews rl-clock timings taken
 * inside a quant — both recorded as open findings.) So the sequencer clocks
 * itself off CIA-A TOD and advances by ELAPSED 50 Hz ticks per call: at 15 Hz
 * servicing the envelopes step in chunks of ~3, but the tempo is true. */
static void bard_step_frame(void);

static void bard_vbl(void)
{
	unsigned long tod, elapsed;

	if (!g_bard_on)
		return;
	g_bard_calls++;
	tod = bard_tod();
	elapsed = (tod - g_bard_tod) & 0xFFFFFFUL;
	g_bard_tod = tod;
	/* arrival shape: a steady every-3rd-frame beat shows as a spike at
	 * gap=3; masked STRETCHES show as mostly gap=1 plus rare huge gaps. */
	g_bard_gap[elapsed <= 4 ? elapsed : 5]++;
	if ((long)elapsed > g_bard_gapmax)
		g_bard_gapmax = (long)elapsed;
	if (elapsed > 8)
		elapsed = 8;            /* wild delta (first call, TOD wrap): clamp */
	while (elapsed-- > 0)
		bard_step_frame();
}

static void bard_step_frame(void)
{
	if (g_bard_frames <= 0) {
		short per = g_bard_tune[g_bard_step][0];

		g_bard_frames = g_bard_tune[g_bard_step][1];
		if (per != BN_REST) {
			CUSTOM->aud[1].ac_per = (UWORD)per;
			g_bard_atk = 3;                 /* pluck: RAMP to 64 over 3
			                                 * frames — an instant step at
			                                 * arbitrary waveform phase is
			                                 * a CLICK, the user's "tick" */
		}
		g_bard_step = (short)((g_bard_step + 1) % BARD_STEPS);
	}
	g_bard_frames--;
	if (g_bard_atk > 0) {
		g_bard_atk--;
		g_bard_vol = (short)(64 - g_bard_atk * 20);
		CUSTOM->aud[1].ac_vol = (UWORD)g_bard_vol;
	} else if (g_bard_vol > 0) {
		g_bard_vol--;                   /* full-rate decay: the half-rate
		                                 * "tavern sustain" made every note
		                                 * a slow fade — literally the sound
		                                 * of dying the user kept hearing */
		CUSTOM->aud[1].ac_vol = (UWORD)g_bard_vol;
	}
	/* the ground bass: its own slow loop under the melody. Deliberately NOT
	 * length-matched to the tune — the lines drift against each other and
	 * realign, which is how a real pair noodles. */
	if (g_bass_frames <= 0) {
		g_bass_frames = g_bard_bass[g_bass_step][1];
		CUSTOM->aud[3].ac_per = (UWORD)g_bard_bass[g_bass_step][0];
		g_bass_atk = 3;
		g_bass_step = (short)((g_bass_step + 1) % BARD_BASS_STEPS);
	}
	g_bass_frames--;
	if (g_bass_atk > 0) {
		g_bass_atk--;
		g_bass_vol = (short)(40 - g_bass_atk * 12);
		CUSTOM->aud[3].ac_vol = (UWORD)g_bass_vol;
	} else if (g_bass_vol > 0) {
		g_bass_vol--;                   /* to SILENCE: the held floor of 12
		                                 * was one more permanent drone */
		CUSTOM->aud[3].ac_vol = (UWORD)g_bass_vol;
	}
}

void plat_sound_shutdown(void)
{
	if (!g_inited)
		return;
	g_ring_live = 0;
	g_ft_rec    = NULL;

	if (g_use_ahi) {
		/* Paula was never programmed and the LED filter never touched,
		 * so there is nothing to undo here beyond stopping the player. */
		ahi_stop();
		g_inited = 0;
		return;
	}

	CUSTOM->dmacon = DMAF_AUD0;
	CUSTOM->aud[0].ac_vol = 0;
	if (g_saved_led == 0)
		CIAA->ciapra &= (UBYTE)~CIAF_LED;       /* filter was on: restore */
	if (g_ring) {
		FreeMem(g_ring, RING_SAMPLES);
		g_ring = NULL;
	}
	if (g_bard_wave) {
		FreeMem(g_bard_wave, BARD_WAVE_LEN);
		g_bard_wave = NULL;
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

	/* Disarm before the fill (the Atari twin's fix, same race): the VBL
	 * mixer keeps running through this loop, and on a 7 MHz 68000 the fill
	 * is tens of milliseconds — with the old effect still armed it mixes
	 * from a half-rewritten buffer, a garbage blip at the sound's start.
	 * len gates the mixer, so clear it first, re-set it (last) below. */
	g_sfx_len = 0;
	g_sfx_pos = 0;
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
static short g_hook_acc;                /* 60 Hz Mac ticks vs real vblank    */
static short g_vbl_hz = 50;             /* from ExecBase at install time     */


void plat_sound_set_vbl_hook(void (*fn)(void))
{
	extern struct ExecBase *SysBase;

	g_vbl_hz = (short)SysBase->VBlankFrequency;
	if (g_vbl_hz < 50 || g_vbl_hz > 60)
		g_vbl_hz = 50;                  /* PAL default on nonsense */
	g_hook_acc = 0;
	s_vbl_hook = fn;
}

/* Called from the input backend's VERTB server every frame. Advance the
 * modeled play cursor by the frame's exact sample count, render up to half
 * a ring of lead, then run the sequencer — refill FIRST so a slow sequencer
 * pass can never starve the DMA. Memory writes only: interrupt-safe. */
void plat_sound_vbl(void)
{
	unsigned long b0 = vbl_beamline();

	bard_vbl();

	/* While the bard plays (cold conversions only) skip the refill AND
	 * the Mac VBL sound task. This is the fix for the "VBL starvation":
	 * with title music audible, refill + hook cost ~2.4 FRAMES per call
	 * on the 7 MHz 68000 (an earlier reading hid this by measuring beam
	 * lines modulo one frame: 2.33 frames = 729 lines = 103 mod 313).
	 * exec merges VERTB requests latched during an overrunning handler,
	 * so servicing locked to a metronomic every-3rd frame — measured as
	 * a clean gap-3 TOD histogram, nothing pending, the whole server
	 * chain starved equally, and it would do the SAME on real silicon
	 * (amiberry was innocent; issue #2288 retracted). Skipping the whole
	 * handler here took the two big cold conversions from 2238+2565 TOD
	 * ticks to 401+456 — the quant only ever needed ~1/5 of that wall
	 * time. Engine audio is muted under the bard anyway, and freezing
	 * the sequencer means the music RESUMES where it paused; the ring
	 * resync in plat_bard_stop re-seats the write point. */
	if (g_bard_on) {
		g_vblprof_calls++;
		g_vblprof_lines += (long)((vbl_beamline() - b0 + 313UL) % 313UL);
		return;
	}

	void (*hook)(void);
	long lead, todo;
	int  audible;

	if (!g_ring_live) {
		g_vblprof_calls++;
		g_vblprof_lines += (long)((vbl_beamline() - b0 + 313UL) % 313UL);
		return;
	}

	/* ★ THE NEEDLE BUMP. This line used to read
	 *     g_ring_play += FRAME_SAMPLES;   (454 per call)
	 * but real PAL Paula at period 156 consumes 71093.75/156 = 455.73
	 * samples per frame (227.5 CCK/line x 312.5 lines). The model
	 * under-advanced ~86 samples/s, the write head closed on the real
	 * DMA read head, and when it lapped, Paula replayed ~80ms of stale
	 * ring — an audible "vinyl needle bump" at a FIXED WALL TIME after
	 * ring start (which is how the tempo fix moved it to a later point
	 * in the tune: the bump never belonged to the music at all).
	 * Advance in 24.8 fixed point at the exact rate, scaled by elapsed
	 * TOD ticks so a missed vblank cannot skew the model either. */
	{
		unsigned long t = bard_tod();
		long e = (long)((t - g_play_tod) & 0xFFFFFFUL);

		g_play_tod = t;
		if (e < 1) e = 1;
		if (e > 4) e = 4;
		g_play_acc  += (unsigned long)g_play_step * (unsigned long)e;
		g_ring_play += (long)(g_play_acc >> 8);
		g_play_acc  &= 0xFF;
	}
	while (g_ring_play >= RING_SAMPLES)
		g_ring_play -= RING_SAMPLES;

	lead = g_ring_w - g_ring_play;
	if (lead < 0)
		lead += RING_SAMPLES;
	todo = (RING_SAMPLES / 2) - lead;       /* stay half a ring ahead */
	if (todo > FRAME_SAMPLES * 2)
		todo = FRAME_SAMPLES * 2;       /* bound one vblank's render —
		                                 * catch-up spreads over frames
		                                 * instead of ballooning one */

	/* #116: DO NOT SYNTHESISE SILENCE — the Falcon backend's #96 gate,
	 * ported. Paula loops the ring forever, so this refill runs every
	 * vblank whether or not the game is making a sound, and FRUA is silent
	 * for almost all of a session: the Atari instrumentation measured
	 * 99.976% of rendered samples inaudible. Each one costs the per-sample
	 * loop (voice tests, tone, effect, two clamps, a store) on a 7 MHz
	 * 68000 with no cache, ~227 samples every frame, forever.
	 *
	 * Once the whole ring holds zeroes there is nothing left to write —
	 * the DMA can loop it indefinitely and still play silence. So zero it
	 * ONCE on the way quiet, then leave it alone until something becomes
	 * audible again. g_quiet_run counts the silence already committed and
	 * resets the instant a voice, tone or effect appears, which puts the
	 * full synth back on the very next vblank. */
	audible = synth_audible();
	if (audible)
		g_quiet_run = 0;                /* full synth resumes THIS vblank */

#ifdef FRUA_SNDNOGATE
	g_quiet_run = 0;                        /* A/B arm: gate disabled */
#endif
	if (g_use_ahi) {
		/* the AHI player process renders straight into its own buffers;
		 * there is no ring and no DMA model to service here. The engine
		 * sequencer hook below still runs — it is what arms the voices. */
	} else if (!audible && g_quiet_run >= RING_SAMPLES) {
		g_vblprof_calls++;
		g_vblprof_lines += (long)((vbl_beamline() - b0 + 313UL) % 313UL);
		/* Hold the write point exactly half a ring ahead rather than
		 * leaving it where it was. Letting it drift would make `lead`
		 * wrap on the next audible frame, `todo` go negative, and the
		 * refill never run again — silence that never recovers.
		 *
		 * This SKIPS THE REFILL, NOT THE FUNCTION: the sequencer hook
		 * below is what arms the voices in the first place, so
		 * returning early here would be a silence that cannot end. */
		g_ring_w = g_ring_play + RING_SAMPLES / 2;
		if (g_ring_w >= RING_SAMPLES)
			g_ring_w -= RING_SAMPLES;
	} else {
		unsigned long f0 = vbl_beamline();
		unsigned long ft0 = bard_tod();

		while (todo > 0) {
			long chunk = RING_SAMPLES - g_ring_w;

			if (chunk > todo)
				chunk = todo;
			if (!audible)
				g_quiet_run += chunk;
			synth_render(g_ring + g_ring_w, chunk);
			g_ring_w += chunk;
			if (g_ring_w >= RING_SAMPLES)
				g_ring_w = 0;
			todo -= chunk;
		}
		g_prof_fill_calls++;
		/* TOD-qualified: ticks*313 + line remainder, so a multi-frame
		 * fill cannot wrap mod one frame (the trap that hid 2.4 frames
		 * as "103 lines" once already) */
		g_prof_fill_lines += (long)((bard_tod() - ft0) & 0xFFFFFFUL) * 313
		                   + (long)((vbl_beamline() - f0 + 313UL) % 313UL);
	}

	hook = s_vbl_hook;
	if (hook != NULL) {
		unsigned long h0 = vbl_beamline();
		unsigned long ht0 = bard_tod();

		/* The Mac VBL task counts SIXTY ticks a second; a PAL vblank
		 * delivers fifty, which played every tune 17% slow (the Mac
		 * tempo was only ever correct on NTSC machines by accident).
		 * Classic fix: accumulate 60 per vblank against the machine's
		 * actual vblank rate — on PAL the sequencer ticks twice on
		 * every 5th vblank (6 per 5), on NTSC exactly once, and the
		 * tempo matches the Mac original everywhere. */
		g_amiga_in_int = 1;     /* dbg sinks defer while engine code runs */
		g_hook_acc += 60;
		while (g_hook_acc >= g_vbl_hz) {
			g_hook_acc -= g_vbl_hz;
			hook();
		}
		g_amiga_in_int = 0;
		g_prof_hook_calls++;
		g_prof_hook_lines += (long)((bard_tod() - ht0) & 0xFFFFFFUL) * 313
		                   + (long)((vbl_beamline() - h0 + 313UL) % 313UL);
	}
	g_vblprof_calls++;
	g_vblprof_lines += (long)((vbl_beamline() - b0 + 313UL) % 313UL);
}

#endif /* FRUA_AMIGA */
