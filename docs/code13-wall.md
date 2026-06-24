# CODE 13 wall — the combat MAIN LOOP + per-turn/per-round tree

**The running worklist for the CODE 13 segment — the heart of combat.**
Update the status columns in the same commit as each lift — this file is the
duplicate-work check: consult it (and `git grep -n 'static.*\bjtNNN(' / '\blXXXX('
src/engine/boot.c`, plus the dual-name check vs `data/work/disasm/jumptable.txt`)
BEFORE lifting anything here. Regenerate counts with `python3 tools/seg_audit.py 13`.

Status legend: **LIFTED** real body / `stub` PROBE placeholder / `—`/MISSING no
symbol yet. Canonical classifier = `tools/jt_progress.py` → `jt-lift-progress.md`.

## What CODE 13 is — the combat orchestrator (mostly LOCALS, not JT)

CODE 13 holds two things:
1. **The `jt490`–`jt509` area-map LINE/REGION renderer** (Bresenham line, AoE
   outlines, region fills) — **DONE** (task #112). 20/22 JT entries lifted.
2. **The combat MAIN LOOP and its per-turn / per-round / per-action tree** —
   `jt511` (the loop entry, **LIFTED level-2** with its heavy locals stubbed),
   `l0006` (teardown, **LIFTED**), and **45 `lXXXX` combat locals** that are the
   actual work: 5 stubs + ~40 missing. These have **0 jumptable callsites**
   (the loop reaches them via `jsr %pc@`), so they don't show in the JT
   scoreboard — `seg_audit.py 13` is the truth here.

**Snapshot (`seg_audit.py 13`):** 75 functions, 28 lifted (37%), 47 remaining.
Only **2 pending JT** entries (jt496, jt510 — leaf MISSING); the rest is the
45-local combat tree below.

### Why this is the highest-leverage combat target

`jt511` runs the loop but its body calls the stub locals, so **nothing combat
actually executes**. Landing the orchestration spine (l076e + friends) is what
makes the 81 **CODE 16** effect handlers and the **CODE 14** combat-field render
(see `code16-wall.md` / `code14-wall.md`) reachable at runtime. CODE 13 is the
TOP of the combat tree. Lift order matters — do the spine first.

## DONE
- `jt511` CODE 13+0x05a6 — combat main loop (LIFTED level-2; installs jt538).
- `l0006` CODE 13 — combat teardown; reinstalls jt601 (LIFTED).
- `jt490`–`jt509` (minus jt496) — the area-map line/region renderer (task #112).

## Cluster 1 — the ORCHESTRATION SPINE (do these FIRST, in order)

The loop skeleton. Lift left-to-right; each is small-to-medium and gates the rest.

| local | addr | size | what (disasm) | status |
|---|---|---:|---|---|
| `l4f22` | 0x4f22 | 64ln | combat ENTRY staging — jt68 setup, jt65, jt525/531 draw, jt77, jt117 present | **LIFTED** (level-2; spawned stubs jt68/jt536/l3f24/l404e/l4af4/l276c) |
| `l0434` | 0x0434 | 120ln | per-ROUND init — fills the `-22624` initiative slots (jt399) + initiative rolls (jt870) | **LIFTED** (insertion-sort party by init member[64][5], jt870 tie-break; deps lifted) |
| `l076e` | 0x076e | 99ln | **per-ACTOR turn (THE KEYSTONE)** — jt868 erase, jt516 creature predicate, jt21 derived-stats, jt525/530/531 draw; dispatches the actor's action | **LIFTED** (level-2; dispatch -> l5008 monster-AI / l08b4 player-cmd stubs) |
| `l102a` | 0x102a | 100ln | END-of-round — death check (`"A Comrade is Dying"`), jt914/jt861/jt879, jt536 field + jt521 map redraw; may end the fight | **LIFTED** (full; death-timer aging + fight-over decision; deps lifted) |
| `l0116` | 0x0116 | 232ln | the combat LOG-LINE painter (`"%s%s%s"` via jt384/jt488/jt96/jt99/jt18/jt860) — every combat message routes here | **LIFTED** (full; per-member status box + monster-count; jt99->l4b84; -131xx templates are DATA-resident) |

## Cluster 2 — the in-combat COMMAND UI

The "Move / Attack / Cast / Guard / …" command bar + status panel the player
drives the turn through.

| local | addr | size | what (disasm) | status |
|---|---|---:|---|---|
| `l1162` | 0x1162 | 411ln | the COMBAT COMMAND MENU (`"Move/Attack, Move Le…"`) — jt525/531/519/532/526 draw, jt521 map | — |
| `l56d8` | 0x56d8 | 382ln | command handler / move-attack resolve (`"Move/Attack…"`, jt870 dice, jt529/535/546) | — |
| `l1714` | 0x1714 | 86ln | in-combat target picker (jt182 list, `"( )"` brackets, jt488/jt399) | — |
| `ld16`  | 0x0d16 | 253ln | combat STATUS / HP panel (jt155 text, jt166, jt173/154, jt525/531) | — |
| `l609a` | 0x609a | 66ln | magic toggle (`"Magic On / Magic Off"`, jt486/jt60/jt545) | — |

## Cluster 3 — ACTION handlers (what each command does)

| local | addr | size | what (disasm) | status |
|---|---|---:|---|---|
| `l08b4` | 0x08b4 | 325ln | action dispatcher / "can I do that?" (`"That doesn't work"`, jt530/543/545/476) | **LIFTED** (level-2 skeleton; JT[3] 13-cmd + JT[1] roster-nav; spawned stubs l609a/l0d16/l26ea/l1162/l272a/l1842/l1714/jt547/jt534) |
| `l4306` | 0x4306 | 541ln | the BIG action switch (jt3 inline dispatch + jt472) | — |
| `l6454` | 0x6454 | 515ln | combat setup/build (jt41/jt882/jt21 derived-stats/jt543) | — |
| `l4af4` | 0x4af4 | 324ln | combat UI/setup (jt68, jt522/524 field, jt19 name) | — |
| `l544e` | 0x544e | 198ln | sub-dispatch (jt41/jt515/jt472/jt3) | — |
| `l525c` | 0x525c | 44ln | field action bridge (jt540/jt534 — into the CODE 14 field tier) | — |
| `l52fe` | 0x52fe | 103ln | spell-in-combat (jt547 camp-gate, jt870) | — |
| `l272a` | 0x272a | 34ln | GUARD action (`"Guarding"`, jt527/jt521) | — |
| `l283e` | 0x283e | 43ln | BANDAGE / first-aid (`"is bandaged"`, jt18) | **LIFTED** (dying-scan + bandage; l102a calls flag 0, Bandage cmd flag 1) |

## Cluster 4 — attack & MORALE resolution (dice)

| local | addr | size | what (disasm) | status |
|---|---|---:|---|---|
| `l3678` | 0x3678 | 72ln | attack-roll resolution (jt870) | — |
| `l375a` | 0x375a | 153ln | attack-roll resolution (jt870) | — |
| `l3936` | 0x3936 | 160ln | attack-roll resolution (jt870) | — |
| `l3d56` | 0x3d56 | 150ln | attack-roll resolution (jt870/jt399) | — |
| `l5008` | 0x5008 | 177ln | FLEE / panic (`"flees in panic"`, jt870, jt546, jt599 effect) | — |
| `l6176` | 0x6176 | 114ln | MORALE (`"is forced to flee / Surrenders"`, jt544/jt877) | — |
| `l167e` | 0x167e | 47ln | attack-validity (`"Not with that weapon"`, jt533/549/550) | — |

## Cluster 5 — combat SETUP / art / sub-dispatch (lower priority leaves)

| local | addr | size | what (disasm) | status |
|---|---|---:|---|---|
| `l3f24` | 0x3f24 | 93ln | combat ART load (`"WildCom1 / DungCom1"` libraries, jt54/jt197) | — |
| `l404e` | 0x404e | 93ln | per-actor build (jt21 stats, jt477 alloc, jt399) | — |
| `l3016` `l32ba` | 0x3016 / 0x32ba | 181/191ln | jt3 inline-switch sub-dispatchers | — |
| `l2d30` | 0x2d30 | 71ln | area-map bridge (jt206/jt202) | — |
| `l41b2` `l490c` `l5b9a` | — | 116/158/371ln | draw/attack composites (jt515 / jt525/531/546/870) | — |
| `l6042` `l6c96` `l6ac4` `l6b2c` `l1842` `l2ca6` `l2e92` `l3540` `l364c` `l3b36` `l4188` `l52ee` `l8b4`… | — | small | leaf helpers (range jt552, fills jt399, etc.) | — |
| `jt496` / `jt510` | CODE 13 | leaf | the 2 pending JT leaves (MISSING) | — |

## Worklist discipline

1. **Spine first** (Cluster 1, in order: l4f22 → l0434 → l076e → l102a → l0116).
   Once l076e dispatches a real action and l0116 paints the log, you can drive a
   minimal fight and the CODE 14 field + CODE 16 handlers start being reached.
2. Pick a row. `git grep -n 'static.*\bl<addr>(' src/engine/boot.c` (duplicate
   check) + dual-name check vs `jumptable.txt` — CODE 13 has `jtNNN`≡`lXXXX`
   collisions (e.g. the jt490–509 renderer family).
3. Lift, gate (`make`, codegen grep `muls.l|bfextu|bfins`, `make -s test`),
   update this file's status column, commit both together.
4. Re-run `python3 tools/jt_progress.py` + `seg_audit.py 13` so counts stay honest.
   This wall is the combat half of task #115 (with `code16-wall.md` / `code14-wall.md`).
