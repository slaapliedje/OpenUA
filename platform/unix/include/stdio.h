/*
 * stdio.h — AMIX's, plus snprintf/vsnprintf, which SVR4.0 predates and the
 * engine uses (platform/unix/snprintf.c). toolchain/sysv4-cc puts this
 * directory ahead of the system headers.
 */
#ifndef OPENUA_UNIX_STDIO_H
#define OPENUA_UNIX_STDIO_H

#include_next <stdio.h>
#include <stdarg.h>
#include <stddef.h>

int snprintf(char *buf, size_t n, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);

#endif /* OPENUA_UNIX_STDIO_H */
