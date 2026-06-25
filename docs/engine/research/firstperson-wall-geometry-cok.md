# First-Person 3-D Dungeon Wall Geometry — DOS Champions of Krynn (older Gold Box engine)

**Status: CONFIRMED from COAB source** for the geometry (viewport, depth slices, the
156-column layout, the offset/relocation rule and the cell-walk), cross-checked
byte-for-byte against the CoK DOS data in this repo. A couple of derived numbers
(exact tile counts per 8X8D block, the precise meaning of the pieces' artwork) are marked
PARTIAL where noted.

This document tells a future implementer exactly how the engine turns
`party (x, y, facing)` + the GEO wall planes + the `WALLDEF*.DAX` 5×156 index table into
8×8 tiles blitted into the dungeon viewport. It is a clean-room study of the open-source
**COAB** re-implementation (`github.com/simeonpilgrim/coab`, MIT/public), which targets the
*same* older Gold Box engine (Curse of the Azure Bonds). The SSI executables were **not**
decompiled. Source files studied (verbatim): `engine/ovr031.cs` (all the Draw3dWorld*
routines, `draw_3D_8x8_titles`, `LoadWalldef`), `engine/ovr038.cs` (`Put8x8Symbol`,
`Load8x8D`), `engine/seg040.cs` (`OverlayUnbounded` → `draw_combat_picture` →
`draw_clipped_picture`, `DrawColorBlock`), `Classes/GeoBlock.cs` (`WallDefs`,
`WallDefBlock.Offset`, `MapInfo`), `Classes/Gbl.cs` (`symbol_set_fix`,
`MapDirection{X,Y}Delta`).

---

## 0. TL;DR data flow

```
GEO plane bytes  ──getMap_wall_type(dir,y,x)──►  wall slot id (0..15) per cell-edge
                                                   │ (this is the WALLDEF "arg_2/slot")
party (x,y,facing) ──Draw3dWorld── walks cells ──► draw_3D_8x8_titles(pieceIdx, slot, row, col)
                                                   │
WALLDEF*.DAX  ──5×156 table, .Offset relocation──► Id(slice, idxOffset[pieceIdx]+i) = 8x8 tile id
                                                   │
8X8D*.DAX  ──Put8x8Symbol(set-routed by id)──────► OverlayUnbounded → draw_clipped_picture
                                                   │  blits one 8×8 tile at pixel
                                                   ▼  ((col+3)*8 , (row+3)*8), clipped to 8..176
                                              dungeon viewport (8,8)…(176,176) = 168×168 px
```

There are **TWO different "5×156" indices** in play and they must not be confused:
- The **5 rows** of a `WallDefBlock` are the engine's "symbol slots" (the GEO wall byte
  low-nibble, 0..15 — but only 5 distinct *visual* slots are stored per wall set; see §6).
  In `draw_3D_8x8_titles` the row is `slice = (arg_2 - 1) % 5`, where `arg_2` is the wall
  slot id from the map. **It is NOT depth.**
- The **156 columns** are a flat pool of per-*screen-piece* tile runs. Which run you read
  is chosen by `idxOffset[pieceIdx]`, and how many tiles you read is
  `colCount[pieceIdx] × rowCount[pieceIdx]`. **The depth comes from which of the 10 screen
  pieces (`pieceIdx`) you draw, set by the Far/Mid/Near pass — not from the table rows.**

This corrects the natural-but-wrong reading in `dax-complex-subframe.md §C` that "the 5
rows correspond to the 5 depth slices." **The 5 rows are wall-graphic variants (slots);
depth is encoded in the 10 screen-piece definitions.** (The §C statement that WALLDEF is a
non-graphics 5×156 byte-id table is still correct.)

---

## 1. The dungeon viewport — pixel bounds  (CONFIRMED)

The full screen is EGA 320×200. The 3-D view occupies a fixed square in the upper-left.

Trace of the blit pixel math (`seg040.cs`):

```
Put8x8Symbol(1, true, id, rowY+2, colX+2)            // ovr031.draw_3D_8x8_titles
  → OverlayUnbounded(set, 1, id, rowY+2, colX+2)      // ovr038
      → draw_combat_picture(dax, (rowY+2)+1, (colX+2)+1, id)   // seg040: +1 more
          → draw_clipped_picture(dax, ROW, COL, id, clip 8,176, 8,176)
              minY = ROW*8 ;  maxY = minY + dax.height        // height = 8
              minX = COL*8 ;  maxX = minX + dax.width*8        // width  = 1*8 = 8
              pixels only drawn where 8 <= x < 176 and 8 <= y < 176
```

So a wall tile addressed by `draw_3D_8x8_titles` at draw-grid `(colX, rowY)` lands at:

```
pixelX = (colX + 3) * 8        // +2 (titles) +1 (OverlayUnbounded) = +3
pixelY = (rowY + 3) * 8
tile size = 8 × 8 px, clipped to the box  x∈[8,176)  y∈[8,176)
```

**Dungeon viewport = the 168×168 px box from (8, 8) to (176, 176).** In 8-px tile-grid
terms that is grid columns/rows **1 through 21** (21 = 168/8). The "background" fill
(`Draw3dWorldBackground`) paints the same region: sky `DrawColorBlock(sky,0x2c,11,16,2)` etc.
all anchor at `colX*8+8 .. +11*8` and `lineY+8`, i.e. the 11-tile-wide × ~21-tall band
starting at x=8.

The on-screen draw-grid the wall code uses (`Column_*`, `Row_*`, `Col`, `var_12` offsets)
spans roughly **colX 0..10** and **rowY 0..10** (the code explicitly guards
`rowY>=0 && rowY<=10 && colX>=0 && colX<=10` in `draw_3D_8x8_titles`). With the +3 blit
offset that places tiles at pixel columns 24..104 / rows 24..104 nominally, but pieces with
large `colCount`/`rowCount` (e.g. the 7-wide, 11-tall near pieces) extend a piece's tiles
out to fill the whole 8..176 box; the clip box is the true bound.

| Quantity | Value | Source |
|---|---|---|
| Screen | 320 × 200 EGA | engine |
| Dungeon viewport | x 8..176, y 8..176 → **168×168 px** | `draw_clipped_picture` clip in `draw_combat_picture` |
| Tile size | **8 × 8 px**, chunky-4bpp, EGA-16 | `8X8D*.DAX`, `Put8x8Symbol` width=1·8 height=8 |
| Blit anchor of `draw_3D_8x8_titles(.., row, col)` | pixel `((col+3)*8, (row+3)*8)` | +2 +1 offsets traced above |

---

## 2. The "5 depth slices" — what they really are  (CONFIRMED)

The engine does **not** project a continuous distance. It draws exactly **three depth
passes** plus near/far side fans, walking out from the party. `Draw3dWorld` (sub_71820):

```
drawStep = 2                          // start FAR
drawX = partyX + drawStep*dirX[facing]   // 2 cells ahead
drawY = partyY + drawStep*dirY[facing]
do {
  drawStep==2 → Draw3dWorldFar (cells 2..? ahead)
  drawStep==1 → Draw3dWorldMid (cell 1 ahead, wider)
  drawStep==0 → Draw3dWorldNear (the party's own cell row)
  drawX += dirX[behind]; drawY += dirY[behind]   // step BACK toward the party
  drawStep--
} while (drawStep >= 0)
```

So the painter order is **far → mid → near** (near overpaints far). "Depth" is therefore a
3-level discrete model (Far=2 cells out, Mid=1 cell out, Near=0, the party's row), **not 5**.

Where does "5" come from then? `draw_3D_8x8_titles` computes
`slice = (arg_2 - 1) % 5` and `wallset = (arg_2 - 1) / 5`, where `arg_2` is the wall **slot
id** (1..15) read from the GEO map. So the WALLDEF rows index **which of 5 wall-graphic
variants** (e.g. plain stone / door / archway / secret / special) the map cell asked for,
and `wallset` (0,1,2) selects which of the up-to-3 loaded `WallDefBlock`s (symbol sets). A
slot id 1..5 → wallset 0; 6..10 → wallset 1; 11..15 → wallset 2.

**Answer to Q1:** there are **3 real depth levels** (near=party cell, mid=1 ahead, far=2
ahead), drawn far-first. The "5" in the 5×156 table is **wall-variant rows, not depth**.
Nearest = Near pass (drawn last, largest tiles); farthest = Far pass (drawn first,
smallest, near the vanishing point). Confidence: **CONFIRMED**.

---

## 3. The 10 on-screen wall pieces and the 156-column pool  (CONFIRMED)

Every wall the engine draws is one of **10 named "pieces"** (`pieceIdx` 0..9), each a fixed
rectangle of 8×8 tiles. `draw_3D_8x8_titles(pieceIdx, slot, rowStart, colStart)` reads a run
of tiles out of the 156-column pool starting at `idxOffset[pieceIdx]` and lays them out
`rowCount[pieceIdx]` tall × `colCount[pieceIdx]` wide beginning at draw-grid
`(colStart, rowStart)`:

```csharp
// ovr031.cs — VERIFIED verbatim
static byte[] idxOffset = { 0, 2, 6, 10, 22, 38, 54, 110, 132, 154, 1 };  // 11 entries
static int[]  colCount  = { 1, 1, 1,  3,  2,  2,  7,   2,   2,   1 };      // 10 pieces
static int[]  rowCount  = { 2, 4, 4,  4,  8,  8,  8,  11,  11,   2 };

void draw_3D_8x8_titles(int pieceIdx, int slot, int rowStart, int colStart) {
    int idx    = idxOffset[pieceIdx];
    int colMax = colCount[pieceIdx] + colStart;
    int rowMax = rowCount[pieceIdx] + rowStart;
    int wallset = (slot - 1) / 5;
    int slice   = (slot - 1) % 5;
    for (int rowY = rowStart; rowY < rowMax; rowY++)
      for (int colX = colStart; colX < colMax; colX++) {
        int tileId = gbl.wallDef.blocks[wallset].Id(slice, idx);   // 5×156 table read
        if (rowY in 0..10 && colX in 0..10 && tileId > 0)
            Put8x8Symbol(1, true, tileId, rowY+2, colX+2);          // blit one 8×8 tile
        idx++;
      }
}
```

Note `idx` increments **across the whole run** (row-major over the piece's
`rowCount×colCount` rectangle), so the 156-column pool is partitioned into contiguous runs.
The 11th `idxOffset` entry (`1`) is the start used by piece **9** (`Row_J` filler, see Far
side-cap usage), and the runs deliberately overlap at the low end.

### 3.1 Piece table — index, size, base column, and which pass/anchor uses it

| pieceIdx | idxOffset (col base) | rows × cols (tiles) | run length | Used by (pass: surface) | Draw anchor consts |
|---:|---:|:---:|---:|---|---|
| 0 | 0   | 2 × 1  | 2  | **Far: front/facing wall** (both L & R fans) | `Row_A=4, Column_A=5` |
| 1 | 2   | 4 × 1  | 4  | **Far: left side wall** | `Row_B=3, Column_B=4` |
| 2 | 6   | 4 × 1  | 4  | **Far: right side wall** | `Row_C=3, Column_C=6` |
| 3 | 10  | 4 × 3  | 12 | **Mid: front/facing wall** | `Row_D=3, Column_D=4` |
| 4 | 22  | 8 × 2  | 16 | **Mid: left side wall** | `Row_E=1, Column_E=2` |
| 5 | 38  | 8 × 2  | 16 | **Mid: right side wall** | `Row_F=1, Column_F=7` |
| 6 | 54  | 8 × 7  | 56 | **Near: front/facing wall** | `Row_G=1, Column_G=2` |
| 7 | 110 | 11 × 2 | 22 | **Near: left side wall** | `Row_H=0, Column_H=0` |
| 8 | 132 | 11 × 2 | 22 | **Near: right side wall** | `Row_I=0, Column_I=9` |
| 9 | 154 | 2 × 1  | 2  | **Far: side-cap filler** (between adjacent far fronts) | `Row_J=4, Column_J=5` |

Sum of run lengths = 2+4+4+12+16+16+56+22+22+2 = **156** ✓ (exactly fills the pool).

So the "156 columns" decompose as: **far front(2) | far-left(4) | far-right(4) | mid
front(12) | mid-left(16) | mid-right(16) | near front(56) | near-left(22) | near-right(22)
| far-cap(2)**, the column bases being exactly `idxOffset`.

### 3.2 Pixel rectangle for each piece (the (x,y,w,h) you asked for)

A piece anchored at draw-grid `(colStart, rowStart)` with `cols × rows` tiles occupies
pixel rectangle (before the 8..176 clip):

```
x = (colStart + 3) * 8
y = (rowStart + 3) * 8
w = cols * 8
h = rows * 8
```

Using the base anchors from the table (with the per-fan `Col`/`var_12` lateral shift = 0,
i.e. the centre-most instance):

| Piece | base (colStart,rowStart) | base pixel (x,y) | size (w,h) px |
|---|---|---|---|
| 0 Far front  | (5,4)  | (64, 56)  | 8×16 |
| 1 Far left   | (4,3)  | (56, 48)  | 8×32 |
| 2 Far right  | (6,3)  | (72, 48)  | 8×32 |
| 3 Mid front  | (4,3)  | (56, 48)  | 24×32 |
| 4 Mid left   | (2,1)  | (40, 32)  | 16×64 |
| 5 Mid right  | (7,1)  | (80, 32)  | 16×64 |
| 6 Near front | (2,1)  | (40, 32)  | 56×64 |
| 7 Near left  | (0,0)  | (24, 24)  | 16×88 |
| 8 Near right | (9,0)  | (96, 24)  | 16×88 |
| 9 Far cap    | (5,4)  | (64, 56)  | 8×16 |

These base anchors get a lateral `Col`/`var_12` added per fan position (see §5), shifting the
rectangle left/right to place far-left/left/center/right/far-right instances. Anything
falling outside x∈[8,176)/y∈[8,176) is clipped. Confidence: **CONFIRMED** (constants and
math are verbatim; the pixel rectangles are derived arithmetic).

---

## 4. How a WALLDEF tile-ID expands to a run of 8×8 tiles  (CONFIRMED)

A single GEO **wall slot** (one byte-edge, value 1..15) does **not** map to one tile — it
maps to a whole `rows×cols` rectangle of 8×8 tiles via `draw_3D_8x8_titles`:

- The slot picks the WALLDEF table **row** (`slice = (slot-1)%5`) and the **wallset**
  (`(slot-1)/5`).
- The **piece** (chosen by the Far/Mid/Near caller) picks the starting **column**
  `idxOffset[piece]` and the rectangle shape `rowCount×colCount`.
- The loop then reads `rowCount×colCount` consecutive entries `Id(slice, idxOffset+i)` and
  blits each as one 8×8 tile, row-major, top-left to bottom-right of the piece's rectangle.

Example tile counts a single wall surface expands to: a **near front** wall = 8×7 = **56**
tiles (a 56×64-px slab); a **near side** wall = 11×2 = **22** tiles; a **far front** wall =
2×1 = **2** tiles. So one map wall-edge becomes a perspective-correct slab of many 8×8
tiles, the count growing with proximity. Confidence: **CONFIRMED**.

---

## 5. The cell-walk algorithm (party pose → which cells/columns)  (CONFIRMED)

### 5.1 Direction model

Directions are the 8-way Gold Box compass (`MapDirection{X,Y}Delta`, index 0..7):

```
dir:    0=N    1=NE   2=E    3=SE   4=S    5=SW   6=W    7=NW
Xdelta: 0,   1,   1,   1,   0,  -1,  -1,  -1
Ydelta:-1,  -1,   0,   1,   1,   1,   0,  -1
```

Party `facing` (`partyDir`) is always even (0/2/4/6). Derived:
`dir_left = (partyDir+6)%8`, `dir_right = (partyDir+2)%8`, `dir_behind=(partyDir+4)%8`.

### 5.2 GEO wall lookup (IMPORTANT cross-reference correction)

`getMap_wall_type(direction, mapY, mapX)` returns the wall **slot** on the given edge of
cell `(mapX,mapY)`. From `Classes/GeoBlock.cs::MapInfo`, the GEO planes pack **two
directions per plane as nibbles** (this differs from our current `geo.ts`!):

```
plane0 byte = (N<<4) | E            // wall_type_dir_0 = N, wall_type_dir_2 = E
plane1 byte = (S<<4) | W            // wall_type_dir_4 = S, wall_type_dir_6 = W
plane2 byte = x2                    // backdrop/zone/special (NOT a wall plane)
plane3 byte = door flags, 2 bits/dir: dir0=bits0-1, dir2=2-3, dir4=4-5, dir6=6-7
```

i.e. each cell stores **all four** wall edges, as 4-bit slot ids (0=open, 1..15=wall
graphic slot). Out-of-range coords wrap (`mapX/Y` clamped/wrapped to 0..15) unless the ECL
block is an overland/special (0 or 10), where off-map reads return 0 (open).

> Cross-ref note for `packages/engine/src/loaders/geo.ts`: our loader currently treats
> plane0 as a **full-byte** North wall and plane1 as a **full-byte** East wall, and derives
> S/W from neighbours. COAB shows the authentic layout is **two nibble-packed directions
> per plane** (plane0 = N|E, plane1 = S|W), with S and W stored explicitly per cell (no
> derivation needed), plane2 = backdrop, plane3 = 2-bit door flags. This is a real
> discrepancy to reconcile when wiring real wall rendering. (Not changing geo.ts here —
> out of scope; flagged for the engine track.)

### 5.3 The three passes (verbatim logic, condensed)

All three start from `drawX/drawY` (the focus cell for that depth) and fan **left** then
**right**, reading the **facing** wall of each cell (front walls) and the **side** wall
(`dir_left`/`dir_right`) of each cell (oblique walls), calling `draw_3D_8x8_titles` with the
matching piece and an incrementing lateral draw-column shift.

**FAR pass** (`Draw3dWorldFar`, focus = 2 cells ahead):
- Left fan: 4 cells, step by `dir_left`, `Col -= 2` each step. For each cell, if facing
  wall ≠ 0 → `draw_3D_8x8_titles(0 /*far front*/, slot, Row_A, Column_A + Col)`; also emits
  piece 9 (`Row_J, Column_J + Col + 1`) as a side-cap when transitioning open→wall.
- Right fan: mirror, step by `dir_right`, `Col += 2`, far front at `Column_A + Col`, cap at
  `Column_J + Col - 1`.
- Left side walls: 3 cells along `dir_left`, piece 1 (`Row_B, Column_B + Col`, `Col -= 2`).
- Right side walls: 3 cells along `dir_right`, piece 2 (`Row_C, Column_C + Col`, `Col += 2`).

**MID pass** (`Draw3dWorldMid`, focus = 1 cell ahead, started two cells to the left):
- 3 cells L→R (`var_12` = −6, −3, 0), step by `dir_right`:
  facing wall → piece 3 (`Row_D, Column_D + var_12`); left wall → piece 4 (`Row_E,
  Column_E + var_12`).
- 3 cells R→L (`var_12` = 6, 3, 0), step by `dir_left`:
  facing wall → piece 3 (`Row_D, Column_D + var_12`); right wall → piece 5 (`Row_F,
  Column_F + var_12`).

**NEAR pass** (`Draw3dWorldNear`, focus = party cell):
- 2 cells (`var_12` = −7, 0) stepping `dir_right` from one left:
  facing → piece 6 (`Row_G, Column_G + var_12`); left wall → piece 7 (`Row_H, Column_H +
  var_12`).
- 2 cells (`var_12` = 7, 0) stepping `dir_left` from one right:
  facing → piece 6 (`Row_G, var_12 + Column_G`); right wall → piece 8 (`Row_I, var_12 +
  Column_I`).

The lateral shift constants (`Col` ±2 in Far, `var_12` ±3 in Mid, ±7 in Near) are the
**screen widths of one cell at that depth** — they widen as you approach (2→3→7 tiles), the
forced-perspective spacing. Confidence: **CONFIRMED**.

---

## 6. 8X8D ↔ WALLDEF pairing and the `.Offset` relocation  (CONFIRMED)

### 6.1 Which 8X8D pairs with which WALLDEF

`LoadWalldef(symbolSet, block_id)` (ovr031) loads `WALLDEF{area}.DAX` block `block_id`,
splits it into `decode_size / 0x30C` records (0x30C = 780), and for each record loads a
matching tile block from `8X8D{area}.DAX` via `Load8x8D`:

```
if (blockCount > 1)  Load8x8D(idx, block_id*10 + block + 1);   // e.g. WALLDEF block 23
else                 Load8x8D(idx, block_id);                   //      → 8X8D 231, 232, ...
```

So a WALLDEF block id `B` with `k` records pairs with **8X8D blocks `B*10+1 … B*10+k`** when
`k>1`, or **8X8D block `B`** when `k==1`. In this repo `8X8D1.DAX` indeed contains ids
`201, 202, 203` (= 20×10+1..3, pairing with WALLDEF1 record-group 20) and ids `11, 12, 31,
32, 23` (single-record sets, pairing 1:1 with WALLDEF blocks 1, 3, 23, etc.). The
**area digit** in both filenames matches (`WALLDEF1`↔`8X8D1`, area 1).

Verified counts (this repo, `tools/dax_decode.py`):
- `8X8D1.DAX`: 8 blocks — `201`(1416 B), `202`(1233), `203`(1457), `11`/`12`/`31`/`32`/`23`
  (2257 B each). At 32 B per 8×8 tile (8 rows × 4 B), a 2257-byte block ≈ **70 tiles**
  (header-adjusted), enough to cover the per-set tile-id range. **PARTIAL:** exact tile
  count per block depends on the 8-byte strip header (`parse_tile_strip`: payload = size−8);
  70 is the working figure, not yet asserted tile-exact.
- `WALLDEF1.DAX`: block `23` = 780 B (1 record), blocks `1` and `3` = 1560 B (2 records each)
  — matches `dax-complex-subframe.md §C`. Tile-id values: block 23 max id = **115** (0x73),
  blocks 1/3 max id = **236** (0xEC) *after* the `.Offset` relocation has been applied.

### 6.2 The relocation rule (how a WALLDEF byte → an 8X8D tile)

Two layers turn a stored WALLDEF byte into the right tile in the right 8X8D block:

**(a) `WallDefBlock.Offset(off)`** — applied at load (`LoadWalldef`):
```csharp
void Offset(int off) {                         // Classes/GeoBlock.cs
  for y in 0..4 for x in 0..155
    if (data[y,x] >= 0x2D) data[y,x] += off;   // relocate only "real tile" ids
}
```
where `off = var_A = symbol_set_fix[symbolSet] - symbol_set_fix[1]`. With
`symbol_set_fix = {0x0001, 0x002E, 0x0074, 0x00BA, 0x0100}`:
- symbolSet 1 → off = 0      (ids stay 0x2E..0x73 → set 1)
- symbolSet 2 → off = 0x46   (ids shift up into 0x74..0xB9 → set 2)
- symbolSet 3 → off = 0x8C   (ids shift up into 0xBA..0xFF → set 3)

Entries **< 0x2D** are border/reserved/"no tile" and are **never** relocated (so 0x00 / 0x01
keep their meaning). This is exactly why raw WALLDEF tables are dominated by small values at
fixed columns.

**(b) `Put8x8Symbol`** routes the (already-relocated) id to a loaded 8X8D set and subtracts
the set's base to get a tile index:
```csharp
symbol_set = id in 0x01..0x2D →0 ; 0x2E..0x73 →1 ; 0x74..0xB9 →2 ; 0xBA..0xFF →3 ; 0x100..0x127 →4
tile_index = id - symbol_set_fix[symbol_set]      // 0-based index into that 8X8D block
blit symbol_8x8_set[symbol_set].data[tile_index * bpp ...]   // bpp = 32 (8×8 4bpp)
```

So the full path is: **WALLDEF byte → (+Offset reloc) → routed to 8X8D set by range →
minus set base → tile index → 8×8 pixel slab in that 8X8D block.** Confidence: **CONFIRMED**
(all arithmetic verbatim; tile-pixel decode already proven by `dax_decode.py`).

---

## 7. Worked example — party at facing North, a wall 1 cell ahead

Setup: `partyDir = 0` (North), so `dirX/dirY[0] = (0,-1)`;
`dir_left = 6 (W, Δ=(-1,0))`, `dir_right = 2 (E, Δ=(1,0))`, `dir_behind = 4 (S, Δ=(0,1))`.
Party at map `(x=8, y=8)`. Suppose the cell directly north of the party `(8,7)` has a solid
**north** wall of slot **1** (plain stone, wallset 0, slice 0), and open sides.

1. `Draw3dWorld` starts FAR: `drawStep=2`, focus `(8, 8-2)=(8,6)`. Far pass reads facing
   (north) walls of the 2-cells-ahead row; assume open → nothing drawn at far depth.
2. Step back: `drawStep=1`, focus `(8,7)` (1 ahead). **MID pass**: the centre cell of the
   L→R scan (`var_12 = 0`) reads `getMap_wall_type(north, 7, 8)` → slot **1** ≠ 0, so it
   calls `draw_3D_8x8_titles(3 /*mid front*/, 1, Row_D=3, Column_D + 0 = 4)`.
   - Inside: `wallset = (1-1)/5 = 0`, `slice = (1-1)%5 = 0`, `idx = idxOffset[3] = 10`,
     rectangle `rowCount[3]×colCount[3] = 4×3 = 12` tiles.
   - It blits tiles for `(rowY 3..6) × (colX 4..6)`, each id =
     `wallDef.blocks[0].Id(0, 10), Id(0,11), … Id(0,21)`.
   - First tile pixel = `((4+3)*8, (3+3)*8) = (56, 48)`; the 3×4 slab fills `x 56..80,
     y 48..80` (a 24×32-px facing wall), clipped to the viewport.
3. Step back: `drawStep=0`, focus `(8,8)` party cell. **NEAR pass**: reads facing wall of
   the party's own cell (`getMap_wall_type(north, 8, 8)`) — open in this example, so the big
   56-tile near front is skipped; near side walls likewise open → nothing.

Result: a single 24×32-px stone wall slab centred high in the viewport, exactly the
"wall one step ahead" look. If `(8,8)` itself had a north wall (slot 1) you'd instead get
the **near front** piece 6 = 8×7 = 56 tiles at base `(Column_G=2, Row_G=1)` → pixels
`((2+3)*8,(1+3)*8) = (40,32)` filling a 56×64-px slab — a wall right in your face.

---

## 8. TypeScript-shaped pseudocode for an implementer

```ts
// ---- constants lifted verbatim from COAB ovr031 / Gbl ----
const IDX_OFFSET = [0, 2, 6, 10, 22, 38, 54, 110, 132, 154, 1] as const;
const COL_COUNT  = [1, 1, 1, 3, 2, 2, 7, 2, 2, 1] as const;   // pieces 0..9
const ROW_COUNT  = [2, 4, 4, 4, 8, 8, 8, 11, 11, 2] as const;
const COL_BASE   = { A:5, B:4, C:6, D:4, E:2, F:7, G:2, H:0, I:9, J:5 };
const ROW_BASE   = { A:4, B:3, C:3, D:3, E:1, F:1, G:1, H:0, I:0, J:4 };
const DIR_DX = [0, 1, 1, 1, 0, -1, -1, -1, 0];
const DIR_DY = [-1, -1, 0, 1, 1, 1, 0, -1, 0];
const SYMBOL_SET_FIX = [0x01, 0x2E, 0x74, 0xBA, 0x100];        // Put8x8Symbol bases
const VIEWPORT = { x0: 8, y0: 8, x1: 176, y1: 176 };          // clip box, 168×168

// WallDefBlock: byte[5][156], .Offset applied at load:
function offsetWallDef(table: Uint8Array /*5*156*/, off: number) {
  for (let i = 0; i < table.length; i++) if (table[i] >= 0x2D) table[i] = (table[i] + off) & 0xff;
}
function wallId(set: WallDefBlock, slice: number, col: number) { return set.data[slice * 156 + col]; }

// Blit one wall piece (rectangle of 8×8 tiles) for a given map wall-slot.
function drawPiece(pieceIdx: number, slot: number, rowStart: number, colStart: number, fb: Framebuffer) {
  if (slot === 0) return;                       // open edge, nothing to draw
  const wallset = Math.floor((slot - 1) / 5);   // 0..2 → which WallDefBlock
  const slice   = (slot - 1) % 5;               // 0..4 → wall-graphic variant row
  let idx = IDX_OFFSET[pieceIdx];
  const cols = COL_COUNT[pieceIdx], rows = ROW_COUNT[pieceIdx];
  for (let r = 0; r < rows; r++)
    for (let c = 0; c < cols; c++, idx++) {
      const colX = colStart + c, rowY = rowStart + r;
      if (rowY < 0 || rowY > 10 || colX < 0 || colX > 10) continue;
      const tileId = wallId(wallDefBlocks[wallset], slice, idx);
      if (tileId <= 0) continue;
      // route id → 8X8D set + tile index (Put8x8Symbol)
      const set = tileId<=0x2D?0 : tileId<=0x73?1 : tileId<=0xB9?2 : tileId<=0xFF?3 : 4;
      const tileIndex = tileId - SYMBOL_SET_FIX[set];
      // pixel anchor: ((colX+3)*8, (rowY+3)*8), clipped to VIEWPORT
      blit8x8Clipped(fb, tiles8x8[set], tileIndex, (colX + 3) * 8, (rowY + 3) * 8, VIEWPORT);
    }
}

// One full frame: walk far→mid→near, exactly like Draw3dWorld.
function draw3dWorld(map: GeoMap, px: number, py: number, facing: number /*0,2,4,6*/, fb: Framebuffer) {
  drawBackground(fb);                            // sky/floor color blocks first
  const left = (facing + 6) % 8, right = (facing + 2) % 8, behind = (facing + 4) % 8;
  let dx = px + 2 * DIR_DX[facing], dy = py + 2 * DIR_DY[facing];   // start 2 cells ahead
  for (let step = 2; step >= 0; step--) {
    if (step === 2) drawFar(map, facing, left, right, dx, dy, fb);
    else if (step === 1) drawMid(map, facing, left, right, dx, dy, fb);
    else drawNear(map, facing, left, right, dx, dy, fb);
    dx += DIR_DX[behind]; dy += DIR_DY[behind]; // step back toward party
  }
}

// drawFar/Mid/Near follow §5.3: fan left then right, reading facing wall (piece 0/3/6),
// side walls (piece 1&2 / 4&5 / 7&8), and the far side-cap (piece 9), advancing the
// lateral draw-column by ±2 (far), ±3 (mid), ±7 (near) per cell. wallType(map,dir,y,x)
// reads the nibble-packed GEO plane: plane0=(N<<4)|E, plane1=(S<<4)|W (see §5.2).
```

A renderer can drop this straight onto our `viewport.ts` as an alternate, art-true
projector: instead of normalised trapezoids it emits **8×8 tile blits at fixed pixel
anchors** (the COAB scheme above). The two can coexist — keep `projectViewport` for the
flat-shaded preview and add a `composeWallTiles()` that walks far→mid→near and blits real
8X8D tiles once the WALLDEF + 8X8D loaders are wired into the engine package.

---

## 9. Confidence summary

| Question | Answer | Confidence |
|---|---|---|
| Q1 Depth slices / distances | **3 discrete depths** (near=party cell, mid=+1, far=+2), painter far→near. The "5" is wall-variant rows, NOT depth. | CONFIRMED (COAB `Draw3dWorld` + `draw_3D_8x8_titles`) |
| Q2 156 cols → screen pieces | 10 pieces; pool partitioned by `idxOffset` into runs 2/4/4/12/16/16/56/22/22/2 = 156; each piece = `rows×cols` 8×8 tiles at base anchor `(Column_*,Row_*)` + lateral fan shift; pixel rect = `((col+3)*8,(row+3)*8)` size `cols·8 × rows·8`, clipped to (8,8)-(176,176). | CONFIRMED (constants verbatim; pixel rects derived) |
| Q3 tile-ID → tile run | one wall slot → `rows×cols` consecutive `Id(slice, idxOffset+i)` tiles, row-major | CONFIRMED |
| Q4 pose → columns | `Draw3dWorld` walks far(2)→mid(1)→near(0) from party, fanning L/R, reading facing + side walls per cell; lateral shift ±2/±3/±7 = per-depth cell width | CONFIRMED |
| Q5 8X8D pairing + offset | WALLDEF block B (k recs) ↔ 8X8D `B*10+1..k` (k>1) or `B` (k=1), same area digit; `.Offset` adds `fix[set]-fix[1]` to ids ≥0x2D; `Put8x8Symbol` routes by range & subtracts set base to index the 8X8D tile | CONFIRMED |
| Viewport pixel bounds | x∈[8,176), y∈[8,176) = 168×168; blit anchor `((col+3)*8,(row+3)*8)`; tiles 8×8 | CONFIRMED |
| GEO plane layout (cross-ref) | plane0=(N<<4)\|E, plane1=(S<<4)\|W, plane2=backdrop x2, plane3=2-bit door flags — **differs from current `geo.ts`** | CONFIRMED from COAB `MapInfo` (flagged; geo.ts not modified) |
| Exact tile count per 8X8D block | ≈70 (block 23) from size/32; header-adjusted exact count not asserted | PARTIAL |

*Researched by Greybox AI (Opus), 2026-06-21. Clean-room from COAB (`simeonpilgrim/coab`,
files ovr031/ovr038/seg040/GeoBlock/Gbl), cross-checked against CoK DOS `8X8D1.DAX` /
`WALLDEF1.DAX` via `tools/dax_decode.py`. SSI EXEs not decompiled.*
