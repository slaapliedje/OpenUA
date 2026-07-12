# Training Hall + char-gen worklist (audit 2026-06-18)

## STATUS 2026-07-04 — jt556 HUMAN CHANGE CLASS fully lifted

The last stub behind the Hall's case-7 arm is real code now: **jt556**
(CODE 17+0x66ee, ~1.3KB) + its three locals — l6c60 (current class =
first live 157-band level slot), l6bee (already-dualled marker = first
164-band slot), l6578 (the AD&D dual-class qualification: differ from
current, current-class primes 15+, NEW-class primes 17+ per the -30552
minima table, alignment allowed per the -30450 row). The flow: guards
("only conscious humans may change" / "%s doesn't qualify.") → the
"Pick New Class" jt169 list built from the race's -30864 allowed-class
row → commit (XP=0, old level parked in the 164 band via jt35, new
class level 1, spell bands 355/150/198 cleared, Cleric/Magic-User
seeds per the JT[1] @0x6a42 switch incl. the -18889..92 known-spell
flags, "%s becomes a %s.", the jt910/911/912/906/907 recompute chain,
and the jt882 unequip sweep of items whose ITEMS.DAT class mask
excludes the new rec[183]). Returns the new class id / 17 = no change
— the contract l0f74 already consumed. CAVEAT: the Hall gate -14433 is
seeded 0 (the button is faithfully disabled in the stock Hall build),
so this is disasm-faithful but not yet runtime-exercised; a design
with Human Change Class enabled would light it up.

## STATUS 2026-07-02 — Hall chrome COMPLETE: every screen is faithful code

The last port-side Hall screens are gone. The **label-crossed Remove /
Change-Class dispatch** (the P3 trap below) is straightened: case 4 (the
REMOVE button) now runs the already-lifted **L1060** body — jt584 saves the
highlighted member to `<NAME>.cch`, jt19 unlinks + destroys the record, NPCs
(rec[147] bit 7) take the jt76/l185e path — gated on the Remove slot's
-14436 (the Mac reads -14433 because ITS selector remap parks this body
under the Change-Class slot). Case 7 (HUMAN CHANGE CLASS, gate -14433 =
always disabled) carries the faithful **L0f74** change-class body (jt556
pick → un-ready item types 8/105 via jt41/jt878 → jt876 re-add per the
picked class; the jt556 stub returns 17 = aborted so it can never mutate on
a fake pick). The `cg_draw_sheet` / `cg_rename` / `cg_modify_sheet` /
`cg_remove_from_party` cluster and its orphaned tables (`k_roster_races`,
`k_class_names`, `cg_class_min`, `cg_race_adj`, `cg_roll_stats`) are
DELETED (~290 lines).

Hatari-verified: Load save A → arrow to a member → R removes them (roster
drops to 5, `NIVLOC.cch` written — the faithful save-on-remove, so the
member reappears in the Add/Delete browsers via jt589). The P3 list below
is now HISTORICAL — Modify/Delete/Add/View/Create/Load were made faithful
in earlier sessions; Save/Begin remain on the #100/#115 walls.

OPEN NIT: ONE BLANK FRAME after Remove until the next input repaints the
Hall. Isolated to the remove path (Add/Load repaint immediately). Suspects,
in order: jt55 → l3b1e(0,…,-27866,rec[189]) → jt1022 LBResize on the Hall's
-27866 registry (an out-of-range item id would pop the l036a "LBResize
invalid item" modal — which paints on a cleared page and waits for a key,
matching the symptom exactly); or a stray present of an uncomposed
triple-buffer page in the release path. Functionally harmless; #144
present-once territory. Check DBG/probe or on-machine whether error text
flashes.

## Architecture (so the worklist makes sense)

- **`l0aae`** (CODE 12 + 0x0aae, boot.c ~20328) — the Training Hall **menu
  builder**. Builds 12 button DLItems from the static `k_jt918_menu_items[]`
  table (jt452), then per item `jt444(i, flags[i] ? 24 : 16)` to
  **enable/disable** (cmd 24 = enable, 16 = disable → jt137 draws disabled rows
  recessed w/ black-128 label and rejects their hits). `flags[]` = the **c79x
  cluster** `g_a5_-14440..-14429` (one byte per item, index 0..11).
- **`jt918`** (CODE 12 + 0x0d90, boot.c ~48819) — the **dispatcher**. Each outer
  loop iteration: paint chrome + roster (l02dc), **set the c79x flags by
  context**, call `l0aae()` for a selection, JT[3]-dispatch the selection to
  `l0f1a`..`l120c` (cases 0..11).
- Menu index ↔ case ↔ item (the install index IS the JT[3] case):
  0 Train(l0f1a) · 1 Modify(l0f2e) · 2 Delete(l0f3e) · 3 Create(l0f60) ·
  4 Remove(l0f74) · 5 Add(l1036) · 6 View(l104c) · 7 ChangeClass(l1060) ·
  8 Exit(l10ca) · 9 Save(l1142) · 10 Begin(l115a) · 11 Load(l120c).

## P1 — Training Hall dynamic menu — DONE 2026-06-18 (e84b4bd)

FINAL: the user's BasiliskII ground truth corrected the disasm-only read below.
Mac behavior: EMPTY roster = Add/Create/Delete/Load/Exit only; with a character
= + Modify/Remove/View/Save/Begin; Train + Human-Change-Class always disabled.
jt918 now sets the c79x flags keyed on the active party (g_a5_-27928 != 0); the
port's l0aae is self-consistent (button k_jt918_menu_items[i] gated by -14440-i)
so each flag is set by its label. Hatari-verified. Also (4211bdb): dropped the
bad-stat HEIRS savegame party (port_load_savgame skipped) + fixed the seed AC
(60-x slot). The gameRecord[48] gate below is moot (invisible for a loaded
design).

----- (superseded) investigation note -----
A live trace (logged the 12 flags + -27932 at l0aae) read all-enabled and I
wrongly concluded "already faithful" — the disasm L0df6/L0e98 mapping didn't
match the Mac; the port over-enabled. On the loaded HEIRS design the flags come out
`4095` = all 12 enabled, and that is the CORRECT faithful result — the Mac's
"current character" branch (L0df6) enables everything except the Create/Remove
game-record gate and View>5, neither of which trips for a 4-member loaded party.
The port matches jt918's L0df6/L0e98 logic. **There is no visible bug to fix in
the common case** — "all buttons enabled with a loaded design+party" is faithful.

The remaining stand-in is invisible + a real lift, not wiring:
- **Create/Remove gate** reads `gameRecord[48]` (g_a5_-28006, a design-header
  byte; 0xFF = enabled — a boolean flag, NOT a count: Add/Remove never touch it,
  and the only set I found is a TEMPORARY save/restore guard during the char
  sheet at CODE 17 ~0x2836). The **persistent** setter lives in the design
  load/save machinery (CODE 22 reads it at 0xeae/0x108a/0x1228) and the port's
  design header is a memset stub — so restoring the gate naively would DISABLE
  Create/Remove (the exact reason the interim exists). Real design-state lift.
- It would NOT change the visible behavior anyway: with a design loaded, [48]
  is 0xFF ⇒ Create/Remove enabled = same as the port's interim.

To actually SEE dynamic disabling (if desired): the **fresh / no-design** state
(L0e98: only Modify/View/Exit) — the port never reaches it because
port_test_seed_design always seeds a party, so -27932 is always set. Showing it
means a "no design loaded" entry path, not a menu fix. The View>5 gate already
works. So P1 is effectively CLOSED; was the menu's appearance compared against a
Mac capture? If buttons should grey on the loaded design, get a BasiliskII
Training-Hall screenshot to pin which item + which state.

(Original framing kept below for reference.)

The menu **does** enable/disable by context already (jt918 ~48885), but on
**stand-in gates**, not the faithful ones — so buttons don't grey out the way
they should:

- **`g_a5_27932 != 0` is the only context axis.** jt918 branches on "a character
  is current" (set everything live) vs "fresh" (only Modify/View/Exit). The
  faithful menu keys each item off real state. CURRENTLY MISSING the per-item
  gates:
  - **Create / Remove** — faithful gate = `(gameRecord[48] > 0 UNSIGNED) ||
    rosterDirty(-22730)`; Remove copies Create. The port's game record
    `g_a5_-28006` (g_area_state) is a **memset stub that never sets [48]**, and
    `-22730` is never raised → the port substitutes "design loaded ⇒ Create on;
    Remove needs -27928". **TODO: lift gameRecord[48] population, then restore
    the faithful gate** (boot.c ~48890 has the exact TODO).
  - **View** — faithful: disable when >5 active (rec[147] bit-7-clear) party
    members. Port walks -27928 for this (~48908) — looks faithful; verify.
  - The rest (Modify/Delete/Add/ChangeClass/Save/Begin) are hard-set to 1 when a
    char is current — **no real gate**. Faithful targets to derive:
    Add → benched chars exist AND party < CG_PARTY_MAX; Begin → party non-empty;
    Save → an adventure is in progress; ChangeClass → selected char is Human.
- **Roster selection → -27932 wiring.** The menu context is keyed on -27932 (the
  "current/highlighted" character). CONFIRM the roster-grid cursor
  (jt934/jt936 nav over l02dc) updates -27932 as you move the highlight — if it
  doesn't, the menu can't reflect "the selected character" no matter how good
  the gates are. (Likely the real gap behind "depending on a selected character
  the buttons may be disabled".)
- **Faithful reference:** the c79x-set logic is L0df6 (current) / L0e98 (fresh)
  inside jt918; the per-item gates are L0e14 (Create/Remove) and L0e44 (View) in
  CODE 12. Pull those arms from the disasm and replace the stand-in gates.

## P2 — char-gen remaining (after P1)

- **Modify Character** — DIAGNOSED 2026-06-18 (user: Modify opens the char sheet
  showing name/stats/THAC0 and lets you EDIT the statistics). The real editor is
  **L618c = JT[560] = CODE 17 + 0x618c (~670B, 0x618c..0x642a)**: it calls
  JT[886] (the 6-panel sheet) + edits the ability words rec[112..] / HP, via
  subs L4ddc / L4d64 / L642c. The port has the two handlers CROSSED: the "Modify
  Character" button (case 1 / l0f2e) runs **L15e2**, which the disasm shows is a
  saved-character DELETE manager ("Delete %s forever?" → unlink; no edit logic —
  its L1838 branch just loops/exits), so a Modify pick only refreshes; and the
  real stat editor L618c is the target of case 2 (port-labeled "Delete",
  l0f3e → cg_delete_character stub). FIX (next, focused): lift L618c + wire the
  "Modify Character" button to it (a sheet view of the SELECTED existing
  character, NOT re-rolling). The AC-convention bug in the cg_modify_sheet
  reroll stand-in is fixed (bc48b0d). The old "L618c = Modify editor" audit note
  was right about L618c being the editor but wrong about the case wiring.
  USER SPEC (2026-06-18): Modify shows the sheet, cycles through the six stats
  with a 'next' button (editing each), and AFTER the stats lets you edit the
  NAME. So L618c = a per-ability edit loop (rec[112..] words) + a name edit, with
  jt886 repainting the sheet and the derived stats (jt21/cg_finalize_stats)
  recomputed on each change.
  PROGRESS (lifting L618c incrementally, l618c stays `unused`/dead-stripped — no
  runtime risk until wired):
  - jt178 action bar — DONE (125e719).
  - display helpers (l4ddc/l4df0/l4dfe/l4d64/l642c) + l618c dispatcher skeleton
    — DONE (2bb241f). Verified against the real L618c disasm: the -6979 ability
    saves, the JT[3] case 0..7 → eight handlers, and the edit loop all match.
  - **stat-edit dispatch (this slice)** — DONE: the 5 self-contained ability
    handlers **L5234 STR / L547c INT / L55da WIS / L576a DEX / L5aa8 CHA** + the
    shared **L4fc6 Exit** (revert from -6979/-6973/-6972/-6971 + jt21) + **L5044
    Keep** (re-grant spells/profs, average-HP recompute over the jt22 hit-die
    loop, promote working→base) + **L64ec** (proficiency-bit clear). Tables:
    racial min/max at -30960+race*16 (STR min/max/exc% gender-interleaved by
    rec[92]), class min at -30552+class*6 — matching the l1672 roll clamp.
  - **CON + HP + the HP-recompute pair (this slice)** — DONE: **L58ca (CON)** and
    **L6084 (HP field 7)** lifted; they re-clamp rec[129] through the **L4e04**
    (min) / **jt899** (max, CODE 19+0x5274) HP recompute over the character's
    classes (jt881/jt22 hit dice, name-level cap -23007, CON bonus -22993, prior
    level rec[164+cls] vs current rec[138]). CON Add re-floors HP at l4e04, CON
    Sub re-caps at jt899; the HP field edits rec[129] directly within those
    bounds. All six ability handlers + the HP field are now faithful.
  - **WIRED + live-verified (this slice)** — the "Modify Character" button
    (l0f2e) now runs l618c on the selected char (seeding -18882 = jt1199(design
    -18844) for the eligibility guard, mirroring the Mac's L0f3e). jt178 fixed to
    its real 4-arg ABI (prompt, options, w1→L206e layout, w0→L23b4 modal). The
    crossed Modify/Delete handlers are resolved by-label (as l0f60 already does
    for Train/Create): case 1 = Modify → l618c, case 2 = Delete → cg_delete.
    Hatari-verified via the new **FRUA_MODIFY** harness (`make
    EXTRA_CFLAGS=-DFRUA_MODIFY run-game`): the sheet + "MODIFY: Next Previous Add
    Sub Keep Exit" bar render; Add clamps STR at the Human max 18 (with the *
    marker), Sub drops WIS 13→11, Next walks STR→INT→WIS, Exit reverts the edits
    and l618c returns cleanly (no hang). The l4e04/jt899 HP math is exercised on
    CON/HP but not yet spot-checked against a Mac capture.
  - **name editor L5c1e — DONE + Hatari-verified.** jt60 was already lifted
    (=l5f84); L5c1e is the in-sheet name-field editor (cursor field 6) over the
    C-string at rec[96] (max 15, caret -6929): printable insert, Backspace(8)/
    Del(138), Left(134)/Right(130) wrap, Up(135/136) and Down|Return(13/132/133)
    leave the field (to CHA 5 / HP 7) after a party name-uniqueness check (jt396,
    "Already a %s in party!"). Field 6 is in the ring when jt180()==0 (-12647).
    PORT CONCESSIONS in the key poll: the Mac uses jt1134(tick)/jt486 but in the
    port jt1134 *consumes* the keyDown (it pumps l725c) and jt486 doesn't drive
    WaitNextEvent — so the poll uses the proven jt1078 pump (jt1125(7)+jt1118) AND
    calls qd_present() each spin (the Mac's jt1134 blitted; without it the name-
    field repaints stay back-buffered and never show). Verified: typed FRODO
    inserts + renders, Return leaves to HP, Keep commits + l618c returns. Harness:
    FRUA_MODIFY (seeds -12647=0 + the party head -27928).
  - REMAINING: (a) the jt178 beveled-button bar renders, but the shape-5 jt452
    mouse-button install (the g_a5_12911 block) is still deferred — keyboard
    drives the bar today; (b) spot-check CON->HP recompute (l4e04/jt899) against a
    BasiliskII capture; (c) l5c1e's arrow/Del codes are faithful to the Mac's
    130-138 but the port jt1133 mapping for those wasn't live-confirmed (printable
    + Backspace + Return are).
- **Body-grid mouse click (A)** — jt1139 grid hit-test now lifted (6d45271), so
  the mechanism is correct, but a **real-mouse click on a body cell is not yet
  user-confirmed** (couldn't verify headlessly). First live check in the
  morning. If clicks land on the wrong cell, suspect the native-vs-×2 coord
  model (#108) in jt1139's scale.
- **L455c starting equipment** — the faithful jt574 tail's equip step is
  unlifted; a new unarmored character's AC/empty pack are already correct, so
  this only matters once starting gear is expected. Low priority.
- **Body-grid perf** — per-cell jt56→l3b1e compose is slow, and jt453 re-runs
  l11ac (full grid redraw) on every nav move. Optimisation, not correctness.

## P3 — dispatch cases that are port stand-ins (not faithful lifts)

The cases call port-local `cg_*` helpers rather than the faithful CODE-12/17
bodies. They work for the demo but aren't 1:1:
- l0f2e Modify, l0f3e Delete, l0f74 Remove, l1036 Add, l104c View → `cg_*`
  reimplementations.
- l1060 ChangeClass, l10ca Exit, l1142 Save, l115a Begin, l120c Load → port
  stand-ins (Save/Load/Begin route through port_* play bridges, not the faithful
  CODE 15-19 path — see [[faithful-play-entry-chain]] / task #100).
- l0f60 Create → **already the faithful char-gen** (jt574, 585630f).

## Cross-refs
- Broader stub list + band coverage: `docs/stub-inventory.md`.
- Char-gen internals + L618c: `docs/code17-chargen-wall.md`.
- Faithful play entry (Save/Begin/Load): task #100, [[faithful-play-entry-chain]].

## The ADD-CHARACTER wedge — SOLVED (2026-07-12)

Clicking **Add** in the party-roster picker froze the dialog: the character was
not added, and **Exit** stopped responding afterwards. The engine was not
crashed — it was spinning, and `jt169` never returned.

**Chain:** Hall menu case 5 (`l1036`) → `l12a0` → `jt169` → `l23b4` (the modal
poll). `l2d3e` DID return the Add button's item index; the hang was downstream,
in `l12a0`'s add path:

    jt477(&g_a5_22212, 398, fresh_slot);          /* out-param never deref'd */
    matched = jt165(idx, tail);
    jt587(fresh_slot, matched, 0, 1);             /* BOTH ARGS WRONG */

The Mac (CODE 12, L13f8..L1430) is `jt587(&node[5], slot, 0, 1)` — the character
**NAME** first, the freshly-allocated 398-byte **slot** second. jt587 zeroes its
2nd argument for 398 bytes and then loads that name's save into it. The port had
them swapped, so it **zeroed 398 bytes over the roster's LIST NODE** and took the
name from an uninitialised 64-byte local. `l01be` never names the peer list's
nodes either (it writes the display name only into the out2/display list), so the
name was empty regardless. The open failed, and **`jt987` spun in its cold-disk
retry dialog forever** (`l157c` is a stub, so the dialog can never be answered) —
the same hard-hang trap as the combat library-id mismatch.

**Why the port cannot take the Mac's add path at all.** The Mac's Add is a DISK
LOAD: allocate a slot from `g_a5_22212`, fill it from the character's own save
file. Two independent blockers:
  1. the port's saved characters are flat 512-byte `cg_pool` dumps
     (`save_roster`), not the Mac's `.cch` stream that `jt577` walks; and
  2. decisively, the port models the party as nodes **inside `cg_pool`** —
     `cg_node_in_pool()` rejects any node not on a 512-byte pool boundary — so a
     `g_a5_22212` slot could never be walked as a party member.
So the pick is resolved against `cg_pool` (ADR-0003), and the Mac's party CAPS
are lifted on top of it. Wiring the faithful loader means migrating the save
format to `jt578`/`.cch` first; `jt577`/`jt578` exist and round-trip, and the
reconciled reader `l08ba_c15`/`l_cch_read` is already there, unused, for then.

**Also fixed, both faithful lifts of the same block:**
- The party CAPS (L1486..L1538) were missing entirely: duplicate rejection,
  bodies < 6, party < 8, and the "too many rangers in party" cap (JT[42],
  STRS 0x5fe0) at 3+ rangers.
- The loop-exit test was **INVERTED**. Mac L159c leaves when the party is FULL
  (`body_count > 5 || occupied > 7`); the port had `body_count < 5 && occupied
  < 7`, so it left after a single add and looped once the party was full.

**LIVE:** Add MALTIER then BARBARUS — both gain the `* ` in-party marker, the
picker stays responsive, re-adding a marked character is ignored, Exit returns
to the Hall, and the roster shows both (MALTIER AC -5 HP 77, BARBARUS AC -3
HP 78).
