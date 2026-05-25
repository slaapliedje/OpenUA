# Milestone v0.2 — Toolbox stack stands itself up

Tagged at `v0.2-demo`. Second release. `v0.1` got real FRUA resources
on screen and accepted real mouse input; `v0.2` lifts most of the Mac
Toolbox the engine reaches for at boot and pushes the engine lift deep
into the new-game / select-design dispatcher.

## What's new since v0.1

### Compat (Toolbox shim)

- **Menu Manager** (`compat/menus.c`) — `NewMenu`, `GetMenu` (parses
  MENU resource), `AppendMenu` with `/`-meta key-equivalents, full
  menu bar + pull-down tracking via Button() / GetMouse(), `MenuKey`
  for Cmd-key dispatch (with title-flash on hit), `MenuSelect` with
  save-and-restore bits so a covered window survives the pull-down.
  FindWindow returns the new `inMenuBar` part code.
- **Dialog Manager beyond Alert** (`compat/dialogs.c`) — DLOG
  resource parser, `GetNewDialog` / `NewDialog` / `DisposeDialog`,
  `DrawDialog`, `ModalDialog` with `WaitNextEvent` loop + per-event
  `DialogSelect` dispatch. DITL types 4/5/6 (button / checkbox /
  radio) route through the Control Manager via `NewControl`; type
  0x10 (editText) goes through TextEdit via `TENew`. Default item
  paints with the Mac's double-frame ring. Return / Enter fires
  aDefItem.
- **Control Manager** (`compat/controls.c`) — ControlRecord at the
  Mac layout, per-window linked list via `contrlNext`. `NewControl` /
  `GetNewControl` (CNTL resource) / `DisposeControl` / `KillControls`.
  `Draw1Control` paints all four primary CDEFs (`pushButProc`,
  `checkBoxProc`, `radioButProc`, `scrollBarProc`) directly; the
  scroll bar gets arrows + chevrons + a proportional thumb. `TestControl` /
  `FindControl` hit-test in window-local coords. `TrackControl` spins
  on Button() / GetMouse() with hilite-tracks-mouse + scroll-bar
  thumb drag + arrow auto-repeat.
- **TextEdit** (`compat/textedit.c`) — TERec at Mac record offsets,
  `TENew` / `TEDispose` / `TESetText` / `TEGetText`, `TEKey`
  (printable insert + backspace + arrows), `TEClick` (caret-by-pixel
  via the 8x8 fallback advance), `TEActivate` / `TEDeactivate`,
  `TEUpdate` (white background + text + vertical caret), `TEIdle`
  (caret blink). Single-line behavior for v0.2; multi-line scroll
  and the styled-variant follow.
- **Sound Manager** (`compat/sound.c`) + Falcon DMA backend
  (`platform/sound_falcon.c`) — Mac SndChannel / SndCommand surface,
  `SndPlay` parses `snd ` format-1 resources (stdSH only) and routes
  through `Locksnd` / `Setbuffer` / `Setmode(MODE_MONO)` / `Settracks` /
  `Devconnect` / `Buffoper(SB_PLA_ENA)` to the Falcon CODEC. Sample-
  rate match picks the closest of the eight Falcon rates (22 kHz →
  CLK20K @ 19668 Hz).
- **InitMenus** now drains the menu bar at toolbox startup.
  **DisposeWindow** calls `KillControls` to clean up.

### Engine (CODE 6 / 7 / 12 lifts)

The full sub-resource release + cache compaction + state-transition
chain runs lifted C end-to-end:

- **`jt131`** state-transition manager + **`jt209`** / **`jt204`**
  multi-slot release + **`jt115`** generic release + **`jt461`**
  free-id stamp.
- **`jt465`** flush-by-key + **`L103c`** four-table compactor + the
  five CODE 3 string helpers (`l466a` isupper, `l46b2` tolower,
  `l39ae` strlen, `l3bda` ieq, `l3cfa` basename-after-colon) +
  `l1020` / `l366a` BlockMove wrappers + `jt384` strcpy.
- **`L5700`** / **`L5864`** mode-cleanup helpers + **`l5f4e`** zero
  wrapper.
- **`L67ca`** saved-game tear-down + redraw, plus **`jt80`**
  secondary-mode toggle + **`l08e6`** redraw-flag setter.

The new-game / select-design dispatcher's body is fully scaffolded:

- **`jt918`** outer loop scaffold + `L0df6` saved-game / `L0e98`
  fresh-init c79x flag-cluster setup + `L0ec6` input poll + JT[3]
  case dispatch on local 0..11.
- All twelve **case bodies** `L0f1a` .. `L120c` (Train / Modify /
  Delete / Create / Remove / Add / View / Human Change Class / Exit /
  Begin / Save / Load), each routing the matching c79x flag check
  to the action in CODE 17 / CODE 18 / CODE 19 / CODE 20 / CODE 15.
- **`L0aae`** design-menu builder — twelve item installs with
  accelerator keys + the c79x flag walk that enable / disables each
  item + selection poll.
- **`L185e`** Human Change Class "Drop NAME forever?" confirmation
  arm — fully lifted, including a working `jt488` sprintf into the
  static `g_a5_10362[256]` buffer via vsnprintf.

Engine-side support primitives:

- **`JT[1001]`** channel-write wrapper (`jt1001(a, b, c, d)` →
  `l309c(a, c, jt468(b), d)`) — the workhorse called by jt76, l66e6,
  jt80, l67ca, l0aae.
- **`JT[158]`** design-list menu walker + **`JT[166]`** mode setter +
  **`JT[179]`** slot-index table init.
- **`JT[452]`** DLItem install — slot allocation skeleton with
  variadic ABI parity.

### Demo (`src/main.c`)

`make run` launches the same boot path as v0.1 (HAL up → frua.rsrc
+ clut 129 + FONT -27001 → `ua_main` → 3 stacked windows + ALRT 200
→ menu bar) and now exits via the menu — File / Edit menus with a
Cmd-Q quit confirmation (DLOG 202 Cancel/OK). New menu items:

- **About FRUA** (File item 2) opens DLOG 201 from the real fork.
- **Enter name...** (File item 4) opens a synthetic editText dialog
  backed by a TEHandle — type / Tab cycles fields / press Return for
  OK.
- **Controls...** (File item 5) opens a Mac-style window with a
  checkbox, two radio buttons (group), an OK push button, and a
  10-step vertical scroll bar — click arrows to step, drag the thumb
  to scroll live, page zones to jump.
- **Quit** (File item 7, Cmd-Q via ALT-Q) opens DLOG 202 — only
  exits on OK.

## How to verify

```sh
make            # builds frua.prg + frua.rsrc (skips rsrcpack if rfork absent)
make run        # boots in Hatari; click File menu, exercise the demos
make ENGINE_PROBE=1 run    # adds per-stub trace to the console
make test       # host-side pytest suite over tools/ (41 tests)
```

The boot trace ends at `main: menu bar drawn` followed by the engine
spinning in jt918's iter-guard-bounded loop. The PROBE trace inside
one engine iteration now traverses the full design-menu setup pipeline
(`jt131 → l5700 → jt115 → jt461`, `l0aae → jt174 → jt447 → jt452 ×13
→ jt158`, `jt453`, etc.) — see `docs/engine-bring-up.md` for the
detailed call sequence and the current PROBE frontier.

## What's still missing

- The CODE 7 dialog runtime's actual rendering: `jt158` / `jt166` /
  `jt452` build the data structures but `jt453` (user selection poll)
  is still a PROBE stub returning 0. The menu doesn't appear on
  screen until that lifts.
- DATA + DREL replay — the THINK C runtime's pool isn't replayed, so
  globals like `g_a5_27980` (the direction-letter table L67ca's JT[1]
  dispatch reads) stay zero-filled. Without it, every comparison-
  based engine branch hits the "no" path.
- The action bodies behind each design-menu case (Train / Modify /
  etc.) — they live in CODE 17 / 18 / 19 / 20 / 15 / 4, segments
  the lifts haven't touched yet.
- Multi-line TextEdit (word wrap, justification, click-loop hook).
- Sound Manager beyond mono 8-bit stdSH — no stereo / 16-bit /
  looping / extSH / cmpSH / YM2149 fallback yet.
- Menu Manager beyond pushButton items — no item icons, no MDEF
  dispatch, no styled item text.

## What's next

Likely candidates for v0.3:

1. **`jt453`** selection poll — turns `l0aae`'s menu into an
   on-screen selectable list, ending the iter-guard hack.
2. **DATA + DREL replay** — unlocks the comparison-based engine
   branches across the whole frontier in one stroke.
3. **CODE 12 helpers `L02dc` / `L15e2` / `L12a0`** — the Modify /
   View per-action dispatchers, each 200-800 bytes of asm.
4. **Multi-line TextEdit + ModalDialog event filter** — pushes the
   editor surface forward.
