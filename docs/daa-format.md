# SSI Gold Box AMIGA `.DAA` Graphics Format

Reverse-engineered and verified empirically against Champions of Krynn (CoK) and
Death Knights of Krynn (DoK) Amiga files in `amiga_extracted/`. The Amiga `.DAA`
format is the **big-endian, bitplanar, palette-carrying** cousin of the DOS
`.DAX` format (see `docs/dax-format.md`). Cross-validated against:
- the DOS `.DAX` render of the same-named blocks (`BIGPIC1` block 114 = identical
  scene, identical 304×120 dimensions, richer 32-color palette), and
- the game's own IFF ILBM title screen (`scrn1b.lbm`), which carries the same
  32-color Amiga palette and confirmed the PNG pipeline first.

Reference decoder: `tools/daa_decode.py`. Sample PNGs: `renders/daa/`.

**All multi-byte integers are BIG-ENDIAN (Motorola 68000).** This is the single
biggest structural difference from DOS DAX (which is little-endian).

---

## 1. Container layout — VERIFIED

```
offset  size  field
0       2     word0          = 2 + entry_count*9   (BIG-ENDIAN). Also == data_base.
2       9*N   TOC: N = (word0-2)/9 entries, each 9 bytes, BIG-ENDIAN:
                +0  uint8   block_id      sparse id (NOT a 0..N-1 index)
                +1  uint32  data_offset   relative to data section base (BE)
                +5  uint16  raw_size      decompressed size (BE)
                +7  uint16  comp_size     compressed size (BE)
word0   ...   data section (concatenated RLE blocks)
```

- `data_base = word0` (i.e. `2 + N*9`). **Note the contrast with DOS DAX, where
  `word0 = N*9` and `data_base = 2 + word0`.** In DAA `word0` *includes* the
  2-byte header, so `data_base == word0`.
- Block *k* compressed bytes = `file[data_base + data_offset : + comp_size]`.
- `data_offset` chains perfectly: `entry[i+1].data_offset == entry[i].data_offset
  + entry[i].comp_size`, and the last block ends exactly at end-of-file.
- `block_id` is sparse, exactly like DAX (e.g. CoK `BIGPIC1` ids 112,114,115,116,121).

Worked example — CoK `disk2/BIGPIC1.DAA`: `word0 = 0x002F = 47 = 2 + 5*9`,
so 5 entries, `data_base = 47`. Entry 0 = `{id=114, off=0, raw=22873, comp=21250}`.

### 1b. The 6-byte sub-frame INDEX variant — PARTIALLY SOLVED
Some files (all `SPRIT*`, the DoK `PIC*`, DoK `PIC2`, and the larger `HEAD*`) use
`word0 = 2 + entry_count*6` with a different TOC: each entry is 6 bytes
`{u8 block_id, u8 zero, u32 sub_offset (BE)}` and carries **no raw/comp sizes** —
each sub-frame's length is `next.sub_offset - this.sub_offset`. The decoder
detects these (when the 9-byte chain validation fails) and lists them, but the
inner sub-frame pixel encoding is **not yet decoded** (it is likely the
transparent/multi-sub-frame variant, the analogue of the DOS DAX "complex" frames
that were also left unsolved there).

How the decoder disambiguates: try the 9-byte TOC, accept it only if every
`data_offset` chains and the last block ends within 4 bytes of EOF; otherwise fall
back to the 6-byte index.

`pointers.daa` / `globals.daa` have `word0 == 0` and are **not** graphics
containers (lookup/pointer tables).

---

## 2. RLE decompression — VERIFIED (identical to DAX)

Byte-order-independent signed-byte run length, byte-for-byte the same scheme as
DOS DAX:

```
while i < len(comp):
    c = comp[i]; i += 1
    if c < 0x80:   copy next (c+1) literal bytes
    else:          repeat next single byte (256-c) times
```

Output length equals `raw_size` exactly for every block tested (BIGPIC, CPIC,
BACK, 8X8D...). The RLE operates on bytes, so big-endianness does not affect it.

---

## 3. Image frame — VERIFIED (BIGPIC, CPIC, BACK, BIGPIC2 across CoK + DoK)

Each renderable block decompresses to:

```
offset  size       field
0       2          height            (pixels, BE)
2       2          width_div8        (width in pixels = width_div8 * 8, BE)
4       2          y_offset          (BE, usually 0)
6       2          x_offset          (BE, usually 0)
8       1          pad / flag        (observed 0x01)
9       64         palette           32 x uint16 BE, 0x0RGB 12-bit Amiga words
73      ...        pixel data        5 CONTIGUOUS bitplanes (see §4)
```

`width = width_div8 * 8`. Verified examples:

| File / block       | header (h, w/8) | width×height | renders as |
|--------------------|-----------------|--------------|------------|
| CoK BIGPIC1 b114   | (120, 38)       | 304×120      | CoK box scene: draconians, woman+infant, fire, mountains |
| CoK BIGPIC2 b116   | (120, 38)       | 304×120      | torch-lit dungeon, draconians at a table |
| CoK CPIC1 b129     | (24, 3)         | 24×24        | draconian warrior sprite w/ blue shield |
| CoK CPIC1 b130     | (24, 6)         | 48×24        | dragon/wyvern sprite |
| DoK BACK1 b1       | (88, 11)        | 88×88        | combat arena floor grid (perspective) |

CoK `BIGPIC1` block 114 matches the **DOS** `BIGPIC1.DAX` block 114 pixel-for-
pixel in subject and dimensions (304×120), the strongest cross-validation: same
scene, the Amiga version simply richer (32 vs 16 colors).

---

## 4. Pixel encoding — PLANAR, VERIFIED

Unlike DOS DAX (chunky 4bpp, 2 px/byte), Amiga `.DAA` stores **5 contiguous
(non-interleaved) bitplanes** for 32 colors:

```
row_bytes  = ceil(width/16) * 2          # rows are WORD (16-bit) aligned
plane_size = row_bytes * height
plane 0 occupies bytes [0 .. plane_size)             (whole image)
plane 1 occupies bytes [plane_size .. 2*plane_size)  ...
... up to plane 4.
pixel(x,y) index = sum over p of  bit(plane[p], y*row_bytes + x/8, 7-(x&7)) << p
```

Two facts had to be nailed empirically:
1. **Contiguous, NOT ILBM per-row interleave.** Per-row interleave (the ILBM
   convention) produced noise; whole-image contiguous planes produced the
   coherent scene. (This differs from the `.LBM` files, which DO use per-row
   interleave — see §6.)
2. **Word-aligned row stride.** Rows are padded to an even byte count
   (`ceil(width/16)*2`). This is invisible for widths that are already a multiple
   of 16 (e.g. 304 → 38 bytes) but essential for 24-wide sprites (3 → 4 bytes),
   which otherwise render as horizontal-streak noise. With the alignment, a
   24×24 sprite needs exactly `4*24*5 = 480` plane bytes = `raw - 73`, matching
   to the byte.

Color index 0 is transparent/background (renders black) on sprites.

---

## 5. Palette — VERIFIED (12-bit Amiga RGB, embedded per frame)

64 bytes at frame offset 9: **32 × big-endian uint16**, each `0x0RGB` (the classic
Amiga 12-bit color word; only the low nibble of each of R/G/B is used). Scale each
4-bit channel to 8-bit with `channel * 17` (equivalently `(c<<4)|c`). The first
eight entries of CoK `BIGPIC1` decode to the EGA-like ramp
`(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),(170,85,0),
(170,170,170)`, which matches the CMAP of the game's own `scrn1b.lbm`
(`(0,0,0),(0,0,160),…,(160,80,0),(160,160,160)`) — confirming both the format and
the x17 scaling. The palette is stored **identically in every block of a file**
(e.g. all 62 CPIC1 blocks repeat the same 64 palette bytes), i.e. it is a
per-file palette duplicated per frame.

---

## 6. IFF ILBM (`.LBM`) — used as the palette/pipeline oracle (VERIFIED)

The title/text screens (`scrn1b.lbm`, `scrn2.lbm`, `Title*.LBM`, `text*.lbm`,
`gothic.lbm`, `chars/chart`) are standard IFF ILBM:
`FORM…ILBM` → `BMHD` (320×200, 5 planes, compression=1) → `CMAP` (32 × 8-bit RGB,
already 0-255) → `BODY` (ByteRun1). **ILBM BODY uses per-row plane interleave**
(plane 0 row, plane 1 row, … then next scanline) — the opposite of `.DAA`'s
contiguous planes. The decoder includes an ILBM reader (`--lbm`); `scrn1b.lbm`
renders as a pixel-perfect "SSI / Advanced Dungeons & Dragons" title screen,
which validated the PNG pipeline before any `.DAA` work.

---

## 7. The 8x8 dungeon tiles (`8X8D*.DAA`) — SOLVED

**Status: SOLVED 2026-06-21.** Full details in
`docs/engine/research/amiga-8x8d-planeorder.md`. Decoder:
`parse_8x8d_header` / `decode_8x8d_tile` / `tiles_8x8d_to_rgb` in
`tools/daa_decode.py`, and `decodeDaaTileStrip` in the engine
(`packages/engine/src/loaders/daa.ts`).

**Cross-platform identity (the validating proof).** Decoding CoK `8X8D1.DAA` block 202
and DOS `8X8D1.DAX` block 202 through the same pipeline and comparing palette indices:
**all 38 tiles are byte-identical (0 differing pixels).** CoK's 8×8 UI chrome (frame,
moons, arrows) is the SAME 16-colour EGA art on both platforms — a correct plane order +
palette reproduces the DOS oracle exactly. (Earlier "richer Amiga 8×8 art" notes were a
circular-method artifact, now corrected in the research doc.) Pinned by
`packages/engine/test/daaTileStrip.test.ts`.

### 7a. Format overview

`8X8D*` blocks have the header `{height=8, width_div8=1, y=0, x=0}` (NO embedded
palette; `width_div8=1` is a marker, not the actual width). Byte 8 is the tile
count N. Pixel data starts at byte 9.

**This is NOT a standard frame.** It is a **4-plane contiguous tile strip**:
N tiles packed side-by-side into a wide virtual image (width = N×8 pixels), using
only 4 bitplanes (16 colors, not 32) and sharing the global EGA dungeon palette.
The detector (`parse_8x8d_header`) must run BEFORE `parse_frame_header` or the
block will be misidentified as a spurious 8×8 image.

```
Structure:
  raw[0:2]  u16 BE   height = 8  (constant)
  raw[2:4]  u16 BE   width_div8 = 1  (marker; actual strip width = N*8)
  raw[4:8]  u32 BE   y=0, x=0
  raw[8]    u8       N = tile_count
  raw[9..]  pixels   4 * row_bytes * 8 bytes

Plane layout (4-plane contiguous strip):
  row_bytes = ceil(N*8/16)*2     (word-aligned; equals N for N <= 128)
  plane_size = row_bytes * 8
  Plane p: raw[9 + p*plane_size .. 9 + (p+1)*plane_size)

Tile t, pixel x in [0,7], row y in [0,7]:
  byte  = raw[9 + p*plane_size + y*row_bytes + t]
  bit_p = (byte >> (7 - x)) & 1
  index = bit_0 | (bit_1<<1) | (bit_2<<2) | (bit_3<<3)

Detection: h==8 AND w_div8==1 AND (raw_size - 9) == 4 * row_bytes * 8
  (where N = raw[8], row_bytes = ceil(N*8/16)*2)
```

### 7b. Palette

No palette is stored in the tile block. Use the **shared EGA-compatible 16-color
dungeon palette** (first 16 entries of the 32-color DUNGCOM.DAA / BORDER.DAA
embedded palette, which is identical to the standard IBM EGA 16-color table):

```
0=#000000  1=#0000aa  2=#00aa00  3=#00aaaa  4=#aa0000  5=#aa00aa
6=#aa5500  7=#aaaaaa  8=#555555  9=#5555ff 10=#55ff55 11=#55ffff
12=#ff5555 13=#ff55ff 14=#ffff55 15=#ffffff
```

### 7c. CoK vs DoK difference

| Game | File | Planes | Palette |
|------|------|--------|---------|
| CoK | `8X8D1.DAA` | 4 (16-color strip, no palette header) | Shared EGA-16 |
| DoK | `8x8d1.daa` | 5 (standard frame with embedded 32-color palette) | Per-block Amiga 12-bit |

DoK `8x8d1.daa` uses the standard §3/§4 frame format (h=8, w_div8=N_tiles, full
73-byte header with embedded palette) and is handled by the existing
`parse_frame_header` path. CoK uses the special 4-plane strip format above.

### 7d. Block assignments (CoK)

| Block | N tiles | Purpose |
|-------|---------|---------|
| 202 | 38 | Exploration frame tiles (20-25), compass arrows (0-3), moons (26-37) |
| 203 | 45 | Alternate decoration tile set |
| 11,12,23,31,32,51,52,7,81,82 | 70 | Dungeon wall art sets |

Block 201 decompresses to all-zeros — skip or treat as null/meta block.

### 7e. Moon tiles (block 202, tiles 26-37)

Three moon groups (4 phases each):
- Tiles 26-29: Lunitari. Phase 3 (full disc, tile 28) = EGA index 4 = #aa0000 (red).
- Tiles 30-33: Solinari. Phase 3 (full disc, tile 32) = EGA index 15 = #ffffff (white).
- Tiles 34-37: Nuitari. Phase 3 (full disc, tile 36) = EGA index 1 = #0000aa (blue).

All three moon groups produce identical inter-phase XOR patterns
`[6, 26, 6, 20, 12, 20]`, confirming structurally correct decode.

---

## 8. Verified vs assumed vs unsolved

**VERIFIED (against raw bytes and cross-checks):**
- Big-endian container: `word0 = 2 + N*9 = data_base`; 9-byte BE TOC
  `{u8 id, u32 off, u16 raw, u16 comp}`; offsets chain; sparse ids.
- Signed-byte RLE (identical to DAX; output == raw_size).
- Frame header `{u16 h, u16 w/8, u16 y, u16 x}` BE; `width = w/8 * 8`.
- 5 contiguous bitplanes, word-aligned row stride, LSB = plane 0.
- Per-frame embedded 32-color palette: 32 × BE uint16 0x0RGB at offset 9, x17.
- IFF ILBM reader + the 32-color Amiga palette (matches `.DAA` palette).
- Cross-validation: CoK BIGPIC1 b114 == DOS BIGPIC1.DAX b114 (same scene/size).
- **CoK `8X8D*` tile strip format: 4-plane contiguous, row_bytes=N, MSB-first,
  plane 0=LSB, shared EGA-16 palette. Decoder verified; moon colors confirmed.**

**ASSUMED (renders correctly, not independently proven):**
- The 1-byte pad/flag at offset 8 (always observed 0x01) in standard frames.
- Color index 0 = transparency on sprites (renders as black background).
- The EGA-16 palette is loaded by the Amiga game from DUNGCOM at startup and applied
  globally to all 8X8D rendering (consistent with observation but not traced in binary).

**UNSOLVED:**
- The 6-byte sub-frame **index** files (`SPRIT*`, DoK `PIC*`/`PIC2`, big `HEAD*`):
  TOC is decoded, inner sub-frame pixel encoding is not (likely a transparent /
  multi-sub-frame variant — the same class left unsolved in DOS DAX).
- Block 201 in `8X8D1.DAA`: decompresses to all-zeros, structure unknown.
- Whether `comspr.daa`/`COMSPR.DAA` (ambiguous: divisible by both 9 and 6) are
  block or index files — the chain-validation heuristic decides, but they were
  not visually confirmed.

---

## 9. Using the decoder

```
python tools/daa_decode.py <file.DAA> [--out DIR] [--scale N] [--list]
python tools/daa_decode.py <file.lbm> [--lbm] [--out DIR]
```

Writes one PNG per renderable frame to `renders/daa/<...>/<stem>_<block_id>.png`.
9-byte-TOC image files render (standard frames AND `8X8D` tile strips); 6-byte-index
files are listed only. `--list` inspects without rendering. Pillow is used if
present, else a built-in PNG writer.
