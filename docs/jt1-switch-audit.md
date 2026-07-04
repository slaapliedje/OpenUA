# JT[1] sparse-switch audit (task #122) — 2026-07-04

## Concern

THINK C's JT[1] (CODE 1+0x130) is a *sparse* value-switch: a
`jsr JT[1]` followed by an inline `.short count` then `count`
`(key, offset)` pairs, each offset PC-relative to its own slot, then
a default offset. Hand-decoding these is error-prone — the classic bug
is an **off-by-one arm shift**, where each case ends up pointing at the
*next* case's arm. `tools/jt1_extract.py` decodes them mechanically;
this audit re-extracts every lifted JT[1] switch and checks the port's
C `switch` case→arm grouping against the ground truth.

## Method

For each lifted JT[1] switch in `src/engine/boot.c`:

```sh
python3 tools/jt1_extract.py data/work/disasm/CODE_NN.bin --jsr-at 0xJSR
# (or --table-at 0xTABLE)
```

Compare the extracted `(key → arm-offset)` map to the C switch: keys
that share an arm offset MUST be grouped in the same C case block
(fall-through or a common `default`). A mismatch — a case grouped with
the wrong arm, or a shifted key set — is the off-by-one bug.

## Result: all audited JT[1] switches are CORRECT

No off-by-one found. Every case grouping matches the ground truth,
including all shared-arm groupings.

| CODE | table | cases | port switch (boot.c) | verdict |
|---|---|---|---|---|
| 14 | 0x47b4 | 29 | l476e_c14 (spell applicability) | ✅ exact — 3/58/71, 134/15, 49/23, 47/133/74/115, 76/110, 120/81 all grouped right |
| 2  | 0x42b6 | 5  | design-editor cmd echo (1st table) | ✅ 1/5/10, 8+11+default→0x4310 |
| 2  | 0x430c | 0  | (degenerate — default only) | ✅ n/a |
| 2  | 0x4388 | 5  | design-editor cmd echo (3rd table) | ✅ 1 grouped with default→0x44be |
| 17 | 0x6a42 | 2  | char-gen class seeds (Cleric/MU) | ✅ case 0 / case 5 / default |
| 18 | 0x1576 | 2  | combat effect dispatch | ✅ case 1 / case 20 / default |
| 16 | 0x603a | 6  | spell effect id | ✅ 127+130→0x6060 grouped right |
| 16 | 0x2d36 | 2  | class-byte switch | ✅ case 0 / case 5 / default |
| 12 | 0x3ed2 | 3  | post-fight pick | ✅ 136+132→0x3eea grouped right |
| 10 | 0x5b00 | 5  | monster-viewer state | ✅ 10+11+default→0x5c14 grouped right |

Already marked "re-decoded via jt1_extract" in-code (trusted, spot-clean):
`0x5a94` (CODE ?, roster-nav key), `0x3a70` (dragon-attack element),
`0x2da2` (spell mode).

## JT[2] (CODE 1+0x144)

Only 4 sites in the whole disasm; the sole lifted one is the keyboard
**scan-code** translation (a 25-entry Mac-scancode→char table, the
`l725c`/event-key path). That table is platform-specific and the port
routes keys through its own IKBD HAL rather than the Mac scan map, so
it is not a faithful-arm-decode concern — noted, not audited as a
value-switch.

## JT[3] combat/char-gen dispatch (extended pass, 2026-07-04)

The same arm-shift risk applies to the THINK C JT[3] inline switch.
Audited the combat + char-gen JT[3] switches with `jt3_extract`
(`--table-at 0xNN`), checking min/max/default and shared-arm groupings
against the port's C `switch`:

| CODE | table | range | port switch | verdict |
|---|---|---|---|---|
| 12 | 0x3230 | 0..16 | L31e0 per-class XP share | ✅ {8,10,11,12,13,14,16}→jt7(,2), {9,15}→jt7(,3), {1,7}→default |
| 12 | 0x3f2c | 0..5  | post-fight pick | ✅ all distinct |
| 14 | 0x53ac | 0..2  | jt40-based base value | ✅ all distinct |
| 13 | 0x437e | 1..3  | area-render mode | ✅ all distinct |
| 16 | 0x3478 | 0..8  | spell save-mod class | ✅ {2,3},{4,5},{7,8} grouped right |
| 16 | 0x642  | 0..5  | spell save-mod class | ✅ {0,1} grouped right |
| 17 | 0x2a74 | 8..16 | l29ae multi-class HP | ✅ SEMANTIC — verified each of the 9 *distinct* arms by the sub-entry byte offset it loads: 8-12→sub0, 14→sub2, 13/15/16→sub5 (the port's collapse is faithful, not a merge error) |

The CODE 17 one is the instructive case: the 9 arms are separate in
the asm (distinct offsets), so it *looks* like the port over-merged
them — but dumping each arm's `base@(off)` accesses shows they all
reduce to one of three sub-entries exactly as the port groups them.
Confirming a decode against *arm behaviour*, not just the table, is
the right check when arms share a computed result.

## Verdict

The off-by-one arm shift #122 was guarding against is **absent** from
every lifted JT[1] value-switch AND from the audited combat/char-gen
JT[3] switches — 19 dispatch tables total. The dispatch driving spell
applicability/save-mods, class HP seeds, XP shares, effect ids, and
monster load is decoded correctly. Re-run this audit after any new
JT[1]/JT[3] lift; the one-liner per site is above (jt1_extract for
JT[1], jt3_extract for JT[3]).
