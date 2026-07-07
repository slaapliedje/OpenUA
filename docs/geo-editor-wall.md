# GEO / design-editor cluster — worklist (ADR-0008 phase-2)

Opening the last big cluster: the **design editor** (jt315 main menu →
EDIT MODULES). ADR-0008 defers the tools behind the runtime; the runtime
is ~96% done, so this is the sanctioned phase-2 work. Scope is unchanged
— the full *Unlimited Adventures* package is the goal.

## The two hard truths about this cluster

1. **It is 100% mouse-gated.** Every entry is reached through the editor
   UI (EDIT MODULES → tool clicks). Hatari does not inject mouse buttons
   (see the run-falcon-port skill), so **nothing here is validatable
   headless.** Correctness rests on faithful transcription cross-checked
   against the disasm + the already-lifted dep tree — the smoke harness
   only proves boot-unregressed (these are all `__attribute__((unused))`
   dormant lifts until the editor UI is wired, itself a large ADR-0006
   Dialog/Control/TextEdit effort).
2. **It is deeply entangled.** Every JT entry pulls in CODE-local
   painter/helper `lXXXX` deps that must be lifted first. There are no
   quick leaf wins — each entry is a mini-project.

Consequence: this is a **multi-session** effort. Bank one coherent
sub-chain per session; do not try to boil it in one pass.

## The open entries, by segment and size

> **Status 2026-07-06:** most of this cluster is now DONE — CODE 22 painter
> trio, the entire CODE 2 event editor (jt246/254/253/248/249/258), and CODE 10
> (jt259 art import + jt266). **The remaining depth is CODE 11 (jt242/jt243)** —
> see the "CODE 11 — GEO 3D-map editor — Phase C SCOPE" section below, which is
> the authoritative worklist. The per-segment tables below are kept as the
> historical campaign record.

Instruction counts from `tools` survey (2026-07-04). "deps clean" = every
JT + lXXXX callee already lifted.

### CODE 22 — design-list rendering — ✅ COMPLETE
| JT | addr | ~insn | notes |
|---|---|---|---|
| ~~jt286~~ | l2aaa | 366 | **LIFTED** — entry painter kind 0 (jt278 case 0) |
| ~~jt282~~ | l2f24 | 283 | **LIFTED** — entry painter kind 1 (jt278 case 1) |
| ~~jt281~~ | l329c | ~120 | **LIFTED** — entry painter kind 2 (jt278 case 2) |
| ~~l347a~~ | l347a | ~200 | **LIFTED** — entry painter kind 3 (jt278 case 3); CODE-local, no JT id |

All four painters (l2aaa/l2f24/l329c/l347a), dispatched by **jt278**
(CODE 22+0x294e) on the entry kind byte, are lifted. **The design-list
paint path is complete** — the first coherent, self-contained deliverable of
the editor cluster. Everything is still dormant (mouse-gated) until the editor
UI (EDIT MODULES, ADR-0006 Dialog/Control/TextEdit) is wired.

### CODE 2 — the design-record editor ("recorder")
| JT | addr | ~insn | notes |
|---|---|---|---|
| ~~jt246~~ | l311a | ~40 | **LIFTED** (2026-07-05) — field-swatch column painter, 8-row loop. Self-contained, no holder/record. (the wall's "161 insn" was over-counted; it's 0x311a..0x31ca.) |
| jt258 | l0004 | ~260 | **NOT a painter** — the record-editor *constructor*: allocs into fp@(-8), stores fields, drives a **JT[1] switch** on fp@(8). Own pass (jt1_extract). |
| jt254 | l4c5a | 367 | **BLOCKED, own session.** 2× **JT[3] switch** (jt3_extract), 4× jt1076 modal, and its deps **jt207 (×12) + jt1076 (×4) are MISSING**, plus 4 unlifted CODE-2 locals (L4dcc/L4c92/L541c/L4e3e). Lift jt207 + jt1076 first. |
| jt253 | l44cc | 569 | JT[1] switch dispatcher (jt273/317/319/320/325) — own session |
| jt248 | l26aa | 773 | list editor (jt117/147/167/168/169) — own session |
| jt249 | l333a | 1025 | big dispatcher — own session |

**CODE 2 reality check (2026-07-05):** only **jt246** was a clean leaf painter —
now done. The rest are dispatchers/constructors/modals, not leaf renderers:
jt258 is the JT[1]-switch constructor; jt254 needs jt207 + jt1076 lifted first
(both MISSING) plus its own JT[3] tables and 4 locals. So the CODE 2 "small
painters" idea only yielded jt246; jt254 is properly a multi-lift subtree.
Better next leaf targets are the **CODE 10 small viewers** (jt264/265/270,
deps mostly lifted) — see below.

### CODE 10 — module viewers (editor-adjacent)
| JT | addr | ~insn | notes |
|---|---|---|---|
| ~~jt265~~ | l65be | 7 | **LIFTED** (2026-07-05) — "Experience:" entry prompt: jt98 field + new L654a atol. Clean leaf. |
| ~~jt264~~ | l6316 | ~130 | **LIFTED** (2026-07-05) — monster-editor art/id sub-state handler (jt263 sibling; 2× JT[3]). |
| ~~jt270~~ | l3262 | ~253 | **LIFTED** (2026-07-05) — monster/spell list-entry edit loop (l2d3e event loop, jt452/jt444 List Manager, jt196 name writeback). |
| ~~jt267~~ | l1a14 | ~129 | **LIFTED** (2026-07-05) — row art-label refresh + subtree (L1282/l15c2_c10/L116a). |
| ~~jt269~~ | l0004 | ~200 | **LIFTED** (2026-07-05) — state-machine entry (21-arm JT[3] + JT[1]/JT[3] flag-pack tail); L03ce/L300c lifted, L040c dialog = PROBE stub pending jt266. |
| ~~jt266~~ | l1bc2 | ~230 | **LIFTED** (2026-07-05) — full faithful lift; body was ~230 insn (the "1692" counted the whole helper cluster). All 17 helpers + 2 JT[3] switches. Unblocks jt269's L040c dialog. |
| jt259 | l368a | 2757 | the giant — own session |

**CODE 10 progress (2026-07-05): jt265 + jt264 + jt270 + jt267 lifted**, along
with the whole jt264/jt270 dependency arc (jt372/L62e0_c8/L611c save side,
L06ae/L2ebe row side) and jt267's subtree (L1282 List-Manager item setter,
l15c2_c10 row-label composer, L116a item-arm). **jt269 was mis-labelled "tiny"**
— it is the ~200-insn monster-editor state-machine entry: a 21-arm JT[3] switch
plus JT[1]/JT[3] tail switches that pack display flags into *outp. LIFTED
2026-07-05: the dispatch + flag packing are faithful, with L03ce (field reset)
and L300c (memorized-bit refresh) lifted; **L040c** — the 222-insn modal dialog
loop (jt168/jt169 List Manager + jt266 the giant + L2a06/L19ea/L0960/L1162/L1396)
— is the single PROBE-stub arm, deferred until jt266's session. jt358 turned out
already lifted (a one-liner), jt208 confirmed void.

Remaining CODE 10: **jt259** (the art-import giant) — jt266 is now LIFTED.
Every other CODE 10 viewer (jt264/jt265/jt267/jt269/jt270) is lifted.

### jt259 (L368a → L36e0) — roadmap (STARTED 2026-07-05)

jt259 (L368a) is a thin ~15-insn wrapper: it clears the top bit of *fp@(10),
splits *fp@(10) into (low byte, byte 1), calls **L36e0(lo, hi)**, and packs the
byte result back into the low word of *fp@(10); returns fp@(8) unchanged. The
real giant is **L36e0** (`linkw #-1306` — a 1306-byte frame): the **MacPaint /
PICT art-IMPORT pipeline** (0x36e0..0x42f2, ~1030 insn main) plus **13
CODE-local helpers**. Total ~2732 insn across the subsystem — genuinely
multi-session (est. 6-10 focused passes).

### jt259 completion — FOCUSED sub-phases (one small chunk per commit)

DONE so far: all leaves (L4924/L4970/L4eda/L4f9c/L42f2/L49f8/L4cae/L509e full
lifts), L66a2/L6606/L6892, DrawPicture shim, jt259 wrapper, L36e0 + L53b0
level-2 skeletons, L53b0 cases 0/2/3/5/7/9. REMAINING, chunked in dependency
order — each is one build+commit:

STATUS 2026-07-05: jt259 art-import COMPLETE — fully lifted end-to-end with NO
remaining stubs or TODOs (pick → decode → per-family descriptor → L53b0 pack for
EVERY format → PICT preview → palette-record append → resource store → .tlb
write). Three "needs a trace / blocked" flags all turned out resolvable from the
disassembly alone: A11 (exact CODE_10.s pushes; jt1163 0-stub, jt1170 empty),
A12 (jt1159 = l4350 already lifted, the HAL-moot Palette-Manager no-op), and the
0x408a append (fp@(-40) is provably 0 — cleared at L3950, never re-set, never
address-taken). jt259 is the first design-editor giant done; on to Phase B.

- **A4** ✓ — l36e0_c10 tile-loop CORE (L3dd0..L407e minus the geometry tables):
  the 120-dim 3-strip vs single-tile dispatch, the L53b0 calls, and the
  jt1022/jt1004/jt468/jt1012/jt406 tile store. Reads geometry from the
  descriptor locals (arms still TODO). Declare the full local set here.
- **A5** ✓ — the geometry-ADVANCE tables (L3faa..L4070): the next-strip tile
  dimensions per art-type/view-mode (the fp@(-16/-14/-12/-6) constant sets).
- **A6** ✓ — JT[1] arm BIGP (case 16/32, 0x3cea) + the container-open (L3d72)
  + the shared-locals refactor.
- **A7** ✓ — JT[1] arms PIC (case 1, 0x398c) + SPRI (case 2, 0x3abc).
- **A8** ✓ — JT[1] arm CPIC (case 4/8, 0x3bf6) incl. the jt182 "Shape:" dialog
  (Normal/Tall/Wide/Big/Cancel → shape bits drive the geometry).
- **A9** ✓ — palette/art-table setup (L37ee..0x3920): jt399 + jt468/jt1012/
  jt406 CLUT copy + DungCom/MENU table load into clutbuf.
- **A10** ✓ — writeout (L413c..0x4196): jt431 x2 dir-prefix + jt392 write +
  jt411 close + jt457 register + jt461 release.
- **A11** ✓ — L53b0 case 1 (0x5698..0x59ba): size-guard, per-plane unpack
  (hi-res col loop / even-width row loop via jt1170/jt1177/jt1197), the two
  l42f2 convert paths (desc[15] gate + jt1200()==3 gate else single), and the
  l4924 pack. Buffer offsets transcribed from the pea/movel pushes — no trace
  needed once jt1163 (0-stub) and jt1170 (empty) were confirmed.
- **A12** ✓ — L67a0 (PICT preview, 0x67a0..0x6890): SetCursor(watch), jt1086,
  picFrame rect from *pic+2 (offset to origin), jt1196, ForeColor(black)/
  BackColor(white)/PenMode(srcCopy), DrawPicture, jt1168, then the two jt1159
  Palette-Manager re-arbitration calls via l4350. GetGWorld/SetGWorld
  (0xaa28/0xaa39) collapse to no-ops on the single shared surface; jt1159=l4350
  was already lifted as the HAL-moot no-op. Needed a forward proto for jt1086
  (defined later in the file).

Counter-reuse note: `fp@(-34)` is BOTH the outer strip counter (L407e dec) and
the inner 3-strip counter (L3df8 0..2, then set to -1) — the 120-dim path runs
the outer loop exactly once. Constants are magic; when in doubt, trace.

- **51 distinct JT deps — all resolve** (jt48=l5864, jt1084=l036a, jt1162=l3e38
  are the only alias reads). Heavy on file/resource I/O (jt418 std-file,
  jt460/461/464/465/468 resource, jt411/413/416/419/431 file, jt1162/1163
  double-buffer, jt1147/1148/1153 compose, jt1170/1171/1177 unpack, jt1066/1069
  CLUT, jt1197/1198 mode).
- **3 JT[3] switches, 2 JT[1] switches** — decode with jt3_extract/jt1_extract.
- Main dispatch: jt1200()==3 → MacPaint import ('PNTG', "MacPaint File:");
  else PICT import ('PICT', "PICT File:"). Both go std-file (jt418) → import.

**13 CODE-local helpers (by size, none lifted yet):**
```
L4924  ✓  ~25  AND-NOT sprite-mask blit (dep jt1163)          LIFTED 2026-07-05
L4970  ✓  ~45  uniform-column run-length (pure)               LIFTED 2026-07-05
L6606  ✓  ~52  PICT scanline unpack+store (jt468/1170/1171/1177/1179/1202/
               1166/413) — jt1170 CONFIRMED no-op in the Mac too (link/unlk/rts),
               so the port's empty void stub is faithful; jt1171 is a direct
               port fn (returns advanced cursor), NOT an alias.  LIFTED 07-05
L4eda  ✓  ~64  PackBits (RLE) compressor, pure                 LIFTED 2026-07-05
L4f9c  ✓  ~86  tile-band pack driver (l4eda + jt1004/1197/406/1177/1170; only
               1-plane real, planes>1 = Mac logged-stub)       LIFTED 2026-07-05
L66a2  ✓  ~84  PICT file -> handle load (jt403/412/1030/1026/1033/414/1032 +
               "Insufficent Memory" alert)                    LIFTED 2026-07-05
L6892  ✓  ~6   dispose a PICT handle (jt1032)                 LIFTED 2026-07-05
◑ L67a0  ~80  PICT DISPLAY — mostly UNBLOCKED (2026-07-05). The big blocker
               is gone: **DrawPicture is now implemented** in the QuickDraw
               shim (compat/quickdraw.c — a v1/v2 PICT opcode interpreter:
               BitsRect/PackBitsRect/DirectBitsRect, 1/2/4/8-bit indexed +
               16/32-bit direct, clip, comments; PackBits validated vs Apple's
               canonical vector), and **ForeColor/BackColor** are added.
               REMAINING for L67a0: _GetGWorld/_SetGWorld (0xaa28/0xaa39) — map
               to the single-surface current-port (no real GWorld in the port),
               _SetCursor/GetCursor/PenMode (present), and **jt1159** (CODE 4,
               still unlifted). Do L67a0 when L36e0 sets up the target buffer.
L4cae ~185   L49f8 ~231   L509e  ~262   (mid helpers)
L42f2 ✓ ~468  imported-art pixel-format converter — FULL faithful lift
              (2026-07-05): mode-3 silhouette-mask builder + the depth/colour-
              key converter (2 JT[3] switches: 1/4/2bpp reduce + 8/4/2/1bpp
              key-match; jt1153/1177/1170/1197 scanline read, jt1163 dither
              gate, jt406 blit). 330-byte sl[] frame + separate scalar locals.
              Resolved the "fp@(-5)" scare (= low byte of the width local ->
              round-to-even). ⚠ dormant/unvalidatable headless — verify the
              pixel output vs a Mac trace of L42f2 when the import is wired.
L53b0 ◑ ~513  the tile-conversion ORCHESTRATOR — LEVEL-2 SKELETON (2026-07-05).
              Faithful prologue (mask pick, header copy, format-nibble set +
              5->1/7->3/3->7 remaps by view-mode+dither, depth scale, flag) +
              the JT[3](0..9) format dispatch. Cases 2->l4f9c, 3->l4cae,
              7->l509e, 9 (record fill) done FULLY; 4/6/default->jt1147; 0/5
              (inline scanline / l4924) and 1 (per-plane l42f2 + l4924) DEFERRED
              with TODO + opcode ranges (0x5534.. / 0x5698..). Returns the
              packed size; jt1130 commit.
```

**L53b0 sub-tree status (2026-07-05):** ALL LEAVES DONE — L4924 ✓ L4970 ✓
L4eda ✓ L4f9c ✓ L42f2 ✓ **L49f8 ✓** (multi-plane RLE encoder: zero-skip / 0x40
literal / 0x80 uniform / 0xC0 marker, plane pitch 3072) **L4cae ✓** (multi-
plane tile-strip orchestrator: l42f2 convert -> l49f8 encode per row, overflow
guard, header + plane compact) **L509e ✓** (single-plane byte-RLE writer:
-count transparent / +count literal). Only **L53b0** itself remains (the
orchestrator that ties L42f2/L49f8/L4cae/L509e together; still a PROBE stub in
l36e0_c10). Once L53b0 is lifted, l36e0_c10's tile-conversion loop can be
filled.

⚠ **The whole converter/RLE family (L42f2, L49f8, L4cae, L509e) is bit-exact,
dormant, and UNVALIDATABLE headless** — the packing (control-byte bits, plane
pitches, peek/backup RLE cursors) can only be confirmed against a **Mac trace
of these functions** on a real import. Do that validation pass before trusting
the import output. See [[mac-blit-ground-truth]] for the trace method.

Also lifted this pass: **jt1032** (_DisposHandle) + **jt1033** (_HLock) — trivial
Memory Manager trap wrappers over the shim, previously absent; needed by L66a2.

**L36e0 main = LEVEL-2 SKELETON DONE (2026-07-05)** — lifted as `l36e0_c10`
(a (CODE,offset) clash with another segment's l36e0), driven by the **jt259**
wrapper (L368a: clears the desc top bit, splits id/arttype, calls the importer,
packs the byte result back). jt259 is now OUT of MISSING (scoreboard done).

The skeleton faithfully captures the top-level spine: std-file open (MacPaint
'PNTG' / PICT 'PICT' via jt418) → the "got a file" gate → decode/display
(mode-3 l6606 MacPaint scanlines; else the CLUT-setup + l67a0 DrawPicture +
l6892 dispose block) → the JT[1] art-family switch → the tile-conversion loop
(l53b0 per strip + store) → the .tlb write-out. Every top-level helper/JT is
called in order.

**Still to FILL (TODO markers in l36e0_c10, with opcode ranges):**
- the PICT palette/DungCom/MENU table setup (0x37ee..0x3920)
- the JT[1] filename-format arms (0x398c..0x3d72; parallels lifted l419e)
- the tile-loop store arithmetic + outer bounds (0x3d72..0x407e)
- the .tlb write-out tail (0x413c..0x4196)
- **L53b0** (tile-converter sub-giant → L42f2/L4924/L4970/L49f8/L4cae/L4eda/
  L4f9c/L509e) — still a PROBE stub; the biggest remaining piece.
- **L67a0** (DrawPicture display) — DrawPicture now EXISTS in the shim; needs
  the GWorld→current-port mapping + jt1159. Still a PROBE stub.

So jt259 "counts done" but the import is NOT functional yet — it's the spine.
Remaining order: L53b0 sub-tree (L42f2 first) → fill l36e0_c10's arms → L67a0
GWorld glue + jt1159 → validate a real import (needs a human at the mouse).
This is dormant/mouse-gated — verify against the disasm, not the smoke harness.

### jt266 (l1bc2) — roadmap (COMPLETE 2026-07-05)

### jt266 (l1bc2) — roadmap (STARTED 2026-07-05)

The monster-editor viewer/dialog: ~1692 insn (CODE_10.s lines 2342–4239), 68
distinct JT deps (**all resolve** — the 5 reading MISSING, jt43/47/48/105/106,
are aliases l579e/l541a/l5864/l3f3c/l3880 with bodies present), 6 switch sites,
and **17 CODE-local helpers**. Already-lifted helpers it reuses: L06ae, L116a,
L15c2(=l15c2_c10), L2ebe, L6028, L611c. The 17 to lift, by tier:

```
Tier 0 (only JT deps — all present):
  L3244 ✓  (l3244, printable-glyph test)        L2f8e ✓  (l2f8e_c10, -12300 hdr init)
  L205a ✓  (pic-slot paint; jt1161/45/47/46)     L24a4 ✓  (backdrop-slot; jt1161/43/106/124)
  L1f86 ✓  (portrait frame; jt80/1173/1200/1161/1001)  L26de ✓ (icon strip; jt209/1200/1161/357)
  L419e ✓  (.tlb purge; JT[1] 1/2/4/8/16/32 → PIC/SPRI/CPIC/BIGP + jt45/48/419/465/431/416)
  L6238 ✓  (MONST%03d.dat delete + name copy; jt394/436/416/l6028/384)
  L2660 ✓  (5x13 grid blit via JT[118]/l37d6_c6)  L2282 ✓ (pic-slot repaint; JT[114]/l3804 or l3880)
  L23c6 ✓  (4x4 DungCom preview via JT[118]; jt54/jt120/jt58)
Tier 1:  L2618 ✓  L263c ✓ (→L2660)  L22f0 ✓ (→L23c6)  L20cc ✓  L27c2 ✓
Tier 2:  L24fa ✓ (→L263c,L2618)

ALL 17 HELPERS DONE (2026-07-05). L20cc (3D sprite preview; jt199 page = the
render_3d_faithful static back-buffer, unused in colour mode — port ABI
artifact, not a stand-in), L27c2 (spell-caption column: jt394 "%s %c" + jt423/
jt448/jt1135/jt1089 text layout), L24fa (Wild/Dung combat-set preview grid).
jt266 main can now be a FULL lift (all helpers present).

Tier-1 wrappers DONE (2026-07-05): L22f0 (2-column CPIC preview: l23c6 + jt56/
jt53/jt57/jt55), L2618/L263c (thin l2660 grid wrappers, sources -12050/-11984).

**DEFERRED — L20cc** (the monster/sprite 3D-preview, ~113 insn, dual loop
mode-3 vs not). Fully decoded: for i=0..2 it calls jt353 + **jt199** + l2282,
stepping raw B by step (48/40) and C by step>>2. The blocker is the PORT jt199:
Mac jt199(B,A,row,col,facing) takes NO surface, but the port refactored it to
`jt199(unsigned char *page, Y, X, row, col, facing)` — the sole live caller
(render_3d_faithful @11657, which the comment confirms is THIS CODE 10 @0x2178
site) supplies a `static unsigned char page[BP_STRIDE*BP_ROWS]` "unused in
colour mode". L20cc must supply the same. Map: port(page, Y=B, X=A,
row=(signed char)-12288, col=(signed char)-12287, facing=(unsigned char)-12286).
Do this in the jt266-main session where the compose surface is in hand — don't
fabricate a second 30KB buffer in isolation.

**DEFERRED — L27c2** (~113, spans 0x27c2–0x29f6+): the real viewer body glue
(L06ae/L3244/L1f86 + jt353 + per-arm logic), not a leaf. Belongs with jt266
main.

Tier-0 leaves ALL DONE (2026-07-05). L419e's JT[1] decoded with
tools/jt1_extract.py --jsr-at 0x41a8 (never by hand): 6 sparse cases
1/2/4/8/16/32, arms build the four .tlb name templates; the PIC letter A..F
comes from the id band (<76 A, <138 B, <164 C, <193 D, <227 E, else F).
```

**GOTCHA — jt118/jt114 blit sig — RESOLVED (2026-07-05).** L2660/L23c6 blit
through JT[118], L2282 through JT[114]. The GLOBAL port `jt118(page,top,left,
idx,handle)` has a divergent, **caller-less** ABI (phantom `page`; takes an
already-resolved handle, skipping the jt468 group-id resolution). The real Mac
jt118 (CODE 6 L37d6) = `jt108(1)` + `jt1001(fp@10, fp@8, *(short*)handleptr,
idx)` — it **derefs** the handle pointer to a group id and lets jt1001/jt468
resolve it. Resolution: the port's existing **l3804** already IS the faithful
Mac JT[114] body (`jt1001(c2,c1,*ptr,frame)`, args in Mac push order v@8/h@10/
idx@12/pad@14/ptr@16). So I added **l37d6_c6** = `jt108(1) + l3804(...)` (the
`_c6` suffix because the port's l37d6 is a different segment's combat-ring
helper). The CODE 10 grid painters route through l37d6_c6 / l3804 — the global
jt118 is left untouched (no callers) and jt114 is NOT modified (dungeon render
depends on it). Extra call-site padding words (L23c6's jt54 push, L2660/L23c6's
pad@14) are ignored exactly as the Mac ignores them. Closes the standing
[jt118/jt114 signature mismatch] latent issue for this call path.

**Naming:** L2f8e clashed with another segment's l2f8e → suffixed `l2f8e_c10`
(the (CODE,offset) recurrence hazard). Check each remaining helper the same way
before naming.

**Recommended order:** finish the tier-0 non-blit leaves (L205a/L24a4/L1f86/
L26de/L6238/L419e) → resolve the jt118 sig once, then L2660/L2282/L23c6 → tier 1
→ tier 2 → jt266 main as a **level-2 skeleton** first (1692 insn; mirror the CFG,
call every helper in order, defer per-arm detail), then fill arms. jt266 also
completes jt269's L040c dialog stub.

**CODE 10 reality check — CORRECTED (2026-07-05).** The earlier "jt264/jt270
bottom out on a ~900-insn subtree with jt1084 + jt456 MISSING" was WRONG: I
trusted `defs=0` without alias-checking. Re-checking the alias doc /
`ALIAS_LIFTED` map showed **jt1084 was already lifted (l036a)** and **jt456
already lifted (l2d3e)** — both counted done, never missing. The real
remaining chain was small and is now **fully lifted this session**:

```
jt264 ✓ → L611c ✓ (monster save, CODE 10)
             ├─ jt372 ✓ (CODE 8, was MISSING) → l6520_c8✓ jt363✓ + L62e0_c8 ✓
             │        L62e0_c8 ✓ (CODE 8, the full sibling of the l6432 stub)
             │          → l60b0✓ jt370✓ jt191✓  (all already lifted)
             └─ jt1084 = l036a ✓  (ALREADY lifted — the Mac's variadic "Error: %r"
                      maps onto l036a(const char*, ...) + vsnprintf; l611c calls it)
jt270 (unblocked) → l06ae ✓ + l2ebe ✓ (CODE 10, this session) + jt456 = l2d3e ✓
             + L611c ✓ + ~20 already-lifted JTs
```

**Lesson (again):** alias-check EVERY `defs=0` against `docs/lxxxx-jt-aliases.md`
AND `tools/jt_progress.py`'s `ALIAS_LIFTED` before calling anything "missing" —
a lXXXX-named lift (l036a, l2d3e, l60b0, l6520_c8) reads as MISSING under its
jtNNNN name but IS the body.

**jt270 remaining** — the only unlifted piece is jt270 itself: a ~253-insn
modal list-picker event loop (the monster/spell chooser). Its own level-3 pass,
like sibling jt263 (lifted as a level-2 skeleton). Dispatcher conventions for
the transcription are settled: `jt444(item,a,b,c)` — pad unused trailing args
with 0; `jt452(long shape0, …)` variadic-longs; `jt456` → **l2d3e()** (no-arg
event poll); list callbacks pass **&jt268 / &jt327** function pointers; the two
title strings format via jt394 "%s %s" then draw with jt1089.

### CODE 11 — GEO 3D-map editor — Phase C SCOPE (2026-07-06)

The last big depth block. **CODE 11 = the GEO (3D area map) editor.** It exports
jt233–244; every export is already lifted EXCEPT the two giants **jt242** and
**jt243**, which are currently PROBE stubs. Both are wired: the CODE 22 command
dispatcher `l0096` routes **cmd 2 → jt243** and **cmd 20 → jt242** (see
`event-editor-wall.md`). Dormant/DCE'd until jt315's selection dispatch is wired.

**Segment layout (address order of the JT exports):**
`l0004=jt244 · l027e=jt233 · l0b24=jt234 · l0b26=jt243 · l4846=jt239 ·
l4ed2=jt235 · l4ffe=jt240 · l5236=jt237 · l5514=jt241 · l5868=jt236 ·
l589a=jt242 · l67d0=jt238`. So jt243 owns **0x0b26–0x4846** and jt242 owns
**0x589a–0x67d0** (the intervening exports jt239/235/240/237/241/236 are all
lifted — the area-map handlers).

#### jt242 (l589a, 0x589a–0x67d0, ~1298 insn) — the CELL-EDIT committer

**Structure: 1 dispatcher + 6 private CODE-local helpers.** The linkw scan of
jt242's 0x589a..0x67d0 range shows 9 functions, but **l6256 and l63c0 are NOT
jt242 helpers** — they are the shared dungeon-walk source-registration (l6256,
called by jt241) and the exploration poll-loop (l63c0), both already lifted for
the play path. jt242's private tree (by its actual call graph) is:
`5a06` · `5dc8` · `5ee2` · `6136` · `61c6` · `5b0e` (which calls 5dc8).
jt242's main dispatch is **JT[3] @0x58ce (min=0, max=2, default)** — a 3-way +
default cell-edit sub-command; 5 JT[3] switches total (0x58ce/5986/5a32/5de6/
66d2).

**C2 COMPLETE (2026-07-06) — jt242 fully lifted.** Helpers: l5a06 (tally),
l5dc8 (preview-paint), l5ee2 (bulk wall-replace), l6136 + l61c6_c11 (cell-code
A/B replace), l5b0e (the modal confirm dialog — 8 STRS strings, jt456/l2d3e poll
loop). jt242 proper (l589a): read old value per kind (JT[3] @0x58ce), tally +
confirm (l5a06/l5b0e), then apply the bulk edit per kind (JT[3] @0x5986 →
l5ee2/l6136/l61c6_c11) and mark desc[3] bit 0 dirty. ABI: jt242(cmd, desc=long*
result/flags, area=the cell record). jt_progress: jt242 no longer a stub
(1175 done / 16 stub). **NEXT: C5+ = jt243** (l0b26, the 5216-insn / ~40-fn GEO
editor MAIN dispatcher — the last big depth block, multi-session).

**Dependencies: ALL LIFTED.** Its JT vocabulary is the shared painter/text
library (jt76/108/112/117/148/179/272/273/277/280–288/293/298/300/303/306/
311/312/384/394/423/447/449/451/452/456/1067/1080/1089/1113/1139/1160/1161/
1173/1193). The one that greps as "MISSING", **jt456, is lifted as `l2d3e`**
(both-directions alias — a5@0x0e62=3682; the jt258-campaign trap). Its two
backward calls into jt243's range (0x4226/0x4268) are **already lifted**
(`l4226`/`l4268`). So jt242 has NO missing deps — the work is purely lifting
its own 8 local helpers then the dispatcher body.

**C2 attack:** lift the 8 helpers bottom-up (leaves 5a06/5dc8/6256 first — small
linkw frames; then the painters 5b0e/5ee2/6136/61c6/63c0), then jt242 proper's
5-switch dispatcher. ~2–3 sessions.

#### jt243 (l0b26, 0x0b26–0x4846, ~5216 insn) — the GEO editor MAIN dispatcher

**This is NOT one function — it is ~40 functions** (40 linkw / 40 rts filling
0x0b26–0x4846): jt243 proper (the dispatcher, ~800 insn) + **39 CODE-local
helpers**. The subtree is dispatch-heavy: **22 JT[3] + 5 JT[1] switches**, 94
pc-relative local calls, 101 distinct JT calls.

jt243 proper: `jt243(short a8, long *rec_desc=fp@14, long *p14)` — NULL-guards
fp@(14) (→ return 0), stores it into a holder at fp@(-8), writes a8 into
rec->word@0, then **JT[3] @0xb48 (min=1, max=20, default=0x136e)** — a 20-arm
tool/command dispatch (the GEO editor's tool palette). Structurally a sibling of
the CODE 22 `l0096` and the CODE 2 `jt258` dispatchers.

**Helper entry addresses (39):** `1626 16ae 16f4 1822 1958 1a1c 1d10 1d88 237c
23de 2414 24b6 2836 28d4 2d40 2dbe 2e1c 2ea0 3236 3380 3654 36f6 37d8 37f6 3ab0
3d1a 3ddc 3e60 4144 4168 41a0 4226 4268 429c 43c2 4416 455c 476e 4810`. Two are
already lifted (`l4226`/`l4268`), so **37 helpers + the dispatcher remain**.

**C5+ attack:** bottom-up per the proven giant method (jt258/jt266) — enumerate
each helper's JT + local deps (alias-check every one against
`docs/lxxxx-jt-aliases.md` BOTH directions), lift leaves → mid → the 20-arm
dispatcher as a level-2 skeleton, then fill. Decode all 22 JT[3] + 5 JT[1]
tables with `jt3_extract`/`jt1_extract --jsr-at` (never hand-decode, #122).
Realistically **6–10 sessions**.

**C5 SCOPE (2026-07-06) — the 40-function subtree mapped.** Already lifted (6):
l4144 l4226 l4268 l429c l476e l4810. jt243 (l0b26) fans out to l16ae/l16f4/l1958/
l23de/l28d4/l36f6/l37d8/l4168 (+ lifted l4144/l476e). **l28d4 is the hub** (waits
on l1a1c/l24b6/l2836/l2d40/l2dbe/l3380/l43c2). **18 unlifted leaves** (lift these
first): l1626 l16f4 l1822 l1958 l1d10 l237c l23de l2414 l24b6 l3236 l36f6 l37d8
l3ab0 l3d1a l3ddc l3e60 l41a0 l455c. **16 non-leaves** blocked on local deps.
NB: some "leaves" call *external* CODE-11 helpers below 0xb26 that may be
unlifted — e.g. l37d8 → l0742 (unlifted); check external pc-relative targets too.

**C5a–C5n DONE (2026-07-06, 14 fns) — the clean-leaf tier is CLEARED:** l23de
(C5a), l16f4 (group-record edit-apply), l1958 (scroll-cursor clamp/re-anchor),
l1626 (holder derived-field refresh), l2414 (cursor/highlight sync — *derefs*
holder→rec), l1d10 (place-cell + commit/beep via jt290), l3654 (place-at-record
wrapper, unblocked by l1d10), l237c (toggle-label format+draw; jt444 takes &-11699
as its (b,c) short pair), l1822 (move object to scroll cursor), l41a0 (reset
active object; A5 scratch g_a5_-22307 is the loop counter), l4168 (enter placement
mode; deps l41a0/l476e), l3236 (tool-command state transition; 2× JT[3]), l455c
(map-view DLItem panel — the editor twin of the play-path l6256), l3ab0 (menu-grid
key navigation — full lift w/ JT[1] arrows/accel/disabled-skip).
**jt243-direct now:** l16f4/l1958/l23de/l4168 DONE; l16ae/l28d4/l36f6/l37d8 left.
**Two arg conventions in this subtree:** most helpers take the record directly
(rec = arg); l2414/l1d10/l3654/l237c/l3236 take a HOLDER whose word@0 points at
the record (rec = *arg) — match the asm's deref per-function, don't assume.

**C5o DONE (2026-07-06):** l066a_c11 (dirty-flag toggle; _c11 because
JT[1165]=CODE4+0x066a is a *different* fn at the same offset — recurring-offset
trap). **C5p was a MISFIRE — see correction below.**

### ALIAS-TRAP AUDIT CORRECTION (2026-07-06) — read this before "blocked" claims
An earlier note here claimed the remaining leaves were "blocked on missing CODE-8
JT stubs (jt330/331/332/334/336/337/342) — PROBE-stub them." **That was WRONG.**
Those functions are **already faithfully lifted** under their `lXXXX` alias names.
The error: the *missing-JT* method grepped the `jtNNN` name, which reports MISSING
for every JT entry that lives under an `lXXXX` alias (there are ~200 such). The
same trap made C5p re-lift JT[310] as a duplicate `jt310` when it already existed
as `l04d6` (reverted in "fix C5p").

**Correct method (always):** resolve each JT dep through `docs/lxxxx-jt-aliases.md`
(`lXXXX = jtN`) and grep the **lXXXX** name too; a JT is only truly missing when
neither `jtN` nor its alias `lXXXX` is defined. Verify the alias is the right
`(CODE, offset)` — the same offset recurs across segments (l066a=jt1165 is CODE 4,
NOT CODE 11's l066a).

**Ground truth (resolver over all 149 JT entries CODE 11 calls):** 132 defined as
`jtN`, **15 lifted via alias** (192=l4e3a 195=l4db4 293=l05ca 300=l0674 310=l04d6
330=l324c 331=l33f6 332=l4a16 334=l3f2e 336=l45c6 342=l567c 364=l6e50 395=l46b2
456=l2d3e 1084=l036a), and **only 2 genuinely MISSING: jt337=l41de, jt371=l660c.**
So the CODE-8 menu/render backend is DONE (band-7 lifts); nothing to stub there.

**C5q–C5s DONE (2026-07-06) — the alias-unblocked leaves:** l3ddc (menu/panel
repaint; JT[330/331/332/336]=l324c/l33f6/l4a16/l45c6), l3d1a (modal menu/colour
pick; JT[334/342]=l3f2e/l567c), l24b6 (menu-item enable/disable; JT[395]=l46b2 +
helper l24b6_label). **Reminder: at each JT[NNN] site whose entry is alias-only,
call the lXXXX name — `jtNNN` is not a symbol (mistyping `jt395` for `l46b2` broke
the l24b6 build once).**

**C5t DONE — l2ea0 (918B):** the cell tool/kind command handler (JT[3] 0..8 on
a2-1: set-kind / toggle-sub-kind / toggle-rec[9]; tool-palette highlight table
g_a5_-11508[k].7; scroll step jt218; recompose chain). Full lift.

**C5u–C5y DONE — hub mids + both giants:** l4416 (rebuild tool menus), l43c2 (open
tool-menu panel), l1a1c (cell paint-drag modal loop), **l1d88** (the 380-insn
per-kind cell-commit giant), l2836 (commit pending edits). Both 380-insn giants
(l1d88 + l2ea0) are now DONE — full lifts.

**C5z DONE — JT[371] = l660c** (monster-record name refresh; = jt350 with 5th arg
0; the categorizer is **l6520_c8**, not the same-offset l6520 in another segment).
One of the two "missing JTs" cleared.

### RESOLVER CAVEAT (2026-07-06) — name-defined ≠ lifted; verify (CODE, offset)
The dependency resolver checks whether a helper's `lXXXX`/alias NAME is defined —
but the SAME offset recurs across segments, so a defined name can be a DIFFERENT
function. This over-reported "ready/lifted" 3× in this area: l6520 (port's is a
2-coord bounds check, NOT the CODE-8 categorizer l6520_c8), **l0854=jt562 is
CODE 17** (not CODE 11's l0854), **l4c4c=jt777 is CODE 18** (not CODE 8's). ALWAYS
confirm the candidate's comment says the RIGHT `CODE N + 0xOFFSET` before trusting
"lifted".

**C6a–C6e DONE — the entire Save-3D-Map subtree (CODE 11) is COMPLETE:** l0ad0
(chunk header) + l0a4e (full chunk) + l0878 (FORM writer: FORM/AMOD/HDR/MAP/ENCR/
STRG, jt1180 byte-swaps around HDR, l4e3a around STRG) + l0854 (ds[0]=106 + run) +
l07c2 (write driver; JT[129] void so its bytes-written recheck is a dead branch) +
l0742 (command entry, JT[318]-gated) + **l37d8** (thin wrapper → l0742).  Gotchas
recorded: jt406 is memmove(dst,src) but Mac ABI copy(SRC,dst) → SWAP the call;
JT[192]=l4e3a and JT[364]=l6e50 and JT[1084]=l036a are alias-only names.

**C6f–C6h DONE — the hub's CODE-11 deps:** l2e1c (file/mode handler), l2dbe
(command router), l3380 (keyboard command handler, ~180 insn). Combined with
l2836/l1a1c/l24b6/l2ea0/l43c2 earlier, the **only remaining hub dep is l2d40**.

**THE WHOLE REMAINDER funnels through ONE chain — the jt337 CODE-8 subtree:**
l2d40 → l37f6 → l3e60 → **jt337=l41de**, which needs **l5150 ✓ → l4b10 + l4c4c**.

**C6i DONE — l5150 (CODE 8, the ~200-insn menu-item renderer) is LIFTED.** It was
error-prone (found+fixed 4 slips on re-verify); the transcription lesson: build each
jt1089/jt995/jt1001/jt1161 call from the exact push order, then re-verify every arg.
Placement note: needed a `static short jt1166(void);` forward decl before it.
**NEXT: l4b10 (~97) + l4c4c (~31), both need l5150 ✓ → l41de=jt337 (~160).**

(Historical scope note — l5150 details:) full menu-ITEM RENDERER (0x5150 → 0x54f8):
two draw modes on fp@27 bit4; icon/highlight blits (JT[995]/JT[1001]), formatted
item text via JT[1089] with STRS strings "%(%c%1g%)" / "^%c" / ".", command-key
measure (JT[423]/JT[408]), coord math via JT[1135]/JT[1161]/JT[1166]/JT[1200]/
JT[1198].  The jt337 chain is a **CODE-8 menu-RENDER sub-campaign** completing the
already-band-7-lifted menu
manager (jt332/jt336/l4a16 done).

**The realistic remaining picture to jt243 proper:**
1. **CODE-8 jt337 render chain** (~490 insn, 4 fns) — l5150 (renderer) → l4b10 +
   l4c4c → l41de=jt337.  (Alt: PROBE-stub jt337 to reach the dispatcher structurally
   — but its siblings jt332/336 are faithful, so a stub is inconsistent.)
2. **l3e60 → l37f6 → l2d40** (CODE 11, 3 fns).
3. **hub l28d4** (~1132B) → **jt243 proper** (~800-insn dispatcher, 20-arm JT[3]
   @0xb48).
This is a multi-session tail, not a quick finish; l5150 in particular wants a fresh
careful pass (coordinate + JT[1089] format-arg heavy).  Verify each callee's
CODE+offset — the name-only resolver has lied 4× here.

**Also found: jt302 was a MIS-LIFT** — the Mac l04f2 is 2-arg `jt302(cell, want)`
writing `(byte)want` (fp@11 = low byte of arg2); the port added a spurious 3rd
`val`. Fixed to 2-arg (no other callers).

**NEXT — l1d88 (1524B) is READY (deps all present via l04d6):** the per-kind
cell-render/commit dispatcher, `rec = *holder`, a **4-arm JT[3] @0x1d9e (min=1
max=4) on rec[18]** (1→L1dac, 2→L2014, 3→L2150, 4→L226c). Arms are LINEAR-but-large
(field copies over the design-state cell table `ds[+290]`/rec bands, cursor
repaints via jt292/278/279/295/290/312/321, calls l237c). ~380 insn — careful
full lift. Then the hub **l28d4**, then jt243 proper (20-arm JT[3] @0xb48).

(Already lifted this campaign: jt233/234/235/236/237/238/239/240/241/244 +
l4226/l4268 — see `docs/area-map-wall.md`.)

## Recommended attack order — updated 2026-07-06

1. ✅ **CODE 22 painter trio** (jt282/jt286/jt281/l347a) — DONE.
2. ✅ **CODE 2 event editor** (jt246/jt254/jt253/jt248/jt249/jt258) — DONE
   (Phase B); wired by the CODE 22 `l0096` command dispatcher.
3. ✅ **CODE 10 viewers + jt259 art import + jt266** — DONE (Phase A).
4. **CODE 11 jt242** (Phase C2) — 8 local helpers then the dispatcher; deps all
   lifted. ← **NEXT**
5. **CODE 11 jt243** (Phase C5+) — the 5216-insn / ~40-function GEO editor main;
   bottom-up, multi-session.
6. **Foundational giant** CODE 8 jt335 (2598) — may unblock the editor giants.
7. **Wire the editor live** — jt315 selection dispatch (CODE 22+0x5180/0x5266)
   → `l0004_22`, making the whole editor reachable for a human tester.

**Deferred/parallel runtime polish** (not editor, but still open): inventory/
equip, remaining `l709e` event arms, camp spell-memorize, save/load pickers,
audio, and the small-stub cleanup (CODE 5/12/4). See `subsystem-status.md`.

**Pattern across CODE 2 and CODE 10 (2026-07-05):** the wall's per-entry
`~insn`/deps columns were scanned label-to-label and swept in the following
CODE-local helpers, so they wildly over-count and mislabel deps. Each
"small viewer/painter" cluster has yielded exactly ONE genuine clean leaf
(jt246, jt265); the rest are dispatchers or bottom out on deep multi-segment
subtrees. Measure each entry against the disasm before trusting the table.

The dispatchers that *wire* the cluster for eventual validation are the
CODE 22 **L0004** area-command loop (21 arms) and jt315's EDIT MODULES
branch; both are mouse-gated, so wiring them does not unlock headless
tests — it only makes the editor runnable for a human tester.

## Coordinate convention: transcribe asm push order DIRECTLY (no swap)

Verified while lifting jt282 (the earlier "jt1089 needs a swap" note was
BACKWARDS — corrected here). Post-#116 the port primitives all match the
Mac's coordinate arg order, so you transcribe each asm call's push order
straight through:

- **jt1089**(v, h, color, fmt, …) — the body param names are `(v, h)`
  (the misleading forward decl says `x,y`); it feeds `jt1135(v, h, …)`.
  The Mac pushes JT[1089] as (v, h), so **no swap**. jt1089 formats via
  `vsnprintf`, so `"%s %d"` + (char* , promoted short) works directly.
- **jt1161**(top, left, bottom, right, fill) and **jt1173**(top, left,
  bottom, right) — Mac (v, h, v, h) order, direct.
- **jt353**(x, y, icon, mode, flag) — x=horiz, y=vert (it calls
  jt1001(y*16-…, x*16-…)); the Mac push order still transcribes direct.

So the rule for every painter: **read the asm push order (last operand
pushed = first C arg) and pass it straight through.** The remaining risk
is not transposition but getting a coordinate offset or a branch arm
wrong — cross-check each against the disasm, since the mouse-gated smoke
harness can't catch it.

## The holder-vs-record split (found lifting jt281 — corrected jt278/282/286)

jt278's `handle_ptr` is an entry **HOLDER**, not the record. Two structures,
two deref depths — verified against the asm (`2050 moveal %a0@,%a0` present =
double deref = record; absent = single deref = holder):

- **record R = `*hp`** (double deref, `(*hp)[N]`): the live design content —
  byte 4 = state (drives jt278's anchor/height + is_zero), byte 5 = kind, and
  the painters' content bytes 10/12/14/15.
- **holder H = `hp`** (single deref, `hp[N]`): the entry's own per-paint
  caches — byte 4 = the `st` value jt278 passes to the painters, byte 36 =
  jt278's last-painted stamp, and the painters' dirty-check caches 30/31/32/33
  and the secondary-state cache 35.

The first-pass lifts of jt278/jt282/jt286 wrongly read those cache bytes through
`rec = *hp` (double). **Corrected in this commit**: jt278 (`st` = holder[4],
stamp `holder[36]=holder[4]`), jt282 (holder[32]), jt286 (holder[30]/[31]) —
each now single-deref via a `holder` local, with the content bytes (5/10/12/14/
15) unchanged (still `(*hp)[N]`). No behaviour change today (all dormant), but
using the record's bytes 30-36 as paint caches would have corrupted record data
once the editor UI is wired.

## Status — painter trio COMPLETE (3/3)

- **jt282 — LIFTED** (2026-07-05). kind 1. jt278 case 1. holder[32] fix.
- **jt286 — LIFTED** (2026-07-05). kind 0 + 3-local dep tree. jt278 case 0.
  holder[30]/[31] fix.
- **jt281 — LIFTED** (2026-07-05, was l329c=entry_jt281). kind 2, faithful
  goto-mirror of 0x329c..0x3478 (the "390 insn" note conflated it with l347a;
  it's ~120 insn). jt278 case 2 rewired. Care-points, all resolved:
  - **jt406** is the swap-convention exception — port lift is
    `memmove(dst,src)` but the Mac pushes `copy(src,dst)`, so the record→scratch
    name read is `jt406(namebuf, base+134+code*16, 16)` (SWAPPED; matches the
    jt406(b,a,n) rule at the other callers).
  - **jt488**(-10604, code+1) = the empty-name fallback string (vsnprintf into
    g_a5_10362, returns it).
  - **jt1089 "%*s"** (STRS 0x30de) width-draw — the port jt1089 is vsnprintf so
    `%*s`(int width, char*) works directly. Width = 13 (compact: z&&sel>=0) or
    16. Terminators at namebuf[13]/[16] (fp@(-5)/fp@(-2)). STRS 0x30e8 = "".
  - Uses -11312 (type label) / -10604 (fallback fmt) / -10648 (state marker).

The coordinate convention (Mac order, direct push-order transcription) is
settled; jt406 is the lone swap. **Kind 3 (l347a = its own entry_jt?) remains
a stub** — the next design-list painter. Everything here is dormant/mouse-gated;
verify against the disasm, not the smoke harness.
