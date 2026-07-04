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

## Verdict

The off-by-one arm shift #122 was guarding against is **absent** from
every lifted JT[1] value-switch (combat CODE 14/16/18, char-gen 17,
monster viewer 10, post-fight 12, design editor 2). The dispatch that
drives spell applicability, effect ids, class seeds, and monster load
is decoded correctly. Re-run this audit after any new JT[1] lift; the
one-liner per site is above.
