# CoK GEO Map Format — Research Findings

**Verdict: PARTIAL** — The block structure and grid dimensions are confirmed with high confidence. The wall byte encoding is consistent with the GEOGRIDS hackdoc definition. The roles of planes 2 and 3 are disputed between two competing theories: one attributing them to S/W walls, the other to backdrop+zone and event fields. The wall byte field semantics (specifically what `0x00` means) also require clarification.

> **RESOLVED 2026-06-21 — neither §3/§4 theory was right; the planes are NIBBLE-PACKED.**
> The COAB clean-room source (`Classes/GeoBlock.cs::MapInfo`, studied in
> `firstperson-wall-geometry-cok.md §5.2`) shows the real layout, since **verified against
> this repo's blocks**:
>
> | Plane | Real meaning |
> |-------|--------------|
> | 0 | `(North << 4) \| East` — two 4-bit wall slots |
> | 1 | `(South << 4) \| West` — two 4-bit wall slots |
> | 2 | backdrop / zone byte (NOT a wall plane) |
> | 3 | door flags, 2 bits per direction (N=bits0-1, E=2-3, S=4-5, W=6-7) |
>
> So **all four walls are stored explicitly per cell** as 4-bit slot ids (0=open, 1–15=wall
> graphic slot; the high "type" nibble of §3 was actually the adjacent direction's slot).
> Discriminator (empirical, this repo): under the nibble model the shared wall edges are
> **62–91% symmetric** (cell South slot == north-neighbour North slot; cell East == east-
> neighbour West), vs **0–13%** for any full-byte reading; every nibble stays in the valid
> 0–15 range; and plane-3 values (0,1,4,5,16,17,20,21,64,65…) are exactly 2-bit-per-direction
> door flags. The §4 Theory-A/Theory-B debate and the §3 high-nibble "wall type" table below
> are **superseded** by this; `packages/engine/src/loaders/geo.ts` now implements the nibble
> layout (golden-tested on GEO1 block 32, including the symmetry assertion).

---

## 1. Container: GEO DAX Blocks

### Block layout

CoK map data lives inside DAX archives compressed with the standard signed-byte RLE described in `docs/dax-format.md`. The game has three GEO DAX files:

| File | Blocks (IDs) | Description |
|------|-------------|-------------|
| `GEO1.DAX` | 32, 34 | First dungeon group (2 levels) |
| `GEO2.DAX` | 48, 49, 50, 64, 66, 67, 68 | Second dungeon group (7 levels) |
| `GEO3.DAX` | 80, 81, 82, 96, 97, 99 | Third dungeon group (6 levels) |

**Total: 15 dungeon levels.**

Block IDs are shared with the corresponding ECL script archives:

| GEO file | ECL file | Shared block IDs |
|----------|----------|-----------------|
| `GEO1.DAX` | `ECL1.DAX` | 32, 34 (ECL1 also has 16, 17, 18, 33, 36) |
| `GEO2.DAX` | `ECL2.DAX` | 48, 49, 50, 64, 66, 67, 68 (ECL2 also has 57) |
| `GEO3.DAX` | `ECL3.DAX` | 80, 81, 82, 96, 97, 99 (ECL3 also has 98) |

**Block N = map grid for dungeon N; ECL block N = ECL bytecode for dungeon N.**

ECL-only blocks (33, 36, 57, 98) are dungeons or encounters that have script events but no stored grid (combat arenas, overland modules, or other non-navigable modules).

### Decompressed block size

Every single GEO block decompresses to exactly **1026 bytes**.

```
raw[0:2]   = uint16_le = 0x0400 = 1024  (data size; constant across all 15 blocks)
raw[2:1026] = 1024 bytes of grid data
```

**Confidence: Confirmed.** All 15 blocks verified.

---

## 2. Grid Dimensions: 16 × 16

1024 bytes of grid data, divided into **four planes of 256 bytes** each. 256 = 16 × 16. This is the standard Gold Box dungeon dimension.

```
raw[2:258]    = Plane 0 (256 bytes)
raw[258:514]  = Plane 1 (256 bytes)
raw[514:770]  = Plane 2 (256 bytes)
raw[770:1026] = Plane 3 (256 bytes)
```

Cell index within each plane:
```
cell_index = row * 16 + col   (row-major, 0-indexed, row 0 = northernmost)
```

**Confidence: Confirmed.** The four-plane structure is corroborated by statistical discontinuities visible in raw hex: planes 0–1 have wall-byte values (0x00–0xDC), plane 2 values cluster differently between GEO1 and GEO2/3, and plane 3 is predominantly 0x00.

---

## 3. Plane 0 and Plane 1: North and East Walls

**Confidence: Confirmed (Plane 0 = N, Plane 1 = E).**

These two planes hold per-cell wall data using the GEOGRIDS encoding (from `hackdocs_extracted/GEOGRIDS.TXT`):

```
wall_byte = wall_slot + wall_type
  wall_slot  = wall_byte & 0x0F   (0–15; 0 = blank/no graphic, 1–15 = slot graphic)
  wall_type  = wall_byte & 0xF0   (see table below)
```

### Wall type codes (high nibble × 16)

| High nibble | Decimal | Name |
|-------------|---------|------|
| 0x0_ | 0 | Open |
| 0x1_ | 16 | Secret |
| 0x2_ | 32 | Locked |
| 0x3_ | 48 | Locked Secret |
| 0x4_ | 64 | Wizard Locked |
| 0x5_ | 80 | Wizard Locked Secret |
| 0x6_ | 96 | Key #1 |
| 0x7_ | 112 | Key #2 |
| 0x8_ | 128 | Key #3 |
| 0x9_ | 144 | Key #4 |
| 0xA_ | 160 | Key #5 |
| 0xB_ | 176 | Key #6 |
| 0xC_ | 192 | Key #7 |
| 0xD_ | 208 | Key #8 |
| 0xE_ | 224 | Blocked |
| 0xF_ | 240 | False Door |

### Open passage vs. wall

`0x00` (slot 0 + type 0 = "Open") is the most common value in planes 0 and 1 (~12% of cells per plane). Based on GEOGRIDS documentation, `0x00` means **no wall / open passage** — the player can move through freely. Any non-zero value represents a wall, door, or barrier of some kind.

**Open passage count for GEO1 block 32:**
- N wall = 0 (open north): 32/256 cells (12.5%)
- E wall = 0 (open east): 31/256 cells (12.1%)

The low open-passage rate reflects CoK's tightly packed dungeon design with narrow corridors. This rate is consistent with a Gold Box dungeon where most cell boundaries are solid rock.

### Value ranges observed (all 15 blocks)

| Plane | Min value | Max value | Notes |
|-------|-----------|-----------|-------|
| Plane 0 (N) | 0x00 | 0xDC | Full wall-byte range; 0xDC = slot 12 + Key8 |
| Plane 1 (E) | 0x00 | 0xD0 | Same range |

Both ranges are 100% consistent with valid GEOGRIDS wall bytes (0x00–0xF0).

---

## 4. Plane 2 and Plane 3: Contested

The roles of planes 2 and 3 are **ambiguous**. Two theories are viable; neither can be definitively ruled out with the available analysis. Both are presented with evidence for each.

### Theory A — Explicit S and W walls (all 4 walls stored per cell)

Under this theory:
- Plane 2 = South walls (N of row below this cell, stored independently)
- Plane 3 = West walls (E of column left of this cell, stored independently)

**Evidence for:**
- All observed plane 2 values (0x00–0xC0) are valid GEOGRIDS wall bytes when decoded the same way as planes 0 and 1 (e.g. 0x0C = slot 12, type 0; 0x14 = slot 4, type Secret; 0xB0 = slot 0, Key 6 lock).
- All observed plane 3 values (0x00–0x84) are valid GEOGRIDS wall bytes.
- GEO2 and GEO3 plane 2 values span 0x00–0xC0 (full wall-byte range), consistent with S walls that vary across dungeon designs.
- Storing all 4 walls independently allows **one-way passages** (wall on side A differs from side B). Gold Box games support directional walls.

**Evidence against:**
- Symmetry check across 240 adjacent cell pairs in GEO2 block 48 showed only 32/240 matches (13%) between plane 2 and the derived S wall (N of row+1). If walls were mostly symmetric, 70–90% matches would be expected.
- GEO1 blocks 32 and 34 have ALL plane 2 values ≥ 0x80 (128 = Key 3 lock). Having 65% of south walls locked with Key 3 on the first dungeon levels is game-design-implausible for an early-game area.
- An 87% asymmetry rate (nearly every wall asymmetric between opposing cell faces) is unusually high for a dungeon RPG.

### Theory B — Backdrop/Zone and Event field

Under this theory:
- Plane 2 = Backdrop + Zone per cell
- Plane 3 = Per-cell event trigger (bit flags or event slot number)

**Evidence for:**
- In GEO1 (blocks 32 and 34), ALL plane 2 values are in range 0x80–0xB0. This is consistent with the GEOGRIDS backdrop+zone byte having a base of 0x80 (i.e., `byte = 0x80 | (backdrop + zone_offset)`), where backdrop ∈ {0,1,2,3} and zone_offset ∈ {0,4,8,12,16,20,24,28}. The vast majority of cells have `0x80` (default: backdrop 0, zone 1).
- Plane 3 in all 15 blocks is predominantly 0x00 (65–75% of cells), with non-zero values forming sparse bit-flag patterns (1, 4, 5, 16, 17, 64, 65…). This matches an "event trigger" field where most cells have no trigger (0 = no event).
- GEOGRIDS specifies exactly these two fields for grid cells: Event# (byte 5) and Backdrop+Zone (byte 6).

**Evidence against:**
- GEO2 and GEO3 plane 2 values include values like 0x93, 0xA1, 0xAD which exceed the GEOGRIDS maximum for backdrop+zone (0 + 28 = 28 without any base offset). With a 0x80 base, maximum would be 0x9F (backdrop 3 + zone 8 = 31). Values up to 0xC0 require extra zones or bits not documented in GEOGRIDS.
- GEO2 plane 2 includes 0x00, 0x14, 0x15 etc. which, without the 0x80 base, would decode to backdrop 0 zone 1, backdrop 0 zone 4... but those same values are valid wall bytes.
- The bit-flag pattern of plane 3 (1, 4, 5, 16, 64…) does NOT match event slot numbers 1–100 which would typically appear consecutively.

### Statistical summary

| Plane | GEO1 value range | GEO2/3 value range | Dominant value | Non-dominant count |
|-------|------------------|--------------------|----------------|-------------------|
| Plane 2 | 0x80–0xB0 | 0x00–0xC0 | 0x80 (GEO1) or varies | 35–50 unique values |
| Plane 3 | 0x00–0x80 | 0x00–0x80 | 0x00 | 15–21 unique values |

---

## 5. Boundary Wall Behavior

Testing across all 15 blocks:

| Boundary | Non-zero wall count | Notes |
|----------|---------------------|-------|
| Row 0, Plane 0 (top N walls) | 16/16 always | Top boundary always solid |
| Col 0, Plane 1 (left E walls) | Usually 16/16 | East walls of leftmost column (interior walls going right) |
| Row 15, Plane 0 (bottom N walls) | 4–16/16 | Varies; some maps have passages near the south edge |
| Col 15, Plane 1 (right E walls) | 7–16/16 | Varies |

The top boundary (row 0 north walls) is always fully solid — confirmed. The left boundary (west wall of column 0) has no explicit storage in N+E-only models; the engine likely treats the map edge as implicitly solid.

---

## 6. Worked Example: GEO1 Block 32, Row 0, Cells 0–4

**Raw bytes** (plane 0 / N walls, starting at raw[2]):
```
37 41 30 41 47 ...
```

**Raw bytes** (plane 1 / E walls, starting at raw[258]):
```
13 47 01 00 41 ...
```

**Raw bytes** (plane 2, starting at raw[514]):
```
80 80 80 80 90 ...
```

**Raw bytes** (plane 3, starting at raw[770]):
```
14 44 40 04 44 ...
```

### Cell-by-cell decode

| Cell | N wall raw | N: slot/type | E wall raw | E: slot/type | Plane 2 | Plane 3 |
|------|-----------|--------------|-----------|--------------|---------|---------|
| [0,0] | 0x37 | slot 7, LockedSecret | 0x13 | slot 3, Secret | 0x80 | 0x14 = 20 |
| [0,1] | 0x41 | slot 1, WizLocked | 0x47 | slot 7, WizLocked | 0x80 | 0x44 = 68 |
| [0,2] | 0x30 | slot 0, LockedSecret | 0x01 | slot 1, Open | 0x80 | 0x40 = 64 |
| [0,3] | 0x41 | slot 1, WizLocked | 0x00 | **open passage** | 0x80 | 0x04 = 4 |
| [0,4] | 0x47 | slot 7, WizLocked | 0x41 | slot 1, WizLocked | 0x90 | 0x44 = 68 |

Cell [0,3] east wall = 0x00 = open passage (can move east from col 3 to col 4). All other walls in this row are locked or wizard-locked, consistent with a heavily magically-sealed early dungeon.

Cell [0,4] plane 2 = 0x90 = the only non-0x80 value in row 0. Under Theory B: 0x90 − 0x80 = 16, backdrop bit 0 = 0, zone = (16 >> 2) + 1 = 5 (zone 5). Under Theory A: 0x90 = slot 0 + type 144 (Key 4 south wall).

---

## 7. Block ID to Dungeon Module Mapping

The block IDs are organized in groups corresponding to the three dungeon progressions in CoK:

| Block ID range | GEO file | ECL file | Dungeon group |
|----------------|----------|----------|---------------|
| 16–36 | GEO1.DAX | ECL1.DAX | Group 1 (early game, IDs 32, 34 have grids) |
| 48–68 | GEO2.DAX | ECL2.DAX | Group 2 (mid game) |
| 80–99 | GEO3.DAX | ECL3.DAX | Group 3 (late game) |

ECL block IDs 33, 36, 57, 98 exist in ECL archives but have no corresponding GEO block. These are likely combat-only modules or scripted sequences with no persistent map grid.

The DAX file split (GEO1/2/3) aligns with the game's disk structure (CoK shipped on 3 disks).

---

## 8. For the Implementer

### TypeScript cell decode pseudocode (Theory B — Recommended starting point)

```typescript
const BLOCK_HEADER_SIZE = 2;      // uint16_le = 1024 (always)
const GRID_SIZE = 16;
const PLANE_SIZE = 256;           // GRID_SIZE * GRID_SIZE

interface GeoCell {
  northWall: WallByte;
  eastWall:  WallByte;
  plane2:    number;  // backdrop+zone (Theory B) or south wall (Theory A)
  plane3:    number;  // event trigger (Theory B) or west wall (Theory A)
}

interface WallByte {
  raw:      number;
  slot:     number;  // raw & 0x0F (0=blank, 1–15=graphic slot)
  typeCode: number;  // raw & 0xF0 (0=Open, 16=Secret, 32=Locked, …)
}

function decodeGeoBlock(raw: Uint8Array): GeoCell[][] {
  // Verify header
  const dataSize = raw[0] | (raw[1] << 8);  // should be 1024
  if (dataSize !== 1024) throw new Error('Unexpected GEO block size');

  const planes = [
    raw.slice(2,   258),   // Plane 0: North walls
    raw.slice(258, 514),   // Plane 1: East walls
    raw.slice(514, 770),   // Plane 2: contested
    raw.slice(770, 1026),  // Plane 3: contested
  ];

  const grid: GeoCell[][] = [];
  for (let row = 0; row < 16; row++) {
    grid[row] = [];
    for (let col = 0; col < 16; col++) {
      const idx = row * 16 + col;
      grid[row][col] = {
        northWall: decodeWallByte(planes[0][idx]),
        eastWall:  decodeWallByte(planes[1][idx]),
        plane2:    planes[2][idx],
        plane3:    planes[3][idx],
      };
    }
  }
  return grid;
}

function decodeWallByte(b: number): WallByte {
  return {
    raw:      b,
    slot:     b & 0x0F,
    typeCode: b & 0xF0,
  };
}

// Derived walls (in N+E-only model):
function getSouthWall(grid: GeoCell[][], row: number, col: number): WallByte | null {
  if (row >= 15) return null;  // no row below
  return grid[row + 1][col].northWall;
}

function getWestWall(grid: GeoCell[][], row: number, col: number): WallByte | null {
  if (col === 0) return null;  // no column to the left
  return grid[row][col - 1].eastWall;
}

// Wall passability
function isPassable(wall: WallByte): boolean {
  // 0x00 = slot 0 + type Open = no wall = open passage
  return wall.raw === 0x00;
}
```

### Parsing backdrop+zone under Theory B

```typescript
function decodeBackdropZone(plane2byte: number): { backdrop: number; zone: number } | null {
  // GEO1 pattern: 0x80 base. GEO2/3 do not always have 0x80 base.
  // Exact encoding is UNKNOWN; this is a best-guess decode for GEO1.
  if ((plane2byte & 0x80) !== 0) {
    const raw = plane2byte - 0x80;
    const backdrop = raw & 0x03;           // bits 0-1 (GUESS)
    const zoneOffset = raw & 0x1C;         // bits 2-4 (GUESS)
    const zone = (zoneOffset >> 2) + 1;   // zone 1–8
    return { backdrop, zone };
  }
  // For GEO2/3 values below 0x80: encoding unknown — may be wall bytes (Theory A)
  return null;
}
```

---

## 9. Remaining Unknowns and How to Resolve Them

### Unknown 1: Plane 2 and Plane 3 roles (Biggest unknown)

**Priority: High.** This is the single most important unresolved question.

**Resolution path:**
1. Run CoK in DOSBox with memory-inspection (DOSBox debugger). Set a breakpoint on the map-rendering or movement-validation code. Observe which memory address the engine reads when: (a) you attempt to move south (should read S-wall data), (b) the backdrop changes (should read backdrop data).
2. Alternatively: edit a GEO DAX block in a hex editor, change a specific plane 2 byte, reload the game, and observe whether the visual backdrop changes or whether a wall appears/disappears on the south face.
3. Cross-reference with DoK (Death Knights of Krynn) which uses the same format — same analysis on DoK GEO blocks may give more counterexamples.

### Unknown 2: Wall byte semantics for `0x00`

**Priority: Medium.** Does `0x00` mean "open passage" (walkable) or "solid rock / no feature" (impassable)?

**Resolution path:**
- Map the connectivity graph from a known dungeon layout (e.g. the first dungeon in CoK, which has published maps online). If cells with N_wall = 0x00 correspond to passable corridors in the published map, Theory "0x00 = open" is confirmed.
- Play the game and check whether the party can walk through a cell whose N wall is 0x00.

### Unknown 3: Exact backdrop+zone bit layout in plane 2

**Priority: Low** (only needed if Theory B is confirmed).

**Resolution path:**
- In GEODATA.TXT (UA format), backdrop+zone byte = `backdrop(0–3) + zone_offset(0–28)`. The maximum is 31. If CoK plane 2 values go up to 0xC0 (192), either: (a) the encoding is different from GEOGRIDS, (b) CoK has more than 8 zones, or (c) plane 2 is walls. Test by checking zone names in the game versus which cells show which backdrop.

### Unknown 4: Plane 3 event field semantics

**Priority: Low** (only needed if Theory B is confirmed).

**Resolution path:**
- Cross-reference non-zero plane 3 cells with known event locations from game walkthroughs (doors that trigger dialogue, enemy encounters, stairways). If they align, plane 3 = event trigger.
- The values (1, 4, 5, 16, 64, 128…) look like bit flags rather than slot numbers 1–100. If they're flags, determine what each bit triggers.

### Unknown 5: Block ID to in-game dungeon name mapping

**Priority: Low** (nice-to-have metadata).

**Resolution path:**
- Run GEOENTRY.TXT analysis against the ECL blocks: block 32 starts the game (entry point data in the ECL should show starting coordinates).
- Consult published CoK walkthroughs and map names to match dungeon content against block IDs.

---

## 10. Sources Cited

| Source | Used for |
|--------|---------|
| `hackdocs_extracted/GEOGRIDS.TXT` | Wall byte encoding (slot + type), cell structure |
| `hackdocs_extracted/GEODATA.TXT` | Overall geo file layout, backdrop/zone/event field offsets |
| `hackdocs_extracted/GEOENTRY.TXT` | Entry point byte layout |
| `hackdocs_extracted/GEOEVENT.TXT` | Event type table |
| `Champions of Krynn/GEO1.DAX` | Empirical block analysis (blocks 32, 34) |
| `Champions of Krynn/GEO2.DAX` | Empirical block analysis (blocks 48–68) |
| `Champions of Krynn/GEO3.DAX` | Empirical block analysis (blocks 80–99) |
| `tools/dax_decode.py` | DAX decompression; `parse_container` + `decompress_block` APIs |
| `Champions of Krynn/ECL1-3.DAX` | Block ID cross-reference (ECL shares ID namespace with GEO) |
| `Champions of Krynn/WALLDEF1-3.DAX` | Existence of wall-slot-indexed graphic definitions |

---

*Researched by Greybox AI (Sonnet), 2026-06-21.*
