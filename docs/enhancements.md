# Enhancements / pre-release audit

Audit run 2026-07-14, ahead of the first tagged release. **Measured, not
guessed** — every gap below was either produced by a tool or reproduced live in
the emulator. Where I have not verified something, it says so.

## Verdict

The runtime is **structurally complete but not feature-complete.** It plays a
real commercial module (Pool of Radiance) end to end on its own art — walk,
events, temple, shop, area transfers, level loads, the automap, save/load, and
combat through to a party wipe.

**Update 2026-07-14 — all 8 exploration commands now work.** CAST and INV were
the two dead buttons; `624ff7b` lifted `L3b80` and repointed `L06d6`. A full
outfitting run then went through end to end on HEIRS: roll six characters, take
the caravan purse, buy banded mail + helm at one shop and a battle axe at
another, ready all three, and watch the sheet move **AC 10 → 2** and **DAMAGE
1D2+1 → 1D8+1** — persisting across a save/load round-trip. Still a "playable
beta" (see the verification gap below), but no longer one with dead buttons.

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
| 1 | ~~CAST does nothing~~ | `jt953` case 2 -> `L06d6` | ✅ FIXED `624ff7b` |
| 2 | ~~INV does nothing~~ | `jt953` case 7 -> `L3b80` | ✅ FIXED `624ff7b` |
| 3 | Default command arm | `jt953` default -> `JT[936]`/`JT[934]` | unhandled |
| 4 | ~~CAST/INV/shop messages are invisible~~ | — | ⛔ **RETRACTED — NEVER TRUE.** See below |

All 8 commands work: MOVE, AREA (automap), CAST, VIEW (character sheet), ENCAMP,
SEARCH, LOOK, INV. **P1 is empty apart from gap 3.**

## ⛔ The "message overpaint" bug DOES NOT EXIST — the harness was hiding it

Carried for months as a known defect ("`jt42` writes the narrative band and the
`jt23` repaint overpaints it"). **It is not real.** Every message displays:

| trigger | message | where |
|---|---|---|
| CAST, as a fighter | `BORIS CANNOT DO MAGIC` | row 24 (`l05c4` -> `jt18`) |
| INV, no special items | `EMPTY!` | row 24 (`l3b80` -> `jt42`) |
| shop BUY | `PIOUS BUYS 20 ARROWS` | shop footer (`jt42`) |

Each paints, **dwells ~1s** (`l4bac` -> `jt476`, fed by the design's text-speed
table at `-17518`; HEIRS' speed byte 4 -> dwell 1000), then the screen repaints
and the command bar restores. That is the faithful transient-message cycle.

**★★ The bug was in how I was LOOKING.** `driver.sh shots` waits for the frame
to *settle* before grabbing — so it skips transient text **by construction**. It
is the right tool for a static screen and exactly the wrong one for a message
that exists for one second. Every "the message never appears" observation came
from a settled-frame grab taken after the message had already gone.

**Use `driver.sh shot` (immediate) within ~1s of the keypress to see a message**,
and `-DFRUA_MSGTRACE` to log the exact string `jt42` was handed plus the dwell
value `l4bac` computed. A tool that silently drops the thing you are hunting is
worse than no tool — see also the STEP-log row/col swap (`0429d4e`).

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
