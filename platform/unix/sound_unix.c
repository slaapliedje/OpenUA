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

/* atari-sysv-sp1 driver-snd/snd.h */
#define SND_IOC		('A' << 8)
#define SND_SETRATE	(SND_IOC | 1)	/* arg = Hz (nearest of 4); returns Hz */
#define SND_SETSTEREO	(SND_IOC | 2)	/* 0 mono, 1 stereo */
#define SND_GETDELAY	(SND_IOC | 5)	/* bytes before a new write plays */
#define SND_GETUNDERRUNS (SND_IOC | 7)	/* times the queue ran dry */
#define SND_SETVOLUME	(SND_IOC | 8)	/* master 0..40, 40 = 0 dB */

#define LEAD_MS		150		/* how far ahead of the play position */
#define CHUNK		4096		/* the most rendered per write */
#define RESYNC		32		/* refills between asking the driver */
#define HIGHPASS_HZ	50		/* the default low-bass cut */

static void (*s_vbl_hook)(void);
static unsigned long s_last_tick;
static int s_fd = -1;
static int s_rec = -1;			/* OPENUA_SNDREC: a copy of the stream */
static char s_buf[CHUNK];
volatile int unix_in_vbl;
static int s_gaplog, s_ngaps;		/* OPENUA_SNDLOG */

int plat_sound_init(void)
{
	int hz;

	s_last_tick = unix_ticks();
	s_fd = open("/dev/audio", O_WRONLY);
	if (s_fd < 0)
		return 1;			/* no sound: the shim continues silent */
	hz = ioctl(s_fd, SND_SETRATE, 25033);	/* the STe/TT rate the Atari build uses */
	if (hz <= 0 || ioctl(s_fd, SND_SETSTEREO, 0) < 0) {
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
	 * written (signed 8-bit mono at mix_hz); OPENUA_SNDLOG logs each gap
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
	s_vbl_hook = NULL;
	mix_set_record(NULL);
	mix_effect_stop();
	mix_tone_off();
	if (s_fd >= 0) {
		dbg_log_num("sound: /dev/audio ran dry this many times: ",
		            (long)ioctl(s_fd, SND_GETUNDERRUNS, 0));
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
static long s_queued;			/* samples queued, as of s_qtime */
static unsigned long s_qtime;		/* unix_ticks() then */
static int s_since_sync = RESYNC;

static long queued_now(void)
{
	unsigned long now = unix_ticks();
	long q = s_queued - (long)(now - s_qtime) * mix_hz / 60;

	if (q <= 0 || ++s_since_sync >= RESYNC) {
		long have = ioctl(s_fd, SND_GETDELAY, 0);

		s_since_sync = 0;
		q = have < 0 ? 0 : have;
	}
	s_queued = q;
	s_qtime = now;
	return q;
}

static void refill(void)
{
	long want, n;

	if (s_fd < 0 || !mix_audible())
		return;
	want = mix_hz * LEAD_MS / 1000 - queued_now();
	while (want > 0) {
		n = want < CHUNK ? want : CHUNK;
		mix_render(s_buf, n);
		mix_highpass(s_buf, n);
		if (write(s_fd, s_buf, (size_t)n) != n)
			return;
		if (s_rec >= 0)
			(void)write(s_rec, s_buf, (size_t)n);
		s_queued += n;
		want -= n;
	}
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

/* Top up the device, then run the engine's sound task (refill first, so a
 * slow sequencer pass cannot starve the sound). */
void plat_sound_vbl(void)
{
	refill();
	if (s_vbl_hook != NULL)
		s_vbl_hook();
}

void plat_sound_set_vbl_hook(void (*fn)(void))
{
	s_vbl_hook = fn;
}

/* One VBL per 60 Hz tick since the last call; a long stall (the process was
 * stopped, the machine busy) is not replayed in a burst beyond 4. Not
 * re-entered: FSRead can be reached from inside a VBL. */
void unix_vbl_poll(void)
{
	unsigned long now;
	int n = 0;

	if (unix_in_vbl)
		return;
	unix_in_vbl = 1;
	now = unix_ticks();
	/* a gap longer than the queue lasts is a dropout: say when, and how
	 * long (OPENUA_SNDLOG, the first 64) */
	if (s_gaplog && s_fd >= 0 && now - s_last_tick > LEAD_MS * 60 / 1000
	    && s_ngaps < 64) {
		s_ngaps++;
		dbg_log_num("sound: unfed at tick ", (long)now);
		dbg_log_num("sound:   for ticks ", (long)(now - s_last_tick));
	}
	while (s_last_tick < now && n < 4) {
		plat_sound_vbl();
		s_last_tick++;
		n++;
	}
	s_last_tick = now;
	unix_in_vbl = 0;
}

#endif /* FRUA_UNIX */
