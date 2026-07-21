/*
 * Load the shared game-rule tables from the user's DOS CKIT.EXE — ADR-0017.
 *
 * A slice of the A5 world is neither a relocation nor a functional constant:
 * it is game-rule data (the big tables at A5-28940, A5-23184, A5-22956 and
 * friends). FRUA is the same game on both releases, so those tables are
 * byte-identical in the DOS executable — 1695 bytes of them, measured. Rather
 * than compile them in (copyrighted, and it would make the binary
 * non-redistributable), the port ships only their POSITIONS and reads the
 * bytes from the user's own copy, exactly as tools/art_convert.py takes their
 * own art and the string map takes their own text.
 *
 * A Mac-only user has no CKIT.EXE, and does not need one: their frua.rsc
 * carries the DATA resource and the replay supplies these slots. Absence is
 * therefore silent and non-fatal — this only fills slots still left at zero.
 *
 * VERIFICATION (ADR-0017 decision #5). Offsets are valid only for the build
 * they were derived from; a repack shifts every one of them and reading from
 * the wrong place would quietly poison the A5 world with plausible-looking
 * garbage — the worst failure mode this project has, because a zeroed or
 * wrong A5 slot yields a correctly-executed WRONG path rather than a crash
 * (see #67, the copy-protection hang). So every run is checksummed before it
 * is applied, and a mismatch refuses the whole file loudly instead of
 * half-filling the world.
 */
#include <string.h>

#include "a4_map.h"
#include "data_pool_replay.h"   /* g_a5_byte */
#include "dbglog.h"
#include "dos_scalars.h"
#include "files.h"              /* FSOpen / FSRead / SetFPos / FSClose */

/* Largest single run in the table (A5-28940 is 782 bytes); sized with headroom
 * so a regenerated map does not silently overflow. Checked below regardless. */
#define DOS_RUN_MAX 1024

int dos_scalars_load(void)
{
	unsigned char buf[DOS_RUN_MAX];
	short  refnum = 0;
	short  i, applied = 0;
	OSErr  err;

	if (g_a5_dos_scalar_count == 0)
		return 0;

	err = FSOpen("\011CKIT.EXE", 0, &refnum);
	if (err != noErr)
		return 0;               /* Mac-only install: expected, silent */

	/* Pass 1 — verify every run before applying any of them. */
	for (i = 0; i < g_a5_dos_scalar_count; i++) {
		const struct a4_dos_run *r = &g_a5_dos_scalars[i];
		long           count = r->len;
		unsigned short sum = 0;
		short          k;

		if (r->len <= 0 || r->len > DOS_RUN_MAX) {
			dbg_log("dos_scalars: run out of range — refusing");
			(void)FSClose(refnum);
			return -1;
		}
		if (SetFPos(refnum, fsFromStart, r->off) != noErr ||
		    FSRead(refnum, &count, buf) != noErr || count != r->len) {
			dbg_log("dos_scalars: short read — refusing (wrong build?)");
			(void)FSClose(refnum);
			return -1;
		}
		for (k = 0; k < r->len; k++)
			sum = (unsigned short)(sum + buf[k]);
		if (sum != r->sum) {
			dbg_log_num("dos_scalars: checksum mismatch at run ", i);
			dbg_log("dos_scalars: CKIT.EXE does not match the map — "
			        "refusing (see docs/dos-strings-probe.md)");
			(void)FSClose(refnum);
			return -1;
		}
	}

	/* Pass 2 — apply, PER BYTE, only into slots still zero. Runs are no
	 * longer zero-free: the map generator re-fuses zero-split tables into
	 * whole regions (coalesce_runs_by_dos, #75), so a run may contain
	 * interior zeros and may overlap bytes the replay or the authored
	 * scalars already supplied — those existing bytes always win. */
	for (i = 0; i < g_a5_dos_scalar_count; i++) {
		const struct a4_dos_run *r = &g_a5_dos_scalars[i];
		long  count = r->len;
		short k, wrote = 0;

		if (SetFPos(refnum, fsFromStart, r->off) != noErr ||
		    FSRead(refnum, &count, buf) != noErr || count != r->len)
			continue;           /* verified above; be defensive anyway */
		for (k = 0; k < r->len; k++) {
			if (g_a5_byte(r->slot + k) == 0 && buf[k] != 0) {
				g_a5_byte(r->slot + k) = buf[k];
				wrote = 1;
			}
		}
		applied += wrote;
	}

	(void)FSClose(refnum);
	dbg_log_num("dos_scalars: runs applied from CKIT.EXE = ", applied);
	return applied;
}
