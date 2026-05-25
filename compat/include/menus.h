/*
 * Mac Menu Manager shim (ADR-0003, ADR-0006). Skeleton.
 *
 * Per ADR-0006 the editor and play-UI widgets are reimplemented inside the
 * shim, drawn into the HAL surface; this is the menus piece. A menu bar is
 * an array of MenuHandles painted across the top of the screen port; a
 * mouseDown that FindWindow reports as inMenuBar drops into MenuSelect,
 * which tracks the mouse over the bar and any pull-down until Button()
 * releases and returns the (menuID, item) pair packed as a long.
 *
 * Here so far: NewMenu / GetMenu (from a MENU resource) / DisposeMenu,
 * AppendMenu (one or more ';'-separated Pascal-string items),
 * CountMItems, GetMenuItemText, InsertMenu / DeleteMenu / ClearMenuBar,
 * GetMenuHandle, DrawMenuBar, HiliteMenu, MenuSelect with a save-and-restore
 * pull-down (the bits under the dropdown are stashed in a Ptr and blitted
 * back on dismiss, so any windows the menu briefly covered come back
 * untouched). EnableItem / DisableItem flip bits in enableFlags. The
 * MenuInfo header keeps the Mac field offsets for menuID / menuWidth /
 * menuHeight / menuProc / enableFlags so by-offset engine access ports
 * unchanged; the item list lives in a private slab off the end of the
 * record rather than in the Mac variable-length menuData blob — engine
 * code that walked menuData would need a fixup, but FRUA reaches items
 * through GetMenuItemText, not by walking the raw bytes. Key-equivalents,
 * styled item text, item icons, MDEF dispatch, and MenuKey () follow.
 */

#ifndef COMPAT_MENUS_H
#define COMPAT_MENUS_H

#include "quickdraw.h"          /* Boolean, ConstStr255Param, Point, Rect */
#include "macmemory.h"          /* Handle */

/*
 * MenuInfo — the Mac 14-byte header. The variable-length menuData blob
 * that follows in the real Mac record is replaced here by a private item
 * slab pointed to from a hidden field after enableFlags (set by the shim,
 * not visible to engine by-offset accesses to the first 14 bytes). The
 * trailing-comment numbers are Mac field offsets.
 */
typedef struct MenuInfo {
	short  menuID;                  /* 0  the menu's ID                  */
	short  menuWidth;               /* 2  pixel width when pulled down   */
	short  menuHeight;              /* 4  pixel height when pulled down  */
	Handle menuProc;                /* 6  MDEF (unused — shim renders)   */
	long   enableFlags;             /* 10 bit 0 = whole menu; N = item N */
	/* Shim-private fields below the Mac record. Engine by-offset access
	 * to menuData would land here; FRUA reaches items through
	 * GetMenuItemText, so the substitution is transparent for the lifts
	 * we've done so far. */
	struct  MenuItem *items;        /* item slab (NULL until appended)  */
	short   item_count;             /* number of items in the slab      */
	short   item_cap;               /* slab capacity                    */
	unsigned char title[256];       /* the menu's title (Pascal string) */
} MenuInfo;

typedef MenuInfo **MenuHandle;
typedef MenuHandle MenuRef;

/*
 * The menu bar's painted height in screen pixels. FindWindow uses this to
 * decide whether a click belongs to the bar; main / engine code can read
 * it to avoid drawing into the menu strip.
 */
short menubar_height(void);

/*
 * 1 if the menu bar currently has any menus inserted, 0 otherwise. The
 * Window Manager consults this to decide whether the top strip is live.
 */
int menubar_active(void);

/* --- the menus themselves --- */

/* Create an empty menu with the given title (Pascal string). */
MenuHandle NewMenu(short menuID, ConstStr255Param title);

/* Load MENU resource `resourceID` from the Resource Manager. */
MenuHandle GetMenu(short resourceID);

/* Free menu + its item slab. Caller must also DeleteMenu first. */
void       DisposeMenu(MenuHandle menu);

/*
 * Append one or more items to `menu`. `data` is a Pascal string of items
 * separated by ';'. Mac meta-characters within an item ('/' key-equiv,
 * '!' mark, '<' style, '(' disable, '^' icon) are recognised by the real
 * Menu Manager; this skeleton stores only the text — the trailing meta-
 * characters are stripped from each item.
 */
void AppendMenu(MenuHandle menu, ConstStr255Param data);

/* Number of items in `menu`. */
short CountMItems(MenuHandle menu);

/*
 * Copy item `item` (1-based) from `menu` into `str` as a Pascal string.
 * Out-of-range writes an empty string.
 */
void GetMenuItemText(MenuHandle menu, short item, unsigned char *str);

/* Enable / disable item `item` (1-based; 0 = the whole menu). */
void EnableItem(MenuHandle menu, short item);
void DisableItem(MenuHandle menu, short item);

/* --- the menu bar --- */

/*
 * Insert `menu` into the bar before the menu with id `beforeID`. `beforeID`
 * <= 0 means append to the end. Caller still owns the MenuHandle; the bar
 * holds a reference. Bar capacity is small (16 menus) — over-insert is a
 * silent no-op.
 */
void InsertMenu(MenuHandle menu, short beforeID);

/* Remove the menu with id `menuID` from the bar. */
void DeleteMenu(short menuID);

/* Empty the menu bar (does not dispose the MenuHandles themselves). */
void ClearMenuBar(void);

/*
 * Find a MenuHandle in the bar by id. NULL if no such menu is in the bar.
 * Mac classic name `GetMHandle` aliases this.
 */
MenuHandle GetMenuHandle(short menuID);
#define    GetMHandle GetMenuHandle

/* Paint the menu bar into the screen port. Call after Insert/DeleteMenu. */
void DrawMenuBar(void);

/*
 * Hilite the title of menu `menuID` (invert it), or 0 to clear the
 * hilite. MenuSelect calls this around its tracking loop; callers should
 * also call HiliteMenu(0) after acting on a MenuSelect return.
 */
void HiliteMenu(short menuID);

/*
 * Track a menu pull-down starting from `startPt` (typically the mouseDown
 * point). Drops the menu whose title is under startPt, then loops on
 * Button() / GetMouse() — the item under the mouse is highlighted, items
 * change as the mouse moves. On Button() release: returns
 *   (menuID << 16) | item
 * if the release was over an enabled item, 0 otherwise. The pull-down's
 * pixels are saved on entry and restored on exit, so the screen comes back
 * exactly as it went in.
 */
long MenuSelect(Point startPt);

#endif /* COMPAT_MENUS_H */
