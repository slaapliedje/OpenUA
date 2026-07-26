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
| 1 | 6 | `L25c0` | insert | 0 | 5 | jt39 | ✅ ported | jt39: no-permadeath skips BOTH status-6 arms — hunk 17's fix on the damage route |
| 2 | 6 | `L4e3a` | replace | 1 | 1 | `l4d98` | ✅ ported | l4d98: clear the -22222 SLOT (`pea`), not the object it points at (`movel`) |
| 3 | 7 | `L1a66` | delete | 11 | 0 | `l1a0c` | ✅ ported | l1a0c: a digit no longer ends a word — only isupper does |
| 4 | 7 | `L1ace` | replace | 1 | 1 | `l1a0c` | ➖ churn | `beqw` -> `beqs`: branch width, shrunk by hunk 3's deletion |
| 5 | 7 | `L21b6` | delete | 18 | 0 | `l2184` | ✅ ported | l2184 boundary test 1: drop the digit alternative |
| 6 | 7 | `L22a8` | replace | 19 | 1 | `l2184` | ✅ ported | l2184 boundary test 2 (scan to end of word): same; dead in the asm, live in the port |
| 7 | 7 | `L3f80` | replace | 1 | 1 | yes | ✅ ported | l2ebc key-mode arg 0 -> 1 (boot.c ~95700) |
| 8 | 10 | `L38ba` | replace | 1 | 1 | `l36e0_c10` | ✅ ported | jt399 fill 0 -> 1: the arttype==2 CLUT hole becomes a no-op |
| 9 | 10 | `L611c` | insert | 0 | 21 | yes | ✅ ported | l611c: sync each ability's CURRENT byte from its PERMANENT byte before the save |
| 10 | 10 | `L6238` | replace | 1 | 1 | yes | ✅ ported | l6238: build the path forwards (clear + 2x jt431) instead of jt436's in-place dir prefix |
| 11 | 10 | `L6238` | replace | 1 | 5 | yes | ✅ ported | l6238: the second jt431 join (same change as hunk 10) |
| 12 | 12 | `L071c` | replace | 1 | 2 | n/a | 🚫 artefact | DISASSEMBLY DESYNC: an inline dispatch table after `jsr JT[1]` decoded as instructions. Both listings are garbage here — not a comparable change |
| 13 | 12 | `L0d3e` | insert | 0 | 16 | `l0aae` | ✅ ported | Training Hall: after a RESUMED save, force-enable Create/Delete/Add/Remove/Load/Save/Exit |
| 14 | 12 | `L3426` | replace | 1 | 5 | `l33d8` | ✅ ported | l33d8 pass 2: skip SUMMONED combatants (`mc[21]==1`) so a conjured creature cannot mask a party wipe |
| 15 | 13 | `L105c` | insert | 0 | 5 | `l102a` | ✅ ported | l102a: no-permadeath also stops the per-round BLEED-OUT (dying -> dead at >9) |
| 16 | 16 | `L50cc` | insert | 0 | 3 | `l4faa` | ✅ ported | l4faa default arm: jt179(0) — init the slot table l2184 reads instead of inheriting a stale one. **OBSERVED FIRING** |
| 17 | 18 | `L003a` | insert | 0 | 13 | jt860 | ✅ ported | jt860: honour the design's no-permadeath flag — status 6/7/8 downgrades to 5. **OBSERVED FIRING** |
| 18 | 18 | `L61d4` | replace | 1 | 1 | `jt822` | ✅ ported | jt822: the effect-148 VALUE word 0 -> 1 (magnitude, one per victim). **OBSERVED FIRING** |
| 19 | 19 | `L25ce` | insert | 0 | 2 | yes | ✅ ported | jt893: hoist the -22281 save+clear to entry. **OBSERVED FIRING** |
| 20 | 19 | `L2c20` | delete | 2 | 0 | yes | ✅ ported | jt893: 1.0 per-case save+clear deleted. **OBSERVED FIRING** |
| 21 | 19 | `L2c20` | delete | 1 | 0 | yes | ✅ ported | jt893: 1.0 per-case restore deleted. **OBSERVED FIRING** |
| 22 | 19 | `L2d74` | insert | 0 | 1 | `jt893` | ✅ ported | jt893: restore at the single exit. **OBSERVED FIRING** |
| 23 | 20 | `L00c4` | delete | 18 | 0 | `l0098` | ✅ ported | l0098: the OTHER half of the tolower fix — deletes 1.0's `A-Z` pre-test; already covered by the A-Z-only implementation |
| 24 | 20 | `L18e2` | insert | 0 | 7 | `l159a` | ✅ ported | l159a combat entry: `ev[12]` bit5 forces the starting range (`rec[56]`) to 0 |
| 25 | 20 | `L24e6` | insert | 0 | 2 | `l216a` | ⛔ blocked | byte-swap `ev[8]` before the money compare; `l216a` IS lifted but its "generous donation" block is a deferred sub-flow (`l026e_c20`/`l4218` stubs) |
| 26 | 20 | `L24e6` | replace | 1 | 1 | `l216a` | ⛔ blocked | the compare rearranged to suit hunk 25 |
| 27 | 20 | `L26de` | insert | 0 | 4 | `l216a` | ✅ ported | l216a: byte-swap the 4-byte `ev[8]` before jt933 (`jt1199`) |
| 28 | 20 | `L26de` | delete | 1 | 0 | `l216a` | ✅ ported | the bookkeeping half of 27 (a reload dropped); no separate content |
| 29 | 20 | `L3114` | insert | 0 | 2 | `l2e42` | ✅ ported | l2e42: set -4942 ("transition done") once the animated passage has moved the party |
| 30 | 20 | `L57a0` | insert | 0 | 5 | yes | ✅ ported | l5676: "Transfer module ends testing!" before the test-play teardown |
| 31 | 20 | `L70d4` | insert | 0 | 1 | `l709e` | ✅ ported | l709e: clear `-4943` per event so the deferred re-trigger flag cannot leak across a chain |
| 32 | 20 | `L76c4` | replace | 3 | 1 | `l709e` | ➖ churn | codegen only: `moveq`+`moveb`+`tstw` -> `tstb`. Same test, no semantic change |
| 33 | 20 | `L76fa` | delete | 1 | 0 | `l709e` | ✅ ported | l709e: drop the tail `-4943` clear, redundant once hunk 31 clears at the loop top |
| 34 | 21 | `L13f6` | insert | 0 | 4 | `l1374` | ✅ ported | l1374: effect id 73 joins the spell-effects DISPLAY whitelist (position is an artefact). **OBSERVED FIRING** |
| 35 | 21 | `L3af2` | delete | 20 | 0 | yes | ✅ ported | l3af2: DELETE 1.0's silent clamp — the half that ARMS hunks 36-38. **OBSERVED FIRING** |
| 36 | 21 | `L4816` | insert | 0 | 8 | yes | ✅ ported | jt955 case 3: overland bounds guard #1 + the refusal message |
| 37 | 21 | `L4816` | insert | 0 | 5 | yes | ✅ ported | jt955 case 3: overland bounds guard #2 (out of range == blocked) |
| 38 | 21 | `L4874` | insert | 0 | 8 | `jt955` | ✅ ported | jt955 case 3: the second guard's tail |

### Ported: 33 of 38 — the structural pass is COMPLETE

2 churn (4, 32), 1 artefact (12), 2 blocked (25, 26). Nothing else is open.

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

| 27, 28 | `l216a` (CODE 20 `L26de`) | byte-swap the 4-byte `ev[8]` before handing it to `jt933`. ENCR records are stored little-endian — the port already assembles every other multi-byte event field that way by hand (`lo \| (hi << 8)`) — so reading a long straight off `ev+8` on a 68k gives it reversed. 1.2 routes it through CODE 4+0x22aa = the port's `jt1199`. **Mind the index:** that entry is JT[1199] in 1.0's table and JT[1198] in 1.2's permuted one, and the port's own `jt1198` is an unrelated plane-count helper — match on (segment, offset). Reachable with real data: of the 836 GEO*.DAT files on hand, **192 hold type-9 temple events and every one stores bytes 8..11 as `01 00 00 00`** — little-endian 1, which 1.0 reads as 16,777,216, off by 2^24 in the direction that makes a threshold unsatisfiable. Hunk 28 is only the bookkeeping half (1.2 keeps the result in d0 and drops a reload) |
| 1 | `jt39` (CODE 6 `L25c0`) | honour no-permadeath on the DAMAGE route — hunk 17's fix from the other side. When `hdr[29]` is set, 1.2 branches past BOTH status-6 ("destroyed") outcomes — `over > 9` massive overkill, and `dealt == 0 && rec[94] == 1` — straight to the `over > 0` arm, which lands on status 5, the one `l33d8` revives at 1 HP. Hunk 17 plugs the explicit-status route (a spell naming 6/7/8); this plugs damage promoting a character to destroyed on its own. **The branch target re-tests `over`** (`tstw %fp@(-2); blss L2616`), so jumping there cannot mislabel a survivor — worth stating because the insert sits at the JOIN of the would-die and survives branches and so runs on both. `hdr[29]` is per-combat, set from the combat event's `ev[12] & 0x40` (`boot.c` ~48861) and cleared after the fight; **19 such combats exist** across the fan modules on hand (Game40 x9, Game39 x4, G39INST/G39RAW x2 each, Curse, GAME39), so both hunks are reachable — just not in HEIRS |
| 29 | `l2e42` (CODE 20 `L3114`) | set `-4942` ("transition done") once the animated passage has finished walking the party. That is l709e's chain-control flag: its loop breaks on `-4945 == 0 \|\| -4942 != 0` and the tail latches it into `-4941`; other movement handlers already set it. `l2e42` physically relocates the party over `ev[6]` frames, so without this the event chain kept running with the pre-move context and could fire follow-on events resolved against the cell the party had just left |
| 33 | `l709e` (CODE 20 `L76fa`) | drop the tail `clrb -4943`. Companion to hunk 31, which moved that clear to the top of the loop body — with it there this one is redundant (every iteration would clear twice). **Unlike hunk 35 this deletion arms nothing:** the loop-back chain at 0x7708 does test `-4943`, but the `tstb -4942; beqw L70d4` immediately above always branches (`-4942` was cleared four instructions earlier), so that test is unreachable in BOTH releases. Also corrects hunk 31's note, which claimed 1.0's tail clear sits inside `if (-4945 == 0)`: the guard at 0x76a6 is `tstb -4945; bnes L76fa`, so L76fa — the clear — is the JOIN both paths reach and is unconditional. The leak hunk 31 fixes is across CALLS (nothing cleared the flag on entry), not across chained events within one call |

| 3, 5, 6 | `l1a0c` + `l2184` (CODE 7) | **digits stop being word boundaries** in both prompt splitters — one coherent CODE 7 fix, and the same "digits are not letters" family as hunk 23's tolower correction. 1.0's boundary test is `isupper(c) \|\| isdigit(c)`; 1.2 deletes every digit clause, leaving `JT[408]`/isupper alone. Measured, not inferred: `l2184` goes from four `cmpib #48/#57` compares to **zero** (the four `cmpib #65/#90` survive), `l1a0c` from two to **zero** with its single JT[408] intact. Why it matters: `l2184` picks the Nth boundary-delimited word by index, so a digit acting as a boundary injects a spurious word and shifts every later index — any prompt containing a number ("1st level", "2 gold") extracted the wrong words from that point on. `l1a0c` is worse: it terminates each word IN PLACE by overwriting the boundary char with NUL (`*(p - 1) = 0`), so a digit boundary CORRUPTED the caller's prompt buffer, chopping "1st" into "" + "st". Hunk 6's site was already inert in the asm (1.0 reaches its digit test only when isupper is true, and nothing is both, so it always fell through) — 1.2 deletes the dead branch, 19 instructions down to 1 — but it was NOT inert in the port, which had written the two clauses as a plain `\|\|`. Verified live: the `View \| Take \| Pool \| Share \| Exit` bar and `Press Return to continue.` still render with correct boxed accelerators |

| 13 | `l0aae` (CODE 12 `L0d3e`) | after a **resumed saved game**, force the roster-management and save/load verbs enabled whatever the flag walk decided. `-18485` is the play-loop mode byte (0 = fresh "new game", non-zero = resumed); the `-14440..-14429` cluster the enable loop reads is computed for the fresh-start case, so on a resume 1.0 could leave the Hall showing a menu where you could not Save, Load, Add or Remove anyone — exactly the verbs a resumed session needs. cmd 16 SETS rec[28] bit 0, the same command the loop's enabled arm uses. By the #82 slot mapping the three calls cover 0-1 (Create, Delete), 6-9 (Add, Remove, Load Saved Game, Save Current Game) and 11 (Exit From Play) — pointedly NOT 2-5 (Modify/Train/Human Change Class/View) nor 10 (Begin Adventuring). Callees by (segment, offset): CODE 3+0x30ba = the port's `l30ba(start,end,cmd)`, CODE 3+0x3056 = `jt444`; 1.2 numbers them JT[445]/JT[443], 1.0 numbers them JT[446]/JT[444], and the port's `jt445` is an unrelated CODE 3+0x294e stub |
| 15 | `l102a` (CODE 13 `L105c`) | the no-permadeath flag also stops the per-round **bleed-out** — the third and most consequential site in that family. A status-5 (dying) character ticks `mc[16]` once per combat round here and converts to status 6 (dead for good) at >9. Hunks 17 and 1 funnel characters INTO status 5 precisely so `l33d8` can revive them at 1 HP; this is what stopped that from working, so a design declaring no permadeath still lost anyone left dying for ten rounds |
| 16 | `l4faa` (CODE 16 `L50cc`) | call `jt179(0)` in the default arm. Every other arm already initialised the `-24126` slot-index table; the default arm did not, so it inherited whatever the PREVIOUS menu left there — and that table is exactly what `l2184` reads to decide which prompt word belongs to which slot, so the one-verb "Exit" prompt could extract the wrong word. Same subsystem as the CODE 7 hunks 3/5/6, which is the reason to port them together. The argument is 0 because `jt179` writes indices 0..count and this arm has a single entry |
| 18 | `jt822` (CODE 18 `L61d4`) | the effect-148 VALUE word goes 0 -> 1. `jt876` stamps `node[2] = value`, so 1.0 appended the effect with magnitude 0 — present on the list but carrying nothing for readers that scale by it; 1.2 makes it one per victim, matching the accumulating loop. (The pre-existing faithful oddity stands: jt876's target is `rec`, the SOURCE, not the victim.) |
| 34 | `l1374` (CODE 21 `L13f6`) | effect id **73** joins the spell-effects DISPLAY whitelist, so an active effect 73 now shows on the party's effects screen instead of being silently omitted. Appended out of ascending order because that is where 1.2 puts it (the cascade tests 73 after 172 — tacked on the end). **The hunk's position is an alignment artefact:** it points at the `#1` test as "4 inserted instructions", but the function is a 60-deep cascade of identical `moveq/moveb/cmpiw/beqw` quads, so difflib can place the insert anywhere in the repeat. Extracting every `cmpiw` operand from both listings shows 60 identical tests and a 61st in 1.2 — compare the SET, not the position |
| 8 | `l36e0_c10` (CODE 10 `L38ba`) | the `jt399` fill value goes 0 -> 1, which makes the whole `arttype == 2` special case a **no-op**: the line above has just filled all 768 bytes of `clutbuf` with 1, so 1.0 was punching a 432-byte hole of zeroes into the middle of an otherwise all-ones buffer for this one art type and 1.2 stops. Kept as an explicit call rather than deleted, because that is what 1.2 does — same instruction, different immediate |

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

**Promotion status: 18 of 33 observed firing, 2 established as unable to fire.**

**Hunk 34 is OBSERVED FIRING (2026-07-25) — it was filed as CANNOT FIRE and
that was wrong.** The camp spell-effects screen, same script, same effect on
the record, same `-20096` name pointer:

| | the effects screen |
|---|---|
| 1.2 (ON) | `BARBARUS` / **Immune to Dragon Breath** |
| 1.0 (OFF) | `BARBARUS` / **&lt;No Spell Effects&gt;** |

The log isolates it: both runs print `l1374 effect on member: id 73` and differ
only at `in whitelist` 1 vs 0.

The old verdict rested on a producer census — "no `jt876` call anywhere applies
kind 73". The census was accurate; the conclusion was not. Hunk 18 established
that an item's byte-15 effect id feeds `jt820` through `l77a0`'s override, and
that produces ANY kind on demand, 73 included. **A census over shipped data
proves the situation is not SHIPPED, never that it is unreachable** — the
question to ask before closing a hunk is whether it is AUTHORABLE.

The second way this hunk could have been inert WAS checked rather than assumed:
`l1374` drops any whitelisted effect whose `-20096` name is empty, so 73 joining
the list would change nothing if 73 were nameless (the shape of hunk 8).
Measured — the name is "Immune to Dragon Breath".

Hunk **23** has been reclassified out of "cannot fire" for the same reason: its
note also said "authorable, but nothing shipped exercises it", which is a
statement about the data. The two entries left (8 and 33) are no-ops by
construction — unreachable for reasons internal to the code, not the data.

**Hunk 18 is OBSERVED FIRING (2026-07-25)** — the one that looked dead and was
not. Same source, same victim, same seed:

    ON   jt822 ENTRY source BARBARUS / VICTIM BASILISK / fx148 node value 1 / count 1
    OFF  jt822 ENTRY source BARBARUS / VICTIM BASILISK / fx148 node value 0 / count 1

Getting there took a producer census, and the census said NO. Nothing on hand
puts effect kind 137 (the `jt822` slot) on a record: the spell table's `def[10]`
kinds top out at 123 across all 137 spells; the id-117 roller (`a5 -15024`)
produces 106; no literal `jt876`/`jt871`/`l3dfe` call uses 137; and 12 item
tables (1,600+ records) max out at 126. On that evidence hunk 18 belonged in the
"cannot fire" list next to 34.

One more measurement changed the answer. `l77a0`'s override slot `-24734` is
non-zero and **equals `jt820`** — and `jt820` is exactly the function that turns
item data into an effect (it mirrors an item's byte-15 effect id onto its
bearer). It is unreachable through the type table, which is why the census
missed it. So the situation IS authorable: patch `[15] = 137` and `[16] = 0x80`
into a saved character's own item-template copy, ready the item, and the next
combat turn's `jt868(15)` sweep detonates it.

Two corrections this produced. The `jt876` value slot is a **WORD**
(`*(short *)(node+2) = c`), so reading `node[2]` as a byte returns its high half
and reports 0 for BOTH builds — the first run looked like a dead hunk for that
reason alone. And `jt822` runs happily outside combat with zero victims, so
`jt822 ENTRY` proves nothing; only `jt822 VICTIM` does.

Recipe and the remaining traps are in `docs/deterministic-ab.md`.

**Hunks 19-22 are OBSERVED FIRING (2026-07-25)** — four at once, since they are
one 1.2 change split across four diff sites. Authored SHOPPIC.DSN
(`tools/mk_bigpic_design.py --shop`): a shop event with a bigpic backdrop, so
`l442e` leaves `-22281` set and `jt183` puts the play mode at 1. Then
View -> Items -> click an item row -> Sell:

    ON   jt893 ENTRY saved -22281 1 / in-browser 0 / loop top 0, 0 / confirm sees 0
    OFF  jt893 ENTRY saved -22281 1 / in-browser 1 / loop top 1, 1 / confirm sees 1

The OFF build reconstructs 1.0's SHAPE rather than just deleting the fix: entry
save+clear and exit restore removed (19, 22), per-case pair restored inside
case 4 (20, 21).

**The arm matters as much as the flag, and that cost two runs.** Outside a vault
`l11a8` offers arm 4, not arm 3 — so the browser's visible `Drop` button is the
trade/give arm, the one arm 1.0 ALREADY suppressed. Running through it gives
`confirm sees 0` on both builds, which reads like a dead hunk and is actually a
correct negative control. The shop's Sell (arm 7, `jt189`) and Id (arm 8,
`jt190`) are the arms 1.0 left unsuppressed.

**What the flag does:** `jt182` hands it to `l23b4`, which uses it to gate a
per-iteration animation block — 1.0 kept combat sprite animation running behind
Items-browser prompts. That block additionally needs a staged animation
(`-24321 > 0 && -24206 >= 1`), which the shop has none of, so the two frames
here are pixel-identical and the divergence is state-only. A visible
demonstration still wants a bigpic prompt with an animation loaded.

**Hunk 17 is OBSERVED FIRING (2026-07-25)** — and it is the most consequential
of the set: the two builds end the session differently. NOPERMA.DSN built with
`--monster 42`, so the opposition is a BASILISK whose petrifying gaze calls
`jt860(target, 7)` on a party member. Same seed, everything upstream identical
(`dealt 23`, `target hp 28`, `attack dir 0`):

    ON   jt860 req-status 7 / subject BARBARUS / side 0 / hdr29 1
         -> final-status 5,  then l102a "BLEED SUPPRESSED, status stays 5"
    OFF  jt860 req-status 7 / subject BARBARUS / side 0 / hdr29 1
         -> final-status 7,  and no l102a line at all (he is stone, not dying)

1.2 leaves BARBARUS in the roster at **1 HP** — `l33d8` revives the status-5
character — and play continues. 1.0 prints **"The monsters rejoice, for the
party has been destroyed!"** and the session is over, in a module that
explicitly declared characters never die permanently. The `subject`/`side`
diagnostic is what makes this airtight: the record is a PARTY member, not a
monster, so this is the feature working as designed and not an incidental
side effect on the enemy side.

The same run also fires hunks **1** (`jt39 no_perma 1`) and **15** (the bleed
clock), so all three members of the family are now demonstrated together —
which is the right way to read them, since 1 and 17 funnel characters INTO
status 5 precisely so 15 and `l33d8` can bring them back.

**The recorded blocker was wrong and cost most of a session.** "Needs a caster"
came from this log's own note that `jt612`/`jt615` are the reachable routes. A
caster is not needed at all — a monster gets there. And the caster route would
not have worked as assumed anyway: measured off the stock spell table, no
lethal spell fits the auto-turn's picker (111/114 are single-target but have a
cast time; 110/125 are instant but area-mode), so it would have needed a new
harness flag on top. Traps and the one-run control that settled the "empty
fight" red herring are in `docs/deterministic-ab.md`.

**Hunk 16 is OBSERVED FIRING (2026-07-25)** — and it is the only promotion so
far whose divergence is visible on SCREEN. Authored a Magic-User with memorized
spells (`tools/mk_caster_chr.py`), added them in the Training Hall, then
View -> Spells: `jt904` -> `jt595(0, 0)` -> `l4faa` mode 0, the default arm.

    ON   -24126 in  1 'S' 7 'E' FF ...    out  0 FF FF FF FF ...
         l2184 src "Exit"  ->  "Exit"
    OFF  -24126 in  1 'S' 7 'E' FF ...    out  1 'S' 7 'E' FF ...
         l2184 src "Exit"  ->  ""

**The 1.0 build's spell picker has no `Exit` button.** A whole-frame pixel diff
of the two runs differs in exactly one region — x 24..103, y 428..449, the
command-bar button — and is identical everywhere else.

The chain is short once the state is right. `jt904` builds its command bar with
`jt155`, which writes each verb's index into `-24126[i*2]`; `l4faa`'s default arm
was the one arm that never re-initialised that table, so it inherited the View
bar's mapping. `l2184` then extracts the Nth boundary-delimited word of the verb
line by matching `-24126[out_idx*2] == iter_char`: with `[0] == 1` the sole
uppercase letter of `"Exit"` sits at `iter_char 0`, matches nothing, and
`-13000` stays empty.

That last step is also why the setup needs a caster with **no inventory**. With
items, `jt904`'s first `jt155` call is `jt155(0)` and the stale `[0]` is already
0 — the same value `jt179(0)` writes — so the fix is a no-op and the A/B reads
as a dead hunk. The recipe and its traps are in `docs/deterministic-ab.md`.

**Hunk 9 is OBSERVED FIRING (2026-07-25)** — the first promotion through the
editor UI, and the cleanest evidence of the set because the observable is a
file. Monster Editor (main-menu hotkey `M`) -> BASILISK (monster id **42**) ->
Edit -> Strength `10` -> `18`, exceptional-Strength % `0` -> `50` -> Ok. Two
runs of the identical injected-click script, one per build, then byte-diff the
saved `MONST042.dat`:

    ON  pairs [(18, 18), ...]  pct (50, 50)
    OFF pairs [(18, 10), ...]  pct (50,  0)
    diff: [(113, 18, 10), (125, 50, 0)]

**Exactly two bytes differ across all 450**, and they are the two the hunk
writes — offset 113 from the `i = 0..5` loop, offset 125 from the separate
trailing percentile statement. Both halves exercised, which took two edited
fields; a Strength-only edit leaves 124/125 equal and proves only the loop.

The live `l611c` diagnostic shows the divergence being repaired —
`perm 18 / cur 10`, `pct perm 50 / pct cur 0` — which is 1.0's bug exactly: the
editor writes the PERMANENT byte, and 1.0 saved the record with the CURRENT byte
still holding whatever was loaded. Note an UNEDITED save proves nothing: every
shipped record already has `rec[113+2i] == rec[112+2i]` (checked on HEIRS'
MONST101/102/108/109 and on stock BASILISK), so the fix would write back bytes
already in place. The divergence must come from an edit. Recipe and the four
traps that cost a run each are in `docs/deterministic-ab.md`.

**Hunks 27/28 are OBSERVED FIRING (2026-07-25).** `tools/mk_temple_design.py`
authors a type-9 temple event whose bytes 8..11 read **100** little-endian and
**1,677,721,600** big-endian, so the two interpretations cannot be confused. At
the live `jt933` call site, one run logs both:

    l216a raw *(long*)(ev+8)  1677721600     <- what Mac 1.0 passes
       swapped (passed)              100     <- what 1.2 passes

No ON/OFF pair was needed: the diagnostic reads out the 1.0 value and the 1.2
value from the SAME live event record, which is stronger evidence than two runs.
Every one of the 192 type-9 events in the fan modules stores `01 00 00 00` there,
so in the wild 1.0 reads 1 as 16,777,216 — off by 2^24 in the direction that makes
any threshold unsatisfiable.

**Hunk 15 is OBSERVED FIRING (2026-07-25).** Same authored NOPERMA.DSN, but with
TWO monsters instead of six groups of 31 — the first attempt's 186 monsters each
take an animated turn on a 16 MHz 030, so a single round outlasted any sane
timeout and the run looked like a stall. With two, round 2 arrives:

| | `l102a` with a dying member (`hdr29 1`) |
|---|---|
| ON (1.2) | **BLEED SUPPRESSED, status stays 5** |
| OFF (1.0) | **bleed tick mc[16] 5** |

More monsters is not a faster death — it is a slower round.
The full table — what each remaining fix needs, and why 8 / 33 / 34 / 23 cannot
be promoted at all — lives in `docs/deterministic-ab.md`. Summary: 24, 35, 36,
37, 38 and 1 are measured ON-vs-OFF; hunk 8 is a no-op by construction, 33 has
no reader after `l709e`, nothing in the port applies effect 73 (34), and no
shipped design has a digit in an option string (23).

**Hunk 1 is OBSERVED FIRING (2026-07-25).** It needed a situation no shipped
design provides, so the situation was authored: `tools/mk_noperma_design.py`
builds a room whose entry cell fires a combat with `ev[12]` bit 6 (the byte the
combat entry copies into `hdr[29]`) and bit 5 set, plus `-DFRUA_PARTYHP=1` to
clamp HP so the fight reaches the overkill branch. Same seed:

| | `no_perma` | `over` | `status-out` |
|---|---:|---:|---|
| hunk 1 ON (1.2) | 1 | 10 | **5** — dying, revivable |
| hunk 1 OFF (1.0) | 0 | 10 | **6** — destroyed |

The run carries its own negative control: a second hit with `over 6` gives
status 5 either way, so the divergence belongs to the `over > 9` branch and is
not a blanket change. Two details worth keeping — the guard applies to ANY
combatant, not just party members (the measured death was a kobold), and
`FRUA_PARTYHP=2` gave `over 9`, one short of the branch, so the first run proved
nothing. **When a fix guards a threshold, aim the harness at the threshold.**

**Hunks 36-38 are OBSERVED FIRING (2026-07-25)** — the same overland step that
promoted 35, read one level deeper. At the map edge: `has_str` 1 (the refusal
string reached -5213), `msg[0]` 84 = `'T'` of "There is no way to go in that
direction.", `blocked t` 1 with `jt210` never consulted. One cell in bounds:
all three zero. That is precisely what the three hunks do — guard, message,
blocked path.

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


### Phantom hunks: where the differ cannot be trusted

Two of the 38 turned out not to be changes at all. Both are worth knowing before
chasing a hunk that will not make sense.

**Hunk 12 — disassembly desync (a whole class).** CODE 12 @0x0730 is
`jsr %a5@(42) -> CODE 1+0x130 (JT[1])`, and from 0x0734 the listing degenerates
into `orib #18,%d4`, `.short 0x004e`, `orib #83,%ccr`. That is an INLINE
DISPATCH TABLE being decoded as instructions — the THINK C pattern CLAUDE.md
documents for JT[3], here after its JT[1] sibling. Both listings are garbage in
that region, and 1.2's table entries differ because the segment's other changes
shifted the offsets, so the aligner reports a hunk. There is nothing to port.
Tell-tale: implausible mnemonics (`orib` into `%ccr`, bare `.short`) in the
hunk's own context. Check for a `jsr %a5@(42)` / `%a5@(58)` just above.

**Hunk 34 — insertion point inside a repeat.** `l1374` is a 60-deep cascade of
identical `moveq/moveb/cmpiw/beqw` quads, and difflib may place an insertion
anywhere within a repeat, so the hunk pointed at the FIRST test. Extracting
every `cmpiw` operand from both listings showed 60 identical tests plus a 61st
in 1.2 (`73`). **In a repetitive region, compare the SET of operands, not the
reported position.** The same reasoning retired hunk 6's "19 instructions
changed" to "a dead branch deleted".

### `--triage`: the enclosing FUNCTION, not the nearest label

Added 2026-07-25, and it moved eleven hunks out of "unknown". A hunk's nearest
LABEL is usually a branch target *inside* a function, so it answers the wrong
question — `mac12_diff.py --triage` walks back to the function PROLOGUE instead
(THINK C's `linkw %fp,#-N` after a return, or the first labelled instruction
after a return for a frameless leaf) and reports whether the port lifted that.

It immediately overturned two calls in this file:

- hunks **27/28** were marked blocked because CODE 20's `L26de` is not lifted.
  The enclosing function is `L216a@0x216a` — a ~1862-byte dispatcher that IS
  lifted, with `L26de` a label 1396 bytes into it. Verified there is no `linkw`
  or `rts` between the two, so the entry detection is not over-reaching.
- hunks **25/26** carried "the port has not lifted the enclosing message
  composition". Same function, also lifted. They stay blocked, but for a
  precise reason: the "generous donation" block they sit in is a deferred
  sub-flow (`l026e_c20`, `l4218`, `jt933` are leaf stubs).

### A silent-failure scan reported the opposite of the truth

Worth recording because it nearly shipped a wrong comment. The first scan for
type-9 temple events reported **zero across 836 files**, and that number went
into a code comment justifying hunks 27/28 as harmless-but-faithful. The scan
called `Geo.load`, which does not exist — the loader is `Geo.parse` — and the
loop wrapped it in `try: ... except Exception: continue`, so every file raised
`AttributeError`, every file was skipped, and the result was a confident `0`.

The re-run found **192 temple events**, all with `01 00 00 00` at bytes 8..11,
which is what makes hunks 27/28 a real fix rather than a formality. Same shape
as the `"A battle begins..."` positive control that tested nothing: **a scan
that reports "none found" must first prove it can find something.** Never let a
blanket `except` sit between a typo and a count you intend to rely on.

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

**CODE 7 `l2184`, 1.0 @0x2212 -> 1.2 @0x21c2 — ⚠️ THIS READING WAS WRONG, see
hunks 3/5/6.** It said 1.2 raises the digit clause's upper bound to `'Z'`,
making the predicate the contiguous `'0'..'Z'` and adding `:;<=>?@` as word
starts — and even flagged that result as "odd". It was an **alignment
artefact**: 1.2 deletes 1.0's digit block entirely, so the instruction sitting
at that offset in 1.2 belongs to the surviving A-Z test, and the operand pass
paired 1.0's `'9'` bound with 1.2's `'Z'` bound from an unrelated compare.

The operand pass compares instructions that align BY MNEMONIC. After a
deletion the alignment shifts, so two same-mnemonic instructions from different
tests can be paired and reported as an operand change. The docstring's warning
about reading an insert hunk's operands applies to the operand pass in reverse:
**check that an operand "change" is not the shadow of a nearby deletion.**

The real change is a plain removal, measured rather than inferred: 1.0's
`l2184` holds FOUR `cmpib #48/#57` ('0'/'9') compares and four `cmpib #65/#90`
('A'/'Z'); 1.2 holds **zero** digit compares and the same four alpha ones. The
`"odd"` `:;<=>?@` behaviour never existed in either release — the port
introduced it, and it is now gone. See hunks 3, 5, 6 below.

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
