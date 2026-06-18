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
  if new: L1346 (=jt573) REVIEW/MODIFY screen     DONE + WIRED into jt574 after
                                                          L0006 (ctx==0 gate; Exit
                                                          aborts the create). Grid
                                                          icons await jt110 loader.
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

Port `jt574` runs the faithful pick screen (`l3666`) then the faithful finalize
chain — `l238e_c17` (name) → `l0006_c17` (body icon) → **`jt573(0)` REVIEW screen
(WIRED)** → `l3cd4_c17` (proficiency) — followed by the port-side
`cg_build_record` overlay (faithful fields copied onto the new pool record).
Remaining to fully faithfulize the tail: replace `cg_build_record` with the Mac
"Save %s?" `jt159` confirm + `L455c` equipment grant + `jt584` `.CHR` save (and
the port-side roster overlay retired once the single record layout is live).

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

L09dc (0x09dc, ~1.5KB) the screen PAINT is now LIFTED: the 49-cell DUNGCOM1
body-icon grid (current icon marked, jt57 kind 31 vs 30) + the info panel (name /
"(NPC)" on rec[147] bit 7 / gender g_a5_-14500 / race g_a5_-14564 / class
g_a5_-14636, multi-class arms splitting the name across rows 12..14) + the jt1089
pen move.  The DUNGCOM1 load (jt110, CODE 6+0x33ac GLIB slot loader) stays a LEAF
STUB — the RM/GLIB art-load subsystem (task #127), so the grid ICONS do not draw
yet; the render logic around it is faithful.  jt384 needs Pascal-string sources,
so the multi-class component names go through ua_strs_at() (a C literal would be
misread as length-prefixed).

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
- 2026-06-15: lifted L09dc — the review-screen PAINT (49-cell DUNGCOM1 grid +
  name/NPC/gender/race/class info panel, multi-class names split across rows
  12..14). Only jt110 (the DUNGCOM1 GLIB slot loader, CODE 6+0x33ac) stays a
  leaf stub (RM/GLIB subsystem, #127) so the grid ICONS don't draw yet; all
  the render logic is faithful. jt384 sources must be Pascal strings → the
  multi-class component names go through ua_strs_at(). cg-audit PASS. The
  jt573 review-screen cluster is now fully lifted except the jt110 art leaf.
- 2026-06-15: WIRED jt573 into jt574's create flow at the faithful Mac
  position (L3b5e: after L0006, gated on ctx==0 = new character; Exit aborts
  the whole create before the roster add). The create finalize chain is now
  l3666 pick -> l238e name -> l0006 body icon -> jt573 review -> l3cd4
  proficiency -> port cg_build_record overlay. Safe for cg-audit: the headless
  probe never completes l3666's modal (no input), so the finalize tail/jt573
  are unreached there; the boot self-tests print PASS earlier. Interactive
  Hatari verify still pending (the review screen runs, grid icons blank until
  the jt110 DUNGCOM1 loader is lifted).
- 2026-06-17: BODY-ICON GRID RENDERS — lifted l3b1e (CODE 6+0x3b1e), the GLIB
  tile compositor (af913ab). The earlier "jt110 leaf stub blocks the grid"
  diagnosis was WRONG twice over: (1) JT[110] IS l33ac (same CODE 6+0x33ac
  address — the binder was never a stub; jt110 just forwards to it); (2) the
  art the grid blits is NOT a directly-bound group. The real chain: l4d98
  stands up -27866 as a writable 'TILE' "activ" GLIB list block via
  l36e0(&-27866, 81); the grid loop calls jt593 -> jt56("CBODYS", rec[188],
  rec[189]) which COMPOSES each CBODY body shape INTO -27866 (item rec[189]=8)
  through l3b1e, and jt57 blits the registry tiles (frame items 30/31, body
  item 8) per cell via jt1001(*-27866, item). l3b1e was a complete PROBE stub,
  so nothing landed in -27866 -> empty panes. Faithful l3b1e: a==0 clears the
  dst item (jt1022 size 0); else src item = (flag) ? jt1020(src,b) : b, size =
  jt1015(src,item), grow the dst item (jt1022; always id<76 else only when
  smaller), then jt406-copy the piece. Deps jt468/jt1012/jt1015/jt1020/jt1022/
  jt406 were all already lifted. A mid-investigation wrong turn (binding
  DUNGCOM into -27866 to "publish the handle") clobbered the l36e0 registry
  with a read-only FC-pool group, making jt1022's resize pop an l036a modal
  (SysBeep+hang) — backed out; -27866 stays the registry, DUNGCOM1 binds into
  a temp slot only for its palette (jt124) then frees (jt115). Verified in the
  new FRUA_BODY harness (cg_body_repro): all 49 sprites tile the left pane,
  READY/ACTION poses render. The combat art-overlay tier (jt55/l3b1e) is now
  unblocked for #115 too. FOLLOW-UP: grid-cell background is the combat-sprite
  palette (purple) vs the grey menu CLUT — the frame/sprite CLUT-sync the user
  flagged (shared-palette model, [[glib-palette-subsystem]]).
- 2026-06-17: ICON-GRID PALETTE — analysed, literal fix REVERTED, deferred to
  task #137 (shared-palette colour-range remap). The cell ground + body sprites
  should use the COMBAT palette; the frame must keep the menu/stone CLUT.
  GROUND TRUTH (BasiliskII mon at the body screen): CLUT[87] = (151,167,179)
  silver. That palette is DUNGCOM's NESTED set-1 GLIB (outer item 1 -> its item
  0), 224 entries, header start=32; entry 55 -> CLUT[87]. The Mac SWAPS that
  set's palette in like the dungeon 3D view swaps a wall set's. A literal
  jt993(set1, 0) commit (start=32 over clut 32..255) paints the cells silver but
  STOMPS the menu stone backdrop (shared 32..255) -> psychedelic frame in
  `make run-game` (committed 49f04ff, reverted acf85ca). Faithful path = the
  GLIB colour-range allocator (jt1069) REMAP into free slots, not a literal
  commit. jt124(handle) must stay (dropping it blacks the backdrop). The port's
  l33ac("DUNGCOM1") loads the OUTER DUNGCOM.CTL (item 0 type 2, not a palette),
  so jt124 can't reach the nested palette; the Mac's FC group for "DUNGCOM1" IS
  the nested set. Other open items (in #137): body shapes 24/27/34/36/39 render
  shape-but-no-colour; the selected-cell index-8 marker lands on a random
  left-pane cell; the 49-cell compose draws slowly.
- 2026-06-17: ICON-GRID PALETTE SOLVED (4c4c4ce) — TWO-RANGE CLUT. clut 0..31 =
  menu/FRAME.CTL UI (stone frame, buttons, text); clut 32.. = DUNGCOM combat
  palette (set-1 item 0, header start=32) installed LITERALLY via qd_set_palette,
  clamped off 0..31. The psychedelic frame was jt124/l3eea reading the OUTER
  DUNGCOM.CTL's nested item 1 as a colour table (stub header start=0/count=256)
  and garbaging 0..255 — dropped jt124 here. Stone frame + correct sprites now
  render together (matches the Mac; verified vs a BasiliskII capture). The
  user's model held: 0..31 reserved for UI, each tileset's item 0 = its palette.
  REMAINING in #137: body shapes 24/27/34/36/39 silhouette (CBODY shape-vs-colour
  compose gap, NOT palette); selected-cell marker on the wrong cell; slow compose.
