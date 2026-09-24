/*
 * Sound HAL — Unix, silent for now.
 *
 * The audio plan for AMIX and Atari System V is a sound driver per machine
 * (Paula, the TT's DMA sound) under NAS, the Network Audio System; until
 * then samples and the four-tone synth play silently.
 *
 * What still runs is the Mac VBL task the engine installs (VInstall, the
 * sound sequencer, jt1091): on the other ports the vertical blank drives
 * it, and the engine's music timing leans on it. Here unix_vbl_poll(),
 * called from the input pump, runs it once per elapsed 60 Hz tick.
 */
#ifdef FRUA_UNIX

#include <stddef.h>

#include "plat_sound.h"
#include "input.h"
#include "unix_vbl.h"

static void (*s_vbl_hook)(void);
static unsigned long s_last_tick;

int plat_sound_init(void)
{
	s_last_tick = unix_ticks();
	return 1;			/* no sound: the shim continues silent */
}

void plat_sound_shutdown(void)
{
	s_vbl_hook = NULL;
}

int plat_sound_play_mono8(const signed char *samples, long count, int rate_hz)
{
	(void)samples; (void)count; (void)rate_hz;
	return 0;			/* "played": over at once */
}

void plat_sound_stop(void) { }
int  plat_sound_playing(void) { return 0; }

int plat_sound_synth_start(const void *ftsoundrec)
{
	(void)ftsoundrec;
	return 0;
}

void plat_sound_synth_stop(void) { }

void plat_sound_tone(int count, int amp, int duration_ticks)
{
	(void)count; (void)amp; (void)duration_ticks;
}

void plat_sound_vbl(void)
{
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
