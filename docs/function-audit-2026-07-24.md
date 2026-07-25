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

**So there is no structural work left to find with these tools.** The remaining
risk is entirely behavioural, which is what §2 is for.

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
| Character **training** | `jt557`, 264 lines | Driven this session and did **not** appear on screen — needs the hall's row selection first, which the harness did not manage. Unknown whether it works. |
| Spell **memorize / cast / scribe** with a real caster | `l06d6` 30, `l0bc6` 83, `l0df2` 165, `l1374` 66 | Only exercised with a Fighter in the party, so every list was legitimately empty. Needs a Magic-User seated. |
| **Inn / tavern** events | `l398a` 34, `l4f9a` | Never driven; no HEIRS cell reached that fires them. |
| **Event / NPC editors** | `jt263` NPC block, event editor | Deferred scope (ADR-0008), never driven. |
| Alt / Fix camp arms | `l2d7e` 52, `l038a` 7 | Bar entries seen, arms not exercised. |

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
| `data/dos-frua/CKIT.EXE` (GOG/Steam) | DOS **1.2** | `Version 1.2        June 28,1993` |

**There is no Mac 1.2 anywhere on this machine.** The 2026-07-20 measurement in
`docs/dos-strings-probe.md` (2147 STRS entries; "22 of 23 CODE segments
changed") was made against a fork that is no longer present, so *those specific
figures are currently unreproducible* and should be read as provenance, not as
something you can re-check today.

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

**Option A — retarget to Mac 1.2.** The jump table reorganises (reported
1208 → 1207 from index 13), so *every* `jtNNN` index shifts, every `lXXXX`
(CODE, offset) pair moves, the DATA/DREL layout changes (invalidating
`a4_map.c`, the DOS scalar positions map and the authored-scalar offsets), and
the STRS pool grows by two entries so every pool offset in the string map moves
with it. That is a re-derivation of essentially all of the project's mechanical
work, for a release that is **eight months further from DOS 1.2** than the
current target. Verdict: **no** — this is ADR-0017 decision 7, and the evidence
above strengthens rather than weakens it.

**Option B — port from DOS 1.2 instead.** DOS 1.2 is x86. This is not a
retarget, it is a different project: it discards the 68k lift, `dis68k`, the
Mac Toolbox shim (ADR-0003) and every `jt`/`l` identity, in exchange for
removing a *build-time* input the player never sees. Verdict: **no**.

**Option C — keep 1.0, take fixes individually.** Unchanged recommendation, with
one correction: the two known fixes need **no Mac 1.2 at all**, because their
text is in the player's own DOS binary. The work is engine-side (a pool slot
plus the code path that prints it) and is measured in hours, not months. A Mac
1.2 fork is only worth acquiring as a **per-function oracle** when chasing a
specific bug — and we do not currently have one, so "keep 1.2 as an oracle" is
at present an intention rather than a capability.

### Effort summary

| Option | Effort | Buys |
|---|---|---|
| A: retarget to Mac 1.2 | months; re-derives ~all address-keyed work | 2 known messages + unknown fixes |
| B: port from DOS 1.2 | restart the decompilation | removes a build-time-only Mac dependency |
| C: cherry-pick into the 1.0 lift | hours per fix | the same 2 messages, from DOS data |
| C′: acquire + archive a Mac 1.2 fork | one-off acquisition | the oracle ADR-0017 assumes we have |

## 6. Bottom line

The engine is **structurally complete** — 1201/1206 JT done, no deferred switch
arms, no real live gaps — and broadly **verified live**, including the paths that
were open this morning (chargen, shop, temple, editor) and now camp/rest/save/load.
What is left is a short, specific list of undriven paths (§3), not a lift
backlog. On versions: stay on Mac 1.0; the only 1.2 content anyone has named is
already in the DOS data the player supplies.
