/* modapp_gem — the module manager's window on the Atari (GEM).
 *
 * The shell around installer/modapp.c: one GEM window, its event loop, and
 * the VDI handle retroplat's GEM backend draws through (atari_gc). Keys:
 * the arrows move, Shift+arrows page, Clr/Home goes to the top, Return
 * installs, R fetches the list, Esc or Q quits; a click picks a module and a
 * double click installs it. The window can be moved, sized and fulled.
 *
 * Runs from the OpenUA folder (it installs into the current folder), as
 * MODMAN.PRG next to FRUA.PRG. 68000 build, so one binary serves every ST.
 */
#include <gem.h>
#include <string.h>

#include "platform.h"
#include "metrics_atari_internal.h"
#include "modapp.h"

#define PARTS (NAME | CLOSER | FULLER | MOVER | SIZER)

static ma_app  g_app;
static ma_view g_view;
static atari_gc g_gc;
static short   g_win = -1;
static GRECT   g_work;

static int rc_intersect_(const GRECT *a, GRECT *b)
{
	short x1 = a->g_x > b->g_x ? a->g_x : b->g_x;
	short y1 = a->g_y > b->g_y ? a->g_y : b->g_y;
	short x2 = (a->g_x + a->g_w < b->g_x + b->g_w) ? a->g_x + a->g_w : b->g_x + b->g_w;
	short y2 = (a->g_y + a->g_h < b->g_y + b->g_h) ? a->g_y + a->g_h : b->g_y + b->g_h;

	b->g_x = x1;
	b->g_y = y1;
	b->g_w = x2 - x1;
	b->g_h = y2 - y1;
	return b->g_w > 0 && b->g_h > 0;
}

static void clip_to(const GRECT *r)
{
	short pxy[4];

	pxy[0] = r->g_x;
	pxy[1] = r->g_y;
	pxy[2] = r->g_x + r->g_w - 1;
	pxy[3] = r->g_y + r->g_h - 1;
	vs_clip(g_gc.vdi, 1, pxy);
	g_gc.clip_on = 1;
	memcpy(g_gc.clip, pxy, sizeof pxy);
}

static void work_area(void)
{
	wind_get(g_win, WF_WORKXYWH, &g_work.g_x, &g_work.g_y, &g_work.g_w, &g_work.g_h);
	g_view.x = g_work.g_x;
	g_view.y = g_work.g_y;
	g_view.w = g_work.g_w;
	g_view.h = g_work.g_h;
}

/* Paint `dirty` (or the whole work area) through the window's visible
 * rectangles; `status_only` repaints just the bottom line. */
static void repaint(const GRECT *dirty, int status_only)
{
	GRECT r, want = dirty ? *dirty : g_work;

	if (!rc_intersect_(&g_work, &want))
		return;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	wind_get(g_win, WF_FIRSTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
	while (r.g_w > 0 && r.g_h > 0) {
		if (rc_intersect_(&want, &r)) {
			clip_to(&r);
			if (status_only)
				ma_draw_status(&g_app, &g_view);
			else
				ma_draw(&g_app, &g_view);
		}
		wind_get(g_win, WF_NEXTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
	}
	vs_clip(g_gc.vdi, 0, 0);
	g_gc.clip_on = 0;
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

static void refresh_status(void *shell)
{
	(void)shell;
	graf_mouse(BUSYBEE, 0);
	repaint(0, 1);
}

static int key_of(short kr, short ks)
{
	int scan = (kr >> 8) & 0xFF, ascii = kr & 0xFF, shift = ks & (K_LSHIFT | K_RSHIFT);

	switch (scan) {
	case 0x48: return shift ? MA_K_PGUP : MA_K_UP;
	case 0x50: return shift ? MA_K_PGDN : MA_K_DOWN;
	case 0x47: return shift ? MA_K_END : MA_K_HOME;   /* Clr/Home */
	case 0x1C: case 0x72: return MA_K_ENTER;          /* Return, keypad Enter */
	case 0x01: return MA_K_ESC;
	default:   return ascii;
	}
}

static int open_window(void)
{
	short dx, dy, dw, dh, wx, wy, ww, wh;
	static char title[] = " OpenUA Module Manager ";

	wind_get(0, WF_WORKXYWH, &dx, &dy, &dw, &dh);
	g_win = wind_create(PARTS, dx, dy, dw, dh);
	if (g_win < 0)
		return 0;
	wind_set_str(g_win, WF_NAME, title);
	/* the whole desktop, less a margin a mono or medium screen can spare */
	wx = dx + 4;
	wy = dy + 4;
	ww = dw - 8;
	wh = dh - 8;
	wind_open(g_win, wx, wy, ww, wh);
	work_area();
	return 1;
}

static int setup_view(void)
{
	short work_in[11] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2 }, work_out[57];
	short cw, ch, bw, bh;
	plat_font_metrics fm;
	i32 w_twips = 0;

	g_gc.vdi = graf_handle(&cw, &ch, &bw, &bh);
	v_opnvwk(work_in, &g_gc.vdi, work_out);
	if (g_gc.vdi <= 0)
		return 0;
	if (plat_font_open((const u8 *)"", 0, 200, PLAT_FAMILY_MODERN << PLAT_FAMILY_SHIFT,
	                   &g_view.font) != WP_OK)
		return 0;
	/* The AES's own character cell: 8x8 on a 200-line screen, 8x16 on a
	 * mono or larger one. The system font is chosen to match it (vst_point
	 * 9 is the 8x8 face, 10 the 8x16), rather than by point size, which on
	 * a low-resolution ST picks the 8x16 face and leaves 40 columns of
	 * enormous type. */
	((atari_font *)g_view.font)->point_size = ch <= 8 ? 9 : 10;
	g_view.cw = cw;
	g_view.lh = ch;
	g_view.ascent = ch - ch / 4;
	(void)fm;
	(void)w_twips;
	g_view.gc = (plat_gc *)&g_gc;
	g_view.refresh_status = refresh_status;
	return 1;
}

int main(void)
{
	short msg[8], mx, my, mb, ks, kr, br, ev;
	int run = 1;

	if (appl_init() < 0)
		return 1;
	graf_mouse(BUSYBEE, 0);
	ma_init(&g_app, "");
	if (!setup_view() || !open_window()) {
		form_alert(1, "[3][The module manager could not|open its window.][ Quit ]");
		appl_exit();
		return 1;
	}
	g_app.view = &g_view;
	graf_mouse(ARROW, 0);
	repaint(0, 0);

	while (run) {
		int r = MA_NOTHING;

		ev = evnt_multi(MU_MESAG | MU_KEYBD | MU_BUTTON, 2, 1, 1,
		                0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		                msg, 0, &mx, &my, &mb, &ks, &kr, &br);
		if (ev & MU_MESAG) {
			GRECT d;

			switch (msg[0]) {
			case WM_REDRAW:
				d.g_x = msg[4];
				d.g_y = msg[5];
				d.g_w = msg[6];
				d.g_h = msg[7];
				repaint(&d, 0);
				break;
			case WM_TOPPED:
				wind_set(g_win, WF_TOP, 0, 0, 0, 0);
				break;
			case WM_CLOSED:
				run = 0;
				break;
			case WM_MOVED:
			case WM_SIZED:
				wind_set(g_win, WF_CURRXYWH, msg[4], msg[5],
				         msg[6] < 200 ? 200 : msg[6], msg[7] < 120 ? 120 : msg[7]);
				work_area();
				r = MA_REDRAW;
				break;
			case WM_FULLED: {
				short x, y, w, h;

				wind_get(g_win, WF_FULLXYWH, &x, &y, &w, &h);
				wind_set(g_win, WF_CURRXYWH, x, y, w, h);
				work_area();
				r = MA_REDRAW;
				break;
			}
			default:
				break;
			}
		}
		if (ev & MU_KEYBD)
			r = ma_key(&g_app, key_of(kr, ks));
		if ((ev & MU_BUTTON) && wind_find(mx, my) == g_win)
			r = ma_click(&g_app, mx, my, br >= 2);
		if (r == MA_QUIT)
			run = 0;
		else if (r == MA_REDRAW) {
			graf_mouse(ARROW, 0);
			repaint(0, 0);
		}
	}
	wind_close(g_win);
	wind_delete(g_win);
	v_clsvwk(g_gc.vdi);
	ma_free(&g_app);
	appl_exit();
	return 0;
}
