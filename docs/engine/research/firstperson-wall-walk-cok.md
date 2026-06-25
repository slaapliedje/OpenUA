# First-Person Cell-Walk & Symbol-Set Loading — DOS Champions of Krynn (older Gold Box)

**Status: CONFIRMED from COAB source** (verbatim transcription of the three draw routines and
`LoadWalldef` + its callers). This is the companion to
`firstperson-wall-geometry-cok.md` — that doc nailed the viewport, the 10-piece / 156-column
table, tile routing and `.Offset` relocation; this doc supplies the **exact loop bodies** for
the far→mid→near cell-walk (`composeWallView`) and the **symbol-set load wiring**
(`loadDungeonWalls`), which the geometry doc only paraphrased in §5/§6.

Clean-room study of the open-source **COAB** re-implementation
(`github.com/simeonpilgrim/coab`, MIT — *Curse of the Azure Bonds*, the **same** older Gold
Box engine generation as CoK). SSI EXEs were **not** decompiled. No GPL/COAB code is copied
verbatim into our repo — the pseudocode below is a re-expression for transcription onto our
own `wallpieces.ts` API.

Source files studied (raw, default `master` branch):
- `engine/ovr031.cs` — `Draw3dWorld` (sub_71820), `Draw3dWorldFar`, `Draw3dWorldMid`,
  `Draw3dWorldNear`, `Draw3dWorldBackground`, `draw_3D_8x8_titles` (sub_71434),
  `getMap_wall_type`, `getMap_XXX`, `MapCoordIsValid`, `LoadWalldef`, `Load3DMap`, the
  `idxOffset`/`colCount`/`rowCount` and `Row_*`/`Column_*` static fields.
- `engine/ovr003.cs` — `CMD_LoadFiles` (sub_26C41) — the ECL **Files/Pieces** opcode that
  drives dungeon init and calls `LoadWalldef`.
- `engine/ovr008.cs` — `vm_SetMemoryValue`-family switch 0x322/0x324/0x326 — the ECL
  "set memory" path that also calls `LoadWalldef`.
- `engine/ovr017.cs` — state-restore loop that re-calls `LoadWalldef` from `setBlocks[]`.
- `engine/ovr038.cs` — `Load8x8D`, `Put8x8Symbol` (set routing).
- `engine/seg001.cs` — engine boot, the fixed `Load8x8D(0,…)`/`Load8x8D(4,…)`.
- `Classes/GeoBlock.cs` — `WallDefs.LoadData`/`BlockOffset`, `WallDefBlock.LoadData`/`Id`/
  `Offset`, `MapInfo` nibble unpack.
- `Classes/Gbl.cs` — `symbol_set_fix`, `MapDirection{X,Y}Delta`, `setBlocks`, `wallDef`.
- `Classes/Sys.cs` — `WrapMinMax`.

**CoK-vs-COAB caveat:** COAB is *Curse of the Azure Bonds*, not CoK, but both are the
pre-DQK ("DAX") Gold Box engine and share this exact 3-D renderer. Every constant here was
already cross-checked against CoK's own `WALLDEF1.DAX` / `8X8D1.DAX` in the geometry doc.
The **per-dungeon block ids** come from each game's ECL scripts (see §3.4), so the *which
block* numbers differ per game/area, but the *mechanism* is identical. Flagged inline where
CoK could differ.

---

## 1. Direction model and the top-level walk (`Draw3dWorld`, sub_71820)  CONFIRMED

`Classes/Gbl.cs`:
```csharp
MapDirectionXDelta = { 0, 1, 1, 1, 0, -1, -1, -1, 0 };   // index 0..8 (8 = null/zero)
MapDirectionYDelta = { -1, -1, 0, 1, 1, 1, 0, -1, 0 };
```
`partyDir` is always even (0=N, 2=E, 4=S, 6=W). Derived in `Draw3dWorld`:
```
dir_left   = (partyDir + 6) % 8
dir_behind = (partyDir + 4) % 8
dir_right  = (partyDir + 2) % 8
```

`Draw3dWorld(partyDir, partyPosY, partyPosX)` — exact body (ovr031.cs:321):
```
Draw3dWorldBackground()                         // sky/floor color blocks, painted first
drawStep = 2
drawX = partyPosX + drawStep * MapDirectionXDelta[partyDir]   // 2 cells AHEAD
drawY = partyPosY + drawStep * MapDirectionYDelta[partyDir]
do {
    switch (drawStep) {
        case 2: Draw3dWorldFar (partyDir, dir_left, dir_right, drawX, drawY); break;
        case 1: Draw3dWorldMid (partyDir, dir_left, dir_right, drawX, drawY); break;
        case 0: Draw3dWorldNear(partyDir, dir_left, dir_right, drawX, drawY); break;
    }
    drawX += MapDirectionXDelta[dir_behind]      // step ONE cell BACK toward party
    drawY += MapDirectionYDelta[dir_behind]
    drawStep -= 1
} while (drawStep >= 0)
```
Painter order is **Far (2 ahead) → Mid (1 ahead) → Near (party cell)**; near overpaints far.
After the loop COAB calls `seg040.DrawOverlay()`. (Note: COAB also calls `Display.UpdateStop`
/ `UpdateStart` around it — pure double-buffering, ignore.)

**`getMap_wall_type(direction, mapY, mapX)`** (ovr031.cs:222) — returns the 4-bit wall slot
on edge `direction` (0/2/4/6 only) of cell `(mapX,mapY)`:
```
mi = getMap_XXX(mapY, mapX)              // null iff off-map AND special block
if (mi == null) return 0
switch(direction){0→mi.wall_type_dir_0; 2→dir_2; 4→dir_4; 6→dir_6}
```
`getMap_XXX(mapY,mapX)` (ovr031.cs:254): if `MapCoordIsValid==false` **and** `gbl.EclBlockId
∈ {0,10}` → return **null** (off-map open). Otherwise **clamp-wrap** each coord:
`>0x0F → 0`, `<0 → 0x0F` (i.e. wrap to opposite edge), then read `gbl.geo_ptr.maps[mapY,mapX]`.
`MapCoordIsValid` = `mapX∈[0,16) && mapY∈[0,16)`.

> So off-map reads only return "open" when the current ECL block is the overland/special
> block 0 or 10; in a normal dungeon block, off-map coords **wrap** to the opposite map edge
> and read a real cell. Reproduce both branches.

---

## 2. The three draw routines (verbatim pseudocode, real names)  CONFIRMED

`draw_3D_8x8_titles(offsetIndex, arg_2, rowStart, colStart)` is the §3/§4 primitive — our
`expandWallPiece(pieceIdx=offsetIndex, slot=arg_2, colStart, rowStart, readId)`. `arg_2`==0
draws nothing (the callers guard `!= 0` before calling, except piece-9 caps which pass a
saved non-zero `var_17`). Below, every `draw_3D_8x8_titles(...)` is one `expandWallPiece` call.

### 2.1 `Draw3dWorldFar(partyDir, dir_left, dir_right, drawX, drawY)` (ovr031.cs:373)

Four sequential sub-loops. `var_17` is a **carry** holding the previous cell's facing-wall
slot, used to emit the **side-cap** piece 9 between two adjacent front walls.

```
// --- LEFT FAN: front walls, 4 cells stepping dir_left, Col -= 2 each step ---
tmpX=drawX; tmpY=drawY; var_10=0; Col=0; var_17=0
while (var_10 < 4) {
    var_14 = getMap_wall_type(partyDir, tmpY, tmpX)          // facing wall of this cell
    if (MapCoordIsValid(tmpY,tmpX)==false && getMap_wall_type(dir_right,tmpY,tmpX)==0)
        var_17 = 0                                           // off-map w/ open right: drop carry
    if (var_14 != 0) {
        if (var_17 > 0)
            draw_3D_8x8_titles(9, var_17, Row_J, Column_J + Col + 1)   // CAP before this front
        var_17 = var_14
        draw_3D_8x8_titles(0, var_14, Row_A, Column_A + Col)          // FAR FRONT
    } else {
        if (var_17 > 0 &&
            getMap_wall_type(dir_left, tmpY - MapDirectionYDelta[dir_left],
                                       tmpX - MapDirectionXDelta[dir_left]) != 0)
            draw_3D_8x8_titles(9, var_17, Row_J, Column_J + Col + 1)   // CAP closing a run
        var_17 = 0
    }
    var_10++; Col -= 2
    tmpX += MapDirectionXDelta[dir_left]; tmpY += MapDirectionYDelta[dir_left]
}

// --- RIGHT FAN: front walls, 4 cells stepping dir_right, Col += 2; cap shift is Col - 1 ---
tmpX=drawX; tmpY=drawY; var_10=0; Col=0; var_17=0
while (var_10 < 4) {
    var_14 = getMap_wall_type(partyDir, tmpY, tmpX)
    if (MapCoordIsValid(tmpY,tmpX)==false && getMap_wall_type(dir_left,tmpY,tmpX)==0)
        var_17 = 0
    if (var_14 != 0) {
        if (var_17 > 0)
            draw_3D_8x8_titles(9, var_17, Row_J, Column_J + Col - 1)
        var_17 = var_14
        draw_3D_8x8_titles(0, var_14, Row_A, Column_A + Col)
    } else {
        if (var_17 > 0 &&
            getMap_wall_type(dir_right, tmpY - MapDirectionYDelta[dir_right],
                                        tmpX - MapDirectionXDelta[dir_right]) != 0)
            draw_3D_8x8_titles(9, var_17, Row_J, Column_J + Col - 1)
        var_17 = 0
    }
    var_10++; Col += 2
    tmpX += MapDirectionXDelta[dir_right]; tmpY += MapDirectionYDelta[dir_right]
}

// --- LEFT SIDE walls: 3 cells stepping dir_left, Col -= 2; first cell uses Col, rest Col-1 ---
tmpX=drawX; tmpY=drawY; var_10=0; Col=0
while (var_10 < 3) {
    var_15 = getMap_wall_type(dir_left, tmpY, tmpX)
    if (var_15 != 0) {
        if (var_10 == 0) draw_3D_8x8_titles(1, var_15, Row_B, Column_B + Col)
        else             draw_3D_8x8_titles(1, var_15, Row_B, Column_B + Col - 1)
    }
    var_10++; Col -= 2
    tmpX += MapDirectionXDelta[dir_left]; tmpY += MapDirectionYDelta[dir_left]
}

// --- RIGHT SIDE walls: 3 cells stepping dir_right, Col += 2; first cell Col, rest Col+1 ---
tmpX=drawX; tmpY=drawY; var_10=0; Col=0
while (var_10 < 3) {
    var_15 = getMap_wall_type(dir_right, tmpY, tmpX)
    if (var_15 != 0) {
        if (var_10 == 0) draw_3D_8x8_titles(2, var_15, Row_C, Column_C + Col)
        else             draw_3D_8x8_titles(2, var_15, Row_C, Column_C + Col + 1)
    }
    var_10++; Col += 2
    tmpX += MapDirectionXDelta[dir_right]; tmpY += MapDirectionYDelta[dir_right]
}
```

**Far side-cap (piece 9) emission rule — exact:** piece 9 fires in two situations, only
when the carry `var_17 > 0` (a previous cell had a front wall):
1. **Run-continue:** the current cell *also* has a front wall (`var_14 != 0`) → emit cap
   *before* drawing this cell's front (closes the gap between two adjacent far fronts).
2. **Run-end:** the current cell has *no* front wall (`var_14 == 0`) **but** the cell one
   step further along the fan direction (`tmpY/X - delta[dir_left|right]`) has a side wall on
   `dir_left` (left fan) / `dir_right` (right fan) `!= 0` → emit cap to seal the corner.
   The carry is reset to 0 when off-map with the opposite side open (the two `MapCoordIsValid`
   guards at the top of each fan).
The cap's `colStart` is `Column_J + Col + 1` (left fan) or `Column_J + Col - 1` (right fan)
— note the **±1** offset vs. the front's `Column_A + Col`.

### 2.2 `Draw3dWorldMid(partyDir, dir_left, dir_right, var_5, var_7)` (ovr031.cs:523)

`var_5 = drawX`, `var_7 = drawY` (focus = 1 cell ahead). Two 3-cell scans. `var_12` is the
lateral draw-column shift (±3 per cell = mid cell width).

```
// --- L→R scan: start TWO cells to the left of focus, step dir_right, var_12: -6,-3,0 ---
tmpX = MapDirectionXDelta[dir_left] + var_5 + MapDirectionXDelta[dir_left]   // var_5 + 2*Δx[left]
tmpY = MapDirectionYDelta[dir_left] + var_7 + MapDirectionYDelta[dir_left]
var_10 = 0; var_12 = -6
while (var_10 < 3) {
    var_14 = getMap_wall_type(partyDir, tmpY, tmpX)
    if (var_14 != 0) draw_3D_8x8_titles(3, var_14, Row_D, Column_D + var_12)   // MID FRONT
    var_15 = getMap_wall_type(dir_left, tmpY, tmpX)
    if (var_15 != 0) draw_3D_8x8_titles(4, var_15, Row_E, Column_E + var_12)   // MID LEFT
    var_10++; var_12 += 3
    tmpX += MapDirectionXDelta[dir_right]; tmpY += MapDirectionYDelta[dir_right]
}

// --- R→L scan: start TWO cells to the right of focus, step dir_left, var_12: 6,3,0 ---
tmpX = MapDirectionXDelta[dir_right] + MapDirectionXDelta[dir_right] + var_5   // var_5 + 2*Δx[right]
tmpY = MapDirectionYDelta[dir_right] + MapDirectionYDelta[dir_right] + var_7
var_10 = 0; var_12 = 6
while (var_10 < 3) {
    var_14 = getMap_wall_type(partyDir, tmpY, tmpX)
    if (var_14 != 0) draw_3D_8x8_titles(3, var_14, Row_D, Column_D + var_12)   // MID FRONT
    var_15 = getMap_wall_type(dir_right, tmpY, tmpX)
    if (var_15 != 0) draw_3D_8x8_titles(5, var_15, Row_F, Column_F + var_12)   // MID RIGHT
    var_10++; var_12 -= 3
    tmpX += MapDirectionXDelta[dir_left]; tmpY += MapDirectionYDelta[dir_left]
}
```
**No piece 9 in Mid.** Both scans hit the centre cell at `var_12 = 0`, so the focus cell's
front wall is drawn **twice** (once per scan, identical args) — harmless overpaint; keep it
1:1 for fidelity. Mid emits no caps.

### 2.3 `Draw3dWorldNear(partyDir, dir_left, dir_right, var_5, var_7)` (ovr031.cs:580)

`var_5 = drawX`, `var_7 = drawY` (focus = party cell). Two 2-cell scans, `var_12` shift ±7
(near cell width).

```
// --- L→R: start ONE cell to the left, step dir_right, var_12: -7, 0 ---
tmpX = MapDirectionXDelta[dir_left] + var_5
tmpY = MapDirectionYDelta[dir_left] + var_7
var_10 = 0; var_12 = -7
while (var_10 < 2) {
    var_14 = getMap_wall_type(partyDir, tmpY, tmpX)
    if (var_14 != 0) draw_3D_8x8_titles(6, var_14, Row_G, Column_G + var_12)   // NEAR FRONT
    var_15 = getMap_wall_type(dir_left, tmpY, tmpX)
    if (var_15 != 0) draw_3D_8x8_titles(7, var_15, Row_H, Column_H + var_12)   // NEAR LEFT
    var_10++; var_12 += 7
    tmpX += MapDirectionXDelta[dir_right]; tmpY += MapDirectionYDelta[dir_right]
}

// --- R→L: start ONE cell to the right, step dir_left, var_12: 7, 0 ---
tmpX = var_5 + MapDirectionXDelta[dir_right]
tmpY = var_7 + MapDirectionYDelta[dir_right]
var_10 = 0; var_12 = 7
while (var_10 < 2) {
    var_14 = getMap_wall_type(partyDir, tmpY, tmpX)
    if (var_14 != 0) draw_3D_8x8_titles(6, var_14, Row_G, var_12 + Column_G)   // NEAR FRONT
    var_15 = getMap_wall_type(dir_right, tmpY, tmpX)
    if (var_15 != 0) draw_3D_8x8_titles(8, var_15, Row_I, var_12 + Column_I)   // NEAR RIGHT
    var_10++; var_12 -= 7
    tmpX += MapDirectionXDelta[dir_left]; tmpY += MapDirectionYDelta[dir_left]
}
```
**No piece 9 in Near.** Both scans reach the party cell at `var_12 = 0`, so the party-cell
front (piece 6) is drawn twice — same harmless overpaint as Mid. The party-cell front is the
"wall right in your face" 8×7 slab.

### 2.4 Constants used by all three (already in `wallpieces.ts`)

`Row_*`/`Column_*` (ovr031.cs:8-…) — these are the **base** `rowStart`/`colStart` per piece;
the per-fan `Col`/`var_12` are *added* to the `Column_*` only:
```
Column_A=5 B=4 C=6 D=4 E=2 F=7 G=2 H=0 I=9 J=5
Row_A=4    B=3 C=3 D=3 E=1 F=1 G=1 H=0 I=0 J=4
```
These match `PIECES[].colBase/rowBase` in `wallpieces.ts` exactly (piece 9 = J).

---

## 3. Symbol-set loading (`loadDungeonWalls`)  CONFIRMED

### 3.1 `LoadWalldef(symbolSet, block_id)` — exact body (ovr031.cs:642)

```
if (symbolSet >= 1 && symbolSet < 4) {                      // only sets 1,2,3
    area_text = gbl.game_area.ToString()
    load_decode_dax(out data, out decode_size, block_id, "WALLDEF{area_text}.dax")
    if (decode_size == 0 || (decode_size / 0x30C) + symbolSet > 4)  // 0x30C = 780 bytes/record
        FATAL
    var_A = gbl.symbol_set_fix[symbolSet] - gbl.symbol_set_fix[1]   // .Offset relocation amount

    gbl.wallDef.LoadData(symbolSet, data)                  // fills k consecutive blocks[]

    blockCount = decode_size / 0x30C                       // k records in this WALLDEF block
    for (block = 0; block < blockCount; block++) {
        idx = symbolSet + block                            // target set slot 1..3
        if (idx >= 1 && idx <= 3) {
            gbl.setBlocks[idx - 1].Reset()
            gbl.wallDef.BlockOffset(idx, var_A)            // relocate blocks[idx-1] by var_A
            if (blockCount > 1) Load8x8D(idx, block_id*10 + block + 1)   // 8X8D B*10+1..k
            else                Load8x8D(idx, block_id)                  // 8X8D B
        }
    }
    gbl.setBlocks[symbolSet - 1].blockId = block_id        // remember for state-restore
    gbl.setBlocks[symbolSet - 1].setId   = symbolSet
}
```

`WallDefs.LoadData` / `WallDefBlock` (GeoBlock.cs):
```
WallDefs.LoadData(baseSet, data):                          // baseSet == symbolSet
    for i in 0 .. (data.Length/780 - 1):
        blocks[baseSet + i - 1].LoadData(data, i*780)      // 5×156 bytes each, row-major
WallDefs.BlockOffset(set, off): blocks[set-1].Offset(off)
WallDefBlock.Offset(off):  for all 5×156 cells: if cell >= 0x2D: cell += off   // byte-wrap
WallDefBlock.Id(y,x): return data[y, x]                    // y=slice 0..4, x=pool col 0..155
```

### 3.2 Answers to the specific questions

**Q: How many WallDefBlocks load, from which WALLDEF block id, into which slot?**
- A **single** `LoadWalldef(symbolSet, block_id)` call reads **one** WALLDEF DAX block
  `block_id`. That block contains `k = decode_size / 780` **records** (k ∈ {1,2,3}).
- That one call fills **k** consecutive `gbl.wallDef.blocks[]` slots:
  `blocks[symbolSet-1], blocks[symbolSet], … blocks[symbolSet-2+k]`
  (via `WallDefs.LoadData`, which writes `blocks[symbolSet + i - 1]` for i=0..k-1).
- So **one block with k records fills k slots** — it is **not** one call per slot when k>1.
  For the common k=1 case it fills exactly one slot.
- A normal dungeon is set up by **three** `LoadWalldef` calls — sets 1, 2, 3 — each with its
  own block id (see §3.4), unless one of them is k>1 and "absorbs" the next slot. The
  validation `(k)+symbolSet > 4` rejects loading a k-record block that would overflow past
  slot 3 (e.g. you cannot load a 3-record block into set 2).

**Q: How is `symbolSet` (the `.Offset` arg) chosen per loaded block/record?**
- `var_A = symbol_set_fix[symbolSet] - symbol_set_fix[1]` is computed **once** from the
  call's `symbolSet` and applied to **every** record loaded by that call (the `for` loop
  passes the same `var_A` to `BlockOffset(idx, var_A)` for each `idx = symbolSet+block`).
- `symbol_set_fix = { 0x0001, 0x002E, 0x0074, 0x00BA, 0x0100 }` (Gbl.cs), so:
  - `symbolSet 1 → var_A = 0` (ids stay in 0x2E..0x73 → 8X8D set 1)
  - `symbolSet 2 → var_A = 0x46` (ids shift up into 0x74..0xB9 → set 2)
  - `symbolSet 3 → var_A = 0x8C` (ids shift up into 0xBA..0xFF → set 3)
- **PARTIAL nuance (flag for implementer):** when k>1 the *same* `var_A` is applied to all k
  loaded slots even though they land in slots `symbolSet-1 … symbolSet-2+k`. So a 2-record
  block loaded as `LoadWalldef(1, …)` relocates **both** record-0 (→slot 0) and record-1
  (→slot 1) by `var_A = 0` (the set-1 amount), *not* by the set-2 amount. The geometry-doc
  §6.1 implied a per-set offset; the real code uses the **call's** `symbolSet` offset for all
  k records. This matters only for multi-record WALLDEF blocks; verify against any CoK
  multi-record block before relying on it. (Single-record blocks are unaffected.)

**Q: Confirm the 8X8D pairing rule.**
CONFIRMED verbatim: in `LoadWalldef`, `blockCount = decode_size/0x30C` and
```
if (blockCount > 1) Load8x8D(idx, block_id*10 + block + 1)   // 8X8D ids B*10+1 .. B*10+k
else                Load8x8D(idx, block_id)                   // 8X8D id == B
```
So WALLDEF block `B` with `k>1` records ↔ 8X8D blocks `B*10+1 … B*10+k`; with `k==1` ↔ 8X8D
block `B`. The area digit comes from `gbl.game_area` (same in both `WALLDEF{area}.dax` and
the `8X8D{area}.dax` that `Load8x8D` opens). This matches the geometry doc §6.1 exactly.

**Q: Which loaded 8X8D block becomes routeTileId set 1/2/3, and what are sets 0 and 4?**
- `Load8x8D(symbolSet, …)` stores into `gbl.symbol_8x8_set[symbolSet]` (ovr038.cs:8-13), and
  in `LoadWalldef` `symbolSet` passed in is `idx ∈ {1,2,3}`. So the per-dungeon wall tiles
  land in **array slots 1, 2, 3**.
- `Put8x8Symbol` (ovr038.cs:25) routes a relocated tile id to a set by range — identical to
  our `routeTileId`:
  ```
  id 0x01..0x2D → set 0 ;  0x2E..0x73 → set 1 ;  0x74..0xB9 → set 2 ;
  0xBA..0xFF   → set 3 ;  0x100..0x127 → set 4 ;  else → exception
  tile_index = id - symbol_set_fix[set]
  ```
- **Set 0** (8X8D block **0xCB**) and **set 4** (8X8D block **0xCA**) are loaded **once at
  engine boot** in `seg001.cs:309-310`:
  ```
  Load8x8D(4, 0xca);  Load8x8D(0, 0xcb);
  ```
  i.e. set 0 = the low fixed/base tiles (ids 1..0x2D, never relocated by `.Offset`), set 4 =
  a high fixed/special tile bank (ids 0x100..0x127). These are **game-global UI/border tiles,
  not per-dungeon wall art** — `LoadWalldef` never touches slots 0 or 4. So:
  - routeTileId **set 1 ← LoadWalldef(1,…)** 8X8D block
  - routeTileId **set 2 ← LoadWalldef(2,…)** 8X8D block
  - routeTileId **set 3 ← LoadWalldef(3,…)** 8X8D block
  - routeTileId **set 0 = fixed 8X8D 0xCB** (boot), **set 4 = fixed 8X8D 0xCA** (boot)

### 3.3 Who calls `LoadWalldef` (the dungeon-init path)  CONFIRMED

Three call sites in the engine:

1. **`CMD_LoadFiles` / ECL `Pieces` opcode** (ovr003.cs:501, `gbl.command != 0x21` branch)
   — the primary per-dungeon path. Reads three ECL operands:
   ```
   var_3 = vm_GetCmdValue(1)   // → symbol set 1
   var_2 = vm_GetCmdValue(2)   // → symbol set 2
   var_1 = vm_GetCmdValue(3)   // → symbol set 3
   ```
   Logic (Pieces, i.e. `gbl.command == 0x20`):
   ```
   if (var_3 == 0x7F) LoadWalldef(1, 0)                    // special: load block 0 into set 1
   else if (area.field_1CE != 0 && area.field_1D0 != 0) {  // "two-set" area variant
       if (var_3 != 0xff) LoadWalldef(1, var_3)
       if (var_1 != 0xff) LoadWalldef(3, var_1)            // note: var_2/set2 skipped here
   } else {                                                // normal three-set area
       if (var_3 != 0xff) LoadWalldef(1, var_3)  else setBlocks[0].Reset()
       if (var_2 != 0xff) LoadWalldef(2, var_2)  else setBlocks[1].Reset()
       if (var_1 != 0xff) LoadWalldef(3, var_1)  else setBlocks[2].Reset()
   }
   ```
   Operand value `0xFF` = "leave this set empty (Reset)". `0x7F` in var_3 = "load WALLDEF
   block 0 as set 1" (a default/blank wall set). The `Files` opcode (`gbl.command == 0x21`)
   instead loads the **GEO** map (`Load3DMap(var_3)`) — the wall *geometry*, not the symbols.

   **CoK addresses (VERIFIED, M1.S5c).** COAB's `area.field_1CE` / `area.field_1D0` are, in
   DOS CoK's running data segment, the two flag bytes at absolute **`0x4be7`** and **`0x4be8`**
   (a real dungeon-init block `SAVE`s them = 1 just before its `Pieces` opcode). The engine VM
   reads them at `LOAD_PIECES` and applies this dispatch in `EclVm.resolvePieces`, surfacing the
   resolved per-set triple on the `loadPieces` effect (`vm.ts`).

   **Worked real example — ECL1 block 34** (a two-set dungeon). It SAVEs `0x4be7=1`, `0x4be8=1`,
   then `Pieces 3, 4, 23`. Dispatch → set 1 ← block 3, **set 2 CALL skipped**, set 3 ← block 23
   (operand `4` for set 2 is never loaded — and indeed block 4 isn't even present in
   `WALLDEF1.DAX`, which holds only 1/3/23). **But** WALLDEF block 3 is a **2-record** block, so
   the §3.2 multi-record spill fills slot 2 from block 3's second record anyway — pairing 8X8D
   blocks 31/32 — while set 3 ← block 23 pairs 8X8D 23. Net: all three wallsets populate from
   blocks that all exist on disk. This end-to-end path is asserted in
   `packages/engine/test/firstperson-integration.test.ts` against real CoK bytes.

2. **`vm_SetMemoryValue` switches 0x322/0x324/0x326** (ovr008.cs:658-684) — an ECL "poke"
   that reloads a single set on the fly:
   ```
   0x322: if (set_value > 0x80) LoadWalldef(1, (set_value & 0x7f) & 0xFF)
   0x324: if (set_value > 0x80) LoadWalldef(2, (set_value & 0x7f) & 0xFF)
   0x326: if (set_value > 0x80) LoadWalldef(3, (set_value & 0x7f) & 0xFF)
   ```
   (Only fires when the high bit `0x80` is set; the block id is the low 7 bits.)

3. **State restore** (ovr017.cs:1080) — on load/return, replays the saved sets:
   ```
   for (i = 0; i < 3; i++)
       if (setBlocks[i].blockId > 0)
           LoadWalldef(setBlocks[i].setId, setBlocks[i].blockId)
   ```
   This is why `LoadWalldef` stores `setBlocks[symbolSet-1].{blockId,setId}` at the end.

### 3.4 Where the per-dungeon "which WALLDEF block" number comes from  CONFIRMED (CoK-specific)

It is **not** a fixed table — it is **ECL script data**. Each dungeon's ECL emits a `Pieces`
opcode whose three operands (`var_3/var_2/var_1`) are the WALLDEF **block ids** for symbol
sets 1/2/3, and a `Files` opcode whose operand is the **GEO** block id (the map). The
`{area}` digit in `WALLDEF{area}.dax` / `8X8D{area}.dax` / `GEO{area}.dax` comes from
`gbl.game_area` (set by the ECL `set_game_area` poke, ovr008 switch 0x312). So:
- *which* WALLDEF/8X8D *file* → `gbl.game_area` (per ECL).
- *which block* within it → the `Pieces` opcode operands (per ECL).
These numbers are **CoK-specific** and live in CoK's own ECL scripts (`EclDump.exe` output),
not in COAB. The geometry doc's CoK cross-check (`WALLDEF1` blocks 1/3/23 ↔ `8X8D1`) is one
concrete instance of these operands for area 1.

---

## 4. Worked example — party facing **North**, wall one cell ahead **and** both sides

Setup: `partyDir = 0` (N). `dir_left = 6` (W, Δ=(-1,0)), `dir_right = 2` (E, Δ=(+1,0)),
`dir_behind = 4` (S, Δ=(0,+1)). Party at map `(px,py) = (8,8)`. Normal dungeon block
(EclBlockId ∉ {0,10}).

Walls present (slot 1 = plain stone, wallset 0, slice 0 everywhere for simplicity):
- Cell **(8,7)** (one north of party): **north** wall = slot 1 (the wall "ahead").
- Cell **(8,8)** (party cell): **west** wall = slot 1 (wall on the left) and **east** wall =
  slot 1 (wall on the right).
- Everything else open.

**Top-level walk** (`Draw3dWorld`):
```
drawStep=2: drawX=8+2*0=8, drawY=8+2*(-1)=6  → FAR focus (8,6)
  step back by dir_behind(4) Δ=(0,+1): drawX=8, drawY=7
drawStep=1: MID focus (8,7)
  step back: drawX=8, drawY=8
drawStep=0: NEAR focus (8,8) = party cell
```

**FAR pass** (focus (8,6)): scans facing(north)/side walls of the row 2+ cells ahead — all
open in this example → **no draws**, `var_17` never set → **no piece 9**.

**MID pass** (focus (8,7)):
- L→R scan start: `tmpX = 8 + 2*Δx[left=6] = 8 + 2*(-1) = 6`, `tmpY = 7 + 2*Δy[6] = 7`.
  Cells visited (step dir_right=E, +1 x): (6,7) var_12=-6, (7,7) var_12=-3, (8,7) var_12=0.
  - (6,7),(7,7): north & west walls open → nothing.
  - **(8,7) var_12=0:** `getMap_wall_type(0/*north*/, 7, 8) = 1` → **expandWallPiece(3, slot=1,
    colStart=Column_D + 0 = 4, rowStart=Row_D = 3)**. (west wall of (8,7) open → no piece 4.)
- R→L scan start: `tmpX = 8 + 2*Δx[right=2] = 10`, `tmpY = 7`. Cells (step dir_left=W, -1 x):
  (10,7) var_12=6, (9,7) var_12=3, (8,7) var_12=0.
  - **(8,7) var_12=0:** north wall=1 → **expandWallPiece(3, slot=1, colStart=Column_D+0=4,
    rowStart=Row_D=3)** again (duplicate of the L→R centre draw — harmless overpaint).
    (east wall of (8,7) open → no piece 5.)

**NEAR pass** (focus (8,8) = party cell):
- L→R scan start: `tmpX = Δx[left=6] + 8 = 7`, `tmpY = Δy[6] + 8 = 8`. Cells (step E):
  (7,8) var_12=-7, (8,8) var_12=0.
  - (7,8): north & west open → nothing.
  - **(8,8) var_12=0:** `getMap_wall_type(0/*north*/, 8, 8)` = open → no piece 6.
    `getMap_wall_type(6/*west*/, 8, 8) = 1` → **expandWallPiece(7, slot=1,
    colStart=Column_H + 0 = 0, rowStart=Row_H = 0)** — the near **left** wall.
- R→L scan start: `tmpX = 8 + Δx[right=2] = 9`, `tmpY = 8`. Cells (step W):
  (9,8) var_12=7, (8,8) var_12=0.
  - (9,8): open → nothing.
  - **(8,8) var_12=0:** north open → no piece 6.
    `getMap_wall_type(2/*east*/, 8, 8) = 1` → **expandWallPiece(8, slot=1,
    colStart=Column_I + 0 = 9, rowStart=Row_I = 0)** — the near **right** wall.

**Full list of `expandWallPiece` calls for this frame** (in paint order):

| # | pass | pieceIdx | slot | colStart | rowStart | surface | pixel rect (pre-clip) |
|--:|------|---------:|-----:|---------:|---------:|---------|------------------------|
| 1 | MID  | 3 | 1 | 4 | 3 | mid front | x=(4+3)*8=56, y=(3+3)*8=48, w=3*8=24, h=4*8=32 |
| 2 | MID  | 3 | 1 | 4 | 3 | mid front (dup) | identical to #1 (overpaint) |
| 3 | NEAR | 7 | 1 | 0 | 0 | near left  | x=(0+3)*8=24, y=(0+3)*8=24, w=2*8=16, h=11*8=88 |
| 4 | NEAR | 8 | 1 | 9 | 0 | near right | x=(9+3)*8=96, y=(0+3)*8=24, w=2*8=16, h=11*8=88 |

Visual: a 24×32 stone slab high-centre (the wall one step ahead) framed by two tall 16×88
side walls hugging the left and right edges of the viewport — the classic "corridor with a
wall ahead" view. (If the party cell (8,8) *itself* had a north wall, NEAR would also emit
`expandWallPiece(6, 1, Column_G+0=2, Row_G=1)` = the 56×64 in-your-face front slab, drawn
twice.)

Each `expandWallPiece` call then internally walks its `rows×cols` tile rectangle and, for each
cell, reads `wallDef.blocks[0].Id(slice=0, idxOffset[pieceIdx]+i)`, routes the relocated id
via `routeTileId`, and blits at `((colX+3)*8,(rowY+3)*8)` clipped to (8,8)-(176,176) — exactly
as `wallpieces.ts::expandWallPiece` already does.

---

## 5. For the implementer — mapping onto our `wallpieces.ts` API

Our existing primitive is:
```ts
expandWallPiece(pieceIdx, slot, colStart, rowStart, readId): WallTileBlit[]
```
`composeWallView` and `loadDungeonWalls` are the two pure functions to add. Direct transcription:

```ts
const XD = [0, 1, 1, 1, 0, -1, -1, -1, 0];   // MapDirectionXDelta
const YD = [-1, -1, 0, 1, 1, 1, 0, -1, 0];   // MapDirectionYDelta

// wallType(dir, y, x): nibble-packed GEO read, with COAB off-map semantics.
//   plane0 byte = (dir0<<4)|dir2  (N|E) ; plane1 = (dir4<<4)|dir6 (S|W)  [see geometry doc §5.2]
//   if !inMap(y,x) && eclBlockId∈{0,10} → return 0
//   else clamp-wrap: x>15→0, x<0→15, y>15→0, y<0→15 ; read nibble for dir.
function wallType(map, dir /*0|2|4|6*/, y, x, eclBlockId): number { ... }

function composeWallView(map, px, py, facing /*0,2,4,6*/, readId, eclBlockId): WallTileBlit[] {
  const left = (facing + 6) % 8, right = (facing + 2) % 8, behind = (facing + 4) % 8;
  const out: WallTileBlit[] = [];
  const emit = (pieceIdx, slot, colStart, rowStart) => {
    if (slot !== 0) out.push(...expandWallPiece(pieceIdx, slot, colStart, rowStart, readId));
  };
  let dx = px + 2 * XD[facing], dy = py + 2 * YD[facing];
  for (let step = 2; step >= 0; step--) {
    if (step === 2) drawFar(map, facing, left, right, dx, dy, emit, eclBlockId);
    else if (step === 1) drawMid(map, facing, left, right, dx, dy, emit, eclBlockId);
    else drawNear(map, facing, left, right, dx, dy, emit, eclBlockId);
    dx += XD[behind]; dy += YD[behind];
  }
  return out;   // already in painter order far→near; blit in array order
}
```
- `drawFar/Mid/Near` are §2.1/§2.2/§2.3 transcribed 1:1. Use `Column_*`/`Row_*` from
  `PIECES[].colBase/rowBase` (J = piece 9). Keep the **double-draw** of the focus front in
  Mid/Near (don't dedupe) for byte-fidelity; it's a harmless overpaint.
- The far **piece-9 cap** logic (the `var_17` carry + the two emission rules in §2.1) is the
  one subtle bit — transcribe the carry exactly, including the `MapCoordIsValid && opposite
  side == 0 → var_17 = 0` reset and the look-ahead `getMap_wall_type(dir_left/right, y-Δ, x-Δ)`.
- `readId(wallset, slice, poolColumn)` is the WALLDEF reader you already pass to
  `expandWallPiece`; it must return the **relocated** id (apply `.Offset` at load — see
  `loadDungeonWalls`). Slot→{wallset,slice} is `wallSlotRouting` (already exact).

```ts
// loadDungeonWalls: replicate LoadWalldef for sets 1..3. Conceptually:
//   for each set s in {1,2,3} with a block id b (from the ECL Pieces opcode):
//     records = decodeWalldef(area, b);  k = records.length;        // 780 bytes each
//     varA = SYMBOL_SET_FIX[s] - SYMBOL_SET_FIX[1];                 // [0,0x46,0x8C][s-1]
//     for (let i = 0; i < k; i++) {
//        blocks[s - 1 + i] = applyOffset(records[i], varA);         // .Offset: id>=0x2D → +varA (byte-wrap)
//        const tile8x8Id = (k > 1) ? b*10 + i + 1 : b;
//        load8x8DInto(set = s + i, tile8x8Id);                      // routeTileId set (s+i)
//     }
//   set 0 = boot 8X8D 0xCB ; set 4 = boot 8X8D 0xCA (game-global, load once).
```
`SYMBOL_SET_FIX` is already exported from `wallpieces.ts`. Note `applyOffset` uses the
**call's** set `s` for all k records (the §3.2 PARTIAL nuance) — if you ever load a
multi-record block, the later records keep set-`s`'s offset, *not* their own slot's.

---

## 6. Confidence summary

| Item | Verdict | Confidence | Source |
|------|---------|-----------|--------|
| `Draw3dWorld` far→mid→near walk, start +2 ahead, step back by dir_behind | exact loop transcribed | CONFIRMED | ovr031.cs:321 |
| Far pass: 4-cell left fan (Col-=2) + 4-cell right fan (Col+=2) + 3-cell each side (piece 1/2) | exact | CONFIRMED | ovr031.cs:373 |
| Far piece-9 cap: carry `var_17`, two emission rules, ±1 colStart, off-map reset | exact | CONFIRMED | ovr031.cs:391-451 |
| Mid pass: 2 scans of 3 cells, var_12 ∓6→0 / ±6→0, pieces 3/4 and 3/5, no cap | exact | CONFIRMED | ovr031.cs:523 |
| Near pass: 2 scans of 2 cells, var_12 ∓7→0 / ±7→0, pieces 6/7 and 6/8, no cap | exact | CONFIRMED | ovr031.cs:580 |
| Focus-front drawn twice in Mid & Near (harmless overpaint) | confirmed | CONFIRMED | ovr031.cs both scans hit var_12=0 |
| `getMap_wall_type` dir constant per call (facing / dir_left / dir_right) | exact | CONFIRMED | ovr031.cs:222 + call sites |
| Off-map: null (open) iff EclBlockId∈{0,10}; else clamp-wrap to opposite edge | exact | CONFIRMED | ovr031.cs:254 getMap_XXX |
| `LoadWalldef`: 1 block → k records → k slots; 8X8D B*10+1..k (k>1) else B | exact | CONFIRMED | ovr031.cs:642 + GeoBlock.cs |
| `.Offset` arg = symbol_set_fix[symbolSet]-symbol_set_fix[1], applied to all k records | exact; multi-record uses call's set offset | CONFIRMED (nuance) | ovr031.cs:658-671 |
| Loaded 8X8D set 1/2/3 ← LoadWalldef sets 1/2/3 → routeTileId sets 1/2/3 | exact | CONFIRMED | ovr038.cs:8 + Put8x8Symbol |
| routeTileId set 0 = boot 8X8D 0xCB, set 4 = boot 8X8D 0xCA (game-global) | exact | CONFIRMED | seg001.cs:309-310 |
| Per-dungeon WALLDEF block ids come from ECL `Pieces` opcode operands (not a fixed table) | confirmed | CONFIRMED | ovr003.cs:501 CMD_LoadFiles |
| Operand sentinels: 0xFF=Reset slot, 0x7F(var_3)=LoadWalldef(1,0); 0x80 high-bit poke path | exact | CONFIRMED | ovr003.cs:539 / ovr008.cs:658 |
| Multi-record offset nuance applies to CoK | unverified for CoK's actual blocks | PARTIAL | resolve: EclDump CoK Pieces operands + check any k>1 WALLDEF block |

**Resolution path for the one PARTIAL:** dump CoK's ECL with `EclDump.exe`, find the `Pieces`
(0x20) opcodes per dungeon to get the real per-area block-id operands, and check whether any
referenced CoK `WALLDEF{area}.DAX` block is multi-record (size > 780). If all CoK wall blocks
are single-record (k=1), the offset nuance is moot and §3 is fully exact for CoK.

*Researched by Greybox AI (Opus), 2026-06-21. Clean-room from COAB (`simeonpilgrim/coab`,
files ovr031/ovr003/ovr008/ovr017/ovr038/seg001/GeoBlock/Gbl/Sys), cross-checked against the
CONFIRMED `firstperson-wall-geometry-cok.md`. SSI EXEs not decompiled; no COAB code copied
into the repo.*
