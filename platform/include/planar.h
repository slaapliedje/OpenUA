/*
 * planar.h — native bitplane pieces + masked plane blit (ADR-0016 phase 1).
 *
 * The bitplane machines (ST/STe, Amiga ECS/OCS) render natively in planes
 * instead of round-tripping an 8bpp chunky surface through a per-present c2p.
 * Wall pieces (and, later, chrome/sprites) are converted to planes ONCE at
 * wall-set load, then blitted 1:1 — the engine's grid dungeon shows each piece
 * at a fixed on-screen size, so there is no runtime scale (see docs/planar-plan.md).
 *
 * A piece is nplanes bitplanes plus a 1-bit transparency mask. Bit order is
 * MSB-first within each byte (bit 7 = leftmost pixel), matching the Amiga/Atari
 * bitplane convention and the shared c2p (platform/amiga/c2p_amiga.c). Rows are
 * word-aligned (2-byte stride granularity) so the hardware blitter can consume
 * them without a sub-word fixup.
 *
 * This module is portable, 68000-clean C — host-compilable so tests/test_planar.py
 * can verify it against a naive reference (same discipline as the c2p32 test).
 * The hardware-blitter fast path (phase 4) plugs in under planar_blit and must
 * match planar_blit_cpu byte-for-byte.
 */
#ifndef PLATFORM_PLANAR_H
#define PLATFORM_PLANAR_H

#include "c2p32.h"   /* #129: the shared 32-pixel bit transpose (subtree) */

/* Bytes per plane row for a piece `w` pixels wide, rounded up to a word. */
#define PLANAR_STRIDE(w)   ((short)((((w) + 15) >> 4) << 1))

/* A pre-converted planar piece. `planes` is one buffer, plane p starting at
 * planes + (long)p * stride * h; `mask` is a single stride*h bitmap (1 = opaque).
 * All bit rows are `stride` bytes, MSB-first. */
typedef struct {
	short          w, h;         /* pixel dimensions                        */
	short          stride;       /* bytes per plane row (word-aligned)      */
	short          nplanes;      /* bitplane count (5 = ECS, 4 = ST)        */
	unsigned char *planes;       /* nplanes * stride * h bytes              */
	unsigned char *mask;         /* stride * h bytes; 1 bit = opaque        */
} planar_piece_t;

/* Bytes the caller must allocate for a piece `w` x `h` with `nplanes` planes. */
#define PLANAR_PLANES_BYTES(w, h, np) ((long)PLANAR_STRIDE(w) * (h) * (np))
#define PLANAR_MASK_BYTES(w, h)       ((long)PLANAR_STRIDE(w) * (h))

/*
 * Convert a chunky indexed piece into `dst` (caller fills w/h/nplanes and the
 * planes/mask buffers; this sets stride and writes the bits).
 *
 *   src, src_pitch, w, h  — the chunky source (one palette index per byte).
 *   remap[256]            — index -> N-colour palette slot (0..2^nplanes-1),
 *                           the per-band map that quant_banded produces; applied
 *                           ONCE here instead of per present.
 *   trans[256]            — 1 = this index is transparent (global key 255 and
 *                           the per-set magenta key both fold in here).
 *
 * Transparent pixels get a clear mask bit; their plane bits are left 0.
 */
void chunky_to_planar_piece(const unsigned char *src, short src_pitch,
                            short w, short h,
                            const unsigned char *remap,
                            const unsigned char *trans,
                            planar_piece_t *dst);

/*
 * Masked plane-blit `piece` into a planar destination at (x, y), clipped to
 * (dst_w x dst_h). `dst_planes[p]` is plane p's base; `dst_stride` its row
 * bytes. Cookie-cut: dst keeps its bits where the piece is transparent. This is
 * the CPU reference / plain-ST fallback; the blitter path (phase 4) matches it.
 */
void planar_blit_cpu(const planar_piece_t *piece,
                     unsigned char *const dst_planes[], short dst_stride,
                     short dst_w, short dst_h, short x, short y);

/*
 * Composite a fully-painted separate-plane region (the dungeon viewport buffer:
 * `nplanes` planes, `src_planes[p]` each `src_stride` bytes/row, `src_w`x`src_h`,
 * MSB-first) into ST-Low INTERLEAVED screen memory at (dx, dy), clipped to
 * (dst_w x dst_h). ST-Low packs `nplanes` big-endian words per 16-pixel group:
 * plane p's word for group g at `dst + y*dst_line_bytes + g*nplanes*2 + p*2`.
 *
 * This is the ST side of the native-planar composite (ADR-0016 phase 2): the
 * engine renders the dungeon into a separate-plane viewport buffer with
 * planar_blit_cpu, then the STE backend drops it into the viewport hole here —
 * NO c2p. Opaque rectangular overwrite (transparency was already resolved when
 * the pieces blitted into the buffer); byte-oriented so it is endianness-neutral
 * (host-testable) and correct on the 68k big-endian screen. Handles a
 * non-16-aligned dx per pixel. This is the plain-ST CPU fallback; the STe/Amiga
 * blitter path (phase 4) does the same rearrange in hardware.
 */
void planar_blit_stlow(unsigned char *const src_planes[], short src_stride,
                       short src_w, short src_h, short nplanes,
                       unsigned char *dst, short dst_line_bytes,
                       short dst_w, short dst_h, short dx, short dy);

/*
 * Install the active bitplane backend's dungeon-viewport composite hooks
 * (ADR-0016 B2). `scratch(pitch)` returns the chunky buffer the engine renders
 * the viewport into (absolute screen coords) and fills *pitch; `commit(x,y,w,h)`
 * converts that rect to planes for the next present's composite. The shared
 * dispatch (dsp_viewport_scratch / dsp_viewport_commit in display.h) routes
 * through these; pass (0, 0) to unregister (backend shutdown). Backends that
 * keep the chunky c2p path never call this, so the engine falls back to
 * rendering straight into the shared surface.
 */
void planar_viewport_register(unsigned char *(*scratch)(short *pitch),
                              void (*commit)(short x, short y, short w, short h));

/*
 * Tell the composite that a SCREEN blit just overwrote rect (x,y,w,h) of the
 * shared chunky surface. The backend invalidates its committed viewport if the
 * rects intersect — the scratch no longer represents what belongs on screen.
 *
 * ★ WHY THIS EXISTS (the ST wrong-event-picture bug, 2026-08-07). The commit
 * scratch is a SNAPSHOT: the composite repaints it onto the pages not just at
 * the present after the commit but again after every force-full (#61 — a
 * rebuild from s_dt leaves a hole where the composite content was, because the
 * composite writes the PAGE, never s_dt). That re-arm is right while the 3D
 * view is the newest content, and exactly wrong after an event picture blits
 * OVER the viewport: the event's palette install triggers a reband, the reband
 * force-fulls, the force-full re-arms the owes — and the next present paints
 * the STALE corridor over the freshly-drawn picture, through the new palette's
 * remap. On screen: the hallway wearing the event's colours, indefinitely,
 * while the engine believes it drew the picture. Registered by the same
 * backends that register the viewport hooks; a no-op everywhere else.
 */
/* ADR-0016 B5: the engine STAMPS the viewport's planes itself and the backend
 * only has to move them. dsp_viewport_planes() hands back a page-layout plane
 * buffer (same interleaved form and pitch as a screen page, addressed in
 * ABSOLUTE screen coords) for the engine to draw into; dsp_viewport_commit_planes
 * then says "these planes hold the frame", and the composite becomes a COPY —
 * no chunky->planar conversion at all. A backend that has not registered returns
 * NULL, and the engine keeps to the chunky scratch it has always used, so the
 * layout stays platform's business and only the ST/STe path changes today. */
void planar_viewport_planes_register(unsigned char *(*planes)(short *pitch),
                                     void (*commit)(short, short, short, short));
void planar_reband_query_register(int (*pending)(void),
                                  void (*chunky_valid)(short));

void planar_viewport_overwrite_register(void (*fn)(short x, short y,
                                                   short w, short h));
void planar_viewport_overwrite(short x, short y, short w, short h);

/*
 * Install the active backend's draw-time plane target (ADR-0016 B4). The shared
 * dsp_planar_draw_target() (display.h) dispatches through the hook a backend
 * running the draw-time plane model installs at init; a backend that keeps the
 * chunky+c2p path never registers, so converted writers see 0 and take their
 * chunky store. Pass 0 to unregister (backend shutdown). Forward-declared struct
 * (defined in display.h) so this header stays display-independent.
 */
struct dsp_planar_dt;
void planar_draw_target_register(int (*fn)(struct dsp_planar_dt *dt));

/* #63 dirty rows — which surface rows a writer touched since the last full
 * present. Lives HERE, not in the shim, because both sides need it and the
 * layer rule runs compat -> platform: the Toolbox shim records (it is what
 * knows the rects), a display backend reads (it is what would otherwise scan
 * all 200 rows to rediscover the same thing).
 *
 * ★ CONSERVATIVE BY DEFAULT. Any writer that cannot name its rows calls
 * planar_touch_all() and the backend scans everything, exactly as before a
 * dirty set existed. A writer that narrows without announcing leaves its rows
 * stale forever — see FRUA_DIRTYCHECK. */
#define PLANAR_DIRTY_MAX 512            /* tallest surface any backend attaches */
void planar_touch_rows(short y0, short y1);      /* [y0, y1), surface coords */
void planar_touch_all(void);
int  planar_dirty_rows(const unsigned char **rows);  /* !0 = scan everything */
int  planar_dirty_any(void);                     /* !0 = some row announced  */
void planar_dirty_reset(void);

/* --- draw-time plane store (ADR-0016 draw-time present model) -------------
 *
 * The primitives the draw-time model routes every writer through: set slot bits
 * straight into the LIVE ST-Low INTERLEAVED screen at draw time (no chunky, no
 * batch c2p). ST-Low packs `nplanes` big-endian words per 16-pixel group; plane
 * p's word for group g sits at dst + y*line_bytes + g*nplanes*2 + p*2, MSB =
 * leftmost pixel. `slot` is the remapped 0..2^nplanes-1 palette index.
 * Header-inline + 68000-clean so the ST backend inlines them and the host test
 * (tests/test_planar_fill.py) exercises the same code. Correctness-first
 * per-pixel form; a word-constant fast path (constant plane words for aligned
 * flat spans, like c2p4st_32_flat) can drop in under the same interface later. */
static inline void planar_put_stlow(unsigned char *dst, short line_bytes,
                                    short nplanes, short x, short y,
                                    unsigned char slot)
{
	short g    = (short)(x >> 4);           /* 16-pixel group        */
	short bit  = (short)(x & 15);           /* 0 = leftmost (MSB)    */
	short byte = (short)(bit >> 3);         /* byte 0/1 of the word  */
	unsigned char  mask = (unsigned char)(0x80u >> (bit & 7));
	unsigned char *grp  = dst + (long)y * line_bytes + (long)g * nplanes * 2;
	short p;

	for (p = 0; p < nplanes; p++) {
		unsigned char *d = grp + (long)p * 2 + byte;
		if ((slot >> p) & 1)
			*d = (unsigned char)(*d | mask);
		else
			*d = (unsigned char)(*d & ~mask);
	}
}

/* Amiga-format draw-time plane store: SEPARATE planes (plane p starting at
 * dst + p*plane_bytes, `pitch` bytes/row, MSB-first) — the ECS/AGA layout,
 * vs planar_put_stlow's ST-Low word-interleave. Same contract otherwise. */
static inline void planar_put_amiga(unsigned char *dst, short pitch,
                                    long plane_bytes, short nplanes,
                                    short x, short y, unsigned char slot)
{
	long          byte = (long)y * pitch + (x >> 3);
	unsigned char mask = (unsigned char)(0x80u >> (x & 7));
	short p;

	for (p = 0; p < nplanes; p++) {
		unsigned char *d = dst + (long)p * plane_bytes + byte;
		if ((slot >> p) & 1)
			*d = (unsigned char)(*d | mask);
		else
			*d = (unsigned char)(*d & ~mask);
	}
}

/* Fill rect [x,x+w) x [y,y+h), clipped to (dst_w x dst_h), with `slot`. */
/*
 * #125e: one row of a SOLID span, written a 16-pixel GROUP at a time.
 *
 * The per-pixel form below it was 96% of jt103's cost — 4.67 s of a 4.85 s
 * panel fill on an 8 MHz STE, ~725 cycles per pixel to mirror a chunky fill
 * that itself took 0.18 s. For a solid slot every plane word inside a fully
 * covered group is a CONSTANT (0xFFFF where the slot bit is set, 0x0000 where
 * it is clear), so a full group costs `nplanes` word stores for 16 pixels
 * instead of 16 read-modify-writes per plane. Only the ragged ends need a
 * mask. This is the "word-constant fast path for aligned flat spans" the
 * header comment above has always promised.
 *
 * ★ MASK BYTE-WISE, NOT THROUGH `unsigned short *`. A word store writes in the
 * HOST's byte order, so the obvious `*(unsigned short *)p |= mask` form is
 * correct on the big-endian m68k target and silently wrong on the
 * little-endian host that runs tests/test_planar_fill.py — a bug that would
 * have shipped fine and failed only in the test, which is the worst possible
 * place to have to argue about it. Splitting the mask into its two bytes is
 * endian-independent, and a FULLY covered group needs no read at all: both
 * bytes are 0xFF, so the plane byte is just stored.
 */
static inline void planar_span_stlow(unsigned char *dst, short line_bytes,
                                     short nplanes, short y,
                                     short x0, short x1, unsigned char slot)
{
	unsigned char *row = dst + (long)y * line_bytes;
	short          x   = x0;

	while (x < x1) {
		short          g    = (short)(x >> 4);
		short          bit  = (short)(x & 15);
		short          n    = (short)(16 - bit);
		unsigned short mask;
		unsigned char *grp;
		short          p;

		if (n > (short)(x1 - x))
			n = (short)(x1 - x);
		/* Bits [15-bit .. 15-bit-n+1] of the group word. Computed in
		 * unsigned long so a full group (bit==0, n==16) shifts by 16
		 * without hitting the 16-bit shift-width edge. */
		mask = (unsigned short)(((0xFFFFUL >> bit)
		                       & ~(0xFFFFUL >> (bit + n))) & 0xFFFFUL);
		grp  = row + (long)g * nplanes * 2;
		{
			unsigned char hi = (unsigned char)(mask >> 8);
			unsigned char lo = (unsigned char)(mask & 0xFF);
			int           full = (hi == 0xFF && lo == 0xFF);

			for (p = 0; p < nplanes; p++) {
				unsigned char *d = grp + p * 2;

				if ((slot >> p) & 1) {
					if (full) {
						d[0] = 0xFF; d[1] = 0xFF;
					} else {
						d[0] = (unsigned char)(d[0] | hi);
						d[1] = (unsigned char)(d[1] | lo);
					}
				} else {
					if (full) {
						d[0] = 0; d[1] = 0;
					} else {
						d[0] = (unsigned char)(d[0] & ~hi);
						d[1] = (unsigned char)(d[1] & ~lo);
					}
				}
			}
		}
		x = (short)(x + n);
	}
}

static inline void planar_fill_stlow(unsigned char *dst, short line_bytes,
                                     short nplanes, short dst_w, short dst_h,
                                     short x, short y, short w, short h,
                                     unsigned char slot)
{
	short yy, x1 = (short)(x + w), y1 = (short)(y + h);

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x1 > dst_w) x1 = dst_w;
	if (y1 > dst_h) y1 = dst_h;
	for (yy = y; yy < y1; yy++)
		planar_span_stlow(dst, line_bytes, nplanes, yy, x, x1, slot);
}

/*
 * #127: the Amiga siblings of planar_span_stlow / planar_c2p_span_stlow.
 *
 * SEPARATE planes (plane p at dst + p*plane_bytes, `pitch` bytes/row,
 * MSB-first, 8 pixels per byte), so the unit is a BYTE, not a 16-pixel word
 * group. Everything else carries over: a solid run makes each fully covered
 * plane byte a constant, and a chunky run's plane bytes are accumulated in a
 * register and stored once instead of `nplanes` read-modify-writes per pixel.
 *
 * ECS/AGA were left on the per-pixel loop by #125e/#126b — correct but paying
 * the full ~450 cycles/pixel the ST no longer pays. No endianness hazard here
 * (byte-addressed throughout), unlike the ST word-group form.
 */
static inline void planar_span_amiga(unsigned char *dst, short pitch,
                                     long plane_bytes, short nplanes, short y,
                                     short x0, short x1, unsigned char slot)
{
	long rowoff = (long)y * pitch;
	short x = x0;

	while (x < x1) {
		short         b    = (short)(x >> 3);
		short         bit  = (short)(x & 7);
		short         n    = (short)(8 - bit);
		unsigned char mask;
		short         p;

		if (n > (short)(x1 - x))
			n = (short)(x1 - x);
		mask = (unsigned char)(((0xFFu >> bit)
		                      & ~(0xFFu >> (bit + n))) & 0xFFu);
		for (p = 0; p < nplanes; p++) {
			unsigned char *d = dst + (long)p * plane_bytes + rowoff + b;

			if (mask == 0xFF)
				*d = (unsigned char)(((slot >> p) & 1) ? 0xFF : 0x00);
			else if ((slot >> p) & 1)
				*d = (unsigned char)(*d | mask);
			else
				*d = (unsigned char)(*d & (unsigned char)~mask);
		}
		x = (short)(x + n);
	}
}

static inline void planar_c2p_span_amiga(unsigned char *dst, short pitch,
                                         long plane_bytes, short nplanes,
                                         short y, short x0, short x1,
                                         const unsigned char *src,
                                         const unsigned char *lut)
{
	long  rowoff = (long)y * pitch;
	short x      = x0;

	while (x < x1) {
		short         b    = (short)(x >> 3);
		short         bit  = (short)(x & 7);
		short         n    = (short)(8 - bit);
		unsigned char built[16];
		unsigned char mask;
		short         i, p;

		if (n > (short)(x1 - x))
			n = (short)(x1 - x);

		/* #129: a WHOLE aligned 32-pixel block goes through the subtree's
		 * bit-matrix transpose (~4 ops/pixel) instead of the per-pixel,
		 * per-plane bit test below — the difference AGA feels most, since
		 * that inner loop runs `nplanes` = 8 times per pixel. Only taken
		 * when the block is 32-aligned AND entirely inside the span, so
		 * every byte is full: no mask, no read-back.
		 * Narrow spans deliberately do NOT come here — padding an 8-pixel
		 * leaf out to a 32-pixel transpose costs MORE than it saves, and
		 * 1,425 of 1,767 chrome leaves are 8 wide (#126). */
		if ((x & 31) == 0 && (short)(x1 - x) >= 32 && nplanes <= 8) {
			c2p_u32       c[8], out[8];
			unsigned char tmp[32];

			/* Remap through the band LUT into a scratch run, then
			 * let the subtree do BOTH the big-endian packing and the
			 * transpose. Open-coding the packing here duplicated
			 * c2p_load32 — and would have been a second place to get
			 * the lane order wrong. (It also keeps c2p_load32 used,
			 * which matters: planar.h is included by TUs built with
			 * -Werror, where an unused static from the subtree header
			 * is a hard error.) */
			for (i = 0; i < 32; i++)
				tmp[i] = lut[src[x + i]];
			c2p_load32(tmp, c);
			c2p_transpose32(c, out);
			for (p = 0; p < nplanes; p++) {
				unsigned char *d = dst + (long)p * plane_bytes
				                 + rowoff + (x >> 3);
				c2p_u32 v = out[p];

				d[0] = (unsigned char)(v >> 24);
				d[1] = (unsigned char)(v >> 16);
				d[2] = (unsigned char)(v >> 8);
				d[3] = (unsigned char)v;
			}
			x = (short)(x + 32);
			continue;
		}

		for (p = 0; p < nplanes; p++)
			built[p] = 0;
		for (i = 0; i < n; i++) {
			unsigned char s  = lut[src[x + i]];
			unsigned char bm = (unsigned char)(0x80u >> (bit + i));

			/* Walk the slot bits with a RUNNING shift rather than
			 * `(s >> p) & 1`: a variable shift costs 6+2n cycles on a
			 * 68000 and this ran nplanes times per pixel. */
			for (p = 0; p < nplanes; p++) {
				if (s & 1)
					built[p] = (unsigned char)(built[p] | bm);
				s = (unsigned char)(s >> 1);
			}
		}
		mask = (unsigned char)(((0xFFu >> bit)
		                      & ~(0xFFu >> (bit + n))) & 0xFFu);
		for (p = 0; p < nplanes; p++) {
			unsigned char *d = dst + (long)p * plane_bytes + rowoff + b;

			if (mask == 0xFF)
				*d = built[p];
			else
				*d = (unsigned char)((*d & (unsigned char)~mask)
				                   | (built[p] & mask));
		}
		x = (short)(x + n);
	}
}

/*
 * #126: chunky -> planar for ONE ROW of a span, a 16-pixel GROUP at a time.
 *
 * `src[x]` is the chunky index of pixel x (an absolute-x row pointer) and
 * `lut[]` maps it to a 0..2^nplanes-1 slot — the per-band remap the c2p uses,
 * so the bytes produced are identical to a per-pixel planar_put_stlow loop.
 *
 * The per-pixel form this replaces (qd_planar_bridge_rect / dc_plane_bridge_span)
 * measured ~450 cycles/pixel and was 57% of l67ca — MORE than the decode+blit it
 * mirrors. The cost is not the arithmetic, it is doing `nplanes` read-modify-write
 * cycles to MEMORY for every pixel. Here the group's plane words are accumulated
 * in registers and stored once: a fully covered group is `nplanes` word stores
 * for 16 pixels with NO read at all.
 *
 * Unlike planar_span_stlow (solid slot -> constant word) every pixel here has its
 * own index, so the words must actually be gathered — this is a real c2p, just a
 * small one.
 *
 * ★ Same endianness rule as planar_span_stlow: assemble in a `unsigned short`
 * but STORE the two bytes explicitly. A `*(unsigned short *)` store uses the
 * HOST's byte order and is silently wrong on the little-endian machine that runs
 * tests/test_planar_fill.py, while looking perfect on the m68k target.
 */
static inline void planar_c2p_span_stlow(unsigned char *dst, short line_bytes,
                                         short nplanes, short y,
                                         short x0, short x1,
                                         const unsigned char *src,
                                         const unsigned char *lut)
{
	unsigned char *row = dst + (long)y * line_bytes;
	short          x   = x0;

	while (x < x1) {
		short          g   = (short)(x >> 4);
		short          bit = (short)(x & 15);
		short          n   = (short)(16 - bit);
		unsigned short built[16];
		unsigned short mask;
		unsigned char *grp;
		short          i, p;

		if (n > (short)(x1 - x))
			n = (short)(x1 - x);
		grp = row + (long)g * nplanes * 2;

		if (nplanes == 4 && bit == 0 && n == 16) {
			/* The common case on ST-Low: a whole aligned group, four
			 * planes, no mask and no read. Shift-accumulate so the
			 * inner loop needs no variable shift. */
			unsigned short w0 = 0, w1 = 0, w2 = 0, w3 = 0;

			for (i = 0; i < 16; i++) {
				unsigned char s = lut[src[x + i]];
				w0 = (unsigned short)((w0 << 1) | (s & 1));
				w1 = (unsigned short)((w1 << 1) | ((s >> 1) & 1));
				w2 = (unsigned short)((w2 << 1) | ((s >> 2) & 1));
				w3 = (unsigned short)((w3 << 1) | ((s >> 3) & 1));
			}
			grp[0] = (unsigned char)(w0 >> 8); grp[1] = (unsigned char)w0;
			grp[2] = (unsigned char)(w1 >> 8); grp[3] = (unsigned char)w1;
			grp[4] = (unsigned char)(w2 >> 8); grp[5] = (unsigned char)w2;
			grp[6] = (unsigned char)(w3 >> 8); grp[7] = (unsigned char)w3;
			x = (short)(x + 16);
			continue;
		}

		for (p = 0; p < nplanes; p++)
			built[p] = 0;
		for (i = 0; i < n; i++) {
			unsigned char  s = lut[src[x + i]];
			unsigned short b = (unsigned short)(0x8000u >> (bit + i));

			for (p = 0; p < nplanes; p++)
				if ((s >> p) & 1)
					built[p] = (unsigned short)(built[p] | b);
		}
		mask = (unsigned short)(((0xFFFFUL >> bit)
		                       & ~(0xFFFFUL >> (bit + n))) & 0xFFFFUL);
		for (p = 0; p < nplanes; p++) {
			unsigned char *d = grp + p * 2;
			unsigned short v = built[p];

			if (mask == 0xFFFF) {
				d[0] = (unsigned char)(v >> 8);
				d[1] = (unsigned char)v;
			} else {
				unsigned short cur =
					(unsigned short)(((unsigned short)d[0] << 8)
					               | (unsigned short)d[1]);
				cur = (unsigned short)((cur & (unsigned short)~mask)
				                     | (v & mask));
				d[0] = (unsigned char)(cur >> 8);
				d[1] = (unsigned char)cur;
			}
		}
		x = (short)(x + n);
	}
}

/*
 * Blit a 1bpp glyph at (x, y) straight into ST-Low interleaved planes — the
 * plane-store analogue of DrawChar (compat/quickdraw.c). `glyph` is `h` rows of
 * `glyph_stride` bytes each, MSB-first (bit 7 of byte 0 = column 0), so glyph
 * column c lives in glyph[row*glyph_stride + (c>>3)] & (0x80 >> (c&7)) — the same
 * packing as the embedded 8x8 font and a mac_font strike row. A set bit lays down
 * `fg`; a clear bit lays down `bg` when `opaque` (srcCopy), else leaves the pixel
 * (srcOr / transparent text — the engine's default txMode). `fg`/`bg` are already
 * remapped 0..2^nplanes-1 slots. Clipped per pixel to (dst_w x dst_h); a negative
 * x/y is fine. Correctness-first per-pixel form (calls planar_put_stlow); a
 * word-oriented glyph fast path can drop in under the same interface later.
 */
static inline void planar_glyph_stlow(unsigned char *dst, short line_bytes,
                                      short nplanes, short dst_w, short dst_h,
                                      const unsigned char *glyph, short glyph_stride,
                                      short x, short y, short w, short h,
                                      unsigned char fg, unsigned char bg,
                                      short opaque)
{
	short row, col;

	for (row = 0; row < h; row++) {
		short yy = (short)(y + row);
		const unsigned char *grow = glyph + (long)row * glyph_stride;
		if (yy < 0 || yy >= dst_h)
			continue;
		for (col = 0; col < w; col++) {
			short xx = (short)(x + col);
			unsigned char bit;
			if (xx < 0 || xx >= dst_w)
				continue;
			bit = (unsigned char)(grow[col >> 3] & (0x80u >> (col & 7)));
			if (bit)
				planar_put_stlow(dst, line_bytes, nplanes, xx, yy, fg);
			else if (opaque)
				planar_put_stlow(dst, line_bytes, nplanes, xx, yy, bg);
		}
	}
}

#endif /* PLATFORM_PLANAR_H */
