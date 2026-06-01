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

UPDATE 2026-06-01 — UNBLOCKED by mon capture. Ran FRUA under the
mon-enabled BasiliskII (`docs/mac-emulator.md`) and dumped the live
globals: `CurrentA5` confirmed `0x01F74AC0`, and the layout table
`g_a5_-12240..-12198` (16-bit words) is **5 4 6 4 2 7 2 0 / 9 5 4 3 3 3 1 1
/ 1 0 0 4 0 0** — tiny values, NOT the 175/516/250 the static DATA image
held (a launch-time init overwrites them). The side xdelta `-12202` is
**4**, so `l5b42`'s deep transform `((8016+4·4)−8012)·4+8 = 88` lands
on-screen. So the static-DATA off-screen result was the red herring; the
faithful pixel path IS reconstructible with the real coords.
DONE: seeded the captured values in `boot_a5_seed_defaults` and confirmed
identical inside the live 3D view (second mon capture). Wired the faithful
colour path — `render_3d_faithful` (behind `FRUA_FAITHFUL`) loads the
active set's 48 pieces (`load_cw_full`) + palette band, and `jt199`'s walk
→ `l5b42` (real coords) → `jt200_layer` → `cw_blit_piece` blits the
pre-sized colour pieces 1:1 on screen. Fixed a row/col transpose (pass
`row=partyY, col=partyX` so `l5b42`'s `cell=col*h+row` matches the map).

WIP / next iterations:
- The walk now fires real slots (e.g. 7 in a corridor vs 0 before) and they
  land at the captured coords, but only the far/small front pieces show —
  the near side walls (big pieces) aren't filling yet. Tune the `jt199_side`
  side-face draws + `soff` stepping / depth coverage so the full frustum
  renders.
- Per-group walls: `render_3d_faithful` loads ONE set (the level's Wall1)
  for all faces; give each Wall1-3 group its own 48-piece store for true
  per-edge faithful walls.
- Strip the `g_cwf_blits` debug logging once the layout is correct.

### Pivot: faithful WALK + our texture renderer

The faithful part that IS sound: `jt199`'s frustum-walk logic + `l5e52`
wall probes (which walls are visible at each depth/side). Plan: drive the
colour render from that faithful visibility, but place slots with an
on-screen coordinate model (the viewport geometry render_3d_view already
uses) instead of the un-reconstructible `l5b42` pixel coords. That is "the
raycaster mixed with the textures" — authentic visibility, working visuals.

- [x] Port `jt199`'s frustum visibility — `render_3d_raycast` walks the
      wider 3-column (left/center/right) field, back-to-front, gating side
      columns on a clear line of sight (the corridor wall toward them being
      open), so it matches the old corridor view on straight halls and opens
      up at junctions/rooms. Now the default; `FRUA_CORRIDOR` selects the old
      `render_3d_view`. Renders with the per-edge wall + facet system at
      viewport-derived positions.
- [ ] Keep `l5b42`/`jt200` lifts in place (faithful, documented) for if/when
      the runtime layout state is ever recovered.

**Unblock route (runtime capture):** a BasiliskII Mac emulator is set up.
Running the real FRUA there and dumping the A5 slot-layout globals
(`g_a5_-12240..-12196`) at the moment `jt199` renders would give the real
on-screen coords and make the pixel-1:1 `l5b42` path renderable. (Needs the
game's copy-protection answers from the manual; not a porting blocker.)

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
