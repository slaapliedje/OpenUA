/* Host driver for installer/modnet.c (tests/test_modnet.py).
 *
 *   modnet_host get  URL OUTFILE     fetch a file
 *   modnet_host list URL             fetch and parse the archive's list
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "modnet.h"

int main(int argc, char **argv)
{
	mn_buf b;
	char err[200];

	memset(&b, 0, sizeof b);
	if (argc == 4 && strcmp(argv[1], "get") == 0) {
		FILE *f;

		if (!mn_http_get(argv[2], mn_buf_sink, 0, &b, err, sizeof err)) {
			printf("ERR %s\n", err);
			return 1;
		}
		f = fopen(argv[3], "wb");
		if (!f || fwrite(b.data, 1, b.len, f) != b.len)
			return 2;
		fclose(f);
		printf("OK %lu\n", b.len);
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "list") == 0) {
		static mn_module m[2000];
		int n, i;

		if (!mn_http_get(argv[2], mn_buf_sink, 0, &b, err, sizeof err)) {
			printf("ERR %s\n", err);
			return 1;
		}
		n = mn_export_parse((char *)b.data, b.len, m, 2000);
		printf("COUNT %d\n", n);
		for (i = 0; i < n; i++) {
			char url[300];

			mn_module_url(&m[i], url, sizeof url);
			printf("M %s|%s|%s|%ld|%s|%s|%d|%s|%s\n", url, m[i].title, m[i].author,
			       m[i].size_kb, m[i].levels, m[i].equipment, m[i].hacked,
			       m[i].date, m[i].about);
		}
		return 0;
	}
	return 2;
}
