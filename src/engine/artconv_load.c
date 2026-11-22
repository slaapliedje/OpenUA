/* In-engine DOS-art conversion (ADR-0014).
 *
 * The measurement behind this being safe on the fly is in
 * docs/fan-module-hacks.md ("In-engine conversion benchmark"): COLOUR
 * conversion runs 48 ms median / ~1.5 s typical picture on the 8 MHz
 * 68000, 6 ms / ~0.3 s on the Falcon — a one-time first-touch pause,
 * because the result is written back as the `.ctl` the probe wanted.
 * Mono synthesis (41–53 s per wall master on the ST) is deliberately
 * NOT here: an HLIB `.tlb` is treated as a miss and the mono build
 * plays base art until the installer / offline converter runs.
 *
 * Layering: engine code, Mac spellings only — FSOpen/FSRead/FSWrite via
 * the shim, NewPtr for the transient buffers, the artconv core for the
 * bytes. No XBIOS/GEMDOS here.
 */
#include <string.h>

#include "files.h"
#include "macmemory.h"
#include "convert/artconv.h"

#include "artconv_load.h"

#ifdef FRUA_ARTTRACE
#include "dbglog.h"
#endif

/* 128 KB covers every file in the fan corpus (measured by bisection over
 * POR's 191 files); the margin absorbs a somewhat larger entry. */
#define CONV_SCRATCH (160L * 1024)
#define CONV_DST_SLACK 2048L

static int c2p(const char *c, unsigned char *p)
{
	size_t n = strlen(c);
	if (n > 255)
		return 0;
	p[0] = (unsigned char)n;
	memcpy(p + 1, c, n);
	return 1;
}

int ua_dos_art_convert_file(const char *in_path, const char *out_path)
{
	unsigned char pin[256], pout[256];
	unsigned char *src = 0, *dst = 0, *scratch = 0;
	short ref = -1;
	long size = 0, count, out_len = -1;
	int ok = 0;

	if (!c2p(in_path, pin) || !c2p(out_path, pout))
		return 0;
	if (FSOpen((ConstStr255Param)pin, 0, &ref) != noErr)
		return 0;
	if (GetEOF(ref, &size) != noErr || size < 16) {
		FSClose(ref);
		return 0;
	}
	src = (unsigned char *)NewPtr((Size)size);
	if (src == NULL) {
		FSClose(ref);
		return 0;
	}
	count = size;
	if (FSRead(ref, &count, src) != noErr || count != size
	    || memcmp(src, "HLIB", 4) != 0) {
		FSClose(ref);
		DisposePtr((Ptr)src);
		return 0;		/* not DOS art (mono GLIB twins land here) */
	}
	FSClose(ref);

	dst = (unsigned char *)NewPtr((Size)(size + CONV_DST_SLACK));
	scratch = (unsigned char *)NewPtr((Size)CONV_SCRATCH);
	if (dst != NULL && scratch != NULL) {
		out_len = artconv_convert(src, size, dst, size + CONV_DST_SLACK,
					  scratch, CONV_SCRATCH);
	}
#ifdef FRUA_ARTTRACE
	if (out_len < 0)
		dbg_file_str("ART: DOS-art convert FAILED for: ", in_path);
#endif
	if (out_len > 0) {
		/* A stale .ctl being regenerated (ua_art_refresh_ctl) is
		 * replaced only now that its successor exists in memory. */
		OSErr cr = Create((ConstStr255Param)pout, 0, 'OpUA', 'GLIB');
		if (cr == dupFNErr
		    && FSDelete((ConstStr255Param)pout, 0) == noErr)
			cr = Create((ConstStr255Param)pout, 0, 'OpUA', 'GLIB');
		if (cr != noErr
		    || FSOpen((ConstStr255Param)pout, 0, &ref) != noErr)
			out_len = -1;
	}
	if (out_len > 0) {
		count = out_len;
		ok = (FSWrite(ref, &count, dst) == noErr && count == out_len);
		FSClose(ref);
#ifdef FRUA_ARTTRACE
		dbg_file_str(ok ? "ART: converted DOS art -> "
				: "ART: DOS-art write FAILED: ", out_path);
#endif
	}
	DisposePtr((Ptr)src);
	if (dst != NULL)
		DisposePtr((Ptr)dst);
	if (scratch != NULL)
		DisposePtr((Ptr)scratch);
	return ok;
}

/* ---- regenerating conversions written before ARTCONV_TRAILER ----------
 *
 * Until 2026-11-22 the converter wrote linear method-23 skips as 257 - v,
 * matching an engine decoder with the same error; both now use SSI's
 * 256 - v (decode_glib_t7). A .ctl converted before then is still on disk
 * beside its DOS .tlb (the engine's own first-touch caches, uainst's
 * installs) and would now draw one pixel short per gap. Such a file is
 * told apart from SSI's own Mac art by its sibling: SSI's .ctl sits beside
 * nothing or beside a Mac B&W GLIB .tlb, ours beside the HLIB we made it
 * from. A file with no .tlb left (uaconv -d deletes them) cannot be told
 * apart and needs converting again from the module. */

#define SEEN_MAX 96
static unsigned long s_seen[SEEN_MAX];
static int s_nseen;

static unsigned long path_hash(const char *p)
{
	unsigned long h = 2166136261UL;
	for (; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (c >= 'A' && c <= 'Z')
			c += 32;
		h = (h ^ c) * 16777619UL;
	}
	return h ? h : 1;
}

/* 1 the first time a path is asked about this session (full table:
 * always 1, so a check is repeated rather than skipped). */
static int first_ask(const char *path)
{
	unsigned long h = path_hash(path);
	int i;
	for (i = 0; i < s_nseen; i++)
		if (s_seen[i] == h)
			return 0;
	if (s_nseen < SEEN_MAX)
		s_seen[s_nseen++] = h;
	return 1;
}

static int read_head(const char *path, unsigned char *b, long want)
{
	unsigned char pp[256];
	short ref;
	long count = want;
	int ok;

	if (!c2p(path, pp)
	    || FSOpen((ConstStr255Param)pp, 0, &ref) != noErr)
		return 0;
	ok = (FSRead(ref, &count, b) == noErr && count == want);
	FSClose(ref);
	return ok;
}

/* 1 when the file is a GLIB without ARTCONV_TRAILER at its end. */
static int unmarked_glib(const char *path)
{
	unsigned char pp[256], m[4], t[ARTCONV_TRAILER_LEN];
	short ref;
	long count, eof = 0;
	int r = 0;

	if (!c2p(path, pp)
	    || FSOpen((ConstStr255Param)pp, 0, &ref) != noErr)
		return 0;
	count = 4;
	if (FSRead(ref, &count, m) == noErr && count == 4
	    && memcmp(m, "GLIB", 4) == 0
	    && GetEOF(ref, &eof) == noErr && eof > 16 + ARTCONV_TRAILER_LEN
	    && SetFPos(ref, fsFromStart, eof - ARTCONV_TRAILER_LEN) == noErr) {
		count = ARTCONV_TRAILER_LEN;
		r = !(FSRead(ref, &count, t) == noErr
		      && count == ARTCONV_TRAILER_LEN
		      && memcmp(t, ARTCONV_TRAILER, ARTCONV_TRAILER_LEN) == 0);
	}
	FSClose(ref);
	return r;
}

int ua_art_refresh_ctl(const char *ctl_path)
{
	char tlb[256];
	size_t n = strlen(ctl_path);
	int k;

	if (n < 5 || n >= sizeof tlb || !first_ask(ctl_path)
	    || !unmarked_glib(ctl_path))
		return 0;
	memcpy(tlb, ctl_path, n + 1);
	for (k = 0; k < 2; k++) {
		unsigned char m[4];
		memcpy(tlb + n - 3, k ? "tlb" : "TLB", 3);
		if (read_head(tlb, m, 4) && memcmp(m, "HLIB", 4) == 0) {
			int ok = ua_dos_art_convert_file(tlb, ctl_path);
#ifdef FRUA_ARTTRACE
			dbg_file_str(ok ? "ART: regenerated pre-fix .ctl: "
					: "ART: pre-fix .ctl regen FAILED: ",
				     ctl_path);
#endif
			return ok;
		}
	}
	return 0;
}
