/*
 * Mac Dialog Manager shim (ADR-0003).
 *
 * First cut: Alert + the ALRT / DITL resource readers. The Mac
 * NoteAlert / CautionAlert / StopAlert / Alert family all funnel
 * through Alert with a different icon hint; we land Alert first and
 * grow the variants when the engine reaches for them. Full
 * GetNewDialog / ModalDialog / DialogSelect machinery follows.
 *
 * Here so far:
 *   - Alert(alertID, filterProc): load the ALRT, parse its DITL,
 *     open a modal-ish window, paint button frames + static text,
 *     spin a small event loop until any keypress or mouse click
 *     dismisses, return the item that was hit (1 for keyboard).
 */

#ifndef COMPAT_DIALOGS_H
#define COMPAT_DIALOGS_H

#include "quickdraw.h"

/*
 * Display alert `alertID`. filterProc (Mac event filter) is accepted
 * for API parity and ignored. Returns the dialog item that dismissed
 * the alert (1 for keyboard / first button hit). Returns -1 when the
 * ALRT or its DITL can't be found.
 */
short Alert(short alertID, void *filterProc);

#endif /* COMPAT_DIALOGS_H */
