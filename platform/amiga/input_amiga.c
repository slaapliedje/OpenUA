/*
 * Amiga input backend (ADR-0012) — keyboard, mouse, tick counter.
 *
 * Implements the input HAL (input.h). Because the AGA display backend owns the
 * machine (direct chipset, OS view unloaded), input is read at the low level:
 * the keyboard from CIA-A's serial handshake, the mouse from JOY0DAT deltas +
 * the CIA-A fire-button bit, and the 60 Hz tick from a VBL-incremented counter.
 * The Event Manager shim sits on top of these six polls exactly as on Atari.
 *
 * ★ SCAFFOLD STATUS: the tick model and the poll structure are real; the raw
 * CIA keyboard decode and the JOY0DAT quadrature marked TODO(hw) need writing
 * and validating on amiberry once the toolchain lands. Polls return "nothing
 * pending" until then. See platform/input.c for the Atari analog.
 */

#include "input.h"

#ifdef FRUA_AMIGA

#include <exec/types.h>

/* Incremented by the display backend's VBL handler (50 Hz PAL / 60 Hz NTSC).
 * The Mac semantics want 60 Hz ticks; on PAL we scale 50->60 so TickCount keeps
 * Mac time. Kept volatile — written from interrupt, read from task level. */
volatile unsigned long g_amiga_vbl_ticks;

unsigned long plat_ticks(void)
{
	/* TODO(hw): if PAL (50 Hz), scale to 60: return vbl * 6 / 5. Detect the
	 * mode from the display backend once it reports it; assume 60 for now so
	 * the arithmetic is a no-op on NTSC. */
	return g_amiga_vbl_ticks;
}

/* --- keyboard ------------------------------------------------------------
 * Raw Amiga keys arrive as CIA-A serial bytes (rawkey code, bit-reversed, with
 * bit 7 = up/down). A small ISR would push (scan, ascii) into a ring; these
 * polls drain it. Until that ISR is wired, report an empty queue. */

int plat_kb_poll(unsigned char *out_scan, unsigned char *out_ascii)
{
	(void)out_scan; (void)out_ascii;
	return 0;   /* TODO(hw): pop the rawkey ring, map to (scancode, ASCII) */
}

int plat_kb_avail(void)
{
	return 0;   /* TODO(hw): ring non-empty? */
}

unsigned char plat_kb_shift(void)
{
	return 0;   /* TODO(hw): track shift/ctrl/alt/caps from the rawkey stream,
	             * return the SAME bitmap layout the Atari Kbshift uses
	             * (bit0 LSHIFT,1 RSHIFT,2 CTRL,3 ALT,4 CAPS) so the Event
	             * Manager translation is machine-independent. */
}

unsigned char plat_kb_unshifted_char(unsigned char scan)
{
	(void)scan;
	return 0;   /* TODO(hw): map rawkey -> unshifted char via the Amiga keymap
	             * (MapRawKey / a default keymap). 0 = no printable char, which
	             * just disables the Event Manager's Alt-key letter recovery. */
}

/* --- mouse ---------------------------------------------------------------
 * Position is integrated from JOY0DAT's X/Y quadrature counters each VBL; the
 * button is CIA-A PRA bit 6 (fire, active low). */

static short s_mouse_h, s_mouse_v;
static int   s_mouse_btn;
static int   s_click_latch;

void plat_mouse_pos(short *h, short *v)
{
	if (h) *h = s_mouse_h;
	if (v) *v = s_mouse_v;
}

int plat_mouse_btn(void)
{
	return s_mouse_btn;   /* TODO(hw): CIAAPRA & CIAF_GAMEPORT0 (inverted) */
}

int plat_mouse_click_pending(void)
{
	return s_click_latch;
}

int plat_mouse_take_click(void)
{
	int c = s_click_latch;
	s_click_latch = 0;
	return c;
}

void plat_input_init(short screen_w, short screen_h)
{
	s_mouse_h = screen_w / 2;
	s_mouse_v = screen_h / 2;
	s_mouse_btn = 0;
	s_click_latch = 0;
	g_amiga_vbl_ticks = 0;
	/* TODO(hw): install the CIA keyboard ISR and a VBL server that integrates
	 * JOY0DAT into (s_mouse_h, s_mouse_v) clamped to the surface, samples the
	 * fire button, sets s_click_latch on a press edge, and bumps
	 * g_amiga_vbl_ticks. Pair with plat_input_shutdown to remove them. */
}

void plat_input_shutdown(void)
{
	/* TODO(hw): remove the keyboard ISR and VBL server installed above. */
}

#endif /* FRUA_AMIGA */
