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

#include "controls.h"
#include "dialogs.h"
#include "events.h"
#include "macmemory.h"
#include "quickdraw.h"
#include "resources.h"
#include "textedit.h"
#include "windows.h"

static unsigned short be16(const unsigned char *p)
{
	return (unsigned short)(((unsigned)p[0] << 8) | p[1]);
}

/* --- per-dialog auxiliary state ------------------------------------ *
 *
 * One slot per DITL item (indexed by item - 1). For each editText
 * (type 0x10) item, `tes[i]` carries the TEHandle NewDialog allocated;
 * for each button / checkbox / radio (types 4 / 5 / 6) item, `ctrls[i]`
 * carries the ControlHandle. Static-text and other inert items keep
 * both slots NULL. */
typedef struct dlg_aux {
	short          item_count;     /* total DITL item count       */
	ControlHandle *ctrls;          /* one slot per item; NULL OK   */
	TEHandle      *tes;             /* one slot per item; NULL OK   */
} dlg_aux;

static dlg_aux *aux_of(DialogPtr d)
{
	return (d == NULL) ? NULL : (dlg_aux *)d->aux;
}

static TEHandle find_te(DialogPtr d, short item)
{
	dlg_aux *a = aux_of(d);

	if (a == NULL || item < 1 || item > a->item_count)
		return NULL;
	return a->tes[item - 1];
}

static short first_edit_item(DialogPtr d)
{
	dlg_aux *a = aux_of(d);
	short    i;

	if (a == NULL)
		return -1;
	for (i = 0; i < a->item_count; i++) {
		if (a->tes[i] != NULL)
			return (short)(i + 1);
	}
	return -1;
}

static short next_edit_item(DialogPtr d, short cur)
{
	dlg_aux *a = aux_of(d);
	short    i, start;

	if (a == NULL)
		return -1;
	start = (cur >= 1 && cur <= a->item_count) ? cur : 0;
	for (i = 0; i < a->item_count; i++) {
		short k = (short)((start + i) % a->item_count);

		if (a->tes[k] != NULL && (k + 1) != cur)
			return (short)(k + 1);
	}
	return cur;             /* nothing else to focus */
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
	case 4:                                    /* standard button */
	case 5:                                    /* checkbox        */
	case 6:                                    /* radio button    */
		/* When painting on behalf of a DialogPtr, NewDialog has
		 * already allocated ControlHandles for these items; the
		 * Control Manager paints them via DrawControls. Skip the
		 * inline draw to avoid double-painting. Alert still uses
		 * paint_ditl without a dialog (ctx->dlg == NULL), so it
		 * keeps the inline button frame. */
		if (ctx->dlg != NULL)
			break;
		{
			short tw = (short)(data_len * 8);
			short tx = (short)((grect.left + grect.right - tw) / 2);
			short ty = (short)((grect.top + grect.bottom) / 2 + 3);

			RGBForeColor(&black);
			FrameRect(&grect);
			if (item == ctx->def_item && ctx->def_item > 0) {
				Rect outer = grect;

				InsetRect(&outer, -3, -3);
				FrameRect(&outer);
			}
			MoveTo(tx, ty);
			DrawString((ConstStr255Param)data);
		}
		break;
	case 8: {                                  /* static text */
		RGBForeColor(&black);
		MoveTo(grect.left, (short)(grect.top + 7));
		DrawString((ConstStr255Param)data);
		break;
	}
	case 16:                                  /* edit text */
		/* When we own a dialog, TextEdit paints these via TEUpdate
		 * — paint_ditl_for_dialog dispatches that after walk_ditl.
		 * The Alert path (ctx->dlg == NULL) still draws inline since
		 * it doesn't allocate per-item TextEdit records. */
		if (ctx->dlg != NULL)
			break;
		RGBForeColor(&white);
		PaintRect(&grect);
		RGBForeColor(&black);
		FrameRect(&grect);
		if (data_len > 0) {
			MoveTo((short)(grect.left + 3),
			       (short)(grect.top + 9));
			DrawString((ConstStr255Param)data);
		}
		break;
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

	/* The Control Manager owns the button / checkbox / radio paint. */
	DrawControls((WindowPtr)d);

	/* TextEdit owns the editText paint. Each TE's destRect already
	 * lives in global coords (NewDialog set it up that way); we wrap
	 * each one in a 1-px frame to give the field its visible border. */
	{
		dlg_aux *a = aux_of(d);

		if (a != NULL) {
			short i;
			static const RGBColor te_frame = { 0x0000, 0x0000, 0x0000 };
			GrafPtr saved2;

			GetPort(&saved2);
			SetPort(qd_screen_port());
			for (i = 0; i < a->item_count; i++) {
				TEHandle te = a->tes[i];

				if (te == NULL || *te == NULL)
					continue;
				if (d->editField == (short)(i + 1))
					TEActivate(te);
				else
					TEDeactivate(te);
				TEUpdate(NULL, te);
				RGBForeColor(&te_frame);
				FrameRect(&(*te)->destRect);
			}
			SetPort(saved2);
		}
	}
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

/* Count every DITL item so we can size the per-item slabs. */
typedef struct {
	short count;
} count_all_ctx;

static int count_all_visitor(short item, const Rect *local, unsigned char type,
                             const unsigned char *data, unsigned char data_len,
                             void *cookie)
{
	count_all_ctx *ctx = (count_all_ctx *)cookie;

	(void)item;
	(void)local;
	(void)type;
	(void)data;
	(void)data_len;
	ctx->count++;
	return 0;
}

/* DITL-control creation visitor — for types 4/5/6, allocate a
 * ControlHandle attached to the dialog window. refCon = the 1-based
 * DITL item index so DialogSelect can map a control hit back to its
 * item. The slab stays allocated with the aux block; KillControls
 * (run by DisposeWindow) tears down the controls themselves. */
typedef struct {
	dlg_aux  *a;
	WindowPtr owner;
	short     dlog_top;
	short     dlog_left;
} init_ctrl_ctx;

static int init_ctrl_item(short item, const Rect *local, unsigned char type,
                          const unsigned char *data, unsigned char data_len,
                          void *cookie)
{
	init_ctrl_ctx *ctx = (init_ctrl_ctx *)cookie;
	short          procID;
	unsigned char  title[256];
	Rect           r;
	ControlHandle  c;
	short          slot = (short)(item - 1);

	if (slot < 0 || slot >= ctx->a->item_count)
		return 0;
	switch (type & 0x7F) {
	case 4:  procID = pushButProc;   break;
	case 5:  procID = checkBoxProc;  break;
	case 6:  procID = radioButProc;  break;
	default: return 0;
	}
	r = *local;
	title[0] = (unsigned char)data_len;
	if (data_len > 0)
		memcpy(title + 1, data, (size_t)data_len);
	c = NewControl(ctx->owner, &r, title, 1, 0, 0, 1, procID, (long)item);
	if (c != NULL && (type & itemDisable))
		HiliteControl(c, inactiveHilite);
	ctx->a->ctrls[slot] = c;
	return 0;
}

/* DITL editText creation visitor — for type 0x10, TENew a record sized
 * to the item's local rect translated to global. Initial text is set
 * from the DITL data bytes. Each slot is indexed by item - 1; non-
 * editText items keep a NULL slot. */
typedef struct {
	dlg_aux *a;
	short    dlog_top;
	short    dlog_left;
} init_te_ctx;

static int init_te_item(short item, const Rect *local, unsigned char type,
                        const unsigned char *data, unsigned char data_len,
                        void *cookie)
{
	init_te_ctx *ctx = (init_te_ctx *)cookie;
	Rect         dest, view;
	TEHandle     te;
	short        slot = (short)(item - 1);

	if (slot < 0 || slot >= ctx->a->item_count)
		return 0;
	if ((type & 0x7F) != 16)
		return 0;
	dest.top    = (short)(ctx->dlog_top  + local->top);
	dest.left   = (short)(ctx->dlog_left + local->left);
	dest.bottom = (short)(ctx->dlog_top  + local->bottom);
	dest.right  = (short)(ctx->dlog_left + local->right);
	view = dest;
	te = TENew(&dest, &view);
	if (te == NULL)
		return 0;
	if (data_len > 0)
		TESetText(data, (long)data_len, te);
	ctx->a->tes[slot] = te;
	return 0;
}

static dlg_aux *dlg_aux_make(Handle items, WindowPtr owner)
{
	count_all_ctx  ac;
	init_ctrl_ctx  icc;
	init_te_ctx    tcc;
	dlg_aux       *a;
	Rect           cont;

	ac.count = 0;
	(void)walk_ditl(items, count_all_visitor, &ac);

	a = (dlg_aux *)NewPtr((Size)sizeof *a);
	if (a == NULL)
		return NULL;
	a->item_count = ac.count;
	a->ctrls      = NULL;
	a->tes        = NULL;

	if (ac.count == 0)
		return a;

	a->ctrls = (ControlHandle *)NewPtr((Size)ac.count
	                                   * (Size)sizeof *a->ctrls);
	a->tes   = (TEHandle *)NewPtr((Size)ac.count
	                              * (Size)sizeof *a->tes);
	if (a->ctrls == NULL || a->tes == NULL) {
		if (a->ctrls != NULL) DisposePtr((Ptr)a->ctrls);
		if (a->tes   != NULL) DisposePtr((Ptr)a->tes);
		DisposePtr((Ptr)a);
		return NULL;
	}
	memset(a->ctrls, 0, (size_t)ac.count * sizeof *a->ctrls);
	memset(a->tes,   0, (size_t)ac.count * sizeof *a->tes);

	icc.a     = a;
	icc.owner = owner;
	(void)walk_ditl(items, init_ctrl_item, &icc);

	if (((WindowPeek)owner)->contRgn != NULL)
		cont = (*((WindowPeek)owner)->contRgn)->rgnBBox;
	else
		SetRect(&cont, 0, 0, 0, 0);
	tcc.a         = a;
	tcc.dlog_top  = cont.top;
	tcc.dlog_left = cont.left;
	(void)walk_ditl(items, init_te_item, &tcc);

	return a;
}

static void dlg_aux_dispose(dlg_aux *a)
{
	if (a == NULL)
		return;
	if (a->tes != NULL) {
		short i;

		for (i = 0; i < a->item_count; i++) {
			if (a->tes[i] != NULL)
				TEDispose(a->tes[i]);
		}
		DisposePtr((Ptr)a->tes);
	}
	if (a->ctrls != NULL)
		DisposePtr((Ptr)a->ctrls);
	/* The ControlHandles themselves are owned by the dialog window;
	 * KillControls (invoked by DisposeWindow) tears them down. */
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

	aux = dlg_aux_make(items, (WindowPtr)d);
	d->aux = aux;
	/* Focus the first editText item if there is one — saves the user a
	 * click before typing. */
	{
		short first = first_edit_item((DialogPtr)d);

		if (first > 0) {
			d->editField = first;
			TEActivate(aux->tes[first - 1]);
		}
	}

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
		ControlHandle hit_ctrl = NULL;
		Rect          cont;
		short         hit;

		if (d->window.contRgn == NULL)
			break;
		/* Controls first — NewDialog wired DITL types 4/5/6 to the
		 * Control Manager, so a button/checkbox/radio click routes
		 * through TrackControl and we map back to the DITL item via
		 * refCon. */
		if (FindControl(event->where, (WindowPtr)d, &hit_ctrl) != 0
		 && hit_ctrl != NULL) {
			if (TrackControl(hit_ctrl, event->where, NULL) != 0) {
				short item = (short)GetControlReference(hit_ctrl);
				short procID = (short)((long)(*hit_ctrl)->contrlDefProc
				                        & 0xFF);

				/* Toggle the value on checkbox / radio so the
				 * caller sees the new state via GetControlValue.
				 * Radio-group exclusivity is the caller's job. */
				if (procID == checkBoxProc)
					SetControlValue(hit_ctrl,
					    (short)(GetControlValue(hit_ctrl) ? 0 : 1));
				else if (procID == radioButProc)
					SetControlValue(hit_ctrl, 1);

				if (item > 0) {
					*itemHit = item;
					return 1;
				}
			}
			break;
		}
		cont = (*d->window.contRgn)->rgnBBox;
		hit  = ditl_hit_button(&cont, d->items, event->where);
		if (hit > 0) {
			*itemHit = hit;
			return 1;
		}
		hit = ditl_hit_edit(&cont, d->items, event->where);
		if (hit > 0) {
			TEHandle te = find_te(d, hit);

			if (te != NULL) {
				if (hit != d->editField) {
					if (d->editField > 0) {
						TEHandle prev = find_te(d, d->editField);

						if (prev != NULL)
							TEDeactivate(prev);
					}
					d->editField = hit;
					TEActivate(te);
				}
				TEClick(event->where, 0, te);
				DrawDialog(d);
			}
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
			short next = next_edit_item(d, d->editField);

			if (next > 0 && next != d->editField) {
				if (d->editField > 0) {
					TEHandle prev = find_te(d, d->editField);

					if (prev != NULL)
						TEDeactivate(prev);
				}
				d->editField = next;
				{
					TEHandle te = find_te(d, next);

					if (te != NULL)
						TEActivate(te);
				}
				DrawDialog(d);
			}
			break;
		}
		/* All remaining keystrokes go to the focused field's TE. */
		if (d->editField > 0) {
			TEHandle te = find_te(d, d->editField);

			if (te != NULL) {
				TEKey((short)ch, te);
				DrawDialog(d);
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
	TEHandle te;
	short    n;
	const char *src;

	if (str == NULL)
		return;
	str[0] = 0;
	te = find_te(d, itemNum);
	if (te == NULL || *te == NULL)
		return;
	n = (*te)->teLength;
	if (n > 255)
		n = 255;
	if (n > 0 && (*te)->hText != NULL && *(*te)->hText != NULL) {
		src = (const char *)*(*te)->hText;
		str[0] = (unsigned char)n;
		memcpy(str + 1, src, (size_t)n);
	}
}

void dialog_set_edit_text(DialogPtr d, short itemNum, ConstStr255Param str)
{
	TEHandle te;

	te = find_te(d, itemNum);
	if (te == NULL || str == NULL)
		return;
	TESetText(str + 1, (long)str[0], te);
	DrawDialog(d);
}
