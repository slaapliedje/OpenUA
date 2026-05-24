/*
 * Mac Toolbox shim — manager startup (ADR-0003). See toolbox.h.
 *
 * A Mac application opens by calling each Toolbox manager's init trap; FRUA
 * does this in its CODE 4 routine JT[1144]. This file provides those seven
 * entry points and toolbox_init(), the sequence that runs them — the lift
 * of JT[1144]'s startup prologue. Per-manager APIs live in their own
 * headers (events.h, windows.h, ...); the init entries here are the
 * lightweight bring-up hooks.
 */

#include <stddef.h>           /* NULL */

#include "toolbox.h"
#include "events.h"           /* FlushEvents + everyEvent for the prologue */

/*
 * InitGraf — QuickDraw startup. The Mac caller passes a pointer to its
 * QuickDraw globals block; the shim keeps QuickDraw's state internally, so
 * the pointer is unused. QuickDraw's drawing state is set up with the
 * display HAL (see quickdraw.h).
 */
void InitGraf(void *qdGlobals)
{
	(void)qdGlobals;
}

/* InitFonts — Font Manager startup. */
void InitFonts(void)
{
}

/* InitWindows — Window Manager startup. */
void InitWindows(void)
{
}

/* InitMenus — Menu Manager startup. */
void InitMenus(void)
{
}

/* TEInit — TextEdit startup. */
void TEInit(void)
{
}

/*
 * InitDialogs — Dialog Manager startup. The Mac caller may pass a
 * resume/restart procedure for system-error recovery; unused here.
 */
void InitDialogs(void *restartProc)
{
	(void)restartProc;
}

/*
 * toolbox_init — the standard Mac Toolbox startup sequence.
 *
 * FRUA's JT[1144] runs exactly this as its prologue, passing its own
 * QuickDraw globals block to InitGraf; the shim ignores that pointer.
 */
void toolbox_init(void)
{
	InitGraf(NULL);              /* QuickDraw   */
	InitFonts();                 /* Font Mgr    */
	InitWindows();               /* Window Mgr  */
	InitMenus();                 /* Menu Mgr    */
	TEInit();                    /* TextEdit    */
	InitDialogs(NULL);           /* Dialog Mgr  */
	FlushEvents(everyEvent, 0);  /* Event Mgr   */
}
