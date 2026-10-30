/*
 * The Amiga half of the MIDI music player (platform/midi_seq.c): Paula's
 * UART behind the serial port, which is how every Amiga MIDI interface is
 * wired (a level shifter on the 25-pin serial connector), and the sound.cfg
 * that opts in.
 *
 * The UART is three custom-chip registers: SERPER sets the bit period,
 * SERDAT takes a byte with its stop bit in bit 8, SERDATR bit 13 (TBE) says
 * the holding register is free. 31250 baud is (3546895 / 31250) - 1 = 113 on
 * a PAL machine, 114 on NTSC. The registers are not privileged, so the
 * main loop's flush drains directly; the VERTB server runs the vblank half.
 *
 * The port is driven directly, not through serial.device: the sequencer
 * sends from the vblank interrupt, where a device I/O request cannot be
 * issued, and a request per byte would never keep a dense score in time.
 * That is what the Amiga MIDI programs (Bars & Pipes, Dr. T's KCS) did too.
 * Nothing else may hold the serial port while the game runs with
 * music=midi; the TBE interrupt is masked for the run and put back on exit.
 *
 * sound.cfg, next to the program, opts in (midi_cfg_apply in midi_seq.c
 * reads it: music=midi|synth, midi=gm|mt32).
 */
#include <exec/types.h>
#include <exec/execbase.h>
#include <hardware/custom.h>
#include <hardware/intbits.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "plat_midi.h"

extern struct ExecBase *SysBase;

#define CUSTOM ((volatile struct Custom *)0xDFF000)

#define SERDATR_TBE   0x2000            /* transmit buffer empty */
#define SERDAT_STOP   0x0100            /* the stop bit above 8 data bits */
#define SERPER_PAL    113               /* 3546895 Hz / 31250 - 1, rounded */
#define SERPER_NTSC   114               /* 3579545 Hz / 31250 - 1, rounded */

static int           s_port_up;         /* SERPER programmed, TBE masked */
static int           s_tbe_was_enabled; /* INTENA TBE before we masked it */

/* The sequencer wants the Atari's 200 Hz count; the Amiga has vblanks at
 * 50 or 60 Hz. Accumulate 200 against the vblank rate: PAL adds exactly 4
 * a frame, NTSC 3,3,4 — the same arithmetic sound_paula.c uses for the
 * Mac's 60 Hz tempo. */
static unsigned long s_hz200;
static short         s_acc;
static short         s_vbl_hz = 50;

int midi_hw_ready(void)
{
	return (CUSTOM->serdatr & SERDATR_TBE) != 0;
}

void midi_hw_put(unsigned char b)
{
	CUSTOM->serdat = (UWORD)(SERDAT_STOP | b);
}

int midi_hw_vbl_bytes(void)
{
	return 12;                          /* ~4 ms of the port's time */
}

long midi_hw_lead_us(void)
{
	return 0;                           /* the UART plays at once */
}

void midi_hw_commit(void)
{
}

void midi_hw_discard(void)
{
}

/* The whole queue, from the main loop. One byte takes 320 us at 31250
 * baud; 8000 status reads bound a dead port to a few milliseconds per
 * poll instead of a hang. The vblank stands down meanwhile (g_busy in
 * midi_seq.c), so the two never write SERDAT together. */
void midi_hw_flush(void)
{
	midi_seq_drain(-1, 8000L);
}

static void port_open(void)
{
	s_vbl_hz = (short)SysBase->VBlankFrequency;
	if (s_vbl_hz < 50 || s_vbl_hz > 60)
		s_vbl_hz = 50;
	s_tbe_was_enabled = (CUSTOM->intenar & INTF_TBE) != 0;
	CUSTOM->intena = INTF_TBE;          /* clear: no serial.device-style TBE IRQ */
	CUSTOM->serper = (UWORD)(s_vbl_hz == 60 ? SERPER_NTSC : SERPER_PAL);
	s_port_up = 1;
}

static void port_close(void)
{
	if (!s_port_up)
		return;
	/* let the last byte leave before the port changes hands */
	{
		long w = 8000;
		while (!(CUSTOM->serdatr & 0x1000) && w-- > 0)   /* TSRE */
			;
	}
	CUSTOM->intreq = INTF_TBE;          /* drop the request our writes raised */
	if (s_tbe_was_enabled)
		CUSTOM->intena = (UWORD)(INTF_SETCLR | INTF_TBE);
	s_port_up = 0;
}

/* From sound_paula.c's VERTB path, every frame. */
void midi_amiga_vbl(void)
{
	if (!s_port_up)
		return;
	s_acc += 200;
	while (s_acc >= s_vbl_hz) {
		s_acc -= s_vbl_hz;
		s_hz200++;
	}
	plat_midi_vbl(s_hz200);
}

int plat_midi_init(void)
{
	char buf[160];
	BPTR fh;
	LONG n;

	fh = Open((CONST_STRPTR)"sound.cfg", MODE_OLDFILE);
	if (!fh) {
		plat_midi_configure(0, 1);
		return -1;                      /* not opted in — the normal case */
	}
	n = Read(fh, buf, (LONG)sizeof buf - 1);
	Close(fh);
	if (midi_cfg_apply(buf, n) != 0)
		return -1;
	port_open();
	return 0;
}

/* From plat_sound_shutdown: the sequencer's notes-off, then the port
 * goes back to whatever held it. */
void midi_amiga_shutdown(void)
{
	if (!s_port_up)
		return;
	plat_midi_shutdown();               /* notes off, drained through the port */
	port_close();
}
