# ADR-0005 — Ruleset plug-in approach (multi-system mechanics)

**Status:** Accepted
**Date:** 2026-06-21

## Context

The roster spans four rulesets: AD&D 1e, AD&D 1e + Dragonlance (Krynn), AD&D 2e (Savage
Frontier), and Buck Rogers XXVc (roster research §5). Authoring must let designers pick a ruleset
and add new ones (VISION.md pillar 3). FRUA and Dungeon Craft are both architecturally married to
AD&D 1e — DC data-drives the *content* but never abstracts the *rule engine* (FRUA/DC research
§5.1). There is no prior art for a true multi-system plug-in in this genre; this is the hardest
abstraction.

## Options

1. **Hardcode AD&D 1e, config the data** (the DC approach) — proven but caps us at one rules
   family; Buck Rogers' careers/psionics/zero-G don't fit; can't add genuinely new systems.
2. **Full scripting language for all rules** — maximally flexible but huge surface, slow, and
   overkill for shipping the known four rulesets.
3. **`RulesetPlugin` interface + registry** — the engine calls *into* a plug-in for every
   mechanics decision (ability rolls, THAC0, attack/damage/save resolution, XP/level-up,
   spell/psionic casting, item semantics, and interpreting opaque `Monster.raw`/`Item.raw`
   records). Plug-ins are code+data, registered by id, selected by `manifest.ruleset`.

## Decision

**Option 3.** Define `RulesetPlugin` / `RulesetRegistry` (ARCHITECTURE.md §2.3). Ship
`addnd1e` as the base; `addnd1e-dragonlance` extends it (Solamnic Knight, Kender/Tinker Gnome,
lunar magic, god-cleric specials); `addnd2e` overrides THAC0/proficiency; `buckrogers` swaps
classes→careers and the spell hook→psionics. The same `castSpell`/`memorize` hooks serve both
magic and psionics. The engine core (loop, render, VM, state) contains **no** mechanics math.

## Consequences

- The engine never branches on game/ruleset; it asks the plug-in. Adding Buck Rogers or a custom
  system is a new package, not engine edits.
- Each plug-in is unit-testable against known values (THAC0 progression, XP thresholds, save
  tables) — strong, cheap test surface and an early ROADMAP slice.
- Monster/item records stay opaque (`raw: Uint8Array`) in the neutral model; the *ruleset*
  interprets them per game via `interpretMonster`/`interpretItem`, so the loader doesn't need
  full field decode to boot.
- Authoring's ruleset picker and "add a ruleset" feature fall out of the registry directly.
- Designing the interface to fit all four real rulesets up front (rather than generalizing later)
  is the main risk; we validate it by implementing `addnd1e` + `buckrogers` (the most divergent
  pair) before declaring the interface stable.
