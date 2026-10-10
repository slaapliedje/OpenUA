/*
 * The Atari half of the MIDI music player (platform/midi_seq.c): the 6850
 * ACIA behind the MIDI port, and the sound.cfg that opts in.
 *
 * The ACIA sits at $FFFC04 (control/status) and $FFFC06 (data). TOS sets it
 * up at boot (31250 baud, 8N1); we only ever write data when the status
 * says the transmitter is empty (bit 1). The registers are supervisor-only,
 * so the main loop's flush runs under Supexec; the sound vblank is already
 * supervisor.
 *
 * sound.cfg, next to the program, opts in (midi_cfg_apply in midi_seq.c
 * reads it: music=midi|synth, midi=gm|mt32).
 */
#include <mint/osbind.h>
#include "plat_midi.h"

#define MIDI_ACIA_CTRL (*(volatile unsigned char *)0xFFFFFC04UL)
#define MIDI_ACIA_DATA (*(volatile unsigned char *)0xFFFFFC06UL)

int midi_hw_ready(void)
{
	return (MIDI_ACIA_CTRL & 0x02) != 0;
}

void midi_hw_put(unsigned char b)
{
	MIDI_ACIA_DATA = b;
}

int midi_hw_vbl_bytes(void)
{
	return 12;                          /* ~4 ms of the port's time */
}

long midi_hw_lead_us(void)
{
	return 0;                           /* the ACIA plays at once */
}

void midi_hw_commit(void)
{
}

void midi_hw_discard(void)
{
}

/* The whole queue, from the main loop. One byte takes 320 us at 31250
 * baud, a few hundred status reads; 8000 bounds a dead port to a few
 * milliseconds per poll instead of a hang. */
static long drain_super(void)
{
	midi_seq_drain(-1, 8000L);
	return 0;
}

void midi_hw_flush(void)
{
	Supexec(drain_super);
}

int plat_midi_init(void)
{
	char buf[160];
	long fh, n;

	fh = Fopen("sound.cfg", 0);
	if (fh < 0) {
		plat_midi_configure(0, 1);
		return -1;                      /* not opted in — the normal case */
	}
	n = Fread((short)fh, (long)sizeof buf - 1, buf);
	Fclose((short)fh);
	return midi_cfg_apply(buf, n);
}
