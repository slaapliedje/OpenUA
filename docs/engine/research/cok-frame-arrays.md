# CoK DOS EGA Exploration Screen — Frame Tile Arrays (VERIFIED)

**Status: VERIFIED against a real screenshot** (non-circular). Every array below is
recovered by exact pixel matching (MSE = 0) of `8X8D1.DAX` tiles against a genuine
Champions of Krynn DOS screenshot, `renders/explore/ref/cok_blog_30.png` (320×200, 16
colours, confirmed 100% EGA-exact). The Greybox compositor render then matches that
screenshot **206/206** on every cleanly-visible frame cell (the only diffs are the inner
viewport's lower edge, which that screenshot covers with an NPC portrait — content, not
frame). Verify with `packages/engine/test/renderExplore.manual.test.ts` + the diff in §6.

> **Supersedes an earlier draft of this file.** The earlier version concluded the frame was
> **block 203** with "brown wood" inner tiles, "near-identical to COAB" arrays. That was an
> artifact of a circular method (matching tile templates against the engine's *own* render,
> which had been built from COAB arrays). Matching against a **real screenshot** instead shows
> the frame is **block 202** with a clean 6-tile cyan/gray design and arrays that are NOT
> COAB's. Block 203 is a *different* tile set (used elsewhere), not the exploration border.

---

## 1. Boot block → set assignments (CoK)

From `GAME.OVR` (Load8x8D call site, `MOV AL,block; PUSH; MOV AL,set; PUSH; CALL`):

| Set | 8X8D1 block | Tiles | Purpose |
|-----|-------------|-------|---------|
| 4   | **202** (0xCA) | 38 | **Exploration frame** (tiles 20–25), direction arrows (0–3), gauges, **moons** (26–37) |
| 0   | 203 (0xCB) | 45 | A different decorative tile set — NOT the exploration border |

The COAB structure (frame symbols routed through set 4, base around 0x114/0x11E) holds for
CoK too — CoK's frame IS in set 4 / block 202, same as COAB. Only the **array values** (which
tile per cell) differ between the two games.

## 2. The frame is six tiles (block 202)

| Tile | Look | Role |
|------|------|------|
| **20** | cyan/green horizontal double-bar | every horizontal run: outer top/bottom, inner top/bottom, both separators |
| **24** | light-gray fluted vertical column | every vertical run: outer left/right, inner sides, vertical separator |
| **23** | upper junction piece | the cell just below each horizontal run start (rows 1, 17; inner row 3 / col 3) |
| **25** | lower junction piece | the cell just above each horizontal run end (rows 15, 22; inner row 13) |
| **21** | left moon bracket | top row, the cell left of each moon (cols 7, 18, 29) |
| **22** | right moon bracket | top row, the cell right of each moon (cols 9, 20, 31) |

The three moons (block-202 tiles 26–37, phase-dependent) overlay the **top row at columns
8 / 19 / 30** on a black cell. In `cok_blog_30.png` the phases shown are tiles 32 (Solinari
white), 29 (Lunitari red), 36 (Nuitari blue).

## 3. Placement arrays (direct block-202 tile indices, MSE=0)

Stored as raw tile indices (base 0). Inner arrays are indexed by col/row directly (cells
2..14; entries 0–1 are unused pads). These are exactly `COK_FRAME_LAYOUT` in
`packages/engine/src/render/exploreScreen.ts`.

```
outerTop    (col 0..39): 20 everywhere, except 21 at cols {7,18,29} and 22 at cols {9,20,31};
                         moons overlay cols {8,19,30}.
outerBottom (col 0..39): 20 (all)
outerLeft   (row 0..23): [20,23,24,24,24,24,24,24,24,24,24,24,24,24,24,25,20,23,24,24,24,24,25,20]
outerRight  (row 0..23): (identical to outerLeft)
hsep        (row 16, col 0..39): 20 (all)
vsep        (col 16, row 0..16): [20,23,24,24,24,24,24,24,24,24,24,24,24,24,24,25,20]
innerTop    (row 2,  col 2..14): 20 (all)
innerBottom (row 14, col 2..14): 20 (all) — inferred (portrait-covered in the ref shot; symmetric with innerTop)
innerLeft   (col 2,  row 2..14): [20,23,24,24,24,24,24,24,24,24,24,25,20]
innerRight  (col 14, row 2..14): (identical to innerLeft)
```

## 4. Geometry (unchanged from `cok-dos-screen-layout.md`)

Outer top row 0, bottom row 23; outer left col 0, right col 39; horizontal separator row 16
(full width); vertical separator col 16 (rows 0..16); inner viewport frame at cols/rows 2..14.
3-D viewport clip [8,176)×[8,176); inner frame drawn over the 3-D render. 40×25 grid, 8px tiles.

## 5. Draw order (`composeExploreScreen`)

Clear interior → viewport (clipped to the window) → outer border → separators → inner frame →
**moons over the top row (cols 8/19/30, black cell)** → party panel → position line → message →
prompt. All text via the block-201 mono font (`loaders/cokFont.ts`).

## 6. Verification

`renders/explore/cok-explore.png` (compositor) vs `renders/explore/ref/cok_blog_30.png` (real):
**206/206** cleanly-visible frame cells byte-identical; the 19 non-matching cells are the inner
viewport's lower edge (row 14 + col 2/14 rows 11–14), which the reference screenshot covers with
an NPC portrait. `innerBottom` = tile 20 is therefore the one **inferred** array (symmetric with
the confirmed `innerTop`); a clean dungeon-view screenshot would confirm it directly.

## 7. Open items

- `innerBottom` (and inner-side rows 11–14) confirmation needs a non-portrait (plain dungeon)
  screenshot.
- Exact binary storage of the arrays in `START.EXE`/`GAME.OVR` not located (FBOV overlays); not
  needed — the screenshot match is ground truth.
- Amiga `.DAA` frame: same logical layout expected, native 32-colour art, not yet derived.

---

*Verified by Greybox (Opus), 2026-06-21. Ground truth: real screenshot
`Champions of Krynn (MS-DOS)` exploration screen (Super Adventures in Gaming blog), matched
pixel-exact against `Champions of Krynn/8X8D1.DAX` block 202. Boot-load offset from `GAME.OVR`.
COAB (`simeonpilgrim/coab`, MIT) used for structural context only.*
