/*
 * Engine error reporting.
 *
 * Lifted (in spirit) from CODE 5, jump-table entry 1084. See error.c.
 */

#ifndef ENGINE_ERROR_H
#define ENGINE_ERROR_H

/* Report a printf-style error message. */
void ua_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* ENGINE_ERROR_H */
