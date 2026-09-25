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
 * The engine's Mac VBL task (VInstall, the sound sequencer, jt1091) still
 * runs from here: unix_vbl_poll(), called from the input pump, runs one
 * VBL per elapsed 60 Hz tick, and each VBL first tops up the device and
 * then runs the task, as the vertical blank does on the other ports.
 */
#ifdef FRUA_UNIX

#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "plat_sound.h"
#include "sound_mix.h"
#include "input.h"
#include "unix_vbl.h"

/* atari-sysv-sp1 driver-snd/snd.h */
#define SND_IOC		('A' << 8)
#define SND_SETRATE	(SND_IOC | 1)	/* arg = Hz (nearest of 4); returns Hz */
#define SND_SETSTEREO	(SND_IOC | 2)	/* 0 mono, 1 stereo */
#define SND_GETDELAY	(SND_IOC | 5)	/* bytes before a new write plays */

#define LEAD_MS		100		/* how far ahead of the play position */
#define CHUNK		4096		/* the most rendered per write */

static void (*s_vbl_hook)(void);
static unsigned long s_last_tick;
static int s_fd = -1;
static char s_buf[CHUNK];

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
	return 0;
}

void plat_sound_shutdown(void)
{
	s_vbl_hook = NULL;
	mix_set_record(NULL);
	mix_effect_stop();
	mix_tone_off();
	if (s_fd >= 0) {
		close(s_fd);			/* the driver lets queued samples play out */
		s_fd = -1;
	}
}

/* Keep LEAD_MS of samples queued while something is audible. While nothing
 * is, write nothing: the driver plays silence once its queue runs dry, and
 * the mixer and the ioctl cost nothing. */
static void refill(void)
{
	long want, have, n;

	if (s_fd < 0 || !mix_audible())
		return;
	have = ioctl(s_fd, SND_GETDELAY, 0);
	if (have < 0)
		return;
	want = mix_hz * LEAD_MS / 1000 - have;
	while (want > 0) {
		n = want < CHUNK ? want : CHUNK;
		mix_render(s_buf, n);
		if (write(s_fd, s_buf, (size_t)n) != n)
			return;
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
 * stopped, the machine busy) is not replayed in a burst beyond 4. */
void unix_vbl_poll(void)
{
	unsigned long now = unix_ticks();
	int n = 0;

	while (s_last_tick < now && n < 4) {
		plat_sound_vbl();
		s_last_tick++;
		n++;
	}
	s_last_tick = now;
}

#endif /* FRUA_UNIX */
