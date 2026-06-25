# ADR-0004 — GraphicSet model & live graphics-mode switching

**Status:** Accepted
**Date:** 2026-06-21

## Context

A pillar feature is switching graphics modes live (DOS EGA ↔ Amiga ↔ VGA) while playing, with
each game showing the *best* art any platform shipped (VISION.md pillar 2). The Amiga has
exclusive frames DOS lacks (CoK character sprites/animations) and different frame counts per
container, so a naive "swap the whole file" approach breaks indexing (graphics-modes research
§4.1). Prior successful live-switchers (Tomb Raider Remastered F1, Diablo II Resurrected G) keep
both asset sets resident and toggle a pointer, not a reload (§3.1).

## Options

1. **Per-file/per-container switching** — swap which container is read. Breaks when sets have
   different frame counts or exclusive frames; can't fall back at frame granularity.
2. **Reload on switch** — decode the other platform's files on demand. Causes a stall/black frame;
   poor game-feel; contradicts the proven pattern.
3. **Resident sets + per-frame resolve with fallback chain** — every logical asset is a
   `(category, index)` ref; the manager resolves it through the active `GraphicSet` then a
   priority fallback chain; both sets stay decoded in memory; switching is a pointer swap +
   redraw.

## Decision

**Option 3.** Model `GraphicSet`, `LogicalAssetRef`, and `GraphicSetManager` as first-class
engine concepts from day one (ARCHITECTURE.md §2.2). Fallback is **per-frame, not per-file**:
`resolve(ref)` tries the active set's `(category→index)`, then each fallback set, returning the
first hit. `has(setId, ref)` lets game logic bounds-check before indexing exclusive frames.
The render layer only ever calls `resolve` — it never touches a container. Switch = `setActive`
+ emit `change` → host redraws; no decode, no reload.

## Consequences

- Retrofitting set-awareness later would be painful, so it's built in from the manifest
  (`graphicSets` + `graphicSetFallback`) through the resolver — even when only one set exists.
- All sets are decoded up front (or lazily cached) into the engine-neutral `IndexedImage`, so a
  switch is O(redraw), matching Tomb Raider-class feel.
- Different per-set frame counts are handled safely because indexing always goes through the
  set-aware resolver, never a raw container.
- Pairs with ADR-0007: every set renders through the same WebGL palette-texture pipeline; only
  the palette/index textures differ. Amiga-32, EGA-16, VGA-256 all just fill different palette
  ranges.
- The Amiga set is gated on the DAA-palette decode (ARCHITECTURE.md §6) — the manager and
  manifest support it before that lands; the set is simply marked incomplete until then.
