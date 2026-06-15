# CODE 17 — character generation / training (task #101) worklist

CODE 17 holds **two big interactive screens** plus a set of helper leaves. The
*pick* spine of CREATE is already faithful; the *finalize / name / review /
save* tail and the whole *train* screen are the remaining campaign.

Regenerate status with `tools/jt_progress.py` (filter CODE 17 via
`data/work/disasm/jumptable.txt`). 20 JT entries; addresses are CODE 17 + off.

## A. CREATE flow — `jt574` (0x3b5e)

```
jt574(existing):                                  STATUS
  jt399(rec,398,0) + seed defaults                LIFTED (port jt574 body)
  L3666 pick screen                               LIFTED (l3666)
    ├ jt566 RACE / jt567 GENDER / jt568 CLASS     LIFTED
    ├ jt569/jt570 ALIGNMENT axes  jt571 Exit      LIFTED
    └ jt572 Done (commit, sets -27932=rec)        LIFTED
  L29ae  max-HP finalize                          LIFTED (l29ae)
  L238e  character-NAME entry (jt98 box loop)     LIFTED (l238e_c17), WIRED LIVE
                                                          into jt574 now jt1078
                                                          is lifted; typed name
                                                          threads to the record.
                                                          NEEDS Hatari verify.
  L0006  body-icon finalize (rec[188])            LIFTED (l0006_c17); wired into
                                                          jt574's Done branch.
                                                          gender×type×class×align
                                                          → rec[188] body shape.
  if new: L1346 (=jt573) REVIEW/MODIFY screen     DONE  (driver + L11ac builder +
                                                          jt562/563/564/565 procs;
                                                          grid PAINT L09dc deferred)
  "Save %s?" jt488 + jt159 confirm                LIFTED deps
  L3cd4 (=jt575) proficiency bitfield finalize    LIFTED (l3cd4_c17); wired into
                                                          jt574 after L0006.
                                                          Merges the -18886..-18893
                                                          per-bit class masks into
                                                          rec[339..354] by class-
                                                          count tier rec[162], the
                                                          rec[376..380] flags, and
                                                          the rec[381] per-slot loop.
  if train: copy rec -> existing                  trivial
  else: L455c + jt584 .CHR save                   TODO  (L455c ~640 lines;
                                                          jt584 = CODE 15 save STUB)
```

Port `jt574` currently runs the faithful pick screen then a **port-side**
finalize (`cg_roll_stats` / `cg_build_record`, placeholder name). Replacing that
tail with the faithful chain above is the bulk of #101.

### Body-icon grid sub-cluster (reached only from L1346/jt573) — DONE
`L11ac` builds the 7×7 body-icon grid screen each frame and registers it via the
`jt452` DLItem stream builder (shape-5 grid item + Exit/Done buttons + the
keyboard source).  All LIFTED this session:
- jt573 (0x1346)  driver: rec[189]=8, double record backup (jt406), loop L11ac,
  restore-on-cancel, return accept/cancel.  Marked `__attribute__((unused))`
  until the create-flow tail wires it (jt574 still uses the port finalize).
- L11ac (0x11ac)  per-frame builder (the three jt452 streams + dialog run/teardown)
- jt562 (0x0854)  grid mouse-click — maps click→grid index -6986, redraws marker,
  stamps rec[188]
- jt563 (0x0fc8)  grid keyboard — arrow keys step the -6986 index (JT[3] 130..136),
  wrap in the 7×7 grid; faithful quirk: the left-arrow arm leaves the moved flag
  unset
- jt564 (0x09ce)  grid "Exit"  — sets -7038=1, -6988=1
- jt565 (0x09c6)  grid "Done"  — sets -6988=1
- L079a (0x079a)  marker frame draw;  L09ba (0x09ba)  redraw flush

REMAINING in this sub-cluster: **L09dc (0x09dc, ~1.5KB)** — the pure-render leaf
that paints the 49 grid cells and latches the current cell value into g_a5_-6985.
Stubbed (PROBE only); the grid draws nothing until it is lifted, but the cluster
is not yet reachable so the blank grid is inert.

TRAP recorded: jt406's Mac ABI is copy(SRC,dst) while the C shim is
jt406(dst,src,n), so every asm `jt406(a,b,n)` was written SWAPPED as
`jt406(b,a,n)` (the three jt573 backup/restore calls).

## B. TRAIN flow (Training Hall, CODE 12 callers)

- jt557 (0x6cd2) **Train Character** — "we only train conscious people",
  "Training costs %s platinum", level-up/cost logic. ~2.7KB. **DONE** (faithful
  full lift). Replaces the old wrong stub (was `(void)jt574(0)`; the comment
  mislabeled it "Create Character"). All deps already lifted (jt26 XP table,
  jt910/jt885/jt33/jt29/jt595/jt41/jt876 + the draw helpers); the live
  g_a5_-14636 class-name table is read for the "will become" preview.
- jt556 (0x66ee) train-related screen (CODE 10/12 caller).  TODO  (STUB)
- jt560 (0x618c) train-related screen (CODE 12 caller).      TODO  (STUB)

## C. Helper leaves

- jt558 (0x6bee) multi-class active-slot finder: for combination-type-5
  (rec[88]==5) returns the first class slot 0..5 with rec[164+slot] > 0, else 17.
  CODE-12 callers.  **LIFTED this session.**
- jt561 (0x4d62) empty (`rts`) — faithful no-op.  **NOOP whitelist this session.**
- jt559 (0x4df0) setter g_a5_-6927 = arg.  LIFTED.

## jt1078 (CODE 5 + 0x440) — the modal line editor — LIFTED 2026-06-15

`jt98` (the framed input box) draws the border + label, then delegates the
keystroke loop to `jt1078`. Now fully lifted (full ~400-line CFG): Return/Enter
accept a non-empty line, Esc/backtick cancel, Backspace/Del delete, printable
32..126 insert; an initial buffer flagged "selected" clears on first edit; the
window context (origin / col-offset / colour) comes from the -4880/-4870/-4860
arrays at paint depth -4886. Also lifted its text-draw helper **L0334** (l0334)
— set colour (l024c) + pen (l0264) + the jt400 %r VM (l0306), with a varargs→
Mac-word/long packer so the C variadic face streams through the faithful VM.

All deps were already lifted/NOOP (JT[1134]/1133/1125/1118/1141/1148/1153/1130/
423/161, l0156). jt98's call gained the 4th `xoff` arg (0). l238e_c17 is now
WIRED into jt574's live create flow (needs Hatari verify of the box render).

## L1346/jt573 review screen — the remaining create-flow blocker

The "review & modify" screen after name entry is an interlocked cluster: jt573
(small backup/restore wrapper) -> L11ac (screen builder) -> the alignment GRID
(shape-5 DLItem) with action procs jt562 (mouse) / jt563 (keyboard) / jt564
(exit) / jt565 (done) + L079a (marker draw). The blocker is **the shape-5 grid
DLItem method** — the port's jt453 fires action procs as `void(*)(void)`, but
jt562/jt563 need the click cell coordinates. That method (Mac: the shape-5
handler that converts a click to a grid cell and calls the action proc with
coords) is not in the port. So the review screen is multi-session DLItem
infrastructure, not a leaf lift. jt574 currently skips it (port build), which is
acceptable — it is the optional "tweak before save" screen.

## Done-criteria for #101
All 20 JT entries LIFTED/NOOP **and** `jt574` runs the faithful finalize/name/
save chain (A) instead of the port stand-in, **and** the Training Hall calls the
faithful jt557 (B) instead of `cg_train_screen`. Cross-segment leaf `jt584`
(CODE 15 .CHR save) may remain a tracked stub until the CODE 15 save slice.

## CREATE-flow status (2026-06-15)

The self-contained finalize chain is now faithful and wired into jt574 in the
Mac order: **L29ae** (HP) -> **L238e** (name, via jt1078) -> **L0006** (body
icon) -> **L3cd4** (proficiency bitfield). What remains each opens a *different*
subsystem, so none is a CODE-17 leaf:

| piece | blocker |
|---|---|
| L1346/jt573 review screen | shape-5 grid DLItem method (DLItem infrastructure) |
| L455c equipment grant      | L439c -> jt902 / jt890 (CODE 19 item-equip) |
| jt584 .cch save            | LIFTED — jt584 (UI/path) + jt578 (serializer) + L0ce0 (record swap); replaces the stub, reachable via the "Save Characters" menu (l1060) |
| jt577 .cch load (read mirror) | LIFTED + REACHABLE — jt577 (deserializer) + jt903 (capacity counter) + l_cch_read (read-opener, flat-shim mirror of l00e0). A probe-gated boot self-test round-trips jt578->jt577 over a scratch record (PASS = name@96 + swapped word@82 survive). Menu-level load (jt576 enumerate dialog) deferred — see impedance note. |
| jt557 TRAIN screen         | DONE (faithful); jt556/jt560 still STUB |

Recommendation: the highest-value next subsystem is the **save tail** (jt584 +
the record/inventory persistence) — it makes every finalize above (L0006/L3cd4/
L455c) actually matter. The TRAIN screen is the runtime-reachable alternative
(replaces cg_train_screen). Both are their own multi-function efforts.

## Save tail — LIFTED 2026-06-15

jt584 (save UI) + jt578 (serializer) + L0ce0 (record byte-swap) replace the
jt584 stub. jt578 writes the 398-byte record + inventory chain (rec[8], 18B/item
from +40, container sub-items at +58 when type +40=='I') + spell chain (rec[4],
10B/node), byte-swapping each multi-byte field to the little-endian .cch format
(jt1180 words / jt1199 longs). All deps were already lifted (jt410 write, l00e0
opener, l005a precondition, jt419/jt431/jt488/jt159/jt98/jt91/jt176/l17e2).
Reachable via the "Save Characters" menu (l1060 -> jt584). The serializer is 1:1
faithful.

CAVEAT (port file layout): jt584's existence check (jt988/l17e2) tests the
nested `<design>/SAVE/<name>.cch` path, but l00e0 opens the basename via the Mac
shim (flat gamedata dir). In the port's flat layout the overwrite prompt may not
find an existing save, so it can re-save without confirming — the write itself
is correct. A full fix is the SAVE-subfolder staging (task #106 territory).

## Read-path wiring + the record-layout impedance (2026-06-15)

l_cch_read (the flat-shim read-opener, mirror of l00e0) makes jt577 reachable,
and a probe-gated boot self-test exercises the full jt578->jt577 round-trip
(`make ENGINE_PROBE=1`; look for "cch round-trip self-test: PASS" in the log).

A menu-level load (Add Character -> a .cch file -> the roster) is intentionally
NOT wired, because **jt577/jt578 use the faithful 398-byte record layout while
the port roster/play reads port-local offsets** (CHAR_RACE=200, CHAR_INPARTY=210,
CHAR_STATS=203, ... see the CHAR_* macros above l02dc). Loading a faithful .cch
into a cg_pool slot would put the faithful fields where the roster expects its
own, and setting CHAR_INPARTY=210 would clobber a faithful field. So a real
in-game load round-trip is blocked on unifying the two record models (a port-wide
migration, task #106 territory), not on the serializer — which is byte-faithful
both directions and self-test-verified.

## Progress
- 2026-06-15: mapped the full segment (this file). Lifted jt558 (multi-class
  finder) + whitelisted jt561 (empty).
- 2026-06-15: lifted L0006 (l0006_c17) the body-icon finalize — faithful
  gender × combination-type × class × alignment(%3) → rec[188] dispatch, wired
  into jt574's Done branch at the Mac call position (after L29ae). 48 distinct
  body-shape ids.
- 2026-06-15: lifted L238e (l238e_c17) the character-name entry — faithful
  jt98 prompt + jt130 basename + empty/leading-digit validation loop. Left
  un-wired: blocked on jt1078 (the modal line editor stub). NEXT = jt1078 to
  make name entry functional, then wire l238e_c17 live; afterwards the L1346
  review screen + L3cd4/L455c/jt584 save tail; then the TRAIN screen (B).
- 2026-06-15: lifted jt1078 (modal line editor) + L0334 (text-draw) and WIRED
  l238e_c17 live into jt574. Name entry now functions end-to-end (needs Hatari
  visual verify of the box in the char-gen context).
- 2026-06-15: lifted L3cd4 (l3cd4_c17) the proficiency-bitfield finalize, wired
  into jt574 after L0006. The CREATE finalize chain (HP/name/icon/proficiency)
  is now faithful end-to-end. Remaining pieces each open another subsystem
  (DLItem grid / CODE 19 items / CODE 15 save) — see the status table above.
- 2026-06-15: lifted jt557 (TRAIN) — faithful full lift (~2.7KB asm). Money
  gate (rec[76] word vs g_a5_-18480), the per-race AD&D level-limit switch
  (rec[88] cases 0-4 keyed to STR rec[113] / INT rec[115]), the two faithful
  "best class" passes (one loop-carried-level, one per-slot-reload — replicated
  verbatim), XP clamp to rec[68], guild-mask gate (g_a5_-28006[48]), the
  "will become" preview over the live g_a5_-14636 class-name table + jt159
  confirm, then level bump / jt910 recompute / jt595 proficiency grant
  (rec[339..] via g_a5_-18893) / jt29-gated spell learning (jt41+jt876) /
  jt885 HP gain / jt33 rec[197]. The old stub was wrong (called jt574 char-gen
  + "Create Character" comment); its caller l0f60 (jt918 case 3) is likewise
  mislabeled "Create Character" — case 3 dispatches TRAIN. cg-audit PASS.
  REMAINING CODE 17: jt573 review screen (DLItem grid), L455c equipment,
  jt556/jt560 train-related stubs.
- 2026-06-15: lifted the jt573/L1346 REVIEW screen cluster — jt573 driver,
  L11ac per-frame builder (the three jt452 DLItem streams + dialog
  run/teardown), and the four action procs jt562 (mouse) / jt563 (keyboard) /
  jt564 (Exit) / jt565 (Done), plus L079a (marker frame) / L09ba (flush). The
  7×7 body-icon grid index model (-6986 index / -6984 row / -6982 col, ×7) is
  faithful; jt406's copy(SRC,dst) Mac ABI was written SWAPPED for the C
  jt406(dst,src) shim. DEFERRED: L09dc (~1.5KB) the grid-cell PAINT leaf
  (PROBE stub). jt573 is `unused` until the create-flow tail wires it.
  cg-audit PASS.
