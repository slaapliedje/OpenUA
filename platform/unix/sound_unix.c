/*
 * Sound HAL - Unix: the shared mixer (platform/sound_mix.c: FRUA's
 * four-tone music, the swMode tone, one effect on top) rendered into
 * /dev/audio.
 *
 * /dev/audio is atari-sysv-sp1's driver-snd: the TT's 8-bit DMA sound
 * under Atari System V. It plays signed 8-bit samples from a ring in
 * ST-RAM, and when the writer falls behind it plays silence rather than
 * old samples. So the backend only has to keep a little ahead of the play
 * position while something is audible, and can simply stop writing when
 * nothing is. Without the device (AMIX, or a kernel without driver-snd)
 * plat_sound_init fails and the game runs silent, as it did before.
 *
 * The engine's Mac VBL task (VInstall, the sound sequencer, jt1091) runs
 * from here: unix_vbl_poll() runs one VBL per elapsed 60 Hz tick, each
 * first topping up the device and then running the task, as the vertical
 * blank does on the other ports. It is called from the input pump, and
 * from FSRead between the pieces of a large read, so that loading a screen
 * does not starve the sound. (A SIGALRM timer was tried and dropped: under
 * amx on Atari System V the signals cost most of the machine, AMIX's libc
 * re-issues an interrupted poll with its whole timeout, and the game
 * stalled.)
 */
#ifdef FRUA_UNIX

#include <stddef.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "plat_sound.h"
#include "sound_mix.h"
#include "input.h"
#include "unix_vbl.h"
#include "dbglog.h"
#include "plat_midi.h"

/* atari-sysv-sp1 driver-snd/snd.h */
#define SND_IOC		('A' << 8)
#define SND_SETRATE	(SND_IOC | 1)	/* arg = Hz (nearest of 4); returns Hz */
#define SND_SETSTEREO	(SND_IOC | 2)	/* 0 mono, 1 stereo */
#define SND_GETDELAY	(SND_IOC | 5)	/* bytes before a new write plays */
#define SND_SETVOLUME	(SND_IOC | 8)	/* master 0..40, 40 = 0 dB */
#define SND_GETRING	(SND_IOC | 6)	/* the ring's size in bytes */

#define LEAD_MS		250		/* how far ahead of the play position */
#define LOAD_LEAD_MS	1000		/* ... while a screen loads */
#define LOAD_TICKS	90		/* how long a read counts as loading */
#define CHUNK		4096		/* the most rendered per write */
#define RESYNC		32		/* refills between asking the driver */
#define HIGHPASS_HZ	50		/* the default low-bass cut */

static void (*s_vbl_hook)(void);
static unsigned long s_last_tick;
static unsigned long s_seq_tick;	/* ticks the sequencer has run */
static long s_tick_frac;		/* 60ths of a sample owed */
static int s_fd = -1;
static int s_ch = 1;			/* channels: 2 = stereo frames */
static int s_rec = -1;			/* OPENUA_SNDREC: a copy of the stream */
static char s_buf[CHUNK];
volatile int unix_in_vbl;
unsigned long unix_seq_now;
static int s_gaplog, s_ngaps;		/* OPENUA_SNDLOG */
static int s_fed;			/* the last refill queued sound */
static long s_dry;			/* ... and the queue ran out after */
const char *unix_vbl_site = "?";
static const char *s_last_site = "?";

int plat_sound_init(void)
{
	int hz;

	s_last_tick = s_seq_tick = unix_seq_now = unix_ticks();
	/* MIDI music (sound.cfg) is pumped from plat_sound_vbl, which runs
	 * with or without /dev/audio */
	(void)plat_midi_init();
	s_fd = open("/dev/audio", O_WRONLY);
	if (s_fd < 0)
		return 1;			/* no sound: the shim continues silent */
	hz = ioctl(s_fd, SND_SETRATE, 25033);	/* the STe/TT rate the Atari build uses */
	/* Stereo (the voices panned apart) when the driver's ring holds the
	 * stereo loading lead with room to spare; a 32 KB ring (the first
	 * driver-snd) does not, and a write would then sleep. OPENUA_STEREO=0
	 * keeps mono. */
	s_ch = ioctl(s_fd, SND_GETRING, 0) >= 2L * hz * LOAD_LEAD_MS / 1000 + 8192
	       && (getenv("OPENUA_STEREO") == NULL || atoi(getenv("OPENUA_STEREO")) != 0)
	       ? 2 : 1;
	if (hz <= 0 || ioctl(s_fd, SND_SETSTEREO, s_ch == 2) < 0) {
		close(s_fd);
		s_fd = -1;
		return 1;
	}
	mix_hz = hz;
	mix_reset_phases();
	/* FRUA's bass reaches ~26 Hz, below anything a Mac could play: cut it
	 * by default. OPENUA_HIGHPASS=hz moves the corner, 0 keeps it all. */
	mix_set_highpass(getenv("OPENUA_HIGHPASS") != NULL
	                 ? atoi(getenv("OPENUA_HIGHPASS")) : HIGHPASS_HZ);
	/* diagnostics: OPENUA_VOLUME=0..40 sets the TT's master volume (40 =
	 * 0 dB, 2 dB steps); OPENUA_SNDREC=file keeps a copy of every sample
	 * written (signed 8-bit at mix_hz, stereo frames left first); OPENUA_SNDLOG logs each gap
	 * in the feeding longer than the queue lasts to DBG.LOG */
	if (getenv("OPENUA_VOLUME") != NULL)
		ioctl(s_fd, SND_SETVOLUME, atoi(getenv("OPENUA_VOLUME")));
	s_gaplog = getenv("OPENUA_SNDLOG") != NULL;
	if (getenv("OPENUA_SNDREC") != NULL)
		s_rec = open(getenv("OPENUA_SNDREC"), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	return 0;
}

void plat_sound_shutdown(void)
{
	dbg_trace_dump();		/* OPENUA_TRACE: the run's events, at the end */
	plat_midi_shutdown();		/* notes off, and the port drained */
	s_vbl_hook = NULL;
	mix_set_record(NULL);
	mix_effect_stop();
	mix_tone_off();
	if (s_fd >= 0) {
		/* the driver counts every time play passes the data, the end of
		 * each tune included; s_dry only while there was more to play */
		dbg_log_num("sound: ran dry while playing, times: ", s_dry);
		close(s_fd);			/* the driver lets queued samples play out */
		s_fd = -1;
	}
	if (s_rec >= 0) {
		close(s_rec);
		s_rec = -1;
	}
}

/* Keep LEAD_MS of samples queued while something is audible. While nothing
 * is, write nothing: the driver plays silence once its queue runs dry, and
 * the mixer costs nothing.
 *
 * The queue level is estimated from what was written and the time since,
 * and the driver is asked (an ioctl, which amx makes expensive) only every
 * RESYNC refills or when the estimate says the queue ran dry. */
static long s_queued;			/* bytes queued, as of s_qtime */
static unsigned long s_qtime;		/* unix_ticks() then */
static int s_since_sync = RESYNC;
static unsigned long s_loading_until;	/* unix_ticks() */

static long queued_now(void)
{
	unsigned long now = unix_ticks();
	long q = s_queued - (long)(now - s_qtime) * mix_hz * s_ch / 60;

	if (q <= 0 || ++s_since_sync >= RESYNC) {
		long have = ioctl(s_fd, SND_GETDELAY, 0);

		s_since_sync = 0;
		q = have < 0 ? 0 : have;
	}
	s_queued = q;
	s_qtime = now;
	return q;
}

/* Write n rendered bytes from s_buf (high-passed) to the device. */
static int put(long n)
{
	if (s_ch == 2)
		mix_highpass_stereo(s_buf, n / 2);
	else
		mix_highpass(s_buf, n);
	if (write(s_fd, s_buf, (size_t)n) != n)
		return 0;
	if (s_rec >= 0)
		(void)write(s_rec, s_buf, (size_t)n);
	s_queued += n;
	return 1;
}

/* Run the engine's sound task once: one 60 Hz tick of the sequencer. It
 * counts as a VBL (FSRead does not refill), and the time it reads is the
 * SOUND's: the sequencer plays the notes due by TickCount, so a burst of
 * ticks rendered ahead, all reading the wall clock, held a note and then
 * caught up in a rush. plat_ticks returns unix_seq_now inside the task. */
static void run_seq(void)
{
	int was = unix_in_vbl;

	s_seq_tick++;
	unix_seq_now = s_seq_tick;
	unix_in_vbl = 1;
	if (s_vbl_hook != NULL)
		s_vbl_hook();
	unix_in_vbl = was;
}

/* OPENUA_SNDLOG: when the music stops and starts (the first 64), with how
 * far the sequencer had run ahead of the wall clock */
static void quiet_log(const char *what)
{
	static int n;

	if (!s_gaplog || n++ >= 64)
		return;
	dbg_log_num(what, (long)unix_ticks());
	dbg_log_num("sound:   sequencer ahead by ", (long)(s_seq_tick - unix_ticks()));
}

/* Queue sound up to the lead, ONE TICK AT A TIME with the sequencer run
 * between ticks: the sequencer rewrites the voices once a tick and the
 * mixer plays whatever they hold, so rendering a second in one go played
 * one note for that second (the note that "dragged" while a screen
 * loaded). The music is clocked by the samples written, as the other
 * ports' VBL is clocked by the sound it refills; the sequencer runs up to
 * the lead ahead of the wall clock. */
static void refill(void)
{
	long want, q, n, got = 0;
	int ticks = 0;
	unsigned long t0 = unix_ticks();

	if (s_fd < 0 || !mix_audible()) {
		if (s_fed)
			quiet_log("sound: quiet at tick ");
		s_fed = 0;
		return;
	}
	if (!s_fed)
		quiet_log("sound: audible at tick ");
	want = mix_hz * s_ch
	     * (unix_ticks() < s_loading_until ? LOAD_LEAD_MS : LEAD_MS) / 1000;
	q = queued_now();
	/* after running dry the driver restarts a little ahead (its LEAD,
	 * 512 bytes of silence): "empty" is at most that much queued */
	if (s_fed && q <= 1024)
		s_dry++;
	s_fed = 1;
	while (q + got < want && mix_audible()) {
		n = mix_hz / 60;
		s_tick_frac += mix_hz % 60;
		if (s_tick_frac >= 60) {
			s_tick_frac -= 60;
			n++;
		}
		if (got + n * s_ch > CHUNK) {
			if (!put(got))
				return;
			q += got;
			got = 0;
		}
		if (s_ch == 2)
			mix_render_stereo(s_buf + got, n);
		else
			mix_render(s_buf + got, n);
		got += n * s_ch;
		run_seq();
		ticks++;
	}
	if (got > 0)
		(void)put(got);
	if (ticks >= 10)		/* a big fill: how long did it take? */
		DBG_TRACE("snd_fill", ticks, unix_ticks() - t0);
}

int plat_sound_play_mono8(const signed char *samples, long count, int rate_hz)
{
	if (s_fd < 0 || samples == NULL || count <= 0 || rate_hz <= 0)
		return 0;			/* silent: "played", over at once */
	mix_effect(samples, count, rate_hz);
	return 0;
}

void plat_sound_stop(void)
{
	mix_effect_stop();
}

int plat_sound_playing(void)
{
	return s_fd >= 0 ? mix_effect_playing() : 0;
}

int plat_sound_synth_start(const void *ftsoundrec)
{
	if (ftsoundrec == NULL)
		return -1;
	mix_set_record(ftsoundrec);		/* LIVE: the sequencer rewrites it */
	return 0;
}

void plat_sound_synth_stop(void)
{
	mix_set_record(NULL);
}

void plat_sound_tone(int count, int amp, int duration_ticks)
{
	if (s_fd < 0) {
		mix_tone_off();
		return;
	}
	mix_tone(count, amp, duration_ticks);
}

/* Top up the device (which runs the sequencer for each tick it renders);
 * with nothing audible, run the sequencer by the wall clock instead - it
 * is what starts the voices - but never while it is still ahead from
 * rendering. */
void plat_sound_vbl(void)
{
	static unsigned long s_midi_ms;
	unsigned long ms = unix_ms();
	unsigned long now = ms * 60 / 1000;	/* = unix_ticks(), one clock read */
	int n = 0;

	refill();
	if (s_seq_tick + 4 < now)
		s_seq_tick = now - 4;	/* a long stall is not replayed in a burst */
	while (s_seq_tick < now && n++ < 4 && !(s_fd >= 0 && mix_audible()))
		run_seq();
	refill();			/* a voice the task just started */
	/* MIDI music: advance the score by the wall clock in 200 Hz units (what
	 * the Atari's _hz_200 gives it) and send a few queued bytes - ONCE a
	 * tick. Each byte is system calls (midi_out_unix.c); a poll that paid
	 * for them on every turn of the engine's wait loop kept the turn too
	 * long for idle_if_spinning to see six in one tick, and an idle menu
	 * took the whole machine. */
	if (ms - s_midi_ms >= 16) {
		s_midi_ms = ms;
		plat_midi_vbl(ms / 5);
	}
}

void plat_sound_set_vbl_hook(void (*fn)(void))
{
	s_vbl_hook = fn;
}

/* A screen is loading (FSRead): what follows the read - decoding and
 * drawing the picture - can run for most of a second without anything
 * that polls, where the Atari's vertical blank would interrupt it. Queue
 * a second of sound now and keep that lead while loading goes on; the
 * rest of the time the lead stays short, so an effect is heard promptly. */
void unix_sound_loading(void)
{
	DBG_TRACE("snd_loading", 0, 0);
	s_loading_until = unix_ticks() + LOAD_TICKS;
	if (!unix_in_vbl)
		refill();
}

/* From every engine function (boot.c's PROBE): every 256th call, a poll
 * - the clock read is a system call, the call itself nearly free. */
void plat_probe_poll(const char *name)
{
	static unsigned n;

	if ((++n & 255) != 0 || unix_in_vbl)
		return;
	unix_vbl_site = name;
	unix_vbl_poll();
}

/* One VBL per 60 Hz tick since the last call; a long stall (the process was
 * stopped, the machine busy) is not replayed in a burst beyond 4. Not
 * re-entered: FSRead can be reached from inside a VBL. */
void unix_vbl_poll(void)
{
	unsigned long now;

	if (unix_in_vbl)
		return;
	unix_in_vbl = 1;
	now = unix_ticks();
	/* a gap longer than the queue lasts is a dropout: say when, and how
	 * long (OPENUA_SNDLOG, the first 64) */
	if (s_gaplog && s_fd >= 0 && now - s_last_tick >
	    (s_last_tick < s_loading_until ? LOAD_LEAD_MS : LEAD_MS) * 60 / 1000
	    && s_ngaps < 64) {
		s_ngaps++;
		dbg_log_num("sound: unfed at tick ", (long)now);
		dbg_log_num("sound:   for ticks ", (long)(now - s_last_tick));
		dbg_log(s_last_site);
		dbg_log(unix_vbl_site);
	}
	plat_sound_vbl();
	s_last_tick = now;
	s_last_site = unix_vbl_site;
	unix_in_vbl = 0;
}

#endif /* FRUA_UNIX */
