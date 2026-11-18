/* modapp — see modapp.h. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "platform.h"
#include "modnet.h"
#include "modinst.h"
#include "modapp.h"

#ifdef __MINT__
#define SEP '\\'
#else
#define SEP '/'
#endif

const char *ma_list_url  = MN_ARCHIVE_LIST;
const char *ma_file_base = "http://" MN_ARCHIVE_HOST "/";

#define LIST_FILE  "MODLIST.TXT"
#define DL_FILE    "MODDL.ZIP"
#define DONE_FILE  "MODINST.TXT"     /* "path|DESIGN.DSN" per install       */
#define DETAIL_ROWS 4

static void path_in(const ma_app *a, const char *leaf, char *out, size_t cap)
{
	size_t n = strlen(a->dir);

	if (n == 0)
		snprintf(out, cap, "%s", leaf);
	else if (a->dir[n - 1] == SEP || a->dir[n - 1] == ':')
		snprintf(out, cap, "%s%s", a->dir, leaf);
	else
		snprintf(out, cap, "%s%c%s", a->dir, SEP, leaf);
}

static void set_status(ma_app *a, const char *s)
{
	snprintf(a->status, sizeof a->status, "%s", s);
	if (a->view && a->view->refresh_status)
		a->view->refresh_status(a->view->shell);
}

/* ------------------------------------------------------------ state */

static void scan_installed(ma_app *a)
{
	DIR *d = opendir(a->dir[0] ? a->dir : ".");
	struct dirent *e;

	a->ninstalled = 0;
	if (!d)
		return;
	while ((e = readdir(d)) != 0 && a->ninstalled < 64) {
		size_t n = strlen(e->d_name), k;

		if (n < 5 || n > 12)
			continue;
		for (k = 0; k < n; k++)
			a->installed[a->ninstalled][k] = (char)toupper((unsigned char)e->d_name[k]);
		a->installed[a->ninstalled][n] = 0;
		if (strcmp(a->installed[a->ninstalled] + n - 4, ".DSN") == 0)
			a->ninstalled++;
	}
	closedir(d);
}

static int folder_installed(const ma_app *a, const char *folder)
{
	int i;

	for (i = 0; i < a->ninstalled; i++)
		if (strcmp(a->installed[i], folder) == 0)
			return 1;
	return 0;
}

/* Is the module installed? By the record kept at install time, else by
 * its archive's name (most fan modules use the same eight letters). */
static int is_installed(const ma_app *a, const mn_module *m)
{
	char path[300], line[300], stem[13];
	const char *base = strrchr(m->path, '/');
	FILE *f;
	size_t k = 0;

	path_in(a, DONE_FILE, path, sizeof path);
	if ((f = fopen(path, "r")) != 0) {
		size_t n = strlen(m->path);

		while (fgets(line, sizeof line, f))
			if (strncmp(line, m->path, n) == 0 && line[n] == '|') {
				char *nl = strpbrk(line + n + 1, "\r\n");

				if (nl)
					*nl = 0;
				fclose(f);
				return folder_installed(a, line + n + 1);
			}
		fclose(f);
	}
	base = base ? base + 1 : m->path;
	while (base[k] && base[k] != '.' && k < 8) {
		stem[k] = (char)toupper((unsigned char)base[k]);
		k++;
	}
	strcpy(stem + k, ".DSN");
	return folder_installed(a, stem);
}

static int load_list(ma_app *a)
{
	char path[300];
	FILE *f;
	long n;

	path_in(a, LIST_FILE, path, sizeof path);
	free(a->list_text);
	a->list_text = 0;
	a->nmod = a->sel = a->top = 0;
	if ((f = fopen(path, "rb")) == 0)
		return 0;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	a->list_text = malloc((size_t)n + 1);
	if (!a->list_text || fread(a->list_text, 1, (size_t)n, f) != (size_t)n) {
		fclose(f);
		free(a->list_text);
		a->list_text = 0;
		return 0;
	}
	fclose(f);
	a->nmod = mn_export_parse(a->list_text, (unsigned long)n, a->mod, MA_MAXMOD);
	if (a->nmod < 0)
		a->nmod = 0;
	return a->nmod > 0;
}

void ma_init(ma_app *a, const char *openua_dir)
{
	memset(a, 0, sizeof *a);
	snprintf(a->dir, sizeof a->dir, "%s", openua_dir ? openua_dir : "");
	scan_installed(a);
	if (load_list(a))
		snprintf(a->status, sizeof a->status,
		         "%d modules. R fetches a fresh list.", a->nmod);
	else
		snprintf(a->status, sizeof a->status,
		         "No module list yet: press R to fetch it from the archive.");
}

void ma_free(ma_app *a)
{
	free(a->list_text);
	a->list_text = 0;
}

/* ------------------------------------------------------------ fetching */

typedef struct file_sink {
	FILE         *f;
	ma_app       *a;
	const char   *what;
	unsigned long last;
} file_sink;

static int to_file(void *p, const unsigned char *d, unsigned long n)
{
	file_sink *s = p;

	return fwrite(d, 1, n, s->f) == n;
}

static void show_progress(void *p, unsigned long got, long total)
{
	file_sink *s = p;
	char msg[120];

	if (got - s->last < 16384 && !(total >= 0 && (long)got >= total))
		return;
	s->last = got;
	if (total > 0)
		snprintf(msg, sizeof msg, "%s: %luK of %ldK", s->what, got / 1024, total / 1024);
	else
		snprintf(msg, sizeof msg, "%s: %luK", s->what, got / 1024);
	set_status(s->a, msg);
}

/* Fetch `url` into the file `leaf` in the OpenUA folder. */
static int fetch_to(ma_app *a, const char *url, const char *leaf, const char *what)
{
	char path[300], tmp[300], err[200], msg[160];
	file_sink s;
	int ok;

	path_in(a, leaf, path, sizeof path);
	snprintf(tmp, sizeof tmp, "%s", path);
	tmp[strlen(tmp) - 1] = '_';         /* MODLIST.TX_: the old one stays until done */
	s.f = fopen(tmp, "wb");
	s.a = a;
	s.what = what;
	s.last = 0;
	if (!s.f) {
		set_status(a, "Cannot write in the OpenUA folder.");
		return 0;
	}
	set_status(a, what);
	ok = mn_http_get(url, to_file, show_progress, &s, err, sizeof err);
	if (fclose(s.f) != 0)
		ok = 0;
	if (!ok) {
		remove(tmp);
		snprintf(msg, sizeof msg, "%s failed: %s", what, err[0] ? err : "disk full?");
		set_status(a, msg);
		return 0;
	}
	remove(path);
	if (rename(tmp, path) != 0) {
		set_status(a, "Cannot rename the download.");
		return 0;
	}
	return 1;
}

int ma_refresh_list(ma_app *a)
{
	char msg[120];

	if (!fetch_to(a, ma_list_url, LIST_FILE, "Fetching the module list"))
		return 0;
	if (!load_list(a)) {
		set_status(a, "The archive sent something that is not its module list.");
		return 0;
	}
	scan_installed(a);
	snprintf(msg, sizeof msg, "%d modules in the archive's list.", a->nmod);
	set_status(a, msg);
	return 1;
}

static void install_log(void *p, const char *line)
{
	ma_app *a = p;

	if (line[0])
		set_status(a, line[0] == ' ' ? line + 2 : line);
}

int ma_install_selected(ma_app *a)
{
	mn_module *m;
	char url[400], zip[300], dsn[16], msg[160], rec[300];
	FILE *f;
	int rc;

	if (a->nmod == 0)
		return 0;
	m = &a->mod[a->sel];
	snprintf(url, sizeof url, "%s%s", ma_file_base, m->path);
	snprintf(msg, sizeof msg, "Downloading %s", m->title);
	if (!fetch_to(a, url, DL_FILE, msg))
		return 0;
	path_in(a, DL_FILE, zip, sizeof zip);
	rc = mi_install(zip, a->dir[0] ? a->dir : ".", install_log, a, dsn, sizeof dsn);
	remove(zip);
	scan_installed(a);
	if (rc != 0)
		return 0;                       /* its last line says why */
	path_in(a, DONE_FILE, rec, sizeof rec);
	if ((f = fopen(rec, "a")) != 0) {
		fprintf(f, "%s|%s\n", m->path, dsn);
		fclose(f);
	}
	snprintf(msg, sizeof msg, "%s is installed as %s.", m->title, dsn);
	set_status(a, msg);
	return 1;
}

/* ------------------------------------------------------------ drawing */

static int rows_total(const ma_view *v) { return v->h / v->lh; }
static int list_rows(const ma_view *v)
{
	int n = rows_total(v) - 1 - (DETAIL_ROWS + 1) - 2;
	return n > 1 ? n : 1;
}
static int cols(const ma_view *v) { return v->w / v->cw; }

static void text_at(const ma_view *v, int col, int row, const char *s, int maxc, int inverse)
{
	int n = (int)strlen(s);
	u8 c = inverse ? 255 : 0;

	if (maxc >= 0 && n > maxc)
		n = maxc;
	if (n <= 0)
		return;
	plat_draw_text(v->gc, v->font, v->x + col * v->cw, v->y + row * v->lh + v->ascent,
	               (const u8 *)s, (u32)n, c, c, c);
}

static void band(const ma_view *v, int row, int inverse)
{
	u8 c = inverse ? 0 : 255;

	plat_fill_rect(v->gc, v->x, v->y + row * v->lh, v->w, v->lh, c, c, c);
}

static void draw_row(ma_app *a, const ma_view *v, int i, int row)
{
	const mn_module *m = &a->mod[i];
	int c = cols(v), inv = i == a->sel, tw = c - 22;
	char tail[40];

	band(v, row, inv);
	if (tw < 10)
		tw = c - 2;
	text_at(v, 1, row, m->title, tw - 1, inv);
	if (c - tw >= 22) {
		snprintf(tail, sizeof tail, "%-12.12s %5ldK %c%c", m->author, m->size_kb,
		         m->hacked ? 'H' : ' ', is_installed(a, m) ? 'I' : ' ');
		text_at(v, tw, row, tail, -1, inv);
	}
}

/* Word-wrap `s` into `rows` lines of `width` columns from `row`. */
static void wrap(const ma_view *v, int row, int rows, int width, const char *s)
{
	char line[200];

	while (*s && rows-- > 0) {
		int n = (int)strlen(s), cut = n;

		if (width > (int)sizeof line - 1)
			width = (int)sizeof line - 1;
		if (n > width) {
			cut = width;
			while (cut > 0 && s[cut] != ' ')
				cut--;
			if (cut == 0)
				cut = width;
		}
		memcpy(line, s, (size_t)cut);
		line[cut] = 0;
		text_at(v, 1, row++, line, -1, 0);
		s += cut;
		while (*s == ' ')
			s++;
	}
}

void ma_draw_status(ma_app *a, ma_view *v)
{
	int row = rows_total(v) - 1;

	band(v, row, 0);
	text_at(v, 1, row, a->status, cols(v) - 2, 0);
}

void ma_draw(ma_app *a, ma_view *v)
{
	int lr = list_rows(v), r, c = cols(v), drow;
	char t[120];

	a->view = v;
	plat_fill_rect(v->gc, v->x, v->y, v->w, v->h, 255, 255, 255);
	band(v, 0, 1);
	snprintf(t, sizeof t, "OpenUA Module Manager - %d modules, %d installed here",
	         a->nmod, a->ninstalled);
	text_at(v, 1, 0, t, c - 2, 1);
	if (a->sel < a->top)
		a->top = a->sel;
	if (a->sel >= a->top + lr)
		a->top = a->sel - lr + 1;
	for (r = 0; r < lr && a->top + r < a->nmod; r++)
		draw_row(a, v, a->top + r, 1 + r);

	drow = 1 + lr;
	plat_fill_rect(v->gc, v->x, v->y + drow * v->lh + v->lh / 2, v->w, 1, 0, 0, 0);
	if (a->nmod > 0) {
		const mn_module *m = &a->mod[a->sel];

		snprintf(t, sizeof t, "Levels %s  Equipment %s  %s%s%s", m->levels,
		         m->equipment, m->date, m->hacked ? "  replaces art" : "",
		         is_installed(a, m) ? "  INSTALLED" : "");
		text_at(v, 1, drow + 1, t, c - 2, 0);
		wrap(v, drow + 2, DETAIL_ROWS - 1, c - 2, m->about);
	}
	text_at(v, 1, rows_total(v) - 2,
	        "[R]efresh list  [Return] Install  [Up/Down] Choose  [Q]uit", c - 2, 0);
	ma_draw_status(a, v);
}

/* ------------------------------------------------------------ input */

int ma_key(ma_app *a, int key)
{
	int lr = a->view ? list_rows(a->view) : 10, old = a->sel;

	switch (key) {
	case MA_K_UP:   a->sel--; break;
	case MA_K_DOWN: a->sel++; break;
	case MA_K_PGUP: a->sel -= lr; break;
	case MA_K_PGDN: a->sel += lr; break;
	case MA_K_HOME: a->sel = 0; break;
	case MA_K_END:  a->sel = a->nmod - 1; break;
	case MA_K_ESC: case 'q': case 'Q':
		return MA_QUIT;
	case 'r': case 'R':
		ma_refresh_list(a);
		return MA_REDRAW;
	case MA_K_ENTER: case '\r': case '\n':
		ma_install_selected(a);
		return MA_REDRAW;
	default:
		if (key < 0x100 && isalnum(key) && a->nmod > 0) {
			/* the next title starting with that letter */
			int i, k = toupper(key);

			for (i = 1; i <= a->nmod; i++) {
				int j = (a->sel + i) % a->nmod;

				if (toupper((unsigned char)a->mod[j].title[0]) == k) {
					a->sel = j;
					break;
				}
			}
		}
		break;
	}
	if (a->sel >= a->nmod)
		a->sel = a->nmod - 1;
	if (a->sel < 0)
		a->sel = 0;
	return a->sel != old ? MA_REDRAW : MA_NOTHING;
}

int ma_click(ma_app *a, int x, int y, int twice)
{
	const ma_view *v = a->view;
	int row, lr;

	if (!v)
		return MA_NOTHING;
	row = (y - v->y) / v->lh;
	lr = list_rows(v);
	(void)x;
	if (row >= 1 && row <= lr && a->top + row - 1 < a->nmod) {
		a->sel = a->top + row - 1;
		if (twice)
			ma_install_selected(a);
		return MA_REDRAW;
	}
	if (row == rows_total(v) - 2) {    /* the keys line: R / Return / Q zones */
		int col = (x - v->x) / v->cw;

		if (col < 16)
			return ma_key(a, 'R');
		if (col < 34)
			return ma_key(a, MA_K_ENTER);
		if (col >= 55)
			return MA_QUIT;
	}
	return MA_NOTHING;
}
