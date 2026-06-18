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

## P1 — Training Hall dynamic menu (the keystone the user flagged)

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

- **Modify Character (case 1, l0f2e)** — "does nothing but refresh the page."
  The Modify stat editor **L618c is unlifted**; case 1 currently re-paints the
  roster (l02dc) and returns. Lift L618c (the per-stat +/- editor) — see
  `docs/code17-chargen-wall.md`.
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
