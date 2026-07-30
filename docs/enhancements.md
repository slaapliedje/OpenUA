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

`tools/stub_audit.py --stubs` reports (2026-07-29, after #103's parser fixes
and #105's triage):

```
54 stub bodies
  6 LIVE GAPS  (lifted code calls them — all reachable from a live root)
 37 faithful no-ops    (the Mac body is empty too — leave them)
  4 platform rulings   (NOT-A-GAP: — printed with their reason; verify the gate)
  7 uncalled gaps
  0 stale stub claims
```

**The six real ones, with why they matter** (#105 walked each caller chain):

| stub | reachable from | consequence |
|---|---|---|
| `l1240` | `jt290 ← … ← l28d4 ← jt243 ← l0096` | **the map editor cannot EDIT** — the wall-pencil click returns 1 and writes nothing (~1.4KB) |
| `l0ee6` | same chain | locked-map click selects/moves nothing (~860B) |
| `l341a` | `jt392 ← … ← jt315` | every "save as" picks nothing. The "no GEMDOS equivalent" excuse expired in 0.5.8 — the GEM selector exists now |
| `l4e8a` | `jt230 ← jt325_tail ← jt325` | every record editor answers "not found" to the −13038 lookup (#88's table) |
| `l501e` | `jt226 ← jt325_tail ← jt325` | "scroll list to row n" does nothing. Read with #87: that stored the position, this would move it |
| `jt1150` | `jt295 ← l1908` (play walk) | mark-rect-dirty. Probably SUPERSEDED by `platform/`'s own dirty tracking — check whether anything consumes the Mac list before lifting |

★ **THE "0 LIVE GAPS" THIS SECTION USED TO QUOTE WAS PARTLY AN ARTEFACT.** Two
bugs in the tool, both fixed in #103 with regression tests:

1. `calls = hits - decls - 1` used `[^;]*` in the forward-declaration pattern,
   and `[^;]` matches newlines — so on a MULTI-LINE definition the pattern ran
   from the signature into the body and stopped at the first statement's `;`,
   counting the definition as its own declaration. Every multi-line stub with
   exactly ONE caller came out at calls = 0 and was filed as **uncalled**
   (harmless) when it gates real behaviour. That is where 12 of the "uncalled"
   went. Excluding `{` as well as `;` separates the two forms.
2. `doc_for`'s shared-header walk skipped blank lines, forward declarations and
   ONE-LINE definitions, but not a multi-line sibling body — so `l493a` looked
   undocumented under the header it shares with `l4932` and was reported as the
   single remaining live gap. It is a genuine no-op (`linkw/unlk/rts` at CODE 21
   0x493a, re-read for #103).

★ **The first count of this was 12, and 12 was wrong too** — in the other
direction. #105 resolved it: **one row was a phantom** (`status` was keyed by
NAME, and `fc_cache_audit` has two definitions under opposite arms of one
`#ifdef`, so the REAL body inherited the empty one's STUB verdict and was
reported as a gap that does not exist); **`l0004_c6` was lifted** (15
instructions — it STRIPS the 17 filename-hostile characters at A5 −31268 out of
a design name; the old comment called it an "append", which is why it read like
a deferrable leaf); and **four are platform rulings**, now tagged `NOT-A-GAP:`
with their gate — `l7de0` (a `_Gestalt` probe for selector `'SAVR'`, trap
`$A1AD`, with no Gestalt to ask), `jt1052` (`_Eject`, nothing ejectable),
`l0370` (gated off by `g_a5_2347 != 0`), `fc_cache_audit` (a debug self-test's
compiled-out arm). Do not quote a live-gap figure from this file — rerun the
tool.

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

### ✅ TOOL GAP CLOSED 2026-07-24 — `stub_audit.py --arms`

The missing half now exists. `--arms` walks every `case`/`default` arm in
boot.c, keeps the ones whose body is empty, and splits them by what the arm
says about itself. Two filters do the real work, and both were needed before
the signal was readable:

- **Fallthrough groups are not gaps.** `case 16: case 17: case 18: do();` has
  two "empty" arms that are ordinary C. An arm counts only if it ends in its
  OWN `break` (or the switch's end); running into the next label with no break
  is a fallthrough and is skipped. This alone dropped 330 raw hits to 236.
- **An empty `default:` is not a gap either** — it is the catch-all that
  deliberately does nothing. 160 of them, reported as a count, not a list.

**Result on the current tree: 0 deferred arms, 0 bare `case` arms.** The one
deferred arm it found (`jt251` case 5) is lifted — see the P2 entry below. The
two bare cases it found (`jt297` case 0 = no key, `jt601` case 1 = "self only")
were both legitimate and now carry a one-line comment saying so.

So the arm count can be read as a verdict rather than a floor, because a second
measurement backs it. **The live-gap count could not** — see the ★ above: it was
depressed by a call-counting bug until #103, which is a reminder that "a second
measurement" only helps when the two measure different things. `--arms` and
`--stubs` shared no code, so `--arms` never cross-checked `--stubs`' arithmetic. `--arms` exits non-zero
when a deferred arm exists, so it can gate CI the way `--quiet` does.

## P1 — player-visible, blocking a 1.0

| # | Gap | Where | Status |
|---|---|---|---|
| 1 | ~~CAST does nothing~~ | `jt953` case 2 -> `L06d6` | ✅ FIXED `624ff7b` |
| 2 | ~~INV does nothing~~ | `jt953` case 7 -> `L3b80` | ✅ FIXED `624ff7b` |
| 3 | ~~Default command arm~~ | `jt953` default -> `JT[936]`/`JT[934]` | ✅ FIXED — the roster cursor; you could not change the active character from the play screen |
| 5 | ~~NO spellcaster could ever CAST~~ | ADD-character dropped `jt587`'s `jt21`+`jt910` tail -> `jt908` never ran -> spell capacity all zero | ✅ FIXED — see `docs/inventory-subsystem-wall.md` |
| 4 | ~~CAST/INV/shop messages are invisible~~ | — | ⛔ **RETRACTED — NEVER TRUE.** See below |

All 8 commands work: MOVE, AREA (automap), CAST, VIEW (character sheet), ENCAMP,
SEARCH, LOOK, INV. **P1 is now EMPTY.**

**Magic is verified end to end (2026-07-14).** Full loop proven live on HEIRS:
capacity grant (`MAGIC-USER : 4 2 2`) → grimoire → memorize (consumes a slot) →
rest → **SPELLS IN MEMORY** → cast. `DETECT MAGIC` cast in camp prints
**"ZOLTAN IS AFFECTED"** and is removed from the memorized list; `MAGIC MISSILE`
offers **"CAN'T BE CAST HERE… LOSE IT?"** (faithful — it is a combat spell).

⚠️ Gotcha that cost a session, now understood: **rest only completes in a
rest-permitting ZONE.** `l473e` reads the party cell's zone
(`jt197 = cell[5] >> 2 & 7`) and `ds[zone*4+49]` bit 7 = "no resting here" →
`hdr[44]=100` → `jt957` drops the REST/FIX rows. HEIRS' town is mostly
rest-hostile (zone 7 = 109 cells no-rest; zones 1/2 = 54 cells guard-interrupt);
only zone 0 (198 cells, e.g. **(11,9)**) rests cleanly. All faithful. Build
`-DFRUA_CELLSCAN` to see each zone's rest rule and the party's current zone.

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
- ~~**`jt303` design-name line**~~ — ✅ DONE. The full `jt406` argument-order
  audit ran 2026-07-14: all **141** call sites asm-cross-checked. `jt303` is
  restored (the faithful call is the *swap* `jt406(buf, level+118, 16)`, which
  the earlier deferral over-cautiously feared). One genuine reversal fixed
  (`l53b0` prologue, dormant tile converter); two `l4842` sites (dormant editor
  map-resize) flagged as a copy-DIRECTION divergence needing a Mac-trace re-lift.
  See the banner on `jt406`'s definition and commit `62527d3e`.
- ~~**`l4842` map column-resize**~~ — ✅ DONE 2026-07-24. The flagged
  copy-direction divergence was an argument-role misreading: l4842's rr/cc are
  the OLD dims (the caller snapshots them before the settings editor writes
  the new dims into the header), so the Mac's directions are correct —
  front-to-back compacts a narrowing (dst trails src), back-to-front spreads a
  widening (dst leads src, prior row's tail gap zeroed each pass; row lim-1's
  own gap stays unzeroed, a faithful quirk). The scans collect events from the
  REMOVED region (the loss warning). Re-lifted to the asm's pointer setup and
  pinned byte-exact across grow/shrink/mixed + overlap-hazard widths by
  `tests/test_l4842_reshape.py`.
- ~~**`jt251` case 5 — the mode-5 redraw-hint pack**~~ — ✅ DONE 2026-07-24,
  the last deferred switch arm in the engine. The old deferral blamed "a CREL
  reloc the disassembler couldn't tell apart" for the two `A5@(-12300)` loads
  at 0x441e/0x4422. **That was a misdiagnosis**: CODE 2's 59 CREL relocations
  all target ABSOLUTE operands and dis68k annotates every one (`reloc
  STRS+0x2b46`); these two carry no note, so both really are -12300 and the
  subtraction is a literal `G - G` = 0. Nothing was unresolved. Confirmed
  independently by `jt253`, which runs the byte-identical block at 0x4784 and
  lifted it the same way months ago. The arm folds `master[12]`'s low byte into
  `*flagsp`, shifts the word down 8, and encodes the remaining cell index in
  three 32-cell bands (512 / 768 / 1024 in bits 8..10). **Not live-exercised**:
  mode 5 is the hand-off to `jt250`, and the Game Settings interactions
  drivable headlessly resolve to modes 1 and 11 (traced with
  `-DFRUA_MODE5TRACE`) — correct by construction and by the sibling precedent,
  but the arm itself has not been seen to fire.

Done (2026-07-24): the drow-gear-dissolves scan (`l5676`, `ev[12]` bit 3) is
lifted and live-verified via the `FRUA_DROWTEST` harness (plants a class-62
item, fires a synthetic type-11 event; DBG.LOG proves the one-item destroy).

Done (2026-07-24): the l63c0 cell-change / hover-cursor arms (asm
L64f2..L666c) — the editor's live mouse tracking (jt272/jt284 hit-test,
jt312/jt280 redraw, l4268 restore). Gated `g_geo_editor_active`: the
hit-tests write the party position from the pointer, which is editor
semantics — the Mac never ran l63c0 in play. Live-verified in the Map
Editor: the cell cursor follows the pointer, cell-to-cell moves restore
the old cell, leaving the map restores fully.

## P3 — art / data formats

All of the format work below closed during the DOS-stack sprint (2026-07-18/19);
this section was stale until 2026-07-24 and is kept as a pointer to the proofs.

- ~~**Drawing method 23, DOS sweep layout**~~ — ✅ SOLVED (commit `33302f8a`):
  sweep `p` starts at `x = p + 1`, literals step 4, skip byte `v` advances
  `4*(256 - v)` (DRAW23.TXT carries two off-by-ones). Proven by re-encoding
  SSI's own DOS streams byte-exactly (57/62; the 5 outliers are the corpus's
  35 `x == W` encoder-artifact pixels). Pinned by the `test_m23_*` trio in
  `tests/test_art_convert.py`; the C converter carries the same law.
- ~~**Method 25** (image-ID list) + **CBODY / COMSPR**~~ — ✅ done in both
  converters (id-list u16 swap `0a4d9273`; type-128 composite table, AND/OR
  mask pairs, `cbod`/`coms` planar classes).
- ~~**`TITLE.CTL`** nested container~~ — ✅ done (`8719aa48`, nested
  PIC*/SPRIT/TITLE frames re-encode; the Mac-only 0xc3 composite codec
  followed in `2c01aab6`, 121 entries content-identical).
- ~~**Custom module music (`.XMI`)**~~ — ✅ shipped: `tools/xmi2slb.py` +
  `tools/voc2glb.py` (the ADR-0017 DOS audio path, ear-verified).
- `art_convert`'s CLI relies on a shell glob — **mixed-case files (`Pica1003.tlb`)
  are silently skipped.** Pass `*.TLB *.tlb`. (Still true.)

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
