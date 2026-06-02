# TODO

Working notes on what's next. Ratified architecture decisions live in
`docs/decisions.md`; this file is the rolling task list.

## Boot UI / menus

- DONE: main menu renders (`jt315`, CODE 22 + 0x4d8a). Builds the ten
  DLItem buttons (jt447/jt452), paints (l2c60 -> jt382 DrawString), draws
  the "Current Game Design:" banner (jt94), runs the dialog event loop
  (jt453). Full screen of UI matching the Mac; Quit-confirm dialog works.
- DONE: per-selection dispatch. jt315 loops + dispatches on the selected
  DLItem index (g_mainmenu order). "Play the Game" (0) -> port_play_demo
  (load level, place party, render jt312, walk WASD, back on 'q'); "Quit
  From Game" (9) -> return 0. Verified in Hatari (-DDEMO_LEVEL=2): boot ->
  menu -> 'P' -> the 3D dungeon corridor. Selection works via the item
  hotkey (jt382 hit-test + l1676 commit -> l2d3e returns the index).
- DONE: faithful Play path to the Training Hall. jt315 Play -> return 1 ->
  ua_main l07dc -> jt918 -> l0aae renders the party-management menu (Add/
  Remove/Modify/Train/View Character, Human Change Class, Create/Delete,
  Load/Save, Begin Adventuring, Exit From Play). l0aae was lifted earlier
  but painted via the jt449 stub + never presented; fixed (clear + l2c60 +
  qd_present, like jt315). "Begin Adventuring" (case 9 / l1142) bridges to
  port_play_demo -> the 3D dungeon (faithful jt585/CODE15-19 chain is still
  stubbed). Full chain verified: menu -> P -> Training Hall -> B -> dungeon.
- PARTIAL: the roster/Adventure menu renders. "Add Character" -> jt904 ->
  jt182 (Add/Modify/Delete roster popup) now displays on a clean backdrop
  (jt155 value list + l206e/l23b4/l25b6 over the l2d3e pump were lifted;
  added the clear+present prime). CHARACTER CREATION + A POPULATED PARTY are
  a LARGE subsystem, NOT done:
  * Create/Add character -> CODE 17 char-gen (~10k lines of asm, barely
    lifted): JT[557] create, JT[574] train, JT[556], JT[560], etc. — all
    PROBE stubs. This is the multi-session piece.
  * Party data g_a5_-5806 is NULL until a real roster block exists; the
    character record format (base+76 party slots, base+198.. roster flags,
    base[94]/[147]/[382] fields read by jt904) needs RE to seed a test party.
  * DONE (i): seeded a test party. Decoded the roster data model from l02dc
    — linked list off g_a5_-27928 (next@+0), name@+96, HP@+385, AC@+395.
    port_test_seed_design seeds 3 characters (Bramble/Korin Vale/Sable);
    lifted jt25 (row name paint) so all entries show; moved the backdrop
    clear to jt918's loop top so the menu no longer wipes the roster.
    Training Hall now lists the party. REMAINING POLISH: lift jt32/jt34
    (AC/HP number paint, still stubs) and fix the "Name"/"AC HP" header
    column positions (jt94 col mapping). The party is a static stand-in —
    replace with real created/loaded characters once CODE 17 lands.
  * STARTED (ii): CODE 17 char-gen. Lifted L35f8 (the PICK RACE/ALIGNMENT/
    GENDER/CLASS headers via jt1089) + L3666 (char-gen screen init skeleton:
    dims, draw headers, seed wizard step g_a5_-7018) + jt574 (case-0 entry
    shows the screen). The big remainder: the ability-score roll (L34f0 over
    the race/class tables at g_a5_-30450) + the jt568 per-step pick state
    machine (race/class/gender/alignment selection + the created record).
    BLOCKED on visual confirm by the present issue below.

## Initial-screen texture (GEN backdrop) — investigated, NOT done

The Mac main menu's textured background is drawn by JT[81] (CODE 6 + 0x6a10):
it loads the "gen" tile library and blits backdrop tiles (idx 1,2,3, +4 in
deep mode) at (8000,8000) via JT[1001]/L309c, then disposes the handle.
Two blockers found:
- JT[110] (CODE 6 + 0x33ac), the NAMED-GLIB loader JT[81] uses to load
  "gen", is NOT lifted. (It's reusable — loads any "<name>.ctl/.tlb" — worth
  lifting on its own.)
- GEN.CTL has a NON-STANDARD tile format, unlike the 8X8 wall sets. Outer
  GLIB = 2 entries: item 0 looks like an RGB palette band (flags 0xc8),
  item 1 a colour image (flags 0xc2, ybear -110, metric[6]=0x28). Decoding
  item 1 as width=8*metric[6]=320 x h=90 gives a 28 800-byte body that does
  NOT fit the 26 818-byte file, so that width is wrong; a direct-blit
  attempt produced magenta/colour NOISE (wrong width/stride + the
  transparency key not skipped). GEN's width/stride/transparency need
  proper RE.
A direct load-and-tile attempt (mirroring the wall-set colour path) was
reverted — the menu keeps its flat clut-32 backdrop for now. NEXT: lift
JT[110], decode GEN's real tile format (probably stride=metric[6] not
8*metric[6], with a magenta-key transparency band), and blit per JT[81]'s
coords. (Also gated by the present-buffer issue below in some contexts.)

## Display present / buffer plumbing (NOW THE PRIORITY UNBLOCKER)

A recurring issue gates several screens: a draw + qd_present does NOT reach
the visible VIDEL buffer in certain call contexts, even a flat gray-fill.
Confirmed cases:
- Dungeon round-trip: 'q' out of port_play_demo -> menu redraws BLACK (the
  3D view's double-buffered mode leaves qd_present on the wrong page).
- Char-gen (jt574) reached via l07dc -> jt918: the screen draws black.
- BUT it works fine for: jt315 main menu, l0aae/jt918 Training Hall loop
  (their in-loop clear+present shows).
So qd_present works in some contexts and not others — likely a front/back
page-tracking bug in platform/display_videl.c (videl_flip / the qd_present
hook) when the active page was changed by a prior screen. Fixing this
unblocks the char-gen render, the round-trip, and future screens.
- NEXT (boot UI):
  - The faithful Begin Adventuring (jt585 -> CODE 15/19 -> the real play
    loop) so the port_play_demo bridge can come off. Exit From Play
    (case 8 / l10ca) -> back to main menu.
  - ROUND-TRIP GLITCH: returning from the dungeon ('q') redraws the menu
    BLACK — after port_play_demo's double-buffered VIDEL mode, even jt315's
    gray-fill + qd_present doesn't reach the visible buffer (the dungeon
    present path and qd_present target different buffers/state). Double
    qd_present didn't fix it; needs a display-HAL present/buffer reset on
    menu re-entry (platform/display_videl.c). The forward path
    (menu -> Play) is unaffected.
  - The faithful Play path (jt315 -> return 1 -> l07dc -> jt918 party
    setup / Training Hall) and Select/Create/Delete design + Unlock Editor
    land in CODE 8/2/12 entries still PROBE-stubbed.
  - Version banner: find the real source for the two top lines
    (g_a5_-13948/-13944 hold a "%s%03d.dat" template in this build).
  - jt131(6) screen-clear is a stub — jt315 paints its own backdrop +
    primes qd_present as a workaround; lift the real clear when convenient.
  - The play-loop body l07dc + jt918 (new-game / Training Hall) and their
    CODE 12/17/18 case bodies are the next big stubbed area.

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
- GROUND TRUTH CAPTURED (2026-06-01, mon BasiliskII + a non-intrusive
  instruction-loop hook at jt200 entry 0x01E5B2D4 and its 4 blit jsr sites;
  log saved /tmp/jt200_capture.log, 100 jt200 calls of a real HEIRS 3D
  frame). This OVERTURNS two earlier wrong conclusions and pinpoints the
  real bug:

  * **jt200 (L59d4) is 100% correct.** All 24 distinct (code,sub)->idx
    tuples match the lift exactly, e.g. code=9,sub=0 -> peel to code=4/grp1,
    code-- =3, far idx=sub+1=2, near idx=code*9+sub+2=30 — matches
    `far idx=2 near idx=30`. code=1,sub=6 -> idx 8; code=5,sub=3 -> idx 42;
    code=2,sub=1 -> idx 13. Every line verified.

  * **The bug is jt199: my lift is INCOMPLETE, not faithful.** I wrongly
    concluded the JT[3] view selector is constant 2. It is NOT — the
    `moveq #2` (L636a) only seeds it; an OUTER LOOP (tail L6e4a) iterates
    the selector 2 -> 1 -> 0, dispatching three scan BANDS:
      - case 2 (L63a2): side scans (sub 0 front-face / 9 side-face, both
        constant) + near front scans L66f2 (sub=1) / L67e2 (sub=2).
      - case 1 (L68be): mid band, nested depth loop, sub=3 (glob
        -12234/-12214) and sub=4 (-12232/-12212).
      - case 0 (L6bc2): far band, sub=5 and sub=6.
    So the real walk emits sub = 0,1,2,3,4,5,6 (+9 side) — a depth RAMP.
    My jt199_front froze sub at 1/2, so jt200 never saw sub>2 and never
    produced the near/big idx (30/31/32/33/42, and side idx 6/7/8). THAT
    is why only small far tiles drew. Earlier "synthesis is the root cause"
    and "needs no emulator" were both wrong; the capture was decisive.

- DONE: jt199 RE-LIFTED faithfully (boot.c). Added `jt199_band` (the
  case-1/0 facing+side scan) and rewrote `jt199` with the selector
  2->1->0 outer loop + all three JT[3] bands, transcribed line-by-line
  from the asm:
  * case 2 (sel=2, origin +2 fwd): the existing jt199_side x2 + jt199_front
    x2 (sub 0/9 side, 1/2 front) — unchanged, already faithful.
  * case 1 (sel=1, +1 fwd): jt199_band x2 — start orow+2*left (facing->sub3
    / left->sub4, soff -6 step +3) and orow+2*right (facing->sub3 depth<2 /
    right->sub5, soff +6 step -3), depth 0..2, globs -12234/-12214,
    -12232/-12212, -12230/-12210.
  * case 0 (sel=0, party cell): jt199_band x2 — start orow+left
    (facing->sub6 / left->sub7, soff -7 step +7) and orow+right
    (facing->sub6 depth<1 / right->sub8, soff +7 step -7), depth 0..1,
    globs -12228/-12208, -12226/-12206, -12224/-12204.
  Origin recedes one cell (back dir) per selector pass. Builds clean;
  asm-faithful + jt200 already verified against /tmp/jt200_capture.log.
  Visual confirmation deferred — the FRUA_MAP_DEMO entry (port_play_demo)
  is no longer on the boot path (the port now boots the real Training Hall
  UI), so rendering the 3D view needs menu navigation or a demo re-wire.
- DONE: demo re-wired. `port_play_demo` was buried behind FRUA_ENGINE_PROBE
  + FRUA_MAP_DEMO in jt361's test soup; added a clean independent hook in
  ua_main after jt361(1) under FRUA_3D_DEMO. `make EXTRA_CFLAGS="-DFRUA_3D_DEMO
  -DFRUA_FAITHFUL -DDEMO_LEVEL=2"` boots straight into the 3D view. CONFIRMED
  the jt199 re-lift: renders a receding stone corridor with side-wall wedges
  at multiple depths (the band/sub ramp firing) — the frozen-sub lift could
  not produce this.
- NEXT: the .tlb-vs-.ctl + synthesis question (deep mode loads .tlb w/
  placeholder synthesis via JT[111]; non-deep loads .ctl, all sizes
  present). The demo render shows the right STRUCTURE but wrong palette
  (blue speckle) + aspect — render_3d_faithful loads .ctl while deep mode
  wants .tlb-with-synthesis. Resolve so it blits the right tiles for the
  now-correct (code,sub) sequence. Also: keyboard walk (WASD) under
  --fast-forward didn't visibly step — check plat_kb_poll input plumbing.
- Meanwhile render_3d_raycast (visibility-faithful, on-screen, looks right)
  is the working demo renderer; the pixel-exact jt199 path is in progress.
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
