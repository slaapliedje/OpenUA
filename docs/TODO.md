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
- The walk fires real slots (7 in a corridor vs 0 before). Two transposes
  fixed: cell indexing (pass row=partyY,col=partyX) and the screen axes
  (l5b42's `top` is X, `left` is Y — `soff` spreads on `top`). Slots now
  spread horizontally.
- DISASSEMBLY FINDINGS (CODE 7 L6234, re-verified):
  * `l5b42` adds `ydelta+soff` to arg1 (`top` = Mac Rect Y) and `xdelta` to
    arg2 (`left` = X). So Mac convention `top=Y, left=X` — the original lift
    naming was right (an X/Y swap experiment was reverted).
  * `jt199` is MULTI-SCAN: the JT[3] selector is constant 2, but case 2
    (L63a2) is a leftward lateral scan that FALLS THROUGH to a rightward
    scan (L6556, `yadj=-1` vs the left scan's `+1`), and presumably the
    front passes after. The lift's 4-call decomposition is roughly right.
  * Each scan varies only `soff` (on the Y/top axis) while `xdelta` is
    constant (-12202/-12220 = 4 -> X = 4*16+24 = 88). So a scan lays its
    pieces along a near-vertical line at X~88; the *perspective* must come
    from the PRE-SIZED pieces (jt200's `sub`/idx picking smaller far tiles),
    EOB-style — not from the anchor moving in 2D.
- LIFT VERIFIED FAITHFUL (full asm re-read, L6234..L67e2): the four scans
  (side L/R `L63a2`/`L6556`, front L/R `L66f2`/`L67e2`) match jt199_side/
  jt199_front exactly — same globals (-12222/-12240/-12202/-12220 side;
  -12238/-12218 front-L; -12236/-12216 front-R), same sub (9/0 side, 1/2
  front), same yadj. The JT[3] view selector is a literal `moveq #2`
  (L636a) — constant 2, so the 4-call decomposition is complete (no outer
  column fan). jt200's index math (L59d4) matches the lift too (peel-fives,
  sub++, ==10 wrap, left+=4 step, code*10+sub+1 / code*9+sub+2). So the
  walk + jt200 code logic is NOT the bug.

- ROOT CAUSE FOUND (2026-06-01, disassembly only — no emulator needed):
  the faithful path blits RAW GLIB items, but the real wall tiles are
  **synthesized at load time** and my `load_cw_full` reads the unbuilt
  placeholders. The chain:
  * jt200 -> JT[114] (CODE6+0x3804) -> JT[1001] (CODE5+0x31ac) -> L309c.
    JT[1001] is just JT[468]+L309c, so the blit + idx semantics are
    identical to the group-2 path; the ONLY difference is the *handle
    content*. The handle is `g_a5_-27894[group]` (Wall1/2/3).
  * That handle is populated by **L6148 -> L6eea** (the per-frame wall-set
    loader my jt199 lift skipped as "cosmetic"). L6eea: builds "8x8d%c%d",
    JT[110] loads the GLIB into the handle slot, then LOOPS item indices
    `4,7,10,14,17,...,47` and for each whose byte size is `<16` (an EMPTY
    placeholder, measured by JT[1015]=CODE5+0x3834, which returns
    `offset[idx+1]-offset[idx]`) calls **JT[111]** (CODE6+0x3b1e).
  * JT[111] is the tile GENERATOR: it sizes the source item `idx-1`
    (JT[1015]), (re)allocates the dest item `idx` (JT[1022]=CODE5+0x46a6),
    then runs **JT[1012]** (CODE5+0x37aa) — the pixel transform that builds
    the larger perspective tile `idx` from its neighbor `idx-1`. So the set
    ships only some sizes (items 1,2=8x8 ... 8=56x56, 9,10=88x16 per
    10-item facet band) and DERIVES the rest at load. Reading the raw CTL
    gives placeholders for exactly the near/big indices → the render shows
    only the small far tiles. This is the whole discrepancy; the jt199/
    jt200 lift was correct all along. (Supersedes the earlier "needs a
    runtime jt200-args capture" plan — static analysis fully explains it.)

- NEXT (the lift): port the L6eea tile-synthesis pipeline so the faithful
  path draws built tiles. Pieces to lift: JT[110] (load), JT[1015] (item
  byte size, easy off l37aa offsets), JT[1022] (GLIB item realloc),
  JT[111] (generate dst from src), and the core JT[1012] (CODE5+0x37aa,
  the scale/derive pixel op). Then `load_cw_full` runs the same 4,7,10..47
  synthesis pass before the render reads g_cwf_body[idx].
- Meanwhile render_3d_raycast (visibility-faithful, on-screen, looks right)
  is the working demo renderer; the pixel-exact jt199 path is in progress.
- Per-group walls: `render_3d_faithful` loads ONE set (the level's Wall1)
  for all faces; give each Wall1-3 group its own piece store for true
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
