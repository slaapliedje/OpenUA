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
 * Keyboard: an input.device HANDLER (pri 100) — see the keyboard section for
 * why not the ciaa.resource ICR vector. The handler consumes rawkey and
 * rawmouse events, so the invisible Workbench behind the game never sees the
 * player's input.
 *
 * Status: VBL server + mouse integration VERIFIED on amiberry; the server
 * also drives the display backend's sprite-0 pointer each frame.
 */

#include "input.h"
#include "plat_sound.h"        /* plat_sound_vbl — fed from the VERTB server */

#ifdef FRUA_AMIGA

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/interrupts.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <hardware/intbits.h>
#include <proto/exec.h>

#ifdef FRUA_KBTRACE
#include "dbglog.h"
static volatile long s_kbt_pushed;      /* handler-side counter (no DOS there) */
#endif

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
 *
 * NOT the ciaa.resource ICR route the roadmap first named: keyboard.device
 * owns the CIA-A SP interrupt bit on any booted system, and AddICRVector
 * refuses a claimed bit — and this port deliberately keeps the OS alive
 * (dos.library does all file I/O), so the bit is never free. The
 * system-friendly handle is an input.device HANDLER at priority 100: it
 * sees every IECLASS_RAWKEY event (with qualifiers, and with the OS's own
 * key-repeat synthesis) before Intuition does, and by consuming the events
 * it also stops keys AND mouse clicks from leaking into the invisible
 * Workbench behind the game — which they did until now.
 *
 * The handler (amiga_ihandler, entered through the register-ABI stub in
 * platform/c2p.S) maps each key-down to its Atari IKBD scancode and pushes
 * {scan, qualifier} into a ring; the polls below drain it and apply the TOS
 * keytable semantics the Event Manager shim expects — including Alt/Amiga+
 * letter delivering ASCII 0 so the shim's cmdKey recovery path fires, the
 * same as TOS's alternate keytable does on the Falcon. */

#define KB_RING 32

static volatile ULONG s_kb_ring[KB_RING];   /* (qual << 16) | (scan << 8)  */
static volatile UBYTE s_kb_head, s_kb_tail; /* head = writer, tail = reader */
static volatile UWORD s_kb_qual;            /* live IEQUALIFIER_* snapshot  */

static struct MsgPort  *s_in_port;
static struct IOStdReq *s_in_io;
static struct Interrupt s_in_handler;
static int              s_in_open;

extern void amiga_ihandler_entry(void);     /* platform/c2p.S              */

/* Amiga rawkey -> Atari IKBD scancode (US layout; 0 = no equivalent).
 * Modifier rawkeys 0x60-0x67 map to 0 — their state travels in the event
 * qualifiers instead. */
static const unsigned char k_raw2atari[0x68] = {
	/* 0x00 */ 0x29, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,  /* ` 1-7 */
	/* 0x08 */ 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x2B, 0x00, 0x70,  /* 890-=\ KP0 */
	/* 0x10 */ 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,  /* QWERTYUI */
	/* 0x18 */ 0x18, 0x19, 0x1A, 0x1B, 0x00, 0x6D, 0x6E, 0x6F,  /* OP[] KP123 */
	/* 0x20 */ 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,  /* ASDFGHJK */
	/* 0x28 */ 0x26, 0x27, 0x28, 0x00, 0x00, 0x6A, 0x6B, 0x6C,  /* L;' KP456 */
	/* 0x30 */ 0x00, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32,  /* ZXCVBNM */
	/* 0x38 */ 0x33, 0x34, 0x35, 0x00, 0x71, 0x67, 0x68, 0x69,  /* ,./ KP.789 */
	/* 0x40 */ 0x39, 0x0E, 0x0F, 0x72, 0x1C, 0x01, 0x53, 0x00,  /* spc BS tab KPent ret esc del */
	/* 0x48 */ 0x00, 0x00, 0x4A, 0x00, 0x48, 0x50, 0x4D, 0x4B,  /* KP- up dn rt lf */
	/* 0x50 */ 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42,  /* F1-F8 */
	/* 0x58 */ 0x43, 0x44, 0x63, 0x64, 0x65, 0x66, 0x4E, 0x62,  /* F9 F10 KP([/*+ help */
	/* 0x60 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* modifiers */
};

/* TOS US keytables by Atari scancode (what Keytbl() returns on the Falcon).
 * Designated initializers: unnamed slots (modifiers, F-keys, arrows) stay 0.
 * The shifted arrows really do yield digits — faithful to TOS. */
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

/* IEQUALIFIER_* -> the Atari Kbshift bitmap the shim translates
 * (bit0 RSHIFT, bit1 LSHIFT, bit2 CTRL, bit3 ALT, bit4 CAPS). Both Alt and
 * Amiga keys land on the ALT bit: the shim turns it into the Mac cmdKey. */
static unsigned char qual_to_kbshift(UWORD q)
{
	unsigned char k = 0;

	if (q & IEQUALIFIER_RSHIFT)   k |= 0x01;
	if (q & IEQUALIFIER_LSHIFT)   k |= 0x02;
	if (q & IEQUALIFIER_CONTROL)  k |= 0x04;
	if (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT
	         | IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))
		k |= 0x08;
	if (q & IEQUALIFIER_CAPSLOCK) k |= 0x10;
	return k;
}

/* The input.device handler. Runs in the input task; single writer of the
 * ring. Consumes rawkey AND rawmouse events (IECLASS_NULL) so nothing
 * reaches Intuition — the game owns input; the mouse itself is read from
 * JOY0DAT, which the consumption doesn't affect. */
struct InputEvent *amiga_ihandler(struct InputEvent *list, APTR data)
{
	struct InputEvent *ie;

	(void)data;
	for (ie = list; ie != NULL; ie = ie->ie_NextEvent) {
		if (ie->ie_Class == IECLASS_RAWKEY) {
			UWORD code = ie->ie_Code;

			s_kb_qual = ie->ie_Qualifier;
			if (!(code & IECODE_UP_PREFIX) && code < 0x68
			    && k_raw2atari[code] != 0) {
				UBYTE next = (UBYTE)((s_kb_head + 1) % KB_RING);

				if (next != s_kb_tail) {    /* full: drop newest */
					s_kb_ring[s_kb_head] =
					    ((ULONG)ie->ie_Qualifier << 16)
					    | ((ULONG)k_raw2atari[code] << 8);
					s_kb_head = next;
#ifdef FRUA_KBTRACE
					s_kbt_pushed++;
#endif
				}
			}
			ie->ie_Class = IECLASS_NULL;
		} else if (ie->ie_Class == IECLASS_RAWMOUSE) {
			ie->ie_Class = IECLASS_NULL;
		}
	}
	return list;
}

int plat_kb_poll(unsigned char *out_scan, unsigned char *out_ascii)
{
	ULONG entry;
	unsigned char scan, kbs, ascii;

#ifdef FRUA_KBTRACE
	{
		extern long g_kbt_l2d3e, g_kbt_1134, g_kbt_qdpresent,
		            g_kbt_qdsuppressed;
		static long polls;
		if ((++polls & 0x0F) == 0) {
			dbg_file_num("kbt: polls=", polls);
			dbg_file_num("kbt: ticks=", (long)plat_ticks());
			dbg_file_num("kbt: l2d3e=", g_kbt_l2d3e);
			dbg_file_num("kbt: 1134=", g_kbt_1134);
			dbg_file_num("kbt: qdpres=", g_kbt_qdpresent);
			dbg_file_num("kbt: qdsupp=", g_kbt_qdsuppressed);
		}
	}
#endif
	if (s_kb_tail == s_kb_head)
		return 0;
	entry = s_kb_ring[s_kb_tail];
	s_kb_tail = (UBYTE)((s_kb_tail + 1) % KB_RING);

	scan = (unsigned char)(entry >> 8);
	kbs  = qual_to_kbshift((UWORD)(entry >> 16));

	if (kbs & 0x08) {
		/* Alt/Amiga chords deliver NO character, exactly like TOS's
		 * alternate keytable: the Event Manager recovers the letter
		 * from plat_kb_unshifted_char and forces cmdKey. */
		ascii = 0;
	} else {
		ascii = (kbs & 0x03) ? k_tos_shift[scan & 0x7F]
		                     : k_tos_unshift[scan & 0x7F];
		if ((kbs & 0x10) && ascii >= 'a' && ascii <= 'z')
			ascii = (unsigned char)(ascii - 'a' + 'A');
		if ((kbs & 0x04) && (ascii & 0x40))
			ascii &= 0x1F;              /* Ctrl-letter, TOS-style */
	}

	if (out_scan)  *out_scan  = scan;
	if (out_ascii) *out_ascii = ascii;
#ifdef FRUA_KBTRACE
	dbg_file_num("kbt: pop scan=", scan);
	dbg_file_num("kbt: pushed total=", s_kbt_pushed);
	dbg_file_num("kbt: ticks=", (long)plat_ticks());
#endif
	return 1;
}

int plat_kb_avail(void)
{
	return s_kb_tail != s_kb_head;
}

unsigned char plat_kb_shift(void)
{
	return qual_to_kbshift(s_kb_qual);
}

unsigned char plat_kb_unshifted_char(unsigned char scan)
{
	return k_tos_unshift[scan & 0x7F];
}

/* Install/remove the handler (called from plat_input_init/shutdown). */

static void kb_install(void)
{
	if (s_in_open)
		return;
	s_in_port = CreateMsgPort();
	if (s_in_port == NULL)
		return;
	s_in_io = (struct IOStdReq *)CreateIORequest(s_in_port,
	                                             sizeof(struct IOStdReq));
	if (s_in_io == NULL) {
		DeleteMsgPort(s_in_port);
		s_in_port = NULL;
		return;
	}
	if (OpenDevice((CONST_STRPTR)"input.device", 0,
	               (struct IORequest *)s_in_io, 0) != 0) {
		DeleteIORequest((struct IORequest *)s_in_io);
		DeleteMsgPort(s_in_port);
		s_in_io = NULL;
		s_in_port = NULL;
		return;
	}
	s_in_handler.is_Node.ln_Type = NT_INTERRUPT;
	s_in_handler.is_Node.ln_Pri  = 100;    /* ahead of Intuition (50) */
	s_in_handler.is_Node.ln_Name = (char *)"OpenUA input";
	s_in_handler.is_Data         = NULL;
	s_in_handler.is_Code         = (VOID (*)())amiga_ihandler_entry;
	s_in_io->io_Command = IND_ADDHANDLER;
	s_in_io->io_Data    = &s_in_handler;
	DoIO((struct IORequest *)s_in_io);
	s_in_open = 1;
}

static void kb_remove(void)
{
	if (!s_in_open)
		return;
	s_in_io->io_Command = IND_REMHANDLER;
	s_in_io->io_Data    = &s_in_handler;
	DoIO((struct IORequest *)s_in_io);
	CloseDevice((struct IORequest *)s_in_io);
	DeleteIORequest((struct IORequest *)s_in_io);
	DeleteMsgPort(s_in_port);
	s_in_io   = NULL;
	s_in_port = NULL;
	s_in_open = 0;
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

/* Display backend's sprite-cursor tick (platform-internal, display_aga.c):
 * repositions the hardware pointer from the mouse state integrated above,
 * inside the same blank. No-ops until a cursor sprite has been pushed. */
extern void amiga_display_vbl_cursor(void);

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

	amiga_display_vbl_cursor();         /* move the hardware pointer */

	/* Feed the Paula ring + run the engine's sound sequencer (memory-only
	 * at interrupt time — sound_paula.c owns the discipline). */
	plat_sound_vbl();

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
	s_kb_head = s_kb_tail = 0;
	kb_install();
}

void plat_input_shutdown(void)
{
	kb_remove();
	if (s_vbl_installed) {
		RemIntServer(INTB_VERTB, &s_vbl_int);
		s_vbl_installed = 0;
	}
}

#endif /* FRUA_AMIGA */
