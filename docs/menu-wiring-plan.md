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

## Phase 3 — Training Hall + play-side menus

`jt918` already uses the runner-ish pattern; finish it on top of Phase 0:

1. Roster ops: Modify/Remove/View character, Human Change Class
   (`l02dc` grid + `jt182` popup are partly there).
2. **Create Character → CODE 17 char-gen** (the big subsystem: race/class/
   stat-roll state machine, `JT[557]/574/556/560`). Multi-session.
3. Load/Save game modals.
4. **Begin Adventuring** — replace the `port_play_demo` bridge with the
   faithful `l1142 → jt585 → CODE 15/19` adventure entry.

## Deferred visual polish (tracked, not blocking)

- **Backdrop tile artifact** — the current backdrop (FRAME.CTL item 4) has
  a baked-in white 3D bevel highlight (it's a *framing* edge piece, not a
  clean field). It tiles across the gaps/perimeter as a stray light line
  (`/tmp/frame_bar.png`). Fix: pick the clean field tile (try FRAME.CTL
  item 1, or a non-bevel sub-row of item 4), or composite the perimeter
  frame separately from the field so the field tile carries no 3D edge.
- **Bars touching / button sizing** — the Mac frames abut with no gap and
  size to content; our plates have small gaps and a fixed 150px width.
- **Disabled-item dimming** — falls out of Phase 0.1 (drive state from
  `rec[28]`); disabled labels also draw dimmer text.
- **Faithful FRAME.TLB compositing** — mask + corner + edge tiles via
  `l309c`/`jt1001` (still PROBE stubs); the `.CTL` color tiles approximate
  the look today.

## Definition of done

From the main menu you can reach every sub-menu, navigate it, back out,
and the current design/party state persists — all rendered with the one
shared chrome. The editors (Edit Modules / Monster Editor) may still be
skeletons, but their menus open and dismiss cleanly.
