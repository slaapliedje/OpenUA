# Modify-Character stat click — trace (SOLVED — wire located)

## THE WIRE (2026-08-10)

`jt178` (the modal action bar, CODE 7 0x2866) installs the clickable stat cells
in a block **gated on `g_a5_-12911`** (0x288e: `tstb -12911; beqw L297a`). When
the gate is set it streams **eight shape-5 DLItems** through `jt452`:

```
  y=8036,8040,8044,8048,8052,8056   the six ability rows (STR..CHA), 4 apart
  y=8004                            (name / top field)
  y=8012 x=8080                     (HP, right column; guarded by L38f8())
```

Each is `jt452(5, rec16=y, rec18=x=8000, rec22=4, rec24=80)` followed by token
`20` (set `rec[28]` bit 4). Shape 5 -> method `jt378`, the list-cell hit-test —
the SAME clickable-list primitive the pick-treasure / character-select screens
use (maintainer's lead was exactly right).

**Our `jt178` lift OMITS this entire block.** It runs `l206e; l2170; l2858;
l23b4` and skips 0x288e..0x2976 — and the code even says so: *"the shape-5 jt452
button bar (for mouse clicks) is a TODO."* That is why the stat frame has no
clickable region (measured: 0 shape-5 installs on the modify screen).

**The gate is already satisfied.** `g_a5_-12911` is set to 1 in our
`jt452_init` (boot.c ~21517, "match real play so jt452 grids draw"). So the ONLY
missing piece is the install block itself.

### Implementation spec

Port the Mac `jt178` block 0x288e..0x2976 verbatim into our `jt178`, between
`l2170(n)` and `l2858(10)`, guarded by `if (g_a5_12911 != 0)`:

1. The seven unconditional shape-5 cells (six stats + name), each `jt452(5, y,
   8000, 4, 80)` then `jt452(20)` (or set `rec[28] |= 0x10` inline).
2. The `L38f8()` conditional: if true, `jt452(17)` on the last cell (set
   `rec[28]` bit 1). `L38f8` is CODE 7 0x38f8 — identify/lift it.
3. The eighth cell at (8012, 8080).

**The one thing to verify during the lift:** how a clicked cell maps to the
stat cursor `-6926`. `l23b4`/`l2d3e` returns the hit item's index; confirm
`jt178`'s return (L25b6 mapping) or `l618c`'s `l4d64(act)` turns the clicked
cell index into the right stat. `-6926` is written ONLY by `l4d64`, so trace
that the clicked-cell index reaches `l4d64` with the stat number. Do this on a
LIVE modify screen (FRUA_MODIFY + FRUA_CLICKDIAG), not by assumption.

**Caution:** `jt178` is shared by many screens; the block only fires when
`-12911` is set, but verify it does not add spurious cells to other command-bar
screens (roll a FRUA_CLICKDIAG shape census on a few before/after).

---

# (historical trace below — kept for the ruled-out list)


**Symptom (hardware):** on the Modify-Character stat editor you cannot click a
stat (STR/INT/WIS/DEX/CON/CHA or its number) to select/highlight it. Keyboard
(the `Next/Previous/Add/Sub/Keep/Exit` command bar) works; the mouse does not.

**This IS a Mac 1.0 feature** — verified in BasiliskII on Mac 1.0: clicking a
stat or its number highlights the number in cyan. It is **byte-identical in
Mac 1.2** (CODE 17 has zero real hunks; see `mac12-hunk-log.md`). So it is a
**port wiring gap against a faithful lift**, not a version difference and not an
oracle cherry-pick. The screen is `l618c` (= JT[560], CODE 17 + 0x618c).

## What is confirmed

**The click DELIVERY chain is complete and correct** (traced live with
`FRUA_CLICKDIAG`):

```
mouseDown -> l725c (JT[456] dispatch) -> l690e case 3 (inContent)
          -> l6b26  (captures click into g_a5_-912 h / -910 v / -901 flag)
          -> jt1125 drains it, returns (ev.where.v, ev.where.h) as (out1,out2)
          -> l2d3e  (JT[456]) hit-tests the DLItem pool: for each item with a
                     method, calls method(item, cmd=2, cy, cx); on a hit,
                     commits with cmd 3 (focus/caret-at-click) or cmd 4 (select)
```

`l618c`'s edit loop is a **faithful** lift of CODE 17 0x62c6..0x6410; it calls
`jt178("Modify:", "Next Previous Add Sub Keep Exit", 1, 0)` and dispatches to the
six stat handlers by the current-stat cursor `-6926`.

**The modify screen's DLItem pool has 7 items** (dumped live via the
`FRUA_MODIFY` harness, which enters `l618c` on the same code path as the Hall
button `l0f2e` — no install difference): the six `jt178` command-bar buttons
(all at `iy 8094`, method `jt137`) plus one list sentinel (`iy 0`, method
`jt376`). **There are NO stat-row items.** So `l2d3e` has nothing to hit when
you click a stat.

## Mechanisms RULED OUT (do not re-check these)

- **Not per-stat DLItems.** No installer puts stat rows in the pool. The two
  install primitives — `jt452` (CODE 3 0x29a0) and `jt325` (CODE 9 0x22d8) — are
  never called for stat positions anywhere in the modify path. `jt325`'s only
  callers are in CODE 2/10/11. The sheet painter `l1276`/`jt886` (CODE 19) and
  the row painter `jt895` (CODE 19 0x1f00) only PAINT (jt94/jt895); neither
  installs an item.
- **Not the move-pending flag.** `-6927` (`l4dfe`) is written ONLY by `l4df0`
  (CODE 17 0x4df4), and BOTH its callers (0x62f6, 0x63f0) push 0. So `-6927` is
  always 0, the `l4d64` cursor-move at 0x6344 is never reached that way, and the
  flag is effectively vestigial in this loop.
- **Not a second writer of the stat cursor.** `-6926` has EXACTLY ONE writer in
  the whole Mac binary — `moveb %fp@(9),%a5@(-6926)` at CODE 17 0x4dd2 inside
  `l4d64`. Every `l4d64` caller is a keyboard `Next/Previous` handler (or the
  setup's `l4d64(0)`). So no click path currently sets `-6926`.
- **Not a coordinate hit-test on the captured click.** The captured click coords
  `-912`/`-910` are READ in exactly one place — `jt1125` (CODE 4 0x624c/0x6254),
  the mouseUp relay. No stat hit-test reads them.
- **Not a harness artifact this time.** `l0f2e` (Hall "Modify Character") only
  calls `l618c`; it installs nothing the `FRUA_MODIFY` harness skips.
- **Not a dropped JT call.** `tools/jt_call_audit.py --func l618c` and `--func
  l1276` are clean (l1276's only drop is the benign name painter JT[25]).

## DECISIVE (2026-08-10): the modify screen installs NO clickable stat region

Following the maintainer's lead — "we have clickable text lists elsewhere
(character selection, pick-treasure); look for a 'pick item' function" — the
mechanism is now identified and the gap is measured.

**The reusable clickable-list is `jt169` (List Manager).** The pick-treasure
screen (`l39ac`) runs `jt169` over a rect and returns the chosen item;
Select-a-Design, coin-trade and Load-Saved-Game use it the same way. Its
clickable primitive is a **shape-5 DLItem** whose method is `jt378` and whose
`rec[4]` is a row-pick proc (`jt156` roster / `jt223` list). A shape-5 cell is
installed by `jt452((short)5, ...)` — that is what `jt158` does.

**The modify screen installs no such cell.** Instrumenting `jt452` (FRUA_CLICKDIAG
logs every allocation's shape) across the whole modify screen build yields:

```
6 x jt452 install shape 1   (the Next/Previous/Add/Sub/Keep/Exit command bar,
                             method later overridden to jt137)
1 x jt452 install shape 7   (the keyboard/list sentinel, iy 0, method jt376)
```

**Zero shape-5.** So there is provably no clickable region over the stat frame —
exactly matching the report ("the stat frame is the only area I'd expect to
click; the rest beeps", and it does nothing). A shape-5 cell over the ability
block, with a `rec[4]` proc that maps the click row to a stat and highlights it
via `l642c(1, stat)`, is what is missing.

**Still unresolved: WHERE the Mac installs it.** `l618c` calls neither `jt452`
nor `jt158` directly (its JT set is 101/1200/178/179/180/3/384/7/882/886/892 —
identical in our lift, audit-clean), and neither the sheet painter `l1276`/`jt886`
(CODE 19) nor the row painter `jt895` calls the shape-5 installer in EITHER fork.
So the Mac's stat-frame cell is installed by a path this trace has not located —
either a conditional `jt452(5,...)` inside a shared function whose branch our
lift takes differently (`jt178`/`jt179`/`jt886`), or a mechanism outside the
shape-5 family after all. The fastest way to settle it is a BasiliskII
behavioural trace of the Mac 1.0 modify screen (which functions run on a stat
click), or a branch-level read of the Mac `jt886`/`jt178` for a shape-5 install
our lift skipped.

## The open question

Every path checked leads to: **the port's modify screen has no clickable stat
region, and none of the usual installers create one — yet Mac 1.0 makes stats
clickable.** The Mac must map a stat click to a selection through a path this
trace has not located. Candidates not yet run to ground:

1. **A method's cmd-3/cmd-4 arm we stubbed.** `l2d3e` commits a hit with cmd 3
   or cmd 4. Our `l2d3e` has special arms for `jt378` (roster row) and text
   fields; the Mac may route a stat hit through a *different* method whose cmd-3
   handler highlights the row and records the stat. If a stat item IS meant to
   exist, find its METHOD first (a CODE 17 proc that, on cmd 3, highlights via
   `l642c(1, stat)`), then find who installs it — the method identity is the
   thread to pull.
2. **The command bar carries the stats.** `jt178`/`l206e` may lay out MORE than
   six buttons — one clickable region per stat as well — and our `l206e` lift
   may install only the six option labels. Compare the Mac `l206e` (CODE 7)
   install count against ours.
3. **A sheet-level clickable cell.** The character sheet (`jt886`) is shared with
   VIEW/CREATE; the cyan-highlight-on-click may be a sheet feature (one shape-5
   cell over the ability block whose method maps click-Y to a stat), installed
   by a call our `jt886` lift omits. Diff the Mac `jt886` (CODE 19 0x1276)
   install calls against ours.

## Fastest experiments to settle it

- **Dump the Mac's pool, not just ours.** Statically count the DLItems the Mac
  modify screen installs (every `jt452`/`jt325` reachable from `l618c` + `jt178`
  + `jt886`). If the Mac count is also ~7, the stat click is NOT pool-based and
  the answer is a coordinate test inside a method or the sheet — look there. If
  the Mac count is ~13 (7 + 6 stats), an installer call is missing from our lift
  — find which `jt452`/`jt325` site.
- **Trace CREATE.** Character creation uses the same `jt886` sheet and lets you
  interact with rolled stats; it is a working reference. Diff how CREATE wires
  stat interaction vs MODIFY.

## Related traps (all cost time earlier this session)

- Harness/live divergence: `FRUA_BODY` calls `jt573` directly and passes where
  the live `jt574` path fails — always verify a screen on the LIVE path.
- `l2856` is not an alias for `JT[1124]`; same-offset-different-segment
  (`l25f4` is CODE 13, not CODE 5's `L25f4`). Match on `(CODE, offset)`.
- Hatari mouse injection fires mid-travel and clamps `cy` at 199 — settle
  coordinate questions from engine logs, never from screenshot pixels.
