# The in-engine GEO map editor (`jt243`)

The interactive area-map editor — the engine's own tool for drawing walls,
placing events, and painting cells into a GEO area. This document is its
**operational spec**: what the editor does and how it mutates the same GEO cell
table that `tools/geo.py` authors offline.

## Status — lifted, but dormant

**The whole CODE 11 GEO editor is faithfully lifted** (`jt233`–`jt244`, incl. the
two giants `jt242` and `jt243`; completed 2026-07-07 — see
`docs/geo-editor-wall.md` for the campaign record). Every function is a real
transcription, not a stub.

It is **not yet reachable**, for two structural reasons (unchanged from the
wall doc):

1. **Mouse-gated.** Entry is `jt315` (main menu) → EDIT MODULES →
   `l0004_22(7)` → editor mode 2 → `jt243`. Every tool is a mouse click, and
   Hatari does not inject mouse buttons (see the `run-falcon-port` skill), so
   the editor is **not headless-testable** — correctness rests on faithful
   transcription against the disasm.
2. **UI not wired.** The editor UI (Dialog/Control/TextEdit, ADR-0006) is a
   separate large effort. Until it is wired, `jt243` and its subtree are
   dormant `__attribute__((unused))` lifts that only prove boot-unregressed.

So there is no lifting work left here — this doc closes the gap by documenting
the editor's *behaviour*, connecting it to the [geo-format](geo-format.md)
cell model.

## How it is reached

The CODE 22 command dispatcher `l0096` routes editor commands:

| cmd | target | role |
|---|---|---|
| 2 | `jt243` (`l0b26`) | the map editor MAIN dispatcher (this doc) |
| 20 | `jt242` (`l589a`) | the cell-edit committer (JT[3] @0x58ce, 3-way) |

## Data model

**Cursor.** The edited cell is at the design-state cursor `col = -11702`,
`row = -11701`. The cell index `l1d88`/`jt243` compute is `height*col + row` —
**column-major**, exactly `geo.py`'s `_cell_off` and the MAP layout in
[geo-format](geo-format.md). (A second cursor, `col = -12287` / `row = -12288`,
drives placement tools; same indexing.)
Design state is at A5 `-12300`; `ds[2]` = width, `ds[3]` = height, and the cell
table lives at `ds[290 + idx*6]`.

**Editor holder** (the working context passed to every tool, `fp@14`):

| field | meaning |
|---|---|
| `holder[0]` | active tool command (written on entry) |
| `holder[2]` | previous tool (restored into `[0]` by most arms) |
| `holder[4]`, `holder[5]` | kind / sub-kind of the current edit |
| `holder[6]` | **pending-op**: `0` = commit now (enter the modal hub `l28d4`); bit 15 set = defer to tool 20 (the committer) with the low bits as the value |
| `holder[10..16]` | working style bands (the wall/floor style being painted) |
| `holder[14]` | item / flag field (tool 8) |
| `holder[17]` | dirty/redraw flag bits |
| `holder[21..34]` | undo/mirror bands (old values captured before a write) |
| `holder[46..]` | the map-view `DLItem` panel |

**Cell record** (`rec`, a packed long the tools edit before commit): low nibble
`rec & 15` = edge/wall kind; `(rec & 0xFF0)>>4` = the two cell-code nibbles;
`(rec & 0xFF00)>>8` = the flags byte; `(rec>>16) & 63` = the group/fill style.
`l1d88` and `jt242` unpack this and stamp the design-state cell via `jt302`
(write) / `l04d6` = `JT[310]` (decode), then repaint.

## Tool palette — the 20-arm dispatch (`JT[3] @0x0b48`)

`jt243(cmd, rec, holder)` writes `cmd` into `holder[0]`, then dispatches. Each
arm edits the holder/`rec` fields, runs the shared commit tail (if
`holder[6]==0`, enter the modal hub `l28d4`), and returns via the finalizer
`l243_finalize` (which bit-packs the result back into `rec`).

| cmd(s) | handler | what the tool does |
|---|---|---|
| 1 | `L12fc` | **flood-fill**: start a fill (seed the style into `holder[6]`) or, if one is in progress, commit it (`l4168`/`l16ae`) |
| 3 | `L126e`→`L1290` | pack the current style (`jt358`) then commit the cell |
| 5 | `L11c0` | place / toggle a cell flag; refresh the `DLItem` panel |
| 8 | `L0ef2` | edit the cell's **item/flag** field (`holder[14]`) — magic-item cells |
| 9 | `L0fce` | multi-mode edit (JT[3] sub-modes 1..10): wall-style / decoration edits, some with a name prompt (`jt133`/`jt135`) |
| 12 | `L0de4` | edit **both cell-code nibbles** (`l23de` × 2) |
| 13, 19 | `L1290` | commit the pending edit; 19 also clears + repaints |
| 20 | `L0dac` | **committer** — apply the deferred edit (holder[6] bit 15 path) and repaint |
| 10, 14, 15, 17 | `L0cee` | layer / backdrop tools (toggle `holder[17]` bit, repaint layer) |
| 11 | `L0bf8` | general handler family (nested JT[3] @0x0c24 / @0x0c3c) |
| 2, 4, 6, 7, 16, 18, default | `L136e` | the paint / no-op arm |

Two arms (8, 12) can **defer**: they set `holder[6] = 0x8000 | value` and
`holder[0] = 20`, handing off to the committer arm on the next pass.

## Commit & save

- **Per-cell commit:** `l1d88` (`JT[3] @0x1d9e` on `rec[18]`, kinds 1..4) swaps
  the working bands into place, calls the Toolbox painter (`jt290`/`jt292`/
  `jt279`/`jt295`/`jt213`), and restores — kind 1 = cell edit, 3 = level
  decoration (`ds[k*4+14]`), etc. `jt242` is the standalone committer.
- **Save 3D map:** the `l0742`→`l0878` subtree writes the
  `FORM/AMOD/HDR/MAP/ENCR/STRG` container (byte-swapping around HDR/STRG via
  `jt1180`/`l4e3a`) — **the exact 12962-byte GEO file format `geo.py` reads and
  builds** ([geo-format](geo-format.md)).

## Relationship to `tools/geo.py`

The in-engine editor and `geo.py` are two front-ends to the **same** GEO area:

| | in-engine editor (`jt243`) | offline (`tools/geo.py`) |
|---|---|---|
| cell addressing | `height*col + row` (column-major) | same (`_cell_off`) |
| wall / floor styles | tool 9 / 12 → cell-code nibbles | `set_cell` / `wall` |
| events | tool places event index into the cell | `set_event` + the event builders |
| output | Save 3D Map → FORM/AMOD container | `Geo.build()` — byte-identical format |

So a module can be authored either way, and the two round-trip through the same
file. `geo.py` is the headless, testable path (the editor cannot be exercised
without the mouse-driven UI); this doc records what the interactive editor does
to the identical underlying data.
