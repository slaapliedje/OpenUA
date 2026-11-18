/* modapp — the module manager's screen, the same on every machine.
 *
 * What the window shows and what keys and clicks do. Drawn through
 * retroplat (plat_fill_rect / plat_draw_text) into whatever plat_gc the
 * machine's shell hands over; the shell owns the window and the event loop
 * (modapp_gem.c on the Atari, modapp_amiga.c on the Amiga, a headless
 * driver for the tests) and turns its events into ma_key / ma_click.
 *
 *   OpenUA Module Manager - 653 modules              row 0: title
 *   The Curse of the Fire Dragon   Someone  1568K H  the list (selected inverted)
 *   ...
 *   ----------------------------------------------   details of the selected one
 *   Levels 2-4  Equipment Average  1993-07-27  replaces art
 *   A description, wrapped ...
 *   [R]efresh list  [Return] Install  [Q]uit          keys
 *   status / progress line
 *
 * The list is the fan archive's export page, kept as MODLIST.TXT in the
 * OpenUA folder so the manager works without a network once fetched.
 * Downloads land in MODDL.ZIP and go through mi_install().
 */
#ifndef MODAPP_H
#define MODAPP_H

#include "platform.h"
#include "modnet.h"

#define MA_MAXMOD 1500

/* Key codes beyond ASCII. */
enum { MA_K_UP = 0x101, MA_K_DOWN, MA_K_PGUP, MA_K_PGDN, MA_K_HOME, MA_K_END,
       MA_K_ENTER, MA_K_ESC };

/* What the shell should do after an event. */
enum { MA_NOTHING = 0, MA_REDRAW, MA_QUIT };

typedef struct ma_view {
	plat_gc   *gc;
	plat_font *font;
	int x, y, w, h;             /* the window's work area, in pixels        */
	int cw, lh, ascent;         /* a character cell: width, line, baseline  */
	/* Called while a download or install runs, to show the status line at
	 * once (the shell clips to the work area and calls ma_draw_status). */
	void (*refresh_status)(void *shell);
	void *shell;
} ma_view;

typedef struct ma_app {
	char        dir[256];       /* the OpenUA folder ("" = current)         */
	char       *list_text;      /* MODLIST.TXT, parsed in place             */
	mn_module   mod[MA_MAXMOD];
	int         nmod, sel, top;
	char        installed[64][13];  /* *.DSN folders here, upper case       */
	int         ninstalled;
	char        status[120];
	ma_view    *view;
} ma_app;

void ma_init(ma_app *a, const char *openua_dir);
void ma_free(ma_app *a);

void ma_draw(ma_app *a, ma_view *v);          /* everything               */
void ma_draw_status(ma_app *a, ma_view *v);   /* just the bottom line     */

int  ma_key(ma_app *a, int key);              /* MA_NOTHING/REDRAW/QUIT   */
int  ma_click(ma_app *a, int x, int y, int twice);

/* The actions behind the keys, for a shell's menu or a test. */
int  ma_refresh_list(ma_app *a);              /* 1 when the list arrived  */
int  ma_install_selected(ma_app *a);          /* 1 when it is installed   */

/* Where the list and downloads come from; a test points it elsewhere. */
extern const char *ma_list_url;
extern const char *ma_file_base;              /* "http://frua.rosedragon.org/" */

#endif
