# TODO

Working notes on what's next. Ratified architecture decisions live in
`docs/decisions.md`; this file is the rolling task list.

## 3D dungeon view

Done (data-driven from the loaded map):

- Per-edge wall sets — each face draws its level's Wall1-3 group
  (`wallset_for_id`, `8X8DB`/`8X8DC`, 3 clut bands).
- Per-edge facets — wall/window/door/brazier/fireplace overlays on the
  front and side faces (`g_cw_facet_piece`, bearings, transparency key).
- Per-cell floor/ceiling/sky backdrop from `BACK.CTL`
  (`cell_backdrop_id`, the level's Backdrop1-4 zones).
- Double-buffered VIDEL present — c2p into the hidden buffer, flip at
  vsync (no more tear-on-move).

### Current focus: faithful raycaster (1:1 port)

The active renderer is `render_3d_view` — a perspective-trapezoid
*reconstruction*, not the Mac engine's real view. Replace it with the
faithful frustum walker so the port is 1:1 with the original:

- `jt199` (CODE 7 +0x6234) is lifted — `jt199_side` / `jt199_front` walk
  the four ray passes and call `l5b42` to place each visible wall slot.
- `l5b42` / `jt200` / `jt200_layer` place + blit a pre-rendered slot
  tile 1:1 (no scale loop), at the screen positions held in the read-only
  DATA layout globals `g_a5_-12202..-12240`.

Sub-tasks to make it the real, colour view:

1. **Fix `l5b42` screen positioning.** The lift has a suspected X/Y swap
   and the `((v-8012)<<2)+8` coord math overshoots the clip rect (see the
   long comment on `l5b42`). Needs runtime instrumentation — log the
   actual output coords for a known cell and reconcile against the Mac.
2. **Wire the colour slot pieces.** `jt200_layer` currently blits only the
   DUNGCOM 1bpp set (group 2); the 3D walls are `8X8DC`/`8X8DB`. Load the
   full perspective piece set (all 48, not just the 5 facet near-pieces
   the trapezoid path uses) and blit the per-slot piece at `(top,left)`
   1:1 with the per-set palette band.
3. **Replace `render_3d_view`** in `jt312` with the `jt199` path once the
   geometry + colour are correct. Keep the trapezoid renderer behind a
   build flag during bring-up.

## Future additions

Out of scope for the 1:1 port — revisit once the faithful engine is
solid:

- **Smooth-transition movement engine.** The original (and the faithful
  port) is instant grid-step: 90° turns and cell-to-cell jumps. A later
  optional mode could interpolate the view between cells/facings (slide
  forward, rotate turns) for a smoother feel — a deviation from the
  original, so gated behind a setting, built on top of the faithful
  renderer rather than replacing it.
- The trapezoid `render_3d_view` may stay as a fast/low-spec fallback.
