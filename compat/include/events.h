/*
 * Mac Event Manager shim (ADR-0003).
 *
 * The engine drives its main loop with GetNextEvent / WaitNextEvent;
 * this stands those calls up over the platform input HAL. Events come
 * from three sources: a small posted FIFO (PostEvent), the platform
 * keyboard poll (Bconstat/Bconin on the Atari), and the Window Manager's
 * update regions (a non-empty updateRgn synthesises an updateEvt for
 * that window). Mouse events are deferred to the IKBD-packet driver.
 *
 * Here so far: EventRecord, the event / mask / modifier constants,
 * TickCount, Button (stubbed), GetMouse (stubbed), GetNextEvent,
 * EventAvail, PostEvent, FlushEvents, WaitNextEvent. activateEvt and
 * the mouseDown / mouseUp / autoKey paths follow when the mouse and
 * focus tracking arrive.
 */

#ifndef COMPAT_EVENTS_H
#define COMPAT_EVENTS_H

#include "quickdraw.h"          /* Point, Boolean, RgnHandle */
#include "macmemory.h"          /* OSErr                     */

typedef struct EventRecord {
	short what;
	long  message;
	long  when;
	Point where;
	short modifiers;
} EventRecord;

/* event types — what */
#define nullEvent     0
#define mouseDown     1
#define mouseUp       2
#define keyDown       3
#define keyUp         4
#define autoKey       5
#define updateEvt     6
#define diskEvt       7
#define activateEvt   8
#define osEvt         15

/* event masks — selected via (1 << what) */
#define mDownMask     0x0002
#define mUpMask       0x0004
#define keyDownMask   0x0008
#define keyUpMask     0x0010
#define autoKeyMask   0x0020
#define updateMask    0x0040
#define diskMask      0x0080
#define activMask     0x0100
#define osMask        0x8000
#define everyEvent    (short)0xFFFF

/* modifier-flag bits — modifiers */
#define activeFlag    0x0001
#define btnState      0x0080
#define cmdKey        0x0100
#define shiftKey      0x0200
#define alphaLock     0x0400
#define optionKey     0x0800
#define controlKey    0x1000

long    TickCount(void);
Boolean Button(void);
void    GetMouse(Point *mouseLoc);

Boolean GetNextEvent(short eventMask, EventRecord *theEvent);
Boolean EventAvail(short eventMask, EventRecord *theEvent);
OSErr   PostEvent(short eventNum, long eventMsg);
void    FlushEvents(short whichMask, short stopMask);
Boolean WaitNextEvent(short eventMask, EventRecord *theEvent,
                      unsigned long sleep, RgnHandle mouseRgn);

#endif /* COMPAT_EVENTS_H */
