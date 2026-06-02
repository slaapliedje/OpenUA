# Menu wiring plan

How we connect every menu in the game, now that the shared chrome (frame
plates, warm UI palette, kerned text, hotkey highlights) renders correctly
and matches `data/frua_mac_menu.png`.

## Why this is worth planning once

Every menu in FRUA — main menu, Training Hall, design picker, Game
Settings, the module editor, Art Gallery, Monster Editor, and all the
modal popups — is built from the **same DLItem primitives** and drawn with
the **same chrome**:

| Primitive | Role |
|-----------|------|
| `jt447`   | init a DLItem group (`g_a5_9254/9286` = `g_dlitem_pool`) |
| `jt452`   | push one item (label, x/y, hotkey via cmd 32, flags via cmd 20/21) |
| `l2c60`   | paint the group (walks recs → shape handler `jt382` cmd 1) |
| `jt453`   | modal event loop (spins `l2d3e` until a hit ≥ 0) |
| `jt315`-style chrome | backdrop + plates + title (now in `boot.c`) |

So the work is mostly *one* reusable runner plus *dispatch wiring*, not ten
bespoke screens. Get the runner right and each menu is "build the item
list + handle the result".

## Current state (2026-06)

- **Chrome**: solid. `load_menu_ui` (MENU.CTL palette + FRAME.CTL band +
  FRAME.CTL backdrop tile), `fill_backdrop`, `draw_plate` (raised/recessed),
  kerned text (`mac_font_offset`), hotkey highlight (`jt382`).
- **Main menu** (`jt315`, CODE 22 + 0x4d8a): renders + dispatches
  **Play → 1** and **Quit → 0**; the other 8 arms fall through to a redraw.
- **Faithful dispatch** lives in the asm as a `JT[3]` inline switch at
  **CODE22 + 0x5112** (decode with `tools/jt3_extract.py`). Its arms call
  CODE 2/6/7/8 entries — `JT[356]`/`JT[361]`/`JT[369]` (CODE 8 design ops),
  `JT[128]/133/135` (CODE 6 screen/mode), `JT[247]` (CODE 2), `JT[159]`
  (CODE 7 menu mgmt), `JT[76]/98` (CODE 6), `JT[1080]` (CODE 5) — **most are
  PROBE stubs**.
- **Training Hall** (`jt918`): partially lifted — `l0aae` builds the
  party-management menu, `l02dc` roster grid shows a seeded party, "Begin
  Adventuring" bridges to `port_play_demo`. The other items are stubs.
- **Add Character popup** (`jt182` via `jt904`): renders; CODE 17 char-gen
  behind it is barely lifted.

> **Phase 0 DONE (commit 5a99162).** `menu_run(items, n, proc, decorate)`
> is the shared runner: it paints the chrome (backdrop + a plate per spec
> entry), builds the DLItem group from a `menu_item_t[]` (label-less entries
> = spacer plates), paints labels, runs the jt453 loop, returns the index.
> `decorate(...,phase)` does menu-specific chrome (phase 0 = title plate,
> phase 1 = banner). `jt315` is now ~10 lines on top of it (pixel-identical).
> New menus = a spec + proc + decorate. Not yet done: driving `recessed`
> from the live `rec[28]` enable bit (kept as a per-spec flag until jt158
> wires real enable state) — see step 1 below.

## Phase 0 — Make the chrome a reusable runner (do first)

Today `jt315` hardcodes the title plate and drives `draw_menu_plates` from
the `g_mainmenu[]` table. Generalise so *any* `jt452`-built menu gets the
same look:

1. **Drive plates from the live DLItem group, not `g_mainmenu`.** Walk the
   records like `l2c60` does (`g_a5_9254`, count `g_a5_9250`), read each
   rec's coords (`rec[16]`/`rec[18]`) for the plate box, and read the
   **enable bit `rec[28] & 0x02`** to choose raised (active) vs recessed
   (disabled). This makes the raised/recessed state *data-driven* (it also
   makes disabled-item rendering automatic — see Deferred polish) and drops
   the hardcoded `g_mainmenu.recessed`.
2. **Extract `menu_chrome(title_lines)`**: restore mode (`g_a5_2347=1`),
   `load_menu_ui`, `fill_backdrop`, optional title plate + banner, then
   `draw_menu_plates_from_group`. `jt315` and every future menu call it.
3. **Extract `menu_run(build_fn) -> hit`**: `jt447` → caller's `build_fn`
   pushes items → `menu_chrome` → `l2c60` → present → `jt453`. Returns the
   selected index. This is the spine every menu reuses.

Deliverable: `jt315` reimplemented on top of `menu_run`, pixel-identical to
now. No behaviour change — pure refactor that unlocks the rest.

> **Phase 1 DONE (commit b4d0b19).** The JT[3] table at CODE22+0x5112 was
> decoded (12 cases, min 0 / max 11). jt315 now dispatches every command to
> a handler: Play -> return 1, Quit -> return 0, and each design / settings /
> editor command opens a sub-screen via `menu_todo()`. The **menu-stack
> pattern** is proven end-to-end — a command runs its own `menu_run` loop
> (header plate + name + "Not yet implemented" + Exit) and returns to the
> dispatch. The port dispatches on its own g_mainmenu index rather than
> replicating the asm's jt452 build order. Phase 2 replaces each `menu_todo`
> with the faithful sub-menu (JT entry noted per case). Note: Game Settings
> (JT[247]) turned out to be a dynamic settings *dialog* (sprintf'd values,
> JT[148] list), not a static list — so no single "easy first" sub-menu;
> each is its own subsystem.

## Phase 1 — Faithful main-menu dispatch + a menu stack

1. Decode the `JT[3]` table at CODE22+0x5112 and replace `jt315`'s
   play/quit `switch` with all 10 arms (structural skeleton: each arm calls
   its real JT entry, inner work deferred). Keep Play/Quit working.
2. Establish the **call convention**: each sub-menu is a C function that
   runs its own `menu_run` loop and returns when the user backs out, so the
   menu graph is the C call stack (no global mode machine yet). The asm's
   re-entry flag dance (`jt215`/`jt356`/`g_a5_-11662`) is layered in only
   where a screen actually needs persistent state.

## Phase 2 — Wire the main-menu arms (one focused lift each)

Ordered easiest → hardest; each is its own commit using `menu_run`.

1. **Quit confirm** — already returns 0; confirm the yes/no modal uses the
   runner.
2. **Game Settings** (`JT[247]`, CODE 2 + 0x23be) — a self-contained
   options menu; good first real sub-menu.
3. **Select a Design** (`JT[361]/369`, CODE 8) — list the `.DSN` files
   (design linked list / `g_a5_-19176` count), set the current design,
   refresh the main-menu banner. Enables Delete/Unlock (`rec[28]` clears).
4. **Create New Design** (CODE 8) — name entry (TextEdit shim) + template
   copy; then Delete becomes meaningful.
5. **Delete the Design / Unlock Editor** — small confirm modals; gated on
   the design state Phase 2.3 establishes.
6. **Art Gallery** (`JT[1080]`, CODE 5) — picture browser; exercises the
   GLIB image blit path (reuses the `unpackbits`/`l309c` work).
7. **Monster Editor** / **Edit Modules** (CODE 12, the design editor) — the
   largest; their own multi-session efforts (ADR-0008 puts the runtime
   before the editors, so these come last).

> **Started (commit 066dd47).** The Play -> Training Hall menu (jt918 /
> l0aae) now uses the shared chrome: jt918 paints the stone backdrop +
> UI palette (was flat clut-8 + the dungeon palette), and l0aae draws a
> bevelled plate per command (menu_draw_plates), recessed = disabled (its
> c79x enable flags). The 12 commands get plates + hotkey highlights +
> kerned text, with the roster (seeded party) intact. Phase 2's main-menu
> sub-commands were all heavy subsystems or blocked by our single-loose-
> design data, so the Training Hall (a real list menu, reachable now, with
> our data) was the productive next reuse of menu_run.
> REMAINING Training Hall polish: the roster header ("Name … HP AC") column
> positions are off (l02dc / jt32 / jt34 number paint still stubs); verify
> the recessed/enabled plate states against a loaded design; then the
> per-command actions (Add/Create -> CODE 17 char-gen, Begin Adventuring ->
> the faithful jt585 chain).

## Phase 3 — Training Hall + play-side menus

`jt918` already uses the runner-ish pattern; finish it on top of Phase 0:

1. Roster ops: Modify/Remove/View character, Human Change Class
   (`l02dc` grid + `jt182` popup are partly there).
2. **Create Character → CODE 17 char-gen** (the big subsystem). STARTED
   (commit 0e3e437): jt574 shows the char-gen screen on the shared stone
   chrome with the PICK headers (l35f8) + the option lists (cg_draw_lists:
   6 races, 9 alignments, Male/Female); jt557 (Create, Training Hall case 3)
   opens it. INTERACTIVE (commits bd55d6a, 19cf09b): a keyboard cursor
   advances the picks race -> gender -> class (Up/Down move, Return
   advances, Esc backs up); the class list is gated to the picked race's
   allowed classes (cg_allowed_classes + cg_race_classes single-class table;
   faithful source g_a5_-30450). Verified: Halfling -> Fighter/Thief only.
   FULL PICK FLOW (commits e3d051a, 8ad3032): the pick state machine now
   runs in order — race -> gender -> class (race-gated) -> alignment
   (class-gated: Thief non-lawful, Paladin LG-only, Ranger any-Good) ->
   stat roll (3d6 + race mods, R = re-roll). Pick state is a cg_state
   struct. Verified: Dwarf Thief -> 6 non-lawful alignments; Halfling ->
   Fighter/Thief classes only.
   END TO END (commit a24aef1): + a name step (typed entry) and the record
   build — cg_build_record assembles a 512-byte record (name@+96, HP@+385 =
   hit die + CON bonus, AC@+395 = 10 - DEX bonus) and appends it to the
   roster (g_a5_-27928, the list l02dc walks). So char-gen now produces a
   real party member: race -> gender -> class -> alignment -> stats -> name.
   CLASS-LEGAL ROLLS (commit 029fd4c): cg_roll_stats takes the class and
   re-rolls each ability to its class minimum (cg_class_min: Fighter STR>=9,
   Paladin STR12/INT9/WIS13/CON9/CHA17, Ranger STR13/INT13/WIS14/CON14, …),
   so a created character is always legal. Verified: Dwarf Fighter -> STR 15.
   Still a PORT keyboard interaction pending the faithful jt568 machine.
   REMAINING refinements: faithful play-record fidelity (stats/class/saves
   at their canonical record offsets + derived combat fields — needs the
   CODE 17 record-commit RE'd; the working record is g_a5_-7008, fields at
   +89/+93, but the six-stat offsets aren't pinned and nothing consumes them
   yet), multi-class combos, and persistence (the play-entry re-seed
   port_test_seed_design wipes created chars until real save/load lands).
3. Load/Save game modals.
4. **Begin Adventuring** — replace the `port_play_demo` bridge with the
   faithful `l1142 → jt585 → CODE 15/19` adventure entry.

## The faithful frame system (RE notes)

`jt81` (CODE 6 + 0x6a10) is the menu frame setup. It:
1. loads the named **"gen"** asset via the named-GLIB loader (`L33ac`,
   = JT[110]) into handle `g_a5_-13044`;
2. blits its sections `JT[1001](8000, 8000, 1, N)` for N = 1,2,3 (and 4 in
   deep mode, gated on `jt1200()==3`) — `JT[1001]` → `l309c(a, c,
   jt468(b), d)`, the masked pixmap blit;
3. cleans up (`L384c`/`L3918`/`JT[174]`/`L31dc`).

So the menu **field = the GEN.CTL stone image** (320x90), drawn in
sections — *not* a FRAME molding tile. This is now done (commit 07f199d):
the backdrop is GEN.CTL item 1, tiled, rendered through the FRAME warm-grey
band — clean dark stone, artifact gone.

The decorative **border molding** is FRAME.CTL's edge/corner pieces:
item 1 = top edge (320x8), item 2/3 = left/right edges (8x184), item 4/6/7
= horizontal dividers, item 9 = a complete ornate sub-panel frame
(136x135), items 10-16/26-28 = corner caps + 8x8 fills. Each carries a
signed `(ybear, xbear)` = its placement offset from the frame origin, and
index 0 = transparent (field shows through). Formats by flag byte:
`0xc0/0xc5` = raw 8bpp, `0xc2` = PackBits, `0xc7/0xc9` = more compressed
(RLE variants, TBD), `0xc8` = palette band.

**`l309c` is now lifted** (commit 48e92a2) as `ui_glib_blit`. RE result:
- `l309c` (CODE 5 + 0x309c) is the *dispatcher* (not a 150-line blit — the
  old stub comment was wrong): jt1135 remap → l2856 metric → (ybear,xbear)
  bearing → composite arm (`metric[7]&0xF==9`: a list of 6-byte
  `{sub_idx,count,dy,dx}` records, recursed) or the leaf.
- `l2d4e` (CODE 5 + 0x2d4e) is the leaf, dispatched by flag low-nibble:
  `3` = scaled column blit (JT[1170/1177/1185]), `2` = PackBits (L2bfc),
  `7` = L2b9a, `0/5` = raw (L2970, = the dungeon's `bp_blit`), `0x0a` =
  deep split; clips against `g_a5_-3050..-3056`.
- `ui_glib_blit` reproduces this for the chunky UI surface (PackBits + raw
  + index-0 transparency + composite recursion + clip), validated against
  FRAME.CTL item 9 (ornate panel blits with a see-through interior).

**Wired (commit d011a3f):** the menu backdrop now draws through
`ui_glib_blit` — `fill_backdrop` tiles the GEN field via the lifted blit
(opaque), so the menu's field goes through the real l309c path. GEN item 1
is one bearing-placed section (ybear -110) of the multi-section "gen"
backdrop; GEN.CTL ships only that section, so we repeat it to cover the
screen.

FRAME.CTL composite RE: items 17-20 (`0xc9`) are **column fills** — they
stack the 8x8 tiles (26 = top cap, 27 = middle, 28 = bottom cap) into
vertical stone strips of various heights (item17 = 120, 18 = 63, 19 = 43,
20 = 31). So FRAME's composites build larger fills from 8x8 tiles; the
full screen-border assembly is done by engine code placing edges/corners/
fills (the top-level layout isn't a single composite item).

**gen backdrop — resolved (commit eea6822).** `L33ac` builds `'%s.ctl'`
so "gen" = GEN.CTL, which has exactly one image section (item 1, 320x90,
ybear -110); jt81's sub-items 2-4 don't exist. The reference backdrop is
continuous full-screen stone (verified by uniform luminance), so the
faithful field is that one section tiled. Tiled through `ui_glib_blit`
with alternate copies mirrored (the new `flip` arg) so the 90-row repeat
is seamless. The jt468/jt1001 page-table indirection (g_a5_-18468 slots)
is internal plumbing that yields the same pixels — deferred, not needed
for the field.

**Remaining frame work:** (1) for ornate dialog frames, blit the FRAME
composite/edge pieces via `ui_glib_blit` (proven with item 9); (2) add the
`0xc7` leaf decode (L2b9a) when a needed piece uses it. The main menu's
simple bevels stay procedural (faithful — it has no ornate molding). The
blit is reused by *every* GLIB image (Art Gallery, portraits, dungeon art).

## Deferred visual polish (tracked, not blocking)

- **Backdrop seams** — GEN is 320x90 tiled to 200; the vertical repeat can
  show faint seams (mostly hidden under the plates). The Mac's section
  placement (l309c, above) positions them to avoid this.
- **Procedural plate bevels** — our plates are drawn procedurally (flat
  fill + 1px bevel); the faithful version composites FRAME.CTL molding
  pieces (needs l309c). Looks close today.
- **Bars touching / button sizing** — the Mac frames abut with no gap and
  size to content; our plates have small gaps and a fixed 150px width.
- **Disabled-item dimming** — falls out of Phase 0.1 (drive state from
  `rec[28]`); disabled labels also draw dimmer text.

## Definition of done

From the main menu you can reach every sub-menu, navigate it, back out,
and the current design/party state persists — all rendered with the one
shared chrome. The editors (Edit Modules / Monster Editor) may still be
skeletons, but their menus open and dismiss cleanly.
