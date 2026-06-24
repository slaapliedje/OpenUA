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

**Snapshot (`seg_audit.py 13`):** 75 functions, 73 lifted (97%) — effectively
**100%**. The 2 the audit still lists are non-issues: `jt510` (CODE 13+0x6d1a is
a bare `rts` — the empty stub IS faithful) and `l2d30` (already lifted as
`l2d30_c13`; the audit just doesn't recognize the `_c13` suffix). The whole
combat-SETUP tree is stub-free, and the monster spell-selection cluster
(`l6c96` → `l6b2c` sort / `l6ac4` iterate / `jt552`) is lifted.

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
| `l1162` | 0x1162 | 411ln | the player MOVE/ATTACK picker (`"Move/Attack, Move Le…"`) — jt173 input loop, JT[1] dir dispatch, jt515 probe -> l167e attack / terrain move | **LIFTED** (full input-loop lift; l167e attack + jt553/551/879/13/l26ea tail; jt532 highlight now LIFTED too — NO stubs) |
| `l56d8` | 0x56d8 | 382ln | command handler / move-attack resolve (`"Move/Attack…"`, jt870 dice, jt529/535/546) | **LIFTED** (full CFG; "Move/Attack" prompt, 6-cell step loop over -7882, caster fizzle, -7839/-7840 resolve; l544e/jt535/jt551/jt553 all LIFTED, reuses l6554=jt516 — NO stubs left in the l56d8 tree) |
| `l1714` | 0x1714 | 86ln | in-combat target picker (jt182 list, `"( )"` brackets, jt488/jt399) | **LIFTED** (full; paged jt182 dialog, JT[1] ESC + JT[3] page rec[18]; jt63 stub) |
| `l0d16` | 0x0d16 | 253ln | combat COMMAND MENU + input reader (jt155 build, jt166, jt173/154, jt525/531) | **LIFTED** (full; conditional cmd list 1-12 + jt173 keystroke validate-loop; spawned stubs l279c/l27e6/jt154) |
| `l609a` | 0x609a | 66ln | magic toggle (`"Magic On / Magic Off"`, jt486/jt60/jt545) | **LIFTED** (full; JT[1] 204 toggle/-22648 + 32 clear-monster + 215 jt545) |

## Cluster 3 — ACTION handlers (what each command does)

| local | addr | size | what (disasm) | status |
|---|---|---:|---|---|
| `l08b4` | 0x08b4 | 325ln | action dispatcher / "can I do that?" (`"That doesn't work"`, jt530/543/545/476) | **LIFTED** (level-2 skeleton; JT[3] 13-cmd + JT[1] roster-nav; spawned stubs l609a/l0d16/l26ea/l1162/l272a/l1842/l1714/jt547/jt534) |
| `l4306` | 0x4306 | 577ln | the BIG AoE-template action switch (JT[3] shape 1/2/3; -8567/-8551/-8547/-8539/-8583 geometry; l2df8_c13 edge query, l41b2 cell place) | **LIFTED** (full 1:1 CFG; do-while cell sweep + per-cell clip/l4188/l2df8_c13 wall tests + l41b2 placement; whole dep surface l4188/l41b2/l2df8_c13/l2d30_c13/jt206/l5e2e/jt472 LIFTED — STUB-FREE; unused until caller CODE 13+0x4d10) |
| `l6454` | 0x6454 | 515ln | combat setup / monster-AI target pick (jt41 special-attack flags, target scan, jt882/jt21/jt543 refresh) | **LIFTED** (full 6-phase lift; scores via l62ec_c13, picks primary/secondary/third target, refresh tree; helper combat_mondef; all deps lifted) |
| `l4af4` | 0x4af4 | 345ln | combat FIELD-PLACEMENT loop — clears 68 cell records, derives spawn spread (-7938/-7939), walks the -27928 roster placing each via l4306, builds the -25410 on-field creature records (jt522 terrain, 30/31 tile marker), drops failed reach-1 summons (jt19) | **LIFTED** (full; l4306 call at 0x4d10 now live — links l4306 in; deps jt490/jt68/jt399/jt19/jt522/jt524 lifted, only l490c draw-composite is a PROBE stub) |
| `l544e` | 0x544e | 198ln | per-step move/contact resolve (jt515 terrain probe, -27848 cost, JT[3] 27/28/29 door/barrier gate via jt41) — l56d8's step loop | **LIFTED** (full; all deps jt41/jt515/jt472 already lifted, no new stubs) |
| `l525c` | 0x525c | 44ln | field action bridge (jt540/jt534 — into the CODE 14 field tier) | — |
| `l52fe` | 0x52fe | 103ln | spell-in-combat (jt547 camp-gate, jt870) | — |
| `l272a` | 0x272a | 34ln | GUARD action (`"Guarding"`, l26ea + jt42) | **LIFTED** (full; mc[9] guard flag + "Guarding"; l26ea resolve still stub) |
| `l283e` | 0x283e | 43ln | BANDAGE / first-aid (`"is bandaged"`, jt18) | **LIFTED** (dying-scan + bandage; l102a calls flag 0, Bandage cmd flag 1) |

## Cluster 4 — attack & MORALE resolution (dice)

| local | addr | size | what (disasm) | status |
|---|---|---:|---|---|
| `l3678` | 0x3678 | 72ln | l375a's straight-channel river painter (col,row,&width): stamps a narrow channel (terrain 58/jt870+58/61) at field -25318 cols col..col+3, resets *width=1 — "attack-roll" guess was WRONG | **LIFTED** (full; dep jt870 lifted; leaf — l375a subtree stub-free) |
| `l375a` | 0x375a | 165ln | combat-field TERRAIN generator (NOT attack-roll): draws a meandering river/chasm down the field via jt870 dice + l3678, stamping terrain 52/55 banks into the -25318 field map | **LIFTED** (full; dep jt870 lifted; spawned PROBE stub l3678) |
| `l3936` | 0x3936 | 173ln | combat-field FOREST/OBSTACLE scatterer (l3ef6's 2nd pass; NOT attack-roll): l364c-gated, density-driven jt870 placement of bushes (41/42) + 2-tall trees (31-35/36-40) on "type-37" grass cells of the -25318 field | **LIFTED** (full; dep jt870 lifted; spawned PROBE stub l364c — the feature RNG) |
| `l3b36` | 0x3b36 | 175ln | combat-field BIOME placer (l3ef6's 3rd/last pass; NOT attack-roll): rolls a climate value from 6 l364c bits, then dispatches l3d56(col,row,...) with a biome param-set per climate band ([-30,9]..[90,110]) over "type-37" grass cells | **LIFTED** (full; dep l364c LIFTED; spawned PROBE stub l3d56 — the biome feature placer) |
| `l364c` | 0x364c | 18ln | the field-feature value source for l3936/l3b36: returns the -7914 table byte at index -7928 (monster cap) — the Mac copies the 32-byte table to a throwaway local first | **LIFTED** (full; leaf, no deps) |
| `l3d56` | 0x3d56 | 157ln | l3b36's biome FEATURE placer (NOT attack-roll): jt870(1,100) roll walked through cumulative weight thresholds (a3 / a3+a4+1 / +a5+a6 / +a7) -> one terrain tile (50/51 water, 44 rock, 68, 62-67) at the cell; includes a faithful dead arm | **LIFTED** (full; dep jt870 lifted; leaf — l4f22 combat-setup tree now 100% stub-free) |
| `l5008` | 0x5008 | 177ln | MONSTER-AI turn (`"flees in panic"`, morale jt870, jt546 acquire, jt599; dispatch attack/move) | **LIFTED** (level-2 skeleton; spawned stubs l6176/l52ee/l525c/l52fe/l6454/l5b9a/l6042) |
| `l6176` | 0x6176 | 114ln | MORALE (`"is forced to flee / Surrenders"`, jt544/jt877) | — |
| `l167e` | 0x167e | 47ln | player attack-validity (`"Not with that weapon"`, jt533/549/550) | **LIFTED** (mirrors l56d8 strike block; jt533 weapon/friendly-fire gate LIFTED too; unused until l1162) |

## Cluster 5 — combat SETUP / art / sub-dispatch (lower priority leaves)

| local | addr | size | what (disasm) | status |
|---|---|---:|---|---|
| `l3f24` | 0x3f24 | 93ln | combat ART load — picks the sprite GLIB (jt54) by -28006[34] outdoor flag: outdoor special "WildCom1" / outdoor normal "DungCom1" / indoor (jt197 cell class) "WildCom1"; resets field header -25318; field-init tail l3540/l3ef6 | **LIFTED** (full; deps jt54/jt197 lifted, art names via ua_strs_at @ 0x44d2/0x44dc/0x44e6; spawned PROBE stubs l3540 + l3ef6 — the jt54 4th push 39/25 is dead, dropped) |
| `l404e` | 0x404e | 100ln | per-actor COMBATANT sub-record build — roster walk: jt21 stats, jt477 alloc the 26-byte mc into +64 + jt399 zero-fill, mc[21] extra-flag vs party count -28006[32], facing mc[11]=-8551[facing>>1], default class band | **LIFTED** (full; deps jt21/jt477/jt399 lifted; jt477 arg order is the swapped lifted sig (bucket,tag,out)) |
| `l3ef6` | 0x3ef6 | 14ln | l3f24's indoor/outdoor-special field-init tail (frameless): jt399-fills field map -25318+9 with terrain 69 (1250B), -7928 = monster cap -28006[418], runs passes l375a/l3936/l3b36 | **LIFTED** (full; dep jt399 lifted; spawned PROBE stubs l375a/l3936/l3b36) |
| `l3540` | 0x3540 | 88ln | l3f24's outdoor-normal field-init tail: jt399-fills field map -25318+9 with terrain 22, then scans a 13(row)x5(col) window around the party — 4 cardinal l2df8_c13 edge queries (-7932/-7933/-7931/-7930) + per-cell passes l2e92/l2f82/l3016/l32ba | **LIFTED** (full; deps jt399/l2df8_c13 lifted; spawned PROBE stubs l3016/l32ba + l2ca6) |
| `l2e92` `l2f82` | 0x2e92 / 0x2f82 | 87/47ln | l3540 west-edge (-7932) and north-edge (-7933) field decorations — stamp the cell-block/passage/door tiles via l2ca6(col,row,val) per the edge code | **LIFTED** (full; dep l2ca6 LIFTED — subtree stub-free) |
| `l2ca6` | 0x2ca6 | 46ln | the field-cell PAINT primitive: maps (a,b)+scan offsets (-7935/-7934) to a sheared field pixel (x=-7935*6+-7934*5+a+21, y=-7934*5+b+10), bounds [0,49]x[0,24], writes c+1 to field map -25318 + y*50 + x, +9 | **LIFTED** (full; leaf, no deps) |
| `l3016` | 0x3016 | 264ln | per-cell WALL-TILE corner computer (l3540's 1st JT[3] pass): 8 nested inline switches over the W/N edge codes (-7932/-7933) + a diagonal-open flag -> four 2x2 corner tiles, painted TL/TR/BL/BR via l2ca6 | **LIFTED** (full; 8 JT[3] tables decoded incl. 2 disasm-misaligned arms; deps l2df8_c13/l2ca6 lifted) |
| `l32ba` | 0x32ba | 250ln | per-cell EAST-quadrant wall-tile computer (l3540's 2nd JT[3] pass; sibling of l3016): nested switches over N/dir-2 edge codes (-7933/-7931) + two neighbour-edge results (e7/e8) + diagonal flag -> four right-hand 2x2 tiles, painted (5,0)/(6,0)/(5,1)/(6,1) via l2ca6 | **LIFTED** (full; deps l2df8_c13/l2ca6 lifted — l3540's per-cell pass set is now stub-free) |
| `l2d30` | 0x2d30 | 71ln | area-map bridge (jt206/jt202) | — |
| `l41b2` `l4188` | 0x41b2/0x4188 | 120/18ln | l4306 AoE geometry helpers — cell placement+validate (jt515) / both-axes OOB clip | **LIFTED** (no new stubs; unused until l4306) |
| `l490c` | 0x490c | 158ln | AoE-TEMPLATE BUILDER (NOT "draw composite" — no Toolbox calls): rebuilds the -8471 grid l4306/l41b2 consume on each facing change; cone deltas -7942/-7940 + per-(shape,dir,row,col) in/out marking vs the -8531 bounds table | **LIFTED** (full; leaf, no deps; guarded by the -7920 facing cache) |
| `l5b9a` | 0x5b9a | 371ln | the monster ATTACK executor (per-attack loop; jt868/l283e gate, monster-def attack count, target re-acquire jt546/-25676, range l713c, resolve l56d8/jt555, jt521 redraw) | **LIFTED** (full CFG; reached via l5008; l56d8/l2484/l713c/l25f4/l2bde + jt554/jt549/jt550/l5c32 all LIFTED — attack tree fully STUB-FREE) |
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
