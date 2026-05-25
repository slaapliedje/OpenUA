/*
 * Mac Dialog Manager shim — Alert + GetNewDialog / ModalDialog. See dialogs.h.
 *
 * The dialog is a colour window (NewCWindow) at the DLOG bounds; the items
 * come from the DITL it points at, kept as a Handle on the DialogRecord.
 * Drawing goes through the screen port (same path Alert uses); hit-testing
 * walks the DITL on each mouseDown and returns the 1-based item index of
 * an enabled standard button under the click. ModalDialog wraps that in a
 * WaitNextEvent loop; Return / Enter fires aDefItem when one is set.
 *
 * DLOG layout — see compat/include/dialogs.h.
 *
 * DITL layout:
 *   +0  short                 item-count minus one
 *   +2  N items, each:
 *      +0  4 bytes             handle / proc placeholder (ignored)
 *      +4  8 bytes Rect        local rect (relative to dialog top-left)
 *      +12 byte                item type (low 7 bits; high bit = disabled)
 *      +13 byte                item data length
 *      +14 N bytes             item data (button title, text, ...)
 *      pad to even byte
 *
 * Item types handled: 0x04 standard button (FrameRect + centred title);
 * 0x08 static text (DrawString at top-left + ascent). Edit-text, user
 * items, icons, pictures follow when the engine reaches for them.
 *
 * Modal hit-testing returns the first enabled button whose rect contains
 * the mouse. Static text never fires. Disabled (high bit of type set)
 * items are skipped. The default-item border (the Mac's thick outline
 * around aDefItem) isn't drawn yet — the dispatch still honours Return
 * / Enter via aDefItem.
 */

#include <stddef.h>             /* NULL */
#include <string.h>             /* memcpy */

#include "dialogs.h"
#include "events.h"
#include "macmemory.h"
#include "quickdraw.h"
#include "resources.h"
#include "windows.h"

static unsigned short be16(const unsigned char *p)
{
	return (unsigned short)(((unsigned)p[0] << 8) | p[1]);
}

/* --- per-dialog edit-text state ----------------------------------- */

#define EDIT_TEXT_CAP   255

typedef struct edit_item {
	short         item;                     /* 1-based DITL index */
	unsigned char text[EDIT_TEXT_CAP + 1];  /* Pascal string      */
} edit_item;

typedef struct dlg_aux {
	short      edit_count;
	edit_item *edits;       /* slab of edit_count entries */
} dlg_aux;

static dlg_aux *aux_of(DialogPtr d)
{
	return (d == NULL) ? NULL : (dlg_aux *)d->aux;
}

static edit_item *find_edit(DialogPtr d, short item)
{
	dlg_aux *a = aux_of(d);
	short    i;

	if (a == NULL)
		return NULL;
	for (i = 0; i < a->edit_count; i++) {
		if (a->edits[i].item == item)
			return &a->edits[i];
	}
	return NULL;
}

/* Walk the DITL items, calling visit(...) on each. Returns the count.
 *
 * visit receives the 1-based item index, the item's local rect (relative
 * to dialog origin), the raw type byte (high bit = disabled), the
 * Pascal-string item data, and the caller's cookie. Stops walking when
 * visit returns non-zero — that visitor value becomes the walk's return. */
typedef int (*ditl_visitor)(short item, const Rect *local, unsigned char type,
                            const unsigned char *data, unsigned char data_len,
                            void *cookie);

static int walk_ditl(Handle items, ditl_visitor visit, void *cookie)
{
	const unsigned char *d;
	long                 sz, off;
	short                count, i;
	int                  hit = 0;

	if (items == NULL || *items == NULL || visit == NULL)
		return 0;
	sz = GetHandleSize(items);
	if (sz < 2)
		return 0;
	d = (const unsigned char *)*items;
	count = (short)(be16(d + 0) + 1);
	off = 2;
	for (i = 0; i < count && off + 14 <= sz; i++) {
		Rect          local;
		unsigned char itype, ilen;

		local.top    = (short)be16(d + off + 4);
		local.left   = (short)be16(d + off + 6);
		local.bottom = (short)be16(d + off + 8);
		local.right  = (short)be16(d + off + 10);
		itype = d[off + 12];
		ilen  = d[off + 13];

		if (off + 14 + ilen > sz)
			break;
		hit = visit((short)(i + 1), &local, itype, d + off + 13,
		            ilen, cookie);
		if (hit != 0)
			return hit;

		off += 14 + ilen;
		if ((ilen & 1) == 1)
			off++;
	}
	return 0;
}

/* --- the shared paint pass ----------------------------------------- */

typedef struct {
	short      dlog_top;
	short      dlog_left;
	DialogPtr  dlg;                 /* NULL for Alert path        */
	short      def_item;            /* aDefItem for thicker frame */
	short      focus_item;          /* editField; 0 if none       */
} paint_ctx;

static int paint_item(short item, const Rect *local, unsigned char type,
                      const unsigned char *data, unsigned char data_len,
                      void *cookie)
{
	paint_ctx *ctx = (paint_ctx *)cookie;
	Rect       grect;
	static const RGBColor black = { 0, 0, 0 };
	static const RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };

	grect.top    = (short)(ctx->dlog_top  + local->top);
	grect.left   = (short)(ctx->dlog_left + local->left);
	grect.bottom = (short)(ctx->dlog_top  + local->bottom);
	grect.right  = (short)(ctx->dlog_left + local->right);

	switch (type & 0x7F) {
	case 4: {                                  /* standard button */
		short tw = (short)(data_len * 8);
		short tx = (short)((grect.left + grect.right - tw) / 2);
		short ty = (short)((grect.top + grect.bottom) / 2 + 3);

		RGBForeColor(&black);
		FrameRect(&grect);
		/* Default item gets a thicker outer frame — the Mac
		 * double-ring around OK that prompts the user that Return
		 * fires it. We inset by 3 and re-frame for the visual cue. */
		if (item == ctx->def_item && ctx->def_item > 0) {
			Rect outer = grect;

			InsetRect(&outer, -3, -3);
			FrameRect(&outer);
		}
		MoveTo(tx, ty);
		DrawString((ConstStr255Param)data);
		break;
	}
	case 8: {                                  /* static text */
		RGBForeColor(&black);
		MoveTo(grect.left, (short)(grect.top + 7));
		DrawString((ConstStr255Param)data);
		break;
	}
	case 16: {                                 /* edit text */
		edit_item *e = find_edit(ctx->dlg, item);
		short      ty;

		/* White interior + 1-pixel black frame. */
		RGBForeColor(&white);
		PaintRect(&grect);
		RGBForeColor(&black);
		FrameRect(&grect);

		ty = (short)(grect.top + 9);
		MoveTo((short)(grect.left + 3), ty);
		if (e != NULL && e->text[0] > 0)
			DrawString(e->text);
		else if (data_len > 0)
			DrawString((ConstStr255Param)data);

		/* Insertion caret at end of text on the focused field. The
		 * shim's 8x8 glyph advance is 6 px (see CharWidth fallback);
		 * the caret sits one pixel right of the last glyph. */
		if (item == ctx->focus_item && ctx->focus_item > 0) {
			short txt_len = (e != NULL) ? (short)e->text[0]
			                            : (short)data_len;
			short cx      = (short)(grect.left + 3 + txt_len * 6);

			if (cx >= grect.right - 2)
				cx = (short)(grect.right - 2);
			MoveTo(cx, (short)(grect.top + 2));
			LineTo(cx, (short)(grect.bottom - 3));
		}
		break;
	}
	default:
		break;
	}
	return 0;       /* never stops the walk */
}

/* Repaint every DITL item inside `bounds` on the screen port. */
static void paint_ditl(const Rect *bounds, Handle items)
{
	paint_ctx ctx;
	GrafPtr   saved_port;

	if (bounds == NULL || items == NULL)
		return;
	ctx.dlog_top   = bounds->top;
	ctx.dlog_left  = bounds->left;
	ctx.dlg        = NULL;
	ctx.def_item   = 0;
	ctx.focus_item = 0;

	GetPort(&saved_port);
	SetPort(qd_screen_port());
	(void)walk_ditl(items, paint_item, &ctx);
	SetPort(saved_port);
}

/* Variant for DrawDialog — knows the dialog so it can show the focus
 * caret and the default-item ring. */
static void paint_ditl_for_dialog(DialogPtr d)
{
	paint_ctx ctx;
	GrafPtr   saved_port;
	Rect      cont;

	if (d == NULL || d->items == NULL || d->window.contRgn == NULL)
		return;
	cont           = (*d->window.contRgn)->rgnBBox;
	ctx.dlog_top   = cont.top;
	ctx.dlog_left  = cont.left;
	ctx.dlg        = d;
	ctx.def_item   = d->aDefItem;
	ctx.focus_item = d->editField;

	GetPort(&saved_port);
	SetPort(qd_screen_port());
	(void)walk_ditl(d->items, paint_item, &ctx);
	SetPort(saved_port);
}

/* --- hit-testing -------------------------------------------------- */

typedef struct {
	Point where;            /* in global screen coordinates    */
	short dlog_top;
	short dlog_left;
} hit_ctx;

static int hit_item(short item, const Rect *local, unsigned char type,
                    const unsigned char *data, unsigned char data_len,
                    void *cookie)
{
	hit_ctx *ctx = (hit_ctx *)cookie;
	Rect grect;

	(void)data;
	(void)data_len;
	if ((type & itemDisable) != 0)
		return 0;
	if ((type & 0x7F) != 4)             /* only buttons fire */
		return 0;
	grect.top    = (short)(ctx->dlog_top  + local->top);
	grect.left   = (short)(ctx->dlog_left + local->left);
	grect.bottom = (short)(ctx->dlog_top  + local->bottom);
	grect.right  = (short)(ctx->dlog_left + local->right);
	if (PtInRect(ctx->where, &grect))
		return item;
	return 0;
}

static short ditl_hit_button(const Rect *bounds, Handle items, Point where)
{
	hit_ctx ctx;

	if (bounds == NULL || items == NULL)
		return 0;
	ctx.where     = where;
	ctx.dlog_top  = bounds->top;
	ctx.dlog_left = bounds->left;
	return (short)walk_ditl(items, hit_item, &ctx);
}

/* Variant of hit_item that fires for edit-text items only — used to give
 * an editText field focus when the user clicks it. */
static int hit_edit_item(short item, const Rect *local, unsigned char type,
                         const unsigned char *data, unsigned char data_len,
                         void *cookie)
{
	hit_ctx *ctx = (hit_ctx *)cookie;
	Rect grect;

	(void)data;
	(void)data_len;
	if ((type & itemDisable) != 0)
		return 0;
	if ((type & 0x7F) != 16)
		return 0;
	grect.top    = (short)(ctx->dlog_top  + local->top);
	grect.left   = (short)(ctx->dlog_left + local->left);
	grect.bottom = (short)(ctx->dlog_top  + local->bottom);
	grect.right  = (short)(ctx->dlog_left + local->right);
	if (PtInRect(ctx->where, &grect))
		return item;
	return 0;
}

static short ditl_hit_edit(const Rect *bounds, Handle items, Point where)
{
	hit_ctx ctx;

	if (bounds == NULL || items == NULL)
		return 0;
	ctx.where     = where;
	ctx.dlog_top  = bounds->top;
	ctx.dlog_left = bounds->left;
	return (short)walk_ditl(items, hit_edit_item, &ctx);
}

/* --- item box lookup (GetDialogItem support) ---------------------- */

typedef struct {
	short        want;
	short        type;
	Rect         local;
	int          found;
} item_ctx;

static int find_item(short item, const Rect *local, unsigned char type,
                     const unsigned char *data, unsigned char data_len,
                     void *cookie)
{
	item_ctx *ctx = (item_ctx *)cookie;

	(void)data;
	(void)data_len;
	if (item != ctx->want)
		return 0;
	ctx->type  = (short)type;
	ctx->local = *local;
	ctx->found = 1;
	return item;
}

/* ================================================================== */
/*  Alert — unchanged behaviour, now backed by the shared paint pass. */
/* ================================================================== */

short Alert(short alertID, void *filterProc)
{
	Handle               h_alrt, h_ditl;
	const unsigned char *a;
	Rect                 bounds;
	short                ditl_id;
	WindowPtr            w;

	(void)filterProc;
	h_alrt = GetResource(0x414C5254L /* 'ALRT' */, alertID);
	if (h_alrt == NULL || *h_alrt == NULL)
		return -1;
	if (GetHandleSize(h_alrt) < 10)
		return -1;
	a = (const unsigned char *)*h_alrt;
	bounds.top    = (short)be16(a + 0);
	bounds.left   = (short)be16(a + 2);
	bounds.bottom = (short)be16(a + 4);
	bounds.right  = (short)be16(a + 6);
	ditl_id       = (short)be16(a + 8);

	h_ditl = GetResource(0x4449544CL /* 'DITL' */, ditl_id);
	if (h_ditl == NULL || *h_ditl == NULL)
		return -1;
	if (GetHandleSize(h_ditl) < 2)
		return -1;

	w = NewCWindow(NULL, &bounds, (ConstStr255Param)"",
	               1, 0, (WindowPtr)-1L, 0, 0);
	if (w == NULL)
		return -1;

	paint_ditl(&bounds, h_ditl);
	qd_present();

	{
		EventRecord e;

		for (;;) {
			if (!WaitNextEvent(everyEvent, &e, 1, NULL))
				continue;
			if (e.what == keyDown || e.what == mouseDown)
				break;
		}
	}

	DisposeWindow(w);
	return 1;
}

/* ================================================================== */
/*  GetNewDialog / NewDialog / DisposeDialog                           */
/* ================================================================== */

/* Visitor that initialises per-edit-item state from the DITL data bytes. */
typedef struct {
	dlg_aux *a;
	short    next;
} init_edit_ctx;

static int init_edit_item(short item, const Rect *local, unsigned char type,
                          const unsigned char *data, unsigned char data_len,
                          void *cookie)
{
	init_edit_ctx *ctx = (init_edit_ctx *)cookie;
	edit_item     *e;
	short          n;

	(void)local;
	if ((type & 0x7F) != 16)
		return 0;
	if (ctx->next >= ctx->a->edit_count)
		return 0;
	e = &ctx->a->edits[ctx->next++];
	e->item = item;
	n = (short)data_len;
	if (n > EDIT_TEXT_CAP)
		n = EDIT_TEXT_CAP;
	e->text[0] = (unsigned char)n;
	if (n > 0)
		memcpy(e->text + 1, data, (size_t)n);
	return 0;       /* keep walking; we want every edit item */
}

/* Count edit-text items so NewDialog can size the slab in one pass. */
typedef struct {
	short count;
} count_edit_ctx;

static int count_edit_visitor(short item, const Rect *local, unsigned char type,
                              const unsigned char *data,
                              unsigned char data_len, void *cookie)
{
	count_edit_ctx *ctx = (count_edit_ctx *)cookie;

	(void)item;
	(void)local;
	(void)data;
	(void)data_len;
	if ((type & 0x7F) == 16)
		ctx->count++;
	return 0;
}

static dlg_aux *dlg_aux_make(Handle items)
{
	count_edit_ctx cc;
	init_edit_ctx  ic;
	dlg_aux       *a;

	cc.count = 0;
	(void)walk_ditl(items, count_edit_visitor, &cc);

	a = (dlg_aux *)NewPtr((Size)sizeof *a);
	if (a == NULL)
		return NULL;
	a->edit_count = cc.count;
	a->edits      = NULL;
	if (cc.count > 0) {
		a->edits = (edit_item *)NewPtr((Size)cc.count
		                               * (Size)sizeof *a->edits);
		if (a->edits == NULL) {
			DisposePtr((Ptr)a);
			return NULL;
		}
		memset(a->edits, 0, (size_t)cc.count * sizeof *a->edits);
		ic.a    = a;
		ic.next = 0;
		(void)walk_ditl(items, init_edit_item, &ic);
	}
	return a;
}

static void dlg_aux_dispose(dlg_aux *a)
{
	if (a == NULL)
		return;
	if (a->edits != NULL)
		DisposePtr((Ptr)a->edits);
	DisposePtr((Ptr)a);
}

DialogPtr NewDialog(void *dStorage, const Rect *bounds,
                    ConstStr255Param title, Boolean visible, short procID,
                    WindowPtr behind, Boolean goAwayFlag, long refCon,
                    Handle items)
{
	DialogRecord *d;
	dlg_aux      *aux;

	if (bounds == NULL)
		return NULL;
	if (dStorage != NULL) {
		d = (DialogRecord *)dStorage;
		memset(d, 0, sizeof *d);
	} else {
		d = (DialogRecord *)NewPtr((Size)sizeof *d);
		if (d == NULL)
			return NULL;
		memset(d, 0, sizeof *d);
	}

	if (NewCWindow(&d->window, bounds, title, visible, procID, behind,
	               goAwayFlag, refCon) == NULL) {
		if (dStorage == NULL)
			DisposePtr((Ptr)d);
		return NULL;
	}

	d->items     = items;
	d->textH     = NULL;
	d->editField = -1;
	d->editOpen  = 0;
	d->aDefItem  = 1;       /* Mac default: item 1 is OK */

	aux = dlg_aux_make(items);
	d->aux = aux;
	/* Focus the first edit field if there is one — saves the user a
	 * click before typing. */
	if (aux != NULL && aux->edit_count > 0)
		d->editField = aux->edits[0].item;

	return (DialogPtr)d;
}

DialogPtr GetNewDialog(short dialogID, void *dStorage, WindowPtr behind)
{
	Handle               h_dlog, h_ditl;
	const unsigned char *p;
	Rect                 bounds;
	short                procID, itemsID;
	Boolean              visible, goAway;
	long                 refCon;
	unsigned char        title[256];
	DialogPtr            d;

	h_dlog = GetResource(0x444C4F47L /* 'DLOG' */, dialogID);
	if (h_dlog == NULL || *h_dlog == NULL)
		return NULL;
	if (GetHandleSize(h_dlog) < 21)
		return NULL;
	p = (const unsigned char *)*h_dlog;
	bounds.top    = (short)be16(p + 0);
	bounds.left   = (short)be16(p + 2);
	bounds.bottom = (short)be16(p + 4);
	bounds.right  = (short)be16(p + 6);
	procID  = (short)be16(p + 8);
	visible = (Boolean)p[10];
	goAway  = (Boolean)p[12];
	refCon  = ((long)p[14] << 24) | ((long)p[15] << 16)
	        | ((long)p[16] << 8)  | (long)p[17];
	itemsID = (short)be16(p + 18);

	title[0] = p[20];
	if (title[0] > 0)
		memcpy(title + 1, p + 21, (size_t)title[0]);

	h_ditl = GetResource(0x4449544CL /* 'DITL' */, itemsID);
	if (h_ditl == NULL || *h_ditl == NULL)
		return NULL;

	d = NewDialog(dStorage, &bounds, title, visible, procID, behind,
	              goAway, refCon, h_ditl);
	return d;
}

void DisposeDialog(DialogPtr d)
{
	WindowPtr win;

	if (d == NULL)
		return;
	dlg_aux_dispose(aux_of(d));
	d->aux = NULL;
	win = (WindowPtr)d;
	/* CloseWindow disposes the regions; DisposeWindow would also free
	 * the WindowRecord storage we own as part of the DialogRecord, so
	 * we explicitly use CloseWindow + DisposePtr on the dialog. */
	CloseWindow(win);
	DisposePtr((Ptr)d);
}

/* ================================================================== */
/*  Drawing                                                            */
/* ================================================================== */

void DrawDialog(DialogPtr d)
{
	if (d == NULL)
		return;
	paint_ditl_for_dialog(d);
	qd_present();
}

/* ================================================================== */
/*  Modal event loop                                                   */
/* ================================================================== */

Boolean DialogSelect(const EventRecord *event, DialogPtr *theDialog,
                     short *itemHit)
{
	DialogPtr d;

	if (event == NULL || theDialog == NULL || *theDialog == NULL
	 || itemHit == NULL)
		return 0;
	d = *theDialog;

	switch (event->what) {
	case mouseDown: {
		Rect  cont;
		short hit;

		if (d->window.contRgn == NULL)
			break;
		cont = (*d->window.contRgn)->rgnBBox;
		hit  = ditl_hit_button(&cont, d->items, event->where);
		if (hit > 0) {
			*itemHit = hit;
			return 1;
		}
		hit = ditl_hit_edit(&cont, d->items, event->where);
		if (hit > 0 && hit != d->editField) {
			d->editField = hit;
			DrawDialog(d);
		}
		break;
	}
	case keyDown: {
		unsigned char ch = (unsigned char)(event->message & 0xFF);

		/* Return / Enter fires the default item. */
		if ((ch == 0x0D || ch == 0x03) && d->aDefItem > 0) {
			*itemHit = d->aDefItem;
			return 1;
		}
		/* Tab cycles focus through the dialog's edit fields. */
		if (ch == 0x09) {
			dlg_aux *a = aux_of(d);

			if (a != NULL && a->edit_count > 0) {
				short i, next;

				for (i = 0; i < a->edit_count; i++) {
					if (a->edits[i].item == d->editField)
						break;
				}
				next = (i + 1) % a->edit_count;
				if (i == a->edit_count)         /* not found */
					next = 0;
				d->editField = a->edits[next].item;
				DrawDialog(d);
			}
			break;
		}
		/* All remaining keystrokes go to the focused field, if any. */
		if (d->editField > 0) {
			edit_item *e = find_edit(d, d->editField);

			if (e == NULL)
				break;
			if (ch == 0x08) {                    /* backspace */
				if (e->text[0] > 0) {
					e->text[0]--;
					DrawDialog(d);
				}
			} else if (ch >= 0x20 && ch < 0x7F) {
				if (e->text[0] < EDIT_TEXT_CAP) {
					e->text[++e->text[0]] = ch;
					DrawDialog(d);
				}
			}
		}
		break;
	}
	default:
		break;
	}
	return 0;
}

void ModalDialog(void *filterProc, short *itemHit)
{
	DialogPtr   d;
	EventRecord e;

	(void)filterProc;
	if (itemHit == NULL)
		return;
	*itemHit = 0;
	d = (DialogPtr)FrontWindow();
	if (d == NULL)
		return;

	/* Paint once on entry so the dialog is visible even if the caller
	 * created it hidden and made it visible just before this call. */
	DrawDialog(d);

	for (;;) {
		if (!WaitNextEvent(everyEvent, &e, 1, NULL))
			continue;
		if (DialogSelect(&e, &d, itemHit))
			return;
	}
}

/* ================================================================== */
/*  Item access — the GetDialogItem / SetDialogItemText helpers        */
/* ================================================================== */

void GetDialogItem(DialogPtr d, short itemNum, short *type, Handle *itemH,
                   Rect *box)
{
	item_ctx ctx;

	if (type != NULL)  *type = 0;
	if (itemH != NULL) *itemH = NULL;
	if (box != NULL)   SetRect(box, 0, 0, 0, 0);
	if (d == NULL || d->items == NULL || itemNum < 1)
		return;
	ctx.want  = itemNum;
	ctx.type  = 0;
	ctx.found = 0;
	(void)walk_ditl(d->items, find_item, &ctx);
	if (!ctx.found)
		return;
	if (type != NULL) *type = ctx.type;
	if (box  != NULL) *box  = ctx.local;
	/* itemH stays NULL — controls and TE handles arrive with those
	 * managers; the skeleton returns the raw rect, which is what the
	 * Mac docs guarantee is set. */
}

void SetDialogItemText(Handle item, ConstStr255Param text)
{
	/* The skeleton's static-text items live inside the DITL bytes; the
	 * Mac copies their text into a separate StringHandle on dialog
	 * creation so editors can edit them freely. We don't allocate that
	 * yet — when an engine call needs round-trip text, plumb through a
	 * per-item string handle here. */
	(void)item;
	(void)text;
}

void GetDialogItemText(Handle item, unsigned char *text)
{
	if (text != NULL)
		text[0] = 0;
	(void)item;
}

/* --- per-dialog edit-text helpers --------------------------------- */

void dialog_get_edit_text(DialogPtr d, short itemNum, unsigned char *str)
{
	edit_item *e;
	short      n;

	if (str == NULL)
		return;
	str[0] = 0;
	e = find_edit(d, itemNum);
	if (e == NULL)
		return;
	n = (short)e->text[0];
	str[0] = (unsigned char)n;
	if (n > 0)
		memcpy(str + 1, e->text + 1, (size_t)n);
}

void dialog_set_edit_text(DialogPtr d, short itemNum, ConstStr255Param str)
{
	edit_item *e;
	short      n;

	e = find_edit(d, itemNum);
	if (e == NULL || str == NULL)
		return;
	n = (short)str[0];
	if (n > EDIT_TEXT_CAP)
		n = EDIT_TEXT_CAP;
	e->text[0] = (unsigned char)n;
	if (n > 0)
		memcpy(e->text + 1, str + 1, (size_t)n);
	DrawDialog(d);
}
