# Dungeon first-person view — stand-in audit (2026-06-13)

User directive: the 3D view must use NO stand-ins. DUNGCOM is COMBAT art,
never the view (see [[dungcom-is-combat-only]]).

## The FAITHFUL render chain (keep)
- jt221 / jt312 -> render_3d_faithful -> jt199 -> jt199_side/band -> l5b42
  -> jt200 -> jt200_layer -> jt114 -> **l309c_tile**.
- Texture PIXELS: l6148 -> l6eea(ds[4]/ds[5]/ds[6]) loads the design's wall
  sets from **8X8DB.CTL / 8X8DC.CTL** into the -27894/-27890/-27886 handles.
- Palette / transparency: load_wall_groups(ds) -> cw_load_slot fills the
  g_cw_* slots; cw_finalize installs the clut bands 32/64/96 + floor/ceiling
  (4/5). l309c_tile rebases each tile's 32-based bytes into the slot's band.
- Cell data: l7226 (GEO parse, MAP chunk -> ds+290); l5e52 (movement/low
  nibble) / jt212 (automap/high nibble) edge reads. ALL asm-faithful.

## STAND-INS — status

| stand-in | where | status |
|---|---|---|
| DUNGCOM.TLB -> g_wall_bmp | dungeon_view_setup | **REMOVED** (was dead — never read by the render) |
| hand-rolled clut 0-15 ramp (automap 1/2/3, depth 8-15) | dungeon_view_setup | **REMOVED** |
| load_color_wallset(g_cw_set) default-set load | dungeon_view_setup | **REMOVED** — replaced by load_wall_groups(ds) (the design's real sets) |
| g_wall_bmp / g_wall_metric / g_wall_n / WALL_NTILES table | global + demo loader ~L13581 | dead; only the FRUA demo (port_view_demo) writes it now — DELETE with the demo |
| render_3d_view / render_3d_raycast (+ FRUA_CORRIDOR/RAYCAST) | ~L9077/L9215 | dead alt-renderers (flags not defined); render_3d_faithful is live — DELETE later |
| g_cw_set / g_cw_file 't'/'y' manual cycle | input demo | debug-only override; harmless, keep behind the keys |

## RESULT
dungeon_view_setup now does ONLY: load_wall_groups(ds[4..6]) (faithful).
No DUNGCOM, no default set, no hand-rolled palette. Runs before the entry
render so l309c_tile's palette rebase has the slots loaded (fixes the
entry-frame wrong-palette: l6148 loaded pixels but the g_cw_* palette had
not, so the rebase used an unloaded slot).

## NOT YET VISUALLY CONFIRMED
The HEIRS start view still can't be eyeballed clean because the merchant
intro (a) renders its event PICTURE in the view pane with the wrong CLUT
(task #125) and (b) the text is very slow + the harness drops keys, so the
intro won't clear to the playable walls. Fix #125 + the text speed to SEE
the stone walls. Then diff the port jt200 slots vs the Mac trace (#126).
