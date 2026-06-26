# CODE 14 wall — area-map render + combat-field tier

**The running worklist for the CODE 14 segment.** Update the status columns
in the same commit as each lift — this file is the duplicate-work check:
consult it (and `git grep -n 'static.*\bjtNNN(' src/engine/boot.c`, plus the
dual-name check against `data/work/disasm/jumptable.txt`) BEFORE lifting
anything here. Regenerate the counts with `python3 tools/seg_audit.py 14`.

Status legend: **LIFTED** real body / `stub` PROBE placeholder in boot.c /
`—`/MISSING no symbol yet. (`tools/jt_progress.py` → `docs/jt-lift-progress.md`
is the canonical classifier.)

## What CODE 14 actually is — TWO halves

CODE 14 is the **area-map renderer**, and it serves two callers:

1. **Overland / wilderness map view** — `jt521` scroll/redraw + its helper
   tree. **DONE** (task #111). This is the out-of-combat automap.
2. **The combat-field tier** — the tiles, actor sprites, attack messages and
   targeting UI drawn while a fight plays out **on the same 50×25 tile grid**.
   This is the remaining 21 JT entries (+ ~18 lXXXX render leaves), and it is
   **the bulk of CODE 14's pending work**.

**Snapshot (`seg_audit.py 14`):** 71 functions, 36 lifted (50%), 35 remaining
— of the 44 *called* JT entries, 23 done / 21 pending (5 stub, 16 missing).

### The dependency that gates ALL of it

Almost every pending CODE 14 entry is called from **CODE 13** (the combat main
loop `jt511` / `l076e` / per-round `l0434`/`l102a` / teardown `l0006`) or
**CODE 18** — NOT from the overland path. Traced callers:

| pending | called from |
|---|---|
| jt512/517/518/520 | C13 combat loop + C18 L0006/L22be (teardown) |
| jt533/534/535/536/540/542/544/547/549/550/551/552/553/554 | C13 combat-loop locals (L16ae/L52be/L102a/L059a/…) |
| jt548 | C7 L26a6 (the list dialog — "cast on whom") |

**So this tier is gated on the CODE 13 combat main loop being live, and it
overlaps `docs/code16-wall.md` / task #115.** `l076e` (the per-actor turn,
~2.2KB) is still a stub, so none of these render at runtime yet — lifting them
is **breadth-first** (faithful but untested until the loop runs). Do the CODE
13 loop wall (code16-wall §1) FIRST, or lift these breadth-first as paired
units with their combat callers.

## DONE — overland map render (task #111) + lifted combat callbacks

- `jt521` (CODE 14+0x6836) overland scroll/redraw + tree: `l6652` scroll-check,
  `l744e`, `l6554` creature-on-map predicate (= jt516), `l6090` redraw base
  cell, `l6520` clamp to 50×25, `l5e9a` party fan, `jt57` creature blit.
- Lifted CODE 14 JT (23): jt513 jt514 jt515 jt516 jt519 **jt521** jt523 jt524
  jt525 jt526 jt527 jt528 jt529 jt530 jt531 jt532 jt537 **jt538** **jt539**
  jt543 jt545 **jt546** jt555. (jt537=beholder eye-rays, jt538=in-combat target
  callback, jt539=crosshair pick, jt546=combat target picker — the combat
  *targeting* callbacks are already lifted; the *field render* below is not.)

## REMAINING — the combat-field tier (21 JT entries)

> **CORRECTION (2026-06-24): the rows below were substantially mislabeled.**
> The combat field's actual TILE + ACTOR draw is **jt521 / jt523 / jt57 —
> already LIFTED** (it reuses the overland 50×25 render). The functions this
> section called "actor draw" are NOT the visible draw:
> - **jt512** (0x5d8e) is **faithfully EMPTY** on the Mac (two `rts`) — its
>   PROBE stub is already correct; nothing to lift.
> - **jt517** (0x5d92) is a small **−27540 combat-slot accessor** (reads the
>   2-byte grid entry into two out-params; returns reached?1:0), not a renderer.
> - **jt536** (0x2cb2) is the **party HP-bar ratio** metric (-22649) — **LIFTED
>   2026-06-24**, not a "targeting helper".
> - **jt542** (0x5434) is combatant-state **setup** (sets the [383] active flag
>   from [95]/[147], then a pairing pass via the still-unlifted `l5392`).
> - **jt541** (0x0006) is **per-member per-round prep** (clears [198] status via
>   jt515=l6c22) — ~177 lines.
> - **jt520** (0x6de8) is **per-actor combat teardown** (~331 lines).
>
> So the field already RENDERS; the genuine remaining stubs are combat-state
> setup/teardown + the HP gauge, not the core draw. Verify a row against the
> disasm before trusting the descriptions below.

Grouped by role. `calls` = static Mac callsites. Lift each as a paired unit
with the CODE 13 caller that needs it.

### A. Field render — actor sprites, HP/stat overlays, tile redraw

| JT | addr | calls | status | what (from disasm) |
|----|------|------:|--------|--------------------|
| jt512 | 0x5d8e | 2 | — | actor draw on the field (jt57 creature blit + jt1193/jt117/jt120); 3-`rts` prologue then the real body |
| jt517 | 0x5d92 | 2 | — | actor-table walk over the `-27540` combat slots (stride 12) — jt512's sibling/body |
| jt518 | 0x6090 | 1 | — | base-cell redraw sibling (shares the l6090 region) |
| jt520 | 0x6de8 | 2 | stub | combat-field cleanup leaf (jt118 blit, jt52, jt1200 colour-mode) — also in code16-wall |
| jt533 | 0x2dbc | 1 | — | HP/number overlay (`"%s%d "`, jt155 text, jt399 fill, jt491/499/504) |
| jt540 | 0x13e6 | 1 | — | actor move + redraw (jt868 erase, jt492 line, jt880, jt13) |
| jt544 | 0x2d48 | 1 | ✅ LIFTED | side morale/best-defender = max(jt37(member)>>1) over LIVING members (rec[382]) on side jt493(m); NOT a render leaf |
| jt551 | 0x074c | 2 | — | field render leaf (jt472, jt52, jt13) |
| jt553 | 0x09b2 | 2 | — | field render leaf (jt492 line, jt65, jt499) |
| jt554 | 0x10c4 | 2 | stub | small field helper (jt868 only) |

### B. Combat actions — attack resolution + message

| JT | addr | calls | status | what (from disasm) |
|----|------|------:|--------|--------------------|
| jt534 | 0x1184 | 2 | — | **TURN UNDEAD** (`"turns undead..."`, jt870 dice, jt18 format, jt176 paint, rec+19/+64 actor mod) |
| jt535 | 0x0ea0 | 2 | — | **FLEE / escape** resolution (`"Got Away"`, jt492 visibility, jt870 dice, jt877) |
| jt542 | 0x5434 | 1 | ✅ LIFTED | combatant active-flag setup (rec[383] from [95]/[147] bit7) + monster-control break pass over the controlled HD>3 monsters via the l5392 d100 save (NOT a disintegrate ray — that earlier label was wrong) |
| jt549 | 0x5a22 | 2 | — | **sweep / AoE** attack (`"sweeps"`, jt494, jt508 area, jt493 line, jt479) |

### C. Targeting — pick, range, validity, per-round prep

| JT | addr | calls | status | what (from disasm) |
|----|------|------:|--------|--------------------|
| jt547 | 0x2744 | 2 | — | spell-validity gate (`"Camp Only Spell"`, jt595/jt496 spell tables, jt52) |
| jt550 | 0x1956 | 2 | — | target dedup (`"Already been targeted"`, jt600 range, jt508 area, jt30) |
| jt552 | 0x4c90 | 1 | ✅ LIFTED | spell range/target resolver: single-target (spec[6]==0) -> l476e_c14(caster); else seed the burst (l6b40/l6b6a range l6b94 -> jt508) and walk the -19170/-25676 entity list for the first valid l476e_c14 target. Lifted with its deps l476e_c14 (the 396-instr 29-case JT[1] per-spell validity+save-band matrix, named _c14 vs the unrelated l476e) + l6b94 |
| jt548 | 0x44f0 | 1 | stub | list-dialog targeting helper (jt41/jt3/jt1 + jt492) — called from CODE 7 "cast on whom" |
| jt541 | 0x0006 | 1 | stub | per-member per-round prep (code16-wall §1) |
| jt536 | 0x2cb2 | 2 | — | small targeting helper (leaf) |
| jt522 | 0x7488 | 2 | stub | targeting/field leaf |

## THE PHYSICAL-DAMAGE TIER — ✅ COMPLETE 2026-06-24 (all 6 lifted)

A weapon swing currently deals **no damage** — `jt555` → `l14bc`/`l2b24` are
PROBE no-ops, and the real logic lives in a small CODE-14 local tier that was
never lifted (NOT just `l14bc`). Lift bottom-up:

| fn | role | ~lines | status |
|----|------|-------:|--------|
| `l29fc` | backstab eligibility (thief class + behind-target) | 94 | ✅ LIFTED |
| `l022c` | **damage roll** → -25242 (jt873 weapon dice + [393] bonus, ×backstab mult) | 80 | ✅ LIFTED |
| `l030a` | report + **jt39 HP-apply** + death/XP (jt865/l6de8) | 275 | ✅ LIFTED |
| `l1d0c` | reach/out-of-range attack-timing penalty | 76 | ✅ LIFTED |
| `l14bc` | the multi-attack **melee round** loop (jt864 to-hit → l022c → l030a) | 400 | ✅ LIFTED |
| `l2b24` | the missile/thrown projectile animation | 128 | ✅ LIFTED |

Damage flow: `l14bc` loops the attacker's swings; per swing `jt864` rolls to-hit,
`l022c` rolls the damage into `-25242`, `l030a` announces it + applies via `jt39`
+ handles "goes down"/"is killed"/XP. All external deps (jt39/jt864/jt494/jt40/
jt873/jt868/jt865) are lifted. **Damage now COMPUTES (l022c) + APPLIES + resolves death (l030a). Next:
`l1d0c` (attack count), then wire `l14bc`/`l2b24` to call jt864->l022c->l030a.**

## REMAINING — the other lXXXX render leaves (non-JT, 0 callsites in jumptable)

Called by the JT entries above; lift alongside their parent. From
`seg_audit.py 14`:

> stubs: `l1dd6` `l4dee`  (`l1090` now lifted: kind->attacks-per-round halve)
> missing: `l315e` `l37d6` `l3a4e`
> (`l022c`/`l29fc`/`l030a`/`l1d0c`/`l5392`/`l44b2`/`l302c`/`l5c32`/`l660`/`l2e30` now lifted;
> `l2e30` (CODE 14+0x2e30) = the per-target combat action picker — reach cost
> (jt506) -> -24126 option list (jt155 arms 0..5, arm 3 conditional) -> jt182
> picker; 9 args, sole caller L3cac still unlifted;
> `l660` (CODE 14+0x0660, not 0x660) = the attacks-of-opportunity free-swing
> pass over m's reach list (jt492 -> -22720) firing jt550+jt555 per pending
> reactor, parked unused until its sole caller L0984 lifts;
> `l44b2` = missile-trail sprite step, parked unused until l3a4e/jt548;
> `l302c` = single-target attack setup + jt555 dispatch, parked unused until the
> 0x3e54/0x3f0e/0x40b8 targeting dispatchers lift;
> `l5c32` = jt549's reach-target counter, already lifted — verified faithful
> 2026-06-25; note jt525≡l6b40, jt531≡l6b6a, jt513≡l6b94 jt5xx/lXXXX aliases)

`l1dd6` (repeat pick from the built area list) and `l4dee` (repeat pick with
per-target area re-aim, jt508) are the combat target-repeat locals already
listed in code16-wall §1.

## Worklist discipline

1. Prefer to land the CODE 13 combat main-loop wall (code16-wall §1: `l076e` /
   `l0434` / `l102a` / `l4f22` / `l0116` + `jt511`) before relying on this tier
   rendering — otherwise these are faithful-but-untested breadth-first lifts.
2. Pick a row. `git grep -n 'static.*\bjtNNN(' src/engine/boot.c` (duplicate
   check) + the dual-name check vs `jumptable.txt` for every local the disasm
   names (CODE 14 has form-`jt5xx`/`lXXXX` collisions — e.g. jt516≡l6554).
3. Lift, gate (`make`, codegen grep `muls.l|bfextu|bfins`, `make -s test`),
   update this file's status column, commit both together.
4. Re-run `python3 tools/jt_progress.py` so the segment counts stay honest.
