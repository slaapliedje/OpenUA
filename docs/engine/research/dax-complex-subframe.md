# DAX "complex / sub-frame" variant — CRACKED

Status: **CONFIRMED** for the two real cases (animated multi-frame pictures, and the
WALLDEF index tables). The original premise — that these blocks were an unsolved
*transparent-RLE / drawing-method-23* pixel encoding — turned out to be **wrong**.
There is no second RLE/transparency layer. Once the DAX container RLE is undone
(already verified in `tools/dax_decode.py`), the bytes are either:

1. a plain **multi-frame chunky-4bpp picture** (SPRIT*, PIC*, animated portraits, …), or
2. a **5×156 wall-symbol index table** (WALLDEF*) that is *not graphics at all*, or
3. a **fixed-size multi-item tile array** (DUNGCOM/WILDCOM/RANDCOM combat backdrops) —
   §D below; the last block that was still flagged "complex" for CoK.

Working decoder: **`tools/dax_complex_decode.py`** (standalone, pure Python, reuses
the verified container + RLE + palette from `tools/dax_decode.py`; does not modify it).
Renders verified visually in `renders/dax_complex/`.

Clean-room basis: reverse-engineered from the open-source COAB re-implementation
(`github.com/simeonpilgrim/coab`, files `engine/ovr030.cs`, `Classes/DaxFiles/DaxBlock.cs`,
`Classes/GeoBlock.cs`, `engine/ovr031.cs`), which targets the same older Gold Box engine
(Curse of the Azure Bonds), then **verified byte-exact and pixel-coherent against the
Champions of Krynn / Death Knights of Krynn DOS files in this repo**. DaxDump.exe was
also run: it only *decompresses* blocks to `.bin` (no rendering), so it is not a pixel
ground truth — visual coherence is the verification method, and it is unambiguous.

---

## A. Which "complex" blocks are which

`tools/dax_decode.py` flags a block "complex/unsupported" whenever the standard-frame
test fails (word1 `width_div8 == 0`, or payload ≠ width·height/2). Those blocks split
into two completely different things:

| File family                        | Real kind                         | Decoder path |
|------------------------------------|-----------------------------------|--------------|
| `SPRIT*.DAX`                       | animated multi-frame picture      | §B (no XOR)  |
| `PIC*.DAX` (multi-frame)           | animated portrait (talk/blink)    | §B (**XOR**) |
| `PIC*.DAX` (single-frame)          | static portrait, anim container   | §B           |
| `BODY*`, `COMSPR` (multi)          | animated picture                  | §B           |
| `WALLDEF*.DAX`                     | **wall-symbol index table** (not graphics) | §C |

`CHEAD.DAX`, `HEAD2.DAX` (and the other `HEAD*`) are **ordinary standard frames**
(24×10, 24×8, 88×40 …) already decoded correctly by `tools/dax_decode.py`. They are not
part of the complex variant despite the task brief listing "big HEAD*".

---

## B. Animated multi-frame picture format  — CONFIRMED

After RLE decompression, the block is:

```
offset 0 : uint8   numFrames
then repeated numFrames times (per-frame record):
  +0   uint32  delay        animation delay  (0 in CoK sprites/portraits)
  +4   uint16  height       pixels
  +6   uint16  width        in 8-pixel units;  pixel_width = width * 8
  +8   uint16  x_pos        placement X (screen tile col origin)
  +10  uint16  y_pos        placement Y          <-- then ONE pad byte is skipped
  +13  uint8[8] field_9     8 bytes, replicated identically across frames
  +21  ...     pixel data:  height * width * 4  bytes
               = (bpp/2), where bpp = height*width*8
               chunky 4bpp, 2 px/byte, HIGH nibble = LEFT pixel,
               each nibble = index 0..15 into the fixed EGA palette
               (same packing & palette as standard frames; see docs/dax-format.md §3/§5)
```

Per-frame header is **21 bytes** (`4+2+2+2+2 = 12` read, but y_pos read advances by 3, so
12+1 pad = 13, plus the 8-byte `field_9` = 21). Pixel bytes per frame = `height*width*4`.
The records are tightly packed and consume the block **exactly** (a strong checksum: a
correct parse lands on `offset == len(raw)`).

### B.1 XOR-delta animation (multi-frame portraits)  — CONFIRMED

For animated *picture/portrait* blocks (the COAB `is_pic_or_final` path), only **frame 0
is stored verbatim**. Every later frame's pixel bytes are **XORed against frame 0's pixel
bytes** before being unpacked:

```
frame0_pixels = raw bytes as-is
frameN_pixels[i] ^= frame0_pixels[i]      (for N >= 1, i over the pixel region)
```

This is delta animation: most of the portrait is unchanged between frames, so the XOR
stream is mostly zero and only the animated region (mouth/eyes) carries data.

- **SPRIT\*** monster sprites do **NOT** use XOR. Their (typically 3) frames are the three
  *view sizes* of the same monster (near / mid / far), each a full verbatim image of a
  different `width×height`, so an XOR delta is impossible by construction.
- **Multi-frame PIC\*** portraits **DO** use XOR. Proof: CoK `PIC1.DAX` block 12 frame 1
  is incoherent RGB noise without XOR and a clean horned-warlord portrait with XOR
  (`renders/dax_complex/PIC1_noxor/PIC1_012_f1.png` vs `…/PIC1_xor/PIC1_012_f1.png`).

Use `tools/dax_complex_decode.py --xor` for known pic/final animated files. (A robust
loader should key this off the file role, exactly as the engine does, rather than
auto-detect; single-frame blocks are unaffected by the flag.)

### B.2 Transparency

There is no in-stream transparency encoding. Transparency is handled at *blit* time: the
engine's masked draw treats one palette index as transparent (COAB `Recolor` →
`DaxToPicture`/`SetMaskedColor` map the mask color to a sentinel `16`). In the CoK sprite
renders the magenta (index 13 / `0x0D`) areas are the masked/transparent background.
The pixel encoding itself is plain chunky 4bpp.

### B.3 No "interlace / 4-sweep" here

The DRAW18/DRAW23 "advance by 4, four sweeps, plot-N / skip-(257−v)·4" interlace from the
hackdocs (`DRAW18.TXT`, `DRAW23.TXT`) is a **256-color UA/TLB** (Unlimited Adventures)
construct. It does **not** apply to these EGA CoK/DoK DAX blocks. The CoK pixels are
linear rows, high-nibble-first, no column interleave. (This retires the prior speculation
in `docs/dax-format.md §4c`.)

### B.4 Worked byte example — CoK `SPRIT1.DAX`, block id 1

Decompressed length = 4028 bytes. First bytes:
```
03 00 00 00 00  50 00 07 00  02 00 01 00  01 10 11 22 21 02 13 30 33  00 00 ...
```
Parse:
```
numFrames = 0x03 = 3

frame 0:
  delay  = 00 00 00 00        = 0
  height = 50 00              = 0x0050 = 80
  width  = 07 00              = 0x0007 = 7  -> 7*8 = 56 px
  x_pos  = 02 00              = 2
  y_pos  = 01 00 (+1 pad)     = 1
  field_9= 10 11 22 21 02 13 30 33
  pixels = height*width*4 = 80*7*4 = 2240 bytes  (a 56x80 chunky-4bpp image)

frame 1:  height 0x0041=65, width 4 -> 32 px, pixels 65*4*4 = 1040 bytes
frame 2:  height 0x0039=57, width 3 -> 24 px, pixels 57*3*4 =  684 bytes

total = 1 + 3*21 + (2240+1040+684) = 1 + 63 + 3964 = 4028  ✓ exact
```
Renders as an armored warrior with sword+shield at 56×80, then the same warrior at 32×65
and 24×57 (`renders/dax_complex/SPRIT1/SPRIT1_001_f{0,1,2}.png`). All five SPRIT1 blocks
parse to exactly their length; e.g. block 30 = a winged dragon at 72×80 / 48×65 / 24×57.

### B.5 Verified blocks

- CoK `SPRIT1.DAX`: ids 1, 8, 12, 30, 35 — all 3 frames, exact, coherent (warriors/dragon).
- CoK `PIC1.DAX`, `PIC2.DAX`: single- and multi-frame 88×88 portraits, exact; multi-frame
  ones coherent **only with XOR** (talking-head animations).
- DoK `PIC1.DAX`, `PIC2.DAX`: single- and multi-frame 88×88 portraits, exact; coherent.
- `COMSPR.DAX` blocks (305 bytes) are *not* this format — they fall through to standard.

---

## C. WALLDEF*.DAX — wall-symbol INDEX TABLE (not graphics)  — CONFIRMED

The first-person "wall" blocks are **layout tables**, not images. The engine
(`ovr031.LoadWalldef`, `Classes/GeoBlock.cs:WallDefs/WallDefBlock`) reads them as fixed
**780-byte (0x30C) records**, each a `byte[5][156]` table of **8×8-tile IDs**:

```
record size = 780 = 5 rows * 156 cols
data[y][x]  = tile id (a block id in the matching 8X8D*.DAX tile file)
```

- A `WALLDEF*.DAX` block of N×780 bytes holds N such records (loaded into wall "symbol
  sets" 1..3). In this repo: id 23 = 780 (1 record), id 1 and id 3 = 1560 (2 records each).
- `blockCount = decode_size / 0x30C`; the loader also pairs each record with an
  `8X8D*.DAX` tile block via `ovr038.Load8x8D`.
- The 5 rows correspond to the 5 depth "slices" of the corridor 3D view; the 156 columns
  are wall positions/segments within a slice.
- `WallDefBlock.Offset(off)` adds a base offset to every entry `>= 0x2D` (45), relocating
  tile IDs into the shared 8×8 tile pool. Entries `< 0x2D` (notably `0x01`, and `0x00`)
  are special/reserved (borders / "no tile"), which is exactly why the raw bytes are
  dominated by small values and recurring `01` at fixed columns (cols 0,2,6,10,11,12).

So the actual wall *pixels* live in `8X8D*.DAX` (already decoded as 16-px-wide tile strips
by `tools/dax_decode.py`); WALLDEF just says which tile goes where. **No pixel decode is
needed for WALLDEF.** This is the correct retirement of the "WALLDEF complex graphics"
unknown.

`tools/dax_complex_decode.py` reports WALLDEF blocks as "NOT animated-multiframe"; to
inspect the tables, slice the decompressed block into 780-byte records and read them as
`5×156` byte grids (see the analysis snippet in the project notes / commit message).

### C.1 Worked byte example — CoK `WALLDEF1.DAX`, block id 23 (780 bytes, 1 record)

```
row 0, cols 0..15:  113  65  1  46  75  79  1  46  75  78  1  1  1  93  93  46
row 1, cols 0..15:  113  75  1  93  75  81  1  93  65  80  1  1  1  46  46  93
row 2, cols 0..15:  113 104  1  46 104 114  1  46 104 115  1  1  1  46  62  46
row 3, cols 0..15:  113  91  1  46  92  96  1  46  92  97  1  1  1  46  49  46
row 4, cols 0..15:  113 105  1  46  76  55  1  46  76  56  1  1  1  46  61  46
```
Every value is a tile id (mostly `0x2D`=45..`0x73`=115); the `1`s at the fixed columns are
the reserved/border symbol. Reads as a coherent per-slice wall layout, not pixels.

---

## D. DUNGCOM / WILDCOM / RANDCOM — fixed-size tile array (combat backdrop)  — CONFIRMED

The last CoK block still flagged "complex" by `tools/dax_decode.py`. `DUNGCOM.DAX`'s one
real block decompresses to **7217 bytes** that begin `18 00 03 00 00 00 00 00 19 00 …`
then long runs of `0x88`. It is **not** the §B animated format (its `numFrames` byte
would be `0x18`=24 and the per-frame header words come out 0) and **not** a standard
frame (an 8-byte-header parse leaves 6921 stray bytes). It is the **multi-item DaxBlock**
layout from COAB `Classes/DaxFiles/DaxBlock.cs`: one shared size, then *N identical-size
tiles*.

```
offset 0 : uint16  height        = 0x0018 = 24 px
offset 2 : uint16  width_div8     = 0x0003 = 3  → pixel_width = 24
offset 4 : uint16  x_pos          = 0
offset 6 : uint16  y_pos          = 0
offset 8 : uint8   item_count     = 0x19 = 25     <-- number of tiles (all same size)
offset 9 : uint8[8] field_9       = 00 32 20 22 11 31 30 33   (EGA-plane/editor meta; ignored)
offset 17: pixel data : item_count × (height·width_div8·4) chunky-4bpp bytes
                       = 25 × (24·3·4) = 25 × 288 = 7200
```

Exact consume: `17 + 25·288 = 7217` ✓. Pixels are the same chunky EGA-16 4bpp
(high-nibble = left pixel), no second RLE, no XOR. The 25 tiles render as coherent
24×24 stone-wall / diagonal-wall pieces — the **combat backdrop tile set** that the
engine composites onto the combat grid by tile index. `WILDCOM`/`RANDCOM` are the
wilderness/random-encounter equivalents.

**Distinguishing it from a standard single frame:** the standard frame has an 8-byte
header with pixels at offset 8 (verified byte-identical to DaxDump on BIGPIC); the tile
array has a **17-byte** header with `item_count` at offset 8. The accept gate is the
exact-consume check `17 + count·tileBytes === rawSize`, tried only after the
frame/tile-strip/animated paths, so it never false-positives on them.

Triangulated three ways: (1) COAB `DaxBlock.cs` read, (2) a coherent render
(`renders/ui/DUNGCOM_montage.png`), (3) an independent Codex analysis that reproduced the
same field table and 7217 total byte-for-byte. TS decoder: `parseTileSet` in
`packages/engine/src/loaders/dax.ts` (`kind:'tileset'`), golden-tested against the real
`DUNGCOM.DAX` (25 tiles of 24×24) in `packages/engine/test/dax-animated.test.ts`.

`field_9` (`00 32 20 22 11 31 30 33`) is copied but unused by the COAB blitter — likely
original EGA-plane order / editor metadata; harmless to ignore for rendering.

---

## E. Confidence summary

| Claim | Confidence | Evidence |
|-------|-----------|----------|
| Animated picture header (21-byte/frame, fields as in §B) | **CONFIRMED** | Byte-exact parse consumes every SPRIT/PIC block; matches COAB `ovr030.cs` |
| Pixel = chunky 4bpp, high-nibble-left, EGA-16 | **CONFIRMED** | Coherent renders (warriors, dragon, portraits) in CoK+DoK |
| Multi-frame portraits use frame-0 XOR delta | **CONFIRMED** | PIC1 b12 f1 garbage w/o XOR, clean portrait w/ XOR |
| SPRIT frames are view-size variants (no XOR) | **CONFIRMED** | 3 frames have *different* w×h; render correctly verbatim |
| WALLDEF = 5×156 tile-id tables (not graphics) | **CONFIRMED** | 780-byte records, value ranges = tile ids, matches COAB `WallDefs` |
| DUNGCOM/WILDCOM = N-item fixed-size tile array (17-byte header) | **CONFIRMED** | 17 + 25·288 = 7217 exact; coherent 24×24 stone tiles; COAB `DaxBlock.cs` + independent Codex agree (§D) |
| Meaning of per-frame `field_9[8]` | PARTIAL | Replicated across frames; copied but unused by the COAB blitter. Looks like header padding / original-plane bytes; harmless to ignore for rendering. |
| `x_pos`/`y_pos` exact screen anchor | PARTIAL | Values are sane (small); used as placement offsets by the engine (COAB `OverlayBounded(... y_pos+3-1, x_pos+3-1)`), not needed to decode pixels. |

### Blocks that still don't decode as graphics
- `COMSPR.DAX` 305-byte blocks: not animated-multiframe and not a plain standard frame
  here; these are small combat-sprite records with their own mini-layout — out of scope
  for this pass and **not** part of the WALLDEF/SPRIT/PIC unknown. (Low priority.)

---

## F. Tooling

- `tools/dax_complex_decode.py <file.dax> [--list] [--out DIR] [--scale N] [--xor]`
  - Decodes/render the animated multi-frame picture blocks (§B). Reuses the verified
    container/RLE/palette from `tools/dax_decode.py` (which is left untouched).
  - `--xor` applies the frame-0 delta for animated pic/portrait files.
  - Reports WALLDEF (and other non-picture) blocks as "NOT animated-multiframe".
- `tools/dax_decode.py` continues to handle standard frames, 8×8 tile strips, and now —
  correctly — leaves WALLDEF/SPRIT/PIC to the complex decoder. (No change required there;
  the only doc fix is that `docs/dax-format.md §4c`'s "method-23 transparent" guess was
  wrong — it's the §B multi-frame format, plus the §C wall tables.)
