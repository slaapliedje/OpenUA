/* modnet — fetching modules: a small HTTP/1.0 client and the archive's list.
 *
 * Over retroplat's byte transport (plat_transport_*, "tcp:host:port"), so the
 * same code runs on the host for the tests, and on the Amiga and the Atari
 * once their transports speak TCP. HTTP/1.0 only, and plain http:// only: the
 * fan archive serves plain HTTP, and a 68000 has no business doing TLS.
 *
 * The archive's list is frua.rosedragon.org/modulelist/export.php: an HTML
 * page wrapping a <pre> of one line per module,
 *
 *   path|file|title|size KB|author|levels|equipment|Hacked or Not Hacked|n|date|about
 *
 * (`file` is cut short by the archive's database; `path` is the one to use.)
 * The same list Gold Box Companion reads.
 */
#ifndef MODNET_H
#define MODNET_H

#define MN_ARCHIVE_HOST  "frua.rosedragon.org"
#define MN_ARCHIVE_LIST  "http://frua.rosedragon.org/modulelist/export.php"

/* Receives the body as it arrives; return 0 to stop. */
typedef int (*mn_sink)(void *ctx, const unsigned char *data, unsigned long n);
/* Called as bytes arrive; total is -1 when the server did not say. */
typedef void (*mn_progress)(void *ctx, unsigned long got, long total);

/* GET `url` (http://host[:port]/path), following up to three redirects.
 * Returns 1 when the whole body arrived (as many bytes as Content-Length
 * said, when it said), else 0 with `err` set. */
int mn_http_get(const char *url, mn_sink sink, mn_progress progress, void *ctx,
                char *err, unsigned errcap);

/* A sink that grows a malloc'd buffer (cap 0 = no limit). */
typedef struct mn_buf {
	unsigned char *data;
	unsigned long  len, room, cap;
} mn_buf;
int mn_buf_sink(void *mn_buf_ptr, const unsigned char *data, unsigned long n);

typedef struct mn_module {
	const char *path;           /* "pc/modules/c/curse.zip"                */
	const char *title;
	const char *author;
	const char *levels;         /* "2-4", "Pregen", ...                    */
	const char *equipment;      /* "Average", "Pregen", "+1", ...          */
	const char *date;           /* "1993-07-27"                            */
	const char *about;
	long        size_kb;
	int         hacked;         /* replaces base-game art                  */
} mn_module;

/* Parse the export page IN PLACE (the strings point into `text`, which is
 * changed and must outlive the list). Returns the number of modules, up to
 * `max`, written to `out`; -1 when this is not the export page. */
int mn_export_parse(char *text, unsigned long len, mn_module *out, int max);

/* "http://frua.rosedragon.org/<path>" for a module. */
void mn_module_url(const mn_module *m, char *out, unsigned cap);

#endif
