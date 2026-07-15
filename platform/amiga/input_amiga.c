/*
 * Amiga input backend (ADR-0012) — keyboard, mouse, tick counter.
 *
 * Implements the input HAL (input.h). Because the AGA display backend owns the
 * machine (direct chipset, OS view unloaded), input is read at the low level:
 * an exec INTB_VERTB interrupt server counts frames (the tick source) and
 * integrates the mouse from JOY0DAT quadrature deltas + the CIA-A fire-button
 * bit. The Event Manager shim sits on top of these polls exactly as on Atari.
 *
 * The 60 Hz Mac tick: ExecBase->VBlankFrequency says 50 (PAL) or 60 (NTSC);
 * plat_ticks scales the frame count so TickCount keeps Mac time either way.
 *
 * Keyboard: raw Amiga keys arrive over CIA-A's serial port. The proper wiring
 * is an ICR vector on ciaa.resource's SP interrupt; still TODO(hw) — the polls
 * report an empty queue until then (the menu is mouse-driven; keys follow).
 *
 * Status: VBL server + mouse integration complete, UNVERIFIED on amiberry.
 */

#include "input.h"

#ifdef FRUA_AMIGA

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/interrupts.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <hardware/intbits.h>
#include <proto/exec.h>

#define CUSTOM ((volatile struct Custom *)0xDFF000)
#define CIAA   ((volatile struct CIA *)0xBFE001)

extern struct ExecBase *SysBase;

/* Incremented by the VBL server (50 Hz PAL / 60 Hz NTSC). Kept volatile —
 * written from interrupt, read from task level. */
volatile unsigned long g_amiga_vbl_ticks;

unsigned long plat_ticks(void)
{
	unsigned long vbl = g_amiga_vbl_ticks;
	UBYTE hz = SysBase ? SysBase->VBlankFrequency : 60;

	/* Mac ticks are 60 Hz; scale a 50 Hz PAL frame count up. */
	if (hz == 50)
		return vbl * 6 / 5;
	return vbl;
}

/* --- keyboard ------------------------------------------------------------
 * TODO(hw): ciaa.resource ICR vector on the SP (serial) interrupt, pushing
 * (rawkey, ascii) pairs into a ring these polls drain. Empty until then. */

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
	             * same bitmap layout as Atari Kbshift (bit0 LSHIFT, 1 RSHIFT,
	             * 2 CTRL, 3 ALT, 4 CAPS). */
}

unsigned char plat_kb_unshifted_char(unsigned char scan)
{
	(void)scan;
	return 0;   /* TODO(hw): rawkey -> unshifted char via a keymap; 0 just
	             * disables the Event Manager's Alt-key letter recovery. */
}

/* --- mouse ---------------------------------------------------------------
 * Integrated in the VBL server: JOY0DAT holds two free-running 8-bit
 * quadrature counters (X low byte, Y high byte); the per-frame DELTA is the
 * signed 8-bit difference from the previous sample. The left button is
 * CIA-A PRA bit 6, active low. All state is written at interrupt time and
 * read at task level — single writer, aligned shorts, no locking (the same
 * discipline as the Atari IKBD handler). */

static volatile short s_mouse_h, s_mouse_v;
static volatile short s_mouse_btn;
static volatile short s_click_latch;
static short          s_max_h = 320, s_max_v = 200;
static UWORD          s_last_joy;
static short          s_last_btn;

static struct Interrupt s_vbl_int;
static int              s_vbl_installed;

/* INTB_VERTB server: all VERTB servers on the chain run every frame; the
 * C ABI already preserves what a server must (d2-d7/a2-a6). Return 0. */
static LONG vbl_server(void)
{
	UWORD joy = CUSTOM->joy0dat;
	BYTE  dx  = (BYTE)((joy & 0xFF) - (s_last_joy & 0xFF));
	BYTE  dy  = (BYTE)((joy >> 8) - (s_last_joy >> 8));
	short btn = !(CIAA->ciapra & CIAF_GAMEPORT0);   /* active low */
	short h, v;

	s_last_joy = joy;

	h = (short)(s_mouse_h + dx);
	v = (short)(s_mouse_v + dy);
	if (h < 0) h = 0;
	if (v < 0) v = 0;
	if (h >= s_max_h) h = (short)(s_max_h - 1);
	if (v >= s_max_v) v = (short)(s_max_v - 1);
	s_mouse_h = h;
	s_mouse_v = v;

	if (btn && !s_last_btn)
		s_click_latch = 1;              /* down edge: latch until taken */
	s_last_btn  = btn;
	s_mouse_btn = btn;

	g_amiga_vbl_ticks++;
	return 0;
}

void plat_mouse_pos(short *h, short *v)
{
	if (h) *h = s_mouse_h;
	if (v) *v = s_mouse_v;
}

int plat_mouse_btn(void)
{
	return s_mouse_btn ? 1 : 0;
}

int plat_mouse_click_pending(void)
{
	return s_click_latch ? 1 : 0;
}

int plat_mouse_take_click(void)
{
	int c = s_click_latch ? 1 : 0;
	s_click_latch = 0;
	return c;
}

void plat_input_init(short screen_w, short screen_h)
{
	if (screen_w <= 0) screen_w = 320;
	if (screen_h <= 0) screen_h = 200;
	s_max_h     = screen_w;
	s_max_v     = screen_h;
	s_mouse_h   = (short)(screen_w / 2);
	s_mouse_v   = (short)(screen_h / 2);
	s_mouse_btn = 0;
	s_click_latch = 0;
	s_last_joy  = CUSTOM->joy0dat;
	s_last_btn  = 0;
	g_amiga_vbl_ticks = 0;

	if (!s_vbl_installed) {
		s_vbl_int.is_Node.ln_Type = NT_INTERRUPT;
		s_vbl_int.is_Node.ln_Pri  = 0;
		s_vbl_int.is_Node.ln_Name = (char *)"OpenUA VBL";
		s_vbl_int.is_Data         = NULL;
		s_vbl_int.is_Code         = (VOID (*)())vbl_server;
		AddIntServer(INTB_VERTB, &s_vbl_int);
		s_vbl_installed = 1;
	}
	/* TODO(hw): the ciaa.resource keyboard ICR vector installs here too. */
}

void plat_input_shutdown(void)
{
	if (s_vbl_installed) {
		RemIntServer(INTB_VERTB, &s_vbl_int);
		s_vbl_installed = 0;
	}
}

#endif /* FRUA_AMIGA */
