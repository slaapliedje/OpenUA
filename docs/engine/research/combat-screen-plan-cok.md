# CoK DOS Tactical Combat Screen — Implementation Plan

**Status: DESIGN (not yet implemented).** This is the implementation plan for the
original-look tactical **combat screen** for Champions of Krynn (DOS EGA, older Gold Box /
DAX). It mirrors the *exploration* pipeline that is already done and verified pixel-exact
(`packages/engine/src/render/exploreScreen.ts` `composeExploreScreen`, `render/screen.ts`,
`render/textmode.ts`, `apps/web/src/ui/explore.ts`). The engine already has combat
*resolution* (`packages/engine/src/combat/`); this plan adds the combat *screen* (renderer
+ controller boundary) without touching the rules core.

Grounded in: `docs/engine/research/cok-dos-screen-layout.md`,
`docs/engine/research/firstperson-wall-geometry-cok.md` (the `draw_combat_picture` clip),
`docs/engine/research/dax-complex-subframe.md` §B/§D (SPRIT sprites + DUNGCOM tilesets),
`docs/engine/research/cok-monster-format.md`, and the COAB oracle
(`github.com/simeonpilgrim/coab`, MIT) cited inline. SSI executables were not decompiled.
Confidence is labelled per claim; **(inferred)** marks anything not yet pinned to COAB or a
real screenshot.

> **Verification stance:** like explore, structural placement gets *synthetic* unit goldens
> now; a *real-asset* manual render (skipped without GOG assets) is added immediately, and a
> pixel-exact array-recovery pass against a genuine CoK combat **screenshot** (MSE=0, the
> method that produced `COK_FRAME_LAYOUT`) is the closing gate. Until that screenshot match
> lands, the **grid-cell pixel anchors and the menu/message exact rows are CANDIDATE**, not
> confirmed.

---

## 1. The CoK combat screen — layout

### 1.1 What it is

CoK tactical combat is an **overhead-but-foreshortened battle grid** drawn into the *same*
upper-left viewport box the dungeon 3-D view uses, with the party-status panel on the right
and a command menu + message line below. It is NOT a separate full-screen mode: the engine
reuses the 320×200 / 40×25 / 8px-tile screen, the `draw_combat_picture` clipped blitter
(`docs/engine/research/firstperson-wall-geometry-cok.md` §1, `seg040.cs`), the block-201
mono font, and the same EGA-16 palette. This is why the explore compositor's primitives
(`createFramebuffer`, `blitIndexed`, `blitIndexedClipped`, `fillRectIndexed`, `drawText`)
carry over unchanged.

The battle area is a rectangular grid of cells. Each cell holds at most one combatant icon.
A **backdrop** (terrain) is composited under the icons from the `DUNGCOM` / `WILDCOM` /
`RANDCOM` 24×24 tile sets (already decoded — `parseTileSet`, `kind:'tileset'`,
`docs/engine/research/dax-complex-subframe.md` §D). Party members and monsters are drawn as
**sprites/icons** (`SPRIT*.DAX`, `docs/engine/research/dax-complex-subframe.md` §B) over the
backdrop. A cursor/highlight marks the active combatant; the bottom command menu
(`MOVE / ATTACK / CAST / USE / GUARD / BANDAGE / VIEW / AIM / TURN / WAIT / …`) and the
scrolling message line drive the turn.

### 1.2 Region map (pixel coords, [inclusive, exclusive))

The screen frame geometry is **identical to exploration** (`cok-dos-screen-layout.md` §3):
40×25 grid, outer border, the vertical separator at col 16 (x=128) and horizontal separator
at row 16 (y=128), the right party panel at cols 17..38, the message area at rows 17..22,
the prompt bar at row 24. The combat screen reuses that chrome; only the *viewport contents*
change (battle grid instead of the 3-D corridor) and the *bottom region* gains a command
menu.

```
┌─────────────────────────────────────────────────────┐
│ Outer top border   y[0,8)   x[0,320)                │
├──────────┬───────────────────────┬──────────────────┤
│ Battle viewport (combat grid)    │ Right panel       │
│ clip x[8,176) y[8,176) 168×168   │ x[136,312)        │
│   backdrop tiles + combatant     │ party rows 2..9   │
│   icons + active-unit cursor     │ (name AC HP)      │
│   (vsep col16 x[128,136))        │                   │
├──────────┴───────────────────────┴──────────────────┤
│ Horizontal separator   y[128,136)  x[0,320)          │
├─────────────────────────────────────────────────────┤
│ Combat message / action log  rows 17..22  x[8,312)   │
│ Command menu (MOVE ATTACK CAST …) drawn here / row24 │
├─────────────────────────────────────────────────────┤
│ Outer bottom border y[184,192)                       │
│ Prompt / menu bar   row 24  y[192,200)               │
└─────────────────────────────────────────────────────┘
```

| Region | Pixel rect (x,y) | Source / confidence |
|---|---|---|
| Outer border + separators + right panel + msg + prompt | (same as explore §3) | `cok-dos-screen-layout.md` §3 — **CONFIRMED** (shared chrome) |
| **Battle viewport** | x[8,176), y[8,176) = 168×168 | `firstperson-wall-geometry-cok.md` §1 — viewport is the `draw_combat_picture` clip, *named for combat*; **CONFIRMED as the clip**, grid mapping CANDIDATE |
| Right party panel | x[136,312), rows 2..9 | reuse explore `PartySummary`; in combat the rows highlight the **active** unit and gray the dead — **CONFIRMED region**, color logic CANDIDATE |
| Command menu | rows 22 / 24 (x[8,312)/full) | COAB `combat menu` is a horizontal `MOVE/ATTACK/…` list with a highlighted item; **(inferred)** exact row |
| Message/log | rows 17..22 | reuse explore text region — **CONFIRMED region** |

### 1.3 Battle-grid geometry (CONFIRMED — recovered from the COAB oracle, A3.1/CS.0)

**Pinned, not candidate.** The cell→pixel mapping was recovered from the COAB clean-room
decompile (`simeonpilgrim/coab`, MIT — the same older Gold Box generation as CoK), reading the
combat-draw call chain directly rather than guessing from a screenshot:

- `ovr033.sub_7416E` draws a backdrop cell with
  `ovr034.DrawIsoTile(tile, screenPos.y * 3, screenPos.x * 3)`.
- `ovr034.DrawIsoTile` → `seg040.OverlayUnbounded(set, 0, tile, rowY, colX)` which calls
  `draw_combat_picture(source, rowY + 1, colX + 1, …)` (the **+1**).
- `seg040.draw_combat_picture(blk, rowY, colX, idx)` = `draw_clipped_picture(blk, rowY, colX, idx,
  8, 176, 8, 176)`; `draw_clipped_picture` sets `minY = rowY * 8`, `minX = colX * 8`.

Composing the chain for a backdrop cell `(c, r)` (`screenPos.x=c, screenPos.y=r`):
`minX = ((c*3) + 1) * 8 = c*24 + 8`, `minY = r*24 + 8`. Combatant icons take the same path —
`ovr034.draw_combat_icon` → `draw_combat_picture(icon, (tileY*3)+1, (tileX*3)+1, 0)` — so icons
land on the **same** grid origin as the tiles. Therefore:

| Quantity | Value | Source |
|---|---|---|
| Cell origin (cell 0,0 top-left) | **pixel (8, 8)** | `(c*3+1)*8` with c=0 |
| Cell size | **24 × 24 px** | the `*3` (→8px units) × 8 = 24; DUNGCOM tiles are 24×24 |
| Clip / viewport box | **x[8,176) y[8,176) = 168×168** | `draw_combat_picture` clip args |
| Visible window | **7 × 7 cells** | 168 / 24 = 7 exactly |
| Foreshortening | **none** (orthogonal) | `screenPos*3` is a straight scale, no skew term |

This is **wider than the explore 3-D viewport** (which clips to x128 — see
`exploreScreen.ts` `VIEWPORT_CLIP`): CoK's battle field genuinely extends past the corridor
view's vertical separator. The logical combat map is larger than 7×7 and **scrolls** under the
window (COAB `gbl.mapToBackGroundTile.mapScreenTopLeft`); the 7×7 are the on-screen cells.

Encoded in `COK_COMBAT_GEOMETRY` (`packages/engine/src/render/combatScreen.ts`) and pinned by
`combatScreen.test.ts`. **Still CANDIDATE:** how the right party panel / vertical separator
reconcile with the 168-wide field is the combat-vs-explore *chrome* question — deferred to CS.10
(the genuine-screenshot closing gate); A3.1 fixes only the grid cell math.

---

## 2. Engine-side interface contract

The boundary mirrors explore exactly: **a pure, DOM-free compositor that returns an
`IndexedFramebuffer`**, plus a **combat-view state model** that is *derived from* the
existing combat core, never duplicating rules. The web layer resolves the framebuffer to
RGBA via the existing `framebufferToRGBA(fb, palette)` and paints it with `ScreenSurface`,
just like `apps/web/src/ui/explore.ts`'s `drawScreen()`.

### 2.1 `composeCombatScreen` — the renderer (new: `packages/engine/src/render/combatScreen.ts`)

```ts
/** Where each combatant sits on the battle grid + how to draw it. View-model only — no rules. */
export interface CombatCellState {
  combatantId: string;           // matches Combatant.id in combat/combat.ts
  side: 'party' | 'enemy';
  col: number; row: number;      // logical grid cell
  /** Icon to blit (SPRIT frame, magenta-keyed), or null to fall back to a colored glyph. */
  icon: IndexedImage | null;
  hpFraction: number;            // 0..1, drives a wound tint / down state
  down: boolean;                 // hp<=0 → drawn prone/removed
  active?: boolean;              // the unit whose turn it is (cursor + panel highlight)
}

/** Logical→pixel mapping for the battle grid (data-driven so the screenshot-recovered
 * numbers drop in without an interface change — cf. ExploreFrameLayout). */
export interface CombatGridGeometry {
  originX: number; originY: number;   // top-left of cell (0,0), default (8,8)
  cellW: number; cellH: number;       // pixels per cell
  cols: number; rows: number;
  /** Optional per-row vertical squash for the Gold Box foreshortened look. Default 0. */
  rowSkew?: number;
}

export interface CombatBackdrop {
  /** 24×24 DUNGCOM/WILDCOM/RANDCOM tiles (parseTileSet kind:'tileset'). */
  tiles: IndexedImage[] | null;
  /** Per-grid-cell backdrop tile index, length cols*rows; out-of-range → tile 0. */
  cellTile?: readonly number[] | null;
  /** Single fill tile when no per-cell map is given. Default: tile 0. */
  fillTile?: number;
}

export interface CombatScreenInput {
  geometry?: CombatGridGeometry;          // default CANDIDATE constant
  backdrop?: CombatBackdrop | null;
  /** Combatant placements + icons (party + enemies). */
  cells: readonly CombatCellState[];
  /** Reuse the explore chrome verbatim. */
  frameTiles?: IndexedImage[] | null;
  frameLayout?: ExploreFrameLayout;       // defaults to COK_FRAME_LAYOUT
  font?: BitmapFont | null;
  /** Right panel: party rows (active highlighted, down grayed). */
  party?: ExploreParty[];
  /** The command menu items, with the selected index highlighted. */
  menu?: { items: readonly string[]; selected: number } | null;
  /** Scrolling combat log (newline-joined; wraps in the message region). */
  message?: string | null;
  /** Bottom prompt bar text. */
  prompt?: string | null;
}

/** Build the full 320×200 indexed combat screen. Pure / DOM-free. Resolve with
 *  framebufferToRGBA(fb, palette). Draw order: clear → backdrop tiles → combatant icons
 *  (back-to-front by row) → active-unit cursor → outer frame + separators (chrome) →
 *  right panel → menu → message → prompt. */
export function composeCombatScreen(input: CombatScreenInput): IndexedFramebuffer;
```

Design notes:
- **Same primitives as explore.** Backdrop tiles via `blitIndexedClipped` into the
  `[8,176)×[8,176)` box (so icons/tiles never spill into the panel/separators); chrome via
  the existing `putFrame` logic — factor the explore frame-drawing into a shared
  `drawScreenChrome(fb, tiles, layout)` so both compositors call it (avoids divergence).
- **No DOM, no rules.** `CombatCellState` is a pure view-model the controller derives from a
  `Combatant[]` + a positions map. The compositor never imports `combat/combat.ts`.
- **Icons are `IndexedImage`** (the neutral model) — the renderer is asset-source-agnostic,
  so DoK/DQK feed differently-decoded icons without renderer changes (extends-cleanly).
- **Graphics-mode switch is free**: the framebuffer stays indexed; switching EGA↔Amiga is a
  palette + icon-set swap at the edge (ADR-0004), no compositor change.

### 2.2 The combat-view controller (new: lives in the player UI, `apps/web/src/ui/combat.ts`)

The controller is the analog of `explore.ts`: it owns turn flow, input, and the bridge to
the rules core. **It does not duplicate `runCombat`'s math.** Two integration options for the
rules boundary are weighed in the ADR; the recommended one (interactive stepper) needs a
small, additive extension to `combat/combat.ts` rather than a rewrite. The controller:
1. Builds combatant placements (initial deployment + a positions map `id → {col,row}`).
2. Each turn, asks the rules core for the acting unit + legal actions, renders via
   `composeCombatScreen`, takes input (keyboard/menu), applies the action, appends to the log.
3. On end, returns a `CombatResult` (same shape the core already produces) to the caller
   (`explore.ts`'s `resolveCombat`), so HP-persistence/XP wiring (`applyCombatHp`) is unchanged.

---

## 3. Assets that drive the screen

| Asset | File(s) | Loader (status) | Role | Gap |
|---|---|---|---|---|
| Backdrop tiles | `DUNGCOM.DAX`, `WILDCOM.DAX`, `RANDCOM.DAX` | `decodeDax` → `parseTileSet` `kind:'tileset'` (**done**, 25×24×24, `dax-complex-subframe.md` §D) | terrain under the grid | **which** of the 25 tiles tile where (per-encounter backdrop map) is unmapped; floor-fill works now |
| Combatant icons | `SPRIT1.DAX` … (monsters), party icons | `decodeDax` animated path (**done**, `dax-complex-subframe.md` §B; 3 view-size frames, magenta=13 mask) | the figures on the grid | **monster→sprite link**: `MON{n}CHA` record has NO verified sprite-id field (`cok-monster-format.md`); the ECL `LOAD_MONSTER` carries a `picBlockId` (`ecl/vm.ts` `EncounterMonster.picBlockId`) — *that* is the icon source, not the monster record. Confirm CPIC vs SPRIT block id semantics. |
| Frame / chrome | `8X8D1.DAX` block 202 + font block 201 | `decode8x8dBlock` + `loadCokFont` (**done**) | border, separators, panel, text | none (reused from explore) |
| Palette | fixed EGA-16 | `makeEgaPalette` (**done**) | color resolve | none |
| Combat command labels | engine strings | n/a | the MOVE/ATTACK/… menu | text is hard-codeable; verify exact CoK wording/order vs a screenshot |

Key asset finding to carry forward: **the icon for an enemy group comes from the ECL
encounter setup (`EncounterMonster.picBlockId`), not from the `MON{n}CHA` record.** The
controller already receives `EncounterMonster[]` (see `explore.ts` `resolveCombat`); it must
pass `picBlockId` into a sprite lookup (`SPRIT*` / `CPIC*` decode) to get the icon. This is
the one new loader-wiring step; the decoders themselves exist.

---

## 4. Milestones / task breakdown

Small, independently testable steps. Golden strategy mirrors explore: synthetic-tile unit
goldens for placement (`exploreScreen.test.ts` style), a skipped real-asset manual render
(`renderExplore.manual.test.ts` style), then a screenshot-recovery pass.

| # | Task | Acceptance / golden | Size | Agent |
|---|---|---|---|---|
| **CS.0** | **Research/recover grid geometry**: from a real CoK combat screenshot, pin `CombatGridGeometry` (cell size, cols/rows, foreshortening) and the menu/message exact rows by MSE-matching DUNGCOM tiles + font, as `COK_FRAME_LAYOUT` was. Write the numbers into this doc. | Doc gives geometry with a screenshot match (cells + 1 backdrop tile byte-exact); CANDIDATE → CONFIRMED | M | Researcher / Sonnet |
| **CS.1** | Factor explore frame drawing into shared `drawScreenChrome(fb, tiles, layout)` used by both compositors (no behavior change). | Existing `exploreScreen.test.ts` still green; new fn unit-tested | S | Implementer / Sonnet |
| **CS.2** | `composeCombatScreen` skeleton: clear → backdrop fill → chrome → panel → message → prompt (no icons yet). Synthetic tiles. | Unit golden: 320×200 fb; backdrop confined to [8,176); panel/prompt text at explore coords | M | Implementer / Opus |
| **CS.3** | Backdrop layer: tile the DUNGCOM set into the grid via `CombatBackdrop` (fill + per-cell map), clipped. | Unit golden: known tile index lands at the right grid cell; nothing past x128/y128 | S | Implementer / Sonnet |
| **CS.4** | Combatant icons: blit `CombatCellState` icons back-to-front, magenta-keyed, with active-unit cursor + down state. | Unit golden: synthetic icon at cell→pixel; active cursor present; down unit grayed | M | Implementer / Opus |
| **CS.5** | Command menu render: horizontal `MOVE/ATTACK/…` list with selected item reversed (white-bg/black-fg, as explore menu highlights). | Unit golden: selected item uses reversed colors at the menu row | S | Implementer / Sonnet |
| **CS.6** | Real-asset manual render (skipped w/o GOG assets): compose a static battle from real DUNGCOM + SPRIT1 + 8X8D1 + font → PNG in `renders/combat/`. | Test writes a coherent combat PNG; `expect(png.length>100)` | M | Implementer / Opus |
| **CS.7** | Monster→icon wiring: resolve `EncounterMonster.picBlockId` → `SPRIT*/CPIC*` frame; controller builds `CombatCellState[]` from a resolved encounter. | Unit: a 3-goblin roster yields 3 placed cells with the right icon block | S | Implementer / Opus |
| **CS.8** | Rules boundary: additive interactive stepper on the combat core (see ADR) — `createCombat(combatants, opts)` → `{ current, legalActions, apply, log, result }` — leaving `runCombat` as a thin loop over it (regression-safe). | Existing combat goldens (`combat.test.ts`, `encounter.test.ts`) byte-identical; new stepper unit-tested; `runCombat` re-expressed via stepper produces the *same* log | L | Implementer / Opus |
| **CS.9** | `apps/web/src/ui/combat.ts` controller: deploy party + enemies, keyboard-driven MOVE/ATTACK turns, render each step via `composeCombatScreen`, return `CombatResult`. Replaces the auto-resolved `encounter-overlay` path in `explore.ts` when a real party fights. | E2e: trigger an encounter → combat screen renders → a turn resolves → outcome returns; HP persists via existing `applyCombatHp`; no console errors | L | Verifier+Implementer / Opus |
| **CS.10** | Screenshot-recovery closing gate: combat compositor output matches a genuine CoK combat screenshot on cleanly-visible cells (MSE-style, as explore hit 206/206). | Documented match; CANDIDATE labels in this doc → CONFIRMED | M | Verifier / Opus |

CS.0/CS.1/CS.2 are the unblocking first slice (CS.1 + CS.2 can proceed on synthetic tiles
in parallel with CS.0's research).

---

## 5. Open questions / risks

1. **Grid geometry (highest risk).** Cell size, grid dimensions, and the foreshortened
   vertical spacing are CANDIDATE until the screenshot pass (CS.0/CS.10). Mitigation:
   `CombatGridGeometry` is a data-driven parameter (the `ExploreFrameLayout` pattern), so the
   renderer ships before the numbers are final and gains correctness with no API churn.
2. **Monster→sprite link.** Confirmed *not* in the `MON{n}CHA` record; the icon comes from
   ECL `EncounterMonster.picBlockId` (already plumbed to the host). Risk: whether that id
   indexes `SPRIT*`, `CPIC*`, or a combat-specific block needs a one-file check (COAB
   `CMD_LoadMonster` / the CPIC decode). `COMSPR.DAX` (305-byte records,
   `dax-complex-subframe.md` §D "still don't decode") may be the combat-sprite container —
   flag for a small reversing pass if `picBlockId` doesn't resolve to SPRIT.
3. **Per-encounter backdrop map.** Which DUNGCOM tile goes in which cell (terrain layout) is
   unmapped; floor-fill is correct enough for first playability. Real terrain layout likely
   lives in the ECL combat-setup opcodes — defer to a later slice.
4. **Rules-core extension shape.** `runCombat` currently runs a fight to completion
   internally. Interactive play needs to *step*. The ADR recommends an additive stepper that
   `runCombat` is re-expressed over, keeping every existing golden byte-identical — but this
   is the one change that touches the verified rules file, so it must be regression-pinned.
5. **Initiative/turn-order display.** The core rolls initiative per round internally
   (`combat.ts`); surfacing turn order to the UI needs the stepper (CS.8). Until then the
   screen can show the resolved log (current behavior) — no regression.

---

## 6. Extending to DoK and DQK

The design is deliberately seam-compatible across the three games; only the *edges* differ:

- **DoK (Krynn-Gen1, DAX-EGA — same engine generation):** identical chrome, identical
  `composeCombatScreen`, same EGA-16 palette. Differences are pure data: DoK's own
  `DUNGCOM/WILDCOM`-equivalent backdrops, `SPRIT*` icons, `MON{n}CHA` records, and (possibly)
  a few ECL opcode deltas for combat setup. **No renderer change** — feed DoK's decoded
  `IndexedImage`s and geometry. Expect CS.0's geometry to transfer (same engine).
- **DQK (Gen2, HLIB-VGA):** the *renderer* is unchanged — it composes an indexed framebuffer
  from `IndexedImage` icons/backdrops regardless of source. What changes at the edge:
  (a) backdrops/icons come from **HLIB `TILE` chunks** (`loaders/hlib.ts`, done) with an
  **embedded 256-color VGA palette**, not DUNGCOM/SPRIT + fixed EGA; (b) the chrome may use a
  different frame layout (pass a different `*_FRAME_LAYOUT`/geometry); (c) Gen2 ECL combat-
  setup opcodes differ (the ECL VM dialect handles that, not the screen). Because the
  framebuffer is indexed and resolved through a swappable `Palette`, **live EGA↔Amiga↔VGA
  switching of the combat screen is the same pointer-swap as explore** (ADR-0004) — built in,
  not bolted on.

The single cross-game contract is: *give `composeCombatScreen` a `CombatGridGeometry`, a set
of `IndexedImage` backdrop tiles + icons, the per-cell placements, and the chrome layout;
it returns an indexed 320×200 framebuffer.* Everything game-specific is data at that seam.

---

*Plan by Greybox (Opus), 2026-06-21. Grounded in the cited repo files + research docs; COAB
(`simeonpilgrim/coab`, MIT) used for structural context only. SSI executables not
decompiled. Geometry/menu specifics labelled CANDIDATE pending the screenshot-recovery pass
(CS.0/CS.10), the same method that produced the pixel-exact exploration `COK_FRAME_LAYOUT`.*
