# Training Hall character-management architecture (disasm deep-dive 2026-06-18)

Traced from the Mac CODE 12 disassembly to pin down how the Training Hall
actually manages characters — selection, the three character "pools", and the
screen behind each button. Written because the port had been wiring individual
buttons without a model of the whole system (the user correctly flagged: you
can't select a character, and a created character doesn't appear in the
roster). Companion to `docs/training-hall-wall.md` (the worklist).

## The three character pools

FRUA keeps characters in **three distinct places**, and the Training Hall
buttons move characters between them:

1. **Saved characters** — `.CHR` files on disk. Created by **Create**, removed
   by **Delete**. Enumerated by **jt589** (CODE 15+0x362) into a list.
2. **The party** — `g_a5_-27928`, the head of a singly-linked list (`.next` at
   offset +0) of the characters in the *current adventuring party*. **This is
   the roster shown at the Training Hall.** Built up by **Add**, torn down by
   **Remove**.
3. **The selected character** — `g_a5_-27932`, the party member currently
   highlighted in the roster. Every per-character action (Modify, Remove, View,
   Train) operates on `-27932`.

A created character is saved to disk but is **NOT** added to the party — that is
exactly what **Add Character** is for. So "I created a character and it didn't
show up in the roster" is the *correct* model; the port is just missing the Add
screen that would let you pull it into the party.

## Selection — `L0848` (CODE 12+0x848), arrow keys

`-27932` is moved by **L0848**, a JT[3] dispatch keyed on the arrow-key codes
(table min=132 max=136 — the same 130-138 nav band the char editor uses). It
walks the party list `-27928` and updates `-27932` up/down the roster. It is
called from the menu input loop (l0aae's helper at 0x0a4c) and from the
Add/Remove screens (0x3c6e/0x3cc8/0x3efe).

**So selection is by ARROW KEYS over the party roster, not (only) mouse.**
`L0848` is **not lifted in the port** → arrow keys at the Hall do nothing →
you're stuck on whatever `-27932` was seeded to. **This is the #1 blocker.**

The roster is painted by **l02dc(highlight)** (lifted, boot.c ~6997), which
takes `-27932` as the highlight argument; jt918 calls `l02dc(-27932)` each
frame, so the selected member IS drawn highlighted — there's just no way to
move the highlight yet.

## jt918 main loop (CODE 12+0xd90)

```
loop:
  render chrome (jt112/jt81/jt108)
  if (-27932 != 0):  l02dc(-27932)          ; paint roster, selected highlighted
                     set the c79x menu flags ; based on -27932 + party walk
  else:              set the "no selection" flags
  pick = l0aae()                            ; menu via jt453 (mouse+kbd) -> 0..11
                                            ;   (arrow keys route to L0848 inside)
  JT[3] dispatch pick -> l0f1a..l120c
```

The c79x cluster (`g_a5_-14440..-14429`, index 0..11) gates the 12 buttons; the
faithful "char selected" arm (L0df6) enables Modify/Delete/Add/View/Save/Begin
and gates Create+Remove on `gameRecord[48]` (-28006[48]) / dirty (-22730), and
greys View when the party has >5 active members (rec[147] bit-7 walk).

## The case ↔ handler ↔ label map (and the "crossed" trap)

`l0aae` builds the 12 buttons in VISUAL order (Train, Modify, Delete, Create,
Remove, Add, View, ChangeClass, Exit, Save, Begin, Load) and `jt453` returns the
clicked index, BUT the Mac's JT[3] handlers are **swapped relative to the
labels** for two pairs — the port already documented this for Train/Create and
it holds for Modify/Delete too:

| idx | button label      | Mac handler        | what it actually does                     |
|-----|-------------------|--------------------|-------------------------------------------|
| 0   | Train Character   | L0f1a → JT[574]    | **Create** (rolls a new char)             |
| 1   | Modify Character  | L0f2e → **L15e2**  | **Delete** browser ("Delete %s forever?") |
| 2   | Delete Character  | L0f3e → **L618c**  | **Modify** stat editor                    |
| 3   | Create Character  | L0f60 → JT[557]    | **Train**                                 |
| 4   | Remove Character  | L0f74 → JT[556]…   | Remove `-27932` from the party            |
| 5   | Add Character     | L1036 → JT[904]    | Add a saved char to the party             |
| 6   | View Character    | L104c              | View `-27932`                             |
| 7   | Human-Change-Class| L1060              | class change                              |
| 8   | Exit from Play    | L10ca              | leave                                     |
| 9   | Save Current Game | L1142              | save                                      |
| 10  | Begin Adventuring | L115a              | enter the dungeon                         |
| 11  | Load Saved Game   | L120c              | load                                      |

**The port resolves this by LABEL** (case N runs what button N is *named*), so
the port's wiring is:
- case 1 (l0f2e, "Modify") → **l618c** (the editor). ✓ DONE.
- case 2 (l0f3e, "Delete") → the **L15e2 delete browser** (port stub
  cg_delete_character). ← still a stand-in.
- case 0 (l0f1a, "Train") → train; case 3 (l0f60, "Create") → jt574. ✓ DONE.

## The screen behind each button

- **Create** (jt574, DONE/faithful) — roll a new character, finalize, **save a
  `.CHR`** (jt584). Does **not** touch the party.
- **Add** (L1036 → **JT[904]**, CODE 19+0x213e) — browse the saved-character
  pool, pick one, splice it into the party list `-27928`. **Port: not faithful.**
- **Remove** (L0f74 → **JT[556]** CODE 17+0x66ee, + jt41/jt878/jt876) — unlink
  the selected `-27932` from the party (back to the saved pool). **Port: stub.**
- **Delete** (L15e2) — jt589 list → jt169 list dialog → "Delete %s forever?" →
  "Are you sure?" → jt431/jt988/jt471/jt581 unlink the `.CHR` file. **Port:
  stub cg_delete_character.**
- **Modify** (L618c, DONE/faithful) — edit `-27932`'s stats (only a fresh char).
- **View** (L104c) — view `-27932`.

## Port gaps, prioritized

1. **Selection (L0848)** — DONE (2026-06-19). Lifted L0848 + wired Up/Down into
   the Training Hall menu modal: l0aae sets `g_hall_roster_nav` around its
   jt453, and l2d3e routes the Up(264)/Down(260) arrow keys to `l0848(135/133)`
   then returns the sentinel 12 so jt918's loop re-renders the roster with the
   new highlight (repainting *inside* the modal draws against the wrong
   CLUT/clip and garbles the roster). Hatari-verified via FRUA_HALL: Down/Up
   toggle the THORIN/ELF highlight. STILL OPEN: roster MOUSE-click selection
   (the Gold Box UI is keyboard-first, but a click-to-select would be nice;
   no L0848-adjacent mouse handler found yet).
2. **Add** — FUNCTIONAL via the port's cg_add_character stand-in (browse the
   cg_pool CHAR_INPARTY==0 slots, Up/Dn, Return adds to the party). CORRECTION
   2026-06-19: **jt904 is the VIEW screen, NOT Add** — it paints a sheet + a
   dynamic jt182 action bar. Add/View are crossed like Train/Create (case 5/Add
   label -> jt904/View; case 6/View label -> L12a0); the real party splice
   (-27928 writes) is in CODE 21. The actual bug was jt574 (Create) auto-joining
   the new char to the party (CHAR_INPARTY=1); FIXED (26fd85b) — Create now
   benches it, so Create -> Add Character -> party is the faithful flow. The
   faithful jt904/L12a0/CODE-21 screens are a fidelity upgrade, deferred.
3. **Remove** — FUNCTIONAL via cg_remove_from_party (browse the party, Up/Dn,
   Return benches the member back to the pool). Verified live (dee4d44).
   CORRECTION 2026-06-19: jt556 ("only conscious humans may change") is the
   Change-Class check, so the Mac's case-4 body (L0f74) is actually
   Change-Class — Remove/Change-Class are mirrored vs their labels (case 4
   "Remove" -> Change-Class body; case 7 "Change Class" -> L1060). The port's
   l0f74 had carried that change-class code as no-op stubs; stripped. l0f74 now
   just removes + re-points the selection. **Delete (L15e2)** — still the
   cg_delete_character stand-in.
4. The port currently **seeds a demo party** (port_test_seed_design) directly
   into `-27928`, bypassing the real Create→.CHR→Add→party flow. Once Add/Remove
   are faithful, this seed can be retired in favour of a real saved-character
   pool.

## Key globals / JT entries

- `-27928` party head · `-27932` selected · `-27940` selection backup (L0848)
- `-28006` gameRecord (Create/Remove gate byte [48]) · `-22730` roster dirty
- c79x `-14440..-14429` = the 12 button-enable flags
- jt589 saved-char list · jt169 list dialog · l02dc(highlight) roster paint
- L0848 selection · JT[904] Add · JT[556] Remove · L15e2 Delete · L618c Modify
