/*
 * Mac Dialog Manager shim (ADR-0003, ADR-0006).
 *
 * Reimplemented in the shim, alongside the Window Manager — per ADR-0006
 * the editor and play-UI dialogs are drawn into the HAL surface, not
 * mapped to AES. Builds on the Window Manager (the dialog is a window)
 * and the DITL reader that was already in place for Alert.
 *
 * Here so far:
 *   - Alert(alertID, filterProc): load the ALRT, parse its DITL, open a
 *     modal-ish window, paint button frames + static text, spin a small
 *     event loop until any keypress or mouse click dismisses, return 1.
 *   - GetNewDialog(dialogID, ...): load a DLOG resource and its referenced
 *     DITL, open a colour window in the DLOG bounds, attach the items, and
 *     return a DialogRecord. NewDialog is the programmatic equivalent.
 *   - DrawDialog: paint the DITL items (button frames + static text) into
 *     the screen port using the dialog's global bounds.
 *   - ModalDialog(filterProc, &itemHit): WaitNextEvent loop; sets
 *     *itemHit to the index of an enabled button the user clicks (and
 *     returns), or to aDefItem on Return / Enter when one is set.
 *   - DialogSelect(event, &dialog, &itemHit): single-event variant that
 *     ModalDialog calls per iteration.
 *   - DisposeDialog, GetDialogItem, SetDialogItemText / GetDialogItemText.
 *
 * DLOG resource layout (Mac Toolbox Essentials):
 *   +0   8 bytes Rect bounds (global screen coords)
 *   +8   short  procID         (window definition; 1 = dBoxProc modal)
 *   +10  byte   visible
 *   +11  byte   pad
 *   +12  byte   goAwayFlag
 *   +13  byte   pad
 *   +14  long   refCon
 *   +18  short  itemsID        (DITL resource id)
 *   +20  Str255 title
 *
 * Edit-text fields, user items, icons, pictures, the default-item
 * thick border, and event-filter dispatch follow as the engine reaches
 * for them.
 */

#ifndef COMPAT_DIALOGS_H
#define COMPAT_DIALOGS_H

#include "events.h"             /* EventRecord */
#include "macmemory.h"          /* Handle      */
#include "quickdraw.h"          /* Rect, ConstStr255Param, Boolean */
#include "windows.h"            /* WindowRecord, WindowPtr */

/*
 * DialogRecord — the Mac dialog data structure. The window comes first,
 * so a DialogPtr (points at the window/port) interconverts with a
 * WindowPtr by a cast. Trailing-comment numbers are Mac field offsets.
 */
typedef struct DialogRecord {
	WindowRecord window;            /* 0   the dialog's window         */
	Handle       items;             /* 156 the DITL Handle             */
	Handle       textH;             /* 160 current edit field (TE)     */
	short        editField;         /* 164 current edit-text item, -1  */
	short        editOpen;          /* 166 internal — text edit open   */
	short        aDefItem;          /* 168 default item (Return / Enter) */
} DialogRecord;

typedef DialogRecord *DialogPtr;
typedef DialogPtr     DialogPeek;

/* DITL item-type constants (low 7 bits of the type byte). */
#define ctrlItem      4                  /* OR'd with btnCtrl etc.  */
#define btnCtrl       0                  /*   pushbutton           */
#define chkCtrl       1                  /*   checkbox (planned)   */
#define radCtrl       2                  /*   radio (planned)      */
#define resCtrl       3                  /*   user-defined (planned) */
#define statText      8                  /* static text             */
#define editText      16                 /* editable text (planned) */
#define iconItem      32                 /* icon (planned)          */
#define picItem       64                 /* picture (planned)       */
#define userItem      0                  /* user item (planned)     */
#define itemDisable   128                /* OR'd with type if disabled */

/* --- Alert (already in place) --- */
short Alert(short alertID, void *filterProc);

/* --- dialog lifecycle --- */
DialogPtr NewDialog(void *dStorage, const Rect *bounds,
                    ConstStr255Param title, Boolean visible, short procID,
                    WindowPtr behind, Boolean goAwayFlag, long refCon,
                    Handle items);
DialogPtr GetNewDialog(short dialogID, void *dStorage, WindowPtr behind);
void      DisposeDialog(DialogPtr d);

/* --- painting --- */
void      DrawDialog(DialogPtr d);

/* --- modal event loop --- */
void      ModalDialog(void *filterProc, short *itemHit);
Boolean   DialogSelect(const EventRecord *event, DialogPtr *theDialog,
                       short *itemHit);

/* --- item access ---
 *
 * Returns the item type, its handle (typically NULL for our skeleton —
 * the Mac fills this in for controls), and the item's local rect (in
 * the dialog's window coordinates). itemNum is 1-based.
 */
void GetDialogItem(DialogPtr d, short itemNum, short *type, Handle *itemH,
                   Rect *box);

/*
 * Pascal-string text accessors for static-text and edit-text items. The
 * skeleton stores the item text in the DITL's bytes themselves, so
 * SetDialogItemText / GetDialogItemText operate on those bytes via the
 * item handle returned by GetDialogItem. For static-text items the
 * "handle" is a tagged pointer that points into the DITL — writeable
 * for short strings; longer text triggers a reallocation that follows
 * when an engine call needs it.
 */
void SetDialogItemText(Handle item, ConstStr255Param text);
void GetDialogItemText(Handle item, unsigned char *text);

#endif /* COMPAT_DIALOGS_H */
