/*
 * snprintf / vsnprintf for SVR4.0, which has only the unbounded forms.
 *
 * Formatted into a scratch buffer with the C library's vsprintf, then
 * copied truncated: the engine's formats are short (menu text, status
 * lines, error messages), so the scratch is sized far beyond them. A
 * va_list is a plain pointer to the stacked arguments in both the GCC and
 * the SVR4 ABI, so it passes straight through.
 */
#ifdef FRUA_UNIX

#include <stdio.h>
#include <string.h>

#define SCRATCH 4096

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
	static char tmp[SCRATCH];
	int len = vsprintf(tmp, fmt, ap);

	if (len < 0)
		len = 0;
	if (n > 0) {
		size_t k = (size_t)len < n - 1 ? (size_t)len : n - 1;
		memcpy(buf, tmp, k);
		buf[k] = '\0';
	}
	return len;
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(buf, n, fmt, ap);
	va_end(ap);
	return len;
}

#endif /* FRUA_UNIX */
