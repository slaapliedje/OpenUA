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
  if new: L1346 (=jt573) REVIEW/MODIFY screen     TODO  (~1435 lines, linkw-796;
                                                          drives the align grid)
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

### Alignment-grid sub-cluster (reached only from L1346/jt573)
`L11ac` builds a 7×7 alignment grid screen with these action procs — lift them
together with jt573 (they are untestable until jt573 has a runtime path):
- jt562 (0x0854) grid mouse-click — maps click→grid index -6986, redraws marker
- jt563 (0x0fc8) grid keyboard — arrow keys step the -6986 index (JT[3] 130..136)
- jt564 (0x09ce) grid "Exit"  — sets -7038=1, -6988=1
- jt565 (0x09c6) grid "Done"  — sets -6988=1

## B. TRAIN flow (Training Hall, CODE 12 callers)

- jt557 (0x6cd2) **Train Character** — "we only train conscious people",
  "Training costs %s platinum", level-up/cost logic. ~600 lines. Port stand-in =
  `cg_train_screen`.  TODO
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
| jt584 .CHR save            | CODE 15 file-write (currently a stub) |
| jt557/556/560 TRAIN screen | level-up logic (likely CODE 19 too); has a live caller |

Recommendation: the highest-value next subsystem is the **save tail** (jt584 +
the record/inventory persistence) — it makes every finalize above (L0006/L3cd4/
L455c) actually matter. The TRAIN screen is the runtime-reachable alternative
(replaces cg_train_screen). Both are their own multi-function efforts.

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
