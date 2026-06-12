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

/*
 * Load every GEOnnn (1..40) from the staged design in turn and draw
 * each present map as a thumbnail in a grid — a contact sheet to
 * confirm the tile layout holds across a whole design. Like
 * port_render_geo_map, a bring-up aid that paints straight to the
 * back buffer.
 */
void port_render_geo_contact(void);

/*
 * Load the real TOPVIEW.TLB tile library and rasterize its 16x16
 * 1bpp top-down map tiles into a grid via a from-scratch GLIB glyph
 * blit — proves the tile-art path renders real game graphics.
 */
void port_render_topview(void);

/*
 * Draw the loaded GEO map with real TOPVIEW tiles: each cell is
 * rendered as the automap tile for its wall combination
 * (tile 1 + N|E|S|W mask) — the GEO map as the game's own top-down
 * view instead of coloured cells.
 */
void port_render_geo_tiles(void);

/*
 * Interactive play-loop demo: enter level 1, then walk the party
 * around the loaded map on the automap (w/s/a/d move, q quit) — the
 * runtime's render-input-move-render cycle.
 */
void port_play_demo(void);

/*
 * Exercise the jt995 bit-packed-page blit foundation: OR-blit a real
 * 1bpp wall tile into the bit-packed page at a run of sub-word offsets,
 * then convert the page to the 8-bit screen.
 */
void port_blit_demo(void);

/*
 * Drive jt200 (JT[200]) — the per-slot wall-tile selector — against the
 * real DUNGCOM wall set: prove the faithful (wall code -> group/position
 * -> tile index -> 1:1 blit) path draws real pre-rendered wall art.
 */
void port_wall_demo(void);

/*
 * Drive jt199 (JT[199]) — the first-person frustum walker — over the
 * loaded design's GEO map: render the dungeon view from the party cell.
 */
void port_view_demo(void);

/*
 * Render FRUA's colour art (the mode-1 4bpp/8bpp "C"-file tiles —
 * CBODY paperdolls, COMSPR combat sprites, CPIC creatures) through the
 * game's 256-colour clut 129. The colour-sprite path the 1bpp dungeon
 * walls don't exercise.
 */
void port_sprite_demo(void);

/*
 * Stand up the GLIB FAR pool (the lifted JT[463] / _LBOpen).  The Mac
 * calls it from jt1079/master_init right beside the file-cache init;
 * master.c calls this wrapper at the same spot so the faithful
 * jt997/jt1014 library loaders have a live pool from boot.
 */
void glib_pool_open(void);

#endif /* ENGINE_BOOT_H */
