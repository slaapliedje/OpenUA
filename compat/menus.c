/*
 * Mac Menu Manager shim — skeleton (ADR-0003, ADR-0006). See menus.h.
 *
 * Menus are stored as MenuInfo records reached through a MenuHandle whose
 * master pointer never moves (the Memory Manager shim never relocates). The
 * menu bar is a small array of MenuHandle in left-to-right paint order;
 * insertion finds an existing entry of the same id and replaces it, so
 * re-inserting a menu just updates its slot. Items live in a private
 * MenuItem slab pointed to from a hidden field after the Mac-faithful
 * 14-byte header; the Mac menuData variable-length blob is not synthesised.
 *
 * Rendering goes through the QuickDraw shim on the screen port (the menu
 * bar paints on the desktop, outside any window). MenuSelect tracks with
 * the same Button() / GetMouse() poll DragWindow uses, so it goes live as
 * soon as the IKBD-packet mouse driver runs. The pull-down's pixels are
 * saved into a NewPtr block on entry and blitted back on exit, so any
 * windows the menu briefly covered come back untouched without needing the
 * Window Manager's update mechanism.
 */

#include <stddef.h>             /* NULL                                   */
#include <string.h>             /* memcpy, memset, memmove                */

#include "events.h"             /* Button, GetMouse                       */
#include "macmemory.h"          /* NewPtr, DisposePtr, NewHandleClear     */
#include "menus.h"
#include "quickdraw.h"          /* GrafPort, drawing primitives           */
#include "resources.h"          /* GetResource for GetMenu                */

/* Menu bar colour swatches — RGB so RGBForeColor resolves them to the
 * nearest palette index, no matter which CLUT is loaded. Index 255 in
 * FRUA's CLUT happens to be yellow, not white, so a hard-coded fgColor
 * would paint the bar yellow. */
static const RGBColor c_bar_bg  = { 0xFFFF, 0xFFFF, 0xFFFF };  /* white   */
static const RGBColor c_bar_fg  = { 0x0000, 0x0000, 0x0000 };  /* black   */
static const RGBColor c_bar_dim = { 0x8000, 0x8000, 0x8000 };  /* grey    */

/* --- layout constants ------------------------------------------------- */

#define MENUBAR_H        14     /* the bar's pixel height                 */
#define MENUBAR_TXT_Y    11     /* baseline of the title text             */
#define TITLE_PAD_H       6     /* horizontal padding either side         */
#define ITEM_H           12     /* row height inside a pull-down          */
#define ITEM_TXT_INSET    8     /* x-inset of item text inside the rect   */
#define ITEM_TXT_DROP     9     /* baseline drop inside an item row       */
#define MENUBAR_MAX      16     /* max menus in the bar                   */

#define CHAR_W            6     /* the 8x8 fallback font advance (approx) */

/* --- the items slab --------------------------------------------------- */

typedef struct MenuItem {
	unsigned char text[256];        /* Pascal string */
} MenuItem;

/* --- the menu bar ----------------------------------------------------- */

static MenuHandle g_menubar[MENUBAR_MAX];
static short      g_menubar_n;

/* The id of the currently-hilited menu, or 0 if none. */
static short      g_hilite_id;

/* --- helpers ---------------------------------------------------------- */

/* Pascal-string length (the first byte). */
static short pstr_len(const unsigned char *p)
{
	return (p == NULL) ? 0 : (short)p[0];
}

/* Copy a Pascal string into dst, clamping to 255 data bytes. */
static void pstr_copy(unsigned char *dst, const unsigned char *src)
{
	short n;

	if (dst == NULL)
		return;
	if (src == NULL) {
		dst[0] = 0;
		return;
	}
	n = (short)src[0];
	if (n > 255)
		n = 255;
	dst[0] = (unsigned char)n;
	if (n > 0)
		memcpy(dst + 1, src + 1, (size_t)n);
}

/* Approximate pixel width of the 8x8 fallback string. */
static short title_width(const unsigned char *p)
{
	return (short)(pstr_len(p) * CHAR_W);
}

/* Append a single item (raw text bytes) to the menu, growing the slab. */
static void menu_push_item(MenuInfo *info, const unsigned char *text, short len)
{
	MenuItem *new_items;
	short     new_cap;

	if (info->item_count >= info->item_cap) {
		new_cap = (short)(info->item_cap == 0 ? 4 : info->item_cap * 2);
		new_items = (MenuItem *)NewPtr((Size)new_cap * (Size)sizeof *new_items);
		if (new_items == NULL)
			return;
		if (info->items != NULL) {
			memcpy(new_items, info->items,
			       (size_t)info->item_count * sizeof *new_items);
			DisposePtr((Ptr)info->items);
		}
		info->items   = new_items;
		info->item_cap = new_cap;
	}
	if (len > 255)
		len = 255;
	info->items[info->item_count].text[0] = (unsigned char)len;
	if (len > 0)
		memcpy(info->items[info->item_count].text + 1, text, (size_t)len);
	info->item_count++;
	/* enable the new item — bit (n) where n = 1..31 maps to item n. */
	if (info->item_count >= 1 && info->item_count <= 31)
		info->enableFlags |= (long)(1L << info->item_count);
}

/* x position of the left edge of the menu at bar index `i`. */
static short bar_left_x(short i)
{
	short x = TITLE_PAD_H, k;

	for (k = 0; k < i; k++) {
		MenuInfo *info = (g_menubar[k] != NULL) ? *g_menubar[k] : NULL;

		if (info == NULL)
			continue;
		x = (short)(x + title_width(info->title) + 2 * TITLE_PAD_H);
	}
	return x;
}

/* Hit-test screen point against the bar; returns bar index or -1. */
static short bar_hit(Point pt)
{
	short i, left, right;

	if (pt.v < 0 || pt.v >= MENUBAR_H)
		return -1;
	left = TITLE_PAD_H;
	for (i = 0; i < g_menubar_n; i++) {
		MenuInfo *info = (g_menubar[i] != NULL) ? *g_menubar[i] : NULL;
		short     w;

		if (info == NULL)
			continue;
		w = title_width(info->title);
		right = (short)(left + w + TITLE_PAD_H);
		if (pt.h >= (short)(left - TITLE_PAD_H) && pt.h < right)
			return i;
		left = (short)(right + TITLE_PAD_H);
	}
	return -1;
}

/* Paint one title at bar slot `i`. `hilited` inverts foreground / background. */
static void draw_title(short i, int hilited)
{
	MenuInfo *info = (g_menubar[i] != NULL) ? *g_menubar[i] : NULL;
	Rect      r;
	short     x, w;

	if (info == NULL)
		return;
	x = bar_left_x(i);
	w = title_width(info->title);
	SetRect(&r, (short)(x - TITLE_PAD_H), 0,
	        (short)(x + w + TITLE_PAD_H), (short)(MENUBAR_H - 1));

	{
		GrafPtr  scr = qd_screen_port(), saved;
		CGrafPtr scp = (CGrafPtr)qd_screen_port();
		long     saved_fg, saved_bk;
		RGBColor saved_rgb_fg, saved_rgb_bk;

		if (scr == NULL)
			return;
		GetPort(&saved);
		SetPort(scr);
		saved_fg     = scr->fgColor;
		saved_bk     = scr->bkColor;
		saved_rgb_fg = scp->rgbFgColor;
		saved_rgb_bk = scp->rgbBkColor;

		RGBForeColor(hilited ? &c_bar_fg : &c_bar_bg);
		PaintRect(&r);
		RGBForeColor(hilited ? &c_bar_bg : &c_bar_fg);
		MoveTo((short)(x), (short)MENUBAR_TXT_Y);
		DrawString(info->title);

		scr->fgColor     = saved_fg;
		scr->bkColor     = saved_bk;
		scp->rgbFgColor  = saved_rgb_fg;
		scp->rgbBkColor  = saved_rgb_bk;
		SetPort(saved);
	}
}

/* Compute the pull-down rect for the menu at bar slot `i`. */
static void pulldown_rect(short i, Rect *out)
{
	MenuInfo *info = (g_menubar[i] != NULL) ? *g_menubar[i] : NULL;
	short     left, k, max_w = 0, h;

	if (info == NULL || out == NULL)
		return;
	left = bar_left_x(i);
	for (k = 0; k < info->item_count; k++) {
		short w = (short)(pstr_len(info->items[k].text) * CHAR_W);

		if (w > max_w)
			max_w = w;
	}
	h = (short)(info->item_count * ITEM_H);
	SetRect(out, (short)(left - TITLE_PAD_H), (short)MENUBAR_H,
	        (short)(left + max_w + 2 * ITEM_TXT_INSET), (short)(MENUBAR_H + h));
}

/* Paint the pull-down for menu at bar slot `i` with item `hi_item` (1-based,
 * 0 = none) inverted. */
static void draw_pulldown(short i, short hi_item)
{
	MenuInfo *info = (g_menubar[i] != NULL) ? *g_menubar[i] : NULL;
	Rect      box, frame;
	GrafPtr   scr = qd_screen_port(), saved;
	CGrafPtr  scp = (CGrafPtr)qd_screen_port();
	long      saved_fg, saved_bk;
	RGBColor  saved_rgb_fg, saved_rgb_bk;
	short     k;

	if (info == NULL || scr == NULL)
		return;
	pulldown_rect(i, &box);
	GetPort(&saved);
	SetPort(scr);
	saved_fg     = scr->fgColor;
	saved_bk     = scr->bkColor;
	saved_rgb_fg = scp->rgbFgColor;
	saved_rgb_bk = scp->rgbBkColor;

	RGBForeColor(&c_bar_bg);
	PaintRect(&box);
	RGBForeColor(&c_bar_fg);
	frame = box;
	FrameRect(&frame);

	for (k = 0; k < info->item_count; k++) {
		Rect row;
		int  enabled = (k + 1 < 32)
		             ? ((info->enableFlags & (1L << (k + 1))) != 0)
		             : 1;
		int  hilited = enabled && (hi_item == (short)(k + 1));

		SetRect(&row, (short)(box.left + 1),
		        (short)(box.top + k * ITEM_H),
		        (short)(box.right - 1),
		        (short)(box.top + (k + 1) * ITEM_H));
		if (hilited) {
			RGBForeColor(&c_bar_fg);
			PaintRect(&row);
			RGBForeColor(&c_bar_bg);
		} else {
			RGBForeColor(enabled ? &c_bar_fg : &c_bar_dim);
		}
		MoveTo((short)(box.left + ITEM_TXT_INSET),
		       (short)(box.top + k * ITEM_H + ITEM_TXT_DROP));
		DrawString(info->items[k].text);
		RGBForeColor(&c_bar_fg);
	}

	scr->fgColor    = saved_fg;
	scr->bkColor    = saved_bk;
	scp->rgbFgColor = saved_rgb_fg;
	scp->rgbBkColor = saved_rgb_bk;
	SetPort(saved);
}

/* Identify which item (1-based) of menu `i`'s pull-down `pt` falls over,
 * or 0 if outside. */
static short pulldown_hit(short i, Point pt)
{
	Rect box;
	short row;

	pulldown_rect(i, &box);
	if (pt.h < box.left || pt.h >= box.right
	 || pt.v < box.top  || pt.v >= box.bottom)
		return 0;
	row = (short)((pt.v - box.top) / ITEM_H);
	return (short)(row + 1);
}

/* --- save / restore the bits under the pull-down --------------------- */

typedef struct {
	Ptr   bits;
	long  size;
	short row;
	short rect_left;
	short rect_top;
	short rect_w;
	short rect_h;
} saved_bits;

/* Snapshot the rectangle from the screen port into `out`. */
static void save_under(const Rect *r, saved_bits *out)
{
	GrafPtr scr = qd_screen_port();
	short   y, w, h;
	Ptr     base, src, dst;

	out->bits = NULL;
	out->size = 0;
	if (scr == NULL || scr->portBits.baseAddr == NULL || r == NULL)
		return;
	w = (short)(r->right - r->left);
	h = (short)(r->bottom - r->top);
	if (w <= 0 || h <= 0)
		return;
	out->row       = scr->portBits.rowBytes;
	out->rect_left = r->left;
	out->rect_top  = r->top;
	out->rect_w    = w;
	out->rect_h    = h;
	out->size      = (long)w * (long)h;
	out->bits      = NewPtr(out->size);
	if (out->bits == NULL) {
		out->size = 0;
		return;
	}
	base = scr->portBits.baseAddr;
	for (y = 0; y < h; y++) {
		src = base + (long)(r->top + y) * out->row + r->left;
		dst = out->bits + (long)y * w;
		memcpy(dst, src, (size_t)w);
	}
}

/* Blit a previously saved rectangle back to the screen port. */
static void restore_under(const saved_bits *in)
{
	GrafPtr scr = qd_screen_port();
	short   y;
	Ptr     base, src, dst;

	if (in == NULL || in->bits == NULL || scr == NULL
	 || scr->portBits.baseAddr == NULL)
		return;
	base = scr->portBits.baseAddr;
	for (y = 0; y < in->rect_h; y++) {
		src = in->bits + (long)y * in->rect_w;
		dst = base + (long)(in->rect_top + y) * in->row + in->rect_left;
		memcpy(dst, src, (size_t)in->rect_w);
	}
}

/* ====================================================================== */
/*  public API                                                            */
/* ====================================================================== */

short menubar_height(void)
{
	return MENUBAR_H;
}

int menubar_active(void)
{
	return g_menubar_n > 0;
}

MenuHandle NewMenu(short menuID, ConstStr255Param title)
{
	Handle    h;
	MenuInfo *info;

	h = NewHandleClear((Size)sizeof *info);
	if (h == NULL)
		return NULL;
	info = (MenuInfo *)*h;
	info->menuID = menuID;
	info->enableFlags = ~0L;        /* enable menu + every potential item */
	pstr_copy(info->title, title);
	return (MenuHandle)h;
}

MenuHandle GetMenu(short resourceID)
{
	Handle               rsrc;
	const unsigned char *p;
	long                 sz, off;
	short                title_len;
	MenuHandle           m;

	rsrc = GetResource(0x4D454E55L /* 'MENU' */, resourceID);
	if (rsrc == NULL || *rsrc == NULL)
		return NULL;
	sz = GetHandleSize(rsrc);
	if (sz < 14)
		return NULL;
	p = (const unsigned char *)*rsrc;

	/* MENU resource: short menuID, short width, short height, short
	 * menuProc, short pad, long enableFlags, Pascal title, then for
	 * each item: Pascal text, byte icon, byte keyEq, byte mark, byte
	 * style; a 0 length byte terminates the items. */
	{
		short menuID = (short)(((unsigned)p[0] << 8) | p[1]);
		long  flags  = ((long)p[10] << 24) | ((long)p[11] << 16)
		             | ((long)p[12] << 8)  |  (long)p[13];
		unsigned char title[256];

		title_len = (short)p[14];
		if (15 + title_len > sz)
			return NULL;
		title[0] = (unsigned char)title_len;
		if (title_len > 0)
			memcpy(title + 1, p + 15, (size_t)title_len);
		m = NewMenu(menuID, title);
		if (m == NULL)
			return NULL;
		(*m)->enableFlags = flags;
	}

	off = 15 + title_len;
	while (off < sz) {
		short item_len = (short)p[off];

		if (item_len == 0)
			break;                  /* terminator */
		if (off + 1 + item_len + 4 > sz)
			break;
		menu_push_item(*m, p + off + 1, item_len);
		off += 1 + item_len + 4;        /* skip icon/keyEq/mark/style */
	}
	return m;
}

void DisposeMenu(MenuHandle menu)
{
	MenuInfo *info;

	if (menu == NULL || *menu == NULL)
		return;
	info = *menu;
	if (info->items != NULL) {
		DisposePtr((Ptr)info->items);
		info->items = NULL;
	}
	DisposeHandle((Handle)menu);
}

void AppendMenu(MenuHandle menu, ConstStr255Param data)
{
	MenuInfo            *info;
	const unsigned char *p;
	short                n, start, i;

	if (menu == NULL || *menu == NULL || data == NULL)
		return;
	info = *menu;
	n = (short)data[0];
	p = data + 1;

	start = 0;
	for (i = 0; i <= n; i++) {
		if (i == n || p[i] == ';') {
			short item_len = (short)(i - start);
			short stop;

			for (stop = 0; stop < item_len; stop++) {
				unsigned char c = p[start + stop];

				if (c == '/' || c == '!' || c == '<'
				 || c == '(' || c == '^')
					break;
			}
			menu_push_item(info, p + start, stop);
			start = (short)(i + 1);
		}
	}
}

short CountMItems(MenuHandle menu)
{
	if (menu == NULL || *menu == NULL)
		return 0;
	return (*menu)->item_count;
}

void GetMenuItemText(MenuHandle menu, short item, unsigned char *str)
{
	MenuInfo *info;

	if (str == NULL)
		return;
	str[0] = 0;
	if (menu == NULL || *menu == NULL)
		return;
	info = *menu;
	if (item < 1 || item > info->item_count)
		return;
	pstr_copy(str, info->items[item - 1].text);
}

void EnableItem(MenuHandle menu, short item)
{
	if (menu == NULL || *menu == NULL || item < 0 || item > 31)
		return;
	(*menu)->enableFlags |= (long)(1L << item);
}

void DisableItem(MenuHandle menu, short item)
{
	if (menu == NULL || *menu == NULL || item < 0 || item > 31)
		return;
	(*menu)->enableFlags &= ~(long)(1L << item);
}

void InsertMenu(MenuHandle menu, short beforeID)
{
	short i, ins;

	if (menu == NULL || *menu == NULL)
		return;
	/* Replace if already present. */
	for (i = 0; i < g_menubar_n; i++) {
		if (g_menubar[i] != NULL && (*g_menubar[i])->menuID
		    == (*menu)->menuID) {
			g_menubar[i] = menu;
			return;
		}
	}
	if (g_menubar_n >= MENUBAR_MAX)
		return;
	ins = g_menubar_n;
	if (beforeID > 0) {
		for (i = 0; i < g_menubar_n; i++) {
			if (g_menubar[i] != NULL && (*g_menubar[i])->menuID
			    == beforeID) {
				ins = i;
				break;
			}
		}
	}
	for (i = g_menubar_n; i > ins; i--)
		g_menubar[i] = g_menubar[i - 1];
	g_menubar[ins] = menu;
	g_menubar_n++;
}

void DeleteMenu(short menuID)
{
	short i, j;

	for (i = 0; i < g_menubar_n; i++) {
		if (g_menubar[i] == NULL)
			continue;
		if ((*g_menubar[i])->menuID != menuID)
			continue;
		for (j = i; j + 1 < g_menubar_n; j++)
			g_menubar[j] = g_menubar[j + 1];
		g_menubar[g_menubar_n - 1] = NULL;
		g_menubar_n--;
		return;
	}
}

void ClearMenuBar(void)
{
	short i;

	for (i = 0; i < MENUBAR_MAX; i++)
		g_menubar[i] = NULL;
	g_menubar_n = 0;
	g_hilite_id = 0;
}

MenuHandle GetMenuHandle(short menuID)
{
	short i;

	for (i = 0; i < g_menubar_n; i++) {
		if (g_menubar[i] != NULL && (*g_menubar[i])->menuID == menuID)
			return g_menubar[i];
	}
	return NULL;
}

void DrawMenuBar(void)
{
	GrafPtr  scr = qd_screen_port(), saved;
	CGrafPtr scp = (CGrafPtr)qd_screen_port();
	Rect     bar;
	long     saved_fg, saved_bk;
	RGBColor saved_rgb_fg, saved_rgb_bk;
	short    i;

	if (scr == NULL)
		return;
	GetPort(&saved);
	SetPort(scr);
	saved_fg     = scr->fgColor;
	saved_bk     = scr->bkColor;
	saved_rgb_fg = scp->rgbFgColor;
	saved_rgb_bk = scp->rgbBkColor;

	SetRect(&bar, scr->portRect.left, scr->portRect.top,
	        scr->portRect.right, (short)MENUBAR_H);
	RGBForeColor(&c_bar_bg);
	PaintRect(&bar);
	RGBForeColor(&c_bar_fg);
	MoveTo(scr->portRect.left, (short)(MENUBAR_H - 1));
	LineTo((short)(scr->portRect.right - 1), (short)(MENUBAR_H - 1));

	scr->fgColor    = saved_fg;
	scr->bkColor    = saved_bk;
	scp->rgbFgColor = saved_rgb_fg;
	scp->rgbBkColor = saved_rgb_bk;
	SetPort(saved);

	for (i = 0; i < g_menubar_n; i++)
		draw_title(i, 0);
	if (g_hilite_id != 0) {
		for (i = 0; i < g_menubar_n; i++) {
			if (g_menubar[i] != NULL
			 && (*g_menubar[i])->menuID == g_hilite_id) {
				draw_title(i, 1);
				break;
			}
		}
	}
}

void HiliteMenu(short menuID)
{
	short i, prev = g_hilite_id;

	if (prev == menuID)
		return;
	g_hilite_id = menuID;

	if (prev != 0) {
		for (i = 0; i < g_menubar_n; i++) {
			if (g_menubar[i] != NULL
			 && (*g_menubar[i])->menuID == prev) {
				draw_title(i, 0);
				break;
			}
		}
	}
	if (menuID != 0) {
		for (i = 0; i < g_menubar_n; i++) {
			if (g_menubar[i] != NULL
			 && (*g_menubar[i])->menuID == menuID) {
				draw_title(i, 1);
				break;
			}
		}
	}
}

long MenuSelect(Point startPt)
{
	short      slot, cur_slot, cur_item = 0, prev_item = 0;
	MenuInfo  *info;
	Rect       box;
	saved_bits under;
	Point      pt;
	long       result = 0;

	slot = bar_hit(startPt);
	if (slot < 0)
		return 0;
	info = (g_menubar[slot] != NULL) ? *g_menubar[slot] : NULL;
	if (info == NULL || info->item_count == 0)
		return 0;

	HiliteMenu(info->menuID);
	pulldown_rect(slot, &box);
	save_under(&box, &under);
	draw_pulldown(slot, 0);
	qd_present();

	cur_slot = slot;
	while (Button()) {
		short new_slot, new_item;

		GetMouse(&pt);
		new_slot = bar_hit(pt);
		if (new_slot >= 0 && new_slot != cur_slot) {
			MenuInfo *ni;

			restore_under(&under);
			ni = (g_menubar[new_slot] != NULL)
			   ? *g_menubar[new_slot] : NULL;
			if (ni == NULL || ni->item_count == 0) {
				qd_present();
				cur_slot = -1;
				prev_item = 0;
				continue;
			}
			HiliteMenu(ni->menuID);
			pulldown_rect(new_slot, &box);
			save_under(&box, &under);
			draw_pulldown(new_slot, 0);
			qd_present();
			cur_slot = new_slot;
			prev_item = 0;
			info = ni;
			continue;
		}
		if (cur_slot < 0) {
			qd_present();
			continue;
		}
		new_item = pulldown_hit(cur_slot, pt);
		if (new_item != prev_item) {
			draw_pulldown(cur_slot, new_item);
			qd_present();
			prev_item = new_item;
		}
		cur_item = new_item;
	}

	restore_under(&under);
	if (under.bits != NULL)
		DisposePtr(under.bits);
	qd_present();
	HiliteMenu(0);

	if (cur_slot >= 0 && cur_item >= 1 && cur_item <= info->item_count) {
		int enabled = (cur_item < 32)
		            ? ((info->enableFlags & (1L << cur_item)) != 0)
		            : 1;

		if (enabled)
			result = ((long)info->menuID << 16) | (long)cur_item;
	}
	return result;
}
