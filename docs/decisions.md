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

## ADR-0006 — Editor UI reimplemented inside the Toolbox shim

**Decision:** The Mac Menu, Window, Dialog, Control, and TextEdit managers are
reimplemented within the `compat/` shim, drawing Mac-style widgets into the
HAL surface. The decompiled design-tools code calls them unchanged.

**Why:** Consistent with ADR-0003 (shim-first) — it keeps the decompiled
editor verifiable against the original and needs no call-site rewrites. The
design tools are not a performance hot path, so a software-rendered UI is
acceptable. Mac Dialog Manager semantics (DITL item lists, modal filter procs,
TextEdit) do not map cleanly onto GEM AES, and an AES mapping would tie the
editor to GEM and to an Atari look-and-feel. One implementation also behaves
identically on the Falcon and the TT.

---

## ADR-0007 — Resource fork delivered as a flat archive

**Decision:** A host-side tool (`tools/rsrcpack`) packs every resource from the
Mac resource fork, keyed by `(type, id)`, into a single indexed archive file.
The Resource Manager shim in `compat/` serves `GetResource()` and friends from
that archive at runtime.

**Why:** Atari filesystems have no resource fork. A single archive handles all
resource types (`DLOG`/`DITL`/`MENU`/`PICT`/`snd`/`STR#`/custom game-data
types) uniformly, keeps the Resource Manager API faithful (consistent with
ADR-0003), and sidesteps the TOS 8.3 filename limits a file-per-resource
scheme would hit — a 4-character type plus an ID does not encode cleanly.
Build-time native conversion can still happen later, per resource type, where
it pays off.

---

## Working assumptions (not yet ratified — confirm or amend)

- **Scope:** the full *Unlimited Adventures* package — the design/editor tools
  **and** the runtime that plays UA modules.
- **Lead target:** Falcon030 first, TT030 as a close follow-on.
- **Audio:** Falcon030 has DMA sound + DSP56001; the TT030 has only the YM2149
  PSG (no DMA sound). The audio HAL must degrade gracefully on the TT.
