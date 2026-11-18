/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <string.h>
#include <Dialogs.h>
#include <Memory.h>
#include <Quickdraw.h>
#include "platform.h"

/* "Choose one of N" as a real modal dialog: the title as static text,
   one push button per option, and Cancel under them when there is a
   choice to cancel. A one-option call is a message with an OK button,
   which is how the application's own notices reach the writer.

   Built from an item list (DITL) assembled in memory and handed to
   NewDialog -- the backend has no resource file (docs/mac-platform.md's
   toolchain section), and the Dialog Manager takes items from any
   handle in the DITL layout, the same as one read from a resource.

   This used to be a popup menu (PopUpMenuSelect) with the title thrown
   away. A popup menu tracks the mouse only while the button is down, and
   the call nearly always comes from a menu pick or a key, after the
   button is up -- so it closed at once and answered "cancelled", and
   every message the application tried to show (a failed save, "Printed
   2 pages", "needs a gateway") vanished. Found on a MiSTer Mac Plus core
   with RetroWP's File > Print.

   Return is item 1, the first option, as ModalDialog has it by default.
   SetDialogDefaultItem would honour default_index, but it is System 7's
   and the backend runs on System 6, so the first button is the one
   Return presses. Labels are passed through as bytes: the application's
   are ASCII. */

#define WP_DIALOG_MARGIN   13
#define WP_DIALOG_BTN_H    20
#define WP_DIALOG_BTN_GAP  8
#define WP_DIALOG_LINE_H   16
#define WP_DIALOG_CHAR_W   7    /* Chicago 12, about */

/* One DITL item at h[at]: a placeholder, the rect, the type, the data
   (padded to even). Returns the offset after it. */
static long ditl_item(unsigned char *h, long at, short top, short left, short bottom,
                      short right, unsigned char type, const u8 *data, u32 len)
{
    short r[4];

    if (len > 255) len = 255;
    memset(h + at, 0, 4);
    at += 4;
    r[0] = top; r[1] = left; r[2] = bottom; r[3] = right;
    memcpy(h + at, r, 8);   /* the 68k's own byte order is the DITL's */
    at += 8;
    h[at++] = type;
    h[at++] = (unsigned char)len;
    memcpy(h + at, data, len);
    at += (long)len;
    if (len & 1u) h[at++] = 0;
    return at;
}

wp_status plat_dialog_choice(plat_window *parent,
                              const u8 *title_utf8, u32 title_len,
                              const u8 * const *option_labels_utf8,
                              const u32 *option_label_lens,
                              u32 option_count, u32 default_index,
                              wp_bool *out_cancelled, u32 *out_index)
{
    static const u8 k_cancel[] = "Cancel";
    Rect screen = qd.screenBits.bounds;
    Rect box;
    Handle items;
    DialogPtr dlg;
    GrafPtr saved;
    wp_bool with_cancel = (wp_bool)(option_count > 1);
    u32 buttons, i, widest = 0, text_lines;
    short w, h, text_h, btn_w, y;
    long size, at;
    INTEGER hit = 0;

    WP_UNUSED(parent);
    WP_UNUSED(default_index);
    if (option_count == 0) return WP_RANGE;
    if (title_len > 255) title_len = 255;

    /* As many options as the screen has room for (a Mac Plus's 342
       lines take about ten). */
    buttons = option_count + (with_cancel ? 1u : 0u);
    while (buttons > 1 &&
           (long)buttons * (WP_DIALOG_BTN_H + WP_DIALOG_BTN_GAP) + 120 >
           (long)(screen.bottom - screen.top)) {
        buttons--;
    }
    if (with_cancel && buttons < 2) return WP_RANGE;
    option_count = with_cancel ? buttons - 1u : buttons;

    for (i = 0; i < option_count; i++) {
        if (option_label_lens[i] > widest) widest = option_label_lens[i];
    }
    w = 300;
    if ((short)(widest * WP_DIALOG_CHAR_W + 2 * WP_DIALOG_MARGIN + 20) > w) {
        w = (short)(widest * WP_DIALOG_CHAR_W + 2 * WP_DIALOG_MARGIN + 20);
    }
    if (w > screen.right - screen.left - 20) w = (short)(screen.right - screen.left - 20);
    text_lines = (title_len * WP_DIALOG_CHAR_W) / (u32)(w - 2 * WP_DIALOG_MARGIN) + 1u;
    text_h = (short)(text_lines * WP_DIALOG_LINE_H);
    btn_w = (option_count == 1 && !with_cancel) ? 70 : (short)(w - 2 * WP_DIALOG_MARGIN);
    h = (short)(WP_DIALOG_MARGIN + text_h + WP_DIALOG_BTN_GAP +
                (short)buttons * (WP_DIALOG_BTN_H + WP_DIALOG_BTN_GAP) + WP_DIALOG_MARGIN - WP_DIALOG_BTN_GAP);

    /* The item list: 2 + (4 + 8 + 2 + 256) bytes an item at most. */
    size = 2 + (long)(buttons + 1u) * 270;
    items = NewHandle(size);
    if (items == NULL) return WP_NOMEM;
    HLock(items);
    {
        unsigned char *p = (unsigned char *)*items;
        short count_minus_1 = (short)buttons;   /* buttons + the text, less one */

        memcpy(p, &count_minus_1, 2);
        at = 2;
        y = (short)(WP_DIALOG_MARGIN + text_h + WP_DIALOG_BTN_GAP);
        for (i = 0; i < buttons; i++) {
            const u8 *label = (with_cancel && i == option_count) ? k_cancel : option_labels_utf8[i];
            u32 len = (with_cancel && i == option_count) ? 6u : option_label_lens[i];
            short left = (short)(w - WP_DIALOG_MARGIN - btn_w);

            at = ditl_item(p, at, y, left, (short)(y + WP_DIALOG_BTN_H), (short)(left + btn_w),
                           (unsigned char)(ctrlItem + btnCtrl), label, len);
            y = (short)(y + WP_DIALOG_BTN_H + WP_DIALOG_BTN_GAP);
        }
        at = ditl_item(p, at, WP_DIALOG_MARGIN, WP_DIALOG_MARGIN,
                       (short)(WP_DIALOG_MARGIN + text_h), (short)(w - WP_DIALOG_MARGIN),
                       (unsigned char)(statText + itemDisable), title_utf8, title_len);
    }
    HUnlock(items);
    SetHandleSize(items, at);

    /* Centred across, a third of the way down, as alerts sit. */
    box.left = (short)((screen.left + screen.right - w) / 2);
    box.right = (short)(box.left + w);
    box.top = (short)(screen.top + (screen.bottom - screen.top - h) / 3);
    if (box.top < screen.top + 40) box.top = (short)(screen.top + 40);
    box.bottom = (short)(box.top + h);

    GetPort(&saved);
    dlg = NewDialog(NULL, &box, (ConstStringPtr) "\p", true, dBoxProc, (WindowPtr)-1L,
                    false, 0, items);
    if (dlg == NULL) {
        DisposeHandle(items);
        return WP_NOMEM;
    }
    SetPort(dlg);
    do {
        ModalDialog(NULL, &hit);
    } while (hit < 1 || hit > (INTEGER)buttons);
    DisposeDialog(dlg);   /* the item list with it */
    SetPort(saved);

    if (with_cancel && (u32)hit == buttons) {
        if (out_cancelled) *out_cancelled = WP_TRUE;
        return WP_OK;
    }
    if (out_cancelled) *out_cancelled = WP_FALSE;
    if (out_index) *out_index = (u32)hit - 1u;
    return WP_OK;
}
