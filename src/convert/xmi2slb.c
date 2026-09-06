/* xmi2slb — DOS XMIDI -> Mac MUSIC.SLB, the C port of tools/xmi2slb.py.
 *
 * Every rule below has a named twin in the Python, and the ORDER of ties is
 * part of the contract: the reference sorts (start, note, dur) tuples, picks
 * the three busiest channels stable on FIRST APPEARANCE, and rounds the
 * period half-to-even. Each of those is reproduced explicitly rather than
 * approximated, because the test is byte identity.
 *
 * Format recap (the Python's docstring has the full derivation):
 *   bank:  'SLBR' u32 size  u8 count  u8 pad  (count+1) x u16 offsets  songs
 *   song:  u16 period  u8 level(127)  u8 nvoices  5 x u16 voice offsets  voices
 *   voice: preamble 87 00 83 00 81 LL 82 1a, then (value, duration) pairs,
 *          value 128 = rest, <128 = MIDI note, FF FF = end; duration bit 6 =
 *          tie into the next pair.
 *   ONE XMI TICK = 112 Mac ticks; period = round(15360 * 500000 / tempo_us).
 */
#include "xmi2slb.h"

#define XMI_TICK     112L
#define PERIOD_REF   15360UL
#define TEMPO_REF    500000UL
#define MAX_VOICES   3
#define NSLOTS       8

static const unsigned char PREAMBLE_LEVELS[5] = { 0xFF, 0x47, 0x4E, 0x47, 0x47 };
/* Mac slot -> DOS song (1..3), 0 = Mac-only composition, left empty */
static const unsigned char SLOT_MAP[NSLOTS] = { 1, 2, 0, 0, 0, 0, 0, 3 };

/* ---- the duration grid (l0ff2, bit-faithful) ---------------------------- */

static unsigned l0ff2(unsigned b)
{
	unsigned d = 26880u >> (b & 7);
	unsigned t;
	if (b & 8)
		d = (d >> 1) * 3;
	t = (b & 48) >> 3;
	if (b & 48)
		d = (d / (t + 1)) * t;
	return d & 0xffffu;
}

static unsigned g_grid_d[0x40];
static unsigned char g_grid_c[0x40];
static int g_grid_n;
static unsigned g_grid_min;

static void grid_init(void)
{
	unsigned code;
	int i, j;
	if (g_grid_n)
		return;
	/* first code for each distinct duration, then sort by duration desc.
	 * The Python keeps insertion order on ties, but durations are DISTINCT
	 * by construction here, so a plain insertion sort is order-exact. */
	for (code = 0; code < 0x40; code++) {
		unsigned d = l0ff2(code);
		int seen = 0;
		if (d == 0)
			continue;
		for (i = 0; i < g_grid_n; i++)
			if (g_grid_d[i] == d) { seen = 1; break; }
		if (seen)
			continue;
		g_grid_d[g_grid_n] = d;
		g_grid_c[g_grid_n] = (unsigned char)code;
		g_grid_n++;
	}
	for (i = 1; i < g_grid_n; i++) {
		unsigned d = g_grid_d[i];
		unsigned char c = g_grid_c[i];
		for (j = i - 1; j >= 0 && g_grid_d[j] < d; j--) {
			g_grid_d[j + 1] = g_grid_d[j];
			g_grid_c[j + 1] = g_grid_c[j];
		}
		g_grid_d[j + 1] = d;
		g_grid_c[j + 1] = c;
	}
	g_grid_min = g_grid_d[g_grid_n - 1];
}

/* Greedy largest-fit decomposition of `target` Mac ticks into grid codes.
 * Writes codes to out (up to cap), returns the count or -1 on overflow;
 * *emitted receives the ticks actually covered. */
static long quantize(long target, unsigned char *out, long cap, long *emitted)
{
	long left = target, n = 0;
	int i;
	while (left >= (long)g_grid_min) {
		for (i = 0; i < g_grid_n; i++) {
			if ((long)g_grid_d[i] <= left) {
				if (n >= cap)
					return -1;
				out[n++] = g_grid_c[i];
				left -= (long)g_grid_d[i];
				break;
			}
		}
	}
	if (left > (long)(g_grid_min / 2)) {
		if (n >= cap)
			return -1;
		out[n++] = g_grid_c[g_grid_n - 1];
		left -= (long)g_grid_min;
	}
	*emitted = target - left;
	return n;
}

/* ---- XMI parsing --------------------------------------------------------- */

struct note {
	long tick;
	long dur;
	unsigned char ch, note;
	unsigned char seq;	/* channel first-appearance rank, filled later */
};

static long rd_vlq(const unsigned char *ev, long n, long *p, long *out)
{
	long v = 0;
	unsigned c;
	do {
		if (*p >= n)
			return -1;
		c = ev[*p];
		(*p)++;
		v = (v << 7) | (long)(c & 0x7F);
	} while (c & 0x80);
	*out = v;
	return 0;
}

/* Parse the EVNT chunk into notes[] (cap entries). Returns the note count or
 * XMI2SLB_ERR_*; *tempo receives the last tempo meta seen (default 500000). */
static long parse_xmi(const unsigned char *data, long len, struct note *notes,
		      long cap, unsigned long *tempo)
{
	long i, n, p = 0, t = 0, count = 0;
	const unsigned char *ev;

	*tempo = TEMPO_REF;
	for (i = 0; i + 8 <= len; i++)
		if (data[i] == 'E' && data[i + 1] == 'V' && data[i + 2] == 'N'
		    && data[i + 3] == 'T')
			break;
	if (i + 8 > len)
		return XMI2SLB_ERR_BAD;
	n = ((long)data[i + 4] << 24) | ((long)data[i + 5] << 16)
	  | ((long)data[i + 6] << 8) | (long)data[i + 7];
	ev = data + i + 8;
	if (n > len - (i + 8))
		n = len - (i + 8);		/* the Python slices; a short file just ends */

	while (p < n) {
		unsigned b = ev[p];
		if (b < 0x80) {			/* interval byte (bare, not VLQ) */
			t += (long)b;
			p++;
			continue;
		}
		if (b == 0xFF) {		/* meta */
			unsigned meta;
			long ln2, q;
			if (p + 1 >= n)
				return XMI2SLB_ERR_BAD;
			meta = ev[p + 1];
			q = p + 2;
			if (rd_vlq(ev, n, &q, &ln2) < 0)
				return XMI2SLB_ERR_BAD;
			if (meta == 0x51) {
				if (q + 3 > n)
					return XMI2SLB_ERR_BAD;
				*tempo = ((unsigned long)ev[q] << 16)
				       | ((unsigned long)ev[q + 1] << 8)
				       | (unsigned long)ev[q + 2];
			}
			if (meta == 0x2F)
				break;
			p = q + ln2;
			continue;
		}
		{
			unsigned st = b & 0xF0, ch = b & 0x0F;
			if (st == 0x90) {	/* note on + VLQ duration, no note-offs */
				long q, dur;
				if (p + 3 > n)
					return XMI2SLB_ERR_BAD;
				q = p + 3;
				if (rd_vlq(ev, n, &q, &dur) < 0)
					return XMI2SLB_ERR_BAD;
				if (count >= cap)
					return XMI2SLB_ERR_SPACE;
				notes[count].tick = t;
				notes[count].ch = (unsigned char)ch;
				notes[count].note = ev[p + 1];
				notes[count].dur = dur;
				count++;
				p = q;
			} else if (st == 0x80 || st == 0xA0 || st == 0xB0 || st == 0xE0) {
				p += 3;
			} else if (st == 0xC0 || st == 0xD0) {
				p += 2;
			} else {
				p += 1;
			}
		}
	}
	return count;
}

/* ---- per-voice pattern --------------------------------------------------- */

/* Ordering of the reference's `sorted(events)` over (start, note, dur). */
static int ev_less(const struct note *a, const struct note *b)
{
	if (a->tick != b->tick) return a->tick < b->tick;
	if (a->note != b->note) return a->note < b->note;
	return a->dur < b->dur;
}

static void sort_events(struct note *v, long n)
{
	long i, j;
	for (i = 1; i < n; i++) {		/* insertion sort: n is small per channel */
		struct note x = v[i];
		for (j = i - 1; j >= 0 && ev_less(&x, &v[j]); j--)
			v[j + 1] = v[j];
		v[j + 1] = x;
	}
}

/* monophonize: truncate overlaps, last note wins, drop zero-length. In place;
 * returns the surviving count. */
static long monophonize(struct note *v, long n)
{
	long i, o = 0;
	sort_events(v, n);
	for (i = 0; i < n; i++) {
		if (o > 0 && v[o - 1].tick + v[o - 1].dur > v[i].tick) {
			long d = v[i].tick - v[o - 1].tick;
			v[o - 1].dur = d > 0 ? d : 0;
		}
		v[o++] = v[i];
	}
	/* filter dur > 0 (the reference filters AFTER the whole pass) */
	{
		long w = 0;
		for (i = 0; i < o; i++)
			if (v[i].dur > 0)
				v[w++] = v[i];
		return w;
	}
}

static long put_pair(unsigned char *dst, long cap, long *n, unsigned a, unsigned b)
{
	if (*n + 2 > cap)
		return -1;
	dst[(*n)++] = (unsigned char)a;
	dst[(*n)++] = (unsigned char)b;
	return 0;
}

/* One monophonic channel -> a Mac pattern stream at dst. Events are aimed
 * at their ABSOLUTE ideal Mac tick so quantization error never drifts. */
static long voice_pattern(const struct note *ev, long n, unsigned level,
			  unsigned char *dst, long cap, unsigned char *codes, long ccap)
{
	long out = 0, pos = 0, i, k, nc, emitted;
	static const unsigned char pre[8] = { 0x87, 0x00, 0x83, 0x00, 0x81, 0, 0x82, 0x1A };
	if (cap < 8)
		return -1;
	for (i = 0; i < 8; i++)
		dst[i] = pre[i];
	dst[5] = (unsigned char)level;
	out = 8;
	for (i = 0; i < n; i++) {
		long ideal = ev[i].tick * XMI_TICK;
		if (ideal > pos) {			/* rest-fill the gap */
			nc = quantize(ideal - pos, codes, ccap, &emitted);
			if (nc < 0)
				return -1;
			for (k = 0; k < nc; k++)
				if (put_pair(dst, cap, &out, 128, codes[k]) < 0)
					return -1;
			pos += emitted;
		}
		nc = quantize(ev[i].dur * XMI_TICK, codes, ccap, &emitted);
		if (nc < 0)
			return -1;
		for (k = 0; k < nc; k++) {
			unsigned tie = (k + 1 < nc) ? 0x40 : 0;
			if (put_pair(dst, cap, &out, ev[i].note, codes[k] | tie) < 0)
				return -1;
		}
		pos += emitted;
	}
	if (put_pair(dst, cap, &out, 0xFF, 0xFF) < 0)
		return -1;
	return out;
}

/* ---- song --------------------------------------------------------------- */

static void be16(unsigned char *p, unsigned v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }

/* period = round(15360 * 500000 / tempo), the reference's round() being
 * half-to-even on an exact quotient. Done as an exact rational so the C
 * cannot disagree with the float by a unit on a .5 boundary. */
static unsigned period_of(unsigned long tempo)
{
	unsigned long long N = (unsigned long long)PERIOD_REF * TEMPO_REF;
	unsigned long long q, r;
	if (tempo == 0)
		tempo = TEMPO_REF;
	q = N / tempo;
	r = N % tempo;
	if (2 * r > tempo || (2 * r == tempo && (q & 1)))
		q++;
	return (unsigned)(q & 0xffffu);
}

long xmi2slb_song(const unsigned char *xmi, long len,
		  unsigned char *dst, long dstcap,
		  unsigned char *scratch, long scratchcap)
{
	struct note *notes, *chan;
	unsigned char *codes, *vbuf;
	long ncap, n, i, nv = 0, out;
	unsigned long tempo;
	long cnt[16], first[16], order[16];
	int pick[MAX_VOICES], npick = 0, c, k;
	long voff[5], vlen[MAX_VOICES], vpos = 0;

	grid_init();

	/* Carve the arena ADAPTIVELY. The first cut gave the whole-file note
	 * array a fixed quarter of the arena, which is 4096 notes at 12 bytes
	 * on a 68000 — and 2730 on a host with 8-byte longs, where the two
	 * Roland-arrangement modules (3591 note-ons, 15.9 KB files) failed with
	 * ERR_SPACE while the 68000 build would have scraped through by luck.
	 * Now: notes take up to HALF the arena; the per-channel copy is sized
	 * from the busiest channel AFTER parsing, placed right behind the notes
	 * actually used; the code buffer and the voice output take the rest. */
	ncap = (scratchcap / 2) / (long)sizeof(struct note);
	if (ncap < 16)
		return XMI2SLB_ERR_SPACE;
	notes = (struct note *)(void *)scratch;

	n = parse_xmi(xmi, len, notes, ncap, &tempo);
	if (n < 0)
		return n;

	/* channels: count, and rank by FIRST APPEARANCE (the reference iterates
	 * a dict, i.e. insertion order, when breaking count ties). Channel 9 is
	 * GM percussion — no pitch — and is dropped. */
	for (c = 0; c < 16; c++) { cnt[c] = 0; first[c] = -1; }
	for (i = 0; i < n; i++) {
		c = notes[i].ch;
		if (c == 9)
			continue;
		if (first[c] < 0)
			first[c] = i;
		cnt[c]++;
	}
	/* candidate list in first-appearance order */
	{
		long m = 0;
		for (i = 0; i < n; i++) {
			c = notes[i].ch;
			if (c != 9 && first[c] == i)
				order[m++] = c;
		}
		/* stable selection: highest count first, ties by appearance */
		for (k = 0; k < MAX_VOICES && k < m; k++) {
			long best = -1, bi = -1;
			for (i = 0; i < m; i++) {
				int used = 0, u;
				for (u = 0; u < npick; u++)
					if (pick[u] == order[i]) used = 1;
				if (used)
					continue;
				if (cnt[order[i]] > best) { best = cnt[order[i]]; bi = i; }
			}
			if (bi < 0)
				break;
			pick[npick++] = (int)order[bi];
		}
	}
	/* keep the source (numeric) ordering of the chosen channels */
	for (k = 1; k < npick; k++) {
		int x = pick[k], j;
		for (j = k - 1; j >= 0 && pick[j] > x; j--)
			pick[j + 1] = pick[j];
		pick[j + 1] = x;
	}

	/* the rest of the arena, behind the notes actually parsed */
	{
		long maxc = 0, used;
		for (c = 0; c < 16; c++)
			if (cnt[c] > maxc) maxc = cnt[c];
		chan = notes + n;
		used = (long)((unsigned char *)(chan + maxc) - scratch);
		if (used + 4096 + 64 > scratchcap)
			return XMI2SLB_ERR_SPACE;
		codes = (unsigned char *)(void *)(chan + maxc);
		vbuf  = codes + 4096;
	}

	/* each voice: copy the channel's events, monophonize, encode */
	for (k = 0; k < npick; k++) {
		long m = 0, l;
		for (i = 0; i < n; i++)
			if (notes[i].ch == pick[k])
				chan[m++] = notes[i];
		m = monophonize(chan, m);
		l = voice_pattern(chan, m, PREAMBLE_LEVELS[k], vbuf + vpos,
				  (scratch + scratchcap) - (vbuf + vpos), codes, 4096);
		if (l < 0)
			return XMI2SLB_ERR_SPACE;
		vlen[k] = l;
		voff[k] = vpos;
		vpos += l;
		nv++;
	}
	for (k = (int)nv; k < 5; k++)
		voff[k] = 0;

	out = 14 + vpos;
	if (out > dstcap)
		return XMI2SLB_ERR_SPACE;
	be16(dst, period_of(tempo));
	dst[2] = 127;
	dst[3] = (unsigned char)nv;
	for (k = 0; k < 5; k++)
		be16(dst + 4 + 2 * k, (unsigned)voff[k]);
	for (i = 0; i < vpos; i++)
		dst[14 + i] = vbuf[i];
	(void)vlen;
	return out;
}

static long empty_song(unsigned char *dst, long cap)
{
	int k;
	if (cap < 16)
		return XMI2SLB_ERR_SPACE;
	be16(dst, (unsigned)PERIOD_REF);
	dst[2] = 127;
	dst[3] = 1;
	for (k = 0; k < 5; k++)
		be16(dst + 4 + 2 * k, 0);
	dst[14] = 0xFF;
	dst[15] = 0xFF;
	return 16;
}

/* ---- bank --------------------------------------------------------------- */

long xmi2slb_bank(const unsigned char *const xmi[3], const long len[3],
		  unsigned char *dst, long dstcap,
		  unsigned char *scratch, long scratchcap)
{
	long hdr = 4 + 4 + 1 + 1 + 2 * (NSLOTS + 1);
	long pos = hdr, off[NSLOTS + 1], total;
	int s;
	if (dstcap < hdr)
		return XMI2SLB_ERR_SPACE;
	for (s = 0; s < NSLOTS; s++) {
		int q = SLOT_MAP[s];
		long l;
		off[s] = pos - hdr;
		if (q != 0 && xmi[q - 1] != 0)
			l = xmi2slb_song(xmi[q - 1], len[q - 1], dst + pos, dstcap - pos,
					 scratch, scratchcap);
		else
			l = empty_song(dst + pos, dstcap - pos);
		if (l < 0)
			return l;
		pos += l;
	}
	off[NSLOTS] = pos - hdr;
	total = pos;
	dst[0] = 'S'; dst[1] = 'L'; dst[2] = 'B'; dst[3] = 'R';
	dst[4] = (unsigned char)(total >> 24); dst[5] = (unsigned char)(total >> 16);
	dst[6] = (unsigned char)(total >> 8);  dst[7] = (unsigned char)total;
	dst[8] = NSLOTS;
	dst[9] = 0;
	for (s = 0; s <= NSLOTS; s++)
		be16(dst + 10 + 2 * s, (unsigned)off[s]);
	return total;
}

/* ---- filename classification -------------------------------------------- */

static int up(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static int isdig(int c) { return c >= '0' && c <= '9'; }

int xmi2slb_classify(const char *b, int *drv, int *q)
{
	char u[16];
	int n = 0;
	while (b[n] && n < 15) { u[n] = (char)up((unsigned char)b[n]); n++; }
	u[n] = 0;
	if (n < 8 || u[n - 4] != '.' || u[n - 3] != 'X' || u[n - 2] != 'M' || u[n - 1] != 'I')
		return 0;
	if (n == 9 && u[2] == 'D' && u[3] == 'Q' && isdig(u[4])) {	/* ??DQ<n>.XMI */
		if (u[0] == 'T' && u[1] == 'Y') *drv = XMI2SLB_DRV_TY;
		else if (u[0] == 'P' && u[1] == 'C') *drv = XMI2SLB_DRV_PC;
		else if (u[0] == 'R' && u[1] == 'O') *drv = XMI2SLB_DRV_RO;
		else if (u[0] == 'A' && u[1] == 'D') *drv = XMI2SLB_DRV_AD;
		else return 0;	/* an unknown prefix is not one of the sets (the
				 * reference records it but never selects it) */
		*q = u[4] - '0';
		return 1;
	}
	if (n == 9 && u[0] == 'D' && u[1] == 'Q' && u[2] == 'K' && u[3] == 'Q' && isdig(u[4])) {
		*drv = XMI2SLB_DRV_DQK; *q = u[4] - '0'; return 1;		/* DQKQ<n>.XMI */
	}
	if (n == 8 && u[0] == 'D' && u[1] == 'Q' && u[2] == 'K' && isdig(u[3])) {
		*drv = XMI2SLB_DRV_DQK; *q = u[3] - '0'; return 1;		/* DQK<n>.XMI */
	}
	return 0;
}
