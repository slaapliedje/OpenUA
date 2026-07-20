/*
 * planar.c — native bitplane piece conversion + masked plane blit.
 * ADR-0016 phase 1. Portable, 68000-clean C (host-testable; see
 * tests/test_planar.py). Interface + rationale in platform/include/planar.h.
 */
#include "planar.h"
#include "display.h"

/* --- dungeon-viewport composite dispatch (ADR-0016 B2) -------------------
 *
 * The engine renders the first-person viewport into a backend-supplied chunky
 * scratch and hands it back for compositing (see display.h). Which backend
 * services that — if any — is only known at runtime (dsp_detect picks one), and
 * the two build trees link different backend objects (the Amiga build has no
 * display_ste.c). Rather than force every build to define the pair, the shared
 * planar module (linked in BOTH trees) owns the entry points and dispatches
 * through a hook the active bitplane backend installs at init. A backend that
 * keeps the chunky path (Falcon/TT VIDEL, and Amiga until its own B2 lands)
 * never registers, so dsp_viewport_scratch() returns NULL and the engine
 * renders straight into the shared surface exactly as before. */
static unsigned char *(*s_vp_scratch_fn)(short *pitch);
static void           (*s_vp_commit_fn)(short x, short y, short w, short h);

void planar_viewport_register(unsigned char *(*scratch)(short *pitch),
                              void (*commit)(short, short, short, short))
{
	s_vp_scratch_fn = scratch;
	s_vp_commit_fn  = commit;
}

unsigned char *dsp_viewport_scratch(short *pitch)
{
	return s_vp_scratch_fn ? s_vp_scratch_fn(pitch) : (unsigned char *)0;
}

void dsp_viewport_commit(short x, short y, short w, short h)
{
	if (s_vp_commit_fn)
		s_vp_commit_fn(x, y, w, h);
}

/* --- native-planar text overlay dispatch (ADR-0016 B Phase-1) -------------
 * Same registration shape as the viewport: the active bitplane backend installs
 * a scratch getter + commit/clear hooks at init; unregistered backends make the
 * entry points inert (scratch NULL) so DrawChar keeps writing the shared surface. */
static unsigned char *(*s_txt_scratch_fn)(short *pitch);
static void           (*s_txt_commit_fn)(short x, short y, short w, short h);
static void           (*s_txt_clear_fn)(void);
static void           (*s_txt_clear_rect_fn)(short x, short y, short w, short h);

void planar_text_register(unsigned char *(*scratch)(short *pitch),
                          void (*commit)(short, short, short, short),
                          void (*clear)(void),
                          void (*clear_rect)(short, short, short, short))
{
	s_txt_scratch_fn    = scratch;
	s_txt_commit_fn     = commit;
	s_txt_clear_fn      = clear;
	s_txt_clear_rect_fn = clear_rect;
}

unsigned char *dsp_text_scratch(short *pitch)
{
	return s_txt_scratch_fn ? s_txt_scratch_fn(pitch) : (unsigned char *)0;
}

void dsp_text_commit(short x, short y, short w, short h)
{
	if (s_txt_commit_fn)
		s_txt_commit_fn(x, y, w, h);
}

void dsp_text_clear(void)
{
	if (s_txt_clear_fn)
		s_txt_clear_fn();
}

void dsp_text_clear_rect(short x, short y, short w, short h)
{
	if (s_txt_clear_rect_fn)
		s_txt_clear_rect_fn(x, y, w, h);
}

void chunky_to_planar_piece(const unsigned char *src, short src_pitch,
                            short w, short h,
                            const unsigned char *remap,
                            const unsigned char *trans,
                            planar_piece_t *dst)
{
	short stride = PLANAR_STRIDE(w);
	short y, x, p;
	long  planebytes = (long)stride * h;

	dst->stride = stride;

	/* Clear planes + mask first — pieces are word-padded, and transparent
	 * pixels leave their plane bits 0. */
	for (p = 0; p < dst->nplanes; p++) {
		long base = (long)p * planebytes;
		long i;
		for (i = 0; i < planebytes; i++)
			dst->planes[base + i] = 0;
	}
	{
		long i;
		for (i = 0; i < planebytes; i++)
			dst->mask[i] = 0;
	}

	for (y = 0; y < h; y++) {
		const unsigned char *srow = src + (long)y * src_pitch;
		long rowoff = (long)y * stride;

		for (x = 0; x < w; x++) {
			unsigned char idx = srow[x];
			unsigned char bit = (unsigned char)(0x80u >> (x & 7));
			short         byte = (short)(x >> 3);
			unsigned char val;

			if (trans != 0 && trans[idx]) {
				/* transparent: mask stays clear, planes stay 0 */
				continue;
			}
			dst->mask[rowoff + byte] |= bit;

			val = remap ? remap[idx] : idx;
			for (p = 0; p < dst->nplanes; p++) {
				if ((val >> p) & 1)
					dst->planes[(long)p * planebytes
					            + rowoff + byte] |= bit;
			}
		}
	}
}

void planar_blit_cpu(const planar_piece_t *piece,
                     unsigned char *const dst_planes[], short dst_stride,
                     short dst_w, short dst_h, short x, short y)
{
	long  planebytes = (long)piece->stride * piece->h;
	short py, px, p;

	for (py = 0; py < piece->h; py++) {
		short dy = (short)(y + py);
		long  srow;

		if (dy < 0 || dy >= dst_h)
			continue;
		srow = (long)py * piece->stride;

		for (px = 0; px < piece->w; px++) {
			short dx = (short)(x + px);
			unsigned char sbit = (unsigned char)(0x80u >> (px & 7));
			short sbyte = (short)(px >> 3);
			unsigned char dbit;
			short dbyte;

			if (dx < 0 || dx >= dst_w)
				continue;
			/* transparent piece pixel: leave dst untouched (cookie-cut) */
			if (!(piece->mask[srow + sbyte] & sbit))
				continue;

			dbit  = (unsigned char)(0x80u >> (dx & 7));
			dbyte = (short)(dx >> 3);

			for (p = 0; p < piece->nplanes; p++) {
				unsigned char *d =
				    &dst_planes[p][(long)dy * dst_stride + dbyte];
				if (piece->planes[(long)p * planebytes + srow + sbyte]
				    & sbit)
					*d |= dbit;
				else
					*d &= (unsigned char)~dbit;
			}
		}
	}
}

void planar_blit_stlow(unsigned char *const src_planes[], short src_stride,
                       short src_w, short src_h, short nplanes,
                       unsigned char *dst, short dst_line_bytes,
                       short dst_w, short dst_h, short dx, short dy)
{
	short y, x, p;

	for (y = 0; y < src_h; y++) {
		short ddy = (short)(dy + y);
		long  srow;
		long  drow;

		if (ddy < 0 || ddy >= dst_h)
			continue;
		srow = (long)y * src_stride;
		drow = (long)ddy * dst_line_bytes;

		for (x = 0; x < src_w; x++) {
			short ddx = (short)(dx + x);
			short g, bit;
			unsigned char sbit = (unsigned char)(0x80u >> (x & 7));
			short sbyte = (short)(x >> 3);
			unsigned char *grp;
			unsigned char dmask;
			short dbyte;

			if (ddx < 0 || ddx >= dst_w)
				continue;
			g     = (short)(ddx >> 4);              /* 16-pixel group      */
			bit   = (short)(ddx & 15);              /* 0 = leftmost (MSB)  */
			dbyte = (short)(bit >> 3);              /* byte 0/1 of the word */
			dmask = (unsigned char)(0x80u >> (bit & 7));
			grp   = dst + drow + (long)g * nplanes * 2;

			for (p = 0; p < nplanes; p++) {
				unsigned char *d = grp + (long)p * 2 + dbyte;
				if (src_planes[p][srow + sbyte] & sbit)
					*d |= dmask;
				else
					*d &= (unsigned char)~dmask;
			}
		}
	}
}
