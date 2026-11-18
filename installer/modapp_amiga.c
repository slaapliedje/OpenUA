/* modapp_amiga — the module manager's window on the Amiga (Intuition).
 *
 * The shell around installer/modapp.c: a window on the Workbench screen,
 * its IDCMP loop, and the RastPort retroplat's Amiga backend draws through
 * (amiga_gc). The window is GIMMEZEROZERO, so the shared screen draws from
 * (0,0) in its inner area. Keys: cursor up/down move, Shift+cursor pages, a
 * letter jumps, Return installs, R fetches the list, Esc/Q quits; a click
 * picks a module and a double click installs it. The window can be moved,
 * sized and sent back; the close gadget quits.
 *
 * Run from the OpenUA drawer (it installs into the current directory), as
 * modman next to frua. Kickstart 2.0 or later; 68000 code.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <string.h>

#include "platform.h"
#include "metrics_amiga_internal.h"
#include "modapp.h"

int uainst_run_big_stack(int (*fn)(int, char **), int argc, char **argv);

struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;

static ma_app     g_app;
static ma_view    g_view;
static amiga_gc   g_gc;
static amiga_font g_font;
static struct Window *g_win;

static void sizes(void)
{
	g_view.x = 0;
	g_view.y = 0;
	g_view.w = g_win->GZZWidth;
	g_view.h = g_win->GZZHeight;
}

static void refresh_status(void *shell)
{
	(void)shell;
	ma_draw_status(&g_app, &g_view);
}

static int key_of(UWORD code, UWORD qual, int raw)
{
	int shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;

	if (raw) {
		switch (code) {
		case 0x4C: return shift ? MA_K_PGUP : MA_K_UP;
		case 0x4D: return shift ? MA_K_PGDN : MA_K_DOWN;
		case 0x3D: return MA_K_HOME;         /* keypad 7 (Home)        */
		case 0x1D: return MA_K_END;          /* keypad 1 (End)         */
		case 0x3F: return MA_K_PGUP;         /* keypad 9 (PgUp)        */
		case 0x1F: return MA_K_PGDN;         /* keypad 3 (PgDn)        */
		default:   return 0;
		}
	}
	if (code == 13)
		return MA_K_ENTER;
	if (code == 27)
		return MA_K_ESC;
	return code;
}

static int run(int argc, char **argv)
{
	struct Screen *scr;
	struct DrawInfo *dri;
	ULONG last_s = 0, last_m = 0;
	int quit = 0;

	(void)argc;
	(void)argv;
	IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 37);
	GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 37);
	if (!IntuitionBase || !GfxBase)
		return 20;                      /* Kickstart 1.x */
	scr = LockPubScreen(NULL);
	if (!scr)
		return 20;
	dri = GetScreenDrawInfo(scr);
	ma_init(&g_app, "");
	g_win = OpenWindowTags(NULL,
		WA_Left, 0, WA_Top, scr->BarHeight + 1,
		WA_Width, scr->Width, WA_Height, scr->Height - scr->BarHeight - 1,
		WA_MinWidth, 240, WA_MinHeight, 100, WA_MaxWidth, -1, WA_MaxHeight, -1,
		WA_Title, (ULONG)"OpenUA Module Manager",
		WA_PubScreen, (ULONG)scr,
		WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
		WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE, WA_Activate, TRUE,
		WA_GimmeZeroZero, TRUE, WA_SmartRefresh, TRUE,
		WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY | IDCMP_RAWKEY |
		          IDCMP_MOUSEBUTTONS | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW,
		TAG_DONE);
	if (!g_win) {
		FreeScreenDrawInfo(scr, dri);
		UnlockPubScreen(NULL, scr);
		return 20;
	}
	/* the window's own font: topaz, or whatever the Workbench uses */
	g_font.tf = g_win->RPort->Font;
	g_font.owns_font = FALSE;
	g_gc.rp = g_win->RPort;
	g_gc.dri = dri;
	g_gc.screen = scr;
	g_view.gc = (plat_gc *)&g_gc;
	g_view.font = (plat_font *)&g_font;
	g_view.cw = g_font.tf->tf_XSize;
	g_view.lh = g_font.tf->tf_YSize + 1;
	g_view.ascent = g_font.tf->tf_Baseline + 1;
	g_view.refresh_status = refresh_status;
	g_app.view = &g_view;
	sizes();
	ma_draw(&g_app, &g_view);

	while (!quit) {
		struct IntuiMessage *m;
		int r = MA_NOTHING;

		WaitPort(g_win->UserPort);
		while ((m = (struct IntuiMessage *)GetMsg(g_win->UserPort)) != 0) {
			ULONG cls = m->Class;
			UWORD code = m->Code, qual = m->Qualifier;
			WORD mx = m->MouseX, my = m->MouseY;
			ULONG s = m->Seconds, us = m->Micros;

			ReplyMsg((struct Message *)m);
			switch (cls) {
			case IDCMP_CLOSEWINDOW:
				quit = 1;
				break;
			case IDCMP_NEWSIZE:
				sizes();
				r = MA_REDRAW;
				break;
			case IDCMP_REFRESHWINDOW:
				BeginRefresh(g_win);
				ma_draw(&g_app, &g_view);
				EndRefresh(g_win, TRUE);
				break;
			case IDCMP_VANILLAKEY:
				r = ma_key(&g_app, key_of(code, qual, 0));
				break;
			case IDCMP_RAWKEY: {
				int k = key_of(code, qual, 1);

				if (k)
					r = ma_key(&g_app, k);
				break;
			}
			case IDCMP_MOUSEBUTTONS:
				if (code == SELECTDOWN) {
					int twice = DoubleClick(last_s, last_m, s, us);

					last_s = s;
					last_m = us;
					/* GIMMEZEROZERO: the inner area's own coordinates */
					r = ma_click(&g_app, g_win->GZZMouseX, g_win->GZZMouseY, twice);
					(void)mx;
					(void)my;
				}
				break;
			default:
				break;
			}
			if (r == MA_QUIT)
				quit = 1;
		}
		if (!quit && r == MA_REDRAW)
			ma_draw(&g_app, &g_view);
	}
	CloseWindow(g_win);
	FreeScreenDrawInfo(scr, dri);
	UnlockPubScreen(NULL, scr);
	ma_free(&g_app);
	CloseLibrary((struct Library *)GfxBase);
	CloseLibrary((struct Library *)IntuitionBase);
	return 0;
}

int main(int argc, char **argv)
{
	/* miniz, the art converter and the install need far more stack than a
	 * Workbench or Shell launch grants: run on a large swapped one. */
	return uainst_run_big_stack(run, argc, argv);
}
