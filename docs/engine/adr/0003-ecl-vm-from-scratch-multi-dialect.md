# ADR-0003 — ECL VM: build from scratch, multi-dialect by config

**Status:** Accepted
**Date:** 2026-06-21

## Context

ECL (Event Control Language) is the bytecode scripting layer in every Gold Box game — events,
dialogue, combat triggers, quest logic. There are at least four opcode **dialects**: PoR-family,
Krynn-Gen1 (CoK/DoK), Savage-Frontier (Gen1-VGA), and DQK-Gen2 (HLIB/C++). **No public document
enumerates any opcode table** (hackdocs `OPCODES.TXT` is x86, not ECL). The only working
references are COAB (unlicensed C#/C, Gen1) and GBC's closed-source ECL-Monitor. This is the
single highest-risk gameplay unknown (findings §5.3, roster research §4, prior-art §2).

## Options

1. **Fork/port COAB's interpreter** — fastest, but COAB is unlicensed (ADR-0001 forbids copying)
   and only covers Gen1, not the DQK-Gen2 target.
2. **One hardcoded VM per game** — duplicative; bakes opcode drift into code; hard to extend.
3. **One VM, dialect tables loaded from config** — the VM is a generic dispatch loop; each game's
   manifest names an `EclDialect` (opcode→handler map + optional decrypt). Handlers call a narrow
   `EclHost` interface (ARCHITECTURE.md §2.4). Opcodes are *data*, derived empirically and stored
   in config, not hardcoded.

## Decision

**Build a single clean-room ECL VM parameterized by a per-game `EclDialect`.** Start with a
**disassembler** (bytes → `EclOp[]`) before an interpreter, so we can validate opcode meanings
visually/against known game behavior and power the Companion ECL-Tool panel early. Target
**DQK-Gen2 first** (it's the documented HLIB generation and the engine target), then add the
Krynn-Gen1 dialect for CoK/DoK. The VM is async/cooperative (`await host.showText(...)`,
`await host.startCombat(...)`) and exposes `step()` for the inspector. Combat outcome is returned
to the script (fixes FRUA's won-vs-fled gap).

## Consequences

- The VM is testable in isolation against a mock `EclHost` (no engine, no UI) — strong unit-test
  surface.
- Opcode tables are derived empirically (disassemble real `DATA`/`ECL*` blocks, cross-check
  against observed game behavior and GBC ECL-Monitor traces). This derivation is the gating
  research task and is sliced explicitly in ROADMAP.
- Adding a new game's scripting = authoring a new dialect table, not new VM code.
- The same VM serves play, the Companion ECL inspector, and authoring playtest — one
  implementation, three consumers.
- COAB may be consulted for *understanding* Gen1 semantics; no code is copied (ADR-0001).
- A user decision (email Simeon Pilgrim for a license grant) could let us cross-check against
  COAB more directly — flagged in ROADMAP.
