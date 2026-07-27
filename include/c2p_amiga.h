/*
 * c2p_amiga.h — chunky-to-planar for SEPARATE-plane bitmaps (Amiga layout).
 *
 * Each bitplane is its own contiguous bitmap of `plane_pitch` bytes per row;
 * `planes[p]` points at plane p. This is the Amiga OCS/ECS/AGA arrangement.
 * For the Atari ST/TT's WORD-INTERLEAVED layout use c2p4st.h instead — the
 * bit transpose is shared (c2p32.h), only the scatter differs.
 *
 * The chunky source is one byte per pixel, the byte being the colour index.
 * Indices must already be in range for the plane count: the transpose always
 * computes all 8 plane outputs (it is no cheaper to compute fewer), and the
 * n-plane forms simply do not store the high ones, so an index of 40 with
 * nplanes = 5 will lose its high bits rather than be clamped.
 *
 * Alignment: `x0` and `w` in the rect forms must be multiples of 8, since a
 * bitplane row is addressed in bytes. Callers align outward.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef C2P_AMIGA_H
#define C2P_AMIGA_H

/* Whole-surface, 8 planes. */
void c2p_amiga(const unsigned char *chunky, unsigned char *const planes[8],
               short w, short h, short plane_pitch);

/* Sub-rectangle, 8 planes. x0 and w must be multiples of 8. */
void c2p_amiga_rect(const unsigned char *chunky, short chunky_pitch,
                    unsigned char *const planes[8], short plane_pitch,
                    short x0, short y0, short w, short h);

/* Whole-surface, `nplanes` planes (ECS uses 5). */
void c2p_amiga_n(const unsigned char *chunky, unsigned char *const planes[],
                 short w, short h, short plane_pitch, short nplanes);

/* Sub-rectangle, `nplanes` planes. x0 and w must be multiples of 8. */
void c2p_amiga_n_rect(const unsigned char *chunky, short chunky_pitch,
                      unsigned char *const planes[], short plane_pitch,
                      short x0, short y0, short w, short h, short nplanes);

#endif /* C2P_AMIGA_H */
