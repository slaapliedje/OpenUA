/*
 * Input HAL — Falcon / TT implementation. See input.h.
 *
 * _hz_200 (the 200 Hz system tick at 0x4BA) → 60 Hz Mac ticks; BIOS
 * Bconstat / Bconin (device 2 = console) for the keyboard; Kbshift for
 * modifier state; the IKBD mousevec for mouse. The IKBD on the ST/Falcon
 * sends 3-byte mouse packets to a callback registered in the keyboard
 * table — install_supervisor patches that vector so each packet drives
 * g_mouse_x / g_mouse_y / g_mouse_btn.
 */

#include <stddef.h>             /* NULL */
#include <mint/osbind.h>

#include "input.h"

/*
 * Keyboard vector table — the layout that Kbdvbase() points at. Mintlib
 * doesn't always expose it as a typed struct, so spell it out: the only
 * field this file touches is mousevec.
 */
typedef struct {
	void (*midivec)(void);
	void (*vkbderr)(void);
	void (*vmiderr)(void);
	void (*statvec)(void *);
	void (*mousevec)(unsigned char *packet);
	void (*clockvec)(void *);
	void (*joyvec)(void *);
	void (*midisys)(void);
	void (*ikbdsys)(void);
} kbdvecs_t;

static long read_hz200(void)
{
	return *(volatile long *)0x4BAL;
}

/* Set while the sound vblank is running the engine's sound task. Supexec is a
 * TRAP, and trapping from inside an interrupt handler is fatal — but a VBL
 * handler is ALREADY in supervisor mode, so it can read _hz_200 straight. The
 * sequencer asks for the tick on every vblank (jt1091 -> jt1149 -> TickCount),
 * so this path is not hypothetical: without the flag it bus-errors within
 * seconds. */
volatile int g_plat_in_super;

unsigned long plat_ticks(void)
{
	/* 200 Hz → 60 Hz: *60/200 = *3/10. The unsigned-long arithmetic
	 * gives us ~248 days of run-time before the 60 Hz counter wraps,
	 * comfortably outside any plausible session. */
	unsigned long h200 = g_plat_in_super
	                   ? (unsigned long)read_hz200()
	                   : (unsigned long)Supexec(read_hz200);

	return (h200 * 3UL) / 10UL;
}

#ifdef FRUA_AUTOPLAY
/*
 * Headless auto-drive (test only, -DFRUA_AUTOPLAY). The STE colour build is slow
 * enough that scripted real-time keystrokes get dropped before the engine's event
 * loop polls them (the #41 input-lag symptom), so the dungeon/combat screens are
 * unreachable from outside. This injects the play-entry keystrokes FROM INSIDE,
 * paced by the 60 Hz engine clock, so each key lands only once the previous
 * transition has drained — no real-time dependency, no dropped keys.
 *
 * Sequence mirrors tools/hatari_ui.sh `beginplay`: p (Play -> Training Hall),
 * a (Add Character -> the seeded roster, BARBARUS highlighted), Return (add it),
 * Escape (back to the hall), b (Begin Adventuring -> the dungeon), then a
 * Right/Left nudge to force the first 3D paint. delay = ticks to wait AFTER this
 * key before the next (a MINIMUM — the engine only consumes when it next polls).
 */
struct ap_key { unsigned char scan, ascii; unsigned short delay; };
static const struct ap_key g_ap[] = {
	{ 0x19, 'p',  600 },    /* Play the Game -> Training Hall (10s)  */
	{ 0x1E, 'a',  600 },    /* Add Character -> seeded roster list   */
	{ 0x50, 0,    300 },    /* Down -> give the list focus + select BARBARUS */
	{ 0x1C, 0x0D, 600 },    /* Return -> add the selected (* BARBARUS)   */
	{ 0x01, 0x1B, 600 },    /* Escape -> back to the hall            */
	{ 0x30, 'b',  900 },    /* Begin Adventuring -> dungeon (15s art)*/
#ifndef FRUA_CBTKEYDIAG
	/* #62 diag runs drop the nudge keys: they queue past the CBTAUTO fire
	 * and get consumed as combat moves, polluting the command-read test. */
	{ 0x4D, 0,    300 },    /* Right (nudge: force the 3D paint)     */
	{ 0x4B, 0,    300 },    /* Left  (net-zero facing)               */
#endif
};
#define AP_N ((short)(sizeof g_ap / sizeof g_ap[0]))
static short         g_ap_idx;
static unsigned long g_ap_next;         /* engine tick the next key is due */
static int           g_ap_started;
/* Armed by the engine when the main menu is actually up (boot.c, at the
 * "menu: modal up" marker). Without this the boot sequence's own event polling
 * eats the first keys before the menu can consume them. */
volatile int         g_ap_armed;

/* Is the next scripted key due now? (non-consuming; used by plat_kb_avail) */
static int ap_due(void)
{
	if (!g_ap_armed || g_ap_idx >= AP_N)
		return 0;
	if (!g_ap_started) {
		g_ap_started = 1;
		g_ap_next = plat_ticks() + 120;   /* let the main menu settle first */
	}
	return plat_ticks() >= g_ap_next;
}

/* Consume the due scripted key into scan/ascii; advance the script. */
static int ap_take(unsigned char *out_scan, unsigned char *out_ascii)
{
	if (!ap_due())
		return 0;
	{ extern void dbg_log_num(const char *, long); dbg_log_num("autoplay: send key idx=", g_ap_idx); }
	if (out_scan)  *out_scan  = g_ap[g_ap_idx].scan;
	if (out_ascii) *out_ascii = g_ap[g_ap_idx].ascii;
	g_ap_next = plat_ticks() + g_ap[g_ap_idx].delay;
	g_ap_idx++;
	return 1;
}
#endif /* FRUA_AUTOPLAY */

int plat_kb_poll(unsigned char *out_scan, unsigned char *out_ascii)
{
	long c;

#ifdef FRUA_AUTOPLAY
	if (ap_take(out_scan, out_ascii))
		return 1;
#endif

	/* Bconstat(2) (BIOS console) returns 0 under Hatari's `--conout 2`
	 * redirect even when a key is buffered — the console device is
	 * routed to host stderr, not the keyboard buffer. Cconis (GEMDOS
	 * func 0x0B) sees the key, and Crawcin (func 0x07) reads it raw
	 * (scan in high word, ASCII in low byte). The Bconstat probe is
	 * kept as a first-line fast-path: on a real Falcon without --conout
	 * 2 it serves keys without going through GEMDOS. */
	if (Bconstat(2) == 0 && Cconis() == 0)
		return 0;
	c = (Bconstat(2) != 0) ? Bconin(2) : Crawcin();
	if (out_scan)
		*out_scan = (unsigned char)((c >> 16) & 0xFF);
	if (out_ascii)
		*out_ascii = (unsigned char)(c & 0xFF);
	return 1;
}

/* Non-destructive "is a key pending?" — the status half of plat_kb_poll
 * (Bconstat/Cconis) WITHOUT consuming the key.  EventAvail uses it so the
 * Toolbox event pump (l731e -> l725c) knows a keyDown is available and runs
 * GetNextEvent, which then consumes it via plat_kb_poll.  Without this,
 * EventAvail can never report a pending key, and l731e (gated on
 * `l6804()==0 || EventAvail(...)`, with the port's l6804 returning 1) never
 * pumps for keyboard-only input — so jt1133's `while(jt1118()==0)` spins
 * forever in every standalone modal (jt891 amount entry, the roster picker). */
int plat_kb_avail(void)
{
#ifdef FRUA_AUTOPLAY
	if (ap_due())
		return 1;
#endif
	return (Bconstat(2) != 0 || Cconis() != 0);
}

unsigned char plat_kb_shift(void)
{
	return (unsigned char)Kbshift(-1);
}

unsigned char plat_kb_unshifted_char(unsigned char scan)
{
	_KEYTAB *kt = Keytbl((void *)-1, (void *)-1, (void *)-1);

	if (kt != NULL && kt->unshift != NULL)
		return ((const unsigned char *)kt->unshift)[scan];
	return 0;
}

/* --- IKBD mouse ---
 *
 * The handler runs in MFP IRQ context — no GEMDOS / BIOS calls allowed.
 * It only reads the packet, accumulates the deltas into the live
 * position with screen-edge clamping, and updates the button byte;
 * everything is plain memory writes. 16-bit position reads and writes
 * are single instructions on the 68000, so plat_mouse_pos's reader and
 * the handler's writer never tear individual coordinates — at worst a
 * reader sees the new x with the old y (or vice versa), one frame of
 * inconsistency that the engine never notices.
 */
static volatile short         g_mouse_x;
static volatile short         g_mouse_y;
static volatile unsigned char g_mouse_btn;
/* Down-edge latch: a full press+release can happen between two engine polls
 * (a fast click, or the modal loop busy after a screen change). The level in
 * g_mouse_btn would be back to 0 by the time the poll runs, so the click is
 * lost entirely. Latch the down-edge here in the interrupt and let the event
 * pump consume it, so no click is dropped. */
static volatile unsigned char g_mouse_click_pending;
static short g_mouse_max_x = 320;
static short g_mouse_max_y = 200;

static void (*g_old_mousevec)(unsigned char *packet);

/*
 * conterm ($484) bit 0 = the OS keyboard click (the BIOS pulses the YM2149 on
 * every keystroke). Handy while testing to confirm keys register, but a game
 * you play for hours should be silent — so we clear it at input init and put
 * the user's original value back on shutdown. Bit 1 (key repeat) and bit 2
 * (Ctrl-G bell) are left untouched. conterm lives in protected memory, so the
 * read/modify/write runs in supervisor (via Supexec, like the mousevec swap).
 */
#define CONTERM       (*(volatile unsigned char *)0x484UL)
#define CONTERM_CLICK 0x01
static unsigned char g_old_conterm;
static int           g_conterm_saved;

/*
 * IKBD packet: byte 0 = 0xF8 | (left ? 2 : 0) | (right ? 1 : 0);
 * byte 1 = signed delta-X; byte 2 = signed delta-Y (positive = down,
 * the IKBD default). Only the left button is mapped — the Mac doesn't
 * have a right button and the engine doesn't reference one.
 */
void ikbd_mouse_handler(unsigned char *packet)
{
	short nx = (short)(g_mouse_x + (signed char)packet[1]);
	short ny = (short)(g_mouse_y + (signed char)packet[2]);

	if (nx < 0) nx = 0;
	if (ny < 0) ny = 0;
	if (nx >= g_mouse_max_x) nx = (short)(g_mouse_max_x - 1);
	if (ny >= g_mouse_max_y) ny = (short)(g_mouse_max_y - 1);
	{
		unsigned char newbtn = (unsigned char)((packet[0] & 0x02) != 0);
		if (newbtn && !g_mouse_btn)      /* left-button DOWN edge */
			g_mouse_click_pending = 1;   /* latch until the pump takes it */
		g_mouse_x   = nx;
		g_mouse_y   = ny;
		g_mouse_btn = newbtn;
	}
}

/*
 * IKBD calls the mousevec with the packet pointer in A0 — not where the
 * C calling convention expects a first argument. This trampoline moves
 * A0 onto the stack, calls the C handler, and returns. Inline asm so
 * the assembler sees the symbol the IKBD will jump to.
 */
__asm__ (
	".globl _ikbd_mouse_trampoline\n"
	"_ikbd_mouse_trampoline:\n"
	"  movel %a0,%sp@-\n"
	"  jbsr  _ikbd_mouse_handler\n"
	"  addql #4,%sp\n"
	"  rts\n"
);
extern void ikbd_mouse_trampoline(unsigned char *packet);

static long install_supervisor(void)
{
	kbdvecs_t *v = (kbdvecs_t *)Kbdvbase();

	g_old_mousevec = v->mousevec;
	v->mousevec    = ikbd_mouse_trampoline;

	g_old_conterm  = CONTERM;               /* silence the keyboard click */
	g_conterm_saved = 1;
#ifdef FRUA_KEYCLICK
	/* Debug builds (-DFRUA_KEYCLICK): FORCE the click ON instead. Every
	 * BIOS-accepted keystroke then pulses the YM2149, so a sound capture of a
	 * scripted key sequence is ground truth for which keys the emulated
	 * machine actually received and when — the harness's key-drop/lag oracle
	 * (task #56). */
	CONTERM        = (unsigned char)(g_old_conterm | CONTERM_CLICK);
#else
	CONTERM        = (unsigned char)(g_old_conterm & ~CONTERM_CLICK);
#endif
	return 0;
}

static long uninstall_supervisor(void)
{
	kbdvecs_t *v = (kbdvecs_t *)Kbdvbase();

	if (g_old_mousevec != NULL) {
		v->mousevec    = g_old_mousevec;
		g_old_mousevec = NULL;
	}
	if (g_conterm_saved) {                  /* restore the user's click setting */
		CONTERM         = g_old_conterm;
		g_conterm_saved = 0;
	}
	return 0;
}

void plat_input_init(short screen_w, short screen_h)
{
	if (screen_w <= 0) screen_w = 320;
	if (screen_h <= 0) screen_h = 200;
	g_mouse_max_x = screen_w;
	g_mouse_max_y = screen_h;
	g_mouse_x     = (short)(screen_w / 2);
	g_mouse_y     = (short)(screen_h / 2);
	g_mouse_btn   = 0;
	Supexec(install_supervisor);
}

void plat_input_shutdown(void)
{
	if (g_old_mousevec != NULL)
		Supexec(uninstall_supervisor);
}

void plat_mouse_pos(short *h, short *v)
{
	if (h) *h = g_mouse_x;
	if (v) *v = g_mouse_y;
}

int plat_mouse_btn(void)
{
	return g_mouse_btn ? 1 : 0;
}

/* Peek the latched click without consuming (for non-destructive EventAvail). */
int plat_mouse_click_pending(void)
{
	return g_mouse_click_pending ? 1 : 0;
}

/* Consume one latched click: returns 1 (and clears the latch) if a down-edge
 * was seen since the last take. Interrupt-set, so read-then-clear; a rare race
 * with a fresh edge just carries that click to the next poll. */
int plat_mouse_take_click(void)
{
	if (g_mouse_click_pending) {
		g_mouse_click_pending = 0;
		return 1;
	}
	return 0;
}
