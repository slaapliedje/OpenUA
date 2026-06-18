# Training Hall + char-gen worklist (audit 2026-06-18)

Audit of the stubs / stand-ins / missing pieces in the **Training Hall menu**
and the **character-creation** flow, written as the morning worklist. Broader
JT-level coverage lives in `docs/stub-inventory.md`; char-gen internals in
`docs/code17-chargen-wall.md`. Priority order is top-down.

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
