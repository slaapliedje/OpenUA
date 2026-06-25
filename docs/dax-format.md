# SSI Gold Box DOS `.DAX` Graphics Format

Reverse-engineered and verified empirically against Champions of Krynn (CoK) and
Death Knights of Krynn (DoK) DOS files in this repo, and cross-checked
byte-for-byte against `daxdump_extracted/DaxDump.exe` (the COAB DaxDump tool,
which produces decompressed block dumps).

Reference decoder: `tools/dax_decode.py`. Sample PNGs: `renders/dax/`.

All multi-byte integers are **little-endian**.

---

## 1. Container layout  — VERIFIED

```
offset  size  field
0       2     toc_bytes      = entry_count * 9   (TOC size in bytes)
2       9*N   TOC: N = toc_bytes/9 entries, each 9 bytes:
                +0  uint8   block_id      sparse id (NOT a 0..N-1 index)
                +1  uint32  data_offset   relative to data section base
                +5  uint16  raw_size      decompressed size (bytes)
                +7  uint16  comp_size     compressed size (bytes)
2+toc   ...   data section (concatenated RLE-compressed blocks)
```

- `data_base = 2 + toc_bytes`.
- Block *k* compressed bytes = `file[data_base + entry.data_offset : + entry.comp_size]`.
- `data_offset` values chain perfectly: `entry[i+1].data_offset == entry[i].data_offset + entry[i].comp_size`.
- **`block_id` is sparse** (e.g. CHEAD ids run 0–13, 64–77, 128–141, 192–205).
  DaxDump labels its output files `NAME_<block_id>.bin`, confirming this.

> Note: the old `tools/dax_inspect.py` mislabels word0 as `entry_count` and word1
> as `table_offset`. In reality **word0 = entry_count × 9**, and the bytes it read
> as "word1" are `block_id` (1 byte) + the low byte of entry 0's `data_offset`.
> `entry_count = word0 / 9`.

Worked example — `Champions of Krynn/8X8D1.DAX`:
`word0 = 0x0048 = 72` ⇒ `entry_count = 8`, `data_base = 2 + 72 = 74`.
Entry 0 = `{block_id=201, data_offset=0, raw=1416, comp=1341}`.

---

## 2. RLE decompression  — VERIFIED (byte-identical to DaxDump)

Signed-byte run length (the classic SSI / PCX-family scheme):

```
i = 0
while i < len(comp):
    c = comp[i]; i += 1
    if c < 0x80:                       # literal run
        copy the next (c + 1) bytes verbatim;  i += (c + 1)
    else:                              # repeat run
        emit the next byte (256 - c) times;    i += 1
```

Output length equals the entry's `raw_size` exactly. Verified by decompressing
every block of CHEAD/8X8D1/BIGPIC1/TITLE and comparing to `DaxDump.exe`'s
`*.bin` dumps — **identical**.

(Equivalently with signed arithmetic: `s = c if c < 128 else c-256`;
`s >= 0` → `s+1` literals; `s < 0` → `-s` repeats.)

---

## 3. Pixel encoding  — VERIFIED

All graphics blocks store pixels as **chunky 4 bits-per-pixel, 2 pixels per byte,
HIGH nibble = left pixel**. Each nibble is an index 0–15 into the fixed EGA
palette (§5). There are **no bitplanes** — it is *not* EGA-planar; it is packed
chunky nibbles. (Most bytes have values > 15, which is what proves 2 px/byte
rather than 1 byte/pixel.)

Bytes per row = `width / 2`.

---

## 4. Frame headers

### 4a. Standard rectangular frame — VERIFIED (17-byte header)
Used by TITLE, BIGPIC, BACK (combat backdrops), CHEAD (combat heads), WALLDEF-style
full rectangles, etc. The header is **17 bytes**, confirmed against the COAB clean-room
decompile (`coab_extracted/Classes/DaxFiles/DaxBlock.cs`):

```
offset  size  field
0       2     height       (pixels)
2       2     width_div8   width in pixels = width_div8 * 8
4       2     x_pos        (placement; usually 0)
6       2     y_pos        (placement; usually 0)
8       1     item_count   number of same-size items (1 = single frame; ≥2 = tile array)
9       8     field_9      EGA-plane / editor metadata (ignored for render)
17      ...   pixel data: item_count * height * (width/2) bytes, chunky 4bpp (§3)
```

**Pixels start at byte 17, not 8.** An earlier 8-byte assumption read every frame 9
bytes early, cyclically rolling it RIGHT by 18 px (rightmost 18 columns wrapped onto
the left margin — subtle on 304-px BIGPIC scenes, badly garbling 24-px portraits).
Verified across every CoK/DoK graphics DAX: all 734 single-frame blocks have
`item_count == 1` and `17 + width*height/2 == rawSize` **exactly** (no trailing bytes),
and the corrected EGA BIGPIC render is column-aligned with the Amiga DAA decode of the
same scene. The "standard frame" is simply the `item_count == 1` case of the same block
layout the multi-item tile arrays (DUNGCOM / 8X8D) use. Verified examples:

| File / block        | header (h, w/8) | width×height | renders as |
|---------------------|-----------------|--------------|------------|
| TITLE_001 (CoK)     | (200, 40)       | 320×200      | SSI/AD&D title screen (legible text) |
| TITLE_004           | (88, 40)        | 320×88       | title art strip |
| BIGPIC1_112 (CoK)   | (120, 38)       | 304×120      | outdoor scene (trees, cabin) |
| BACK1_001 (DoK)     | (88, 11)        | 88×88        | combat backdrop (mountains) |
| CHEAD_000 (CoK)     | (10, 3)         | 24×10        | combat head icon |

`width = width_div8 * 8` was confirmed across all of the above (e.g. big-pic
width 304 also matches the hackdoc `DRAW18.TXT`, which states a big-pic row is 304
pixels wide).

#### Horizontal-roll quirk (TITLE block 1) — handled on decode
A few full-screen frames are stored **horizontally cyclic-rolled**: every row is
rotated so the leftmost ~N columns actually belong on the right edge (the on-disk
SSI boot screen `TITLE.DAX` block 1 is rolled **+13 px** — its left margin is the
true right margin). Decompression is byte-identical to `DaxDump.exe`; only the
framing is shifted, so a dumb dumper shows the same wrapped image. The decoders
(`tools/dax_decode.py` `_deroll_indices`, engine `derollFrameIndices` in
`packages/engine/src/loaders/dax.ts`) un-roll it with a conservative, data-driven
detector: for full-screen frames (≥64×64) only, find the shift `k` (≤ min(40, w/4))
that best makes column 0 match column w-1 across all rows; apply it **only** when
the unshifted borders are clearly torn (miss₀ ≥ max(8, 10% of height)) and some
`k>0` makes them essentially seamless (≤2 mismatched rows, and ≥8× better than
miss₀). This fires on exactly 1 of 26 CoK frames (TITLE block 1) and leaves
full-bleed art untouched. Synthetic coverage: `packages/engine/test/dax-deroll.test.ts`.

### 4b. 8×8 dungeon-tile strips — VERIFIED (rendered), header partly assumed
`8X8Dn.DAX` blocks have header `{height=8, width_div8=1, ...}` but the payload is a
tall **vertical strip of stacked 8×8 tiles laid out 16 px wide** (two tiles per
row), not a single 8×8 image. Payload length is always `8*k + 1` bytes (one
leading/extra byte), so: `width = 16`, `height = (raw_size-8) // 8`. Rendered
output is coherent dungeon wall / door art, so the 16-px-wide layout is correct;
the exact meaning of the `+1` byte and the per-tile grouping is **assumed**.

### 4c. Complex / transparent frames — UNSOLVED
Most `PIC*.DAX` and `SPRIT*.DAX` blocks (and some `WALLDEF*`) use a different,
richer header: word1 (`width_div8`) is **0** and extra header words are non-zero
(e.g. SPRIT1 `{3,0,0x5000,0x0700,…}`, PIC1 `{n,0,0x5800,0x0b00,…}` where
`0x58 = 88`). These are a transparency / multi-sub-frame variant analogous to UA
"drawing method 23" (`DRAW23.TXT`). The decoder **detects and skips** these rather
than guessing. Decoding them is the main remaining work item.

---

## 5. EGA palette  — the 16 fixed colors used for rendering

CoK/DoK DOS are EGA 16-color and do **not** store a palette in the file. The
decoder uses the standard IBM EGA default palette (with the corrected brown at
index 6). RGB 0–255:

| idx | RGB           | name        | idx | RGB           | name          |
|-----|---------------|-------------|-----|---------------|---------------|
| 0   | 00 00 00      | black       | 8   | 55 55 55      | dark gray     |
| 1   | 00 00 AA      | blue        | 9   | 55 55 FF      | bright blue   |
| 2   | 00 AA 00      | green       | 10  | 55 FF 55      | bright green  |
| 3   | 00 AA AA      | cyan        | 11  | 55 FF FF      | bright cyan   |
| 4   | AA 00 00      | red         | 12  | FF 55 55      | bright red    |
| 5   | AA 00 AA      | magenta     | 13  | FF 55 FF      | bright magenta|
| 6   | AA 55 00      | brown       | 14  | FF FF 55      | yellow        |
| 7   | AA AA AA      | light gray  | 15  | FF FF FF      | white         |

The title screen and scene renders come out with correct, expected colors
(cyan sky, green foliage, brown/red structures, white text), which corroborates
this palette and the high-nibble-first nibble order.

---

## 6. Verified vs assumed vs unsolved

**VERIFIED (against raw bytes and/or DaxDump.exe):**
- Container header (`word0 = entry_count×9`), 9-byte TOC entry layout
  `{u8 block_id, u32 data_offset, u16 raw_size, u16 comp_size}`, sparse block ids.
- Signed-byte RLE algorithm (byte-identical output to DaxDump).
- Chunky 4bpp, 2 px/byte, high-nibble-first pixel packing.
- Standard frame **17-byte** header `{u16 height, u16 width_div8, u16 x, u16 y,
  u8 item_count, u8[8] field_9}`, pixels at offset 17; `width = width_div8 * 8`.
  Confirmed against the COAB `DaxBlock.cs` oracle and on TITLE, BIGPIC, BACK, CHEAD
  across CoK+DoK (734/734 single frames: `item_count==1`, exact `17 + w*h/2` fit), with
  the corrected EGA BIGPIC column-aligned to the Amiga DAA decode.
- EGA palette / nibble order (colors render correctly).

**ASSUMED (renders correctly but not fully proven):**
- 8×8 tile strips are 16 px wide with a single leading pad byte; exact tile
  grouping and the meaning of `y_off`/`x_off` placement words.
- The few trailing pad bytes after standard-frame pixel data.

**UNSOLVED:**
- The "complex" transparent frame variant used by most `PIC*`/`SPRIT*` blocks
  (header with `width_div8 == 0` and extra words). Likely a method-23-style
  transparent / sub-framed encoding; needs its own reversing pass.
- Whether any block carries an explicit per-file palette override (none seen in
  CoK/DoK; expected, since these are fixed-palette EGA).

---

## 7. Using the decoder

```
python tools/dax_decode.py <file.dax> [--out DIR] [--scale N] [--list]
```

Lists every block (id, raw/comp sizes, detected kind) and writes one PNG per
renderable frame to `renders/dax/<stem>/<stem>_<block_id>.png` (or `--out DIR`).
`--list` inspects without rendering. Pillow is used if present; otherwise a
built-in raw-PNG writer is used.
