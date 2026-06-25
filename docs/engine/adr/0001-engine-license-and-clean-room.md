# ADR-0001 — Engine license (MIT) & clean-room reimplementation

**Status:** Accepted (assumption set by the lead; flagged here for the record)
**Date:** 2026-06-21

## Context

Prior art for Gold Box reimplementation is mixed-license: Dungeon Craft / UAF is **GPL-2.0**
(copyleft), Simeon Pilgrim's **COAB** has **no LICENSE file** (default: all rights reserved),
and Gold Box Companion is closed-source freeware. We want maximum ecosystem openness and a
distribution model that ships engine+tools only with user-supplied assets (VISION.md; prior-art
research §4). We must decide what we can legally build from and under what license.

## Options

1. **GPL-2.0 engine** — lets us copy Dungeon Craft C++ directly. Cost: copyleft forces all
   downstream/derivatives to GPL; precludes a permissive ecosystem; DC is C++ anyway (would be
   ported, not linked) so the benefit is marginal.
2. **MIT engine, clean-room reimplementation** — reimplement all logic (ECL VM, combat, loaders)
   in TypeScript from format docs + our own decoders + independent rules knowledge. Study GPL/
   unlicensed sources for understanding, copy nothing. Maximum openness; safe vs the unlicensed
   COAB.
3. **Dual-license / defer** — adds process overhead with no current benefit.

## Decision

**MIT-licensed engine, clean-room.** The ECL VM, combat math, and all decoders are reimplemented
from scratch regardless of references. We may *read* COAB and Dungeon Craft to understand the
formats and algorithms; we copy no source from either. Facts and data tables (opcode meanings,
field layouts) are not copyrightable and may be documented independently. No copyrighted SSI game
asset ever enters the repo or a build; the public app gates assets behind a "point at your install"
flow (matches ScummVM/DC/DOSBox precedent).

## Consequences

- The ECL VM is built from scratch (see ADR-0003) — slower than a fork but legally unencumbered
  and TypeScript-native.
- We cannot lift Dungeon Craft C++; we use it only as an architecture/feature reference.
- COAB code must not be incorporated unless/until a permissive grant is obtained — listed as a
  user decision (email Simeon Pilgrim) in ROADMAP. Reading it for reference is fine.
- The product name must avoid trademarked terms ("Gold Box", "D&D", "Dragonlance", "Forgotten
  Realms", "AD&D") — listed as a user decision.
- MIT means we keep the existing `web-engine/` license (already MIT) — no relicensing needed.
