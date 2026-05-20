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
- **`DATA` / `CREL` / `DREL`** are the initial globals image and the code/data
  relocation tables applied at load. Extracted for later; not yet processed.

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

`main()` opens with a subsystem-initialiser roll-call: ~16 back-to-back
parameterless calls, one into nearly every code segment. It then queries a
status (`JT[398]`), sets up the screen at 450×214 or 400×160 depending on it
(`JT[1079]`), installs a callback (`CODE 6+0x538`), and `_UnLoadSeg`s the
one-shot init segment `CODE 8`. `CODE 6` contains no event traps — the event
loop is reached by a call out.

Per ADR-0008 this `main()` is the runtime-first trace target: the subsystem
initialisers and the path to playing a `.DSN` lead out from here.

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
