# GEO area file format (`GEOnnn.DAT`)

One `GEOnnn.DAT` inside a `.DSN` design folder is a single **adventure area** —
a level map: its dimensions, wall layout, per-cell event hooks, the area's event
table, and its string table. The runtime loads it in `jt198 → l7226`
(`src/engine/boot.c`) into the design-state buffer (A5 global `-12300`).
`tools/geo.py` reads/writes/builds these files and round-trips them byte-for-byte.

Everything multi-byte is **big-endian** (authored on a 68k Mac). The file is
**always exactly 12962 bytes** — a fixed-layout container, not a variable IFF.

## Container

```
FORM <0x329a = filesize-8>
  AMOD <0x3292 = filesize-16>          ← a SIZED sub-chunk, not a bare IFF
    HDR  <0x122  = 290>   → design-state[0..289]     header
    MAP  <0xd80  = 3456>  → design-state[290..]      cells
    ENCR <0x7d0  = 2000>  → event table  (A5 -13038) 100 × 20 bytes
    STRG <0x1c00 = 7168>  → string table (A5 -13034)
```

`AMOD` carries a size word and doubles as the FORM's type — the "no formType"
FRUA quirk that `l7470`'s FORM branch special-cases (`boot.c:1185`). Each chunk
is `tag(4) + size(4) + body`; sizes here are all even, so no odd-byte padding
occurs in practice (the parser still honours it). `l7226` validates:

- first four bytes ∈ {`FORM`,`CAT `,`LIST`} (else the buffer is treated as raw
  legacy data and accepted as-is);
- `AMOD` size == 12946; each chunk present at its exact fixed size;
- HDR version word ∈ **100..106**;
- `width × height` ∈ **1..576**.

## HDR — design-state[0..289]

| offset | type | field |
|---|---|---|
| 0 | u16 BE | **version** (100..106; real data is 106) |
| 2 | u8 | **width** (columns) |
| 3 | u8 | **height** (rows per column) |
| 14 + i·4 | u8×3 | **entry-point `i`**: `[+0]`=Y, `[+1]`=X, `[+2]`=facing (low 3 bits) — see below |
| 48 + z·4 | u8×4 | **zone `z` rule** (z=0..7): `[+0]`=interrupt event, `[+1]` bit7 = "no resting" |
| 272 + k·2 | u16 | 8 shorts stored **byte-swapped** on disk, flipped via `jt1180` on load |

The remaining HDR bytes hold per-area design metadata (light level, save flags,
etc.) not yet individually mapped; for a bare walkable area they can stay zero.

**Party start.** `l0bbc` places the party from entry point `g_a5_-18488` (the
Game-Settings "AT ENTRY POINT" value): `st = ds + entry*4`, then
`X = st[15]`, `Y = st[14]`, `facing = st[16] & 7`. So entry 0's record lives at
`ds[14..16]`, entry 1 at `ds[18..20]`, and so on (4-byte stride).

## MAP — design-state[290..]

`width × height` cells, **6 bytes each**, laid out **column-major**:
`cell_index = height · col + row` (`jt201`/`jt212`), `col` ∈ [0,width),
`row` ∈ [0,height). Capacity is 576 cells (the MAP chunk is a fixed 3456 bytes;
cells past `width×height` are unused padding).

| byte | meaning |
|---|---|
| 0..3 | the four **edge walls** (direction = `edge/2`, i.e. `edge` 0/2/4/6). High nibble = wall id (0..15, `jt212`); low nibble = wall attribute (door/secret, read `& 15`). |
| 4 | **special** = event index + 1 into ENCR (0 = no event on this cell); `jt201` returns it |
| 5 | **zone** in bits 2..4 (`(byte>>2) & 7`, `jt197`) + misc flags in the other bits |

## ENCR — the event table (A5 `-13038`)

**100 events × 20 bytes.** Cell `special` value *N* selects event `N-1`. Byte 0
of a record is the **type** (`l709e`'s dispatch, JT[3] 0..38 — e.g. 1/33 = combat,
34 seen in the tutorial). The other 19 bytes are type-specific parameters, not
yet exhaustively mapped. A bare area with no events zero-fills this chunk.

## STRG — the string table (A5 `-13034`)

7168-byte string pool for the area's event text. **If the whole chunk is NUL the
loader re-seeds it** (`l7226` → `l4db4` + `l4e3a`), so a generated area may leave
STRG all zero and let the engine initialise it.

## Generating an area

`tools/geo.py`:

```python
from geo import Geo
g = Geo.blank(width=8, height=8)      # zeroed HDR/MAP/ENCR/STRG, version 106
g.set_entry_point(0, x=1, y=1, facing=0)
g.set_cell(col=1, row=1, walls=(0x10, 0, 0, 0))   # a wall on edge 0
open("GEO001.DAT", "wb").write(g.build())          # exactly 12962 bytes
```

`Geo.parse(g.build()) == g` round-trips exactly; verified byte-for-byte against
real tutorial and HEIRS areas (11×24, 15×38, 28×20). A minimal playable area
needs at least a valid entry point on a walkable cell; walls, events and strings
are optional. Placing the area in a design and making it the "begins in" area
(Game Settings) is what wires it into a module — see [create-new-design](../CLAUDE.md).
```
python3 tools/geo.py path/to/GEO001.DAT   # dump a summary of an existing area
```
