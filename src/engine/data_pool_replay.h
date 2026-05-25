/*
 * THINK C DATA + DREL replay — engine startup.
 *
 * data_pool_replay() copies the FRUA application's DATA resource into
 * a 32 KB below-A5 buffer and applies the DREL relocation table on
 * top of it. Lifted globals can read against the buffer via
 * g_a5_at(offset) using their A5-relative offset.
 *
 * Call once at startup, after the Resource Manager has the FRUA
 * archive open (data_pool_replay reads STRS via ua_strs_at for the
 * A4 base address).
 */

#ifndef ENGINE_DATA_POOL_REPLAY_H
#define ENGINE_DATA_POOL_REPLAY_H

void  data_pool_replay(void);

/* Size of the below-A5 buffer. Covers the deepest DREL reloc the
 * FRUA build references (-20096) with comfortable headroom for the
 * BSS portion above it (g_a5_27932 / 28006 / etc.). */
#define A5_BELOW_SIZE 32768

/* The below-A5 byte buffer. byte 0 sits at the deepest address; A5
 * conceptually points just past byte A5_BELOW_SIZE - 1. Lifts that
 * model an A5-relative global as a buffer slot reach it via the
 * `g_a5_byte(off)` macro below. */
extern unsigned char g_a5_below[A5_BELOW_SIZE];

/* L-value accessor: g_a5_byte(-N) → the byte at A5 - N inside the
 * buffer. `&g_a5_byte(-N)` returns the address (for array-style
 * walks). */
#define g_a5_byte(off) (g_a5_below[A5_BELOW_SIZE + (int)(off)])

/* Base of the below-A5 region — the byte address conceptual A5
 * points just past (so g_a5_below_base() returns &buffer[size]). */
void *g_a5_below_base(void);

/* Pointer to byte at A5 + offset (offset is signed: negative for the
 * data + bss area we replay into, positive for the above-A5 region
 * which we haven't set up yet — those return NULL). */
void *g_a5_at(int offset);

#endif /* ENGINE_DATA_POOL_REPLAY_H */
