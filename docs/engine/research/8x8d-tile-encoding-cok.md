# 8X8D tile pixel encoding — DOS Champions of Krynn — **SOLVED**

**Status: SOLVED (2026-06-21, corrected).** The 8X8D tiles are the standard multi-item
DaxBlock format — the SAME container as DUNGCOM — with a **17-byte header**. They decode to
authentic CoK art: dungeon wall pieces (cyan-dithered stone, brown beams, magenta-keyed
perspective triangles, black doorways) and a UI/decoration set (direction arrows, gauges,
orbs). The pixel encoding (chunky 4bpp) was always correct; the bug was the header length.

## The fix in one line

The 8X8D block header is **17 bytes, not 9** — `{u16 height, u16 width/8, u16 x, u16 y,
u8 item_count@8, u8[8] field_9}` — and tiles start at offset **17** with **no trailer**.
An earlier pass split the same 17 non-tile bytes as a *9-byte header + 8-byte trailer*,
placing tiles 8 bytes too early; that folded `field_9` into tile 0 and **rolled every tile
down by 2 rows**. (`loaders/tiles8x8.ts`.)

## Verified block layout

A decompressed `8X8D{area}.DAX` block is:

```
raw[0:2]       u16_le  height = 8
raw[2:4]       u16_le  width/8 = 1            (→ 8 px wide)
raw[4:8]       u16_le  x_pos, y_pos (0)
raw[8]         u8      item_count N
raw[9:17]      u8[8]   field_9   (EGA-plane/editor meta; ignored for render)
raw[17 .. 17+N*32]     N contiguous 32-byte tiles   (no trailer)
```

Proof on `8X8D1.DAX` — `17 + N*32 == len` to the byte for **every** real tile block:

| block | `raw[8]` (N) | 17 + N*32 | length |
| ----- | ----------- | --------- | ------ |
| 11/12/23/31/32 | 70 | 2257 | 2257 |
| 202   | 38 | 1233 | 1233 |
| 203   | 45 | 1457 | 1457 |

(Block 201 is **not** a tile block — its dims word is garbage; `sniff8x8dBlock` rejects it.)

This is identical to DUNGCOM's layout (25× 24×24 opaque combat-backdrop tiles); see
`dax-complex-subframe.md §D` for the shared format and the COAB `DaxBlock.cs` derivation.

## How the offset-9 error was caught

The earlier "SOLVED" note validated only on dense **wall** blocks (31, 203), which are mostly
magenta + stone — a 2-row vertical roll there still *looks* wall-ish, so confirmation bias let
it pass. Its cited evidence ("block 203 tile 0 = two patterned rows over six rows of solid
`0xdd`") was in fact the roll: those "two patterned rows" are `field_9` (`0011222301212011`),
glued onto the start of the real tile body (`raw[17:] = dd dd dd …`).

The **UI block 202** (direction arrows) is the disambiguator: at offset 17 the arrows are
crisp and centred in their 8×8 cells; at offset 9 they are rolled and smeared. Side-by-side
renders of both the UI block and the wall blocks settle it unambiguously:

- `renders/ui/_cmp_8x8d202_off17_myhdr.png` (clean) vs `_cmp_8x8d202_off9_trailer.png` (rolled)
- `renders/ui/_wall_b31_off17.png` / `_wall_b203_off17.png` (clean, apexes at corners) vs the
  `_off9` variants (2-row roll, broken triangle apexes, top-band seams)

## Per-tile pixel encoding (unchanged — was always correct)

Each 32-byte tile is **chunky 4bpp**: 8 rows × 4 bytes, 2 px/byte, **high nibble = left pixel**.
Colour **13 (EGA magenta)** is the conventional transparent/background key (the SPRIT sprites
use the same key; COAB blits with `OverlayUnbounded`). `decode8x8dBlock(.., { transparentMagenta:
true })` sets `transparentIndex = 13`; the asset browser decodes opaque by default. The
previously-suspected DRAW18 plane-interleave is **not** used — planar reads are noise; only
chunky-4bpp at offset **17** is clean.

## Cross-checks (locks the fix)

- `decode8x8dBlock` (dedicated wall path) and `decodeDax` → `parseTileSet` (generic DAX path)
  decode every real 8X8D1 tileset block **pixel-for-pixel identically** (test in
  `tiles8x8.test.ts`), so neither path can silently regress to offset 9.
- Golden tests assert `17 + N*32 == len` and N ∈ {70,45,38} against the real file.

## Follow-ups (not blocking)

- `field_9`'s exact meaning is unknown (likely EGA-plane order / editor metadata); copied but
  unused by the COAB blitter, harmless to ignore for rendering.
- Wiring the decoded sets into the live first-person viewport (`renderWallView`) is still
  pending — the web explore view currently uses the procedural backdrop, not these tiles.
