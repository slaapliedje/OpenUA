/*
 * Mac Event Manager shim — see events.h.
 *
 * The queue is a small FIFO behind PostEvent (which the engine and the
 * shim use to inject synthesised events). GetNextEvent's priority order
 * mirrors the Mac: queued events first, then mouse-button edges (a
 * change vs. the last seen state synthesises mouseDown / mouseUp), then
 * the platform keyboard, then the Window Manager's update regions, then
 * nullEvent. EventAvail peeks non-destructively — for keyboard that
 * means we don't pull a key (BIOS Bconin would consume it) and for
 * mouse we don't acknowledge the edge (so the next GetNextEvent sees
 * and consumes it), so EventAvail only sees those sources via the queue.
 *
 * The queue overflow policy is drop-oldest: a runaway producer mustn't
 * stall the engine. Mac's PostEvent returns evtNotEnb / queueFull; for
 * now we always return noErr to keep the engine's checks simple, and
 * accept the overwrite when the queue is full.
 */

#include <stddef.h>             /* NULL */

#include "events.h"
#include "windows.h"            /* WindowPeek, FrontWindow, updateRgn */
#include "input.h"              /* plat_ticks, plat_kb_poll, ... */
#include "quickdraw.h"          /* qd_cursor_refresh (idle cursor tracking) */

#define EVENT_QUEUE_CAP 16

static EventRecord  g_queue[EVENT_QUEUE_CAP];
static unsigned char g_q_head, g_q_count;

static short xlate_modifiers(unsigned char kbshift)
{
	short m = 0;

	if (kbshift & 0x03) m |= shiftKey;     /* LSHIFT | RSHIFT */
	if (kbshift & 0x04) m |= controlKey;
	if (kbshift & 0x08) m |= cmdKey;       /* ALT → Cmd       */
	if (kbshift & 0x10) m |= alphaLock;
	if (plat_mouse_btn())
		m |= btnState;
	return m;
}

static Point current_mouse(void)
{
	Point p;
	short h, v;

	plat_mouse_pos(&h, &v);
	SetPt(&p, h, v);
	return p;
}

static void fill_common(EventRecord *e)
{
	e->when      = TickCount();
	e->where     = current_mouse();
	e->modifiers = xlate_modifiers(plat_kb_shift());
}

/* --- the posted-event queue --- */

static void queue_push(short what, long msg)
{
	EventRecord *e;
	unsigned char tail;

	if (g_q_count == EVENT_QUEUE_CAP) {
		/* drop the oldest to make room — engine moves on rather than
		 * stalling. */
		g_q_head = (unsigned char)((g_q_head + 1) % EVENT_QUEUE_CAP);
		g_q_count--;
	}
	tail = (unsigned char)((g_q_head + g_q_count) % EVENT_QUEUE_CAP);
	e = &g_queue[tail];
	e->what    = what;
	e->message = msg;
	fill_common(e);
	g_q_count++;
}

/*
 * Remove the i-th queue entry (0 = oldest) and compact the slots after
 * it. Linear, but capped at EVENT_QUEUE_CAP and called only on hit.
 */
static void queue_remove(unsigned char i)
{
	while (i + 1 < g_q_count) {
		unsigned char a = (unsigned char)((g_q_head + i) % EVENT_QUEUE_CAP);
		unsigned char b = (unsigned char)((g_q_head + i + 1) % EVENT_QUEUE_CAP);
		g_queue[a] = g_queue[b];
		i++;
	}
	g_q_count--;
}

static Boolean event_matches(short mask, short what)
{
	if (mask == everyEvent)
		return 1;
	return (Boolean)((mask >> what) & 1);
}

static Boolean queue_peek(short mask, EventRecord *out)
{
	unsigned char i;

	for (i = 0; i < g_q_count; i++) {
		unsigned char idx = (unsigned char)((g_q_head + i) % EVENT_QUEUE_CAP);

		if (event_matches(mask, g_queue[idx].what)) {
			if (out)
				*out = g_queue[idx];
			return 1;
		}
	}
	return 0;
}

static Boolean queue_pop(short mask, EventRecord *out)
{
	unsigned char i;

	for (i = 0; i < g_q_count; i++) {
		unsigned char idx = (unsigned char)((g_q_head + i) % EVENT_QUEUE_CAP);

		if (event_matches(mask, g_queue[idx].what)) {
			if (out)
				*out = g_queue[idx];
			queue_remove(i);
			return 1;
		}
	}
	return 0;
}

/* --- source pumps --- */

static Boolean kb_to_event(EventRecord *out)
{
	unsigned char scan, ascii;

	if (!plat_kb_poll(&scan, &ascii))
		return 0;
	/* The arrow keys carry no ASCII on the Atari (scancode only), but the
	 * Mac delivers them as char codes 28..31 in the event message's low
	 * byte — which is what lifted dialogs (the JT[169] list picker, etc.)
	 * test. Translate so Mac key handling works; the scancode stays in the
	 * high byte for the movement path that reads it. */
	if (ascii == 0) {
		switch (scan) {
		case 0x4b: ascii = 28; break;   /* left  */
		case 0x4d: ascii = 29; break;   /* right */
		case 0x48: ascii = 30; break;   /* up    */
		case 0x50: ascii = 31; break;   /* down  */
		default: break;
		}
	}
	out->what    = keyDown;
	out->message = ((long)scan << 8) | (long)ascii;
	fill_common(out);
	return 1;
}

/*
 * Last-seen mouse-button state. The Event Manager compares plat_mouse_btn()
 * against this on every pump; a transition synthesises mouseDown / mouseUp
 * and updates the value (the edge is acknowledged). EventAvail does the
 * same comparison but does *not* update — peeks are non-destructive.
 */
static unsigned char g_last_mouse_btn;

static Boolean mouse_edge_to_event(EventRecord *out, Boolean ack)
{
	int cur = plat_mouse_btn();

	if ((cur != 0) == (g_last_mouse_btn != 0))
		return 0;
	out->what    = cur ? mouseDown : mouseUp;
	out->message = 0;
	fill_common(out);
	if (ack)
		g_last_mouse_btn = (unsigned char)(cur ? 1 : 0);
	return 1;
}

static Boolean update_to_event(EventRecord *out)
{
	WindowPeek w;

	for (w = (WindowPeek)FrontWindow(); w != NULL; w = w->nextWindow) {
		if (w->visible && w->updateRgn != NULL
		 && !EmptyRgn(w->updateRgn)) {
			out->what    = updateEvt;
			out->message = (long)(WindowPtr)w;
			fill_common(out);
			return 1;
		}
	}
	return 0;
}

static void make_null(EventRecord *out)
{
	out->what    = nullEvent;
	out->message = 0;
	fill_common(out);
}

/* --- public API --- */

long TickCount(void)
{
	return (long)plat_ticks();
}

Boolean Button(void)
{
	return (Boolean)(plat_mouse_btn() != 0);
}

void GetMouse(Point *mouseLoc)
{
	if (mouseLoc != NULL)
		*mouseLoc = current_mouse();
}

Boolean GetNextEvent(short eventMask, EventRecord *theEvent)
{
	EventRecord tmp;

	if (theEvent == NULL)
		return 0;
	if (queue_pop(eventMask, theEvent))
		return 1;
	if (mouse_edge_to_event(&tmp, 1)) {
		/* Edge acknowledged regardless of mask: dropping an unmasked
		 * edge silently would mean the engine misses the matching
		 * up/down pair if it only watches one of them. Re-post into
		 * the queue when the mask doesn't accept it now. */
		if (event_matches(eventMask, tmp.what)) {
			*theEvent = tmp;
			return 1;
		}
		queue_push(tmp.what, tmp.message);
	}
	if (event_matches(eventMask, keyDown) && kb_to_event(theEvent))
		return 1;
	if (event_matches(eventMask, updateEvt) && update_to_event(theEvent))
		return 1;
	/* Idle: no real event this pump. Keep the cursor following the live
	 * mouse so it doesn't freeze between engine repaints (no-op unless the
	 * mouse moved since the last pump). */
	qd_cursor_refresh();
	make_null(theEvent);
	return 0;
}

Boolean EventAvail(short eventMask, EventRecord *theEvent)
{
	if (theEvent == NULL)
		return 0;
	if (queue_peek(eventMask, theEvent))
		return 1;
	/* Non-destructive peeks only. The mouse-edge check passes ack=0 so the
	 * next GetNextEvent still sees the same transition. */
	if ((event_matches(eventMask, mouseDown)
	  || event_matches(eventMask, mouseUp))
	 && mouse_edge_to_event(theEvent, 0))
		return 1;
	/* Keyboard: plat_kb_avail() (Bconstat/Cconis) reports a pending key
	 * WITHOUT consuming it (Bconin would). Report it as a keyDown so the
	 * Toolbox event pump (l731e -> l725c) runs GetNextEvent, which then
	 * consumes the key via kb_to_event. message is left 0 here — this is a
	 * peek; GetNextEvent fills the real scan/ASCII. Without this, standalone
	 * modals that block on jt1133's `while(jt1118()==0)` (jt891 amount entry,
	 * the roster/recipient pickers) never pump for keyboard-only input and
	 * hang. */
	if (event_matches(eventMask, keyDown) && plat_kb_avail()) {
		theEvent->what    = keyDown;
		theEvent->message = 0;
		fill_common(theEvent);
		return 1;
	}
	if (event_matches(eventMask, updateEvt) && update_to_event(theEvent))
		return 1;
	make_null(theEvent);
	return 0;
}

OSErr PostEvent(short eventNum, long eventMsg)
{
	queue_push(eventNum, eventMsg);
	return 0;
}

void event_post_full(short eventNum, long eventMsg, short modifiers)
{
	EventRecord *e;
	unsigned char tail;

	if (g_q_count == EVENT_QUEUE_CAP) {
		g_q_head = (unsigned char)((g_q_head + 1) % EVENT_QUEUE_CAP);
		g_q_count--;
	}
	tail = (unsigned char)((g_q_head + g_q_count) % EVENT_QUEUE_CAP);
	e = &g_queue[tail];
	e->what      = eventNum;
	e->message   = eventMsg;
	e->when      = TickCount();
	e->where     = current_mouse();
	e->modifiers = modifiers;
	g_q_count++;
}

/*
 * FlushEvents — drop every queued event matching `whichMask`. The Mac's
 * `stopMask` halts the discard at the first matching event, keeping it
 * and everything after; the engine's call sites don't exercise that
 * variant, so the parameter is accepted and ignored for now.
 */
void FlushEvents(short whichMask, short stopMask)
{
	unsigned char i = 0;

	(void)stopMask;
	if (whichMask == 0)
		return;
	while (i < g_q_count) {
		unsigned char idx = (unsigned char)((g_q_head + i) % EVENT_QUEUE_CAP);

		if (event_matches(whichMask, g_queue[idx].what))
			queue_remove(i);
		else
			i++;
	}
}

/*
 * WaitNextEvent — like GetNextEvent, but spins for up to `sleep` ticks
 * waiting for an event. mouseRgn is ignored: the Mac uses it to wake on
 * mouse-region transitions, which is meaningless until the mouse driver
 * lands. With no system-yield primitive yet this is a tight loop on
 * GetNextEvent + TickCount — fine on a 16 MHz 030 for short waits.
 */
Boolean WaitNextEvent(short eventMask, EventRecord *theEvent,
                      unsigned long sleep, RgnHandle mouseRgn)
{
	long deadline;

	(void)mouseRgn;
	if (theEvent == NULL)
		return 0;
	deadline = TickCount() + (long)sleep;
	for (;;) {
		if (GetNextEvent(eventMask, theEvent))
			return 1;
		if (TickCount() >= deadline)
			return 0;
	}
}
