# Mac 1.2 hunk log — the cherry-pick worklist (ADR-0018)

Every difference between Mac 1.0 (the lift target) and Mac 1.2, with what has
been ported into the 1.0 lift and what has not. Regenerate the raw data with:

```sh
python3 tools/mac12_diff.py --list        # structural hunks (this table)
python3 tools/mac12_diff.py --hunk N -C 12
python3 tools/mac12_diff.py --operands    # operand-only changes (second table)
```

**Read `tools/mac12_diff.py`'s header before trusting any count here.** A byte
diff of these two forks is 11,872 bytes of noise: 1.2 removes one jump-table
entry, so every `jsr %a5@(…)` operand above it shifts. Two earlier hand-rolled
parsers each mis-tokenised the listings and produced *plausible* but wrong
totals (32, then 28); 38 is what a correctly-anchored mnemonic compare gives.

## Structural hunks — 38, in 10 of 23 segments

CODE 1, 2, 3, 4, 5, 8, 9, 11, 14, 15, 17 and 22 have **identical instruction
streams**; CODE 17 changes only in operands (see below).

| # | seg | 1.0 label | kind | 1.0 | 1.2 | lifted | status | note |
|--:|--:|---|---|--:|--:|---|---|---|
| 1 | 6 | `L25c0` | insert | 0 | 5 | jt39 | ⬜ open |  |
| 2 | 6 | `L4e3a` | replace | 1 | 1 | `l4d98` | ✅ ported | l4d98: clear the -22222 SLOT (`pea`), not the object it points at (`movel`) |
| 3 | 7 | `L1a66` | delete | 11 | 0 | - | ⬜ open |  |
| 4 | 7 | `L1ace` | replace | 1 | 1 | - | ⬜ open |  |
| 5 | 7 | `L21b6` | delete | 18 | 0 | - | ⬜ open |  |
| 6 | 7 | `L22a8` | replace | 19 | 1 | - | ⬜ open |  |
| 7 | 7 | `L3f80` | replace | 1 | 1 | yes | ✅ ported | l2ebc key-mode arg 0 -> 1 (boot.c ~95700) |
| 8 | 10 | `L38ba` | replace | 1 | 1 | - | ⬜ open |  |
| 9 | 10 | `L611c` | insert | 0 | 21 | yes | ✅ ported | l611c: sync each ability's CURRENT byte from its PERMANENT byte before the save |
| 10 | 10 | `L6238` | replace | 1 | 1 | yes | ✅ ported | l6238: build the path forwards (clear + 2x jt431) instead of jt436's in-place dir prefix |
| 11 | 10 | `L6238` | replace | 1 | 5 | yes | ✅ ported | l6238: the second jt431 join (same change as hunk 10) |
| 12 | 12 | `L071c` | replace | 1 | 2 | - | ⬜ open |  |
| 13 | 12 | `L0d3e` | insert | 0 | 16 | - | ⬜ open |  |
| 14 | 12 | `L3426` | replace | 1 | 5 | `l33d8` | ✅ ported | l33d8 pass 2: skip SUMMONED combatants (`mc[21]==1`) so a conjured creature cannot mask a party wipe |
| 15 | 13 | `L105c` | insert | 0 | 5 | - | ⬜ open |  |
| 16 | 16 | `L50cc` | insert | 0 | 3 | - | ⬜ open |  |
| 17 | 18 | `L003a` | insert | 0 | 13 | jt860 | ✅ ported | jt860: honour the design's no-permadeath flag — status 6/7/8 downgrades to 5 |
| 18 | 18 | `L61d4` | replace | 1 | 1 | - | ⬜ open |  |
| 19 | 19 | `L25ce` | insert | 0 | 2 | yes | ✅ ported | jt893: hoist the -22281 save+clear to entry |
| 20 | 19 | `L2c20` | delete | 2 | 0 | yes | ✅ ported | jt893: 1.0 per-case save+clear deleted |
| 21 | 19 | `L2c20` | delete | 1 | 0 | yes | ✅ ported | jt893: 1.0 per-case restore deleted |
| 22 | 19 | `L2d74` | insert | 0 | 1 | `jt893` | ✅ ported | jt893: restore at the single exit |
| 23 | 20 | `L00c4` | delete | 18 | 0 | `l0098` | ✅ ported | l0098: the OTHER half of the tolower fix — deletes 1.0's `A-Z` pre-test; already covered by the A-Z-only implementation |
| 24 | 20 | `L18e2` | insert | 0 | 7 | `l159a` | ✅ ported | l159a combat entry: `ev[12]` bit5 forces the starting range (`rec[56]`) to 0 |
| 25 | 20 | `L24e6` | insert | 0 | 2 | ? | ⛔ blocked | byte-swap `ev[8]` before the money compare — but the port has not lifted the enclosing message composition |
| 26 | 20 | `L24e6` | replace | 1 | 1 | ? | ⛔ blocked | the compare rearranged to suit hunk 25 |
| 27 | 20 | `L26de` | insert | 0 | 4 | ✗ | ⛔ blocked | CODE 20's L26de is NOT lifted — the old "yes" was a CODE 10 name collision |
| 28 | 20 | `L26de` | delete | 1 | 0 | ✗ | ⛔ blocked | same function as 27; blocked with it |
| 29 | 20 | `L3114` | insert | 0 | 2 | - | ⬜ open |  |
| 30 | 20 | `L57a0` | insert | 0 | 5 | yes | ✅ ported | l5676: "Transfer module ends testing!" before the test-play teardown |
| 31 | 20 | `L70d4` | insert | 0 | 1 | `l709e` | ✅ ported | l709e: clear `-4943` per event so the deferred re-trigger flag cannot leak across a chain |
| 32 | 20 | `L76c4` | replace | 3 | 1 | - | ⬜ open |  |
| 33 | 20 | `L76fa` | delete | 1 | 0 | - | ⬜ open |  |
| 34 | 21 | `L13f6` | insert | 0 | 4 | - | ⬜ open |  |
| 35 | 21 | `L3af2` | delete | 20 | 0 | yes | ✅ ported | l3af2: DELETE 1.0's silent clamp — the half that ARMS hunks 36-38. **OBSERVED FIRING** |
| 36 | 21 | `L4816` | insert | 0 | 8 | yes | ✅ ported | jt955 case 3: overland bounds guard #1 + the refusal message |
| 37 | 21 | `L4816` | insert | 0 | 5 | yes | ✅ ported | jt955 case 3: overland bounds guard #2 (out of range == blocked) |
| 38 | 21 | `L4874` | insert | 0 | 8 | `jt955` | ✅ ported | jt955 case 3: the second guard's tail |

### Ported so far (19 of 38)

| hunks | function | fix |
|---|---|---|
| 36, 37, 38 | `jt955` case 3 (CODE 21 `L4816`) | bounds-check the overland target cell (38x15 -> max 37/14) at both use sites; out of range is refused with "There is no way to go in that direction." instead of indexing the HDR and wall-art table out of bounds |
| 30 | `l5676` (CODE 20 `L57a0`) | say "Transfer module ends testing!" before a type-11 transfer tears down a test-play session, instead of vanishing silently |
| 19–22 | `jt893` (CODE 19 `L25ce`) | the in-combat flag `-22281` is suppressed across the WHOLE Items browser (save+clear at entry, restore at exit) rather than only around the trade/give confirm |
| 7 | the `L3f80` picker (CODE 7) | the modal key-mode argument to `l2ebc` goes 0 -> 1, enabling `l23b4`'s `arg_lo != 0` arm |
| 23 | `l0098` (CODE 20 `L00c4`) | the OTHER half of the tolower fix, and it corrects the story. 1.0 has an `A-Z` pre-test at 0x00c4 that ALSO jumps to the `+32` block, so 1.0 is `if ((c in A-Z) \|\| (c in 0-9)) c += 32;` — the port was faithful all along, and an earlier note here wrongly accused it of "adding" the A-Z clause. 1.2 deletes that pre-test AND retargets the digit range to A-Z; the A-Z-only implementation already covers both, so this hunk needed no further code |
| 31 | `l709e` (CODE 20 `L70d4`) | clear `-4943` at the START of every event. 1.0 clears it only in the `L76a6` tail, and that clear sits INSIDE `if (-4945 == 0)` — so when an event chains (`-4945 != 0`) the clear is skipped and the deferred re-trigger flag survives into the next event, where the `-4942 && -4943` test can re-scan the party's cell on the strength of a previous event's request. **Read the operands, not the hunk position:** the diff points at 1.2's `clrb -4945` as the insert, but 1.0 already has that (@0x70de); both instructions are `clrb`, so the aligner paired the wrong ones. The genuinely new instruction is `clrb -4943` |
| 24 | `l159a` combat entry (CODE 20 `L18e2`) | `ev[12]` bit5 now forces `rec[56]` (the starting range from `ev[14]` bits5–6) to 0, and the existing clamp drags `rec[55]` to 0 with it — the fight starts adjacent whatever range the designer picked. Measured, not guessed: 1.0 reads `ev[12]` bit5 at exactly ONE site (CODE 20 @0x4668, gating a `jt221`+`jt938` view refresh for type-1 events) and 1.2 reads it at two, so the flag was already live and 1.2 gives it an additional effect. **This is the first ported fix with reachable data in the shipped designs:** 11 of HEIRS' 175 combat events have bit5 set with a non-zero starting range (`GEO011` ev12 at cell(col=18,row=16) is one), and 31 such events exist across all designs on hand |
| 14 | `l33d8` (CODE 12 `L3426`) | pass 2 of the post-fight outcome resolver now SKIPS summoned combatants. 1.0 `bras L347a` -> 1.2 `braw L34c2` (the branch outgrew its 8-bit displacement) plus 4 inserted instructions testing `node[64]->[21] == 1` and branching to the ADVANCE label — a `continue`. Pass 1 and the main pass already stop at the first summoned entry; pass 2 was the one place scanning unfiltered. It matters because `found` + the design's `hdr[29]` no-permadeath flag CLEARS `-27982`, the "party destroyed" flag: in 1.0 a summoned creature sitting in status 3/4/5 (fled/dead/petrified) or carrying `rec[382]` satisfied `found` on its own, so a party that had actually been wiped could come out not registered as destroyed — on the strength of a monster it had conjured |
| 10, 11 | `l6238` (CODE 10 `L6238`) | build the delete path FORWARDS — clear the buffer, `jt431` the design dir, `jt431` the leaf — instead of `jt436`'s in-place directory prefix, which has to slide the existing contents up inside a fixed 202-byte buffer. 1.2 also grew the frame 16 bytes for a separate leaf buffer. `l419e` right below already used the two-append idiom, so 1.2 is making `l6238` consistent with the rest of CODE 10 |

| 35 | `l3af2` (CODE 21 `L3af2`) | **DELETE** 1.0's 20-instruction clamp, and this is the load-bearing half of the overland bounds fix. 1.0 ended `l3af2` by resolving an off-map candidate BACK to the party's current cell, so a step at the border was a silent no-op with no feedback. 1.2 deletes the clamp so the caller sees the raw candidate and refuses it with a message. **The two halves are one fix:** `L3af2` has exactly ONE call site in the Mac (CODE 21 @0x4816 = the `jt955` case-3 site hunks 36–38 patch), so while the clamp stood `ov_step_out_of_bounds()` could never be true and hunks 36–38 were DEAD CODE in the port. Ported 2026-07-25 and **OBSERVED FIRING** (below). 1.0's clamp also carried two never-taken `tstw`+`bcs` branches for the `< 0` half of `x < 0 \|\| x > 37` — THINK C zero-extends the byte and `tst` always clears carry, so the wrap case was caught by the unsigned `> 37` test; the dead branches changed nothing |
| 17 | `jt860` (CODE 18 `L003a`) | honour the design's **no-permadeath** flag. `hdr[29]` off -28006 is the same byte `l33d8` consults when deciding whether a wiped party is really destroyed; statuses 6/7/8 are the permanent removals (exactly the values the function's own entry switch treats as terminal) and 5 is the status `l33d8` REVIVES AT 1 HP when the flag is set. So 1.0 had a hole in the feature: a design could declare that characters never die permanently and Slay Living, Finger of Death, petrification and annihilation would still remove them for good, because they route through `jt860` with 6/7/8 and 1.0 wrote that straight into `rec[94]`. Reachable: `jt612` calls `jt860` with 6, `jt615` with 8 |
| 9 | `l611c` (CODE 10 `L611c`) | reconcile the **ability-score pairs** before the edited monster record is written back: `rec[113+2i] = rec[112+2i]` for i = 0..5 (STR/INT/WIS/DEX/CON/CHA) plus `rec[125] = rec[124]` for the exceptional-Strength percentile. The record keeps every ability twice — permanent at `112+2i`, current at `113+2i` (the layout `L24d2`'s roll writes, `docs/char-record-layout.md`; `l1d54` reads Strength from `rec[113]`). Only the permanent copy is meaningful in a monster TEMPLATE, so an edit lands there and 1.0 wrote the record out with the current bytes still stale. It sits one line below the existing `dest[395] = dest[129]` HP reconciliation, i.e. 1.2 extends a pattern the function already had. Direction is from the asm (`moveb %a0@(112),%a1@(113)`, both registers `dest + 2i`), not from the reading. Reachable via the Monster Editor (`jt263`, `jt264` dispatch case 21) |
| 2 | `l4d98` (CODE 6 `L4e3a`) | clear the **-22222 slot**, not the object it points at. 1.0 pushed the slot's CONTENTS (`movel %a5@(-22222)`), 1.2 pushes its ADDRESS (`pea %a5@(-22222)`); the callee is the same `l5f4e`/jt65 "zero `size` bytes at `ptr`". So on every new-game reset 1.0 zeroed the first 4 bytes of whatever -22222 pointed AT — the shared FC/art handle `jt204` hands to JT[115], the one `jt121` blits from and `jt124` commits as a palette — and left a dangling pointer in the slot. Every other clear in that reset block already takes the address of its own A5 slot (`-27894`, `-24204`, `-24236`), so -22222 was the odd one out; same shape of fix as hunks 10/11. The port's `!= 0` PORT-SAFETY guard went with it — it existed only because 1.0 dereferenced the slot |

Verified: same-harness before/after frames are byte-identical on the walk, camp,
Magic, chargen and map-editor paths (AE=0), i.e. no regression.

**Hunk 24 is OBSERVED FIRING (2026-07-25) — the first one.** The deterministic
A/B harness (`-DFRUA_RNGSEED`, `docs/deterministic-ab.md`) settled it. HEIRS
`GEO011` event **44** (`ev[12] = 0x20`, bit5 set; range bits 2), reached at
cell(col=5, row=17) of area 11:

| | `rec[56]` start range | `rec[55]` depth |
|---|---:|---:|
| hunk 24 ON | **0** | **0** |
| hunk 24 OFF | 2 | 2 |

Pixels: **AE 60696**. ON, BARBARUS stands on the tactical map adjacent to three
Driders with the party command bar up; OFF, the party is off-screen at range 2
and the Driders take the first round — "Composite Long Bow", "DRIDER is
Unaffected", `Spell: Mirror Image`. 1.0 ignored the designer's "start adjacent"
flag, so a melee ambush played as a ranged shooting gallery.

Two corrections to the 2026-07-24 entry that stood here:
- its AE=1012 was **pure RNG**, not the fix — with the seed pinned the same
  comparison gives AE=0;
- it named the wrong cell. `special = event index + 1`
  (`docs/geo-format.md`), so comparing the special byte to the index sent the
  run to record 13, whose bit5 is clear. The event-byte count (11 of HEIRS' 175
  combat events affected) was unaffected; only the cell attribution was wrong.

**Hunk 35 is OBSERVED FIRING (2026-07-25) — the second one, and it arms three
more.** The party-at-a-map-edge situation the note below used to call
outstanding turned out to be cheap to reach: `g_a5_18878 <= 4` routes play into
`l0b88()` (mode 3 = overland), and `FRUA_ENTRY_LEVEL` sets that byte. HEIRS area
1, party seated at row 37 (the last row of the 38x15 overland map) facing 2
(`drow = +1`), same seed, same key sequence, `-DFRUA_OVDIAG` for the state:

| | `cand-row` (-4904) | `ov_step_out_of_bounds()` |
|---|---:|---:|
| hunk 35 ON (1.2) | **38** — raw | **1** |
| hunk 35 OFF (1.0 clamp restored) | 37 — clamped back | 0 |

The step was refused and the party stayed at row 37; the two following
diagnostic records (a turn, then an in-bounds step) are identical between runs,
so the difference is attributable to the one deleted clamp. This is the
measurement behind the "dead code" claim above: with 1.0's clamp in place the
guard reads 0 at a genuine map edge, so **hunks 36-38 had never once been able
to fire** in the port until hunk 35 landed. Four hunks promoted by one deletion.

Found while setting this up: `FRUA_ENTRY_ROW`/`_COL` were **silently ignored on
the overland path**. `p[37]`/`p[38]` are the party cell there, and they are
copied from `-12288`/`-12287` in the branch ABOVE the harness override, so only
the facing took effect and the party landed on the design's own start cell (row
13, not 37). The override now mirrors that copy, shadows included.

The remaining ported fixes are still ported-and-non-regressing rather than
observed-firing — each needs its own situation (a design under test, a prompt
inside the Items browser, that one picker, a monster edited then saved). The
harness now exists to promote them one at a time; see ADR-0018.


### The `lXXXX` collision made this table lie — fixed 2026-07-25

The "lifted" column used to be a bare name grep (`\b[lL]<offset>\b` in
`boot.c`) with **no segment awareness**, which is exactly the trap CLAUDE.md's
naming rule warns about: the same hex offset is a DIFFERENT function in each
CODE segment. Consequences, both found by reading a hunk that the table
promised was portable:

- hunks **27/28** (CODE 20 `L26de`) matched the port's CODE **10** `l26de` and
  were recorded `lifted: yes`. CODE 20 + 0x26de is not lifted at all — they are
  **blocked**, not open.
- hunk **2** (CODE 6 `L4e3a`) matched the port's CODE **7** `l4e3a`. That one
  turned out portable anyway, but only because `L4e3a` is a label INSIDE the
  lifted `l4d98` — not because the grep was right.

`in_boot_c()` is now segment-aware: the port writes its provenance into each
definition's doc comment (`/* L611c (CODE 10+0x611c) — ... */`), so a hit
carrying an explicit `CODE <n>` marker is evidence either way. `--hunk` prints
mismatches as `IGNORE (other segment, same offset)` and warns when no hit names
the hunk's own segment. A second signal was added because the label check could
not see hunk 17 at all — its enclosing CODE 18 `L003a` is lifted as **`jt860`**,
so `jt_lifted()` now checks whether the JT export the hunk sits under is
defined in `boot.c`.

Column values now mean:

| value | meaning |
|---|---|
| `yes` | a `boot.c` hit names this segment — confirmed |
| `jtN` / `` `lXXXX` `` | lifted under that name (JT export, or a label inside a larger function) |
| `?` | the name appears but no hit names this segment — bare call sites only |
| `-` / `✗` | no evidence / confirmed absent |

**`?` and `-` do NOT mean "not ported".** The check is label-based, and a hunk's
enclosing label is often just a branch target inside a differently-named
function — hunks 14 (`L3426` in `l33d8`), 22 (`L2d74` in `jt893`) and 2
(`L4e3a` in `l4d98`) are all ported with a weak or absent signal. Trust the
hand-maintained **status** column; the lifted column is only a triage hint.


### Blocked on a prerequisite lift — hunks 25, 26 (CODE 20 `L24e6`)

Not "open": there is nothing to apply them to yet. 1.2 passes the event's
4-byte amount `ev[8]` through the 32-bit byte swap at CODE 4+0x22aa (1.0's
`JT[1199]`, the port's `jt1199`) before comparing it with the party's money —
an **endianness fix**, the same class as the port's own text-id endianness work
(#37). Reading a little-endian design field as big-endian gives a wildly wrong
threshold, so 1.0's "you have left N" test fired on garbage.

But `L24e6` is the temple's multi-line "a priest says you left money"
composition (a run of `jt96(1, row, 38, 22, 7, 0, 1, text)` calls), and the
port's `l24e6` lift deliberately skips all of it — its own comment records the
Yes/No and the donation transfer as leaf stubs with the faithful effect
deferred. Port the composition first; then lift the comparison in its 1.2 form
rather than 1.0's.

### Call retargets — 1, already ported

`mac12_diff.py --calltargets` (new) asks a question neither other pass can:
did 1.2 change WHICH jump-table entry a call goes to? `--operands` normalises
`%a5@(positive)` away as renumbering noise, so a genuine retarget is invisible
there, and both instructions being `jsr` hides it from `--list` too.

Answer: exactly **one** retarget in the whole release — CODE 10 `L6238`'s
`JT[436]` → `JT[431]`, which is hunks 10/11, already ported. So there are no
hidden call-target changes left to find.

Two failed discriminators are recorded in the tool, because both looked
plausible and both were wrong: comparing JT **indices** ("one entry removed, so
a delta of 0 or 1") gives 3651 false hits, and comparing the target's **CODE
segment** barely helps because the table is grouped by segment. What works is
target identity — the (segment, offset) pair, falling back to rank among the
segment's exports.

## Frame-slot changes — 134, all churn

`--operands` normalises `%fp@(N)` away because a 1.2 function that gained a
local renumbers every slot below it. That also hid the class of bug the tool
exists to find, so `mac12_diff.py --frameslots` reports frame slots separately.

Frame SIZE alone does not separate fix from churn: CODE 19's `L25ce` (`jt893`)
keeps `linkw #-28` on both sides yet moves seven distinct slots. The
discriminator that works is per-FUNCTION shape — a real wrong-slot bug changes
ONE use site while its siblings stay put; a re-layout renames EVERY use of the
local. Grouping must be by enclosing function (the last `linkw`), not by nearest
label, or one re-layout leaks out as several single-mapping "fixes".

**Result: 0 genuine frame-slot fixes, 134 churn rows** — 111 in CODE 19, 23 in
CODE 10, i.e. entirely inside the two functions 1.2 restructured and that are
already ported (`jt893`, hunks 19–22; `l6238`, hunks 10–11, whose frame grew
202 → 218 bytes). This **retracts** the audit's "two stack-slot corrections"
(`%fp@(-24)` → `%fp@(-20)` in CODE 19, `%fp@(-7)` → `%fp@(-9)` in CODE 10):
both rename every use of their local and are re-layout, not fixes. They were an
artifact of the first unusable 229-difference run.

## Operand-only changes — 93

Instructions whose mnemonics align but whose meaningful operands differ. The
pass normalises away jump-table slots, branch targets, CREL absolutes **and
frame slots** — several 1.2 functions gained a local, which renumbers every slot
below it and accounted for 136 of the raw 229 differences. Negative A5
displacements are NOT normalised: those are A5-world globals, and `DATA`/`DREL`
are byte-identical between the releases, so a change there would be real.

| segment | count | what it looks like |
|---|--:|---|
| CODE 21 | 61 | not yet analysed |
| CODE 20 | 15 | one is now analysed + ✅ PORTED: the `l0098` tolower range — see below |
| CODE 17 | 13 | **a systematic record-field correction** — see below |
| CODE 7, 12, 18, 19 | 1 each | CODE 7 is `cmpib #57` -> `cmpib #90`, a `'9'` -> `'Z'` bound near `L21e6` — ✅ PORTED, see below |

### The two character-range changes — ✅ PORTED

Both are `cmpib #57` -> `cmpib #90`, and they are NOT the same fix.

**CODE 20 `l0098`, 1.0 @0x011e -> 1.2 @0x00ee** (with its partner `cmpib #48`
-> `cmpib #65` at 1.0 @0x0106): the whole range moves from `'0'..'9'` to
`'A'..'Z'`. The matched byte gets `addiw #32` — `tolower()` — so 1.0's range was
simply wrong: adding 32 to a digit maps `'0'..'9'` (48..57) onto `'P'..'Y'`
(80..89) and lower-cases no letter at all. 1.2 fixes the range. The port had
1.0's digit clause *plus* an added `A-Z` clause, i.e. it reproduced the
corruption and patched around it; the digit clause is now gone.

*Measured scope:* the corruption is real but **unexercised**. STRG's 6-bit
alphabet does encode digits (codes 48..57), yet across every design on hand
(HEIRS, BEOWOLF, GIANTS, TUTORIAL, Game39/40) **406** strings carry a `~`/`^`
option marker and **not one** contains a digit. So this changes no observable
output today — it removes a latent corruption a digit-using prompt would hit.

**CODE 7 `l2184`, 1.0 @0x2212 -> 1.2 @0x21c2**: 1.0's word-start test is
`(c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')`; 1.2 raises the second
clause's upper bound to `'Z'`, making the effective predicate the contiguous
`'0'..'Z'` and subsuming the first clause. Net delta: the seven characters
between `'9'` and `'A'` — `:;<=>?@` — now also start a word. Ported faithfully
but flagged as odd: unlike the CODE 20 change this is not obviously the intent
rather than a sloppy widening. Verified no regression on `jt904`'s record sheet
(the screen `l2184` feeds), which renders identically.

### The CODE 17 cluster — ✅ PORTED (13 sites)

Thirteen sites across a run of small predicates (`L6dcc`, `L6dfe`, `L6e28`,
`L6e58`, `L6e7a`, `L6eac`, `L6ed6`, `L6f58`, …) change
`moveb %a0@(113)` -> `%a0@(112)` (9 sites) and `%a0@(115)` -> `%a0@(114)` (4).

Per `docs/char-record-layout.md` the abilities are stored as pairs —
`rec[112 + i*2]` is the **base** score and `rec[113 + i*2]` the **working /
current** one. So 1.2 switches these checks from the current score to the base
score: a character temporarily drained or boosted no longer passes or fails a
chargen check it should not.

**Resolved:** those labels are not functions at all — they are branch labels
inside ONE function, `CODE 17+0x6cd2` = **JT[557] = `jt557`**, the Training
handler. The 13 sites are its copy of the AD&D racial level-limit table (switch
on race `rec[88]`, per class slot), and our lift has the identical chain at
`boot.c` ~31350-31400 with exactly 9 reads of `rec[113]` and 4 of `rec[115]` —
matching the asm site counts. Ported by reading the BASE bytes into `baseStr` /
`baseInt` at the top of the loop.

Why it matters: the working score is temporary. A strength-drained fighter would
hit a lower racial cap than their race allows; one standing in a temporary boost
could train *past* their real cap — and since training permanently raises the
level, that exploit outlives the boost.

**Caveat, and it is a big one: this fix cannot fire in the port today.**
`jt557` has **no caller** — `grep -n 'jt557 *('` finds nothing but its own
definition, and clicking "Train Character" in the Training Hall (verified live,
with a party seated) does nothing at all: no picker, no message, no state change.
The Mac dispatches JT[557] from `CODE 10+0x5cec` and `CODE 12+0x0f68`; neither
call was wired during the lift. So the ported fix is correct and inert until
task #78 wires the menu. See the function audit §3.

## Not yet analysed

26 structural hunks (1, 3–6, 8, 9, 12–18, 23–29, 31–35) and 80 operand changes,
concentrated in CODE 20/21 (the play loop and camp/overland). Each is 1–20
instructions; `mac12_diff.py --hunk N` prints both sides with the enclosing
label and any boot.c mentions, which is how the nine above were done.
