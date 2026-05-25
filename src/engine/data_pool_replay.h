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

/* Base of the below-A5 region — the byte address conceptual A5
 * points just past (so g_a5_below_base() returns &buffer[size]). */
void *g_a5_below_base(void);

/* Pointer to byte at A5 + offset (offset is signed: negative for the
 * data + bss area we replay into, positive for the above-A5 region
 * which we haven't set up yet — those return NULL). */
void *g_a5_at(int offset);

#endif /* ENGINE_DATA_POOL_REPLAY_H */
