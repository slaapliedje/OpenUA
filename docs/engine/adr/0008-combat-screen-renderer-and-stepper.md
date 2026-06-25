# ADR-0008 — Combat screen: pure compositor + interactive rules stepper

**Status:** Proposed
**Date:** 2026-06-21
**Related:** ADR-0004 (GraphicSet / live switch), ADR-0007 (palette-texture render),
`docs/engine/research/combat-screen-plan-cok.md`

## Context

The engine already has a complete, golden-tested **combat resolution** core
(`packages/engine/src/combat/combat.ts` `runCombat`, `encounter.ts` `resolveEncounter`,
`party.ts` `partyCombatant`) that runs an AD&D-1e melee to completion deterministically and
returns a `CombatResult` (log + outcome + XP). What is missing is the **tactical combat
SCREEN**: the original-look battle grid, sprites, command menu, and message line — and the
ability to *play* a fight turn-by-turn rather than auto-resolve it.

Two coupled decisions must be made together because they define the renderer/rules seam:

1. **How does the combat screen render?** (relative to the done, pixel-exact exploration
   pipeline: `composeExploreScreen` → `IndexedFramebuffer` → `framebufferToRGBA` →
   `ScreenSurface`.)
2. **How does interactive turn-by-turn play reach the rules core** without duplicating its
   verified math or breaking its goldens?

`runCombat` today is internally a closed loop (rolls initiative, picks targets, resolves to
the end). Interactive play needs to *pause* between actions for player input. That tension is
the crux of decision 2.

## Options

### Rendering

- **R-A. New bespoke combat renderer / CSS-DOM overlay.** Fast to mock, but diverges from the
  proven indexed-framebuffer pipeline, can't share the explore chrome, and breaks the
  "indexed buffer + swappable palette = free graphics-mode switch" property (ADR-0004).
- **R-B. A pure `composeCombatScreen(input): IndexedFramebuffer`, mirroring
  `composeExploreScreen`**, sharing the chrome/primitives, taking icons/backdrops as neutral
  `IndexedImage`s and a data-driven `CombatGridGeometry`.

### Rules boundary

- **B-1. Keep `runCombat` as-is; screen replays its log.** Zero rules change; but the player
  can only *watch* a pre-resolved fight, never choose actions — not the original game.
- **B-2. Duplicate a turn loop in the UI controller.** Fast, but forks the rules: two
  implementations of to-hit/initiative drift apart; the goldens protect only one.
- **B-3. Additive interactive stepper in the core.** Factor the per-action logic out of
  `runCombat` into `createCombat(combatants, opts) → { current, legalActions, apply(action),
  log, result }`, and **re-express `runCombat` as a thin loop over the stepper** (auto-picking
  the same default action it picks today). The rules live in exactly one place; the UI drives
  the same engine the goldens pin.

## Decision

**R-B + B-3.**

- The combat screen is a **pure, DOM-free `composeCombatScreen`** in
  `packages/engine/src/render/combatScreen.ts`, mirroring `composeExploreScreen` (returns an
  `IndexedFramebuffer`; the web layer resolves + paints it exactly as `explore.ts` does). The
  explore chrome-drawing (outer frame, separators, panel, message, prompt) is factored into a
  shared `drawScreenChrome` so both compositors call one implementation. Battle geometry is a
  **data-driven `CombatGridGeometry`** parameter (the `ExploreFrameLayout` pattern), so the
  yet-to-be-recovered cell metrics drop in without an API change.
- Interactive play goes through an **additive stepper** (`createCombat`) in the rules core.
  `runCombat` becomes a loop over it and **must produce a byte-identical log** to today's, so
  every existing combat/encounter golden stays green (the regression gate). The UI controller
  (`apps/web/src/ui/combat.ts`) drives the stepper for player turns and returns the same
  `CombatResult` the host already consumes (so `applyCombatHp`/XP wiring is untouched).

Combatant icons are resolved from the **ECL encounter's `EncounterMonster.picBlockId`**
(already plumbed into the host), not the `MON{n}CHA` record (which has no verified sprite
field) — decoded via the existing `SPRIT*`/animated DAX path.

## Consequences

- **Pipeline reuse, free graphics switch.** The combat screen is an indexed framebuffer like
  every other screen, so EGA↔Amiga↔VGA switching is the same palette/icon-set pointer swap
  (ADR-0004); no combat-specific switch code.
- **One source of combat truth.** B-3 keeps to-hit/initiative/damage in `combat.ts` only; the
  cost is a refactor of a *verified* file, mitigated by the byte-identical-log gate (existing
  goldens) and stepper unit tests. This is the one change touching the rules core — it is
  explicitly regression-pinned in task CS.8.
- **Clean cross-game extension.** DoK reuses everything (same engine, EGA). DQK swaps the
  edge only: HLIB `TILE` icons/backdrops + embedded VGA palette + a different chrome layout;
  the compositor and stepper are unchanged (see plan §6).
- **Deferred, flagged:** exact battle-grid geometry and command-menu wording/rows are
  CANDIDATE pending the screenshot-recovery pass (CS.0/CS.10), and per-encounter backdrop
  terrain layout (which DUNGCOM tile per cell) is deferred to a later slice — floor-fill is
  used first. Whether `picBlockId` resolves to `SPRIT*` vs `CPIC*`/`COMSPR` needs a one-file
  confirmation; if it lands on `COMSPR` (the 305-byte records still undecoded,
  `dax-complex-subframe.md` §D) a small reversing pass is triggered.
- **User decision needed (flag):** none blocks the design, but the *first interactive scope*
  is a product call — ship MOVE/ATTACK melee first (matches the current rules subset) and add
  CAST/ranged/AIM when the spell/ranged rules land, OR hold the screen until those rules
  exist. Recommend the former (incremental, keeps the screen unblocked).
