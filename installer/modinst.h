/* modinst — install one module archive into an OpenUA folder (modinst.c). */
#ifndef MODINST_H
#define MODINST_H

/* One line of progress, without its newline. */
typedef void (*mi_log)(void *ctx, const char *line);

/* Install `zip_path` into `dest_dir`. The design's folder name ("CURSE.DSN")
 * goes to `dsn_out` when given. Returns 0 when it is installed (an art or
 * music file that would not convert is reported but does not count against
 * it), 1 when nothing was. */
int mi_install(const char *zip_path, const char *dest_dir, mi_log log, void *ctx,
               char *dsn_out, unsigned dsn_cap);

#endif
