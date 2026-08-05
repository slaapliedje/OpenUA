# Combat feature audit — 2026-07-03

Method: parse every function body in boot.c, classify trivial-PROBE stubs,
build the static call graph, and BFS from the combat roots (l08b4, jt511,
l5b9a, l5008, jt555/l14bc/l2b24, jt599 + the CODE-16 handlers, jt547,
jt539/l315e/jt538/l1efa, l102a, jt930/l2fd4/l3d1e, jt541/jt490, l0d16,
jt595/jt597/l4faa, jt501/jt502, l6042/l6176/l52fe, jt534, jt893, jt904,
l1162, l1714). 754 functions reachable; 61 flagged; each hand-verified.

## Fixed in this audit (stub-shadowed FULL LIFTS — the alias-twin class)

| stub | = lifted twin | live callers | player-visible impact |
|---|---|---|---|
| l7894 | **jt529** (facing bearing) | jt555 x2, l2b24, l5b9a x2, l29fc | defenders now TURN to face attackers; missile sprites bind the real direction (was always 0); the backstab behind-check can fire |
| l0660 | **l660** (attacks of opportunity) | l56d8 post-move | AoO free swings now trigger when moving out of reach (was never) |
| l2d48 | **jt544** (side morale value) | jt535 flee resolution | flee attempts now weigh the enemy side's real threat (was always 0 = trivially escaping) |
| l3918 | **jt120** (viewport reskin) | jt79 x3, jt49 | the record-window close repaint actually reskins the viewport |

All four verified: build clean, `make test` green, a full QUICK combat runs
healthy in Hatari with visibly correct facings.

## Remaining combat-reachable stubs (real gaps, ranked)

| fn | where | size | status |
|---|---|---|---|
| jt522 | CODE 14+0x7488 | leaf | **LIFTED 2026-07-03**: cell class + the 27/28/29 multi-cell resolution over -23234 (origin m[28]/[29] + the shared -27862/-27853 deltas via -24085) |
| jt897 | CODE 19+0x420e | leaf | **LIFTED 2026-07-03**: rec w[86] -= amount (coin-pool weight) |
| jt520 | CODE 14+0x6de8 | ~930B not leaf | **LIFTED 2026-07-04** (band 6): the combat DEATH cleanup — death flicker over the l5d92 footprint, corpse parked in the -25410 registry + the 30/31 map marker, zone facing cleared, field recommit (jt524), scroll-back, sub-record reset. Both callers (out-of-combat + the case-13 death arm) were live. |
| l61ae | CODE 14+0x61ae | ~300B | **RESOLVED 2026-07-04**: l61ae = JT[524], and jt524 was ALREADY fully lifted (the alias-twin class) — the jt551 call site was repointed; no lift needed. The "carded" plan was chasing done work. |
| l2d78 | CODE 19+0x2d78 | ~500B | **LIFTED 2026-07-04** (band 6, = JT[890]): ready/un-ready side effects by hook kind item[56]&7f — l77a0 effect core / the wizardry-ring slot doubler (member[377] + the 198-band trim) / jt875 recompute / jt878 effect-23 strip. Three live callers (READY, UNREADY, the 58448 chain). |
| l0116 | — | — | LIFTED already (post-combat aftermath) |

## CODE 16 spell-effect handlers

docs/code16-wall.md tallies **160 LIFTED**; its remaining "stub" rows are
mostly STALE (jt539/jt541/jt542/jt546/jt50/jt51/jt64/jt67/l0116 have all
since been lifted). The honest remaining:
- **jt974** — the sound-mixer pump (CODE 5+0x1304, ~600B): the audio
  subsystem, not combat logic.
- **jt55/jt58** — art/resource teardown leaves (l0006).
- **jt537** notes "inert until jt546 lifts" — jt546 is lifted; re-verify jt537.

## Confirmed FAITHFULLY trivial (do NOT "fix")

- jt512 (CODE 14+0x5d8e) and jt510 (CODE 13+0x6d1a) — the Mac bodies are bare
  `rts` (disasm-verified).
- jt1163 (`return 0`) / jt1170 (empty) — the Mac bodies are literal.
- jt94's row-23 l3f88 band erase — dead on the Mac too (double projection).
- l217e/l2170 — real one-line accessors of -13016.
- jt472 (`v & 1`) — the diagonal-direction predicate.

## Low-priority / port-moot stubs in the reach set

jt1081 (fatal-path teardown), jt1044/jt1050 (Window Manager alloc — HAL-moot),
l24aa (palette resume — dormant), l341a (Mac SFPutFile dialog — GEMDOS-moot),
l5726 (camp scribe scanner — the SCRIBE slice), l006c (CODE 19 rest
side-effects — the REST-leaves slice), jt68 (setup yield/pump).

## Unverified UNKNOWNs (a follow-up pass; likely faithful accessors)

l5ac0, l6804, l3d8c, l7de0, l4350, jt1064, l0004, l31ea, l31f0, jt985, jt965,
l4f2c, l4ff6, jt441, l0062, l2cf4, l32e2, l15bc, and jt1109/l157c/jt1121
(display-mode arms the colour build may never take). Each needs a one-minute
Mac-asm check; none showed runtime symptoms. The CODE 4 codec siblings
jt1183/jt1188/jt1181/jt1184/jt1189/jt1191 were FULLY LIFTED 2026-07-04 (the
shifted row-blit family + the faithful jt995 re-lift); jt1152/jt1142 lifted
in band 7 batch 8.

## Verdict

Core combat is fully lifted: initiative, the player action tier (all 13
command arms), targeting (jt539/l315e/jt538), strikes (jt555/l14bc/l2b24),
projectiles (jt501/jt502), the effect pipeline (jt599 + 160 handlers), AI
(l5b9a/l5008/l6176 morale/l6042 move), XP/treasure, and the info panel. The
four twin fixes above were the last silent no-ops in the hot path; what
remains is five small leaves (table 2), the audio mixer, and the UNKNOWN
sweep.

## Off-screen monster movement is FAITHFUL — do not "fix" it

Reported as a possible bug (2026-08-05): during combat you hear the monsters'
footstep sounds while the viewport stays on the party and never pans to whoever
is moving. **That is the original's behaviour, on both the Mac and DOS
releases.** Verified twice over, and the code is a one-for-one lift.

One flag, `-22626`, gates the whole *visual* half of an actor's turn. It is set
at the top of `l076e` (CODE 13+0x76e, "execute one actor's combat turn"):

```
07d8:  moveb %a0@(95),%d0     ; the combat-side byte
       tstw  %d0
       beqs  L07fa            ; side == 0 (party side) -> flag = 1, always
       jsr   JT[516]          ; l6554(actor, 0) — any part of it in the window?
       tstb  %d0
       bnes  L07fa            ; already visible -> flag = 1
       moveq #0,%d0           ; off-window monster -> flag = 0
L07fa: moveq #1,%d0
L07fc: moveb %d0,%a5@(-22626)
```

With the flag clear, the move commit `jt551` skips both `jt521`
scroll-and-repaint calls and the `l635e` trail repaint — but `jt52(11)`, the
step sound, sits **outside every guard**. Hence sound with no picture. Two
things re-arm it, both in `l56d8`'s resolution (L5a6c): the `-22628` hit flag
(it connected with someone) or `l6554` going true (it walked into the window).
Party-side actors short-circuit to 1 before the window test even runs.

### The DOS confirmation (HEIRS rider/ogre encounter)

Driven headless with `tools/dosdrive.sh` against SSI's DOS 1.2. Handy trick:
**our port's slot save loads directly in DOS**, so copying
`gamedata/HEIRS.DSN/SAVE/SAVGAM<c>.CSV` (+ `VAULT<c>.DAT`) into
`dos-run/HEIRS.DSN/SAVE/` puts DOS at the exact same spot as the Atari build —
no replaying to the encounter. DOS pre-selected the slot and listed all six
characters.

`AIM` -> `NEXT` scrolls the view unconditionally, which is how you prove the
horde starts off-window: it jumped clean off the party to minotaurs and ogres
at **RANGE = 14**. Then, tagging every captured frame by the active actor's
name colour (green = monster, `(0,170,0)` — *not* >170, an off-by-one that made
the first pass read `?` for every frame) against the % of the field viewport
that changed:

| | party-side actor | monster |
|---|---|---|
| off-window, approaching | 34–38% = full recentre, every time | **0.0%** — 36 consecutive frames (~15 s) with a HILL GIANT acting and not one pixel drawn |
| in contact | 43–47% recentre | 36–43% recentre (hit flag / on-window) |

The horde crossed all 14 cells and simply *arrived* in frame; the view never
followed it in. Once engaged, monster turns recentre exactly like party turns.
So both arms of the rule reproduce in DOS, and our port matches.
