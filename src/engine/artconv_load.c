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
	if (out_len > 0
	    && Create((ConstStr255Param)pout, 0, 'OpUA', 'GLIB') == noErr
	    && FSOpen((ConstStr255Param)pout, 0, &ref) == noErr) {
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
