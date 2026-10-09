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
 * sound.cfg, next to the program, whitespace-separated key=value tokens:
 *   music=midi      the music goes to the MIDI port (default: synth)
 *   midi=gm         MT-32 patches mapped to General MIDI (the default)
 *   midi=mt32       patch numbers sent as they are, for a real MT-32
 */
#include <mint/osbind.h>
#include <string.h>
#include "plat_midi.h"
#include "dbglog.h"

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
	int  i, enabled = 0, gm = 1;

	fh = Fopen("sound.cfg", 0);
	if (fh < 0) {
		plat_midi_configure(0, 1);
		return -1;                      /* not opted in — the normal case */
	}
	n = Fread((short)fh, (long)sizeof buf - 1, buf);
	Fclose((short)fh);
	if (n <= 0)
		n = 0;
	buf[n] = '\0';
	for (i = 0; buf[i]; i++)
		if (buf[i] >= 'A' && buf[i] <= 'Z')
			buf[i] = (char)(buf[i] - 'A' + 'a');

	for (i = 0; buf[i]; ) {
		int j = i;
		while (buf[j] && buf[j] != ' ' && buf[j] != '\t'
		       && buf[j] != '\r' && buf[j] != '\n')
			j++;
		if (j > i) {
			char save = buf[j];
			buf[j] = '\0';
			if (strcmp(buf + i, "music=midi") == 0)
				enabled = 1;
			else if (strcmp(buf + i, "music=synth") == 0)
				enabled = 0;
			else if (strcmp(buf + i, "midi=gm") == 0)
				gm = 1;
			else if (strcmp(buf + i, "midi=mt32") == 0)
				gm = 0;
			buf[j] = save;
		}
		i = j;
		while (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n')
			i++;
	}
	plat_midi_configure(enabled, gm);
	dbg_file_str("midi: sound.cfg music=", enabled ? "midi" : "synth");
	if (enabled)
		dbg_file_str("midi: patches=", gm ? "gm" : "mt32");
	return enabled ? 0 : -1;
}
