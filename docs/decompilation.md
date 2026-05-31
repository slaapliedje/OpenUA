# Decompilation workflow

How the Mac 68k binary is turned into something liftable to C. Per ADR-0002
the port is hybrid C + asm; this covers producing the disassembly the lifting
works from. Run unpacking first — see `docs/mac-release.md`.

## The Mac runtime model

The application is a classic Mac **near-model** segmented program built with
THINK C:

- **`CODE 0`** is the jump table. Its 16-byte header gives the A5-world sizes
  and the table's offset from A5 (here A5+0x20), followed by 1208 eight-byte
  entries. Each unloaded entry is the routine offset word, then
  `MOVE.W #seg,-(SP) ; _LoadSeg`.
- **`CODE 1`–`CODE 22`** are the code segments. Code begins 4 bytes into each
  resource — every segment has a `LINK` prologue there. `CODE 1` is the
  THINK C runtime / main segment.
- **Inter-segment calls** are `JSR disp(A5)` with `disp = 0x20 + 2 + 8*index`
  — the call lands on the `MOVE.W` two bytes into the jump-table entry.
- **Globals** are A5-relative: `disp(A5)` with negative `disp` (31336 bytes
  below A5). The jump table sits just above A5.
- **`DATA` / `CREL` / `DREL`** — `DATA` is the initial A5-globals image;
  `CREL`/`DREL` are relocation tables, each a flat array of 16-bit words.
  Bit 0 of a word picks the base (0 = A5 world, 1 = A4 string pool); the
  rest is an even offset whose 32-bit slot gets that base added at load.
  `CREL` id _N_ relocates `CODE` _N_; `DREL` relocates `DATA`. `dis68k.py`
  applies `CREL` and resolves string-pool references against `STRS`.

Segments 2–22 carry a 4-byte header THINK C uses for its own segment
management; the disassembler treats code as starting at +4 (confirmed by the
prologues) and takes entry points from the authoritative `CODE 0` table.
Jump-table routine offsets are measured from that code start, so `dis68k.py`
adds 4 to recover resource-relative addresses.

## tools/dis68k.py

Drives `m68k-atari-mint-objdump` over each segment and annotates the output:

- A-line `$Axxx` instructions named as Toolbox/OS traps (`tools/mactraps.py`).
- Inter-segment `JSR disp(A5)` resolved to `-> CODE seg+offset (JT[n])`.
- Intra-segment branch targets turned into `Lxxxx:` labels.
- Jump-table entry points marked `entry_jtN:`, keyed to `jumptable.txt`.
- CREL-relocated absolute operands resolved to `STRS+offset "string"` (the
  A4 string pool) or `A5+offset` (the A5 world).

```sh
python3 tools/dis68k.py data/work/UnlimitedAdventures.rfork
```

Output lands in `data/work/disasm/` (git-ignored — ADR-0009): `CODE_NN.s`
annotated listings, `CODE_NN.bin` raw segments, `jumptable.txt`, and
`DATA/CREL/DREL.bin`.

## Segment map

The trap distribution splits the program cleanly:

- **`CODE 1`–`CODE 6`** carry nearly all the Toolbox traps — the shell, event
  loop, resource loading, menus/dialogs, and drawing.
- **`CODE 7`–`CODE 22`** are trap-free game logic — combat, rules, map and
  module data processing — reaching the Toolbox only indirectly through the
  jump table. This is the bulk of the Gold Box engine.

`dis68k.py` prints live per-segment byte / instruction / trap / JT-call counts.

## Runtime startup

`CODE 1` is entirely the THINK C runtime — no FRUA code. The application
entry `JT[0]` → `CODE 1+0x0c` runs the startup, then hands off:

1. **Load `STRS`** — coalesce the `STRS` resource(s) into a buffer; `A4`
   holds the THINK C string pool thereafter.
2. **Build the A5 world** — `ZERO` + `DATA` resources form the initial
   globals image; `DREL` relocates A5-relative pointers into it.
3. **Patch traps** — hook `_LoadSeg` so each segment load also applies that
   segment's `CREL` code relocations, and hook `_ExitToShell` for cleanup.
4. **Call `JT[12]` → `CODE 6+0x58a`** — the application's `main()`.

`CODE 1`'s other exports (`JT[1]`–`JT[9]`) are THINK C library glue: 32-bit
multiply / divide / modulo (the 68000 has no 32-bit division), `switch`
dispatchers, and handle-state helpers.

None of `CODE 1` is ported as engine code. It becomes the ordinary C runtime
/ crt0: the A5/A4 world becomes linked globals plus a string table, and
`_LoadSeg`/`CREL` relocation disappears — the port links one flat executable.

### The application main() — CODE 6+0x58a

`main()` is the bootstrap. Its phases:

1. **Module-init roll-call** (`0x58e`–`0x5ca`) — ~16 calls to empty `rts`
   stubs; the only effect is paging the segments resident (above).
2. **Core init — `L4cc0`** — `DBInit` (`JT[1002]`) allocates the 37888-byte
   DB arena; `JT[387]` allocates three working buffers (1024 / 2064 / 4590
   bytes); seven helpers (`L30cc`…`L31a4`) each fill a `(count, elem_size,
   base)` data-table descriptor — counts 8 / 640 / 400 / 60 / 70 / 68,
   element sizes 398 / 62 / 10 / 398 / 34 / 26 — backed by `JT[387]` or by
   the DB arena's bump allocator (`L531e`). Lifted → `src/engine/core.c`.
3. **Screen mode** (`0x5d2`–`0x614`) — `JT[398]` returns a capability/status
   word; `JT[1079]` then brings up the screen at 450×214 or 400×160
   depending on it.
4. **UI handlers** — `JT[989]` registers callbacks (`CODE 6+0x538`,
   `CODE 6+0x4c0`) passed as `(id, ptr, flag, fnptr)`.
5. **String checks** (from `0x67e`) — a run of `JT[475]`(index) fetches
   table strings and `JT[393]` compares them against fixed constants,
   branching on the result; the precise purpose needs the CREL-relocated
   constants resolved.
6. **Segment-cycling loop** (`L073e`–`L0792`) — loads, uses, and
   `_UnLoadSeg`s transient segments (CODE 2, 8, 9, 11, 12, 20, 21, 22),
   looping on `JT[315]`.

`main()` runs `0x58a`–`0x7da`. Lifted → `src/engine/boot.c` as `ua_main()`:
the roll-call and every `_LoadSeg` / `_UnLoadSeg` collapse — a flat
executable has no segments to page — leaving the real control flow. The ~29
routines `main()` calls that are not lifted yet are no-op stubs in `boot.c`,
each tagged with its CODE location: the runtime port's to-do list.

Frequently-called shared routines, worth naming early when lifting:

| Entry      | Segment        | Apparent role                       |
|------------|----------------|-------------------------------------|
| `JT[387]`  | CODE 3+0x36bc  | `alloc(size)` — memory allocator    |
| `JT[398]`  | CODE 3+0x37e4  | capability / status query           |
| `JT[475]`  | CODE 3+0x3da   | bounds-checked string-table lookup  |
| `JT[393]`  | CODE 3+0x3b8c  | `strcmp` — signed-char string compare |
| `JT[989]`  | CODE 5+0x1b56  | install a handler/callback          |
| `JT[1079]` | CODE 5+0x4     | screen / window setup               |

CODE 3 reads as the utilities + resource segment; CODE 5 as display / UI.

`JT[387]`, `JT[393]`, and `JT[475]` are lifted into `src/engine/`
(`alloc.c`, `str.c`); the rest above are still characterised, not yet lifted.
`core_init` and the DB arena (`JT[1002]` / `JT[1004]`, `L531e`) are lifted
into `core.c`, with `JT[421]` — `ua_alloc`'s long-size sibling — in
`alloc.c`. `core_init` calls four routines still in unlifted segments
(`JT[442]`, `JT[231]`, `JT[211]`, `JT[981]`); they are no-op stubs for now.

Per ADR-0008 this `main()` is the runtime-first trace target: the
post-roll-call setup and the path to playing a `.DSN` lead out from here.

## Subsystems

Maps of FRUA subsystems as they are traced, to guide lifting into
`src/engine/`.

### Play path — boot to a running adventure (the road map)

Traced route from the boot to actually playing a module, with the
current lift status of each gate. The runtime spine:

```
ua_main (CODE 6)
  └─ while (jt315())            top-level loop predicate  [stub: 1-shot]
       └─ l07dc()               per-iteration PLAY BODY    [lifted]
            ├─ jt918(1)         party / new-game menu      [skeleton; dispatches]
            │    └─ case 9 "Begin Adventuring" -> L1142    [lifted]
            │         └─ jt585()  save state + slot         [partial, CODE 15]
            ├─ L0bbc()          ENTER A LEVEL               [LIFTED]
            │    └─ jt198(level)  load the GEO map           [lifted — GEO loader]
            │         + place the party from the start tile
            └─ (CODE 20 play loop, ~4500 lines)             [movement core lifted]
                 ├─ party_step: turn/move + collision        [LIFTED — clean model]
                 └─ first-person 3D VIEW                     [LIFTED — textured]
                      render_3d_view (from-scratch perspective,
                        DUNGCOM.TLB wall tile texture-mapped)
                      jt954 (CODE 21) / jt332 (CODE 8)        [faithful blit: NOT lifted]
                           pre-rendered wall pieces via jt995
```

**Movement loop (working).** The play loop's render-input-move-render
cycle runs as `port_play_demo` (an interactive automap walk; `make
FRUA_MAP_DEMO=1`): `L0bbc` enters a level (loads the map via `jt198`,
places the party), then each frame draws the automap + party marker
and reads a key. `party_step` is the movement model distilled from the
CODE 20 dispatch + `JT[202]`: facing 0..7 (cardinals N=0/E=2/S=4/W=6),
turn = facing±2 mod 8, forward/back advance one cell when the facing
edge `t[(f&6)>>1]` is passable (movement nibble 0). Verified the party
walks a real HEIRS level deterministically and interactively.

**First-person 3D view (working, flat-shaded).** `render_3d_view`
draws the corridor ahead from the party's (x,y,facing): depth slices
0..3, side walls as perspective trapezoids, a front wall where the
way is blocked, over a ceiling/floor split, depth-shaded. It is
geometry-faithful to FRUA's view (the cells `JT[201]`/`JT[202]`
cover); `port_play_demo` shows it by default (`m` toggles the
automap). The walls are **textured** with a real 32x32 1bpp tile from
the dungeon wall set: `DUNGCOM.TLB` is a GLIB-of-GLIBs (item 1 = a
nested `TILE` library of 32x32 1bpp wall tiles), reached with
`L37aa`/`L2856`. `port_play_demo` loads the tiles into a table;
`render_3d_view` reads each wall's edge byte (`cell_edge`) and picks a
tile by its code (`pick_wall`), so wall/door types texture
differently. `fill_wall_trap` texture-maps the tile across the
side-wall trapezoids **perspective-correctly** (horizontal texcoord
proportional to 1/depth; front faces stay affine), depth-shaded. The
remaining gap to FRUA's exact look is the design's real wall-set ->
tile assignment, which (traced) is **not a static table**: the dungeon
wall art `DUNGCOM.TLB` is a GLIB-of-GLIBs with 5 nested `TILE`
libraries (~135 **32x32 1bpp** tiles total — FRUA also textures from
32x32 tiles), but the edge-code -> tile *index* is resolved per-frame
by the 3D renderer. The chain: `jt954` (CODE 21) -> `JT[914]` (CODE 19,
shuffles the player-record view bytes around the draws `L025a`/`L006c`)
-> `JT[342]` (CODE 8 + 0x567c), which **screen-region hit-tests** the
visible wall against the wall-piece layout table `g_a5_-10472` (8-byte
records, count `g_a5_-10474`) and returns a packed value whose nibbles
`(v>>8)&0xf` / `v&0xf` are the graphic + sub-index, then `jt332`
(CODE 8 + 0x4a16) blits it.

**Full wall-render subsystem (traced; a multi-session arc).**
The complete chain and what each part needs:

| Part | Role | Blocker to a faithful lift |
|------|------|----------------------------|
| `jt954` (CODE 21) | per-frame view orchestrate | — |
| `JT[914]` (CODE 19) | shuffle view bytes, call `L025a`/`L006c` | — |
| `jt338` (CODE 8 + 0x5504) | build the wall-slot layout `g_a5_-10472` (8-byte records: screen-x@4 at +8000, width@6, flags@8/10), laid out cumulatively | wall-set descriptors |
| `JT[342]` (CODE 8 + 0x567c) | screen-region hit-test vs `g_a5_-10472` -> packed wall value | — |
| `jt332` (CODE 8 + 0x4a16) | resolve the wall graphic **by name** (`JT[394]` sprintf + `JT[423]`) from a wall-set descriptor, then blit | file-cache name lookup; wall-set definition format |
| `jt995` (CODE 5 + 0x21fc) | the 1:1 clipped text/glyph blit (pixel-walk + the JT[1181] family; per-plane loop, **not** a scaler) | pixel-walk + dispatch core + both-axis clip + jt1135 remap — DONE (`bp_blit`). The dungeon view uses JT[200]→JT[999]→L2d4e instead (see below) |

**Option-A blit foundation — done (`bp_blit_or` / `bp_blit_andnot` /
`bp_present`).** The JT[1181] family splits into: two writing
primitives — `JT[1181]` OR/set (`bp_blit_or`) and `JT[1184]` AND-NOT/
clear (`bp_blit_andnot`), both lifted + Hatari-verified — plus two
**collision tests** `JT[1183]`/`JT[1188]` (they `andl a3@` to read the
dest and return a hit flag; they never write — used for combat/move,
not rendering) and the 2-source composites `JT[1189]`/`JT[1191]`. The
page is **1bpp** and the wall tiles are 1bpp, so there is **no colour
blit** — colour is the *pen* applied at conversion (`bp_present`'s
fg/bg), one colour per wall/depth. `L05dc`/`L05ea` + `g_a5_-3076/-3078/
-3084` are FRUA's pen-position bookkeeping (dest word addr / sub-word
shift / row stride); `bp_blit_*` take dx/dy explicitly instead.

**jt995 dispatch core — done (`bp_blit`).** Over those primitives,
`bp_blit` lifts jt995's per-blit dispatch: decode the glyph metric,
remap the position through `jt1135` (the VIDEL coord remap jt995
applies before clipping), clip the bitmap to the page in **both axes**
(0x230c..0x23fe: intersect the pixel span with the clip rect, reduce to
the visible source words, trim the partial edge words with `lmask`/
`rmask`), then dispatch by mode (0 = OR, 1 = AND-NOT). The primitives
clamp each word's composite to its row, so a word straddling the page
edge writes only its on-page bytes — the safe equivalent of jt995's
on-page clip guarantee. Verified host-bit-exact (left/right/corner
clip, no underflow) and in Hatari (`port_blit_demo`: left-clipped OR
sliver + left-clipped AND-NOT carve, both showing only their visible
12 px). jt995's collision (mode 1/3) and 2-source (mode 2) variants
remain ahead. **There is no scale loop in jt995** — see below.

### The dungeon view renders pre-rendered slot tiles 1:1 — no scaling

An earlier note here claimed "jt995's own scale loop scales a 32x32
tile to each perspective slot." That was **wrong** (corrected
2026-05-31, verified against the Mac asm). FRUA's walls are
**pre-rendered art blitted 1:1** — DUNGCOM.TLB's ~135 tiles ARE the
walls already drawn at every (distance, position) slot. There is no
runtime scaler anywhere in the path:

- `jt995` (JT[995], CODE 5+0x21fc) is the 1:1 *text/glyph* blit; its
  outer loop is over **colour planes**, not a scale ratio. The dungeon
  does not call it.
- The dungeon view calls **`JT[200]`** (CODE 7+0x59d4) per wall slot.
  It decomposes the wall code into `(group, position)`
  (`while(code>5){code-=5;group++}`), checks the wall-set enable flag
  `design_state(g_a5_-12300)[group]@4`, and draws **1 or 2 tile layers**
  (code<=1 → one; code>1 → a far face `sub+1` then a near face, `sub`
  recomputed). Group 2 → `JT[1004]`+`JT[999]` (DUNGCOM handle
  `g_a5_-4582`); other groups → `JT[114]` with handles from
  `g_a5_-27894[group*4]`.
- **`L5bfa`** (CODE 7+0x5bfa) is the raycaster — walks map cells via
  the direction-delta tables `g_a5_-27862` / `-27853`, bounds-checked by
  **`L5baa`** (against design dims `g_a5_-12300@2`,`@3`); **`L5b42`**
  sets up per-step coords (the `8000`-anchor remap when `JT[1200]==3`).
- Blit leaf: **`JT[999]`/`L309c`** (coord remap + multi-part sprite
  composite) → **`L2d4e`** (1:1 clip against `g_a5_-3050..-3056` + mode
  dispatch; mode 10 = two-segment page wrap) → **`L2970`** (mono
  row-blit). All 1:1 — `bp_blit` already implements the mono case.

So a faithful 3D view = lift `JT[200]` (tile selection) + the raycaster
(`L5bfa`/`L5baa`/`L5b42`) and draw each selected tile through the
already-verified `bp_blit`. `render_3d_view`'s option-B texture-mapper
is *less* faithful than this and is slated for replacement by it.
`pick_wall`'s code-nibble selection stands in until `JT[200]` lands.
Still ahead too: **encounters / events**, and a real **party** so
"Begin Adventuring" runs without the test scaffold.

#### Renderer status (lifted) + play-loop integration (next)

The whole renderer stack is now lifted in `src/engine/boot.c`:
`bp_blit_or`/`bp_blit_andnot` (pixel-walk) → `bp_blit` (`L2d4e` 1:1
clip+blit) → `l309c_tile` (`JT[999]` blit entry) → `jt200` (per-slot
selector) → `l5baa`/`jt210`/`l5e52` (cell readers) + `l5b42` (slot
coord-setup) → **`jt199`** (`JT[199]` frustum walker — 4 ray passes).
Everything is host- or Hatari-verified except the on-screen `jt199`
view, which needs the play-loop context below.

**The frustum walker is driven by `JT[312]` (CODE 22 + 0x23ee), the
dungeon-view render.** Its body around the `jsr JT[199]` (0x24e6) is the
integration target:

```
JT[1173](8000, 8007, ...)          ; set the view clip rect
JT[1001](16, 8000, 1, 9)           ; background / ceiling-floor fill
JT[1193](); JT[118](24, 8, 1, 0, g_a5_-22222)   ; backdrop sprites
jt199(8012, 8016,                  ; Y base 8012, X base 8016
      g_a5_-12288,                 ;   row   = party row
      g_a5_-12287,                 ;   col   = party col
      g_a5_-12286 & 7)             ;   facing
JT[117](); JT[221](...)            ; foreground overlays
```

Key facts the boot-time `port_view_demo` confirmed, and why it can't
render faithfully on its own:

- The view runs in **deep display mode** (`JT[1200]()==3`, i.e.
  `g_a5_2347==0`); the `8012`/`8016` and `8000`-anchored coords are
  remapped down by `l5b42`/`l309c_tile`'s `jt1135`. The demo saw
  `jt1200()==0` (plain) at boot — wrong coordinate regime.
- **Party state** lives in `g_a5_-12288` (row) / `-12287` (col) /
  `-12286` (facing), set by **CODE 11** (movement; e.g. from the party
  struct fields @14/@15/@16 at CODE 11+0x070c, and updated on each
  move at CODE 11+0x19b8/0x1988/0x18f0).
- The **direction-delta tables** are the first bytes of the `-27862`
  *view-state struct* (drow at `-27862+dir`, dcol at `-27853+dir`),
  initialised — together with the live slot-layout globals — by the
  view-init in **CODE 21** (the dungeon-geometry module; `jt954` =
  JT[953] = CODE 21+0x38f4 lives here). At boot these are zero, so an
  isolated demo cannot walk; the layout globals' boot values likewise
  aren't the play-time values.

So integration = lift **`JT[312]`** (the view render: clip + bg +
sprite passes + `jt199`) and the **CODE 21 view-init** that seeds the
`-27862` struct (deltas + layout) and engages deep mode, then call
`JT[312]` from the play loop with the real party globals. `jt199`
itself is ready; it just needs that runtime state.

What works today: the boot reaches the **main menu** (`jt315` builds
"Play the Game / Select a Design / ..."; the party menu `jt918` shows
"Add Character / Begin Adventuring / ..."), the design + level loaders
run (`jt361`, `jt127`, `jt198`, `jt363`), and `L0bbc` loads a level's
map and places the party. The gap to a *playable* adventure is the
**CODE 20 play loop** (movement, turns, the per-frame orchestration)
and the **CODE 21 / CODE 8 first-person view** rendering — plus a real
**party** (`g_a5_-28006` player record, the character-creation arc) so
the "Begin Adventuring" gate (`L1142`: design loaded + `g_a5_-27928`
party present) passes for real rather than via the test scaffold.
This is the next multi-session arc; the top-down automap tiles are a
separate **editor** feature (ADR-0008, deferred).

### FC — the file cache (CODE 3)

FRUA's data-file manager: a large allocated buffer holds file data; up to 48
logical "groups" map onto up to 48 de-duplicated 14-byte file records.

Data model — the Mac A5-world globals:

| Global    | Role                                                       |
|-----------|------------------------------------------------------------|
| `A5-10270`| `g_fc_buffers[]` — data-buffer pointers                    |
| `A5-10074`| `g_fc_group_table[48]` — group → record index (0xFF free)  |
| `A5-10026`| `g_fc_records[48]` — 14-byte file records (13-char name)   |
| `A5-9306` | `g_fc_record_count`                                        |
| `A5-9304` / `-9300` | data-buffer end / size                           |
| `A5-9296`…`-9292` | FC runtime cursors                               |

API — CODE 3 jump-table entries (only `FCSetup` is a confirmed original
name; the rest are inferred):

| Entry             | Routine     | Role                                  |
|-------------------|-------------|---------------------------------------|
| `JT[463]` `0x538` | `FCInit`    | allocate the data buffer, reset tables |
| `JT[464]` `0x644` | `FCSetup`   | register a group → file record        |
| `JT[466]` `0x632` | `FCCleanup` | reset the tables, dispose the buffer  |
| `JT[467]` `0x7d0` | `fc_read`   | reserve + copy bytes from the buffer  |
| `JT[458]` `0x846` | —           | diagnostic dump of the group table    |

The error reporter `JT[1084]` (CODE 5) draws an on-screen box; the
free-memory query `JT[1026]` is `_FreeMem` trap glue. Helpers: `L39d2` is
`memset`; `L3cfa`/`L3952` copy strings/records; `L3bda` compares a record
name; `L366a` is a record/list op.

Lifted → `src/engine/fc.c`: the FC lifecycle (`fc_init`, `fc_setup`,
`fc_cleanup`), the buffer reserve/read path (`fc_read`, `fc_reserve`,
`fc_ensure_space`, `fc_buffer_query`), `fc_make_room`, and the helper
`fc_remove_record`. `JT[1083]` → `ua_rand` (`src/engine/rand.c`).

Still to do: `JT[458]` — the out-of-memory diagnostic dump — is a no-op
stub (`fc_dump`); a faithful version needs the display subsystem
(`JT[1089]` formatted print, `JT[1153]` page select). `L0b7a`'s
forget-by-name path is also still to lift.

**On-disk design / module format (test targets).** A FRUA module is a
`.DSN` *directory* of `.DAT` files, not a single file. The Mac release
ships two, unpacked under `data/frua-mac/joined/` (git-ignored):

- `TUTORIAL.DSN` — minimal: `GAME001.DAT` (388 B header/index), one map
  `GEO040.DAT` (12962 B), `STRG003.DAT` (574 B strings), `SAVE/`. The
  canonical first load-test target.
- `HEIRS.DSN` — the full "Heirs to Skull Crag" sample: 27 `GEO0nn.DAT`
  maps, `MONSTnnn.DAT`, `STRGnnn.DAT`, `GAME001.DAT`.

Per-design `.DAT` files (maps, monsters, strings) load through the file
cache alongside the shared engine libraries in `Disk1`…`Disk4`
(`.GLB`/`.TLB`/`.SLB` data + `.CTL` control). `GEO0nn.DAT` are the 12962-
byte level maps; `GAME001.DAT` is the design index/header. The community
archive at `frua.rosedragon.org/modulelist` (Mac list = `.sit` StuffIt,
unpack with `unar`) is the source for broader compatibility testing once
the load path works — but the shipped `TUTORIAL.DSN` is the place to
start.

#### `GEO0nn.DAT` — the level/map container (lifted: jt198 / L7226)

A `GEO0nn.DAT` is an IFF-ish container, parsed by `L7226` into the
3746-byte design-state buffer (`g_a5_-12300`). The 4-byte chunk sizes
are **big-endian** on disk — read directly on the big-endian 68k, no
swap. Note the FRUA quirk: the outer `FORM` carries *no* 4-char
formType; it opens straight onto the `AMOD` wrapper, and `L7470`
(the chunk walker) steps into it transparently.

```
'FORM' <size = filesize-8>
  'AMOD' <size = filesize-16>          ; 12946 for the 12962-byte file
    'HDR ' <0x122 = 290>  -> design-state[0..289]
    'MAP ' <0xd80 = 3456> -> design-state[290..]
    'ENCR' <0x7d0 = 2000> -> g_a5_-13038 (NCR buffer)
    'STRG' <0x1c00 = 7168>-> g_a5_-13034 (string table)
```

The `'MAP '` chunk holds the tile grid, **6 bytes per tile**,
row-major, **packed at the map's own width** `ds[2]` (the stride is
`ds[2]`, *not* a fixed 24 — see below). It is sized for the worst
case (3456 = 576·6), so `ds[2]·ds[3] ≤ 576`; `tile(x,y)` lives at
`MAP + (y·ds[2] + x)·6`:

```
tile[6] = { N_wall, S_wall, E_wall, W_wall, 0 (reserved), floor_flag }
```

Each wall byte is 0 (no edge) or an edge code; `floor_flag` is 0..3.
The 55 distinct edge codes seen across the 26 HEIRS maps split
cleanly on **bit 7**: a set bit 7 (the `0xe0..0xff` cluster, e.g.
`0xe1`/`0xeb`/`0xf0`) is a **solid wall** (the low nibble selects
one of the wall textures); a clear bit 7 (the `0x01..0x5a` cluster,
e.g. `0x05`/`0x09`/`0x0e`) is a **door / passage**. `edge_color`
(boot.c) renders walls as a grey ramp by texture and doors as bright
colours by type — so doorways read as coloured gaps in the room
walls.

Map dimensions are **variable**: rendering all 26 HEIRS maps shows
15×38, 28×20, 19×19, 24×24, 16×16, … — anything with `w·h ≤ 576`.
The header gate only checks the product, so a renderer must not
assume `w,h ≤ 24` (an early version did and silently dropped the 10
maps wider/taller than 24).

`port_render_geo_map` / `port_render_geo_contact` (boot.c, behind
`make FRUA_MAP_DEMO=1`) paint the grid — floor squares shaded by
flag, white walls — straight into the 8-bit back buffer via
`qd_screen_pixels`, bypassing the deferred 1bpp GLIB blit. Verified
visually in Hatari: HEIRS `GEO040` (21×21) draws as a dungeon of
rooms and corridors, and the 26-map contact sheet renders every map
with its correct aspect ratio.

Header sanity gates: `ds[0]` (a big-endian word) in `100..106`, and
`ds[2]*ds[3]` (map dims) in `1..576`. A handful of HDR words at `+272`
*are* stored byte-swapped (`jt1180` flips them back) — a mixed-endian
legacy of FRUA's cross-platform data layout. GEO040.DAT validates with
`ds[0]=106`, dims `264` (11×24).

The per-design buffers (GEO read buffer `-4582`, design header `-28006`,
NCR/STRG `-13038`/`-13034`, design state `-12300`, STRG scratch `-21148`)
are allocated by `L4cc0` — lifted as a structural skeleton: the
content-load buffers are real, the combat/sprite tables are deferred.

#### `STRGnnn.DAT` — phrase tables (lifted: jt363)

A flat array of 14-byte phrase records prefixed by a **little-endian**
count word: `filesize == (count+1)*14`. `jt363` keeps a one-entry cache
(`g_a5_-10370`) keyed by a 6-byte `"STR@<n>"` tag, so a repeat request
for the same table skips the file I/O. STRG003.DAT = 574 = (40+1)·14.

#### `MONSTnnn.DAT` — monster records (lifted: L6028)

The leaf loader `L6028` (CODE 10 + 0x6028) reads a MONST record into
the design header (`g_a5_-28006 + 101`), with the same shape as the
GEO loader (file group 50, byte gate 1..450). A 450-byte file is used
raw; a packed file (< 450) is relocated to the buffer tail and
expanded back via `jt1171` — the decompressor, deferred to a stub
since every testable MONST file is stored at the full 450 bytes
(`dst[0..1]` would hold the big-endian uncompressed length).

`jt263` (CODE 10 + 0x5acc), the monster/NPC setup state machine that
drives `L6028` as one arm, is lifted as a **structural skeleton**: the
`JT[1]` state dispatch, the per-state field setup, the state-8
MONST-load arm, and the trailing `JT[3]` flag-packing switch are
faithful; the middle NPC-editor + `JT[325]` record-serialize block
(the `setup_done == 0` path, "Re-create NPC?" / "change class?"
dialogs through ~10 editor entries) is deferred. Note that `JT[1]`
(CODE 1 + 0x130) is a *value-list* dispatch — distinct from the
`JT[3]` range switch — where each inline table entry is an
`(offset, value)` pair and the branch target is `&offset + offset`.

`TUTORIAL.DSN` ships no `MONST*.DAT`; stage the full sample with
`make gamedata DSN=HEIRS.DSN` (4 MONST files + 26 GEO maps) to
exercise the loaders. The probe boots verify GEO001/002/040 parse
(dims 570/570/441), STRG cache hit/miss, and MONST101 load.

#### `JT[325]` — the record-database engine (lifted: prologue only)

`jt263`'s serialize step and several CODE 2 callers route record
I/O through `JT[325]` (CODE 9 + 0x22d8), a 1135-line stage / load /
store engine keyed by a record type (1/21/33/51/52) and a command
(0..7). It is lifted **partially**: the prologue + input command
class — file-group setup, the staging buffer (`g_a5_-22208`, the
`L30cc` block of `8*398` bytes), the cmd 2/5/6/7 BlockMove of the
caller's source into staging (+ the `rec[20..22]` tag and
`rec[2510]` marker), and cmd 0/1 clear. The ~3000-byte per-type
field-serialization tail (0x242c..0x30c2 — the cmd-3 fetch and the
`L258e` type dispatch) is deferred, so `jt325` stages the raw
record but does not yet transform fields and returns a provisional
0. The probe boot stages a 450-byte test record and confirms it
lands intact in `g_a5_-22208`.

**The tail is the record *editor*, not a data codec.** A trace
(1023 lines, ~40 entries) shows every command routes through
either `L1ae2` (CODE 9 + 0x1ae2, 566 lines — the field codec, but
it reads each record's field-layout *script* and edits fields via
the `JT[452]` cmd-arg stream parser ×6, plus `JT[1012]` GLIB,
`JT[468]`, `JT[423]`; `L0052` is its per-field descriptor accessor,
a `JT[3]` type switch 50..53 = byte/word/long over the staging
buffer) or the `L2626+` editor UI (`JT[1089]` field/"Page %2d"
strings, `JT[155]` driver, `JT[452]` menus). The return status is
written in ~10 places across the editor body, so there is no
faithful slice that completes the read/write contract without
lifting the editor — it is a multi-session subsystem arc, tracked
in the deferred-block comment in `jt325`. Record types in the
tail: 1, 21, 33, 51, 52.

### Display — screen and drawing (CODE 4 / CODE 5)

FRUA's display layer spans CODE 4 (Mac Toolbox init, page management) and
CODE 5 (drawing primitives, text, the master init). It is the domain of the
port's display HAL — ADR-0005.

**Toolbox init** — `JT[1144]` (CODE 4) is the standard Mac startup:
`InitGraf` / `InitFonts` / `InitWindows` / `InitMenus` / `TEInit` /
`InitDialogs` / `FlushEvents`. It then probes the screen depth (`A5-1318`):
8-bit colour is the target, 1-bit mono a fallback. The seven-trap startup
prologue is lifted → `compat/toolbox.c` (`toolbox_init()`); the rest of
`JT[1144]` — screen mode, the window, the colour tables, the menu bar — is
still to lift.

**Data model — double-buffered pages.** FRUA draws into two offscreen colour
ports and flips between them:

| Global    | Role                                                       |
|-----------|------------------------------------------------------------|
| `A5-2570` | two 108-byte `CGrafPort`s — the offscreen drawing pages    |
| `A5-2352` | active page index (0/1)                                    |
| `A5-2354` | the front (displayed) page                                 |
| `A5-3076` | the active page's pixel base address                       |
| `A5-2347` | colour-vs-mono mode flag                                   |

`JT[1153]` (CODE 4) flips the page — it toggles `A5-2352` and re-points
`A5-3076` at the selected port's PixMap base.

**Drawing surface — the Mac Toolbox.** CODE 4/5 draw with QuickDraw —
`SetPort` (heavily, switching window vs. pages), `MoveTo`/`LineTo`,
`PenPat`/`PenMode`, `SetRect`/`PaintRect`, `CopyBits` — and the Window
Manager (`NewWindow`, `SizeWindow`, `SelectWindow`, `FrontWindow`,
`InvalRect`). Text goes through `JT[1089]`: set colour, set pen position,
format-and-draw.

**Master init** — `JT[1079]` (CODE 5), called by `main()`, runs `JT[1144]`
(Toolbox), the offscreen-page setup (`JT[1157]` / `JT[1155]` / `JT[1138]`
and CODE 5 helpers), and `fc_init`. The `(214,450)` / `(160,400)` numbers
`main()` passes are the `fc_init` KB sizes, not screen dimensions. Lifted →
`src/engine/master.c` as `master_init()`; its teardown counterpart
`JT[1081]` — Toolbox shutdown, page teardown, `fc_cleanup` — is lifted there
too as `master_shutdown()`.

#### Mapping to the port

This confirms ADR-0005 and the `platform/include/display.h` design:

- The two offscreen `CGrafPort`s → two 8-bit paletted `dsp_surface_t`
  buffers; the page flip → the HAL's `present()`.
- QuickDraw primitives → a `compat/` QuickDraw shim drawing into HAL
  surfaces; the Window Manager mostly dissolves — the port runs fullscreen
  through the HAL rather than as a GEM app.
- 8-bit colour → the VIDEL / TT-shifter 256-colour modes the HAL backends
  already target.

### GLIB bitmap render path (jt995) — investigated, blit deferred

The game's sprites / custom fonts render through a separate bitmap path
that sits under `jt382 cmd=1` → `L148a` → `JT[995]` (CODE 5 + 0x21fc). It
is fully mapped but its low-level blit is **deferred** — see the blocker
at the end. Pieces and their state (2026-05):

**Resource format — "GLIB" libraries.** Game graphics live in *Graphics
LIBrary* files loaded through the **file cache** (`jt468(tag)` →
`g_fc_buffers[...]`, i.e. `A5-10270` indexed by `A5-10074`), *not* the Mac
`.rsc` resources. Layout:

| Offset | Field                                               |
|--------|-----------------------------------------------------|
| +0     | `long` magic `'GLIB'` (0x474C4942)                  |
| +8     | `word` item count                                   |
| +16    | `long[count]` item offset table (base-relative)     |

Each item: an 8-byte metric header then the bitmap. Header (big-endian):

| Offset | Field                                                       |
|--------|-------------------------------------------------------------|
| +0     | `word` height (rows)                                        |
| +2     | `word` y-bearing (subtracted from the pen top)              |
| +4     | `word` x-bearing (subtracted from the pen left)             |
| +6     | `byte` `bpp_w` — bytes per bitmap row                       |
| +7     | `byte` flags — bit 7 = single-row; low nibble (≤1) = valid  |

The full GLIB header, confirmed against the real `.GLB` files (see
"Validated against real data" below):

| Offset | Field                                                       |
|--------|-------------------------------------------------------------|
| +0     | `long` magic `'GLIB'`                                       |
| +4     | `long` total file size                                      |
| +8     | `word` item count                                          |
| +0A    | `word` version (0x0001 observed)                            |
| +0C    | `'DATA'` sub-tag (4 bytes)                                  |
| +10    | `long[count+1]` offset table (base-relative); the trailing |
|        | entry is an end sentinel, so item *n*'s size is            |
|        | `offset[n+1] - offset[n]`                                  |

`L37aa` only reads magic@0, count@8, and the table@16 — all validated.

**Lifted + verified:** `L37aa` (CODE 5 + 0x37aa, GLIB magic-check + offset-
table lookup) and `L2856` (CODE 5 + 0x2856, copies the 8-byte header,
returns `entry+8` = bitmap ptr). A probe-gated self-test in
`boot_a5_seed_defaults` builds a known 2-item GLIB and confirms every
extracted field. `jt406`/`L366a` confirmed = `BlockMove(src, dst, count)`
(the C `jt406(dst, src, count)` is the opposite order — mind it when
lifting new callers from asm).

**Validated against real data (2026-05):** the unpacked Mac release in
`data/frua-mac/joined/Disk3/` holds real `.GLB` libraries. `GAME.GLB`
(422 bytes): `'GLIB'` @0, count = 2 @8, table @16 = {0x1C, 0x22, 0x1A6};
item 1 @0x22 is the title string "Heirs of skull crag". `GEO.GLB`
(39096 bytes): count = 4, table = {0x24, 0xD2, 0x3374, 0x6616, 0x98B8}
(4 items + end sentinel). The `L37aa` lookup `base + offset[idx]` matches
both exactly — the synthetic self-test and real game data agree. The
8-byte *metric* header is glyph-library-specific; non-glyph libraries
(GAME.GLB title strings, GEO maps) put other content after the offset
table.

`jt995`'s structural skeleton (clip math, mode-dispatched row loop) is
present but has two known bugs to fix when the blit is done: it reads the
metric header as **bytes** where the asm reads **words** (works only for
small big-endian values), and the row-blit stubs `jt1183`/`jt1188` are
declared 6-arg where the asm passes **7**:
`(data, h, w_bytes, src_row_stride, lmask, rmask, pen_shift)`.

**The blit family — the blocker.** `JT[1177]` (column cursor) + six
masked-write variants:

| Entry      | Mode                                                       |
|------------|------------------------------------------------------------|
| `JT[1181]` | `*dest \|= (src_word >> pen_shift)` — OR / set             |
| `JT[1184]` | `*dest &= ~(src_word >> pen_shift)` — AND-NOT / clear      |
| `JT[1183]` | collision/AND variant (returns a hit byte)                 |
| `JT[1188]` | collision/AND variant, colour-mode counterpart            |
| `JT[1189]` / `JT[1191]` | 2-source composite variants                   |

These are **1-bit-per-pixel shift blits**: the source is 1bpp, masked at
the left/right edges (`lmask`/`rmask`), spread across a 32-bit window via
`swap` + `lsrl pen_shift` (a sub-word **bit** offset, 0–15, for horizontal
sub-pixel positioning), and composited word-at-a-time into a **bit-packed**
destination. Dest row stride `A5-3084` = `L04de() >> jt1200()` = 60 / 160 /
320 bytes for 1bpp / 4bpp / 8bpp — i.e. the Mac drawing page is bit-packed
at the active depth, **320×200**.

**Why it's deferred (not a faithful lift):** our Falcon shim back-buffer is
**320×400 8bpp chunky**. The Mac blit targets a bit-packed **320×200** page
— mismatched in *both* pixel format (packed bits vs one byte/pixel) *and*
resolution (200 vs 400 height). A literal translation would write
structurally-wrong bytes regardless of correct source data or a valid
`A5-3076`. The blit needs a **HAL-adapted reimplementation**, one of:

- **(A)** allocate the Mac page (320×200, active depth), run the blits into
  it bit-for-bit, then add a page→screen converter/scaler to the 8bpp/400h
  shim buffer — most faithful, most machinery; or
- **(B)** a from-scratch rasterizer: read the GLIB 1bpp glyph and draw it
  directly to the 8bpp shim at the right scale + pen colour — pragmatic,
  diverges from the asm.

**Practical gate:** this path renders GLIB *game data* that only exists once
a real design module loads through the file cache (the `jt557`/`jt585`
chain, currently scaffolded). The menu text already renders via the
`jt382 cmd=1` `DrawString` shortcut, so the blit's only *current* exercise
would be a synthetic test glyph. Banked here as a self-contained, well-
specified task; pick it up alongside (or after) real design-module loading
so there's actual content to render and verify against.

**Update (option B implemented).** `blit_glyph_1bpp` + `port_render_topview`
(boot.c) take the pragmatic route: a from-scratch 1bpp→8bpp rasterizer that
reads a GLIB glyph (`metric[0..1]` = height, `metric[6]` = `bpp_w`
bytes/row, MSB-first) and paints set bits as the pen colour straight into
the 8-bit shim buffer — bypassing the bit-packed-page mismatch entirely.
Verified against the real `TOPVIEW.TLB` (Disk1): 24 × 16×16 1bpp top-down
map tiles (items 1..24; height=16, `bpp_w`=2; items 17..24 add a 2nd mask
plane via flags bit 0; item 0 is the directory) extracted with the lifted
`L37aa`/`L2856` and rendered on the Falcon — floor/wall textures and the
directional/corridor markers. The faithful `jt995` bit-packed-page blit
(variants `JT[1181]`…) stays deferred; option B is what draws tile art for
now.

**The GEO map drawn with real tiles.** `TOPVIEW.TLB` glyphs 1..16 are the
automap cell graphics, one per wall combination — confirmed directly from
the bitmaps: tile 2 = a solid N bar, 3 = E, 5 = S, 9 = W, ... 16 = all
four, i.e. **tile = 1 + (N | E<<1 | S<<2 | W<<3)**. `port_render_geo_tiles`
computes that mask from each GEO cell's four edge bytes and rasterizes the
matching tile, so the loaded map renders as the game's own dithered-floor +
wall-bar top-down view instead of coloured cells (verified on GEO040, all
441 cells). Glyphs 17..24 (with a 2nd plane = a direction arrow) are the
door / arrow variants; doors are wired to them — solid walls draw the
white wall tile, each special edge overlays the matching directional door
glyph (N=17, S=19, E=20, W=18) in a distinct colour, so walls and doors
read apart on the automap.

**Map layout + edge codes, from `JT[202]`.** `JT[202]` (CODE 7 + 0x5e52),
the runtime wall query, is the authority: the `'MAP '` data is **column-
major**, stride = height (`ds[3]`) — a tile is at `MAP + (col·h + row)·6`,
*not* row-major (an earlier renderer was transposed; harmless on square
maps but wrong). The edge byte splits in two: the **high nibble** is the
wall art — `0xe_` = a standard wall texture, `0x0_`/`0x3_` = a special edge
from GEO.GLB's 43-entry code→graphic-set table — and the **low nibble** is
the per-edge **movement type** `JT[202]` returns for collision, indexing
the editor's 16-type list (found in the rfork: *Free movement, Movement
blocked, Open, Open secret, Locked, Locked secret, Locked wizard, Lock wiz
scrt, Locked key1..8*). The full top-down tile automap is an **editor**
feature (not in the lifted runtime, ADR-0008); the runtime has `jt954`
(CODE 21, the movement / first-person view) plus the party-position inset
(`jt927`/`jt928`). So the exact automap door-*glyph* rule is the editor's,
unlifted — `port_render_geo_tiles` uses the high-nibble art split (standard
wall vs special edge) as the visual heuristic, now grounded in `JT[202]`.

## Lifting to C

Per ADR-0008 the runtime comes first. Per routine:

1. Find the routine in a `CODE_NN.s` listing (entry points are `entry_jtN:`).
2. Read it with the jump-table and trap annotations; follow `-> CODE seg+off`
   calls across segments.
3. Write the equivalent C into `src/engine/`, grouped by subsystem; keep 68k
   asm only where lifting is impractical (ADR-0002).
4. Route Toolbox traps through the `compat/` shim (ADR-0003); record them in
   `docs/toolbox-mapping.md`.

Start point: `CODE 1 + 8` (jump-table entry `JT[0]`) is the application entry
— trace from there into the play-a-`.DSN` path.
