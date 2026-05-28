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

## ADR-0008 — Runtime before editor

**Decision:** Port the play-UA-modules runtime first; bring up the design
tools as a second phase. Scope is unchanged — the full *Unlimited Adventures*
package remains the goal.

**Why:** The runtime exercises far less of the Mac Toolbox than the
dialog-heavy editor and can reach a playable state before the Menu/Window/
Dialog/Control/TextEdit shim work of ADR-0006 is needed. Front-loading the
runtime de-risks the port: it proves out the decompilation, the Toolbox shim,
and the display/input/audio HAL on the smaller surface first.

---

## ADR-0009 — Scripted objdump-based disassembly

**Decision:** Disassembly is produced by `tools/dis68k.py` driving the
`m68k-atari-mint-objdump` from the project toolchain (ADR-0004). It emits
annotated listings under `data/work/disasm/`, which is git-ignored. The
committed work product is the hand-lifted C in `src/engine/`, not the
disassembly.

**Why:** Chosen over interactive Ghidra. A scripted pass is reproducible and
diffable, needs no toolchain beyond the binutils already required, and
resolves the classic-Mac jump-table and trap model in a way tailored to this
binary. Keeping the raw disassembly out of git — it is the original
copyrighted program in another notation — keeps the repository to the port's
own code and tooling, consistent with the `data/` policy.

---

## ADR-0010 — Engine bring-up PROBE-lift phase complete; remaining work
gated on HAL

**Decision:** Treat the engine bring-up phase (lifting PROBE-only stubs in
the boot path) as structurally complete. Remaining unlifted bodies are
gated on host-facing infrastructure that doesn't exist yet (Falcon display
HAL, input HAL, audio HAL, Palette Manager shim, font cache). Land that
infrastructure before resuming the per-function lifting.

**Why:** A 15-second probe boot in Hatari now generates ~2000 PROBE log
lines across 117 unique labels, finishes without bus or address errors, and
exercises every phase of the dialog event loop. Every event arm of L725c
(Mac event-pump dispatcher) routes through a lifted handler; the dialog
loop (L2d3e) iterates 30 times correctly; the base DLItem handler L1676
fires 222 times with all command arms covered; the text-bounds chain
(JT[1005] → L2856 → jt1135 → jt1200 → jt397 / jt413), the InvalRect
dispatcher (L4d88), the event-pump prelude (jt1134 + L731e + L66e8 +
L6538 + L62fa + jt1118 + L31ea + L3198), and the no-hit feedback chime
(jt1080) are all lifted. Probe counts:

| Function | Calls | Role |
|----------|-------|------|
| L1676    | 222   | DLItem base handler |
| jt382 etc| 60-92 | Shape handler hit-tests (cmd=2) |
| L4d88    | 60    | InvalRect dispatcher |
| L6804    | 60    | Front-window probe |
| jt397/413| 60    | min/max in text-bounds chain |
| jt468    | 34    | Resource-handle lookup |
| jt376/1200/1153 | 31 | Boot-time leaves |
| L725c / jt1134 / L2d3e / L31ea / L3198 / L66e8 / L6538 / L62fa / L731e / jt1118 / jt1005 / L2856 | 30 | Event-pump + dialog loop |
| jt452    | 14    | DLItem stream installer |
| jt444    | 12    | DLItem dispatcher |

What's *not* lifted falls into five buckets, each blocked on prerequisite
infrastructure:

1. **Display HAL** — `L4fae` / `L4e12` are 200+-line text-paint routines
   (SetPort + StringWidth + character-class table at g_a5_-3016 + EraseRect
   / PaintRect / MoveTo / DrawString). `L309c` is a 200+-line scaled
   bitmap blit. `L3e38`'s deep blit arm walks page descriptors at
   g_a5_-2570 and BlockMoves into the page's bits pointer. All wait on a
   Falcon-side pixel destination and font metrics. L3d8c, jt1084, jt1064,
   L448c, L4350, l24aa, L24aa (palette restore), l3e38 page-walk all sit
   downstream of this.

2. **Input HAL** — `jt1132` (mouse poll) stays PROBE-only with zeroed
   coords and "button released" return. Until Hatari mouse / keyboard
   events route through `platform/input.c`, the `L725c` mouseUp / mouseDown
   / keyDown arms never fire and `cmd=3` mouse-track / `cmd=4` action /
   `cmd=5` keyboard-select arms of L1676 stay dormant. `GlobalToLocal` is
   skipped from compat (single-window engine, coords already local).

3. **Audio HAL** — Currently no boot path drives audio. Trap calls in the
   asm (_TickCount aside) don't reach our code; jt1122 turned out to be a
   menu-bar slot setter, not the audio gate we initially guessed. Real
   audio plumbing (Falcon DMA / DSP, TT YM2149 fallback per ADR-0010's
   audio assumption) waits on the runtime phase consuming UA module SFX
   tags.

4. **Palette Manager shim** — `L24aa` is a 700+-line Palette Manager state
   restore via _PMForeColor. Without the Palette Manager in the compat
   shim or a direct VIDEL bridge, the function stays PROBE. Only L71ac /
   L7204 osEvt resume paths reach it.

5. **Resource manager + module loader** — The runtime per ADR-0008 needs
   the engine to read UA modules from disk and walk record graphs. Stubs
   like `jt361` (loads "GAME"), `jt81` (loads module 51), `l0444` (opens
   "start.dat"), `jt449` (rebuilds shape-handler table), `jt938` /
   `jt942` / `jt918` sit on this. The PROBE labels fire once each during
   boot's resource-touching prelude.

A handful of small surprises landed during bring-up that future readers
should know:

- **`jt1134`'s "dead arithmetic"** wasn't dead — d0 holds `(elapsed * 6) /
  5` after rts, which `jt1080` reads as the idle-adjusted tick counter.
  Signature corrected from void to long.
- **DATA-blit pre-loaded values** in g_a5_-1316 (0x05 idle flag), -820 /
  -810 / -809 / -814 (event cluster), -126 / -130 (idle ticks) must be
  zeroed in `boot_a5_seed_defaults`. The Mac runtime drained these via
  pre-main init we don't run. Without the explicit zero, l31ea reports
  "keep polling" every iteration and L2d3e's Phase 5 walks every DLItem
  cmd=5 method (L1676 spikes from 222 to 887 calls).
- **`l747a` signature** was originally `(void*, short, short)`; corrected
  to `(void*, long, long)` after asm analysis showed 12-byte caller
  cleanup.
- **`L7690` ≡ `JT[1122]`** — same function, two naming conventions.
  Consolidated.
- **Misnomers corrected:** L24aa = palette restore (not "menu repaint"),
  L309c = scaled bitmap (not "channel write"), L4fae / L4e12 = text paint
  (not "rect builders"), jt1080 = menu-slot blink feedback (not audio
  "chime"), jt1122 = menu-bar slot setter (not "audio gate").

**Consequences:**
- Future PROBE-lift sessions targeting the boot path should pause until
  Display / Input / Palette / Audio HAL or the resource manager lands.
  Lifting in isolation produces PROBE-stub helpers that don't help — the
  meaningful next steps go through HAL.
- The 117 distinct probe labels in the current trace are a usable
  regression fingerprint: changes that move major counts (L1676 = 222,
  L2d3e = 30, jt1134 = 30, L4d88 = 60, L6804 = 60) should be investigated.
  `tools/run_probe.sh` is the gate.
- When the HAL phases land, the dormant arms (L690e cases 3/5/6 + drag/
  zoom, L6dd0 Cmd-key + key-map, L71ac / L7204 suspend/resume, L7090
  updateEvt + L3e38 repaint, L70e0 activateEvt) come alive without
  further engine-side lifting.

---

## Working assumptions (not yet ratified — confirm or amend)

- **Scope:** the full *Unlimited Adventures* package — the design/editor tools
  **and** the runtime that plays UA modules.
- **Lead target:** Falcon030 first, TT030 as a close follow-on.
- **Audio:** Falcon030 has DMA sound + DSP56001; the TT030 has only the YM2149
  PSG (no DMA sound). The audio HAL must degrade gracefully on the TT.
