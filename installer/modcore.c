/* modcore — see modcore.h. The rules are tools/modman/core.py's, restated. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MINIZ_HEADER_FILE_ONLY
#include "miniz.c"
#include "modcore.h"

/* ------------------------------------------------------------ what it is */

int mc_kind(const unsigned char *head, unsigned long n)
{
	static const char *const web[] = { "<!doctype", "<html", "<?xml", "<head" };
	unsigned long i = 0;
	unsigned k;

	while (i < n && i < 64 && isspace(head[i]))
		i++;
	for (k = 0; k < sizeof web / sizeof web[0]; k++) {
		unsigned long m = strlen(web[k]), j;

		for (j = 0; j < m && i + j < n; j++)
			if (tolower(head[i + j]) != web[k][j])
				break;
		if (j == m)
			return MC_WEBPAGE;
	}
	if (n >= 4 && head[0] == 'P' && head[1] == 'K'
	 && ((head[2] == 3 && head[3] == 4) || (head[2] == 5 && head[3] == 6)))
		return MC_ZIP;
	if ((n >= 4 && memcmp(head, "SIT!", 4) == 0)
	 || (n >= 8 && memcmp(head, "StuffIt ", 8) == 0))
		return MC_STUFFIT;
	if (n >= 5 && head[2] == '-' && head[3] == 'l'
	 && (head[4] == 'h' || head[4] == 'z'))
		return MC_LHA;
	return MC_UNKNOWN;
}

const char *mc_kind_text(int kind)
{
	switch (kind) {
	case MC_ZIP:     return "a zip archive";
	case MC_WEBPAGE: return "a web page, not an archive (the download failed "
	                        "or the link has moved)";
	case MC_STUFFIT: return "a StuffIt archive; unpack it on another machine first";
	case MC_LHA:     return "an LHA archive; unpack it with lha first";
	default:         return "not an archive this can read";
	}
}

/* ------------------------------------------------------------ names */

static int ok83(int c)
{
	return isupper(c) || isdigit(c) || (c && strchr("_$~!#%&'(){}@^`-", c));
}

int mc_dos83(const char *name, char out[13])
{
	char up[64];
	const char *dot;
	size_t n = strlen(name), b, e, i;

	if (n == 0 || n >= sizeof up)
		return 0;
	for (i = 0; i <= n; i++)
		up[i] = (char)toupper((unsigned char)name[i]);
	dot = strrchr(up, '.');
	b = dot ? (size_t)(dot - up) : n;
	e = dot ? n - b - 1 : 0;
	if (b >= 1 && b <= 8 && e <= 3 && (!dot || e >= 1)) {
		int good = 1;

		for (i = 0; i < n && good; i++)
			if ((size_t)i != b)
				good = ok83((unsigned char)up[i]);
		if (good) {
			memcpy(out, up, n + 1);
			return 1;
		}
	}
	/* a Mac long art name: four characters, letters, a digit and the id */
	if (dot && e == 3 && (!strcmp(dot, ".CTL") || !strcmp(dot, ".TLB"))
	 && b >= 8 && b < 32) {
		size_t j;

		for (i = 0; i < 4; i++)
			if (!isupper((unsigned char)up[i]) && !isdigit((unsigned char)up[i]))
				return 0;
		for (j = 4; j < b - 4; j++)
			if (!isupper((unsigned char)up[j]))
				return 0;
		for (j = b - 4; j < b; j++)
			if (!isdigit((unsigned char)up[j]))
				return 0;
		memcpy(out, up, 4);
		memcpy(out + 4, up + b - 4, 4);
		memcpy(out + 8, dot, 5);
		return 1;
	}
	return 0;
}

/* ------------------------------------------------------------ explode */

typedef struct sf_tree {
	unsigned short code[256];       /* read LSB first                        */
	unsigned char  len[256];
	unsigned char  value[256];      /* entries sorted by length              */
	short          first[18];       /* first entry of each length            */
} sf_tree;

typedef struct bits {
	const unsigned char *p, *end;
	unsigned long acc;
	int n;
} bits;

static unsigned getbits(bits *b, int count)
{
	unsigned v;

	while (b->n < count) {
		b->acc |= (unsigned long)(b->p < b->end ? *b->p : 0) << b->n;
		b->p++;
		b->n += 8;
	}
	v = (unsigned)(b->acc & ((1UL << count) - 1));
	b->acc >>= count;
	b->n -= count;
	return v;
}

/* A Shannon-Fano table from its packed description. Returns bytes used, 0
 * when the description is damaged. */
static unsigned long sf_build(const unsigned char *in, unsigned long inlen,
                              int count, sf_tree *t)
{
	unsigned char lens[256];
	unsigned long nbytes, i;
	int k = 0, L, j, order[256], nord = 0;
	unsigned code = 0, inc = 0, last = 0;

	if (inlen < 1)
		return 0;
	nbytes = (unsigned long)in[0] + 1;
	if (nbytes + 1 > inlen)
		return 0;
	for (i = 1; i <= nbytes; i++) {
		int reps = (in[i] >> 4) + 1, bl = (in[i] & 15) + 1;

		while (reps--) {
			if (k >= count)
				return 0;
			lens[k++] = (unsigned char)bl;
		}
	}
	if (k != count)
		return 0;
	for (L = 1; L <= 16; L++)            /* sorted by (length, value) */
		for (j = 0; j < count; j++)
			if (lens[j] == L)
				order[nord++] = j;
	for (j = count - 1; j >= 0; j--) {
		int v = order[j], b;
		unsigned rev = 0;

		code = (code + inc) & 0xFFFF;
		if (lens[v] != last) {
			last = lens[v];
			inc = 1u << (16 - last);
		}
		for (b = 0; b < 16; b++)         /* MSB first -> LSB first */
			if (code & (1u << b))
				rev |= 1u << (15 - b);
		t->code[j] = (unsigned short)(rev & ((1u << lens[v]) - 1));
		t->len[j] = lens[v];
		t->value[j] = (unsigned char)v;
	}
	for (L = 0; L < 18; L++)
		t->first[L] = (short)count;
	for (j = count - 1; j >= 0; j--)
		t->first[t->len[j]] = (short)j;
	t->first[17] = (short)count;
	for (L = 16; L >= 1; L--)            /* lengths with no codes */
		if (t->first[L] == count)
			t->first[L] = t->first[L + 1];
	return nbytes + 1;
}

static int sf_read(bits *b, const sf_tree *t, int count)
{
	unsigned v = 0;
	int n, j;

	for (n = 1; n <= 16; n++) {
		v |= getbits(b, 1) << (n - 1);
		for (j = t->first[n]; j < count && t->len[j] == n; j++)
			if (t->code[j] == v)
				return t->value[j];
	}
	return -1;
}

long mc_explode(const unsigned char *in, unsigned long inlen,
                unsigned char *out, unsigned long outlen, unsigned flags)
{
	int big = (flags & 2) != 0, literals = (flags & 4) != 0;
	sf_tree *lit = 0, *lent, *dist;
	unsigned long pos = 0, o = 0, u;
	long result = -1;
	bits b;
	int low = big ? 7 : 6, minimum = literals ? 3 : 2;

	lent = malloc(sizeof *lent);
	dist = malloc(sizeof *dist);
	if (literals)
		lit = malloc(sizeof *lit);
	if (!lent || !dist || (literals && !lit))
		goto done;
	if (literals) {
		if (!(u = sf_build(in, inlen, 256, lit)))
			goto done;
		pos += u;
	}
	if (!(u = sf_build(in + pos, inlen - pos, 64, lent)))
		goto done;
	pos += u;
	if (!(u = sf_build(in + pos, inlen - pos, 64, dist)))
		goto done;
	pos += u;
	b.p = in + pos;
	b.end = in + inlen;
	b.acc = 0;
	b.n = 0;
	while (o < outlen) {
		if (getbits(&b, 1)) {
			int c = literals ? sf_read(&b, lit, 256) : (int)getbits(&b, 8);

			if (c < 0)
				goto done;
			out[o++] = (unsigned char)c;
		} else {
			unsigned long d = getbits(&b, low);   /* the low bits come first */
			int hi = sf_read(&b, dist, 64), n;
			long start, k;

			if (hi < 0)
				goto done;
			d |= (unsigned long)hi << low;
			n = sf_read(&b, lent, 64);
			if (n < 0)
				goto done;
			if (n == 63)
				n += (int)getbits(&b, 8);
			n += minimum;
			start = (long)o - (long)d - 1;
			for (k = 0; k < n && o < outlen; k++)
				out[o++] = start + k >= 0 ? out[start + k] : 0;
		}
		if (b.p > b.end + 4)
			goto done;              /* ran off the end: damaged */
	}
	result = (long)outlen;
done:
	free(lit);
	free(lent);
	free(dist);
	return result;
}

/* ------------------------------------------------------------ plan */

const char *mc_skip_text(int why)
{
	switch (why) {
	case MC_SKIP_OUTSIDE:   return "outside the design folder";
	case MC_SKIP_SUBFOLDER: return "in a subfolder the engine does not read";
	case MC_SKIP_PROGRAM:   return "a DOS/Windows program or temporary file";
	case MC_SKIP_NO83:      return "no 8.3 name for it";
	case MC_SKIP_SAME83:    return "same 8.3 name as another file";
	default:                return "left out";
	}
}

static int ieq(const char *a, const char *b)
{
	while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)*b))
		a++, b++;
	return *a == 0 && *b == 0;
}

static int iends(const char *s, const char *tail)
{
	size_t n = strlen(s), m = strlen(tail);
	return n >= m && ieq(s + n - m, tail);
}

static void design_name(const char *text, char out[9])
{
	char up[256];
	size_t n = 0, i, len = strlen(text);

	if (len >= sizeof up)
		len = sizeof up - 1;
	for (i = 0; i < len; i++)
		up[i] = (char)toupper((unsigned char)text[i]);
	up[len] = 0;
	if (len >= 4 && (!strcmp(up + len - 4, ".DSN") || !strcmp(up + len - 4, ".ZIP")))
		up[len - 4] = 0;
	for (i = 0; up[i] && n < 8; i++)
		if (isupper((unsigned char)up[i]) || isdigit((unsigned char)up[i]) || up[i] == '_')
			out[n++] = up[i];
	if (n == 0) {
		strcpy(out, "MODULE");
		return;
	}
	out[n] = 0;
}

static void member_name(mz_zip_archive *za, unsigned i, char *nm, unsigned cap)
{
	char *c;

	mz_zip_reader_get_filename(za, i, nm, cap);
	for (c = nm; *c; c++)
		if (*c == '\\')
			*c = '/';
}

static void set_err(char *err, unsigned cap, const char *a, const char *b)
{
	if (cap) {
		strncpy(err, a, cap - 1);
		err[cap - 1] = 0;
		if (b && strlen(err) + strlen(b) < cap)
			strcat(err, b);
	}
}

int mc_plan_zip(void *zap, const char *archive_name, mc_plan *p,
                char *err, unsigned errcap)
{
	static const char *const skip_ext[] = { ".EXE", ".COM", ".BAT", ".PIF",
	                                        ".LNK", ".SCR", ".DLL", ".TMP" };
	mz_zip_archive *za = zap;
	unsigned n = mz_zip_reader_get_num_files(za), i, game = n;
	char nm[512], root[512], base[256];
	size_t rlen = 0;
	int roots = 0;

	memset(p, 0, sizeof *p);
	root[0] = 0;
	for (i = 0; i < n; i++) {
		char *slash;

		if (mz_zip_reader_is_file_a_directory(za, i))
			continue;
		member_name(za, i, nm, sizeof nm);
		slash = strrchr(nm, '/');
		if (!ieq(slash ? slash + 1 : nm, "GAME001.DAT"))
			continue;
		if (slash)
			*slash = 0;
		else
			nm[0] = 0;
		if (roots == 0 || strcmp(root, nm) != 0) {
			if (roots && strcmp(root, nm) != 0) {
				set_err(err, errcap, "it holds more than one design; "
				        "add them one at a time", 0);
				return 0;
			}
			strcpy(root, nm);
			roots = 1;
			game = i;
		}
	}
	if (!roots) {
		set_err(err, errcap, "it holds no design (no GAME001.DAT anywhere)", 0);
		return 0;
	}
	rlen = strlen(root);
	{
		const char *last = strrchr(root, '/');

		last = last ? last + 1 : root;
		if (rlen && iends(last, ".DSN")) {
			design_name(last, p->design);
		} else {
			const char *b = archive_name, *q, *dot;

			for (q = archive_name; *q; q++)
				if (*q == '/' || *q == '\\' || *q == ':')
					b = q + 1;
			strncpy(base, b, sizeof base - 1);
			base[sizeof base - 1] = 0;
			if ((dot = strrchr(base, '.')) != 0)
				base[dot - base] = 0;
			design_name(base, p->design);
		}
	}
	for (i = 0; i < n; i++) {
		char *rel, *leaf, dest[13], full[MC_DEST];
		int sub = 0, k, slashes = 0;
		unsigned e;

		if (mz_zip_reader_is_file_a_directory(za, i))
			continue;
		if (p->nfiles >= MC_MAXFILES || p->nskip >= MC_MAXFILES) {
			set_err(err, errcap, "too many files in the archive", 0);
			return 0;
		}
		member_name(za, i, nm, sizeof nm);
		if (rlen && (strncmp(nm, root, rlen) != 0 || nm[rlen] != '/')) {
			p->skip[p->nskip].index = (unsigned short)i;
			p->skip[p->nskip++].why = MC_SKIP_OUTSIDE;
			continue;
		}
		rel = rlen ? nm + rlen + 1 : nm;
		for (leaf = rel, k = 0; rel[k]; k++)
			if (rel[k] == '/') {
				slashes++;
				leaf = rel + k + 1;
			}
		if (leaf[0] == 0 || leaf[0] == '.')
			continue;
		if (slashes == 1) {             /* only SAVE/ is read below the root */
			int save = 1;

			for (k = 0; k < 5; k++)
				if (toupper((unsigned char)rel[k]) != "SAVE/"[k])
					save = 0;
			if (!save)
				slashes = 2;
		}
		if (slashes == 1)
			sub = 1;
		else if (slashes > 1) {
			p->skip[p->nskip].index = (unsigned short)i;
			p->skip[p->nskip++].why = MC_SKIP_SUBFOLDER;
			continue;
		}
		for (k = 0; k < (int)(sizeof skip_ext / sizeof skip_ext[0]); k++)
			if (iends(leaf, skip_ext[k]))
				break;
		if (k < (int)(sizeof skip_ext / sizeof skip_ext[0])) {
			p->skip[p->nskip].index = (unsigned short)i;
			p->skip[p->nskip++].why = MC_SKIP_PROGRAM;
			continue;
		}
		if (!mc_dos83(leaf, dest)) {
			p->skip[p->nskip].index = (unsigned short)i;
			p->skip[p->nskip++].why = MC_SKIP_NO83;
			continue;
		}
		strcpy(full, sub ? "SAVE/" : "");
		strcat(full, dest);
		for (e = 0; e < (unsigned)p->nfiles; e++)
			if (!strcmp(p->file[e].dest, full))
				break;
		if (e < (unsigned)p->nfiles) {
			p->skip[p->nskip].index = (unsigned short)i;
			p->skip[p->nskip++].why = MC_SKIP_SAME83;
			continue;
		}
		p->file[p->nfiles].index = (unsigned short)i;
		strcpy(p->file[p->nfiles++].dest, full);
		if (iends(dest, ".TLB"))
			p->pc_art = 1;
		if (iends(dest, ".CTL"))
			p->mac_art = 1;
	}
	{
		unsigned long len = 0;
		unsigned char *g = mc_extract(za, game, &len, err, errcap);
		size_t t = 0, s = 0;

		if (!g)
			return 0;
		while (t < 31 && t < len && g[t])
			t++;
		while (s < t && g[s] == ' ')
			s++;
		while (t > s && g[t - 1] == ' ')
			t--;
		memcpy(p->title, g + s, t - s);
		p->title[t - s] = 0;
		free(g);
	}
	return 1;
}

/* ------------------------------------------------------------ members */

unsigned char *mc_extract(void *zap, unsigned index, unsigned long *len,
                          char *err, unsigned errcap)
{
	mz_zip_archive *za = zap;
	mz_zip_archive_file_stat st;
	size_t got = 0;
	void *raw;
	unsigned char *out;

	if (!mz_zip_reader_file_stat(za, index, &st)) {
		set_err(err, errcap, "damaged archive", 0);
		return 0;
	}
	if (st.m_method == 0 || st.m_method == 8) {
		out = mz_zip_reader_extract_to_heap(za, index, &got, 0);
		if (!out) {
			set_err(err, errcap, "could not unpack ", st.m_filename);
			return 0;
		}
		*len = (unsigned long)got;
		return out;
	}
	if (st.m_method != 6) {
		set_err(err, errcap, "a packing method this cannot read: ", st.m_filename);
		return 0;
	}
	raw = mz_zip_reader_extract_to_heap(za, index, &got, MZ_ZIP_FLAG_COMPRESSED_DATA);
	out = malloc(st.m_uncomp_size ? (size_t)st.m_uncomp_size : 1);
	if (!raw || !out
	 || mc_explode(raw, (unsigned long)got, out, (unsigned long)st.m_uncomp_size,
	               st.m_bit_flag) < 0
	 || mz_crc32(MZ_CRC32_INIT, out, (size_t)st.m_uncomp_size) != st.m_crc32) {
		free(raw);
		free(out);
		set_err(err, errcap, "did not unpack correctly: ", st.m_filename);
		return 0;
	}
	free(raw);
	*len = (unsigned long)st.m_uncomp_size;
	return out;
}
