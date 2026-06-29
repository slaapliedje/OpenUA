# jt169 — lift the real List Manager (replace the reimplementation) — #146

Status: **CUTOVER DONE 2026-06-28.** The faithful body already existed
(`jt169_faithful`) and was ALREADY live for **7 of 10 callers** (the
`__attribute__((unused))` was just suppressing warnings during a partial
migration). Promoted `jt169_faithful` → `jt169`; the old reimplementation is
demoted to `jt169_reimpl` (unused fallback). All 10 callers now route to the
faithful List Manager. Load picker verified headless (renders via l162e/l1bfe +
l23b4 modal, selecting 'A' loads → roster).

REMAINING USER VERIFICATION (switched callers I can't reach headless):
- **Design picker** (l494e, caller 54590): now faithful → the port-added
  Select/Cancel mouse command bar is GONE (the Mac selects by row-click/keyboard;
  keycode==0=select, ESC=cancel still work). Verify the Select-a-Design flow.
- **Encounter prompt** (l0380, caller 39951): return is discarded so the contract
  can't break; verify the type-21 prompt list still renders.
Fallback: `jt169_reimpl` is intact in git if any picker regresses.

(Original scope below — the asm read + deps map that confirmed the cutover.)

The old `jt169` (boot.c, ~160 lines) was a port REIMPLEMENTATION; this is the
faithful lift of JT[169] = CODE 7 + 0x3600 (~726 bytes).

## Why staged + verified (not a one-shot rewrite)

Replacing a *working* 726-byte subsystem used by **3 callers** carries real
regression risk. The reimpl is the git fallback. Lift, then verify each picker.

## Faithful structure (L3600, 10 sections)

1. **L3600** empty list (arg h2 / fp@24 == 0) → clear idx/next, return 27 (ESC).
2. **L3618** `l206e(h1, &buf116)` counts/loads list rows → fp@-2 = count-1.
3. **L3634-3694** clamp scroll offset `-12656` to the list; walk the `-12664`
   cursor (head = fp@24) counting nodes, bumping the selection past the active
   (rec[4]) node.
4. **L3696-36f2** geometry: rows = rec[19]/[17]/[23]/[21] (top/left/bottom/right)
   each `ext`+`<<2` (×4, the 8000-coord scale); scrollbar flag fp@-28 = 2 when
   list rows > visible rows, else 0.
5. **L36f2-372a** DLItem handler table: fp@-20 = JT[143] (jt143, cell getter),
   fp@-16 = JT[144] (jt144) iff `-12649`; snapshot+clear `-12654`.
6. **L372a-374a** up-arrow (key 38 / rec[21]) adjusts the left coord fp@-30 by
   the scrollbar flag.
7. **L374a-3796** BUILD+RENDER: `l2062`(=jt174 dirty) · jt447 (DLItem reset) ·
   jt108(1) clear · jt112(1) · `l162e` window box · **`l1bfe`(fp@-2, &buf116,
   h1, 1)** the list-content builder (installs jt138/jt139 per row) · jt449(1)
   paint · jt112(0) · jt117.
8. **L3796-37cc** `l0c82(&fp@-36, flag)` item layout; clear flag (fp@32);
   jt444(1,33,64) enable iff `-12649`.
9. **L37cc-37f6** set `-12649`=1; **`l23b4(-12650)` → fp@-6 = modal result**;
   clear `-12650`; if `-13006` (abort) → return `-24134`.
10. **L37f6-38d2** jt451; classify fp@-6 (sel): 0 → key=27, `-24139`=1; > count
    → buf116[0]; else → buf116[sel-1][0]. Then **map the key through the
    `-24126` slot table** (jt179's 40-byte table, stride 2): scan 0..19 for the
    entry whose byte[1]==key → return byte[0]; key 27 → 27; else → -1. Finally
    write rec[0]=fp@-24 (the count), `L3564`(jt143) refresh, `next`(fp@40) =
    `-12660`, restore `-12656` = fp@-26, return the mapped byte (fp@-122).

## Dependencies — ALL LIFTED

l206e@12931 · l162e@19736 · l1bfe@24393 (installs jt138/jt139 ✓) · l0c82@24320 ·
l23b4@53069 (the modal loop) · jt143@23375 (L3564 cell getter) · jt144@24382 ·
jt179@13060 (the -24126 slot table) · jt447 · jt451 · jt444 · jt108 · jt112 ·
jt117 · l2062→jt174. Nothing blocks the lift.

## A5 globals

-12656 scroll offset · -12664 / -12660 list-node cursor · -12654 (snapshot) ·
-12649 (list-built flag) · -12650 (l23b4 arg) · -13006 abort · -24126 slot table
(40B, jt179) · -24134 abort-return · -24139 (ESC flag) · -12656 restore.

## Callers (verify each after the lift)

| site | picker | args (h1, h2, …) |
|---|---|---|
| 39951 | (h1, -13848, …) | TBD — design/list |
| 54590 | (-13952, …) → keycode | the save/character picker |
| 64513 | (-13868, -13696, …) | the Load-Saved-Game picker |

The reimpl's signature `(h1,h2,top,left,right,bottom,head,a,b,*flag,*idx,*next)`
maps to the fp args above (fp@8=h1, fp@12=h2/buf, fp@24=head, fp@32=flag,
fp@36=idx-ish, fp@40=next). Keep the C signature; rewrite the body.

## Verification matrix (headless harness + user)

- **Load picker** (caller 64513): `p` → Hall → `l` → picker renders → `a`/Down/
  Return selects → roster. Reachable headless (FRUA_NO_CONOUT for arrows).
- **Save picker** (54590) + **design picker** (39951): need the user (Save Game
  / Select a Design flows) — flag for manual verify.
- Watch: scroll clamp (off-by-one), the -24126 keycode mapping (wrong map →
  wrong item selected), the empty-list ESC return.

## Plan

1. Write the faithful body (≈120 lines) replacing 24543-24699, keeping the sig.
2. Build; verify the **Load picker** headless (the reachable one).
3. Commit; ask the user to verify Save + design pickers.
4. Revert to the reimpl (git) if any picker regresses.
