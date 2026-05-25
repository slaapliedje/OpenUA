/*
 * THINK C DATA + DREL replay — engine startup.
 *
 * The Mac runtime, on launch, sets up the A5 world by:
 *   1. allocating the below-A5 region (data + bss, sized via CODE 0
 *      header bytes),
 *   2. zero-filling it,
 *   3. copying the DATA resource into the top of that region (DATA
 *      ends one byte before A5),
 *   4. walking the DREL resource, adding the A5 base address to each
 *      A5-base pointer slot and the A4 base address (the STRS pool)
 *      to each A4-base pointer slot.
 *
 * `data_pool_replay()` reproduces that flow on the Atari side. The
 * below-A5 region lives in a statically-allocated 32 KB buffer
 * (slightly above the deepest reloc the FRUA build references at
 * -20096); A5 is conceptually the address just past the last byte
 * (`g_a5_below + A5_BELOW_SIZE`). All offsets in the reloc table are
 * negative relative to that address.
 *
 * Lifted globals are still scattered file-level statics across boot.c;
 * the replay populates a SEPARATE buffer accessible via `g_a5_at()`.
 * Migrating individual lifts to read from the replay buffer is a
 * gradual job — the replay infrastructure landing first lets future
 * lifts pull initial values from FRUA's actual DATA pool one cluster
 * at a time.
 */

#include <stddef.h>
#include <string.h>

#include "data_pool.h"
#include "data_pool_replay.h"
#include "str.h"             /* ua_strs_at */
#include "dbglog.h"

#define A5_BELOW_SIZE   32768

static unsigned char g_a5_below[A5_BELOW_SIZE];

/* The conceptual A5 base — the byte address just past the last byte
 * of g_a5_below. Reads `g_a5_at(-N)` resolve to g_a5_below[A5_BELOW_SIZE
 * - N]. */
void *g_a5_below_base(void)
{
	return (void *)(g_a5_below + A5_BELOW_SIZE);
}

void *g_a5_at(int offset)
{
	long pos = (long)A5_BELOW_SIZE + offset;

	if (pos < 0 || pos >= (long)A5_BELOW_SIZE)
		return NULL;
	return (void *)(g_a5_below + pos);
}

void data_pool_replay(void)
{
	int   i;
	void *strs_base;

	/* Step 1 — zero the buffer (matches the Mac's BSS init). */
	memset(g_a5_below, 0, sizeof g_a5_below);

	if (G_A5_INIT_BYTES_LEN == 0 || G_A5_RELOCS_COUNT == 0) {
		dbg_log("data_pool: stub — no replay");
		return;
	}

	/* Step 2 — copy DATA into the top of the buffer so the last byte
	 * of DATA sits one byte before A5 (i.e. at g_a5_below[A5_BELOW_SIZE
	 * - 1]). */
	memcpy(g_a5_below + A5_BELOW_SIZE - G_A5_INIT_BYTES_LEN,
	       g_a5_init_bytes, G_A5_INIT_BYTES_LEN);

	/* Step 3 — apply relocations. */
	strs_base = (void *)ua_strs_at(0);
	for (i = 0; i < G_A5_RELOCS_COUNT; i++) {
		short  off  = g_a5_relocs[i].a5_offset;
		long   pos  = (long)A5_BELOW_SIZE + off;
		long  *slot;
		long   base = 0;

		if (pos < 0 || pos + 4 > (long)A5_BELOW_SIZE)
			continue;
		slot = (long *)(g_a5_below + pos);
		switch (g_a5_relocs[i].base) {
		case G_A5_RELOC_A5:
			base = (long)(unsigned long)g_a5_below_base();
			break;
		case G_A5_RELOC_A4:
			base = (long)(unsigned long)strs_base;
			break;
		default:
			continue;
		}
		*slot += base;
	}
	dbg_log_num("data_pool: replayed bytes = ",
	            (long)G_A5_INIT_BYTES_LEN);
	dbg_log_num("data_pool: relocs applied = ",
	            (long)G_A5_RELOCS_COUNT);
}
