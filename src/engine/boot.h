/*
 * FRUA application entry point — see boot.c.
 *
 * Lifted from CODE 6 + 0x58a (jump-table entry 12): the target the THINK C
 * runtime hands off to once the A5/A4 world is built — FRUA's own main().
 */

#ifndef ENGINE_BOOT_H
#define ENGINE_BOOT_H

/*
 * FRUA's bootstrap. In the Mac build the THINK C runtime calls this as
 * JT[12]; arg1 / arg2 are the two values it passes — their meaning is still
 * to be traced. Returns 0.
 */
int ua_main(short arg1, long arg2);

/*
 * Seed the A5-world defaults that aren't covered by data_pool_replay's
 * DATA blit — the DLItem capacity (g_a5_9288 = 64) and the manager
 * active flag (g_a5_9248 = 1), plus any future non-zero scalar living
 * in the BSS portion of the A5 world. Idempotent; safe to call after
 * data_pool_replay() whether the replay loaded real DATA or stubbed.
 */
void boot_a5_seed_defaults(void);

/*
 * Stand up the GLIB FAR pool (the lifted JT[463] / _LBOpen). The Mac
 * calls it from jt1079/master_init, sizing the pool from free memory
 * between kb_min and kb_max KB (214/450 on the normal path); master.c
 * forwards the same bounds so the faithful jt997/jt1014 library loaders
 * have a live, Mac-sized pool from boot. glib_pool_close is the lifted
 * JT[466] / FCCleanup teardown (dispose the master buffer).
 */
void glib_pool_open(short kb_min, short kb_max);
void glib_pool_close(void);

#endif /* ENGINE_BOOT_H */
