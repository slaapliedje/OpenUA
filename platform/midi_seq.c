/*
 * MIDI music: the DOS release's XMIDI scores played out the MIDI port.
 *
 * FRUA's music is the Mac four-tone synth, which the ports render in
 * software (sound_mix.c) from the Tandy 3-voice arrangement of the DOS
 * score (tools/xmi2slb.py). The DOS release also ships the Roland MT-32
 * arrangement of the same songs — RODQ1-3.XMI — and every ST, STe, TT and
 * Falcon has a MIDI port. This plays that score through it, with the
 * synth's music voices muted meanwhile; effects and the beep are untouched.
 *
 * Platform-independent: the file is parsed into an event list, a scheduler
 * driven by the 200 Hz system counter fires events into a byte FIFO, and the
 * platform half (midi_out_*.c) moves bytes to the hardware. Two contexts
 * touch the state — the sound vblank (interrupt) and the engine's main loop
 * (song start/stop/mute, the poll) — so the main loop raises g_busy around
 * its work and the vblank stands down while it is up.
 *
 * XMIDI: time is a sum of delay bytes (< 0x80); a note-on carries its
 * duration as a VLQ and there are no note-offs; 0xFF metas hold the tempo
 * (0x51, us per quarter) and the end (0x2F). Sixty ticks make a quarter
 * note, so one tick is tempo/60 us (the rule tools/xmi2slb.py validated
 * against the Mac bank). Controllers 110-120 are XMIDI's own and stay off
 * the wire. There is no SysEx in these scores; the MT-32 patch numbers are
 * either sent as they are (midi=mt32) or mapped to General MIDI (midi=gm).
 */
#include <string.h>
#include "plat_midi.h"
#include "sound_mix.h"

#define MAX_EVENTS   6144            /* the retail scores need ~2700 */
#define MAX_ACTIVE   64              /* sounding notes awaiting their off */
#define FIFO_SIZE    1024            /* power of two */
#define VBL_BYTES    12              /* per vblank: ~4 ms at 31250 baud */
#define VBL_WAIT     1500            /* status reads to wait for one byte */

struct ev {
	unsigned long tick;
	unsigned long dur;               /* note-on: ticks; 0xFF: tempo (us) */
	unsigned char st, d1, d2, pad;
};

static struct ev     g_ev[MAX_EVENTS];
static long          g_nev;
static unsigned long g_end_tick;     /* the score restarts here */
static unsigned long g_tempo_us;     /* at the start of the score */

static int          g_enabled, g_gm = 1;
static volatile int g_playing, g_muted, g_busy;

/* play position */
static long          g_pos;
static unsigned long g_tick, g_acc_us, g_tick_us, g_last200;
static int           g_have_last;

struct act { unsigned char ch, note; unsigned long end; };
static struct act g_act[MAX_ACTIVE];
static int        g_nact;

static unsigned char     g_fifo[FIFO_SIZE];
static volatile unsigned g_fh, g_ft; /* head = next to send, tail = next free */

/* MT-32 preset -> General MIDI program, by instrument name. Only used for
 * midi=gm; channel 10 (the rhythm part) is not mapped. */
static const unsigned char mt32_to_gm[128] = {
	 0,  1,  0,  4,  5,  4,  5,  3,      /* Acou Piano 1-3, Elec Piano 1-4, Honkytonk */
	16, 17, 18, 16, 19, 19, 19, 21,      /* Elec Org 1-4, Pipe Org 1-3, Accordion */
	 6,  6,  6,  7,  7,  7,  8,  8,      /* Harpsi 1-3, Clavi 1-3, Celesta 1-2 */
	62, 63, 62, 63, 38, 39, 38, 39,      /* Syn Brass 1-4, Syn Bass 1-4 */
	88, 89, 52, 98, 97, 99, 14, 54,      /* Fantasy, Harmo Pan, Chorale, Glasses, Soundtrack, Atmosphere, Warm Bell, Funny Vox */
	102, 96, 68, 95, 86, 103, 98, 80,    /* Echo Bell, Ice Rain, Oboe 2001, Echo Pan, Doctor Solo, School Daze, Bellsinger, Square Wave */
	48, 49, 48, 45, 40, 40, 42, 42,      /* Str Sect 1-3, Pizzicato, Violin 1-2, Cello 1-2 */
	43, 46, 46, 24, 25, 26, 27, 104,     /* Contrabass, Harp 1-2, Guitar 1-2, Elec Gtr 1-2, Sitar */
	32, 32, 33, 34, 36, 37, 35, 35,      /* Acou Bass 1-2, Elec Bass 1-2, Slap Bass 1-2, Fretless 1-2 */
	73, 73, 72, 72, 74, 75, 64, 65,      /* Flute 1-2, Piccolo 1-2, Recorder, Pan Pipes, Sax 1-2 */
	66, 67, 71, 71, 68, 69, 70, 22,      /* Sax 3-4, Clarinet 1-2, Oboe, Engl Horn, Bassoon, Harmonica */
	56, 56, 57, 57, 60, 60, 58, 61,      /* Trumpet 1-2, Trombone 1-2, Fr Horn 1-2, Tuba, Brs Sect 1 */
	61, 11, 11, 12, 14,  9, 14, 13,      /* Brs Sect 2, Vibe 1-2, Syn Mallet, Windbell, Glock, Tube Bell, Xylophone */
	12, 107, 111, 77, 78, 78, 76, 76,    /* Marimba, Koto, Sho, Shakuhachi, Whistle 1-2, Bottleblow, Breathpipe */
	47, 117, 118, 118, 118, 116, 116, 119, /* Timpani, Melodic Tom, Deep Snare, Elec Perc 1-2, Taiko, Taiko Rim, Cymbal */
	115, 112, 55, 124, 123, 55, 98, 122  /* Castanets, Triangle, Orche Hit, Telephone, Bird Tweet, One Note Jam, Water Bell, Jungle Tune */
};

/* --- the byte FIFO -------------------------------------------------------- */

static unsigned fifo_count(void)
{
	return (g_ft - g_fh) & (FIFO_SIZE - 1);
}

static void fifo_put(unsigned char b)
{
	if (fifo_count() >= FIFO_SIZE - 1)
		return;                         /* full: drop rather than corrupt */
	g_fifo[g_ft] = b;
	g_ft = (g_ft + 1) & (FIFO_SIZE - 1);
}

static void send2(unsigned char st, unsigned char d1)
{
	fifo_put(st);
	fifo_put(d1);
}

static void send3(unsigned char st, unsigned char d1, unsigned char d2)
{
	fifo_put(st);
	fifo_put(d1);
	fifo_put(d2);
}

int midi_seq_pending(void)
{
	return fifo_count() != 0;
}

void midi_seq_drain(long budget, long wait_iters)
{
	while (fifo_count() != 0 && budget != 0) {
		long w = wait_iters;
		while (!midi_hw_ready() && w-- > 0)
			;
		if (!midi_hw_ready())
			return;                 /* the port is slow: try again later */
		midi_hw_put(g_fifo[g_fh]);
		g_fh = (g_fh + 1) & (FIFO_SIZE - 1);
		if (budget > 0)
			budget--;
	}
}

/* --- sounding notes ------------------------------------------------------- */

static void note_off_at(int i)
{
	send3((unsigned char)(0x80 | g_act[i].ch), g_act[i].note, 0);
	g_nact--;
	if (i < g_nact)
		memmove(&g_act[i], &g_act[i + 1],
			(size_t)(g_nact - i) * sizeof g_act[0]);
}

static void notes_off_all(void)
{
	while (g_nact > 0)
		note_off_at(g_nact - 1);
}

static void note_on(unsigned char ch, unsigned char note, unsigned char vel,
		    unsigned long dur)
{
	if (g_muted)
		return;
	if (g_nact >= MAX_ACTIVE)
		note_off_at(0);                 /* oldest makes room */
	g_act[g_nact].ch   = ch;
	g_act[g_nact].note = note;
	g_act[g_nact].end  = g_tick + (dur ? dur : 1);
	g_nact++;
	send3((unsigned char)(0x90 | ch), note, vel);
}

/* --- the scheduler -------------------------------------------------------- */

/* Everything due at g_tick: offs first, then the score's events; wrap to
 * the start when the score has run out. */
static void fire(void)
{
	int i;

	for (;;) {
		for (i = 0; i < g_nact; ) {
			if (g_act[i].end <= g_tick)
				note_off_at(i);
			else
				i++;
		}
		while (g_pos < g_nev && g_ev[g_pos].tick <= g_tick) {
			const struct ev *e = &g_ev[g_pos++];
			switch (e->st & 0xf0) {
			case 0x90:
				note_on((unsigned char)(e->st & 0x0f), e->d1, e->d2, e->dur);
				break;
			case 0xC0:
			case 0xD0:
				send2(e->st, e->d1);
				break;
			case 0xF0:
				if (e->st == 0xFF && e->dur >= 6000UL)  /* tempo */
					g_tick_us = e->dur / 60;
				break;
			default:                /* note off, aftertouch, control, bend */
				send3(e->st, e->d1, e->d2);
				break;
			}
		}
		if (!(g_pos >= g_nev && g_tick >= g_end_tick))
			break;
		/* the end of the score: loop, as the Mac bank does */
		notes_off_all();
		if (g_nev == 0 || g_end_tick == 0) {
			g_playing = 0;
			break;
		}
		g_pos  = 0;
		g_tick = 0;
	}
}

/* --- the XMIDI parser ----------------------------------------------------- */

static int add_event(unsigned long t, unsigned long dur, unsigned char st,
		     unsigned char d1, unsigned char d2)
{
	struct ev *e;

	if (g_nev >= MAX_EVENTS)
		return -1;
	e = &g_ev[g_nev++];
	e->tick = t;
	e->dur  = dur;
	e->st   = st;
	e->d1   = d1;
	e->d2   = d2;
	e->pad  = 0;
	return 0;
}

/* A MIDI variable-length quantity at p[*i], bounded by n. */
static unsigned long read_vlq(const unsigned char *p, long *i, long n)
{
	unsigned long v = 0;
	int k;

	for (k = 0; k < 4 && *i < n; k++) {
		unsigned char b = p[(*i)++];
		v = (v << 7) | (b & 0x7f);
		if (!(b & 0x80))
			break;
	}
	return v;
}

static int parse_xmi(const unsigned char *x, long len)
{
	const unsigned char *p;
	unsigned long t = 0, end = 0;
	long i, n;

	g_nev      = 0;
	g_tempo_us = 500000UL;
	for (i = 0; i + 8 <= len; i++)
		if (x[i] == 'E' && x[i + 1] == 'V' && x[i + 2] == 'N' && x[i + 3] == 'T')
			break;
	if (i + 8 > len)
		return -1;
	n = ((long)x[i + 4] << 24) | ((long)x[i + 5] << 16)
	  | ((long)x[i + 6] << 8) | (long)x[i + 7];
	p = x + i + 8;
	if (n > len - (i + 8))
		n = len - (i + 8);

	i = 0;
	while (i < n) {
		unsigned char b = p[i];
		unsigned char ch;

		if (b < 0x80) {                 /* a delay byte */
			t += b;
			i++;
			continue;
		}
		i++;
		ch = (unsigned char)(b & 0x0f);
		switch (b & 0xf0) {
		case 0x90: {                    /* note on + duration */
			unsigned char note, vel;
			unsigned long dur;
			if (i + 2 > n)
				return g_nev ? 0 : -1;
			note = p[i];
			vel  = p[i + 1];
			i += 2;
			dur = read_vlq(p, &i, n);
			add_event(t, dur, b, note, vel);
			if (t + dur > end)
				end = t + dur;
			break;
		}
		case 0x80:                      /* note off (not XMIDI's way, but legal) */
		case 0xA0:                      /* key pressure */
		case 0xE0:                      /* pitch bend */
			if (i + 2 > n)
				return g_nev ? 0 : -1;
			add_event(t, 0, b, p[i], p[i + 1]);
			i += 2;
			break;
		case 0xB0:                      /* control change */
			if (i + 2 > n)
				return g_nev ? 0 : -1;
			if (p[i] < 110 || p[i] > 120)   /* 110-120 are XMIDI's own */
				add_event(t, 0, b, p[i], p[i + 1]);
			i += 2;
			break;
		case 0xC0: {                    /* program change */
			unsigned char prog;
			if (i + 1 > n)
				return g_nev ? 0 : -1;
			prog = (unsigned char)(p[i] & 0x7f);
			if (g_gm && ch != 9)
				prog = mt32_to_gm[prog];
			add_event(t, 0, b, prog, 0);
			i += 1;
			break;
		}
		case 0xD0:                      /* channel pressure */
			if (i + 1 > n)
				return g_nev ? 0 : -1;
			add_event(t, 0, b, p[i], 0);
			i += 1;
			break;
		case 0xF0:
			if (b == 0xFF) {            /* meta */
				unsigned char type;
				unsigned long l;
				if (i + 1 > n)
					return g_nev ? 0 : -1;
				type = p[i++];
				l = read_vlq(p, &i, n);
				if (type == 0x51 && l == 3 && i + 3 <= n) {
					unsigned long tempo = ((unsigned long)p[i] << 16)
					                    | ((unsigned long)p[i + 1] << 8)
					                    | (unsigned long)p[i + 2];
					if (tempo < 6000UL)
						tempo = 6000UL;
					if (g_nev == 0 && t == 0)
						g_tempo_us = tempo;
					else
						add_event(t, tempo, 0xFF, 0, 0);
				} else if (type == 0x2F) {
					i = n;              /* end of track */
					break;
				}
				i += (long)l;
			} else {                    /* SysEx: not sent */
				unsigned long l = read_vlq(p, &i, n);
				i += (long)l;
			}
			break;
		default:
			return g_nev ? 0 : -1;      /* not a MIDI status: give up here */
		}
	}
	if (t > end)
		end = t;
	g_end_tick = end;
	return g_nev ? 0 : -1;
}

/* --- the API -------------------------------------------------------------- */

void plat_midi_configure(int enabled, int gm)
{
	g_enabled = enabled;
	g_gm      = gm;
}

int plat_midi_enabled(void)
{
	return g_enabled;
}

int plat_midi_playing(void)
{
	return g_playing;
}

int plat_midi_song(const unsigned char *xmi, long len)
{
	if (!g_enabled || xmi == NULL || len <= 0)
		return -1;
	g_busy = 1;
	if (g_playing) {
		notes_off_all();
		g_playing = 0;
	}
	if (parse_xmi(xmi, len) != 0) {
		mix_music_mute(0);
		g_busy = 0;
		return -1;
	}
	g_pos       = 0;
	g_tick      = 0;
	g_acc_us    = 0;
	g_tick_us   = g_tempo_us / 60;
	g_have_last = 0;
	g_nact      = 0;
	mix_music_mute(1);                  /* the port carries the music now */
	g_playing = 1;
	fire();                             /* the score's tick 0: patches, first notes */
	g_busy = 0;
	return 0;
}

void plat_midi_stop(void)
{
	if (!g_enabled)
		return;
	g_busy = 1;
	if (g_playing) {
		notes_off_all();
		g_playing = 0;
	}
	mix_music_mute(0);
	g_busy = 0;
}

void plat_midi_mute(int muted)
{
	if (!g_enabled)
		return;
	g_busy = 1;
	g_muted = muted;
	if (muted)
		notes_off_all();
	g_busy = 0;
}

void plat_midi_vbl(unsigned long hz200)
{
	unsigned long dt;

	if (!g_enabled || g_busy)
		return;
	if (g_playing) {
		if (!g_have_last) {
			g_last200   = hz200;
			g_have_last = 1;
		}
		dt = hz200 - g_last200;
		g_last200 = hz200;
		if (dt > 100)                   /* after a stall: no catching up */
			dt = 100;
		g_acc_us += dt * 5000UL;
		while (g_playing && g_acc_us >= g_tick_us) {
			g_acc_us -= g_tick_us;
			g_tick++;
			fire();
		}
	}
	midi_seq_drain(VBL_BYTES, VBL_WAIT);
}

void plat_midi_poll(void)
{
	if (!g_enabled || g_busy || fifo_count() == 0)
		return;
	g_busy = 1;
	midi_hw_flush();
	g_busy = 0;
}

void plat_midi_shutdown(void)
{
	if (!g_enabled)
		return;
	plat_midi_stop();
	g_busy = 1;
	midi_hw_flush();                    /* the offs reach the instrument */
	g_busy = 0;
	g_enabled = 0;
}
