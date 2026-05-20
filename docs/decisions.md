# Architecture Decisions

Decisions are append-only. Supersede an old one with a new entry rather than
editing it in place.

---

## ADR-0001 — Port from the Macintosh 68k release

**Decision:** Base the port on the Macintosh release of FRUA, not the MS-DOS
release.

**Why:** The Mac version is already 68k machine code. The Atari Falcon030 and
TT030 are 68030 machines, so the CPU instruction stream carries over directly.
The porting effort collapses to retargeting the OS layer (Mac Toolbox → Atari
TOS/GEM/XBIOS) and the display/sound paths, instead of an x86 → 68k rewrite.

---

## ADR-0002 — Hybrid C + 68k assembly decompilation

**Decision:** Decompile game logic to recompilable C; keep original 68k
assembly for routines that do not lift cleanly.

**Why:** Pure C is the most maintainable long term but a full clean
decompilation is a large up-front cost. Keeping everything as raw asm boots
fastest but is hard to maintain and diff. The hybrid path gets a running build
sooner while letting the codebase converge toward C subsystem by subsystem.

---

## ADR-0003 — Mac Toolbox compatibility shim first, native later

**Decision:** Implement a Mac Toolbox compatibility layer (`compat/`) so the
decompiled code can call QuickDraw / Resource / Memory / Sound / File Manager
APIs unchanged. Migrate hot paths to native Atari APIs afterward.

**Why:** Keeping original call sites intact lets the decompiled output be
verified against the original behaviour, and gets the game running before any
rewriting. Native rewrites then happen incrementally where performance demands.

---

## ADR-0004 — m68k-atari-mint GCC cross toolchain

**Decision:** Build with the m68k-atari-mint GCC cross toolchain on Linux.

**Why:** Best-in-class debugging, CI integration, dependency tracking, and
mixed C + asm support. Cross-building is faster to iterate than native or
emulator-hosted builds.

---

## ADR-0005 — Hardware abstraction layer for display

**Decision:** Route all rendering through a thin display HAL (`platform/`)
with selectable backends: VIDEL (Falcon030), TT-shifter (TT030), and a later
VDI fallback.

**Why:** The Falcon and TT have different video hardware. A single
engine-facing surface API keeps the decompiled engine machine-agnostic and
isolates the per-machine code to one swappable backend.

---

## Working assumptions (not yet ratified — confirm or amend)

- **Scope:** the full *Unlimited Adventures* package — the design/editor tools
  **and** the runtime that plays UA modules.
- **Lead target:** Falcon030 first, TT030 as a close follow-on.
- **Audio:** Falcon030 has DMA sound + DSP56001; the TT030 has only the YM2149
  PSG (no DMA sound). The audio HAL must degrade gracefully on the TT.
