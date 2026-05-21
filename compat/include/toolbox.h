/*
 * Mac Toolbox shim — manager startup (ADR-0003).
 *
 * The seven Toolbox manager init traps a Mac application calls at startup,
 * and toolbox_init(), which runs them in the standard order. FRUA's CODE 4
 * routine JT[1144] runs exactly this sequence as its prologue.
 *
 * First cut: the managers behind these entry points (Window, Menu, Dialog,
 * TextEdit, and the Event queue) are not built yet — docs/toolbox-mapping.md
 * tracks them — so the calls here establish the API surface and the startup
 * order, and gain real behaviour as each manager comes online.
 *
 * Distinct from macglue_init() (macglue.h): that brings up the shim's own
 * infrastructure; these are the Mac-facing API the engine itself calls.
 */

#ifndef COMPAT_TOOLBOX_H
#define COMPAT_TOOLBOX_H

#define everyEvent  ((short)0xFFFF)      /* FlushEvents: every event class */

void InitGraf(void *qdGlobals);         /* QuickDraw      */
void InitFonts(void);                   /* Font Manager   */
void InitWindows(void);                 /* Window Manager */
void InitMenus(void);                   /* Menu Manager   */
void TEInit(void);                      /* TextEdit       */
void InitDialogs(void *restartProc);    /* Dialog Manager */
void FlushEvents(short whichMask, short stopMask);  /* Event Manager */

/* Run the seven inits above in the standard Mac startup order. */
void toolbox_init(void);

#endif /* COMPAT_TOOLBOX_H */
