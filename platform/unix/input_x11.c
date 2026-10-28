/*
 * Input HAL — X11 (AMIX, Atari System V, or any X server over TCP).
 *
 * The engine was built against the Atari BIOS keyboard (the Falcon port's
 * native interface): a scancode plus ASCII per key, the Kbshift modifier
 * bitmap, and TOS's US key tables. X delivers KeySyms, so they are mapped
 * onto Atari scancodes here — by KeySym, not keycode, because an X server
 * on the TT and one on an Amiga number their keys differently. The ASCII
 * side then comes from the same TOS tables the Amiga backend uses, so all
 * three ports feed the Event Manager identical key events.
 *
 * X events are pumped from every poll the engine makes: there is no
 * interrupt-driven handler as on the Atari and Amiga. The mouse position is
 * the pointer's in game pixels ((window - game origin) / scale). Ticks are Mac 60 Hz ticks
 * from gettimeofday.
 */
#ifdef FRUA_UNIX

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <sys/types.h>
#include <sys/time.h>
#include <poll.h>
#include <stdlib.h>

#include "input.h"
#include "x11_unix.h"
#include "unix_vbl.h"
#include "dbglog.h"

extern int gettimeofday(struct timeval *, void *);
extern int poll(struct pollfd *, unsigned long, int);

/* --- ticks, and waiting ---------------------------------------------------- */

static struct timeval s_t0;

/* elapsed time since the clock started, in ms */
static long elapsed_ms(void)
{
	static long last;
	struct timeval t;
	long ms;
	int tries;

	/* AMIX's gettimeofday is an hrtcntl system call: if it ever fails, t
	 * holds whatever the stack had and time jumps. Ask again, and never
	 * let the clock run backwards. */
	for (tries = 0; gettimeofday(&t, 0) < 0; tries++)
		if (tries > 100)
			return last;
	if (s_t0.tv_sec == 0)
		s_t0 = t;
	/* whole seconds and the microsecond parts separately: a microsecond
	 * total would overflow 32 bits after 35 minutes */
	ms = (t.tv_sec - s_t0.tv_sec) * 1000L
	   + (long)t.tv_usec / 1000 - (long)s_t0.tv_usec / 1000;
	if (ms < last)
		ms = last;
	last = ms;
	return ms;
}

/*
 * One clock read per entry. On the real TT a system call costs ~0.4 ms
 * under amx and gettimeofday (an hrtcntl) 0.7 ms; an engine call used to
 * read the clock five or six times on its way down (the ticks, the VBL,
 * the sound's refill, the MIDI statistics, the X flush), 4 ms of kernel
 * time per TickCount, 87% of the machine in a menu. The entry points
 * take one sample and hold it; everything below reads the sample.
 */
static int  s_held;
static long s_held_ms;

/* the loop's rate (OPENUA_LOOPLOG=1), logged every 5 s for the first
 * minute: how many times the engine came in, asked X, got an event,
 * slept, read the clock */
static long s_n_pump, s_n_xask, s_n_xev, s_n_sleep, s_n_clock, s_n_ticks;
static long s_rate_ms;
static int  s_rate_logged;
static int  s_rate_on = -1;

static void rate_log(long ms)
{
	if (s_rate_on < 0)
		s_rate_on = getenv("OPENUA_LOOPLOG") != NULL;
	if (!s_rate_on || s_rate_logged >= 12 || ms - s_rate_ms < 5000)
		return;
	if (s_rate_ms != 0) {
		dbg_file_num("loop: per 5 s, engine polls ", s_n_pump);
		dbg_file_num("loop:   asked X ", s_n_xask);
		dbg_file_num("loop:   X events ", s_n_xev);
		dbg_file_num("loop:   slept ", s_n_sleep);
		dbg_file_num("loop:   clock reads ", s_n_clock);
		dbg_file_num("loop:   ticks seen by plat_ticks ", s_n_ticks);
		s_rate_logged++;
	}
	s_rate_ms = ms;
	s_n_pump = s_n_xask = s_n_xev = s_n_sleep = s_n_clock = s_n_ticks = 0;
}

static long sample_ms(void)
{
	return s_held ? s_held_ms : elapsed_ms();
}

void unix_clock_hold(void)
{
	if (s_held++ == 0) {
		s_held_ms = elapsed_ms();
		s_n_clock++;
		rate_log(s_held_ms);
	}
}

void unix_clock_release(void)
{
	if (s_held > 0)
		s_held--;
}

unsigned long unix_ticks(void)
{
	return (unsigned long)(sample_ms() * 60 / 1000);
}

unsigned long unix_ms(void)
{
	return (unsigned long)sample_ms();
}

/*
 * The engine waits the way it did on the Atari: it polls TickCount and the
 * input until something changes. On a TOS machine that only burns an idle
 * CPU; on Unix every turn of such a loop is system calls (the clock, X),
 * so an OpenUA sitting in a menu took the whole machine. So: when the
 * engine asks for the time or its input SPIN_LIMIT times within one tick
 * and nothing has happened, it is waiting - sleep on the X connection
 * until the next tick, or until X has input for us. Measured at the main
 * menu (emulated TT): the wait loop asks ~15 times per tick, each turn
 * nearly all system calls; with the limit at 6 the machine went from 0%
 * to 98% idle, and the boot to the menu (real work) took as long as
 * before. On the real TT each ask is a 0.7 ms system call, so six of
 * them were a third of every tick before the sleep began: 3 now, one
 * turn of the engine's wait loop (its input polls and TickCount).
 */
#define SPIN_LIMIT	3

static unsigned long s_spin_tick;
static int           s_spin_n;
static unsigned long s_xchk_tick;	/* the tick X was last asked in */
static int           s_x_due = 1;	/* ask X again before the next tick */

static void idle_if_spinning(unsigned long now)
{
	Display *d;
	struct pollfd pfd;
	long ms;

	if (now != s_spin_tick) {
		s_spin_tick = now;
		s_spin_n = 0;
		return;
	}
	if (++s_spin_n < SPIN_LIMIT)
		return;
	s_spin_n = 0;
	x11_flush_due();
	d = x11_display();
	/* events Xlib has already read need no system call to see; the
	 * socket itself is what poll watches */
	if (d != NULL && XEventsQueued(d, QueuedAlready) > 0) {
		s_x_due = 1;			/* input already waiting */
		return;
	}
	ms = (long)((now + 1) * 1000 / 60) - sample_ms() + 1;
	if (ms <= 0)
		return;
	pfd.fd = d != NULL ? ConnectionNumber(d) : -1;
	pfd.events = POLLIN;
	pfd.revents = 0;
	s_n_sleep++;
	if (poll(&pfd, d != NULL ? 1 : 0, (int)ms) > 0)
		s_x_due = 1;			/* woken by X: drain it next */
}

unsigned long plat_ticks(void)
{
	unsigned long now;

	/* The engine's VBL task asks for the time too (jt1091 -> jt1149): that
	 * is not the engine waiting, so no flush, no idle sleep (the TOS build
	 * does the same under g_plat_in_super) - and the time is the sound's,
	 * which the task runs by (sound_unix.c, run_seq). */
	if (unix_in_vbl)
		return unix_seq_now;
	unix_clock_hold();
	now = unix_ticks();
	s_n_ticks++;
	/* An engine waiting on the clock (a wipe, a pause, a picture paced
	 * row by row) may not pump input for seconds: the VBL still has to
	 * run, as the interrupt does on the Atari. */
	unix_vbl_site = "clock";
	unix_vbl_poll();
	x11_flush_due();
	idle_if_spinning(now);
	unix_clock_release();
	return now;
}

/* --- TOS US keytables by Atari scancode (as input_amiga.c) ---------------- */

static const unsigned char k_tos_unshift[128] = {
	[0x01] = 27,
	[0x02] = '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
	[0x0E] = 8, 9,
	[0x10] = 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 13,
	[0x1E] = 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
	[0x2B] = '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
	[0x39] = ' ',
	[0x4A] = '-',                       /* KP- */
	[0x4E] = '+',                       /* KP+ */
	[0x53] = 127,                       /* Del */
	[0x63] = '(', ')', '/', '*',        /* keypad chrome */
	[0x67] = '7', '8', '9', '4', '5', '6', '1', '2', '3', '0', '.', 13,
};

static const unsigned char k_tos_shift[128] = {
	[0x01] = 27,
	[0x02] = '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
	[0x0E] = 8, 9,
	[0x10] = 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 13,
	[0x1E] = 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
	[0x2B] = '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
	[0x39] = ' ',
	[0x47] = '7', '8',                  /* Home, Up */
	[0x4A] = '-',
	[0x4B] = '4',                       /* Left */
	[0x4D] = '6', '+',                  /* Right, KP+ */
	[0x50] = '2',                       /* Down */
	[0x52] = '0', 127,                  /* Insert, Del */
	[0x63] = '(', ')', '/', '*',
	[0x67] = '7', '8', '9', '4', '5', '6', '1', '2', '3', '0', '.', 13,
};

/* Non-printing keys: KeySym -> Atari scancode. */
static const struct { KeySym ks; unsigned char scan; } k_special[] = {
	{ XK_Escape, 0x01 }, { XK_BackSpace, 0x0E }, { XK_Tab, 0x0F },
	{ XK_Return, 0x1C }, { XK_KP_Enter, 0x72 }, { XK_Delete, 0x53 },
	{ XK_Insert, 0x52 }, { XK_Home, 0x47 }, { XK_Help, 0x62 },
	{ XK_Undo, 0x61 }, { XK_Up, 0x48 }, { XK_Down, 0x50 },
	{ XK_Left, 0x4B }, { XK_Right, 0x4D },
	{ XK_F1, 0x3B }, { XK_F2, 0x3C }, { XK_F3, 0x3D }, { XK_F4, 0x3E },
	{ XK_F5, 0x3F }, { XK_F6, 0x40 }, { XK_F7, 0x41 }, { XK_F8, 0x42 },
	{ XK_F9, 0x43 }, { XK_F10, 0x44 },
	{ XK_KP_0, 0x70 }, { XK_KP_1, 0x6D }, { XK_KP_2, 0x6E }, { XK_KP_3, 0x6F },
	{ XK_KP_4, 0x6A }, { XK_KP_5, 0x6B }, { XK_KP_6, 0x6C }, { XK_KP_7, 0x67 },
	{ XK_KP_8, 0x68 }, { XK_KP_9, 0x69 }, { XK_KP_Decimal, 0x71 },
	{ XK_KP_Subtract, 0x4A }, { XK_KP_Add, 0x4E }, { XK_KP_Divide, 0x65 },
	{ XK_KP_Multiply, 0x66 },
};

/* KeySym -> Atari scancode: the special keys, then any printable character
 * found in the TOS tables (unshifted first, so 'a' and 'A' both give 0x1E).
 * 0 = no Atari equivalent. */
static unsigned char keysym_to_scan(KeySym ks)
{
	unsigned i;

	for (i = 0; i < sizeof k_special / sizeof k_special[0]; i++)
		if (k_special[i].ks == ks)
			return k_special[i].scan;
	if (ks >= 0x20 && ks < 0x7F) {
		for (i = 1; i < 0x60; i++)
			if (k_tos_unshift[i] == ks)
				return (unsigned char)i;
		for (i = 1; i < 0x60; i++)
			if (k_tos_shift[i] == ks)
				return (unsigned char)i;
	}
	return 0;
}

/* X modifier state -> the Atari Kbshift bitmap the shim translates
 * (bit0 RSHIFT, bit1 LSHIFT, bit2 CTRL, bit3 ALT, bit4 CAPS). Alt and Meta
 * (Mod1) land on ALT: the shim turns it into the Mac cmdKey. */
static unsigned char state_to_kbshift(unsigned int st)
{
	unsigned char k = 0;

	if (st & ShiftMask)   k |= 0x02;
	if (st & ControlMask) k |= 0x04;
	if (st & Mod1Mask)    k |= 0x08;
	if (st & LockMask)    k |= 0x10;
	return k;
}

/* --- the event pump ---------------------------------------------------------- */

#define KB_RING 32
static unsigned short s_kb_ring[KB_RING];	/* (kbshift << 8) | scan */
static int s_kb_head, s_kb_tail;
static unsigned char s_kbshift;

static short s_mx, s_my, s_scr_w = 320, s_scr_h = 200;
static int s_btn, s_click;

static void pump(void)
{
	Display *d = x11_display();
	int scale = x11_scale(), ox, oy;
	unsigned long now;

	x11_origin(&ox, &oy);

	unix_clock_hold();
	now = unix_ticks();
	s_n_pump++;
	unix_vbl_site = "input";
	unix_vbl_poll();		/* the engine's VBL task, 60 Hz */
	x11_flush_due();
	if (d == NULL) {
		unix_clock_release();
		return;
	}
	/* X is asked once a tick (a system call, 0.4 ms on the TT): the
	 * Atari polls its input at the VBL too. The idle sleep wakes on X
	 * input and marks it due. */
	if (!s_x_due && now == s_xchk_tick) {
		idle_if_spinning(now);
		unix_clock_release();
		return;
	}
	s_xchk_tick = now;
	s_x_due = 0;
	s_n_xask++;
	if (XPending(d) == 0) {
		idle_if_spinning(now);		/* asked again, nothing new */
		unix_clock_release();
		return;
	}
	s_spin_n = 0;
	while (XPending(d) > 0) {
		XEvent ev;
		XNextEvent(d, &ev);
		s_n_xev++;
		switch (ev.type) {
		case KeyPress: {
			char buf[8];
			KeySym ks;
			unsigned char scan;

			XLookupString(&ev.xkey, buf, sizeof buf, &ks, NULL);
			s_kbshift = state_to_kbshift(ev.xkey.state);
			scan = keysym_to_scan(ks);
			if (scan != 0 && (s_kb_head + 1) % KB_RING != s_kb_tail) {
				s_kb_ring[s_kb_head] = (unsigned short)((s_kbshift << 8) | scan);
				s_kb_head = (s_kb_head + 1) % KB_RING;
			}
			break;
		}
		case KeyRelease:
			s_kbshift = state_to_kbshift(ev.xkey.state);
			break;
		case ButtonPress:
			s_btn = 1;
			s_click = 1;
			/* the press carries a position too */
			/* fall through */
		case ButtonRelease:
			if (ev.type == ButtonRelease)
				s_btn = 0;
			s_mx = (short)((ev.xbutton.x - ox) / scale);
			s_my = (short)((ev.xbutton.y - oy) / scale);
			break;
		case MotionNotify:
			s_mx = (short)((ev.xmotion.x - ox) / scale);
			s_my = (short)((ev.xmotion.y - oy) / scale);
			break;
		case Expose:
			if (ev.xexpose.count == 0)
				x11_repaint();
			break;
		default:
			break;
		}
	}
	if (s_mx < 0) s_mx = 0;
	if (s_my < 0) s_my = 0;
	if (s_mx >= s_scr_w) s_mx = (short)(s_scr_w - 1);
	if (s_my >= s_scr_h) s_my = (short)(s_scr_h - 1);
	unix_clock_release();
}

int plat_kb_poll(unsigned char *out_scan, unsigned char *out_ascii)
{
	unsigned short entry;
	unsigned char scan, kbs, ascii;

	pump();
	if (s_kb_tail == s_kb_head)
		return 0;
	entry = s_kb_ring[s_kb_tail];
	s_kb_tail = (s_kb_tail + 1) % KB_RING;
	scan = (unsigned char)(entry & 0xFF);
	kbs  = (unsigned char)(entry >> 8);

	if (kbs & 0x08) {
		/* Alt chords deliver NO character, exactly like TOS's alternate
		 * keytable: the Event Manager recovers the letter from
		 * plat_kb_unshifted_char and forces cmdKey. */
		ascii = 0;
	} else {
		ascii = (kbs & 0x03) ? k_tos_shift[scan & 0x7F]
		                     : k_tos_unshift[scan & 0x7F];
		if ((kbs & 0x10) && ascii >= 'a' && ascii <= 'z')
			ascii = (unsigned char)(ascii - 'a' + 'A');
		if ((kbs & 0x04) && (ascii & 0x40))
			ascii &= 0x1F;              /* Ctrl-letter, TOS-style */
	}
	DBG_TRACE("key", scan, ascii);
#ifdef FRUA_BEEPTRACE
	/* one line per key taken from X: two engine reads of one key show as
	 * two ph5 lines after one of these (see the #11/#132 trace) */
	dbg_file_num("beep: kb poll tick*1000+scan = ",
	             (long)(unix_ticks() % 1000000L) * 1000L + scan);
#endif
	if (out_scan)  *out_scan  = scan;
	if (out_ascii) *out_ascii = ascii;
	return 1;
}

int plat_kb_avail(void)
{
	pump();
	return s_kb_tail != s_kb_head;
}

unsigned char plat_kb_shift(void)
{
	pump();
	return s_kbshift;
}

unsigned char plat_kb_unshifted_char(unsigned char scan)
{
	return k_tos_unshift[scan & 0x7F];
}

/* --- mouse ---------------------------------------------------------------- */

void plat_mouse_pos(short *h, short *v)
{
	pump();
	if (h) *h = s_mx;
	if (v) *v = s_my;
}

int plat_mouse_btn(void)
{
	pump();
	return s_btn;
}

int plat_mouse_click_pending(void)
{
	pump();
	return s_click;
}

int plat_mouse_take_click(void)
{
	int c;

	pump();
	c = s_click;
	s_click = 0;
	return c;
}

void plat_input_init(short screen_w, short screen_h)
{
	s_scr_w = screen_w;
	s_scr_h = screen_h;
	s_mx = (short)(screen_w / 2);
	s_my = (short)(screen_h / 2);
	(void)unix_ticks();		/* start the clock */
}

void plat_input_shutdown(void)
{
}

#endif /* FRUA_UNIX */
