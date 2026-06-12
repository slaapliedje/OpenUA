/*
 * Mac Toolbox shim — manager startup (ADR-0003).
 *
 * The seven Toolbox manager init traps a Mac application calls at startup,
 * and toolbox_init(), which runs them in the standard order. FRUA's CODE 4
 * routine JT[1144] runs exactly this sequence as its prologue.
 *
 * The init entries here are the per-manager bring-up traps the engine calls
 * at startup; each manager's actual API (FlushEvents, NewWindow, ...) lives
 * in its own header (events.h, windows.h, ...). docs/toolbox-mapping.md
 * tracks per-manager status.
 *
 * Distinct from macglue_init() (macglue.h): that brings up the shim's own
 * infrastructure; these are the Mac-facing API the engine itself calls.
 */

#ifndef COMPAT_TOOLBOX_H
#define COMPAT_TOOLBOX_H

void InitGraf(void *qdGlobals);         /* QuickDraw      */
void InitFonts(void);                   /* Font Manager   */
void InitWindows(void);                 /* Window Manager */
void InitMenus(void);                   /* Menu Manager   */
void TEInit(void);                      /* TextEdit       */
void InitDialogs(void *restartProc);    /* Dialog Manager */

/* Run the seven inits above in the standard Mac startup order. */
void toolbox_init(void);

/* Process Manager: terminate the application immediately. Tears the
 * platform back down (sound vector, input vector, display mode) before
 * exiting so TOS gets its screen back — the Mac trap's "return to
 * Finder" contract. */
void ExitToShell(void) __attribute__((noreturn));

#endif /* COMPAT_TOOLBOX_H */
