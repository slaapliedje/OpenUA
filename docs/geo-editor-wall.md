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
| jt266 | l1bc2 | 1692 | big — own session; also unblocks jt269's L040c dialog |
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

Remaining CODE 10: **jt266** (1692-insn giant; also unblocks jt269's L040c
dialog) and **jt259** (2757-insn giant) — each its own session. Every other
CODE 10 viewer (jt264/jt265/jt267/jt269/jt270) is lifted.

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
