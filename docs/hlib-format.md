# HLIB / DataLib container — The Dark Queen of Krynn (DOS VGA) `.TLB` / `.GLB`

Verified spec for the DQK graphics container. Cross-checked against the community
DataLib/FRUA docs in `hackdocs_extracted/` (`TLBFORM.TXT`, `DRAW18.TXT`,
`DRAW23.TXT`, `UAPALETT.TXT`, `FRM_DESC.TXT`) **and** against the real game bytes,
with renders confirmed as legible VGA art (title screens, overland maps, encounter
pics, sprites). Decoder: `tools/hlib_decode.py`.

DQK is **VGA 256-color, 8 bits-per-pixel** (1 byte = 1 palette index). This is a
different lineage from the DAX (DOS EGA) and DAA (Amiga planar) formats — see the
contrast table at the bottom.

---

## 1. Container header (little-endian) — VERIFIED

```
off 0   ASCII  "HLIB"
off 4   DWORD  file size (the trailing EOF pointer equals this)
off 8   WORD   pointer count - 1   (the stored value; the real count INCLUDES the
               trailing EOF pointer, so total DWORDs in the list = stored+1)
off 10  BYTE   unused (0)
off 11  BYTE   magic: 0 = no image-id table, 1 = image-id/set-id table present
off 12  ASCII  tag:  "TILE" => leaf tile library
                     "HLIB" => MASTER library (collection of leaf TILE sub-files)
off 16  DWORD[count]  pointer list; the LAST entry == file size (EOF sentinel)
```

The byte at offset 8 being `count-1` is the one true gotcha: e.g. ALWAYS.TLB stores
`30` → 31 DWORDs, of which `ptr[30]` == file size. Verified that `ptr[-1] == fsize`
for every file tested.

### 1a. LEAF library (tag `"TILE"`)
Pointers locate, in order:
- `magic == 1`: `ptr[0]` → image-id table, `ptr[1]` → colour table, `ptr[2..]` → images.
- `magic == 0`: `ptr[0]` → colour table (**or** the first image, for `cpic` combat
  icons / `TOPVIEW` which store **no** palette — detect by reading the colour-table
  header: `ncolors == 0` means there is no palette), `ptr[1..]` → images.

Each image spans `[ptr[i], ptr[i+1])`; the final image ends at the EOF pointer.

### 1b. MASTER library (tag `"HLIB"` at offset 12)
The file is a pack of complete leaf TILE files ("sets").
- `magic` is always 1.
- `ptr[0]` → set-id table, `ptr[1..]` → sets, last pointer = EOF.
- Each **set** begins with its own full `HLIB`/`TILE` leaf header, and **its pointers
  are relative to the start of that set** (so a set is self-contained / extractable).
- Masters: TITLE, BIGPIC, CPIC(no—CPIC is a leaf), BACK, SPRIT, 8X8DB/8X8DC, PICA/B/C,
  DUNGCOM, WILDCOM, FRAME?(leaf). (CPIC, FRAME, GEN, COMSPR, ALWAYS, TOPVIEW are leaves.)

### 1c. Image-id / set-id table (only when `magic == 1`)
```
WORD  number of entries (== number of images / sets)
then per entry:  WORD image/set ID#,  WORD pointer-# (1-based into the pointer list)
```
Used by the engine to address images by logical ID. Not needed for rendering; the
decoder ignores it and walks pointers positionally.

---

## 2. Colour table (palette) — VERIFIED, **8-bit RGB (NOT 6-bit DAC)**

8-byte header followed by RGB triplets:
```
WORD  cycle      (< 3 => no colour cycling)
WORD  first_col  (palette index of the first stored colour)
WORD  ncolors    (0..256; 0 => no palette stored in this leaf)
BYTE  nranges    (number of colour-cycling ranges)
BYTE  cmagic     (observed 8 or 24)
then ncolors * 3 bytes:  R, G, B  — each a FULL 8-bit value 0..255
then nranges * 4 bytes of cycle data {dir, speed, start, count}  (skipped by decoder)
```

**Palette values are already 8-bit (0..255); do NOT scale by `<<2`.** Verified:
ALWAYS.TLB palette begins `00 00 00 / 00 00 AB / 00 AB 00 / 00 AB AB …` (0xAB = 171),
the classic full-range EGA-derived VGA colours; max byte observed = 255. (The prompt's
"6-bit DAC, scale ×4" assumption is **false** for HLIB — corrected here.)

The first stored colour maps to index `first_col` (e.g. BACK leaves store `first_col=144,
ncolors=32` → only palette slots 144..175 are defined; the rest default black). This
matches `UAPALETT.TXT`: ALWAYS=0-15, FRAME/GEN=16-31, walls=32-68, combat sprites=64-95,
backdrops=144-175, sprites≈176-255, smallpics/bigpics/overland=32-255. Because each
sub-image only defines its own slice, a fully-correct in-game render layers several
palettes; standalone a backdrop looks dim/near-grayscale (its 144-175 ramp), which is
**correct data**, not a decode error.

`cpic`/combat-icon leaves and `TOPVIEW` carry **no** palette (`ncolors==0`): in-engine
they reuse the currently-loaded DUNGCOM/WILDCOM palette.

---

## 3. Image header (8 bytes) — VERIFIED

```
WORD  height
WORD  vertical offset    (signed placement hint; ignored for rendering)
WORD  horizontal offset  (signed placement hint; ignored for rendering)
BYTE  width / 4          (actual width in pixels = this * 4; width always %4==0)
BYTE  drawing method
```

---

## 4. Pixel layout: Chain-4 interleave — VERIFIED

Pixels are stored **Chain-4 / interlaced 1/4-row at a time**. Within a row the column
visit order is 4 sweeps:
```
sweep 0: columns 0, 4, 8, ...      sweep 2: columns 2, 6, 10, ...
sweep 1: columns 1, 5, 9, ...      sweep 3: columns 3, 7, 11, ...
```
Each sweep covers `q = width/4` columns. The decoder builds this column order and
fills it; getting this wrong produces vertical-stripe garbage.

## 5. Drawing methods — VERIFIED (16, 17, 18, 21, 23)

| # | Description | Encoding |
|---|-------------|----------|
| 16 | uncompressed, opaque | `width*height` bytes, Chain-4 order |
| 17 | uncompressed, transparent (AND-mask + OR-mask) | two `width*height` blocks; OR-mask block is the visible pixels (decoder uses it) |
| 18 | **compressed, opaque** (the workhorse: BIGPIC, PICA backdrops, title backdrops) | per **row**; control `c<128` → copy `c+1` literals; `c>=128` → repeat next byte `257-c` times. Runs span the row's 4 sweeps continuously (one stream per row of `width` pixels). |
| 21 | uncompressed, transparent | like 16; index 255 = transparent |
| 23 | **compressed, transparent** (sprites, title text/logo overlays) | 4 image sweeps; per row: `c==0` ends the row; `c>=128` → skip `256-c` transparent pixels; `c<128` → copy `c` literal pixels. |
| 25 | image-id list (rare) | **unsolved** — skipped by decoder |

### Verified compression-count details (these differ subtly from the docs)
- **Method 18 repeat count is `257 - c`** (matches `DRAW18.TXT`), and runs are a single
  continuous stream over the full Chain-4 row order of `width` pixels. Verified by
  rendering BIGPIC as coherent overland maps and the title backdrops as smooth gradients
  / detailed stone.
- **Method 23 skip count is `256 - c`, NOT the `257 - c` printed in `DRAW23.TXT`.**
  Empirically `257-c` overshoots the row width (columns ran to 80 on a 62-wide image);
  `256-c` caps exactly at `q` and makes the title copyright text and "MicroMagic"/SSI
  logos render perfectly legibly. Method 23 is organised as 4 *image* sweeps (outer:
  sweep, inner: rows), each row terminated by a `0` control byte; literal copies of
  `c` bytes, transparent skips of `256-c`.

Transparent index is **255** (methods 17/21/23 leave it untouched; decoder emits 255 as
the key colour where a backdrop would show through in-game).

---

## 6. `.GLB` master libraries (non-image)
`SOUNDS.GLB`, `ECL.GLB`, `GEO.GLB`, `MONCHA.GLB` use the **same** `HLIB` container and
pointer-table directory, but their member chunks are tagged `DIG4` (digitised sound),
`DATA` (ECL scripts / GEO maps / generic) rather than `TILE`. The container walk is
identical; only the member payloads differ. Image rendering does not apply. (Member
payload decoding for DIG4/DATA is out of scope here — **unsolved/not attempted**.)

---

## 7. HLIB vs DAX vs DAA — cross-format contrast (for the asset library)

| Aspect | **DAX** (DOS CoK/DoK) | **DAA** (Amiga CoK/DoK) | **HLIB** (DOS DQK) |
|--------|----------------------|-------------------------|--------------------|
| Magic | none (headerless) | none (`word0` only) | ASCII `"HLIB"` |
| Endianness | little-endian | **big-endian** (68000) | little-endian |
| Directory | sized TOC `{u8 id, u32 off, u16 raw, u16 comp}` | same TOC, big-endian | **pointer table** (DWORD offsets only; sizes implied by next pointer) |
| Nesting | flat | flat (or 6-byte sub-index) | **nested**: MASTER `HLIB` packs self-contained leaf `TILE` files |
| Compression | signed-byte RLE (`c<0x80` copy c+1; else repeat 256-c) | same RLE | **per-row PCX-style** (method 18) / transparent RLE (method 23); also raw (16/17/21) |
| Pixel depth | **4bpp** chunky (2 px/byte, hi nibble left) | **5-plane planar** (32 colours) | **8bpp** chunky (1 byte = 1 index) |
| Pixel order | linear rows | contiguous bitplanes | **Chain-4 interleave** (1/4 row per sweep) |
| Palette | none — fixed 16-colour EGA | 32× 12-bit Amiga RGB in each frame | colour table per leaf, **256× 8-bit RGB**, with `first_col`/`ncolors` slicing |
| Palette scaling | n/a | nibble ×17 → 8-bit | **none — already 8-bit** |
| Frame header | `{u16 h, u16 w/8, u16 yoff, u16 xoff}` | same + pad + 64B palette | `{u16 h, u16 yoff, u16 xoff, u8 w/4, u8 method}` |

Same-named assets across formats: `BIGPIC`, `CPIC`, `BACK`, `8X8D*`, `SPRIT`, `PIC*`
appear in all three games — useful for visual cross-checking decoders.

---

## 8. Verified / Assumed / Unsolved

**Verified (rendered as recognizable VGA art):**
- Container header, pointer-count-minus-1 quirk, EOF==filesize.
- Leaf vs master walk; nested self-contained sets with relative pointers.
- Colour table layout; 8-bit RGB; `first_col`/`ncolors` slicing; no DAC scaling.
- Image header; Chain-4 interleave; drawing methods 16, 17, 18, 21, 23.
- Method-18 `257-c` repeat (continuous per-row stream) and method-23 `256-c` skip
  (4 image sweeps, `0`-terminated rows) — both confirmed against legible text/art.
- Files confirmed as real art: TITLE (backdrops + MicroMagic/SSI/TSR title overlays),
  BIGPIC (Krynn overland maps), PICA (encounter pics: giant spider, robed figure),
  SPRIT, ALWAYS (UI borders), 8X8DB, DUNGCOM/WILDCOM tiles, BACK (dim backdrop textures).

**Assumed (plausible, not exhaustively proven):**
- Method 17's visible plane is the OR-mask (AND-mask used for transparency in-engine).
- Vertical/horizontal offsets are signed placement hints (not used for raster output).
- Colour-cycling range data layout (read but unused).

**Unsolved:**
- Drawing method **25** (image-id list) — rare; skipped.
- `.GLB` `DIG4` (sound) and `DATA` (ECL/GEO) member payload formats — container walk
  works, payload decode not attempted (out of scope for image rendering).
- Exact in-engine multi-palette layering (which leaf's palette wins per screen) — known
  qualitatively from `UAPALETT.TXT` but not reproduced; standalone renders use each
  leaf's own partial palette.
