# Enhancements / pre-release audit

Audit run 2026-07-14, ahead of the first tagged release. **Measured, not
guessed** — every gap below was either produced by a tool or reproduced live in
the emulator. Where I have not verified something, it says so.

## Verdict

The runtime is **structurally complete but not feature-complete.** It plays a
real commercial module (Pool of Radiance) end to end on its own art — walk,
events, temple, shop, area transfers, level loads, the automap, save/load, and
combat through to a party wipe. But **two of the eight exploration commands are
dead buttons**, so this is an honest "playable beta", not a 1.0.

## ★★ The measurement that lied — and the tool gap behind it

`tools/stub_audit.py --stubs` reports:

```
2166 functions, 55 stub bodies
  0 LIVE GAPS  (lifted code calls them)
 35 faithful no-ops (the Mac body is empty too — leave them)
 20 uncalled gaps
  0 stale stub claims
```

`tools/jt_progress.py`: **top-100 JT entries 100/100.**

**"0 live gaps" does NOT mean feature-complete, and I nearly shipped on it.**
`stub_audit` counts *PROBE stub bodies*. An unimplemented **empty switch arm** is
not a stub:

```c
case 2:                 /* Cast — TODO: L06d6 */
        break;
```

It is invisible to the audit, it calls nothing, and it silently does nothing at
runtime. **CAST and INV are both this.** ★**TOOL GAP: stub_audit should also flag
switch arms whose body is empty-or-TODO.** Until it does, the JT/stub numbers
overstate completeness — treat them as a floor, not a verdict.

## P1 — player-visible, blocking a 1.0

| # | Gap | Where | Status |
|---|---|---|---|
| 1 | **CAST does nothing** | `jt953` case 2 -> `L06d6` unlifted | dead button on the command bar |
| 2 | **INV does nothing** | `jt953` case 7 -> `L3b80` unlifted (the arm calls `jt23`, a frame redraw, not the inventory screen) | dead button on the command bar |
| 3 | Default command arm | `jt953` default -> `JT[936]`/`JT[934]` | unhandled |

Working and verified live: MOVE, AREA (automap), VIEW (character sheet), ENCAMP,
SEARCH, LOOK. So **6 of 8**.

**Fixed during this audit:** clicking CAST — a command that does *nothing* —
**corrupted the play screen** (blank roster, blank clock, three stray FRAME
plates). The corruption was not the command: `l63c0`'s re-entry never force-fulled
the repaint unless an event had fired, so *any* command taking that exit did it
(`6afc38a`). Worth remembering: **an unimplemented feature was masking a rendering
bug that affected implemented ones.**

## P2 — fidelity / polish

- **Smooth-scroll + move sound** (`L4900` / `L423e` / `L3998`) — the port hard-jumps
  the view cell per step instead of animating the walk. Deferred, cosmetic.
- **`jt303` design-name line** — blocked on an argument-order audit of `jt406`
  (the port's `jt406(dst,src)` is REVERSED vs the Mac's `BlockMove(src,dst)`;
  calling it faithfully today would corrupt the level header). **Fix jt406's arg
  order across its call sites first** — this is a latent-corruption hazard, not
  just a missing line.
- **Drow-gear-dissolves scan** (`l5676`, `ev[12]` bit 3) — unimplemented event arm.
- Per-step cell-change / redraw-hint arms (`l63c0`, `0x43f8..0x445c`).

## P3 — art / data formats

- **Drawing method 23, DOS sweep layout: UNSOLVED.** The Mac/CTL linear layout is
  solved and confirmed; the DOS 4-plane sweep puts the right rows and the right
  opaque-pixel count in the wrong columns. Ground truth is staged. Tried and
  rejected: `x = p + 4c`, `x = 22p + c`, both sweep orders, both skip scalings.
- **Method 25** (image-ID list) and the **CBODY / COMSPR** entry class in
  `art_convert`.
- **`TITLE.CTL`** converts differently (nested container).
- **Custom module music (`.XMI`)** — a whole unstarted subsystem. Needs a decision
  before anyone promises it.
- `art_convert`'s CLI relies on a shell glob — **mixed-case files (`Pica1003.tlb`)
  are silently skipped.** Pass `*.TLB *.tlb`.

## P4 — engineering / release hygiene

- **20 uncalled gaps** (see `stub_audit --stubs`) — nothing lifted calls them, so
  they gate no behaviour. Lift on demand.
- **35 faithful no-ops** — the Mac body is empty too. **Leave them alone**; they are
  not work.
- **Behaviour-altering build flags must never ship**: `FRUA_AUTOWIN` (instantly
  kills the monster side), `FRUA_SKIP_ENTRY_EVENTS`, `FRUA_CORRIDOR` / `FRUA_RAYCAST`
  (alternate renderers), `FRUA_SHIM_DEMO`. All are opt-in and none is on by
  default; `make release` now hard-`#error`s if one is enabled.
- **The debug click crosshair shipped for months** (`2f3afe7`) — it is in nearly
  every screenshot of this port. Now behind `-DFRUA_CLICKMARK`, off by default.
  *Audit what the default build actually draws, not what you think it draws.*

## ⚠️ Verification gap — read before tagging

**Everything in this project has been verified in Hatari. Nothing has ever been
run on real Falcon030 or TT030 hardware.** Emulator fidelity is good but not
total (timing, VIDEL edge modes, real DMA sound, real disk). The release notes
must say "emulator-validated" until someone boots it on iron.
