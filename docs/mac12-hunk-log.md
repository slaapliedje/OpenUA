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
| 1 | 6 | `L25c0` | insert | 0 | 5 | - | ⬜ open |  |
| 2 | 6 | `L4e3a` | replace | 1 | 1 | yes | ⬜ open | value -> pointer for the L5f4e 2nd arg (-22222): needs the callee read too |
| 3 | 7 | `L1a66` | delete | 11 | 0 | - | ⬜ open |  |
| 4 | 7 | `L1ace` | replace | 1 | 1 | - | ⬜ open |  |
| 5 | 7 | `L21b6` | delete | 18 | 0 | - | ⬜ open |  |
| 6 | 7 | `L22a8` | replace | 19 | 1 | - | ⬜ open |  |
| 7 | 7 | `L3f80` | replace | 1 | 1 | yes | ✅ ported | l2ebc key-mode arg 0 -> 1 (boot.c ~95700) |
| 8 | 10 | `L38ba` | replace | 1 | 1 | - | ⬜ open |  |
| 9 | 10 | `L611c` | insert | 0 | 21 | yes | ⬜ open |  |
| 10 | 10 | `L6238` | replace | 1 | 1 | yes | ✅ ported | l6238: build the path forwards (clear + 2x jt431) instead of jt436's in-place dir prefix |
| 11 | 10 | `L6238` | replace | 1 | 5 | yes | ✅ ported | l6238: the second jt431 join (same change as hunk 10) |
| 12 | 12 | `L071c` | replace | 1 | 2 | - | ⬜ open |  |
| 13 | 12 | `L0d3e` | insert | 0 | 16 | - | ⬜ open |  |
| 14 | 12 | `L3426` | replace | 1 | 5 | yes | ✅ ported | l33d8 pass 2: skip SUMMONED combatants (`mc[21]==1`) so a conjured creature cannot mask a party wipe |
| 15 | 13 | `L105c` | insert | 0 | 5 | - | ⬜ open |  |
| 16 | 16 | `L50cc` | insert | 0 | 3 | - | ⬜ open |  |
| 17 | 18 | `L003a` | insert | 0 | 13 | yes | ⬜ open |  |
| 18 | 18 | `L61d4` | replace | 1 | 1 | - | ⬜ open |  |
| 19 | 19 | `L25ce` | insert | 0 | 2 | yes | ✅ ported | jt893: hoist the -22281 save+clear to entry |
| 20 | 19 | `L2c20` | delete | 2 | 0 | yes | ✅ ported | jt893: 1.0 per-case save+clear deleted |
| 21 | 19 | `L2c20` | delete | 1 | 0 | yes | ✅ ported | jt893: 1.0 per-case restore deleted |
| 22 | 19 | `L2d74` | insert | 0 | 1 | - | ✅ ported | jt893: restore at the single exit |
| 23 | 20 | `L00c4` | delete | 18 | 0 | yes | ⬜ open |  |
| 24 | 20 | `L18e2` | insert | 0 | 7 | yes | ⬜ open |  |
| 25 | 20 | `L24e6` | insert | 0 | 2 | yes | ⬜ open |  |
| 26 | 20 | `L24e6` | replace | 1 | 1 | yes | ⬜ open |  |
| 27 | 20 | `L26de` | insert | 0 | 4 | yes | ⬜ open |  |
| 28 | 20 | `L26de` | delete | 1 | 0 | yes | ⬜ open |  |
| 29 | 20 | `L3114` | insert | 0 | 2 | - | ⬜ open |  |
| 30 | 20 | `L57a0` | insert | 0 | 5 | yes | ✅ ported | l5676: "Transfer module ends testing!" before the test-play teardown |
| 31 | 20 | `L70d4` | insert | 0 | 1 | yes | ⬜ open |  |
| 32 | 20 | `L76c4` | replace | 3 | 1 | - | ⬜ open |  |
| 33 | 20 | `L76fa` | delete | 1 | 0 | - | ⬜ open |  |
| 34 | 21 | `L13f6` | insert | 0 | 4 | - | ⬜ open |  |
| 35 | 21 | `L3af2` | delete | 20 | 0 | yes | ⬜ open |  |
| 36 | 21 | `L4816` | insert | 0 | 8 | yes | ✅ ported | jt955 case 3: overland bounds guard #1 + the refusal message |
| 37 | 21 | `L4816` | insert | 0 | 5 | yes | ✅ ported | jt955 case 3: overland bounds guard #2 (out of range == blocked) |
| 38 | 21 | `L4874` | insert | 0 | 8 | yes | ✅ ported | jt955 case 3: the second guard's tail |

### Ported so far (12 of 38)

| hunks | function | fix |
|---|---|---|
| 36, 37, 38 | `jt955` case 3 (CODE 21 `L4816`) | bounds-check the overland target cell (38x15 -> max 37/14) at both use sites; out of range is refused with "There is no way to go in that direction." instead of indexing the HDR and wall-art table out of bounds |
| 30 | `l5676` (CODE 20 `L57a0`) | say "Transfer module ends testing!" before a type-11 transfer tears down a test-play session, instead of vanishing silently |
| 19–22 | `jt893` (CODE 19 `L25ce`) | the in-combat flag `-22281` is suppressed across the WHOLE Items browser (save+clear at entry, restore at exit) rather than only around the trade/give confirm |
| 7 | the `L3f80` picker (CODE 7) | the modal key-mode argument to `l2ebc` goes 0 -> 1, enabling `l23b4`'s `arg_lo != 0` arm |
| 14 | `l33d8` (CODE 12 `L3426`) | pass 2 of the post-fight outcome resolver now SKIPS summoned combatants. 1.0 `bras L347a` -> 1.2 `braw L34c2` (the branch outgrew its 8-bit displacement) plus 4 inserted instructions testing `node[64]->[21] == 1` and branching to the ADVANCE label — a `continue`. Pass 1 and the main pass already stop at the first summoned entry; pass 2 was the one place scanning unfiltered. It matters because `found` + the design's `hdr[29]` no-permadeath flag CLEARS `-27982`, the "party destroyed" flag: in 1.0 a summoned creature sitting in status 3/4/5 (fled/dead/petrified) or carrying `rec[382]` satisfied `found` on its own, so a party that had actually been wiped could come out not registered as destroyed — on the strength of a monster it had conjured |
| 10, 11 | `l6238` (CODE 10 `L6238`) | build the delete path FORWARDS — clear the buffer, `jt431` the design dir, `jt431` the leaf — instead of `jt436`'s in-place directory prefix, which has to slide the existing contents up inside a fixed 202-byte buffer. 1.2 also grew the frame 16 bytes for a separate leaf buffer. `l419e` right below already used the two-append idiom, so 1.2 is making `l6238` consistent with the rest of CODE 10 |

Verified: same-harness before/after frames are byte-identical on the walk, camp,
Magic, chargen and map-editor paths (AE=0), i.e. no regression. None of the four
fixes has been *observed firing* — each needs a specific situation (a party at a
map edge, a design under test, a prompt inside the Items browser, that one
picker). That distinction is deliberate; see ADR-0018's caveat.

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
