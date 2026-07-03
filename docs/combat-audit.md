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

| fn | where | size | what breaks without it |
|---|---|---|---|
| jt522 | CODE 14+0x7488 | leaf | combat-map occupant-class fetch; jt538's pick-many spell-target weighting and the 40919 site read 0 |
| jt520 | CODE 14+0x6de8 | leaf | area-map combat cleanup (l5008 paths 44948/45158, jt860) |
| l61ae | CODE 14 | leaf | l56d8's field commit after monster moves |
| l2d78 | CODE 19+0x2d78 | ~500B | readied magic-item side effects (equip/unequip hooks) |
| jt897 | CODE 19+0x420e | leaf | per-coin-bank XP credit on pooling |
| l0116 | — | — | LIFTED already (post-combat aftermath); the code16-wall row is stale |

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
l4f2c, l4ff6, jt441, l0062, l2cf4, l32e2, l15bc, and the CODE 4 codec
siblings jt1183/jt1188/jt1181/jt1184/jt1189/jt1191 + jt1109/l157c/jt1152/
jt1142/jt1121 (display-mode arms the colour build may never take). Each needs
a one-minute Mac-asm check; none showed runtime symptoms.

## Verdict

Core combat is fully lifted: initiative, the player action tier (all 13
command arms), targeting (jt539/l315e/jt538), strikes (jt555/l14bc/l2b24),
projectiles (jt501/jt502), the effect pipeline (jt599 + 160 handlers), AI
(l5b9a/l5008/l6176 morale/l6042 move), XP/treasure, and the info panel. The
four twin fixes above were the last silent no-ops in the hot path; what
remains is five small leaves (table 2), the audio mixer, and the UNKNOWN
sweep.
