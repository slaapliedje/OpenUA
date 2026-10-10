/*
 * The Unix half of the MIDI music player (platform/midi_seq.c), for Atari
 * System V on the TT030: the 6850 ACIA behind the MIDI port, reached
 * through /dev/mem. ASV has no MIDI driver — its keyboard driver sets up
 * and services only the keyboard ACIA at $FFFFFC00/02 — so the MIDI
 * registers at $FFFFFC04 (control/status) and $FFFFFC06 (data) are not
 * otherwise reachable from a program. Two ways in, tried in this order:
 *   mmap    the I/O page mapped into the process and the registers read
 *           and written directly, as the ATW800/2 framebuffer is;
 *   devmem  lseek + read/write of single bytes, two system calls a go.
 * OPENUA_MIDI=mmap|devmem forces one, OPENUA_MIDI=off keeps the port shut.
 *
 * TOS leaves the MIDI ACIA with its receive interrupt enabled, on the line
 * it shares with the keyboard ACIA. Nothing under ASV services it, so a
 * byte arriving on MIDI IN would hold the line down and silence the
 * keyboard: the port is opened with control = $15 (8N1, /16, receive
 * interrupt off) and a pending received byte is read away first.
 *
 * sound.cfg next to the program opts in (see midi_out_acia.c).
 */
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "plat_midi.h"
#include "dbglog.h"

#define IO_PAGE_32  0xFFFFF000UL    /* the 32-bit I/O mirror */
#define IO_PAGE_24  0x00FFF000UL    /* the ST image mirror */
#define OFF_CTRL    0xC04
#define OFF_DATA    0xC06

enum { PORT_NONE, PORT_MMAP, PORT_DEVMEM };
static int s_port = PORT_NONE;
static int s_fd = -1;
static volatile unsigned char *s_map;
static unsigned long s_base;

static int rd(unsigned long off, unsigned char *b)
{
	if (s_port == PORT_MMAP) {
		*b = s_map[off];
		return 0;
	}
	if (lseek(s_fd, (off_t)(s_base + off), 0) == (off_t)-1)
		return -1;
	return read(s_fd, (char *)b, 1) == 1 ? 0 : -1;
}

static int wr(unsigned long off, unsigned char b)
{
	if (s_port == PORT_MMAP) {
		s_map[off] = b;
		return 0;
	}
	if (lseek(s_fd, (off_t)(s_base + off), 0) == (off_t)-1)
		return -1;
	return write(s_fd, (char *)&b, 1) == 1 ? 0 : -1;
}

int midi_hw_ready(void)
{
	unsigned char s;

	if (s_port == PORT_NONE)
		return 1;                   /* nothing to wait for */
	return rd(OFF_CTRL, &s) == 0 && (s & 0x02) != 0;
}

void midi_hw_put(unsigned char b)
{
	if (s_port != PORT_NONE)
		(void)wr(OFF_DATA, b);
}

/* The whole queue. A byte takes 320 us; through devmem each status read
 * is two system calls, so far fewer of them bound a port that never comes
 * ready than on the Atari. */
void midi_hw_flush(void)
{
	midi_seq_drain(-1, s_port == PORT_MMAP ? 8000L : 400L);
}

static int try_mmap(void)
{
	static const unsigned long page[2] = { IO_PAGE_32, IO_PAGE_24 };
	int i;

	for (i = 0; i < 2; i++) {
		char *p = (char *)mmap((caddr_t)0, 4096, PROT_READ | PROT_WRITE,
				       MAP_SHARED, s_fd, (off_t)page[i]);
		if (p != (char *)-1) {
			s_map  = (volatile unsigned char *)p;
			s_port = PORT_MMAP;
			return 0;
		}
	}
	return -1;
}

static int try_devmem(void)
{
	static const unsigned long page[2] = { IO_PAGE_32, IO_PAGE_24 };
	unsigned char s;
	int i;

	s_port = PORT_DEVMEM;
	for (i = 0; i < 2; i++) {
		s_base = page[i];
		if (rd(OFF_CTRL, &s) == 0)
			return 0;
	}
	s_port = PORT_NONE;
	return -1;
}

static int open_port(void)
{
	const char *how = getenv("OPENUA_MIDI");
	unsigned char s, d;

	if (how != NULL && strcmp(how, "off") == 0)
		return -1;
	s_fd = open("/dev/mem", O_RDWR);
	if (s_fd < 0)
		return -1;
	if ((how == NULL || strcmp(how, "mmap") == 0) && try_mmap() == 0)
		;
	else if ((how == NULL || strcmp(how, "devmem") == 0) && try_devmem() == 0)
		;
	else {
		close(s_fd);
		s_fd = -1;
		return -1;
	}
	/* receive interrupt off, a pending byte read away (see the top) */
	if (rd(OFF_CTRL, &s) == 0 && (s & 0x01) != 0)
		(void)rd(OFF_DATA, &d);
	(void)wr(OFF_CTRL, 0x15);
	dbg_file_str("midi: port=", s_port == PORT_MMAP ? "mmap" : "devmem");
	return 0;
}

int plat_midi_init(void)
{
	char buf[160];
	long n;
	int  fd;

	fd = open("sound.cfg", O_RDONLY);
	if (fd < 0) {
		plat_midi_configure(0, 1);
		return -1;                  /* not opted in — the normal case */
	}
	n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (midi_cfg_apply(buf, n) != 0)
		return -1;
	if (open_port() != 0) {
		dbg_file_str("midi: ", "no port - the synth keeps the music");
		plat_midi_configure(0, 1);
		return -1;
	}
	return 0;
}
