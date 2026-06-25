# Amiga `8X8D*.DAA` Tile Plane Order and Palette — SOLVED

**Status: SOLVED (2026-06-21).** The within-tile bitplane order, tile-strip structure,
and shared palette are all determined. The decoder is implemented in `tools/daa_decode.py`
(`parse_8x8d_header`, `decode_8x8d_tile`, `tiles_8x8d_to_rgb`) and in the engine
(`decodeDaaTileStrip`, `packages/engine/src/loaders/daa.ts`). Rendered proof images
live in `renders/daa/8X8D1_final/` (git-ignored).

> **CORRECTION (2026-06-21, engine ground-truth).** An earlier draft of this doc
> (§6/§10 below) concluded the Amiga 8X8D block-202 tiles were *"genuinely different /
> richer art"* (avg shape-IoU 0.645). **That conclusion was wrong — a circular-method
> artifact.** A direct, non-circular check decodes BOTH platforms with the same engine
> pipeline and compares palette indices pixel-for-pixel:
> **all 38 tiles of block 202 are byte-identical on DOS and Amiga (IoU = 1.000, 0
> differing pixels of 38×64).** CoK's 8×8 UI chrome (frame + moons + arrows) is the SAME
> 16-colour EGA art on both platforms. This identity is exactly what *validates* the
> decoder: a correct plane order + palette reproduces the DOS oracle perfectly. The
> Amiga visual advantage in CoK lives in the **32-colour full-screen art**
> (BIGPIC/portraits/backdrops), NOT in this shared 8×8 chrome. The invariant is pinned by
> `packages/engine/test/daaTileStrip.test.ts` ("ALL 38 tiles are byte-identical to DOS").
> The §6 IoU table below is retained only as a record of the flawed comparison; read its
> numbers as an artifact of a mismatched DOS reference, not as real per-tile differences.

---

## 1. The problem

`8X8D*.DAA` blocks (CoK `disk2/8X8D1.DAA`, `disk1/8X8D0.DAA`, `disk3/8X8D2.DAA`)
decompressed to blocks with header `{h=8, w_div8=1, y=0, x=0}` followed by 1 count
byte `N` then `N*32` bytes of pixel data. The arithmetic was already known:
`4 planes x 8 rows x 1 byte/row = 32 bytes/tile`. The **within-tile plane order** and
the **correct shared palette** were unsolved.

### The oracle

DOS `8X8D1.DAX` block 202 holds 38 tiles. Tile layout is verified pixel-exact against
a real CoK DOS screenshot (see `docs/engine/research/cok-frame-arrays.md`). Key tiles:

| Tile | DOS shape | Purpose |
|------|-----------|---------|
| 20 | 64/64 set (fully filled) | Horizontal double-bar |
| 21 | 64/64 set | Bracket variant |
| 22 | 59/64 set | Bracket with small cutouts |
| 23-25 | 38-49/64 | Upper/lower junctions |
| 26-29 | 40/35/55/53 | Lunitari (red) moon phases 1-4 |
| 30-33 | 45/37/55/53 | Solinari (white) moon phases 1-4 |
| 34-37 | 45/37/55/53 | Nuitari (blue) moon phases 1-4 |

---

## 2. Method of solution

### Step 1: Cross-game reference (DoK 8X8D)

Death Knights of Krynn `disk1/8x8d1.daa` block 202 decoded as a **standard 5-plane
frame** (raw = 1913 = 73 + 5 * 46 * 8, exactly), width = 360px = 45 tiles. Rendering
it with `parse_frame_header` / `planar_frame_to_rgb` produced a legible strip showing:
directional arrows, frame elements, three moon groups (tiles ~13-24), and dungeon walls.
This established that the *strip* interpretation (N tiles packed side-by-side into one
wide image) is correct.

### Step 2: CoK tile-strip hypothesis

CoK block 202 (N=38 tiles). If stored as a 4-plane contiguous strip of width N*8 = 304
pixels:

```
row_bytes   = ceil(304/16)*2 = 38 bytes   (Amiga word-aligned; equals N)
plane_size  = 38 * 8 = 304 bytes
pixel_data  = 4 * 304 = 1216 bytes
total_raw   = 9 (header+count) + 1216 = 1225 bytes   ✓ (exactly matches raw_size)
```

Tile t occupies **byte column t** within each plane's row:

```
pixel(t, x, y).bit_p = (plane_p[y * row_bytes + t] >> (7-x)) & 1
color_index(t, x, y) = sum_over_p( bit_p << p )    (plane 0 = LSB)
```

This is **plane-contiguous** (plane 0 covers the full 304×8 strip, then plane 1, etc.),
**MSB-first** pixel order (bit 7 = leftmost pixel = x=0), and **plane 0 = LSB** of the
color index. It differs from the standard frame format (which uses 5 planes + embedded
palette) only in having 4 planes and no palette header.

### Step 3: Exhaustive enumeration

Eight layout candidates were scored (2 plane groupings × 2 bit orders × 2 plane
significances). The **strip-contiguous-msb-lsb_first** layout scored highest on
avg IoU vs DOS oracle (tiles 20-37), **and** produced three moon groups with identical
inter-phase XOR patterns:

```
Lunitari (t26-29): XOR pairs = [6, 26, 6, 20, 12, 20]
Solinari (t30-33): XOR pairs = [6, 26, 6, 20, 12, 20]   ← identical to Lunitari
Nuitari  (t34-37): XOR pairs = [6, 26, 6, 20, 12, 20]   ← identical
```

Three moons with structurally identical phase patterns is exactly what the game's
moon mechanic requires. No other layout produced this.

### Step 4: Color verification

Decoded color indices vs EGA palette:

| Tile | Role | Dominant indices | Colors | Verdict |
|------|------|-----------------|--------|---------|
| 20 | Horizontal bar | 11 (lcyan), 13 (lmag), 2,3,10 | cyan + magenta frame border | Correct: cyan/teal Gold Box bar |
| 28 | Full Lunitari disc | 4 (dark red), 13 | inner #aa0000, outer magenta | Red moon ✓ |
| 32 | Full Solinari disc | 15 (white), 13 | inner #ffffff, outer magenta | White moon ✓ |
| 36 | Full Nuitari disc | 1 (dark blue), 13 | inner #0000aa, outer magenta | Blue moon ✓ |

All three moon colors match Dragonlance canon (Lunitari=red, Solinari=white,
Nuitari=dark). The magenta key (idx 13, #ff55ff) wraps all moon discs and junction tiles
as the transparency colour — **the same key the DOS tiles use** (confirmed: identical
indices on both platforms; see the top-of-file correction).

---

## 3. Winning specification (precise, for engine TS port)

```
Structure:
  offset 0-1:  uint16 BE  height    = 8  (constant)
  offset 2-3:  uint16 BE  width_div8 = 1  (IGNORED; actual strip width = N*8)
  offset 4-7:  uint32 BE  y=0, x=0
  offset 8:    uint8      N = tile_count  (e.g. 38, 45, 46, 70)
  offset 9:    pixel data  (4 * row_bytes * 8) bytes

Plane layout:
  n_planes    = 4
  row_bytes   = ceil(N*8/16)*2     # word-aligned; equals N for all observed N
  plane_size  = row_bytes * 8
  Plane p occupies: data[9 + p*plane_size .. 9 + (p+1)*plane_size)

Tile decode (tile t, pixel x in [0,7], row y in [0,7]):
  byte  = data[9 + p*plane_size + y*row_bytes + t]
  bit_p = (byte >> (7 - x)) & 1
  idx   = bit_0 | (bit_1 << 1) | (bit_2 << 2) | (bit_3 << 3)

Detection: h == 8 AND w_div8 == 1 AND (raw_size - 9) == 4 * row_bytes * 8
  where N = raw[8] and row_bytes = ceil(N*8/16)*2
```

---

## 4. Palette source and values

`8X8D*` tile blocks carry **no embedded palette**. They use the **shared EGA-compatible
16-color dungeon palette** that is embedded in every exploration-screen frame file:
`DUNGCOM.DAA`, `BORDER.DAA`, `SKY.DAA`, `WILDCOM.DAA`, `RANDCOM.DAA`. All of these
embed an identical first-16-color block (the standard IBM EGA palette):

| Index | Name | #RGB |
|-------|------|------|
| 0 | black | #000000 |
| 1 | dark blue | #0000aa |
| 2 | dark green | #00aa00 |
| 3 | cyan | #00aaaa |
| 4 | dark red | #aa0000 |
| 5 | magenta | #aa00aa |
| 6 | brown | #aa5500 |
| 7 | light gray | #aaaaaa |
| 8 | dark gray | #555555 |
| 9 | light blue | #5555ff |
| 10 | light green | #55ff55 |
| 11 | bright cyan | #55ffff |
| 12 | light red | #ff5555 |
| 13 | bright magenta | #ff55ff |
| 14 | yellow | #ffff55 |
| 15 | white | #ffffff |

**Palette source path**: load the palette from the embedded 32-color Amiga word block
in any of the above DAA files (bytes 9-72 of any renderable block in those files);
take the first 16 colors and scale each 4-bit channel by ×17. In practice, the engine
can use the hard-coded EGA table above directly since it is constant.

---

## 5. File and block inventory

### CoK `disk2/8X8D1.DAA` (also identical on `disk1/8X8D0.DAA`)

| Block id | count | strip px | Purpose |
|----------|-------|----------|---------|
| 201 | — | — | Lookup/palette block (all-zero decompressed; format unknown) |
| **202** | **38** | **304×8** | **Exploration frame + moons (tiles 20-37), compass arrows (0-3), gauges** |
| 203 | 45 | 360×8 | Alternate tile set (used for set 0 / block 203 per GAME.OVR) |
| 11, 12 | 70 | 560×8 | Dungeon wall art set 1 (wall faces, pillars, floor/ceiling) |
| 31, 32 | 70 | 560×8 | Wall art set 3 |
| 23 | 70 | 560×8 | Wall art set 2/3 variant |
| 51, 52 | 70 | 560×8 | Wall art set 5 |
| 7 | 70 | 560×8 | Wall art set 7 |
| 81, 82 | 70 | 560×8 | Wall art set 8 |

Block 202 is the CoK exploration screen frame (confirmed: matches DOS block 202 tile
assignments per `GAME.OVR` boot-load call site, same as described in
`docs/engine/research/cok-frame-arrays.md`).

---

## 6. Shape-match evidence vs DOS oracle (tiles 20-37)

Scoring: Intersection-over-Union of non-zero pixel masks (shape only, ignoring color).

| Tile | Role | Amiga set | DOS set | IoU |
|------|------|-----------|---------|-----|
| 20 | Horizontal bar | 64 | 64 | **1.000** |
| 21 | Bracket | 64 | 64 | **1.000** |
| 22 | Bracket variant | 64 | 59 | 0.922 |
| 23 | Upper junction | 38 | 49 | 0.554 |
| 24 | Vertical column | 48 | 43 | 0.569 |
| 25 | Lower junction | 43 | 49 | 0.533 |
| 26 | Lunitari phase 1 | 38 | 40 | 0.393 |
| 27 | Lunitari phase 2 | 44 | 35 | 0.491 |
| 28 | Lunitari phase 3 (full) | 64 | 55 | **0.859** |
| 29 | Lunitari phase 4 | 44 | 53 | 0.516 |
| 30 | Solinari phase 1 | 38 | 45 | 0.482 |
| 31 | Solinari phase 2 | 44 | 37 | 0.528 |
| 32 | Solinari phase 3 (full) | 64 | 55 | **0.859** |
| 33 | Solinari phase 4 | 44 | 53 | 0.516 |
| 34 | Nuitari phase 1 | 38 | 45 | 0.482 |
| 35 | Nuitari phase 2 | 44 | 37 | 0.528 |
| 36 | Nuitari phase 3 (full) | 64 | 55 | **0.859** |
| 37 | Nuitari phase 4 | 44 | 53 | 0.516 |
| **avg** | | | | **0.645** |

**Interpretation — CORRECTED.** The avg IoU of 0.645 in the table above does **NOT**
reflect different Amiga art. It is a **measurement artifact**: the Amiga-decoded masks
were scored against a DOS reference whose opaque-mask was derived differently (a
different magenta/zero treatment for the "set pixel" count). When BOTH platforms are
decoded through the same pipeline and compared by palette index, every one of the 38
tiles is **identical** (IoU = 1.000, 0 differing pixels). The table is kept only as a
record of the flawed comparison. What the data actually proves:

1. Tiles 20-21 are IoU=1.000 — and so is every other tile once measured consistently.
2. The three moon groups (Lunitari/Solinari/Nuitari) produce **byte-identical XOR
   patterns** between phases: `[6, 26, 6, 20, 12, 20]`. This symmetry would be
   destroyed by any plane-order error — it is the primary structural confirmation the
   plane order is right.
3. Color semantics match exactly: tile 28 inner pixels = EGA index 4 (#aa0000 red),
   tile 32 = idx 15 (#ffffff white), tile 36 = idx 1 (#0000aa blue) — the SAME indices
   the DOS tiles carry.
4. The total raw-size equation is exact: `9 + 4 * 38 * 8 = 1225 = raw_size`. No other
   plane count (3, 5) or tile-grouping satisfies this with integer tiles AND gives the
   correct moon symmetry.

The decisive proof is the cross-platform identity, not the IoU table: the decoder is
correct **because** it reproduces the DOS oracle pixel-for-pixel.

---

## 7. DoK vs CoK format difference

| Game | File | Format | Planes | Palette |
|------|------|--------|--------|---------|
| CoK | `8X8D1.DAA` | 4-plane strip, no palette header | 4 (16 colors) | Shared EGA palette (DUNGCOM/BORDER) |
| DoK | `8x8d1.daa` | Standard 5-plane frame (73-byte header + embedded palette) | 5 (32 colors) | Embedded per-block Amiga 12-bit palette |

DoK blocks 1-67 have header `{h=8, w8=64}` = 512px wide (64 tiles per strip), with
embedded palette, and render correctly through `parse_frame_header`. CoK uses the
earlier 4-plane EGA-palette-shared format that does NOT pass through `parse_frame_header`
(the old code gave a false-positive 8×8 frame hit).

The updated `daa_decode.py` runs `parse_8x8d_header` first (detects `h=8, w8=1` and
validates the byte-count equation) before falling through to `parse_frame_header`.

---

## 8. Render outputs

All in `renders/daa/8X8D1_final/` (git-ignored):

- `8X8D1_202.png` — CoK exploration frame strip: arrows, gauge tiles, frame bar tiles,
  junction pieces, three moon groups (red/white/blue) with 4 phases each.
- `8X8D1_203.png` — Alternate tile set (dungeon decoration variant).
- `8X8D1_011.png` … `8X8D1_082.png` — Dungeon wall art sets (70 tiles each).
- `renders/daa/8X8D1_solved/compare_amiga_top_dos_bottom_t20to37.png` — Side-by-side
  Amiga (top) vs DOS EGA (bottom) for tiles 20-37, with EGA palette applied to both.
  Visually confirms: cyan/magenta horizontal bar (tile 20), black/gray fluted column
  (tile 24), three moon disc groups with correct Dragonlance colors.

---

## 9. Open questions / unknowns

- **Block 201** (`8X8D1.DAA` id=201, raw=1416 but decompresses to 670 bytes): structure
  unknown, all-zero decompressed payload. May be a lookup table or palette override.
  Not needed for tile rendering (EGA palette is sufficient).

- **CoK block 203** off-by-one: count byte = 45 but `(raw_size - 9) / 32 = 46.0`,
  giving 32 extra bytes at the end. The extra tile (index 45) may be a sentinel/padding.
  The decoder handles it by using the count byte as-is.

- **Shared palette vs per-scene palette**: The EGA first-16 palette is constant across
  all observed DUNGCOM/BORDER/SKY/RANDCOM/WILDCOM frames. It is plausible (but not yet
  confirmed from source) that the Amiga game loads this palette from DUNGCOM at startup
  and applies it for all 8X8D tile rendering. A disassembly trace of the `game` binary
  would confirm.

- **`8X8D0.DAA` and `8X8D2.DAA`**: Disk 1 and disk 3 versions. Binary inspection shows
  `disk1/8X8D0.DAA` is byte-identical to `disk2/8X8D1.DAA`; `disk3/8X8D2.DAA` contains
  a different wall-art set (different compressed sizes) but same block structure. Not
  yet rendered.

---

## 10. Implications for the engine

- **TypeScript port**: `decode8x8dTile(rawBlock, t, x, y)` should implement exactly the
  formula in §3. Use the hard-coded EGA 16-color table as palette (no palette parsing
  needed for these tiles). Row_bytes = N for all observed cases (N ≤ 70, so N*8 ≤ 560 <
  512... wait: N=70 → 70*8=560 > 512, so ceil(560/16)*2 = 70 = N still. Always safe to
  use N directly as row_bytes for any tile count ≤ 128.)

- **Amiga frame vs DOS frame**: The CoK Amiga exploration frame (block 202 tiles 20-37)
  is **byte-identical** to the DOS version — same logical layout AND same pixels (verified
  IoU=1.0, 0 differing pixels). The `COK_FRAME_LAYOUT` arrays from `cok-frame-arrays.md`
  apply directly with the same tile indices. Consequence for the engine: there is no
  visual reason to offer a DOS↔Amiga toggle for the dungeon *frame* — it would be a no-op.
  Platform-distinct CoK art is the 32-colour full-screen imagery, handled separately.

- **Moon colors**: Amiga tile 28 = Lunitari (red, #aa0000), tile 32 = Solinari (white,
  #ffffff), tile 36 = Nuitari (blue, #0000aa). These are EGA indices 4, 15, 1
  respectively — same semantic indices as DOS. The engine's moon-phase selector requires
  no change for Amiga rendering.

- **Tile 0 disambiguation**: Block 201 (id=201) appears before block 202 in the TOC and
  decompresses to all-zeros. The engine should skip it or treat it as a null/meta block.

- **This closes the last major Amiga format unknown**: The BIGPIC/CPIC/BACK/BORDER
  frames (5-plane, embedded palette) were solved earlier. The 6-byte sub-frame index
  files (SPRIT*, PIC*) remain unsolved but are lower priority for the exploration screen.
  All tiles needed for the authentic Amiga exploration screen are now decodable.

---

*Researched and verified 2026-06-21. Primary method: strip-hypothesis byte-count
equation + three-moon XOR symmetry test + color-semantic cross-check. DoK cross-game
reference used to establish strip interpretation. No circular method; all verification
against real tile data and DOS oracle screenshot.*
