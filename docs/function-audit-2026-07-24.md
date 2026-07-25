# Function audit — 2026-07-24

A full audit of **what the port actually does**, measured rather than read off
the status docs, plus the long-hanging **version question** (retarget to Mac 1.2
or DOS 1.2?).

Method: regenerate the counting tools, classify every function the older docs
call a stub, then drive the engine in Hatari and look at the screens. Nothing
below is carried over from a previous doc's claim — where an older doc disagrees,
the older doc is wrong and is flagged in §4.

## 1. Hard numbers (regenerated this session)

| Metric | Value | Source |
|---|---|---|
| JT entries called | 1206 | `tools/jt_progress.py` |
| Done | **1201** (1072 lifted + 54 noop + 75 alias) | same |
| Stub / stand-in / missing | 1 / 0 / 4 | same |
| Top-100 most-called JT | 100/100 done | same |
| Stub bodies in `boot.c` | 56 → 36 faithful no-ops, **1 "live gap"**, 19 uncalled | `stub_audit.py --stubs` |
| Empty switch arms | 235 → **0 deferred, 0 bare**, 58 commented, 17 explained, 160 empty `default:` | `stub_audit.py --arms` |
| `boot.c` | 99,393 lines | `wc` |
| Host test suite | 358 passed / 2 skipped | `make test` |

The one reported "live gap" is `l493a`, a **classifier false positive** — its own
comment records it as one of the compiled-out no-ops in the shipping build
(`linkw`/`unlk`/`rts`), and its sibling `l4932` is already in the no-op list.
The 4 "missing" are not gaps either (superseded Toolbox printing paths, an
uncalled deferred arm, and the Mac Package Manager, which has no Falcon
counterpart).

**So there is no structural work left to find with these tools** — but note the
word *tools*. A third blind spot showed up later in the session, beyond the PROBE
bodies `--stubs` counts and the empty switch arms `--arms` counts: **a fully
lifted body that nothing calls.** `jt557` (Training, 289 lines) is real,
faithful, classified `REAL`, counted "done" by `jt_progress.py` — and dead,
because the menu dispatch was never wired (§3). Neither audit tool models
reachability, so "1201/1206 done" and an inert menu item are not in conflict.

**That pass now exists: `tools/reach_audit.py` (built 2026-07-25).** It was
indeed not a grep — successive naive versions reported 641, 115, 187, 96 and
then 0 uncalled bodies on the same tree, every number wrong. The traps, all now
pinned by `tests/test_reach_audit.py` (13 cases):

- a **forward declaration** is not a call site — counting it is exactly how
  `jt557` hid, and deduping on the declaration instead of the definition
  flagged the working `jt183` / `jt957`;
- a name in a **comment** or inside `PROBE("jtN")` is not a call site (house
  style names `jtNNN` in prose constantly, so this dominates);
- a call from a **non-`static`** function IS a call site — collecting callers
  only from `static` definitions (`stub_audit`'s `FUNC_RE` is anchored on
  `^static`) is what produced 96, falsely condemning `l07dc` (the phase-6 play
  loop calls it), `jt919`, `jt931`, `jt989`;
- a function's **own signature line** is not a call to itself. When `{` sits on
  its own line the signature falls outside the `[open..close]` body range, so
  every function looked called and the audit reported **0 findings** — the
  failure mode that makes such a tool worse than useless.

Validated against ground truth rather than trusted: run over `boot.c` at
`acb844f1` (before the wiring commit) it reports `jt557` uncalled; at HEAD it
does not.

It runs three passes, because "unreachable" has three distinct shapes here —
and the third was found the same day, so this is not hypothetical:

| pass | shape | instance |
|---|---|---|
| `--uncalled` | no call site at all | `jt557` (the trainer, #78) |
| `--gated` | called, but behind an A5 flag the port pins to constant 0 | `jt556` (Human Change Class, #82) |
| `--harness` | called only inside `#ifdef FRUA_*` | none at HEAD |

`--gated` self-verifies against the disassembly: a pin is only a defect if the
**Mac** stores a non-zero there. Of 8 pinned slots, 7 are `clr`-only on the Mac
too (faithful), and the 8th (`-1314`, Color QuickDraw present) is a deliberate
platform constant, recorded in the tool's `TRIAGED_PINS` with its reason so
nobody re-derives it.

**Baseline at HEAD: 0 findings in all three passes** — 41 uncalled bodies all
carry `__attribute__((unused))`, and the two that did not (`jt165`, `jt587`) are
now annotated with why (below).

## 2. Verified live this session

Falcon (Hatari, TOS 4.04), HEIRS, **DOS-sourced game data** — so this doubles as
a Mac-free stack check. Screens were inspected, not just reached.

| Path | Evidence |
|---|---|
| Boot → main menu → design picker | title, `CURRENT GAME DESIGN`, button grid |
| Training Hall | roster list; add-list shows `* BARBARUS` (the #67 selection-state symptom, now correct) |
| Char-gen | race/class/alignment/gender radio picker; rolled sheet (level 6, age 26, HP, THAC0, AC, damage, encumbrance, movement); name entry; combat-icon picker; save prompt → `CHAR0006.CHR` written |
| Dungeon walk | 3D corridor, compass with ticks + cardinal letter, clock, coordinate readout, per-step event dispatch |
| Events | caravan message chain → XP award → treasure screen (View/Take/Pool/Share/Exit) → "still treasure left" Yes/No |
| Shop | merchant portrait, "MAY I HELP YOU?", BUY list `BELT 4 / BOOTS 4 / CLOAK 4 / ROBE 4 / MIRROR 16`, `PERSONAL 100 / POOLED 0`, POOL grows the bar |
| Temple | TEMPLE OF TYMORA, priest portrait, services bar |
| Camp | "the party makes camp…", bar View/Magic/Rest/Alt/Fix/Load/Save/Exit |
| Camp → Magic | Cast/Memorize/Scribe/Display/Rest/Exit (the four screens the June docs list as stubs) |
| Camp → Rest | `Rest Time 00:08:00` via Hours/Add, then the **zone no-rest rule fires**: "A GUARD YELLS ANGRILY AT THE PARTY, 'NO SLEEPING IN THE STREETS, MOVE ALONG!'" and the clock advances only 5 minutes |
| Save | A–J slot picker → `SavGamB.csv` rewritten |
| Load | picker offers **only the slots that exist** (A B), loads into the Training Hall with the party intact |
| Map editor | module picker (list read from the design), editor with 3D preview, map thumbnail, compass, `Wd 19 Ht 19`, command bar |
| Combat | tactical map, party turn resolves (#74/#75, `FRUA_CBTPLAY`) |
| Game Settings | field pickers (#27/#28) |

## 3. Not verified — the honest gap list

Real bodies exist for all of these (classified `REAL`, sizes noted); what is
missing is a live run. This is the list to work from, not a stub list.

| Path | Function | Why it is still open |
|---|---|---|
| ~~Character **training**~~ | `jt557`, 289 lines | **DONE 2026-07-24 — verified end to end, both branches.** It had no caller: `l0f1a` (the hall's case 0) ran a port stand-in and the menu item was pinned disabled. Both fixed. Live on HEIRS `GEO008` cell(col=24,row=6), the in-dungeon Training Hall of the Road Guards: refusal branch prints **"Not Enough Experience"** with `guildMask 61` / `haveMask 8`; success branch (`-DFRUA_HALLFREE`) previews **"BARBARUS will become: a level 7 Fighter"**, the `jt159` confirm runs, and the roster shows HP 78 → 86 from the `jt885` hit-die + CON gain. |
| Spell **memorize / cast / scribe** with a real caster | `l06d6` 30, `l0bc6` 83, `l0df2` 165, `l1374` 66 | Only exercised with a Fighter in the party, so every list was legitimately empty. Needs a Magic-User seated. |
| **Inn / tavern** events | `l398a` 34, `l4f9a` | Never driven; no HEIRS cell reached that fires them. |
| **Event / NPC editors** | `jt263` NPC block, event editor | Deferred scope (ADR-0008), never driven. |
| Alt / Fix camp arms | `l2d7e` 52, `l038a` 7 | Bar entries seen, arms not exercised. |
| `jt587` — faithful Add-Character loader | `jt587`, CODE 15 +0x08e8 | **Deliberately not called, and the only entry here that is a real capability gap rather than an undriven path.** Its Mac call site (CODE 12 @0x1430 in `L12a0`) cannot be taken: the port's saved characters are flat 512-byte `cg_pool` dumps, not the Mac's `.cch` stream `l08ba` walks, and the port models party members as nodes *inside* `cg_pool`, which a fresh `-22212` slot never is. Calling it anyway wedged the Add picker (zeroed 398 bytes over the list node, spun `jt987`'s cold-disk retry). `l12a0` substitutes a `cg_pool` lookup plus jt587's **tail** (`jt21` + `jt910`) at the Mac's own point in the sequence. Closing it means migrating `save_roster` to `jt578`/`.cch`; the reconciled reader `l08ba_c15` already exists, unused. |
| `jt165` — n'th list node | `jt165`, CODE 7 +0x15c2 | Not a gap. Both Mac call sites (`L12a0` @0x1414, `L15e2` @0x1746) resolve the list widget's selected *index* back to a node; the port's `jt169` lift already returns the node via its `&entry` out-param, so the call would be a redundant re-walk. Annotated `__attribute__((unused))`. |
| ~~Training's **guild class mask**~~ | `jt557` L7324 gate | **RESOLVED 2026-07-24 — there was never a missing writer. The mask was 0 because I was entering the hall the wrong way.** `guildMask = g_a5_28006[48]` is written by the **Training-Hall event** (`CODE 20 @0x2d32` = `l2d32`, `rec[48] = ev[8]`), which the port has lifted faithfully and which was already wired into `l709e` case 6. Every earlier trace entered the hall from the **main menu** (`jt918(1)` from `l07dc`), where no hall event has run and `[48] == 0` is the *correct* value — so "we don't train that class here" was the honest answer to "which classes does this nonexistent guild train?". Driving the real event (HEIRS `GEO008` cell(col=24,row=6), "SIR FTUCIS, TRAINING MASTER OF THE ROAD GUARDS" → "Does the party want to train?" → Yes) gives `[48] = 61`, exactly the event's `ev[8]`, and the verdict is right. Do **not** force `0xFF` at our call site — that is the trainer UI's behaviour (`CODE 17 @0x2840`, `CODE 10 @0x5cbe`), not this arm's. **Lesson: before hunting for a missing writer, check that the caller was reached the way the Mac reaches it.** |
| ~~Hall menu **enable flags** + slot mapping~~ | `L0aae`/`L0df6`/`L0e98`, CODE 12 | **RESOLVED 2026-07-24 (#82) — the cluster is now a straight transcription and the port's three compensating swaps are gone.** One index runs the whole menu: install position = enable slot (`-14440 + i`) = `JT[3]` dispatch case. Proof: all twelve case bodies test exactly `-14440 + their own case number`. **The trap** — `JT[452]` is variadic and C pushes right-to-left, so within each 4-item group the *last* item in the asm is the *first* argument. Reading top-down reverses each group, which is where the port's "Train/Create, Add/View and Remove/Change-Class are label-crossed on the Mac" theory came from. There is no crossing. True order: 0 Create, 1 Delete, 2 Modify, 3 Train, 4 Human Change Class, 5 View, 6 Add, 7 Remove, 8 Load, 9 Save, 10 Begin, 11 Exit. Four independent confirmations: each body calls the JT its label implies (`0→JT[574]` create, `3→JT[557]` train, `4→JT[556]` class, `7→JT[584]` remove, `9→JT[585]` save); the STRS label offsets come out strictly ascending; `-14434`'s ">5 members" clear lands on Add, exactly where `#100` had moved it empirically; and the cluster reproduces the live BasiliskII enable set for **all 12** items (always = Create/Delete/Add/Load/Exit, roster-gated = Modify/View/Remove/Save/Begin, mask-gated = Train + Change Class). `-14440` and `-14429` are never written by any CODE segment — DATA seeds, `a5_scalars.c` has `-14440 = 1`; verified live (Create is enabled on an empty roster). Verified in-game: empty roster shows exactly the five always-on items; Add opens the pool picker; Create opens PICK RACE/CLASS; View opens `jt904`'s sheet; in a real hall Train and Change Class both light up (`-14436 = -14437`) and Change Class opens "Pick New Class" — a path that was permanently dead with `-14433` pinned to 0. |
| ~~`jt101` alerts never survive a repaint~~ | — | **RETRACTED — this was my measurement error, not a bug.** `jt101` paints correctly and dwells: `l4bac` waits `jt476(-17518[gameRec[18]])`, and with text speed 4 that is 1000 ticks. Sampling frames at t=0/1/2/4 s after a Train click shows "we don't train that class here" on screen at t=0 and t=1 and gone by t=2. My earlier "no output" reading came from a single screenshot taken 7 s after the click — after the dwell had expired. Nothing to fix. |

## 4. Status docs that are wrong (do not trust these)

Measured against the code, three trackers are materially stale. They are useful
as history, not as status:

- **`docs/gap-analysis.md`** (dated 2026-06-20) — says combat is 🔴 "the SPINE is
  stubbed", camp magic screens are stubs, "~17 event handlers still STUB", and
  save/load is missing the A–J pickers. **Every function it names as a stub
  classifies `REAL` today**, and combat, camp, rest and both save/load pickers
  are verified above.
- **`docs/milestone.md`** (snapshot 2026-06-26) — "943 / 1205 done", `boot.c`
  ~65.7k lines, 129 tests. Today: 1201 / 1206, 99.4k lines, 358 tests.
- **`docs/subsystem-status.md`** — counts dated 2026-07-12. Its own header
  already says to regenerate rather than trust the table, which is the right
  instinct; the counts below that header are simply old.

`docs/jt-lift-progress.md` is generated and therefore fine — regenerate before
quoting any number.

## 5. The version question — Mac 1.0 vs Mac 1.2 vs DOS 1.2

### What we actually hold

| Source | Version | Banner |
|---|---|---|
| `data/work/UnlimitedAdventures.rfork` (lift target) | Mac **1.0** | `Version 1.0       April 27,1993` |
| `data/frua-mac/joined/…`, `data/work/frua.rsrc`, `~/minivmac/frua-clean.dsk` | Mac **1.0** | same |
| `data/unlimited_adventures.sit` → `data/work/UnlimitedAdventures-1.2.rfork` | Mac **1.2** | `Version 1.2    February 28,1994` |
| `data/dos-frua/CKIT.EXE` (GOG/Steam) | DOS **1.2** | `Version 1.2        June 28,1993` |

**We do have Mac 1.2** — it was inside `data/unlimited_adventures.sit` (a StuffIt
5 archive of an *installed* "SSI Unlimited Adventures Folder", not floppy
images), unextracted. Staged 2026-07-24:

```sh
unar -o data/work/mac12 data/unlimited_adventures.sit
python3 tools/appledouble.py \
  "data/work/mac12/SSI Unlimited Adventures Folder/Unlimited Adventuresƒ/Unlimited Adventures.rsrc" \
  --fork resource -o data/work/UnlimitedAdventures-1.2.rfork
python3 tools/dis68k.py data/work/UnlimitedAdventures-1.2.rfork --out data/work/disasm-1.2
```

633,145 bytes, `sha256 c9673b14…6ad1` on the AppleDouble `.rsrc`, app dated
1994-03-02. The 2026-07-20 string figures **reproduce exactly** against it
(2147 entries, 2070 recovered / 96.4%, 40 substring-only, 37 absent, vs 1.0's
2145 / 2068), so ADR-0017's oracle is now a capability rather than an
intention.

### The finding that changes the answer

DOS 1.2 **already contains both of the strings that were treated as Mac
1.2-only**:

```
Transfer module ends testing!
There is no way to go in that direction.
```

Mac 1.0 has neither. So the entire *known* user-visible delta of Mac 1.2 is
already sitting in the data the player supplies — the fixes travelled along the
DOS line eight months before Mac 1.2 shipped. Nothing about them requires a Mac
1.2 fork.

(The port does not print either message. That is faithful to Mac 1.0: walking
into a wall silently recentres, per the `l1908` blocked-step lift in #77.
Printing them would be a deliberate 1.0 divergence and wants an ADR.)

### The measured 1.0 → 1.2 delta (this is the important part)

With both forks disassembled side by side, the delta is no longer a guess:

| | Mac 1.0 | Mac 1.2 | |
|---|---|---|---|
| `CODE` segments | 23 / 564,850 B | 23 / 564,984 B | **+134 bytes total** |
| Segments byte-identical | — | **1 of 23** | (CODE 1) |
| Segments with an **identical instruction stream** | — | **12 of 22 changed** | only operands/addresses moved |
| `DATA` (initialised A5 image) | 12,694 B | 12,694 B | **BYTE-IDENTICAL** |
| `DREL` (A5 relocations) | 2,052 B | 2,052 B | **BYTE-IDENTICAL** |
| Jump table (`CODE 0`) | 1208 entries | 1207 entries | **RESTRUCTURED, not "one entry removed"** — see the correction below |
| `STRS` | 29,148 B / 2145 | 29,220 B / 2147 | 94.9% of entries at an identical offset |
| **Real code change** | — | **32 instruction hunks in 10 segments** | 1–15 instructions each |

The often-repeated "22 of 23 CODE segments changed" is true at the byte level and
badly misleading: removing one jump-table entry shifts every `jsr %a5@(…)`
operand above it, which rewrites thousands of bytes without changing a single
instruction. Comparing *mnemonic streams* instead — operands dropped, so
relocation noise cannot masquerade as change — the entire release is **32 hunks**:

| segment | hunks | segment | hunks |
|---|---:|---|---:|
| CODE 20 | 11 | CODE 12 | 3 |
| CODE 21 | 5 | CODE 6, 19 | 2 / 4 |
| CODE 7 | 5 | CODE 13, 16, 18 | 1–2 each |
| CODE 10 | 4 | CODE 1,2,3,4,5,8,9,11,14,15,17,22 | **0** |

**And both new messages are locatable.** 1.2 references them at:

- `CODE 21 + 0x484c` → `"There is no way to go in that direction."` — inside the
  hunk cluster at **CODE 21 `L4816`**, which is the **OVERLAND arm of JT[955]**
  and is already in our tree (`boot.c:48155`, `case 3: /* L4816 — OVERLAND */`).
  1.2 adds ~8 instructions there.
- `CODE 20 + 0x57c0` → `"Transfer module ends testing!"` — the hunk at **CODE 20
  `L57a0`** (a 2-instruction insertion).

So the two known fixes are ~10 instructions at two sites in functions we
already have, and the other 34 hunks are the previously "unknown" 1.2 bug
fixes — now an enumerable worklist rather than a mystery, tracked hunk by hunk
in `docs/mac12-hunk-log.md`. **Correction:** the count in this section was first
published as 32 from a throwaway parser that mis-tokenised the listings; a
correctly-anchored mnemonic compare (`tools/mac12_diff.py`) gives 38 structural
hunks plus 93 operand-only changes.

**Second correction (2026-07-25), to this section's own examples.** It offered
three specimens of "the kind of thing a bug-fix release contains". One was
real and two were not:

- the CODE 12 `L3426` change **is** a real fix, but it is not a `bras` → `beqs`.
  It is `bras` → `braw` (the branch simply outgrew its 8-bit displacement) plus
  **four inserted instructions**, and the fix is a `continue` that skips
  summoned combatants in `l33d8`'s second pass. Ported; see the hunk log.
- the "two stack-slot corrections" (`%fp@(-24)` → `%fp@(-20)` in CODE 19,
  `%fp@(-7)` → `%fp@(-9)` in CODE 10) are **not fixes at all** — they are
  compiler frame re-layout. They came from the first, unusable 229-difference
  run before frame slots were normalised. `mac12_diff.py --frameslots` now
  separates the cases by per-function shape: a real wrong-slot bug changes ONE
  use site while its siblings stay put, whereas each of these renames EVERY use
  of its local, inside a function 1.2 restructured (`l6238`'s frame grew
  202 → 218 bytes; `jt893` was restructured). Result: **0 genuine frame-slot
  fixes, 134 churn rows**, all confined to CODE 10 (23) and CODE 19 (111) — the
  two segments whose restructures are already ported.

### What a retarget would actually cost

Everything in the port is keyed to Mac 1.0 addresses. Measured:

| Artefact keyed to 1.0 | Count |
|---|---|
| `jtNNN` references in `src/` (jump-table **indices**) | 17,614 across 1,161 distinct entries |
| `lXXXX` references (CODE-local **offsets**) | 7,628 |
| A5-offset references | 9,766 across 1,227 distinct offsets |
| Generated `a4_map.c` rows (A4 slots + A5-internal + DOS runs) | 1,396 |
| `installer/strs_map_dos12.json` positions, keyed to the Mac **1.0** pool | 2,108 |
| `jtNNN` / `lXXXX` mentions across `docs/` | 22,277 |
| The lift itself | 99,393 lines of C over 555,170 bytes of 1.0 CODE |

**Option A — retarget to Mac 1.2.** Cheaper than previously believed, and still
not worth doing. What survives and what does not:

- **The A5 world needs NO work.** `DATA` and `DREL` are byte-identical, so
  `a4_map.c`'s 1,016 A4 slots, the DOS scalar positions map, `a5_scalars.c`'s
  authored offsets and all 1,227 A5 offsets in the code carry over unchanged.
  This was the largest line item in the earlier estimate and it is now zero.
- **The STRS map is a tool re-run**, not manual work
  (`strs_dos_probe.py --emit-map` against the 1.2 fork), plus regenerated
  offsets for `rsrc_from_dos.py`'s 37 authored strings.
- **The jump table is a PERMUTATION, not a scripted renumber.** This was the
  most wrong claim in the original write-up. It said "one entry is removed, so
  `jtN` above the removal point becomes `jtN-1`", with the first divergence at
  index 489 and everything below ~374 provably unchanged. Measured properly
  (2026-07-25) by matching each entry's TARGET inside the 12
  instruction-identical segments — where offsets cannot move, so target
  identity is exact:

  | | |
  |---|---:|
  | indices whose target is unchanged | 118 |
  | indices whose target MOVED | **465** |
  | index deltas observed | −34 … +10, **not** a uniform −1 |
  | lowest moved index | **254**, not 489 |
  | port `jtN` names affected (lower bound) | **439** of 1,161 |

  `−1` is merely the commonest delta (304 of 465); the rest sit at −34, −24,
  −19, −17, −15, −7, −6, −5, −3, −2, +1, +2, +4, +5, +6, +7, +8, +10. And
  `254 → 256`, `255 → 254`, `256 → 255` is a genuine three-way permutation, so
  entries were ADDED as well as removed. The 439 figure is a lower bound: it
  counts only the stable segments, because in the 10 changed ones offsets move
  too and matching needs instruction alignment first.

  Consequence: a retarget cannot be done with a `jtN -> jtN-1` sweep above a
  cut point. It needs a derived target-matching map, and the map is only exact
  where the segment is unchanged. Still mechanical, but not a one-liner — which
  pushes Option A further away, not closer.
- **The `lXXXX` helpers are the real grind.** 858 helpers keyed to
  `(CODE, offset)`, and offsets moved in 22 of 23 segments — 7,628 references
  plus `docs/lxxxx-jt-aliases.md` and ~22,277 doc mentions. Mechanical, wide,
  and entirely churn.

For that you get 32 hunks of behaviour, all of which can be taken individually
(Option C) without moving a single address. And the target would sit **eight
months further from DOS 1.2** than 1.0 does. Verdict: **no** — ADR-0017
decision 7 stands, now for measured reasons rather than an over-estimate.

**Option B — port from DOS 1.2 instead.** DOS 1.2 is x86. This is not a
retarget, it is a different project: it discards the 68k lift, `dis68k`, the
Mac Toolbox shim (ADR-0003) and every `jt`/`l` identity, in exchange for
removing a *build-time* input the player never sees. Verdict: **no**.

**Option C — keep 1.0, take fixes individually. RECOMMENDED.** Now fully
supported: we have the 1.2 fork, both disassemblies, and a located 32-hunk
worklist. Each hunk is 1–15 instructions in a function we have already lifted,
so porting one is an afternoon's read-and-patch with the two listings side by
side. The two user-visible ones are known exactly (CODE 21 `L4816`, CODE 20
`L57a0`), and their text is in the player's own DOS binary as well as in 1.2.

One judgement call remains, unchanged by any of this: printing "There is no way
to go in that direction." is a **deliberate divergence from 1.0**, whose
blocked-step behaviour (silent recentre, the `l1908` lift in #77) the port
currently reproduces faithfully. That wants an ADR, not a quiet patch.

### Effort summary

| Option | Effort | Buys |
|---|---|---|
| A: retarget to Mac 1.2 | weeks of pure churn — jump-table renumber + 858 `lXXXX` re-keys over 7.6k references; A5 world and DATA/DREL cost nothing | the same 32 hunks Option C gets for free |
| B: port from DOS 1.2 | restart the decompilation (x86) | removes a build-time-only Mac dependency |
| C: cherry-pick individual hunks | ~an afternoon each, 32 known sites | exactly the 1.2 behaviour, no address churn |
| C′: acquire + archive a Mac 1.2 fork | **DONE 2026-07-24** — it was in `data/unlimited_adventures.sit` | the oracle ADR-0017 assumes we have |

## 6. Bottom line

The engine is **structurally complete** — 1201/1206 JT done, no deferred switch
arms, no real live gaps — and broadly **verified live**, including the paths that
were open this morning (chargen, shop, temple, editor) and now camp/rest/save/load.
What is left is a short, specific list of undriven paths (§3), not a lift
backlog.

On versions: **stay on Mac 1.0.** The 1.2 fork turned up unextracted in
`data/unlimited_adventures.sit` and is now staged, which settles the question
rather than deferring it: 1.2 is a 32-hunk bug-fix release over an
**byte-identical A5 world**, so its fixes can be cherry-picked into the 1.0 lift
one at a time with no address churn at all. A retarget would buy the same 32
hunks in exchange for renumbering the jump table and re-keying 858 helpers.
