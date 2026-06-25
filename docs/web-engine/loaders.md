# Loaders — Porting the Decoders to TypeScript

This is **layer 2**. Each loader is a direct port of a verified Python decoder
([`../../tools/dax_decode.py`](../../tools/dax_decode.py),
[`daa_decode.py`](../../tools/daa_decode.py),
[`hlib_decode.py`](../../tools/hlib_decode.py)) and produces the
[asset-model.md](./asset-model.md) types. Format facts come from
[`../dax-format.md`](../dax-format.md), [`../daa-format.md`](../daa-format.md),
[`../hlib-format.md`](../hlib-format.md) — those are the source of truth; do not deviate.

## Byte-access conventions (all loaders)

Work over `Uint8Array`; read multibyte fields via a `DataView` so endianness is explicit per
format (DAX/HLIB little-endian, DAA big-endian):

```ts
const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
dv.getUint16(off, true);   // DAX / HLIB  (littleEndian = true)
dv.getUint16(off, false);  // DAA         (littleEndian = false / big-endian)
dv.getUint32(off, le);
```

The signed-byte RLE is **shared and byte-order-independent** across DAX and DAA — write it
once:

```ts
/** SSI signed-byte RLE. c<0x80 → copy c+1 literals; else repeat next byte (256-c) times. */
export function rleDecode(src: Uint8Array, expected?: number): Uint8Array {
  const out: number[] = [];
  let i = 0;
  while (i < src.length) {
    if (expected !== undefined && out.length >= expected) break;
    const c = src[i++];
    if (c < 0x80) { const n = c + 1; for (let k = 0; k < n; k++) out.push(src[i++]); }
    else { const n = 256 - c; const v = src[i++]; for (let k = 0; k < n; k++) out.push(v); }
  }
  return Uint8Array.from(out);
}
```
(Preallocate a `Uint8Array(expected)` with a write cursor in real code; the array form is
shown for clarity.)

---

## 1. DAX loader (DOS CoK / DoK — EGA, little-endian, 4bpp chunky)

Port of `dax_decode.py`. **No palette in file → use the fixed EGA-16 table** (see
[`palettes-and-rendering.md`](./palettes-and-rendering.md)).

### Container parse (verified)
```
word0 (u16 LE)  = entry_count * 9          // toc_bytes
n               = word0 / 9
data_base       = 2 + word0
TOC[i] at 2 + i*9, 9 bytes LE:
  +0 u8  block_id     (sparse id, NOT 0..n-1)
  +1 u32 data_offset  (relative to data_base)
  +5 u16 raw_size     (decompressed)
  +7 u16 comp_size    (compressed)
block bytes = data[data_base+data_offset : +comp_size]
```
Reject the file if `word0 === 0 || word0 % 9 !== 0`. Decompress each block with `rleDecode(comp, raw_size)`.

### Frame decode (verified)
Standard rectangular frame header (8 bytes LE):
```
+0 u16 height
+2 u16 width_div8     → width = width_div8 * 8
+4 u16 y_offset
+6 u16 x_offset
+8 .. pixels: height * (width/2) bytes, chunky 4bpp, 2 px/byte, HIGH nibble = LEFT pixel
```
Accept as a standard frame iff `width_div8 !== 0 && height !== 0` and
`payload >= width*height/2` with `payload - need <= 16` (trailing pad bytes ignored).

Pixel expand → `IndexedImage.indices` (each nibble is a 0..15 EGA index):
```ts
for (let y = 0; y < height; y++)
  for (let xb = 0; xb < width / 2; xb++) {
    const b = pix[y * (width / 2) + xb];
    indices[y * width + 2 * xb]     = b >> 4;     // high nibble = left
    indices[y * width + 2 * xb + 1] = b & 0x0F;
  }
```

### 8×8 tile strips (ASSUMED layout — renders coherently)
Header `{height=8, width_div8=1}`: treat payload as a 16-px-wide vertical strip,
`height = (raw_size - 8) / 8` (one leading/extra byte; meaning of the +1 unknown). Split into
8×8 cells → `TileSet`. **Plane/tile grouping is assumed**, not proven — flag in UI.

### Graceful degradation
Blocks with `width_div8 === 0` plus extra nonzero header words are the **complex / transparent
sub-frame variant (UNSOLVED)** — most `PIC*`, `SPRIT*`, some `WALLDEF*`. The Python decoder
*detects and skips* them. The TS loader must do the same: emit nothing for that block, mark it
`kind:'unknown'` / log `encoding:'dax-complex-unsolved'`, never guess. Continue with the rest
of the container.

---

## 2. DAA loader (Amiga CoK / DoK — 32-color, **big-endian**, 5-plane planar)

Port of `daa_decode.py`. The structural cousin of DAX, but **big-endian (68000)**,
**bitplanar**, and **palette-carrying per frame**.

### Container parse (verified) — note the data_base contrast with DAX
```
word0 (u16 BE) = 2 + entry_count*9   AND  word0 == data_base   (it INCLUDES the 2-byte header)
n              = (word0 - 2) / 9
TOC[i] at 2 + i*9, 9 bytes BE: {u8 id, u32 off, u16 raw, u16 comp}
```
**Disambiguation heuristic (port exactly):** try the 9-byte TOC; accept it only if every
`data_offset` chains (`off[i+1] === off[i] + comp[i]`, starting 0) AND the last block ends
within 4 bytes of EOF. Otherwise fall back to the **6-byte sub-frame index**.

### Image frame (verified) — BE header + embedded palette + planar pixels
```
+0  u16 height (BE)
+2  u16 width_div8 (BE)   → width = width_div8 * 8
+4  u16 y_offset (BE)
+6  u16 x_offset (BE)
+8  u8  pad/flag          (observed 0x01)
+9  64  palette           32 × u16 BE, 0x0RGB 12-bit; channel*17 → 8-bit
+73 ..  pixels: 5 CONTIGUOUS bitplanes (NOT ILBM per-row interleave)
```
Row stride is **word-aligned**: `rowBytes = ceil(width/16)*2`. `planeSize = rowBytes*height`.
Plane p occupies `[p*planeSize, (p+1)*planeSize)`. Combine (LSB = plane 0):
```ts
const rowBytes = (((width + 15) >> 4) << 1), planeSize = rowBytes * height;
for (let y = 0; y < height; y++)
  for (let x = 0; x < width; x++) {
    const byteI = y * rowBytes + (x >> 3), bit = 7 - (x & 7);
    let idx = 0;
    for (let p = 0; p < 5; p++) idx |= ((pix[p * planeSize + byteI] >> bit) & 1) << p;
    indices[y * width + x] = idx;
  }
```
Palette: 32 × `u16 BE`; `r=((v>>8)&0xF)*17`, `g=((v>>4)&0xF)*17`, `b=(v&0xF)*17`. Stored
identically in every block of a file (per-file palette duplicated per frame). On sprites,
**index 0 = transparent** (set `transparentIndex: 0` for sprite kinds).

### 8×8 tiles (`8X8D*`) — PARTIALLY SOLVED
Header `{8,1,0,0}` + 1 count byte, then `count` tiles of **4 planes × 1 byte/row × 8 rows =
32 bytes/tile** (count = `(raw-9)/32`, verified). **No embedded palette** (shared/global,
probably from `globals.daa`). Produce correctly-sized cells; **within-tile plane order and
the correct shared palette are UNSOLVED** — colors may be wrong; flag it.

### Graceful degradation
The **6-byte sub-frame index** files (`SPRIT*`, DoK `PIC*`/`PIC2`, big `HEAD*`): parse the
index `{u8 id, u8 0, u32 sub_offset BE}` (each sub-frame length = next.offset − this.offset)
and expose it, but the **inner pixel encoding is UNSOLVED** — do not render, mark
`encoding:'daa-subframe-unsolved'`. `pointers.daa`/`globals.daa` have `word0 === 0` → not
graphics containers; skip. `comspr.daa` is ambiguous (divisible by 9 and 6) — the
chain-validation heuristic decides; treat its render as unverified.

---

## 3. HLIB loader (DOS DQK — VGA 256-color, little-endian, Chain-4, drawing methods)

Port of `hlib_decode.py`. The richest format and the **neutral model's natural shape**.

### Container header (verified)
```
+0  "HLIB"
+4  u32 file_size           (== last pointer; EOF sentinel)
+8  u16 pointer_count - 1   (THE gotcha: real count = stored + 1, includes EOF ptr)
+10 u8  unused
+11 u8  magic               (0 = no image-id table, 1 = present)
+12 "TILE"  → leaf tile library     |   "HLIB" → MASTER (pack of leaf sets)
+16 u32[count] pointers     (last == file_size)
```

### Leaf walk
- `magic==1`: `ptr[0]`→image-id table, `ptr[1]`→colour table, `ptr[2..]`→images.
- `magic==0`: `ptr[0]`→colour table **or** first image (cpic/TOPVIEW carry no palette →
  detect by reading the colour-table header: `ncolors==0` ⇒ no palette ⇒ `ptr[0]` is the
  first image). `ptr[1..]`→images.
- Image i spans `[ptr[i], ptr[i+1])`; last ends at EOF pointer.

### Master walk (tag `"HLIB"` at +12)
`ptr[0]`→set-id table, `ptr[1..]`→sets. **Each set is a self-contained leaf HLIB/TILE whose
own pointers are relative to the set's start** — recurse `readHeader(data, setBase)` and make
pointers absolute by adding `setBase`. (BIGPIC, TITLE, 8X8DB/C, SPRIT, BACK, PICA/B/C,
DUNGCOM, WILDCOM are masters; CPIC, FRAME, GEN, COMSPR, ALWAYS, TOPVIEW are leaves.)

### Colour table (verified) — **8-bit RGB, NOT 6-bit DAC**
```
u16 cycle      (<3 ⇒ no cycling)
u16 first_col  (palette index of first stored color)
u16 ncolors    (0..256; 0 ⇒ no palette here)
u8  nranges
u8  cmagic     (8 or 24)
ncolors × {u8 R, u8 G, u8 B}   — FULL 8-bit 0..255; DO NOT shift <<2
nranges × {u8 dir, speed, start, count}  — cycling bands → Palette.cycles
```
Fill `Palette.colors[first_col .. first_col+ncolors)`, leave the rest `null` (inherit). See
[`palettes-and-rendering.md`](./palettes-and-rendering.md) for layering.

### Image header (8 bytes, verified)
```
u16 height
u16 vertical offset    (signed placement hint)
u16 horizontal offset  (signed placement hint)
u8  width / 4          → width = w4 * 4  (always %4==0)
u8  drawing method
```

### Chain-4 column order
Within a row, columns are visited in 4 sweeps of `q = width/4`:
sweep s → columns `s, s+4, s+8, …`. Build once per width:
`order = [for s in 0..3 for i in 0..q) i*4 + s]` (length = width).

### Drawing-method dispatch (verified: 16, 17, 18, 21, 23)
Output buffer prefilled with **255** (transparent default).

| # | Decode |
| - | ------ |
| **16** | uncompressed opaque. `height` rows × 4 sweeps × `q` bytes; sweep s writes column `i*4+s`. |
| **17** | uncompressed transparent: two `w*h` blocks (AND mask, OR mask); use the **OR-mask** block, laid out like 16. |
| **18** | compressed opaque (workhorse: BIGPIC/PICA/title). Per **row**, one continuous stream over the full Chain-4 `order` of `width` pixels: `c<128`→copy `c+1` literals; `c>=128`→repeat next byte **`257-c`** times. |
| **21** | uncompressed transparent, layout like 16; index 255 = transparent. |
| **23** | compressed transparent (sprites, title overlays). **4 image sweeps** (outer s, inner rows); per row: `c==0` ends the row; `c>=128`→skip **`256-c`** transparent pixels; `c<128`→copy `c` literal pixels. |

> Two count details that differ from the hackdocs and are **verified empirically** — port
> these, not the docs: method **18 repeat = `257-c`** (matches DRAW18.TXT); method **23 skip =
> `256-c`** (DRAW23.TXT's `257-c` overshoots row width — `256-c` makes title text render
> legibly).

```ts
// method 18 core (per row y):
let col = 0;
while (col < width && pos < n) {
  const c = body[pos++];
  if (c < 128) { for (let k = 0; k <= c; k++) if (col < width) px[y*width + order[col++]] = body[pos++]; }
  else { const cnt = 257 - c, v = body[pos++]; for (let k = 0; k < cnt; k++) if (col < width) px[y*width + order[col++]] = v; }
}
```

### Graceful degradation
- Method **25** (image-id list): **UNSOLVED** — skip, mark `encoding:'hlib-method25-unsolved'`.
- `.GLB` libraries whose member tag is `DIG4` (sound) or `DATA` (ECL/GEO): the container walk
  is **identical**, but member payloads are out of scope for the image loader. The HLIB
  reader should still walk them and hand the raw member bytes to the relevant non-graphics
  loader (see [`game-data-and-ecl.md`](./game-data-and-ecl.md) and
  [`palettes-and-rendering.md`](./palettes-and-rendering.md) for DIG4).

---

## 4. IFF / ILBM (`.LBM`) loader — title screens (verified, in `daa_decode.py`)

Standard IFF, **big-endian**: `FORM…ILBM` → `BMHD` (w,h,nplanes,mask,compression) → `CMAP`
(already 8-bit RGB triplets) → `BODY`. Used for CoK/DoK title/text screens (`scrn*.lbm`,
`Title*.LBM`, `text*.lbm`, `gothic.lbm`, `char[ST].lbm`).

- BODY compression `1` = **ByteRun1**: `n<128`→copy `n+1` literals; `n>128`→repeat next byte
  `257-n` times; `n==128`→noop.
- ILBM uses **per-row plane interleave** (plane0 row, plane1 row, … then next scanline) — the
  **opposite** of DAA's contiguous planes. Account for the mask plane in stride if `mask==1`
  (`totalPlanes = nplanes + (mask===1?1:0)`).
- CMAP → `Palette{source:'iff-cmap'}` directly (no scaling). Produces a 320×200 `IndexedImage`.

```ts
const rowBytes = (((w + 15) >> 4) << 1), totalPlanes = nplanes + (mask === 1 ? 1 : 0);
for (let y = 0; y < h; y++) {
  const rbase = y * rowBytes * totalPlanes;
  for (let x = 0; x < w; x++) {
    const byteI = x >> 3, bit = 7 - (x & 7); let idx = 0;
    for (let p = 0; p < nplanes; p++) idx |= ((body[rbase + p*rowBytes + byteI] >> bit) & 1) << p;
    indices[y * w + x] = idx;
  }
}
```

---

## Loader contract & dispatch

Every loader implements:
```ts
interface ContainerLoader {
  /** Cheap magic/shape check so the manifest can auto-detect when needed. */
  sniff(bytes: Uint8Array): boolean;
  /** Full decode to neutral model; throws only on genuinely malformed input. */
  load(bytes: Uint8Array, ctx: LoadContext): FrameSet | TileSet | SpriteSet;
}
interface LoadContext {
  kind: AssetKind;            // from manifest/filename
  fixedPalette?: Palette;     // EGA-16 for DAX; global Amiga palette for 8X8D*
  endian?: 'little' | 'big';  // manifest hint; loaders also self-detect
}
```
Dispatch order for auto-sniff: HLIB (`"HLIB"` magic) → IFF (`"FORM…ILBM"`) → DAA (big-endian
`word0` chains) → DAX (little-endian `word0 % 9 === 0`). The manifest's `container`/`endian`
fields short-circuit this; sniffing is only the fallback.

**Golden-file rule:** for every (file, block) the Python decoder renders, the TS loader's
`IndexedImage` rendered through its palette must match the Python PNG byte-for-byte. Unsolved
variants are excluded from the golden set by design. See [`roadmap.md`](./roadmap.md).
