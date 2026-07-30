# The in-engine GEO map editor (`jt243`)

The interactive area-map editor — the engine's own tool for drawing walls,
placing events, and painting cells into a GEO area. This document is its
**operational spec**: what the editor does and how it mutates the same GEO cell
table that `tools/geo.py` authors offline.

## Status — WIRED AND LIVE

**The whole CODE 11 GEO editor is faithfully lifted** (`jt233`–`jt244`, incl. the
two giants `jt242` and `jt243`; completed 2026-07-07 — see
`docs/geo-editor-wall.md` for the campaign record) **and reachable from the
running game** (verified 2026-07-18).

**Verified live in Hatari.** From the main menu, the `E` key ("Edit Modules")
opens the Map Editor's module picker; selecting an area and pressing Return
(Open) brings up the full editing canvas — the FILE / MAP / UTILITIES menu bar,
the 3D cell view, the `PLACE / BLOCK / PASSABLE` tool palette, and the
`SELECT / LEFT / PLACE / RIGHT / UNDO / MARK` command bar. The command-bar keys
(`S/L/P/R/U/M`) register — the bar highlights the active command — so the
keyboard command path is exercised end-to-end.

**Mouse — this now works** (corrected 2026-07-26; the note below used to say
Hatari does not inject mouse buttons, which stopped being true once the harness
started launching with `--mousewarp no`). `driver.sh click X Y` registers, and
`driver.sh drag X1 Y1 X2 Y2` drives the pulldown menus. `tools/geo.py` is still
the fully headless-*testable* authoring path and edits the same file (see
below), but the editor's own mouse path is drivable.

**Pulldown menus — the two traps.** Both cost a boot on 2026-07-26:

- **A plain `click` on an already-open menu does NOT select** *unless the item is
  already highlighted.* Mac menus commit on the mouse RELEASE inside the item, so
  `drag` from the menu title to the item. The `drag` alone often leaves the menu
  open with the item highlighted; a following `click` on that highlighted item
  then commits (this is the gesture the #110 save test used). A click on an
  UNhighlighted open menu just dismisses it.
- **`Escape` closes the whole EDITOR**, not just the open menu — it drops you
  back to the main menu and you have to walk in from `E` again.

And you cannot screenshot while the button is held (it deadlocks the Xvfb), so
the item coordinates have to be known in advance. Measured, for the **MAP** menu
(title at x≈285, y≈67; items at x≈380):

| item | y |
|---|---|
| 3-D VIEW | 88 |
| AREA VIEW | 109 |
| *(separator)* | 130 |
| WALL PLACEMENT | 150 |
| BACKDROP PLACEMENT | 170 |
| ZONE PLACEMENT | 190 |
| EVENT PLACEMENT | 210 |
| MOVE THROUGH WALLS | 248 |

A drag that releases on the separator leaves the menu open with no selection —
which is how these were measured, since the open menu can then be screenshotted.

**Getting in:** main menu `E` → picker → `click` OPEN at (62, 439) → the canvas.

★ **THE ENTRY PICKER'S LIST IS EMPTY — the old claim here that it "shows
OVERLAND 01..04 then DUNGEON 01.." is WRONG** (re-driven 2026-07-29, #101, with
10 designs installed and 11 areas in HEIRS: zero rows drawn, just the header,
the MAP EDITOR / design / area caption and OPEN|CANCEL). So on entry OPEN can
only accept the current area — which is why this recipe works at all, and why
the mistake went unnoticed. The list that IS populated is the in-editor one:

**Switching areas:** inside the editor, `click` FILE at (85, 68) to drop the
pulldown, then `drag 85 68 100 89` onto **OPEN..** — that pops the same dialog
with all 11 rows (OVERLAND 01–04, DUNGEON 01–07), the current one highlighted
and a working scrollbar. `click` a row, then `click` OPEN at (62, 439). Verified
DUNGEON 01 (WD 19 HT 19) → DUNGEON 06 "KEEP -- ENDGAME" (WD 28 HT 20): new
automap, new 3D view, compass re-reads N. FILE items, measured: OPEN.. y=89,
SAVE 109, WRITE TO... 129, COPY FROM... 149, REVERT TO SAVED 169, GLOBAL INFO
209, PRINT 229, LEAVE 268 (title x≈85, items x≈100).

## ★ THE EDIT FIRES AND THE SAVE WORKS (#108 / #109 / #110, 2026-07-29)

Driven live on HEIRS DUNGEON 01 (19x19), coverage probe on
(`EXTRA_CFLAGS=-DFRUA_ENGINE_PROBE_ONCE`, read `data/work/gamedata/DBG.LOG`).

**The click path works.** Clicking the **PLACE** button at (265, 439) — NOT a
grid click; a click in the automap only fires `jt1080`, the blocked cue — logs
exactly the lifted call order:

```
jt290 -> L0ee6 -> jt285 -> L05ca -> jt277 -> L0614 -> jt295 -> jt296
      -> jt1150 -> jt213 -> L5752 -> jt321
```

i.e. capture the undo band (jt285 low nibble, l05ca wall), write both halves of
the edge (jt277 = rec[10], l0614 = rec[12]), redraw the cell and the party
marker. The automap repainted (1828 px changed). It took **l0ee6**, not l1240,
because `l4900()` reports the editor LOCKED — reaching l1240 needs the main
menu's UNLOCK EDITOR first.

### SAVE WORKS (#110) — the cursor is the writer's own parameter slot

`l0878` hands each chunk writer **`pea %fp@(8)`** — the ADDRESS of its own
by-value buffer argument — at 0x0892, 0x08b2, 0x091c, 0x09d0, 0x09f4 and 0x0a20.
It is the ordinary `write_chunk(&p, tag, len)` idiom: the cursor is a stack local
that `l0878` bumps as it writes, and the buffer's own bytes are never used to
store it. The port had passed the buffer pointer down BY VALUE, so `*(long *)ctx`
read the first four bytes OF THE ARENA — which at save time are the loaded file's
`'FORM'` magic. That one instruction is the whole of #109 and #110.

Fixing it reconciles all three readings that had looked contradictory:

| reading | why it is fine |
|---|---|
| `l07c2` pushes `jt1004()`'s VALUE (0x07d8 `movel`, not `pea`) | correct — `l0878` is the one that takes an address, of its own copy |
| `jt1002` stores a raw `NewPtr` (JT[421] -> JT[1028] = `_NewPtr`) in -4582 | correct — the arena is a plain buffer with no header |
| `jt129` flushes 12962 bytes from that same `h` | correct — `l0878` mutated only its own stack slot, so `l07c2`'s `h` still points at the true base |

1.2 is byte-identical here (only the JT permutation differs), so this was never
a version issue.

**Verified live on HEIRS, area 5 (`GEO005.DAT`), Falcon/TOS 4.04:**

- `l0878: cur 2295572` == `arena 2295572` — the cursor starts at the arena base.
- Every chunk written, no bounds-guard trip, no `ERRMODAL`.
- The file is **12962 bytes** and the chunk walk is EXACT:
  `FORM 0x329a / AMOD 0x3292 / HDR 0x122 @24 / MAP 0xd80 @322 / ENCR 0x7d0 @3786
  / STRG 0x1c00 @5794` -> offset 12962 == EOF. Magic intact.
- **Exactly ONE byte differs from the pre-save file**: offset 1301, `5 -> 0`.
  That is MAP-relative 979 = cell 163, edge 1; at WD 19 cell 163 is A 8 / B 11.
  So the writer round-trips the entire area faithfully and lands precisely the
  one cell edge the author edited — nothing else moves.
- Re-opening the area in the running editor loads it cleanly (full canvas,
  automap, viewport; no reject, no alert).

★ **`FILE -> SAVE` IS GATED ON `jt318()`, THE DIRTY FLAG** —
`*(unsigned char *)g_a5_long(-11714)`. `l0742` returns immediately when it is 0,
so **a save with no edit does nothing at all** and the files stay byte-identical.
Do not read that as a passing round-trip: it is a non-event. Always click PLACE
(`click 265 439`) first, then save, and confirm `jt318 == 1`. An hour went into
"the save is broken again" that was only this gate. The trace to instrument is
`l2dbe` (group/sub) -> `l0742` (num, jt318) -> `l0878`; the working menu gesture
is `drag 85 67 100 109` (to highlight) then `click 100 109` — a click on an
unhighlighted pulldown just dismisses it.

★ **`dbg_log`/`dbg_log_num` go to the CONSOLE, not `DBG.LOG`** — only
`dbg_file_*` writes the file (`platform/dbglog.c`: `SINK_CON` vs `SINK_FILE`).
Read `/tmp/frua-ui/conout.log` (or `driver.sh log`) for `dbg_log` output. A stale
`DBG.LOG` left over from an earlier session is easy to mistake for fresh
evidence — check its mtime before believing it.

The bounds guard from #109 stays as a backstop; with the cursor fixed it can no
longer trip, so a `save declined (guard)` line in the log means the writers
regressed.

### How it was found (#109 — corruption stopped, cause not yet known)

`l07c2` does `h = jt1004()` — the GEO arena at A5 -4582 — and hands `h` to the
serialiser as `ctx`. `l0ad0`/`l0a4e` then take **`*(long *)ctx` as the write
cursor**. But at save time that arena still holds the area file as LOADED
(`jt127` reads it in at offset 0), so the "cursor" is the file's own first long:
`'FORM'` = 0x464F524D, a bogus address. Every chunk write goes there (nowhere),
the arena keeps the loaded copy untouched, and the cursor cell — which OVERLAPS
the magic — is incremented by each chunk. Then `jt129` flushes the arena.

The arithmetic is exact, which is what makes this certain rather than plausible:

```
8 + 8 + (8+0x122) + (8+0xd80) + (8+0x7d0) + (8+0x1c00) = 0x32A2 = 12962
0x464F524D + 0x32A2                                    = 0x464F84EF
observed first long after a save                       = 0x464F84EF
```

And the live guard printed the cursor it was handed: **1179603533 = 0x464F524D**,
against arena 0x230DE8 size 37888. So the edit never failed — **the
serialisation never landed in the arena at all**, which also answers #108's open
question about the cell byte.

**What is fixed:** `jt1002` now stores the arena size at A5 -4576 (Mac 0x2846,
which the port had dropped), and `l0ad0` refuses to serialise through a cursor
outside `[arena, arena+size)`. A save now leaves the file byte-identical instead
of mangling it. **Deliberate divergence:** the Mac follows its "Unable to write
geo in Save3DMap." alert with `JT[69]` = ExitToShell; on a guard trip the port
alerts and returns instead, because killing the editor would cost the author
every other unsaved area for what is a port bug. A genuine `l0854` failure still
exits as the Mac does.

At this point saving still wrote nothing, and where the cursor belonged looked
unresolvable: the Mac pushes `jt1004()`'s VALUE, `jt1002` stores a raw `NewPtr`,
and `jt129` flushes from that same `h`, so the payload and the cursor cell seemed
to want the same four bytes. Seeding `*(long *)h = h` is provably NOT the answer
(the first tag write lands on the cursor cell and the next increment re-reads
'FORM'), and it was left alone rather than guessed at. **Resolved in #110 above:
the missing piece was one addressing mode inside `l0878`, so none of the three
readings had to be wrong.** The lesson worth keeping is that the contradiction
was real evidence of a missing fourth fact, not a reason to pick a plausible fix
for a data path.

Historical detail (the original measurement):

**FILE -> SAVE produced a broken file.** `GEO005.DAT` after a save differed
from the original in **exactly two bytes**, and they are the second half of the
`FORM` magic:

```
offset 2: 0x52 0x4d  ('RM')  ->  0x84 0xef
```

Every other byte of the 12962 is identical — so the serialiser round-trips the
whole area faithfully and then lands a stray 16-bit write at buffer offset 2,
leaving a file that is no longer a valid FORM container. `tools/geo.py` and the
engine's own loader both key on that magic.

Two consequences, and one thing NOT established:

- The save is a **data-loss bug**: an author who edits and saves gets an
  unloadable area. Chain to audit: `l0742 -> l07c2 -> l0854 -> l0878 ->
  l0ad0`/`l0a4e -> jt129`. `l0ad0` writes the tag as one 4-byte `jt406` from a
  `long` parameter, which cannot half-fail, so the clobber is something else
  writing a word at +2 — start by dumping the buffer immediately after
  `l0ad0`'s FORM write.
- It is **NOT from #107**: nothing in that save chain calls l1240 or l0ee6
  (grep-checked), which are only reachable from jt290's tool-0 arm. This is a
  code-path argument, not an A/B experiment.
- **Whether the cell edit reached the buffer** was open here; #110 settled it —
  the edit lands, and the saved file differs from the original in exactly the one
  cell edge byte (offset 1301, cell 163 edge 1).

★ **BACK UP THE ONE GEO FILE BEFORE ANY SAVE TEST.** This run corrupted HEIRS'
GEO005.DAT and restored it from a checksum-verified copy; without that backup
the sample module would have been left unloadable.

**★ l1240 + l0ee6 ARE LIFTED (#107) BUT NOT RUNTIME-VERIFIED.** Those two are
the tool-0 click — the reason the editor could render and navigate but not EDIT.
Both are now full lifts from the asm (CODE 22 0x1240..0x14d6 and
0x0ee6..0x123e), every callee resolved and every push order checked. What is
NOT done is a live round trip: place a wall, FILE -> SAVE, read the edge byte
back with `tools/geo.py`. Do not claim the editor edits until that runs.

Where the drive stalled, so the next attempt starts ahead: on a
`mk_walktest_design.py`-generated design, `E` -> click OPEN (62,439) does NOT
reach the canvas — it lands on a **WALLS / OBSTRUCTIONS chooser** (15 wall
swatches on the left; a radio list OPEN / OPEN SECRET / BLOCKED / FALSE DOOR /
LOCKED / LOCKED SECRET / LOCKED WIZARD / LOCKED WIZARD SECRET / LOCKED KEY1..8
on the right, with OPEN|CANCEL in the title bar at y≈78). Clicks at (85,78) and
(285,78) moved the pointer but did not commit, and the panel is drawn over
un-erased remnants of the previous screen (sprite garbage along the top, the
picker's button row still reading "PLEASE SELECT AN ADVENTURE"). That
composition state smells like the generated-design art gap (#94/#106), not the
click handler. **HEIRS reaches the canvas directly** — use it, backing up just
the one GEO file first.

**jt244 is never called.** A full-session coverage probe
(`EXTRA_CFLAGS=-DFRUA_ENGINE_PROBE_ONCE`, read `DBG.LOG`) over entry + both
loads logs `L0004_22` → `L0096` → `jt243` → `jt248`, and `jt233` for the area
load — never `jt244`, and never `jt325` on this path. `l0096` case 19 is
compiled and correct; the open question is which handler is supposed to RETURN
19, not who receives it.
Verified 2026-07-26 against a `tools/geo.py`-authored 9x9 room: the editor drew
the room and its perimeter walls correctly and reported `WD 9 HT 9`, which
cross-validates the offline GEO writer against the engine's own reader.

## How it is reached

Main menu (`jt315`) → **`E` / "Edit Modules"** → `l0004_22(7)` → editor mode 2 →
`jt243`. Inside, the CODE 22 command dispatcher `l0096` routes editor commands:

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

## Automated round-trip test

`tests/test_geo_editor_roundtrip.py` locks down the contract that the editor and
`geo.py` share the same on-disk GEO. It asserts, at the **file-contract** level:

1. a synthetic edit → save → reload persists every change (walls, entry point,
   an attached event) and is idempotent — what re-opening a saved area must do;
2. **byte-for-byte** `parse → build` against **real engine/editor-authored**
   `GEO*.DAT` (all 199 staged areas pass) — proof `geo.py` reads and writes
   exactly what the in-engine editor saves. Skipped when `data/work/gamedata`
   is absent (the copyrighted files are git-ignored, so this is a no-op in CI).

It is deliberately **not** a live GUI drive: the editor's Save is behind a Mac
press-drag pulldown, and synthetic input can't reach it reliably (Alt+letter
menu accelerators race the modifier release — `compat/events.c:169`; a held
mouse button under Hatari's SDL grab deadlocks the X server; the SDL window is
recreated on every video-mode change). Driving that would be flaky, so the test
asserts the invariant the GUI round-trip rests on instead. See the
`run-falcon-port` skill's input notes for the headless-editor recipe (keyboard
to reach it; assert by saving and re-reading with `geo.py`).

## Driving it headlessly — what a live session actually found (2026-07-27, #93)

Walking in from the main menu with `E` → `click` OPEN at (62, 439) works exactly
as described above. Three things learned by doing it that are not obvious from
the code:

- **A plain `click` DOES open a pulldown** (the menu stays up), but selecting an
  item needs the press-drag-release `drag`, as documented. The MAP menu item
  coordinates in the table above are correct.
- **Grid clicks only MOVE THE CURSOR.** They select the edited cell and re-render
  the 3D preview; they do not place anything. In `3-D VIEW` the placement
  metaphor is the 3D pane, and the `PLACE` command-bar button on its own did
  nothing observable in this session — neither the 3D pane, the grid nor the
  status text changed. Placement was not driven to a confirmed wall write.
- **`AREA VIEW` has a redraw defect**: switching to it leaves the previous 3D
  bitmap on screen with only a sliver of top-down map drawn over the top-left
  corner. Stable across a 6 s settle, so not a mid-redraw artefact. Switching
  back to `3-D VIEW` restores a correct canvas. Not investigated further.

**The editor is a good READER even when placement is not driven.** Pointing it
at a `geo.py`-authored room and reading the grid is a cheap, authoritative check
that the offline writer produced what the engine believes: the corridor walls
authored for #61 showed up in the editor's grid exactly where intended, which
is how they were confirmed present before chasing why they were not visible in
play.
