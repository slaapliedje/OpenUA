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

#endif /* PLATFORM_PLANAR_H */
