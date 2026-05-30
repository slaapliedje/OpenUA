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
 * Visualize the currently-loaded GEO map (design-state g_a5_-12300,
 * the 'MAP ' chunk at +290) as a width x height grid of 8-bit cells
 * painted straight into the display back buffer — open tiles vs wall
 * tiles — then present. A bring-up aid for the content loaders; does
 * nothing if no map is loaded or no screen is attached.
 */
void port_render_geo_map(void);

#endif /* ENGINE_BOOT_H */
