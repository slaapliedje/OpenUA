/* Headless shell for installer/modapp.c (tests/test_modapp.py).
 *
 *   modapp_host OPENUA_DIR LIST_URL FILE_BASE key...
 *
 * Draws into retroplat's host backend (which records the text it is given),
 * feeds the keys, and prints the status line and the drawn text after each
 * one. A key is a single character, or up / down / pgup / pgdn / home /
 * end / enter / esc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "draw_host.h"
#include "modapp.h"

static ma_app g_app;

static void dump(ma_view *v)
{
	u32 i, n;

	draw_host_reset_text();
	ma_draw(&g_app, v);
	printf("STATUS %s\n", g_app.status);
	printf("SEL %d\n", g_app.sel);
	n = draw_host_text_count();
	for (i = 0; i < n; i++) {
		i32 x, y;
		const char *t = draw_host_text_at(i, &x, &y);

		if (t)
			printf("T %d %s\n", (int)((y - v->ascent) / v->lh), t);
	}
	printf("--\n");
}

int main(int argc, char **argv)
{
	ma_view v;
	int i;

	if (argc < 4)
		return 2;
	ma_list_url = argv[2];
	ma_file_base = argv[3];
	memset(&v, 0, sizeof v);
	v.w = 640;
	v.h = 400;
	v.cw = 8;
	v.lh = 16;
	v.ascent = 12;
	if (plat_font_open((const u8 *)"", 0, 240, 0, &v.font) != WP_OK)
		return 3;
	ma_init(&g_app, argv[1]);
	dump(&v);
	for (i = 4; i < argc; i++) {
		const char *k = argv[i];
		int key = k[0];

		if (!strcmp(k, "up")) key = MA_K_UP;
		else if (!strcmp(k, "down")) key = MA_K_DOWN;
		else if (!strcmp(k, "pgup")) key = MA_K_PGUP;
		else if (!strcmp(k, "pgdn")) key = MA_K_PGDN;
		else if (!strcmp(k, "home")) key = MA_K_HOME;
		else if (!strcmp(k, "end")) key = MA_K_END;
		else if (!strcmp(k, "enter")) key = MA_K_ENTER;
		else if (!strcmp(k, "esc")) key = MA_K_ESC;
		printf("KEY %s -> %d\n", k, ma_key(&g_app, key));
		dump(&v);
	}
	ma_free(&g_app);
	return 0;
}
