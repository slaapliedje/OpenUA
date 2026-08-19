/*
 * planar.c — native bitplane piece conversion + masked plane blit.
 * ADR-0016 phase 1. Portable, 68000-clean C (host-testable; see
 * tests/test_planar.py). Interface + rationale in platform/include/planar.h.
 */
#include <string.h>
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

static unsigned char *(*s_vp_planes_fn)(short *pitch);
static void           (*s_vp_commitp_fn)(short x, short y, short w, short h);

void planar_viewport_planes_register(unsigned char *(*planes)(short *pitch),
                                     void (*commit)(short, short, short, short))
{
	s_vp_planes_fn  = planes;
	s_vp_commitp_fn = commit;
}

unsigned char *dsp_viewport_planes(short *pitch)
{
	return s_vp_planes_fn ? s_vp_planes_fn(pitch) : (unsigned char *)0;
}

void dsp_viewport_commit_planes(short x, short y, short w, short h)
{
	if (s_vp_commitp_fn)
		s_vp_commitp_fn(x, y, w, h);
}

/* Committed-viewport invalidation — see the long note in planar.h. */
static void (*s_vp_overwrite_fn)(short x, short y, short w, short h);

void planar_viewport_overwrite_register(void (*fn)(short, short, short, short))
{
	s_vp_overwrite_fn = fn;
}

void planar_viewport_overwrite(short x, short y, short w, short h)
{
	if (s_vp_overwrite_fn)
		s_vp_overwrite_fn(x, y, w, h);
}

/* --- draw-time plane target dispatch (ADR-0016 B4) ----------------------- */

static int (*s_dt_fn)(struct dsp_planar_dt *dt);

void planar_draw_target_register(int (*fn)(struct dsp_planar_dt *dt))
{
	s_dt_fn = fn;
}

int dsp_planar_draw_target(dsp_planar_dt_t *dt)
{
	return s_dt_fn ? s_dt_fn(dt) : 0;
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

/* --- #63 dirty rows (see planar.h) -------------------------------------- */

static unsigned char s_dirty_rows[PLANAR_DIRTY_MAX];
static int           s_dirty_all = 1;
static int           s_dirty_any;        /* #61: any row announced at all */

void planar_touch_all(void)
{
	s_dirty_all = 1;
	s_dirty_any = 1;
}

void planar_touch_rows(short y0, short y1)
{
	if (y0 < 0)                 y0 = 0;
	if (y1 > PLANAR_DIRTY_MAX)  y1 = PLANAR_DIRTY_MAX;
	if (y0 >= y1)
		return;
	memset(s_dirty_rows + y0, 1, (size_t)(y1 - y0));
	s_dirty_any = 1;
}

/* #61: did ANY writer announce a row since the last present?
 *
 * Distinct from g_qd_touched, which answers "did anyone take a POINTER to the
 * screen" — a grab marks that flag whether or not a pixel changed, and it must
 * (suppressing it once made qd_present skip a frame whose viewport composite
 * was still pending). The engine's ~5 Hz idle present needs the other
 * question, "did any pixels change", and this is it. */
int planar_dirty_any(void)
{
	return s_dirty_all || s_dirty_any;
}

int planar_dirty_rows(const unsigned char **rows)
{
	if (rows)
		*rows = s_dirty_rows;
	return s_dirty_all;
}

void planar_dirty_reset(void)
{
	s_dirty_all = 0;
	s_dirty_any = 0;
	memset(s_dirty_rows, 0, sizeof s_dirty_rows);
}
