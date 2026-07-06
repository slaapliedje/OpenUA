# Event-editor wall (CODE 2) — Phase B of #153

The design-side **event editor**: the screens that author a design's events
(the tile/wall specials the play runtime dispatches through `l709e`). This is
the largest design-editor cluster. Runtime event *dispatch* is already lifted
(see [[event-subsystem-campaign]]); this wall is the *authoring UI*.

## Launcher context (why it's dormant today)

The five giants are arms of the **CODE 22 design-editor command dispatcher**
(function @ CODE 22 + 0x0096) — a row of `jsr JT[N]` menu handlers:

```
JT[247] CODE 2   JT[248] CODE 2   JT[249] CODE 2   JT[254] CODE 2   (event editor)
JT[233/239/240/241/244] CODE 11  (GEO 3D-map editor — Phase C)
JT[259] CODE 10 (art import, DONE)   JT[263]/JT[270] CODE 10
```

That dispatcher is **not lifted** (its arms have no live callers; only the main
menu jt315 is live). So even a fully-lifted event editor stays dormant until
the CODE 22 dispatcher is lifted and wired — which can't happen faithfully
until enough of these arms exist. Track that as the Phase-B/C "wire-up" step.

## Targets — bottom-up by size (alias offsets from lxxxx-jt-aliases.md)

| entry | alias | CODE 2 off | insn | role |
|-------|-------|-----------|------|------|
| jt254 | l4c5a | 0x4c5a | 422 | event-list PRINT command — **COMPLETE** |
| jt253 | l44cc | 0x44cc | 644 | editor screen — **NEXT** |
| jt252 | l44ca | 0x44ca | tiny | 2 bytes before jt253 — fold in with jt253 |
| jt248 | l26aa | 0x26aa | 890 | editor screen |
| jt249 | l333a | 0x333a | 1102 | editor screen |
| jt258 | l0004 | 0x0004 | 2808 | **event-editor MAIN** — skeleton then fill, last |

## jt254 sub-scope (B2-B4)

`l4c5a` itself is a tiny wrapper (`jsr JT[358]` + `jsr L541c` + rts). The 422
insn is the CODE-2-local **helper cluster** it anchors:

- **L4c92** — JT[207]×N + JT[397] loop (a row/field painter)
- **L4dcc**, **L4e3e** — JT[207] + JT[397] + a JT[3] inline switch each
- **L4eb2** (frame −102) — the worker: JT[384]/394/404/1076 + calls
  L4c92/L4dcc/L4e3e + JT[201]
- **L514e**, **L5194** (frame −10, JT[193]…), **L541c** (the entry's callee)

Leaf dependencies — **ALL 9 already lifted** (verify by the lXXXX alias, not the
jtN symbol!): jt358, jt397, jt384, jt394, jt404, jt201, and jt193 = **l4fbe**
(27554), jt207 = **l5484** (2729), jt1076 = **l7ab4** (59636). The first pass
mistook these three "absent" because it grepped `jt193` etc. instead of their
lXXXX names — the same trap as jt1159=l4350. So no leaf lifts are needed; the
whole jt254 job is the CODE-2-local helper cluster below.

Cluster helpers to lift (all genuinely absent — pure CODE-2 locals, not JT
exports, so not in the alias map):

| helper | off | deps | order |
|--------|-----|------|-------|
| L4c92 | 0x4c92 | JT[207]+JT[397] | ✓ B2 (pure local) |
| L4dcc | 0x4dcc | JT[207]+JT[397]+JT[3] | ✓ B2 (pure local) |
| L4e3e | 0x4e3e | JT[207]+JT[397]+JT[3] | ✓ B2 (pure local) |
| L514e | 0x514e | **= jt255** (line 64342) | ✓ ALREADY LIFTED |
| L5194 | 0x5194 | **= jt256** (line 64503, word-wrap) | ✓ ALREADY LIFTED |
| L541c | 0x541c | jt254 entry's direct callee | B3 (pure local) |
| L4eb2 | 0x4eb2 | calls L4c92/L4dcc/L4e3e + JT[384/394/404/1076/201] | B3 — worker |
| l4c5a | 0x4c5a | **= jt254** — JT[358] + L541c — the ENTRY | B3 — wire last |

**LESSON (both directions):** a cluster member's lXXXX name being absent does
NOT mean it is unlifted — it may be a JT export lifted under its jtN name. Two
of these five (L514e=jt255, L5194=jt256) were already done; nearly re-lifted
L5194. For every lXXXX, grep `docs/lxxxx-jt-aliases.md` for `lXXXX = jtN` and
check jtN too (the mirror of the jtN→lXXXX trap the jt_lookup fix now handles).

**B2 COMPLETE** — the only genuinely-absent leaves were the three pure-local
glyph pickers (L4c92/L4dcc/L4e3e), now lifted; L514e/L5194 were already jt255/
jt256.

**B3 — jt254 is the event-list PRINT command** (bigger than the insn count
implied). The l4c5a entry is a thin jt259-style wrapper (call worker → pack byte
result into *desc → return state); the real worker is **L541c** (frame −282, a
large print driver: "Printing . . ." + printer setup + JT[76/94/1134/1009/978]
+ text-format helpers + _UnLoadSeg housekeeping). L541c drives **L4eb2**, the
ASCII map-grid printer, and **L5304** the recursive event-chain printer.
- **B3a DONE** — L4eb2 (map-grid renderer) lifted.
- **B3b+c DONE — jt254 COMPLETE.** L541c (the print driver, 4 output blocks:
  map tiles → cell legend → Step Events → Rest-in-Zone) + **L5304** lifted as
  `l5304_c2` (recursive chain printer; (CODE,offset) clash with a CODE 6 l5304
  item-loader → `_c2` suffix) + the l4c5a=jt254 entry wrapper. Two tail leaves
  (jt260 CODE 10+0x5aca, jt709 CODE 16+0x0004) were empty `rts` in the Mac →
  added as PROBE stubs (mirror jt859). All other leaves already lifted.
  - **Print path is a documented stub at the printing-manager boundary:**
    jt428 (open job) is a PROBE stub → the -9162 gate never opens → jt433 output
    is gated off by l7a24(). So L541c/L5304 transcribe faithfully and emit
    nothing — the ADR-consistent HAL-boundary treatment, no Falcon print driver.
  - **jt325 is a level-2 skeleton** that doesn't populate the child list via
    g_a5(-12070), so l5304_c2's recursion is currently inert (count stays 0). It
    activates faithfully once jt325's record-editor tail is lifted.
  - **jt1074 collapses the Mac "%r" sub-format to one tail operand** — L5304's
    "%s - %s" 2nd operand (the event name) is dropped by the port's fixed jt1074
    signature; moot behind the dead print gate. Noted in the lift comment.

**jt254 (l4c5a, the smallest editor screen) is now COMPLETE.**

## jt253 (l44cc) — the event-cell EDIT command (STARTED)

The interactive "place / edit an event at the clicked map cell" command. Bigger
and branchier than jt254 — a ~530-insn CODE-2 cluster of the main + four local
helpers, several of which are SHARED with jt257/jt258 (the selection ring):

| piece | off | frame | role | leaves | status |
|-------|-----|-------|------|--------|--------|
| L20ac | 0x20ac | 0 | selection-ring PRODUCER (append/reset the -12190 ring) | — | ✓ done |
| L2350 | 0x2350 | -4 | selection-ring CONSUMER (advance cursor -12196, validate) | jt229, jt321 | ✓ done |
| L4842 | 0x4842 | -22 | cell-array RESHAPE (grow/shrink col stride via jt406/jt399; ring-scans affected events) | L20ac, jt413/406/399/7/4 | ✓ done |
| L4bd4 | 0x4bd4 | -18 | event-picker COMMIT loop (snapshot rec14 → jt325 serialize → restore) | jt348/359/325, L2350 | ✓ done |
| L44cc | 0x44cc | -16 | **= jt253 main dispatcher** — 2× JT[1] {2,5,10,8} + JT[3] {5}; packs display flags into *desc | jt319/273/317/320/325, L4842, L4bd4 | ✓ done |
| l44ca | 0x44ca | — | **= jt252**, empty `rts` (adjacent placeholder) | — | ✓ stub |

Switch tables (extract-tool decoded, #122-safe):
- JT[1] @0x44fe: {2→0x4512, 5→0x4524, 10→0x4536, 8/default→0x45d4}
- JT[1] @0x4718: {10→0x472c, 8→0x4766, 2/default→0x4836, 5→0x4784}
- JT[3] @0x4550: min=max=5 {5→0x4558, default→0x45d4}

All JT leaves already lifted (jt319/273/317/320/325/229/321/348/359/413/406/399/
7/4). Design record base = g_a5(-12300); event-cell records are 6-byte at
rec+290 (the L4842 field scan), 20-byte at g_a5(-13038) (jt325 src).

**B-jt253a DONE:** L20ac + L2350 (the selection-ring pair) + jt252 stub.
**B-jt253b DONE:** L4842 (the cell-array reshape / column re-stride helper —
grow forward, shrink back-to-front, or tail-zero; collects affected events into
the ring).
**B-jt253c DONE:** L4bd4 (event-picker commit loop — snapshot rec14, serialize
each ring selection via jt325, restore). Faithful quirk: the Mac feeds L2350's
0/1 flag (not the event byte) to jt348/jt359.
**B-jt253d DONE — jt253 COMPLETE:** the L44cc main dispatcher. JT[1] on a8
{2 store / 5 recall / 10 begin-edit (nested JT[3] on ctx[4] case 5 = L4842
reshape / L4bd4 picker when *desc&15, else seed dims + jt319) / default no-op};
then (unless committed) jt325 serialize (type 51/52 per jt273), jt317 dim
reconcile, and on a jt320 commit either mark the cell or L4842 reshape; then a
second JT[1] on ctx[2] packs p16[12] into *desc's bit-fields (case 5 = the
tiered 512/768/1024 position pack via the 16-bit lslw/oriw/extl sequence).
Both JT[1] + the JT[3] decoded via the extract tools (#122-safe). Returns ctx[2].

**jt253 (l44cc) is now COMPLETE** — two editor screens (jt254, jt253) done.

## jt248 (l26aa, frame -140) — the interactive event-parameter EDITOR (STARTED)

Bigger and different from jt253: a mostly-MONOLITHIC modal (~668 insn, 0x26aa..
0x311a), NOT a helper cluster. Structure:
- **Prologue** (0x26aa-0x2806): parse *desc — type = *desc&15 (fp-2), sub =
  (*desc&1008)>>4 (fp-4); type==4 is an early exit via jt314 + repack (-> L3116).
  Build a base prompt string (jt394) for type==3 ("...%d, %d%s?") / type<4
  ("...%s:") using the -108xx string-table globals.
- **8-arm JT[3]** on type @0x280e {0->L2824, 1->L29c8, 2->L2af6, 3->L2cbe,
  4/def->L2dc2, 5->L2b9a, 6/7->L2a36} — each arm formats one event type's
  parameters into the fp(-62) buffer. THE per-arm work (the bulk).
- **Modal tail** (0x2ec8-0x3116): jt84/jt117 screen setup, jt423+jt1089 draw the
  prompt, L31cc labels, jt169 List Manager pick, jt347 value clamp, pack the
  adjusted value back into *desc, jt147 cleanup. Returns fp@(8).

Only local helper = **L31cc** (0x31cc, a centered-label renderer). Everything
else is inline arms. All JT leaves already lifted (jt314=l494e, jt347, jt167,
jt179, jt488, jt168, jt360, jt147, jt169 List Mgr, jt1089, jt423, jt394, jt84,
jt117, jt273, jt179). JT[3] decoded via the extract tool.

**jt248a DONE:** L31cc (lifted as `l31cc_c2` — (CODE,offset) clash vs a CODE 6
l31cc design-name copy).

**jt248b DONE — L26aa level-2 SKELETON:** the prologue (parse type=*desc&15,
sub=(*desc&1008)>>4), the type-4 early exit (l494e repack), BOTH base-string
prompt builds (type 3 = jt273/jt488 "%s %s %s %s %d, %d%s?"; else "%s %s %s
%s:"), the 8-arm JT[3] scaffold, and the short-circuit cleanup (jt147 + return
a8) are lifted and faithful on the always-run path. jt314 is callable as
**l494e** (not jt314). All 8 arms converge at L2e42; the prologue zeroes the
-132 list holder so with arms deferred the tail short-circuits to jt147.

**jt248 FILL-PASS TODO (remaining):**
- The 8 JT[3] arms (converge at L2e42), each populating the -132 list (jt167) +
  modal state (-113/-122/-6/-124/-119/-117): c0=L2824, c1=L29c8, c2=L2af6,
  c3=L2cbe, c5=L2b9a, c6/7=L2a36, c4/def=L2dc2. Arms use jt352/jt349 (enumerate),
  jt394 formats, and the -108xx/-109xx/-11xxx string-table globals.
- The shared modal tail (L2e42..L30d8): list-walk, jt179, jt84/jt117/jt1089 +
  l31cc_c2 draw, the **jt169** List-Manager pick LOOP (re-runs until the pick is
  0/1), jt347 value-adjust behind its **JT[2] @0x3042** switch, and the *desc
  repack at L30d8. jt168/jt360 also here.

### JT[2] @0x3042 — DECODED (2026-07-05)

jt1_extract read it wrong (it assumes JT[1] *word* keys; JT[2] uses *long* keys
— boot.c:5724). Decoded by the documented `(off.W, key.L)`-pairs format and
cross-checked against the arm bodies (the 0x308c arm literally tests
`*desc&15==6`). switch(*desc & 15): **{1 → 0x305c, 6 → 0x308c, 7 → 0x308c,
default → 0x30d8}**. 0x305c/0x308c call jt347 (value clamp); default 0x30d8 =
straight to the *desc repack.

### Fill-pass leaf signatures (all lifted, arg orders pinned)

- `jt352(short kind, short mask, long lo_p, long hi_p, long cap_p, long out_idx,
  long proc)` — arm enumerate; arm0 = jt352(11,1,&fp[-113],&fp[-114],0,&fp[-2],0)
- `jt349(long node, short kind, short mask, short minlvl, short hdrflag,
  short u6, long proc)` — arm0 = jt349(fp[-132],11,1,fp[-113],1,0,0)
- `jt169(long h1, long h2, short top, short left, short right, short bottom,
  long head, short a, short b, uchar *flag, short *idx, long *next)` — the modal
  = jt169(g_a5(-13952), &fp[-112], 2,4,38,15, fp[-132], 1,0, &fp[-116],
  &fp[-122], &fp[-128])
- jt167(count, &holder), jt147(&holder), jt168(head,0,1), jt179(1), jt360(fmt,..),
  jt367(v,buf), jt397(a,b), jt84(void), jt117(void), l31cc_c2 (jt248a).

### COUPLING (why the arms + tail are one unit)

Arm 0 writes fp[-114/-124/-113/-2/-8/-128/-12/-122/-6/-119/-117] and the tail
consumes them (list_holder=fp[-132] via jt167; fp[-6/-119/-117/-12] are
tail-ONLY reads). So an arm can't commit without the tail (dead stores) and the
tail can't run without an arm (uninit reads).

**jt248c DONE — arm 0 (L2824) + the full shared modal tail.** Arm 0 enumerates
the design's items (jt352 count → jt167 list → jt394/jt367 node labels → jt349
finalize) with the sub-type seeding from *desc bits 10-12. The tail: list-walk
to the selected node, title (jt394), draw (jt84/jt117/jt1089 + l31cc_c2), the
jt169 List-Manager pick LOOP (re-runs until pick∈{0,1}), the JT[2]-decoded
jt347 value clamp, and the L30d8 *desc repack `(*desc & 0xFFFFF000) |
((1-pick)&15) | ((idx&255)<<4)`. The tail is SHARED — so the remaining 7 arms
now each plug in as one commit (they populate the same list_holder + modal
state). Remaining fill: c1=L29c8, c2=L2af6, c3=L2cbe, c5=L2b9a, c6/7=L2a36,
c4/def=L2dc2.

Next targets by size: **jt248 main (l26aa)** → jt249 (l333a, 1102) → jt258
(l0004, 2808, the event-editor MAIN — skeleton-then-fill, last).

## Method (same as jt259 / #153 so far)

Faithful transcription from `data/work/disasm/CODE_02.s`. Read the exact stack
pushes for arg order; JT[1]/JT[3] switches via the extract tools (never
hand-decode, #122); check `docs/lxxxx-jt-aliases.md` before treating any lXXXX
as unlifted (three "blocked/needs-trace" flags on jt259 all dissolved that
way). Each leaf/helper = one build + commit; codegen stays 1889, tests 129/1.
Dormant (mouse-gated, unvalidatable headless) — verify vs disasm, not smoke.

STATUS 2026-07-05: B1 (scope) DONE. Leaf audit corrected — all 9 jt254 leaves
were already lifted (the "3 absent" were lXXXX-aliased). B2 in progress: L4c92
(junction '+'), L4dcc (horiz '.'/'-'), L4e3e (vert ':'/'|') lifted — the ASCII
map-grid glyph trio. Next B2: L514e, L5194. Then B3: L541c + L4eb2 + l4c5a.
