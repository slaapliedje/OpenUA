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

FINDING (2026-06-01, byte-exact re-trace of CODE 7): the faithful PIXEL
pipeline is **blocked** by layout-global state we can't reconstruct.
Confirmed against the asm:

- `jt199` (L6234) deep clip rect is `((v-8012)<<2)+8` off the (8012,8016)
  anchor → the on-screen viewport is *small* deltas (screen =
  `delta_lowbyte<<4 + 8`, so the layout-global low bytes must be ~0-12).
- The side-wall pass (L63a2 case 2) passes `l5b42(8012, 8016,
  ydelta=g_a5_-12222+soff, xdelta=g_a5_-12202, …)` — the lift is faithful.
- `l5b42` (L5b42) reads each delta's **low byte** (signed). `-12222=516`
  → low byte 4 → on-screen; but `-12202=175` → low byte −81 → screen
  coord ≈ −1272, **off-screen**, on either axis (the X/Y-swap reading
  doesn't rescue it).
- `-12202` is **read-only across all 23 CODE segments** (no view-init
  writes it), so 175 is its permanent value. There is no recoverable
  state that maps the side walls on-screen.

So a literal 1:1 *pixel* raycaster render isn't reconstructible from the
decompilation data alone — the Mac sets up the slot-layout coordinates
in a way our static image doesn't capture (would need real-hardware /
emulator runtime capture of `g_a5_-12240..-12196` at render time).

### Pivot: faithful WALK + our texture renderer

The faithful part that IS sound: `jt199`'s frustum-walk logic + `l5e52`
wall probes (which walls are visible at each depth/side). Plan: drive the
colour render from that faithful visibility, but place slots with an
on-screen coordinate model (the viewport geometry render_3d_view already
uses) instead of the un-reconstructible `l5b42` pixel coords. That is "the
raycaster mixed with the textures" — authentic visibility, working visuals.

- [ ] Port `jt199`'s four-pass walk to emit visible (depth, side, wall id)
      slots, replacing render_3d_view's simpler 4-slice probing.
- [ ] Render each slot with the existing per-edge wall + facet system at
      viewport-derived positions.
- [ ] Keep `l5b42`/`jt200` lifts in place (faithful, documented) for if/when
      the runtime layout state is ever recovered.

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
