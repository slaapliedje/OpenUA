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

### Display — screen and drawing (CODE 4 / CODE 5)

FRUA's display layer spans CODE 4 (Mac Toolbox init, page management) and
CODE 5 (drawing primitives, text, the master init). It is the domain of the
port's display HAL — ADR-0005.

**Toolbox init** — `JT[1144]` (CODE 4) is the standard Mac startup:
`InitGraf` / `InitFonts` / `InitWindows` / `InitMenus` / `TEInit` /
`InitDialogs` / `FlushEvents`. It then probes the screen depth (`A5-1318`):
8-bit colour is the target, 1-bit mono a fallback.

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
