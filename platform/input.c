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

unsigned long plat_ticks(void)
{
	/* 200 Hz → 60 Hz: *60/200 = *3/10. The unsigned-long arithmetic
	 * gives us ~248 days of run-time before the 60 Hz counter wraps,
	 * comfortably outside any plausible session. */
	unsigned long h200 = (unsigned long)Supexec(read_hz200);

	return (h200 * 3UL) / 10UL;
}

int plat_kb_poll(unsigned char *out_scan, unsigned char *out_ascii)
{
	long c;

	if (Bconstat(2) == 0)
		return 0;
	c = Bconin(2);
	if (out_scan)
		*out_scan = (unsigned char)((c >> 16) & 0xFF);
	if (out_ascii)
		*out_ascii = (unsigned char)(c & 0xFF);
	return 1;
}

unsigned char plat_kb_shift(void)
{
	return (unsigned char)Kbshift(-1);
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
static short g_mouse_max_x = 320;
static short g_mouse_max_y = 200;

static void (*g_old_mousevec)(unsigned char *packet);

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
	g_mouse_x   = nx;
	g_mouse_y   = ny;
	g_mouse_btn = (unsigned char)((packet[0] & 0x02) != 0);
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
	return 0;
}

static long uninstall_supervisor(void)
{
	kbdvecs_t *v = (kbdvecs_t *)Kbdvbase();

	if (g_old_mousevec != NULL) {
		v->mousevec    = g_old_mousevec;
		g_old_mousevec = NULL;
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
