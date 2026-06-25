# CoK DOS EGA Exploration/Travel Screen — Reproduction Spec

**Status: CONFIRMED** from COAB source oracle (MIT, `github.com/simeonpilgrim/coab`),
cross-checked against real CoK `8X8D1.DAX` in `Champions of Krynn/` via
`tools/dax_decode.py`. SSI executables were **not** decompiled. Every structural claim
is labelled with its COAB source file + function. Inferences are labelled **(inferred)**.

This document drives `composeExploreScreen` in the Greybox engine. For the 3-D viewport
internals (wall geometry, depth passes, WALLDEF/8X8D pairing) see
`docs/engine/research/firstperson-wall-geometry-cok.md`.

> **CORRECTION (2026-06-21, VERIFIED vs a real screenshot, MSE=0):** §2/§6 below give COAB's
> frame *values*, which are NOT CoK's. CoK's exploration frame is still **set 4 = block 202**
> (the boot-load and routing match COAB), but the **placement arrays differ**: CoK uses a clean
> 6-tile cyan/gray frame (block-202 tiles 20=horiz bar, 24=vert column, 23/25=junctions,
> 21/22=moon brackets) with the three **moons** (tiles 26–37) overlaid on the top row at cols
> 8/19/30. COAB's arrays point at tiles 30–39 (= CoK's moons), so they draw moons as the border.
> The font (§7, block 201), party panel (§4), position line, palette (§8), and 40×25 geometry
> (§1) are all confirmed correct and render perfectly. The verified CoK arrays are in
> **`docs/engine/research/cok-frame-arrays.md`** = `COK_FRAME_LAYOUT` (the compositor default).
> (An earlier note here said the frame art was "block 203"; that was a circular-method artifact —
> the border is block 202.)

---

## 1. Screen geometry

| Property | Value | Source |
|---|---|---|
| Resolution | **320 × 200 pixels** | EGA mode 0Dh/10h (standard Gold Box) |
| Color depth | **16 colors** (EGA palette) | `seg040.SetPaletteColor` → `Display.SetEgaPalette` |
| Tile grid | **40 cols × 25 rows** (0-indexed 0..39, 0..24) | `seg041.display_char01`: guard `XCol < 40 && YCol < 25` |
| Tile size | **8 × 8 pixels** | `seg041.displayString` increments `xCol` per char; `DrawRectangle` multiplies by 8 |
| Tile-to-pixel | pixel_x = col × 8; pixel_y = row × 8 | `seg040.draw_clipped_picture` |

The exploration screen is the **`GameState.DungeonMap`** layout. It is drawn by:

1. `seg037.draw8x8_03()` — sets up the outer border, dividers, and inner viewport frame.
2. `ovr029.RedrawView()` → `ovr031.Draw3dWorld()` — fills the 3-D first-person view.
3. `ovr025.PartySummary()` — right-panel party status.
4. `ovr025.display_map_position_time()` — compass/time line.

Source: `ovr025.cs:1435–1440`, `ovr003.cs:598–600`.

---

## 2. Frame layout — complete symbol placement for `draw8x8_03`

`seg037.draw8x8_03()` (source: `seg037.cs:73–102`) calls `DrawFrame_Outer()` then adds
a horizontal separator, a vertical separator, and an inner viewport border.

### 2.1 Outer border — `DrawFrame_Outer()` (`seg037.cs:31–54`)

Clears the interior first: `draw8x8_clear_area(0x16, 0x26, 1, 1)` →
`DrawRectangle(0, 22, 38, 1, 1)` → pixels y∈[8,184), x∈[8,312) set to color 0.

Then draws four runs:

```
Top row:    col=0..39, row=0    → outer_frame_top[col]   + 0x11E
Bottom row: col=0..39, row=23   → outer_frame_bottom[col] + 0x11E
Left col:   row=0..22, col=0    → outer_frame_left[row]  + 0x11E
Right col:  row=0..22, col=39   → outer_frame_right[row] + 0x11E
```

Symbol ids are all in **set 4** (range 0x100..0x127), so every border tile comes from
`8X8D1.DAX` block 202. The four arrays (verbatim from `seg037.cs:7–21`):

```
outer_frame_top    = {0,6,1,1,1,1,1,1,6,1,1,1,1,4,1,1,1,6,1,1,1,1,1,1,1,8,1,1,1,1,1,1,1,4,1,1,1,6,1,2}
outer_frame_bottom = {1,8,6,1,1,1,1,1,1,1,1,4,1,1,1,1,1,6,8,1,1,1,4,1,1,1,1,1,1,6,1,1,1,1,1,1,1,1,4,3}
outer_frame_left   = {0,2,9,5,2,2,2,2,2,2,5,7,2,2,2,2,2,9,7,2,2,2,7,1}   (24 entries, rows 0..23)
outer_frame_right  = {2,2,9,7,2,2,2,5,2,2,2,2,2,2,2,2,2,7,2,2,2,2,5,2}   (24 entries, rows 0..23)
```

Pixel coverage:
- Top border:    y ∈ [0, 8),   x ∈ [0, 320)
- Bottom border: y ∈ [184,192), x ∈ [0, 320)
- Left border:   x ∈ [0, 8),   y ∈ [0, 192)
- Right border:  x ∈ [312,320), y ∈ [0, 192)

### 2.2 Horizontal separator at row 16 (`seg037.cs:63–66`)

```
col=0..39, row=16 → x8x8_07[col] + 0x11E
x8x8_07 = {0,8,1,1,1,1,1,1,1,1,1,6,1,1,1,8,4,1,1,1,6,1,1,1,1,1,1,1,1,1,4,1,6,1,1,1,1,1,8,2}
```

Pixel coverage: y ∈ [128,136), x ∈ [0,320). Same set-4 symbols.

### 2.3 Vertical separator at col 16 (`seg037.cs:80–83`)

```
row=0..16, col=16 → unk_16F0A[row] + 0x11E
unk_16F0A = {0,7,5,2,2,2,2,2,2,2,2,2,2,5,2,9,4}  (17 entries, rows 0..16)
```

Pixel coverage: x ∈ [128,136), y ∈ [0,136).

The intersection at (row=16, col=16) is drawn by the horizontal pass (it overwrites the
vertical's last entry).

### 2.4 Inner viewport border (`seg037.cs:88–99`)

Four runs using **base offset 0x114** (set 4, indices 0x14..0x1D):

```
Inner top:    col=2..14, row=2   → unk_16ED6[col-2] + 0x114
Inner bottom: col=2..14, row=14  → unk_16EE3[col-2] + 0x114
Inner left:   row=2..14, col=2   → unk_16F31[row-2] + 0x114
Inner right:  row=2..14, col=14  → unk_16F3E[row-2] + 0x114
```

Arrays (verbatim `seg037.cs:9–24`; indices 2..14 from each = 13 entries used):

```
unk_16ED6 = {4,3,0,6,1,1,1,1,8,1,1,4,1,1,2,1,4}   (col offset, use [2..14])
unk_16EE3 = {1,2,1,4,1,1,1,1,1,1,8,4,1,1,3}         (col offset, use [0..12] → cols 2..14)
unk_16F31 = {5,2,0,2,7,2,2,2,2,5,2,2,2,2,1}         (row offset, use [2..14])
unk_16F3E = {2,1,2,5,9,2,2,2,7,5,2,2,2,2,3}         (row offset, use [2..14])
```

Pixel coverage:
- Inner top:    y ∈ [16,24), x ∈ [16,120)
- Inner bottom: y ∈ [112,120), x ∈ [16,120)
- Inner left:   x ∈ [16,24), y ∈ [16,120)
- Inner right:  x ∈ [112,120), y ∈ [16,120)

---

## 3. Screen region map (pixel coordinates, exploration mode)

All coordinates are **[inclusive_start, exclusive_end)** in pixels.

```
Full screen: x [0,320), y [0,200)

┌─────────────────────────────────────────────────────┐
│ Outer top border   y[0,8)   x[0,320)                │
├──────────┬──────────┬──────────┬────────────────────┤
│OL        │ 3-D VP   │ VSep     │ Right panel         │
│x[0,8)   │ inner    │ x[128,   │ x[136,312)          │
│          │ frame    │  136)    │                     │
│          │ x[16,120)│          │ row 2: "Name"/stats │
│          │ y[16,120)│          │ rows 4..9: players  │
│          │          │          │ row 15: pos/time    │
│ (OL)     │ 3-D clip:│          │                     │
│ y[8,136) │ x[8,176) │ y[8,136) │ y[8,128)            │
│ x[0,8)   │ y[8,176) │          │                     │
├──────────┴──────────┴──────────┴────────────────────┤
│ Horizontal separator   y[128,136)   x[0,320)         │
├─────────────────────────────────────────────────────┤
│ Text/message area   y[136,184)   x[8,312)            │
│ (rows 17..22, cols 1..38)                            │
├─────────────────────────────────────────────────────┤
│ Outer bottom border y[184,192)   x[0,320)            │
├─────────────────────────────────────────────────────┤
│ Prompt area (row 24) y[192,200)  x[0,320)            │
└─────────────────────────────────────────────────────┘
OL = outer left border x[0,8)
OR = outer right border x[312,320) (not shown separately above)
```

### Region table

| Region | Tile rect (row,col) | Pixel rect (x,y) | Notes |
|---|---|---|---|
| Outer top border | row=0, col=0..39 | x[0,320), y[0,8) | `DrawFrame_Outer` |
| Outer bottom border | row=23, col=0..39 | x[0,320), y[184,192) | same |
| Outer left border | col=0, row=0..23 | x[0,8), y[0,192) | same |
| Outer right border | col=39, row=0..23 | x[312,320), y[0,192) | same |
| Inner viewport frame | row=2..14, col=2..14 | x[16,120), y[16,120) | `draw8x8_03` inner |
| **3-D viewport clip** | (logical, not tile) | **x[8,176), y[8,176)** | `draw_combat_picture` clip |
| 3-D background fill | (within clip) | x[24,112), y[24,112) | `Draw3dWorldBackground` color blocks |
| Vertical separator | col=16, row=0..16 | x[128,136), y[0,136) | `draw8x8_03` |
| Horizontal separator | row=16, col=0..39 | x[0,320), y[128,136) | `draw8x8_03` |
| Right panel | row=1..15, col=17..38 | x[136,312), y[8,128) | party status area |
| Text/message area | row=17..22, col=1..38 | x[8,312), y[136,184) | `press_any_key` region |
| Prompt bar | row=24, col=0..39 | x[0,320), y[192,200) | `ClearPromptArea` |

### 3-D viewport detail

The 3-D first-person view clip box is **x∈[8,176), y∈[8,176) = 168×168 pixels** (the
`draw_combat_picture` hard clip; see `seg040.cs:117`). Wall tiles can reach anywhere
inside this box; the background color-fill anchors at x=24, y=24. The **inner viewport
frame** (rows 2..14, cols 2..14) is drawn **after** the 3-D render, so it appears on top
as a decorative border. Net visible 3-D interior (inside the inner frame):
rows 3..13, cols 3..13 → x∈[24,112), y∈[24,112) = 88×88 pixels.

The 3-D clip is shared with the combat viewport (hence the name `draw_combat_picture`).
`draw_picture` (unclipped, 0..320, 0..200) is used for non-3D symbol blits.

---

## 4. Right-panel party summary

Source: `ovr025.cs:216–258` (`PartySummary`), `ovr025.cs:1476–1510`
(`display_map_position_time`).

In `GameState.DungeonMap`, `x_pos = 17` (not 1). All columns below are tile units;
multiply by 8 for pixels.

```
row=2, col=17:   "Name"   (header, color 15 = white)
row=2, col=33:   "AC  HP" (header, color 15 = white)
row=4:           player[0]
row=5:           player[1]
  ...
row=3+N:         player[N-1]  (max 6 players → rows 4..9)

Per-player row layout:
  col=17 (x=136): player name   (active = color 15 white; others via displayPlayerName)
  col=31 (x=248): AC value      (color 10 = bright green)
  col=36 (x=288) or 37 (x=296): HP value  (color 10/14 depending on HP status)
    hpXPos=0 if HP ≥ 100, =1 if HP 10..99, =2 if HP 0..9 → adjusts HP column
    HP color: 0x0A (bright green) if full; 0x0E (yellow) if below max; 0x0D (magenta) if highlighted

Map position/time line:
  row=15, col=17 (x=136, y=120):  "X,Y DIR HH:MM [mode]"
  clear before: draw8x8_clear_area(15, 38, 15, 17) → y[120,128), x[136,312)
  color 10 (bright green)
```

---

## 5. Text area and prompt

Two distinct zones:

| Zone | Tile bounds | Pixel bounds | Usage |
|---|---|---|---|
| Main text (bounds[0]) | rows 17..22, cols 1..38 | x[8,312), y[136,184) | Scrolling messages, menus |
| Short text (bounds[1]) | rows 21..22, cols 1..38 | x[8,312), y[168,184) | Shorter prompts |
| Prompt bar | row=24, cols 0..39 | x[0,320), y[192,200) | Input prompts, "Press any key" |

Source: `seg041.cs:119–123` (bounds array), `ovr027.cs:351–354` (`ClearPromptArea`).

`press_any_key` word-wraps within the given bounds, advancing `textXCol`/`textYCol`.
When the text overflows `yEnd`, it pauses ("Press any key to continue") and restarts.

---

## 6. Symbol → asset mapping

### Symbol id ranges and sets

`Put8x8Symbol` (source: `ovr038.cs:25–72`) routes symbol ids to one of five in-memory
symbol sets by range:

| Range | Set index | `symbol_set_fix` base | DAX block (boot load) |
|---|---|---|---|
| 1..0x2D (1..45) | 0 | 0x0001 | `8X8D1.DAX` block **203** (0xCB) — 45 tiles |
| 0x2E..0x73 (46..115) | 1 | 0x002E | loaded by wall rendering |
| 0x74..0xB9 (116..185) | 2 | 0x0074 | loaded by wall rendering |
| 0xBA..0xFF (186..255) | 3 | 0x00BA | loaded by wall rendering |
| 0x100..0x127 (256..295) | 4 | 0x0100 | `8X8D1.DAX` block **202** (0xCA) — 38 tiles |

`symbol_set_fix` is declared in `Classes/Gbl.cs:425`:
```csharp
public readonly static short[] symbol_set_fix = { 0x0001, 0x002E, 0x0074, 0x00BA, 0x0100 };
```

Tile lookup: `tile_index = symbol_id - symbol_set_fix[set]`, then blit
`symbol_8x8_set[set].data[tile_index * 32 ... + 32]` (32 bytes per 8×8 4bpp tile).

**Boot loads** (source: `seg001.cs:309–310`):
```csharp
ovr038.Load8x8D(4, 0xca);   // set 4 ← 8X8D1 block 202 (UI/frame tiles, 38 tiles)
ovr038.Load8x8D(0, 0xcb);   // set 0 ← 8X8D1 block 203 (frame corner/decor tiles, 45 tiles)
```

Sets 1/2/3 are loaded on-demand by the wall rendering system.

### Frame tile mapping (exploration screen only)

All outer-frame and separator symbols come from **set 4** (block 202, 38 tiles):

```
Outer frame base offset:  0x11E (= 286)
  → set 4 indices 0x11E - 0x100 = 30..39 (values 0..9 from the arrays)
  Symbol 0x11E → tile index 30;  0x127 → tile index 39

Inner viewport border offset: 0x114 (= 276)
  → set 4 indices 0x114 - 0x100 = 20..29 (values 0..9 from inner arrays)
  Symbol 0x114 → tile index 20;  0x11D → tile index 29
```

### 8X8D block 202 (set 4) tile inventory

Block 202 (`8X8D1.DAX`, 0xCA): **38 tiles**, confirmed by `raw[8]` byte = 0x26 = 38 and
`17 + 38*32 = 1233 = raw_size` (source: `docs/engine/research/8x8d-tile-encoding-cok.md`).

```
Tiles  0..19: wall rendering tiles (used by WALLDEF system, via sets 1/2/3 overlap)
Tiles 20..29: inner viewport border pieces (set4 indices for offset 0x114..0x11D)
              — top/bottom/left/right border segments, corners, junctions
Tiles 30..37: outer frame pieces (set4 indices for offset 0x11E..0x125)
              — horizontal bar, vertical bar, corners, T-junctions, decorative nodes
```

**(inferred)** Exact artistic assignment of the individual 38 tiles (which index = corner
vs. straight vs. T-junction) is not specified in COAB — the array values define placement
and the tile art is in the DAX. Render `8X8D1.DAX` block 202 at offset 17 with
`tools/dax_decode.py` to see all 38 tiles.

### 8X8D block 203 (set 0) tile inventory

Block 203 (`8X8D1.DAX`, 0xCB): **45 tiles** (`raw[8]` = 0x2D = 45, `17+45*32=1457`).
Symbol ids 1..45 map to set 0 tile indices 0..44. These tiles are used by the area map
display (`Put8x8Symbol` in `ovr031.DrawAreaMap`) for map cell overlays.

---

## 7. Bitmap font (text rendering)

### Font source

Block 201 (`8X8D1.DAX`, id 0xC9) is the **monochrome 8×8 bitmap font**. It is loaded at
boot by `seg041.Load8x8Tiles()` (source: `seg041.cs:24–40`):

```csharp
seg042.load_decode_dax(out block_ptr, out block_size, 201, "8X8d1.dax");
// Copies into gbl.dax_8x8d1_201[j, k]:
// j = glyph index (0..176), k = row byte (0..7)
// stride: i += 8 per glyph (8 bytes per glyph)
```

Block 201 is **not** a standard tile-strip block — its first two bytes are not a valid
header (`raw[8] = 0x2D` but dims bytes are garbage). The code treats it as a flat
177 × 8 byte array (177 glyphs, 8 bytes each = 8 scanline bytes per glyph, 1 bit per
pixel). Confirmed by: `gbl.dax_8x8d1_201 = new byte[177, 8]`, `i += 8, j++`.

**File location:** `Champions of Krynn/8X8D1.DAX`, block id 201 (first TOC entry).
**Format:** flat array, 1 bit per pixel per row, 8 rows × 8 columns per glyph.
**Glyph count:** 177 glyphs (`raw_size / 8 = 1416 / 8 = 177`).

### ASCII → glyph mapping

Source: `seg041.cs:49`:
```csharp
char index = (char)(char.ToUpper(ch) % 0x40);
```

Mapping: `glyph_index = ASCII_value % 64` (after uppercasing).

Key mappings:
```
ASCII 64 ('@')  → glyph 0   (also: null char, '@' itself)
ASCII 65 ('A')  → glyph 1
ASCII 66 ('B')  → glyph 2
  ...
ASCII 90 ('Z')  → glyph 26
ASCII 32 (' ')  → glyph 32
ASCII 48 ('0')  → glyph 48
  ...
ASCII 57 ('9')  → glyph 57
ASCII 33 ('!')  → glyph 33, ASCII 44 (',') → 44, etc.
```

All characters are upper-cased before index calculation; only indices 0..63 are
meaningful (the `% 0x40` folds the range). Glyphs 64..176 exist in the data but are not
addressed by normal text paths.

Verified by decoding block 201 bytes: glyph 1 (='A') produces a recognizable capital A
bitmap; glyph 32 (=' ') is all zeros (blank).

### Rendering

`seg041.display_char01(char ch, repeatCount, bgColor, fgColor, rowY, colX)`:
- Reads 8 bytes from `gbl.dax_8x8d1_201[index, 0..7]`.
- Calls `Display.DisplayMono8x8(colX, rowY, monoCharData, bgColor, fgColor)`.

`Display.DisplayMono8x8` (not in COAB source, **(inferred)**) expands each bit to a
pixel using `fgColor` for 1-bits and `bgColor` for 0-bits, placed at pixel
(colX × 8, rowY × 8). Normal text: bgColor=0 (black), fgColor=10 (bright green) or
other EGA colors per context.

Character guards: `XCol < 40 && YCol < 25` — writes outside the 40×25 grid are
silently dropped.

---

## 8. EGA palette

Standard IBM EGA 16-color palette as used throughout the Gold Box engine
(from `tools/dax_decode.py EGA_PALETTE`, matching `seg040.SetPaletteColor` behavior):

| Index | Color | R,G,B |
|---|---|---|
| 0 | Black | 0x00, 0x00, 0x00 |
| 1 | Blue | 0x00, 0x00, 0xAA |
| 2 | Green | 0x00, 0xAA, 0x00 |
| 3 | Cyan | 0x00, 0xAA, 0xAA |
| 4 | Red | 0xAA, 0x00, 0x00 |
| 5 | Magenta | 0xAA, 0x00, 0xAA |
| 6 | Brown | 0xAA, 0x55, 0x00 |
| 7 | Light gray | 0xAA, 0xAA, 0xAA |
| 8 | Dark gray | 0x55, 0x55, 0x55 |
| 9 | Bright blue | 0x55, 0x55, 0xFF |
| 10 | Bright green | 0x55, 0xFF, 0x55 |
| 11 | Bright cyan | 0x55, 0xFF, 0xFF |
| 12 | Bright red | 0xFF, 0x55, 0x55 |
| 13 | Bright magenta | 0xFF, 0x55, 0xFF |
| 14 | Yellow | 0xFF, 0xFF, 0x55 |
| 15 | White | 0xFF, 0xFF, 0xFF |

Color 13 (bright magenta) is the **transparency key** for overlay blits
(`OverlayUnbounded`, `OverlayBounded`). The engine skips pixels of this color when
drawing over existing content.

Color usage in exploration UI:
- Party names (current player): color 15 (white)
- Party names (others): context-dependent (green normally)
- HP (full): color 10 (bright green); HP (damaged): color 14 (yellow)
- AC and general stats: color 10 (bright green)
- Map position/time: color 10 (bright green)
- Menu highlights: color 15 (white) bg, color 0 (black) fg — reversed
- Sky color: from `sky_colours` array = {0,15,4,11,13,2,9,14,...} indexed by area setting

The `makeEgaPalette` function in the Greybox engine should match this table exactly.
No palette remapping is applied in the exploration mode (only `SetPaletteColor` calls
which stay within the standard EGA range). Source: `seg040.cs:131–140`.

---

## 9. Platform notes (Amiga / C64)

### Amiga

Assets: `amiga_extracted/` (`.DAA` container, big-endian DAX cousin).

- Resolution: likely **320×200** with **32 colors** (OCS/ECS HAM or palette-cycling),
  not 16. **(inferred)** — Amiga Gold Box titles typically ran in EHB or 32-color mode.
- The frame/border tiles in `.DAA` are Amiga-native and may differ artistically; the
  same logical layout (outer frame + inner viewport frame + right panel + text area) is
  expected to apply but exact symbol ids and tile art differ.
- The vertical and horizontal separator positions are likely identical (same engine
  design), but art quality is higher (more colors per tile).
- Do NOT use Amiga `.DAA` blocks for CoK DOS screen reproduction — they are a separate
  graphical set with different palettes.
- For CoK exploration screen, Amiga assets are only relevant to the **live graphic-mode
  switching** feature (switch Amiga art into the same layout at runtime).

### C64

**(inferred, not researched)** The Commodore 64 port used a different screen mode
(typically 320×200 multicolor or HIRES with very limited palette). Layout proportions
may differ; the text area may be smaller or repositioned. Not a current target for
Greybox; flag for later research if C64 support is added.

---

## 10. Draw order summary (compositor sequence)

For a `composeExploreScreen` implementation:

```
1. Fill: DrawRectangle(0, 22, 38, 1, 1)         — clear interior [8,184)×[8,312)
2. Outer frame: Put8x8Symbol for rows/cols 0,23 and 0,39 — 4 passes over border arrays
3. draw8x8_03 additions:
   a. Horizontal separator: Put8x8Symbol(x8x8_07[col]+0x11E) at row=16, col=0..39
   b. Vertical separator:   Put8x8Symbol(unk_16F0A[row]+0x11E) at col=16, row=0..16
   c. Inner top:    Put8x8Symbol(unk_16ED6[ci]+0x114) at row=2, col=2..14
   d. Inner bottom: Put8x8Symbol(unk_16EE3[ci]+0x114) at row=14, col=2..14
   e. Inner left:   Put8x8Symbol(unk_16F31[ri]+0x114) at col=2, row=2..14
   f. Inner right:  Put8x8Symbol(unk_16F3E[ri]+0x114) at col=14, row=2..14
4. 3-D viewport render: Draw3dWorldBackground() then Draw3dWorld() — all clipped [8,176)×[8,176)
5. Right panel: PartySummary() — text blits at col=17..38, rows 2..9
6. Position/time: display_map_position_time() — row=15, col=17
7. Text area: press_any_key / displayString — rows 17..22, cols 1..38
8. Prompt: row 24, full width (on user input only)
```

Step 4 is drawn **inside** the border set in step 3; the inner frame (step 3c-f) visually
frames the 3-D view. The 3-D tiles clip at [8,176)×[8,176) and therefore extend slightly
beyond the inner border (8px gap from the true screen edge to the inner border at row/col 2).

---

## 11. Open questions / unknowns

1. **`Display.DisplayMono8x8` internals** — the C# Display class is an abstraction layer
   in COAB that does not expose its pixel-level implementation. The bit→pixel expansion
   rule (fg/bg color per bit) is inferred from the parameters passed; the exact scan order
   (MSB first or LSB first per byte) must be confirmed by rendering a known glyph and
   comparing against a screenshot. **Hypothesis: MSB = leftmost pixel** (standard for
   monochrome bitmaps), confirmed by visual inspection of decoded block 201 glyphs.

2. **Block 202 tile artistic index** — which of the 38 tiles in block 202 is the
   horizontal bar, which is the corner, etc. Not needed for layout correctness (the
   arrays define placement), but needed for a tile-catalog comment. Resolve by
   rendering `8X8D1.DAX` block 202 at offset 17 and annotating the output.

3. **Set 0 (block 203) during exploration** — block 203 is loaded into set 0 at boot,
   but the exploration frame arrays only use set 4 (block 202). Block 203 tiles (symbol
   ids 1..45) appear in `DrawAreaMap` (the 2-D map overlay mode). It is unknown whether
   any exploration-screen elements use set 0. Current evidence: **none in use**
   during exploration (all frame symbols are in the 0x114..0x127 = set4 range).

4. **Party name color logic** — `displayPlayerName` (called for non-active players) does
   not reveal its color in the excerpts read. **(inferred)** It uses color 10 or a
   context color; the highlighted (active) player always gets color 15 (white). Confirm
   by instrumenting or screenshot-matching.

5. **Prompt bar row** — `ClearPromptArea` clears `DrawRectangle(0, 0x18=24, 0x27=39,
   0x18=24, 0)` = rows 24..24 = y[192,200). But `displayString` in `getUserInputString`
   and `DisplayAndPause` use `rowY=0x18=24` which is `y=[192,200)`. This is the 25th row
   (index 24) = last row. Confirmed: prompt bar is the bottom 8 pixels of the screen.

6. **CoK vs. COAB (Curse of Azure Bonds) differences** — COAB targets the same older
   Gold Box engine as CoK/DoK, but is a different game. Array values (frame symbols) and
   8X8D block ids may differ slightly between CoK and AzBonds. The block 202/203 ids and
   the `symbol_set_fix` are almost certainly shared (they are engine constants, not
   game-data), but verify against a CoK screenshot if available.

---

## 12. Implications for the engine (`composeExploreScreen`)

1. **Two sets of frame symbols**: the outer border and main separators all use set-4
   symbols (block 202); the inner viewport border also uses set 4 but a different index
   offset (0x114 vs 0x11E). A single symbol-set load at boot covers all frame drawing.

2. **Font is monochrome, not EGA**: `Display.DisplayMono8x8` expands 1-bit glyphs to
   two EGA colors at blit time. The engine must implement a mono-to-color expander,
   **not** use the chunky-4bpp tile blitter for text.

3. **3-D viewport clip is hard-coded at [8,176)**: All wall tile blits go through
   `draw_clipped_picture` with fixed clip constants. The compositor must not assume the
   inner frame at rows 2..14 / cols 2..14 is the effective clip — it is a decorative
   element drawn over the already-rendered 3-D content.

4. **Vertical separator splits the screen at col 16 (x=128), not at x=128+8=136**: The
   right panel begins at col=17 (x=136), giving a 1-tile-wide visual border between the
   3-D area and the party panel.

5. **Party summary `x_pos=17`** in dungeon/exploration mode (not 1). A compositor that
   handles multiple game states must switch `x_pos` accordingly.

6. **`flag: most likely wrong assumption`** — The inner viewport frame position (rows 2..14,
   cols 2..14) implies the 3-D view window is 13×13 tiles = 104×104 px. But the hard
   clip is 168×168 px and the background fill starts at x=24, y=24. **The most
   dangerous assumption is that the inner frame exactly encloses the 3-D content** — it
   does not. Wall tiles can and do extend beyond the inner frame into the single-tile
   gap (rows/cols 1..1 and 15..15 between inner frame and outer border). The
   compositor must apply the [8,176) clip first, then draw the inner frame on top, so
   wall overflow into the gap is masked by the frame tiles, not the other way around.

---

*Research by Greybox AI (Sonnet), 2026-06-21. Oracle: COAB (`simeonpilgrim/coab`, MIT),
files `engine/seg037.cs`, `engine/ovr038.cs`, `engine/seg040.cs`, `engine/seg041.cs`,
`engine/ovr025.cs`, `engine/ovr027.cs`, `engine/ovr029.cs`, `engine/ovr031.cs`,
`engine/seg001.cs`, `Classes/Gbl.cs`. Cross-checked against `Champions of Krynn/8X8D1.DAX`
via `tools/dax_decode.py` and inline Python. SSI executables not decompiled.*
