# DQK VGA 256-colour art through the render pipeline — C3 notes

**Phase C, C3.** Bringing The Dark Queen of Krynn's real VGA 256-colour graphics onto the
screen. The HLIB `TILE` decoder, the per-leaf colour table, the Chain-4 pixel layout and the
`indexedToRGBA` render path were all already verified (see `docs/hlib-format.md` and the
golden `hlib.test.ts`). The remaining gap was **palette compositing** — the open
"multi-palette layering" question in `hlib-format.md §8`.

## The problem — a leaf only owns a slice of the DAC

A single HLIB `TILE` leaf stores a *partial* palette: a `first_col`/`ncolors` window, not the
whole 256-colour table. Measured on the real DQK files:

| Library | leaf palette `first_col` | `ncolors` | owns slots |
|---|---|---|---|
| `ALWAYS.TLB` (UI chrome) | 0 | 16 | 0–15 |
| `GEN.TLB` / `FRAME.TLB` (frame UI) | 16 | 16 | 16–31 |
| `BIGPIC.TLB` (overland maps) | 32 | 224 | 32–255 |

This matches `UAPALETT.TXT`: ALWAYS=0–15, FRAME/GEN=16–31, walls=32–68, combat sprites=64–95,
backdrops=144–175, sprites≈176–255. So a `BIGPIC` overland picture rendered with **only its own
leaf palette** paints slots 32–255 correctly but leaves 0–31 black; standalone-correct but not
the full image the engine shows. The DQK runtime builds the active 256-colour DAC by **layering**
the leaves that are co-resident on a screen.

## The fix — `mergePalettes` / `compositeHlibPalette`

`render/palette.ts`:
- `mergePalettes(...palettes)` — composite partial palettes into one 256-slot table. Fill order =
  argument order; a later palette's defined colour wins (last-wins) and a `null` never clobbers an
  already-defined slot. Colour-cycle ranges concatenate. Undefined slots stay `null` → black at
  render time, exactly as on hardware.
- `definedColorCount(palette)` — how many of the 256 slots a palette actually defines.

`loaders/hlib.ts`:
- `compositeHlibPalette(archive)` — merge every leaf palette in one archive (a master like `BIGPIC`
  whose 3 leaves all carry the 32–255 slice → that slice). Cross-library layering is just another
  `mergePalettes` over each archive's composite.

So the full overland DAC is `mergePalettes(compositeHlibPalette(ALWAYS), compositeHlibPalette(BIGPIC))`
= 0–15 (UI) + 32–255 (picture) = **240 defined slots**, and the `BIGPIC` picture renders as real
full-colour VGA art.

## Verification

- Engine golden (`test/palette.test.ts`): on the real `BIGPIC.TLB` + `ALWAYS.TLB`, the picture
  composite defines exactly 224 slots (32–255, 0–31 null), the UI composite 16 (0–15), the layered
  DAC 240; the first overland picture (304×120, method 18) rendered through the DAC yields **>50
  distinct colours** — real art, not a dim blob.
- Web (`apps/web/src/ui/dqkExplore.ts`, `?dqk=1`): the DQK explore view now shows a **VGA art panel**
  — the 3 BIGPIC overland pictures painted through the composited DAC, with a prev/next stepper. e2e
  reads `artDistinctColors()` off the painted canvas and asserts 304×120 dims, ≥240 palette slots and
  >50 distinct rendered colours.

## Open follow-ups (not C3)

- First-person dungeon **wall art** (DUNGCOM + 8X8D tiles assembled into the 3D corridor view) — the
  Gen2 equivalent of CoK's wall-piece compositor. Lands with the DQK content/systems slice (C4).
- Combat sprite / portrait palettes (CPIC/SPRIT/CBODY/CHEAD use the 64–95 / 176–255 windows): same
  compositing path, wired when those screens get their DQK art.
- Colour-cycling animation (the `cycles` ranges are now merged but not yet animated in the viewer).
