# ADR-0006 — Authoring data model vs FRUA import

**Status:** Accepted
**Date:** 2026-06-21

## Context

The authoring pillar is "FRUA but broader": everything FRUA/UA allowed, plus multiple rulesets,
unlimited slots, real scripting, and modern art/audio (VISION.md pillar 3; FRUA/DC research §5).
Dungeon Craft proved a folder-based design package (`.dsn` + text databases) works but is GPL-2.0
(study-only, ADR-0001) and AD&D-1e-bound. FRUA modules (`.DSN`: 36 dungeon + 4 overland maps,
35+ event types, slot-numbered art) are a large existing content corpus we want to import.

## Options

1. **Adopt FRUA's `.DSN` format directly** — instant import, but inherits every hard limit (40
   places, ~100 events/map, 13 big-pics, no new spells/items, fixed entrypoint) and the AD&D-1e
   lock-in. Defeats "broader."
2. **Fork Dungeon Craft's format** — GPL entanglement (ADR-0001) and still 1e-shaped.
3. **Independent `ModulePackage` model (superset) + a FRUA importer** — our own folder-based,
   JSON-manifest + text-DB design (conceptually DC-like, independently designed), with no
   hardcoded ceilings, real scripting (variables/arithmetic/branching), combat-outcome triggers,
   pluggable rulesets, and modern asset formats. A separate importer maps FRUA `.DSN` (and our
   decoded DAX/DAA/TLB art) *into* this model.

## Decision

**Option 3.** Define `ModulePackage` / `ModuleMap` / `ModuleEvent` (ARCHITECTURE.md §2.5) as a
*superset* of the loaded-game model, so an authored module loads and plays through the **same**
`loadGame`/engine/VM/ruleset path as an original game (no separate authoring runtime). The 35+
FRUA event types are the MVP authoring vocabulary; we add scripting hooks on top. A FRUA importer
maps the `.DSN` layout, event types, and slot-numbered art into `ModulePackage` using our native
decoders (full art import, unlike DC's "vanilla-only").

## Consequences

- Authored content and original content share one engine — playtest is just "load the module."
- No FRUA hard limits are inherited; the model is the source of truth and original limits live
  only in the importer's mapping.
- The importer is a clean, independently testable slice (FRUA `.DSN` → `ModulePackage`), unblocked
  by the unlicensed-source concerns (we wrote the decoders ourselves).
- Events compile to ECL-compatible bytecode (or our IR) so the same VM runs authored and original
  scripts — reuses ADR-0003 rather than adding a second scripting engine.
- Building the full editor UI is the largest, last milestone; the *data model* and importer can
  land early and be exercised headlessly before the editor exists.
