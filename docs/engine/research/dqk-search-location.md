# DQK (Gen2) per-cell events — SearchLocation reverse-engineering (C4.1)

How The Dark Queen of Krynn fires per-cell dungeon events, and how it differs from the Gen1
(Champions / Death Knights of Krynn) `SearchLocation` the engine already runs.

## Gen1 recap

In CoK/DoK the area ECL block's 2nd far pointer (`SearchLocation`) dispatches on the **cell
backdrop byte** (`mapWallRoof`, GEO plane-2) at the fixed data address `0x7F79`, via an
`AND mapWallRoof & 63 → ON_GOTO [table]`. Index 0 = the wandering-monster check, other indices =
per-cell event scripts. See `world/dungeonEvents.ts`.

## Gen2 (DQK) — coordinate dispatch, low data addresses

DQK does **not** use the backdrop byte. Its `SearchLocation` handlers dispatch on the **party
coordinates**, and its data segment uses *low* addresses (near 0), unlike Gen1's `0x7Fxx`. Read off
the real `ECL.GLB` (Gen2 dialect, vbase `0x8000`):

- **`0xBF` = party X**, **`0xC0` = party Y**, **`0x1B` = current area number** — **CONFIRMED**.
  Two independent lines of evidence:
  1. The **area-2 teleporter** (`WHERE DO YOU WISH TO GO?` / `X POSITION : 0-27`) reads its X/Y
     inputs with `INPUT_NUMBER → 0xBF` then `→ 0xC0`, and the area number into `0x1B`.
  2. A **position sweep**: seeding `{0xBF:x, 0xC0:y, 0x1B:area}` and running the handler through the
     VM changes which event fires. DQK **area 30**:

     | (x,y) | event text |
     |---|---|
     | (0,0) | "A PATROL OF ZOMBIES PASS YOU BY." |
     | (5,5) | "A SMALL CHILD WHISTLES AN ALARM, THEN RUNS INTO ONE OF THE BUILDINGS." |
     | (10,10) | *(nothing)* |
     | (3,12) | "DOGS RUN THROUGH THE STREETS CHASING RATS. YOU DISTURB VULTURES FEEDING." |

  Distinct cells fire distinct events — the dispatch is genuinely coordinate-driven.

- Some areas fire an **area-wide ambient** event regardless of cell (e.g. **area 4** always prints
  "HUGE SHADOWS SKIM OVER THE GROUND. GIANT SHAPES DISAPPEAR OVER THE TREE TOPS."). These are
  flag/area gated, not coordinate gated — both paths run through the same handler.

- The `vmRun1` (1st far pointer, the per-step main loop) of a dungeon area dispatches with
  `COMPARE_AND <posExpr>, <const>, <facing 0x11>, <dir>` chains — i.e. it also branches on facing
  (`0x11`) and a position expression. Fully mapping `vmRun1`'s position packing and the facing code
  is **not yet reversed** (deferred — see below).

## What C4.1 shipped

- `world/dungeonEvents.ts` — `primeSearchLocation`/`dialogueForCell` are now **dialect-aware**
  (`opts.dialect`, default Gen1): the header length and vbase follow the dialect, the Gen1 backdrop
  seeding only happens for the magic-bearing (Gen1) dialect, and a generic `opts.seedMemory`
  pre-seeds arbitrary data addresses. New constants `DQK_PARTY_X_ADDR`/`DQK_PARTY_Y_ADDR`/
  `DQK_AREA_ADDR` + `dqkPositionSeed(x,y,area)`.
- Web `dqkExplore.ts` (`?dqk=1`) — each step runs the area's Gen2 `SearchLocation` for the party's
  live cell and surfaces the event it fires on a per-cell event line (room descriptions, ambient
  text, scripted fights auto-resolved). The DQK dungeon now **plays through the engine**, firing real
  position-dependent events, not just walking.
- Every CoK/DoK Gen1 event golden is unchanged (the default dialect path is byte-identical).

## Open follow-ups (deferred from C4)

- **C4.2** — DQK **combat**: hand a scripted `SearchLocation`/event combat to the shared
  `launchCombat` overlay with faithful DQK monsters (needs `MONCHA.GLB` decoded; today scripted
  fights auto-resolve and surface a marker).
- **C4.3** — DQK on the shared Phase-A **systems** (magic/items/town/character sheet) driven by a
  DQK party — these are already party/ruleset-driven, so this is a wiring+verify slice.
- `vmRun1` position packing + the facing code at `0x11` (full per-step main-loop dispatch) — only
  needed for the events `vmRun1` (rather than `SearchLocation`) owns; not required for C4.1.
