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

## The 18 open entries, by segment and size

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
L6606     ~52  PICT scanline unpack+store loop (jt1170/1171/1177/1179/1202/
               1166/413/468) — clean leaf; ⚠ jt1170 is void in the port but
               the Mac pushes a word arg (verify like jt80/jt208); jt1171=l108e.
L4eda     ~64  (next to read)      L4f9c  ~86      L66a2  ~84   L67a0  ~80
L4cae    ~185  L49f8 ~231  L509e  ~262
L42f2    ~528  sub-giant           L53b0  big (spans past jt260)  L6892  tail
```

**Recommended order:** finish the clean leaves (L6606 after the jt1170 sig
check, then L4eda/L4f9c/L66a2/L67a0) → the mid helpers (L4cae/L49f8/L509e) →
the two sub-giants (L42f2, L53b0) → L36e0 main as a **level-2 skeleton** first
(mirror the CFG + call every helper, defer per-arm), then fill. This is the
FRUA art-import path (design-editor "Import Picture") — dormant/mouse-gated,
so unvalidatable headless; verify against the disasm, not the smoke harness.

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

### CODE 11 — area/geo editor (continuing earlier work)
| JT | addr | ~insn | notes |
|---|---|---|---|
| jt242 | l589a | 1122 | cell-edit committer; JT[3] over 5 unlifted painters (L5a06/L6136/L61c6/L5ee2/L5b0e) — lift painters first |
| jt243 | l0b26 | ~800 | the big CODE-11 dispatcher — own session |

(jt233/jt239/jt244 + l4d24/l49dc already lifted this campaign — see
docs/area-map-wall.md.)

## Recommended attack order

1. ✅ **CODE 22 painter trio** (jt282/jt286/jt281/l347a) — DONE.
2. **CODE 2 small painters** — only jt246 was a clean leaf (DONE); the rest
   (jt258/jt254/…) are dispatchers/constructors needing jt207+jt1076 first.
3. **CODE 10 small viewers** — only jt265 was a clean leaf (DONE, 2026-07-05);
   jt264/jt270 need the **L611c subtree** (see the CODE 10 reality-check
   above). Best next entry there: **jt1084** (the low-level Error alert —
   MISSING, broadly used), then jt372 (+L62e0_c8/L60b0_c8), then L611c, then
   jt264; jt270 additionally needs jt456 + l06ae + l2ebe.
4. **CODE 11 jt242** (after its 5 painter locals).
5. **The giants** (jt259/266/249/248/243) — one focused session each.

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
