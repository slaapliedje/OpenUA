/*
 * Input HAL — keyboard, mouse, and the tick counter.
 *
 * The Event Manager shim sits on top of this. Each platform target
 * provides the four polls; the Falcon implementation talks to BIOS
 * Bconstat/Bconin, Kbshift, and reads _hz_200 via Supexec.
 */

#ifndef PLATFORM_INPUT_H
#define PLATFORM_INPUT_H

/*
 * Mac-style 60 Hz ticks since boot. The Atari runs a 200 Hz counter at
 * _hz_200 (0x4BA); this scales to 60 Hz so the Event Manager's TickCount
 * matches the Mac semantics the engine was built against.
 */
unsigned long plat_ticks(void);

/*
 * Non-blocking keyboard poll. When a key is pending, fills *out_scan
 * and *out_ascii from the BIOS console-2 word (high byte = scancode,
 * low byte = ASCII) and returns 1. Returns 0 when nothing is queued.
 */
int plat_kb_poll(unsigned char *out_scan, unsigned char *out_ascii);

/*
 * Non-destructive "is a key pending?" — the BIOS/GEMDOS status check
 * (Bconstat/Cconis) WITHOUT consuming. Lets EventAvail report a pending
 * keyDown so the Toolbox event pump knows to run GetNextEvent (which then
 * consumes the key via plat_kb_poll). Returns non-zero when a key waits.
 */
int plat_kb_avail(void);

/*
 * Current keyboard modifier state — the raw Atari Kbshift bitmap
 * (bit 0 LSHIFT, 1 RSHIFT, 2 CTRL, 3 ALT, 4 CAPSLOCK). The Event
 * Manager translates to Mac modifier flags.
 */
unsigned char plat_kb_shift(void);

/*
 * Current mouse position, in screen coordinates. Stubbed (0,0) until
 * the IKBD-packet mouse driver lands.
 */
void plat_mouse_pos(short *h, short *v);

/* 1 if the mouse button is currently pressed. */
int plat_mouse_btn(void);

/*
 * Install the IKBD-packet mouse handler — wires our trampoline into the
 * keyboard table's mousevec via Supexec, with the mouse initialised to
 * the centre of a (screen_w, screen_h) surface. Subsequent IKBD packets
 * drive plat_mouse_pos / plat_mouse_btn. Call once at startup; pair with
 * plat_input_shutdown so the OS-side vector goes back to its original
 * owner on exit.
 */
void plat_input_init(short screen_w, short screen_h);
void plat_input_shutdown(void);

#endif /* PLATFORM_INPUT_H */
