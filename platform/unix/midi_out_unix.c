/*
 * The Unix half of the MIDI music player (platform/midi_seq.c), for Atari
 * System V on the TT030: the 6850 ACIA behind the MIDI port. Three ways
 * to it, tried in this order:
 *   driver  atari-sysv-sp1's midi driver: /dev/midit, a timed stream the
 *           kernel plays from its clock (the ACIA's transmit interrupt
 *           moves the bytes; one write a poll from here), or /dev/midi,
 *           the same without the waits;
 *   mmap    the I/O page mapped into the process and the registers read
 *           and written directly, as the ATW800/2 framebuffer is;
 *   devmem  lseek + read/write of single bytes, two system calls a go.
 * The last two exist because the stock kernel has no MIDI driver - its
 * keyboard driver sets up and services only the keyboard ACIA at
 * $FFFFFC00/02, and the MIDI registers at $FFFFFC04/06 are otherwise out
 * of a program's reach. OPENUA_MIDI=driver|mmap|devmem forces one,
 * OPENUA_MIDI=off keeps the port shut.
 *
 * System calls are slow here: on the real TT one costs 500-800 us, so a
 * status read before every byte would make a byte cost 3 ms, four times
 * what the port needs. Through devmem the write itself is two system calls,
 * so consecutive writes are already further apart than a byte takes to
 * leave the transmit register (320 us at 31250 baud), and the status read
 * tells nothing new. When a status read measures at least PACE_MIN_US at
 * open, the port runs paced: no status read per byte. A faster machine
 * keeps the per-byte check. The vblank's budget shrinks with it: the input
 * pump only sleeps when a poll fits in a tick, so a poll that spent 50 ms
 * on MIDI bytes turned an idle menu into a spinning one.
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
#include <sys/time.h>
#include <sys/ioctl.h>
#include "plat_midi.h"
#include "dbglog.h"
#include "unix_vbl.h"

/* atari-sysv-sp1 driver-midi/midi.h */
#define MIDI_IOC        ('M' << 8)
#define MIDI_FLUSH      (MIDI_IOC | 1)  /* drop what is queued; the timeline restarts */
#define MIDI_GETQUEUED  (MIDI_IOC | 2)  /* returns bytes not yet sent */
#define MIDI_STATS      (MIDI_IOC | 4)  /* arg = struct midi_stats * */
struct midi_stats {
	unsigned long bytes, intrs, waits, late, snaps, kicks, queued, hz;
};
#define DRIVER_LEAD_US  1000000L        /* how far ahead the score runs on /dev/midit */
#define STAGE           1024            /* bytes a drain stages for one write */

extern int gettimeofday(struct timeval *, void *);   /* 1991 headers: no prototype */

#define IO_PAGE_32  0xFFFFF000UL    /* the 32-bit I/O mirror */
#define IO_PAGE_24  0x00FFF000UL    /* the ST image mirror */
#define OFF_CTRL    0xC04
#define OFF_DATA    0xC06
#define PACE_MIN_US 700             /* a status read this slow (2.2 byte times):
                                       writes pace themselves */
#define VBL_US      8000            /* MIDI time per tick: half of one */

enum { PORT_NONE, PORT_DRIVER, PORT_MMAP, PORT_DEVMEM };
static int s_port = PORT_NONE;
static int s_fd = -1;
static volatile unsigned char *s_map;
static unsigned long s_base;
static int s_paced;                 /* devmem: no status read per byte (see the top) */
static int s_vbl_bytes = 12;        /* what VBL_US buys */
static long s_wfail, s_wfail_logged; /* writes the kernel refused */
static int s_timed;                 /* driver: /dev/midit, waits honoured */
static unsigned char s_stage[STAGE]; /* driver: a drain's bytes, written at the commit */
static int s_staged;

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

	if (s_port == PORT_DRIVER)
		return s_staged < STAGE;    /* room to stage */
	if (s_port == PORT_NONE || s_paced)
		return 1;                   /* nothing to wait for */
	return rd(OFF_CTRL, &s) == 0 && (s & 0x02) != 0;
}

void midi_hw_put(unsigned char b)
{
	if (s_port == PORT_DRIVER) {
		if (s_staged < STAGE)
			s_stage[s_staged++] = b;
		return;
	}
	if (s_port != PORT_NONE && wr(OFF_DATA, b) < 0)
		s_wfail++;
}

/* driver: its statistics every 10 s of play, the first two minutes - the
 * waits it honoured, how many were already due (the writer behind), how
 * often its timeline restarted (a stall longer than the lead) */
static void driver_stats(void)
{
	static unsigned long last_ms;
	static int logged;
	struct midi_stats st;
	unsigned long ms = unix_ms();

	if (logged >= 12 || ms - last_ms < 10000)
		return;
	last_ms = ms;
	logged++;
	if (ioctl(s_fd, MIDI_STATS, &st) < 0)
		return;
	dbg_file_num("midi: driver bytes=", (long)st.bytes);
	dbg_file_num("midi: driver waits=", (long)st.waits);
	dbg_file_num("midi: driver late=", (long)st.late);
	dbg_file_num("midi: driver snaps=", (long)st.snaps);
	dbg_file_num("midi: driver kicks=", (long)st.kicks);
	dbg_file_num("midi: driver queued=", (long)st.queued);
}

/* driver: the staged bytes in one write (the kernel queues 8 KB) */
void midi_hw_commit(void)
{
	int done = 0, n;

	if (s_port != PORT_DRIVER)
		return;
	driver_stats();
	while (done < s_staged) {
		n = (int)write(s_fd, (char *)s_stage + done, s_staged - done);
		if (n <= 0) {
			s_wfail++;
			break;
		}
		done += n;
	}
	s_staged = 0;
}

void midi_hw_discard(void)
{
	if (s_port == PORT_DRIVER) {
		s_staged = 0;
		(void)ioctl(s_fd, MIDI_FLUSH, 0);
	}
}

long midi_hw_lead_us(void)
{
	return s_port == PORT_DRIVER && s_timed ? DRIVER_LEAD_US : 0;
}

/* When the queue has fallen behind - a dense passage while the engine
 * draws, polling rarely - the music's timing matters more than the
 * engine's pace: four times the budget until it has caught up. */
int midi_hw_vbl_bytes(void)
{
	if (s_port == PORT_DRIVER)
		return STAGE;               /* one write's worth; the rest next poll */
	return midi_seq_queued() > 64 ? 4 * s_vbl_bytes : s_vbl_bytes;
}

static int try_driver(void)
{
	s_fd = open("/dev/midit", O_WRONLY);
	s_timed = s_fd >= 0;
	if (s_fd < 0)
		s_fd = open("/dev/midi", O_WRONLY);
	if (s_fd < 0)
		return -1;
	s_port = PORT_DRIVER;
	s_staged = 0;
	return 0;
}

/* The whole queue. A byte takes 320 us; through devmem each status read
 * is two system calls, so far fewer of them bound a port that never comes
 * ready than on the Atari. */
void midi_hw_flush(void)
{
	if (s_wfail != s_wfail_logged && s_wfail_logged < 8) {
		s_wfail_logged = s_wfail;
		dbg_file_num("midi: port writes refused: ", s_wfail);
	}
	if (s_port == PORT_DRIVER) {
		/* everything, STAGE bytes a write */
		while (midi_seq_pending())
			midi_seq_drain(STAGE, 1);
		return;
	}
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
	if ((how == NULL || strcmp(how, "driver") == 0) && try_driver() == 0) {
		dbg_file_str("midi: port=", s_timed ? "driver, timed" : "driver, raw");
		return 0;
	}
	if (how != NULL && strcmp(how, "driver") == 0)
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
	if (s_port == PORT_DEVMEM) {
		struct timeval t0, t1;
		long us;
		int  i;

		gettimeofday(&t0, 0);
		for (i = 0; i < 8; i++)
			(void)rd(OFF_CTRL, &s);
		gettimeofday(&t1, 0);
		us = ((t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_usec - t0.tv_usec)) / 8;
		s_paced = us >= PACE_MIN_US;
		/* a byte is a write pair (paced) or two pairs; never below 2 */
		if (us > 0) {
			s_vbl_bytes = (int)(VBL_US / (s_paced ? us : 2 * us));
			if (s_vbl_bytes < 2)
				s_vbl_bytes = 2;
			if (s_vbl_bytes > 12)
				s_vbl_bytes = 12;
		}
		dbg_file_num("midi: devmem status read us=", us);
		dbg_file_str("midi: devmem ", s_paced ? "paced" : "checked");
		dbg_file_num("midi: bytes per poll=", s_vbl_bytes);
	}
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
