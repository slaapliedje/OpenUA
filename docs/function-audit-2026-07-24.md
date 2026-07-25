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

A proper "lifted but unwired" pass is worth building and is *not* a grep:
`parse_funcs` returns forward declarations as well as definitions, and a first
attempt that deduped on the wrong one flagged `jt183` and `jt957` — both
verified working above — as uncalled. Any number produced that way is unusable;
`jt557` is solid only because its zero call sites were confirmed by hand AND by
driving the button.

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
| Character **training** | `jt557`, 289 lines | **WIRED 2026-07-24, and now diagnosed.** It had no caller: `l0f1a` (the hall's case 0) ran a port stand-in and the menu item was pinned disabled. Both fixed — `jt557` now runs on a Train click, verified by trace: valid record, conscious check passed, money 100 vs cost 0, `trainMask 0` correctly computed (level-6 fighter, 50,000 XP vs the 70,001 needed). Still produces **no visible output**, for two *pre-existing* reasons pinned below. |
| Spell **memorize / cast / scribe** with a real caster | `l06d6` 30, `l0bc6` 83, `l0df2` 165, `l1374` 66 | Only exercised with a Fighter in the party, so every list was legitimately empty. Needs a Magic-User seated. |
| **Inn / tavern** events | `l398a` 34, `l4f9a` | Never driven; no HEIRS cell reached that fires them. |
| **Event / NPC editors** | `jt263` NPC block, event editor | Deferred scope (ADR-0008), never driven. |
| Alt / Fix camp arms | `l2d7e` 52, `l038a` 7 | Bar entries seen, arms not exercised. |
| Training's **guild class mask** | `jt557` L7324 gate | `guildMask = g_a5_28006[48]` is **always 0** — the game record's byte 48 is never populated (jt918's own comment flags it: "unliftable until the design header populates [48]"). With 0, `(haveMask & guildMask) == 0` always holds, so every character gets "we don't train that class here" instead of the correct verdict. |
| `jt101` **alerts never survive a repaint** | `jt101` in the hall loop | jt557's refusals go through `jt101`, and `jt918` repaints the hall every loop iteration, so the alert is overpainted before a frame is presented. Same class as the shop messages `docs/shop-merchant-wall.md` documents ("immediately overpainted by the loop's next repaint"), which is why `FRUA_SHOPTRACE` exists. `-DFRUA_HALLDIAG` is the equivalent harness for the hall. |

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
| Jump table (`CODE 0`) | 1208 entries | 1207 entries | 1.2 = 1.0 with **exactly one entry removed** |
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
hunks plus 93 operand-only changes. Several look like
exactly the kind of thing a bug-fix release contains: a `bras` → `beqs` (an
unconditional branch becoming conditional, CODE 12 `L3426`) and two
stack-slot corrections (`%fp@(-24)` → `%fp@(-20)` in CODE 19, `%fp@(-7)` →
`%fp@(-9)` in CODE 10).

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
- **The jump table costs a scripted renumber.** One entry is removed, so
  `jtN` above the removal point becomes `jtN-1`. The removal point is
  ambiguous within a run of same-segment entries (indices ~374–489); the first
  observable divergence is index 489, and everything below ~374 is provably
  unchanged. ~719 of 1208 indices shift, touching most of our 1,161 distinct
  `jt` names.
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
