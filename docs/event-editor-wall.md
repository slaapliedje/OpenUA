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
state).

**jt248d DONE — arm 1 (L29c8), the simplest arm.** A plain kind-10/mask-96
enumerate: `f117=1`; `cnt124 = jt352(10, 96, &lo113, 0L, 0L, &idx122, 0L)` (the
selected index writes straight into idx122 via out_idx — no per-node relabel
loop, unlike arm 0's kind-11/mask-1); `str12 = g_a5_long(-10704)`; jt167 holder;
`loop8 = lo113 + jt349(list_holder, 10, 96, lo113, 0, 0, 0L)` (hdrflag=0 here,
=1 in arm 0). Consistency: the tail's JT[2] case 1 clamps this arm with the same
(10, 96) jt347 pair.

**jt248e DONE — arm 2 (L2af6), the fixed 9-entry table arm.** Unlike arms 0/1
this one has NO jt352 (cnt124 is the constant 9) and NO jt349: `cnt124=9`;
`if (sub!=0 && sub<=9) idx122 = sub-lo113`; the TITLE is built directly into
buf112 via `jt394(buf112, "%s %s"@0x2c00, g_a5(-10796), g_a5(-10700))` (so the
tail's own str12 title-build is skipped, which is why str12 is never set here);
jt167 holder; then loop each of the 9 nodes labeling `cursor+5` from the
`g_a5_longs(-11224)[loop8]` string table via `jt394(., "%s"@0x2c06, table[i])`,
`cursor[4]=0`, next, loop8++ (loop8 starts at lo113, ends lo113+9 = lo113+cnt124
→ tail guard passes). f117 stays 0 → the tail's jt347 value-clamp is skipped for
this arm. STRS confirmed: 0x2c00="%s %s", 0x2c06="%s". Remaining fill: c3=L2cbe,
c6/7=L2a36, c4/def=L2dc2.

**jt248f DONE — arm 5 (L2b9a), the nested-JT[3] rec-array arm.** The richest
arm so far. `lea a5@(2002)` = the code address of JT[246] (annotated disasm) →
`p136 = (void*)jt246` (the selection hook the tail stashes via jt168). cnt124=8,
lo113=0. `loop8 = (sub & 48)>>4` (bits 8-9 of *desc) is the INNER JT[3] key
(jt3_extract @0x2be2: min=1 max=2, key1→0x2bf0, key2→0x2bf8, default→0x2c28);
then `sub &= 15` and the same `if (sub!=0 && sub<=8) idx122 = sub-lo113`. Inner
JT[3]: key1 → str12=g_a5(-10704); key2 → str12=g_a5(-10800) + rebuild buf62 via
`jt394(buf62, "%s %s %s?"@0x2c0a, -10800, -10740, g_a5_longs(-10956)[type])`;
default → neither (all three converge on the common list build). Then jt167(8)
and the 8-node loop fills each `cursor+5` (16 bytes) from the -12300 event
record's name field `rec + loop8*16 + 134` via **jt406 with the (src,dst)→
(dst,src) SWAP** (the flagged Mac ABI: rec is the Mac top-of-stack = src, node
is dst), null-terminates cursor+21, and falls back to `jt394(cursor+5,
"#%d"@-10604, loop8+1)` when the copied name is empty; cursor[4]=0; advance.
No jt349. jt246 stays DCE'd (jt248 is dead). Remaining fill: c6/7=L2a36,
c4/def=L2dc2.

**jt248g DONE — arm 3 (L2cbe), the per-cell coordinate arm.** cnt124=8, lo113=0,
str12=g_a5(-11008). `idx122 = sub` directly, gated `if (sub!=0 && sub<8)` (bge,
not bgt). jt167(8) then an 8-node loop: each cell = the -12300 event record at a
4-BYTE stride (`rec + loop8*4`, vs arm 5's *16). Label = `jt394(cursor+5,
"%s %d: %d,%d %s"@0x2c14, g_a5(-10636), loop8+1, cell[15], cell[14], strval)`.
`strval` is f116-gated: f116 set -> ""@0x2c24 (dead — f116 is 0 until the tail);
f116 clear -> `g_a5_longs(-10924)[(cell[16] & 6) >> 1]` (the same facing table
the type-3 prologue prompt uses). No jt349; f117 stays 0 so the tail's jt347
clamp is skipped. STRS verified: 0x2c14="%s %d: %d,%d %s", 0x2c24="".

**jt248h DONE — arm 6/7 (L2a36), the door/wall-type arm (types 6 & 7 share it).**
Structurally arm-1-like (jt352 enumerate + jt349 finalize, no per-node loop),
but the list KIND is 12 (type 6) or 14 (type 7) — the SAME value the tail's
JT[2] case-6/7 jt347 clamp uses. Details: f117=1; sub is re-derived with the
WIDER 4080 mask (bits 4-11, vs the prologue's 1008/bits 4-9); lo113=0, v6=-1,
idx122 = sub - (lo113!=0?1:0); the original sub byte is captured into a new
cap115 local (fp@(-115)) BEFORE sub is overwritten to 12/14, then passed as
jt352's cap_p (5th arg; arms 0/1 passed 0L there). str12=g_a5(-10704). jt349
uses kind=12/14, mask=1, hdrflag=0.

**jt248i DONE — arm 4/default (L2dc2) — jt248 is now a FULL LIFT.** The outer
JT[3] (jt3_extract @0x280a: min=0 max=7, default=0x2dc2, and key 4 ALSO =0x2dc2)
routes case 4 and out-of-range (8-15) to the same block, so the C `default`
covers both. `type>=8` (invalid): blank buf62 and fall through with an empty
list — the tail's list_holder guard then short-circuits to jt147. `type<8`
(only type 4, which the prologue early-exit pre-filters → faithful-but-dead):
build cnt124=(sub?sub:255) nodes each `jt394(cursor+5, "Item %d"@0x2c26, loop8)`
from loop8=lo113=1. STRS verified: 0x2c26="Item %d".

**jt248 COMPLETE** — all 8 arms (c0 c1 c2 c3 c4/def c5 c6 c7) + the shared modal
tail lifted across 9 commits (jt248a L31cc → jt248i). The whole cluster is DCE'd
(jt248 unreferenced until the CODE 22 dispatcher is lifted), codegen holds 1889.

## jt249 (l333a, CODE 2+0x333a → 0x4024, frame -98, ~900 insn) — STARTED

The large sibling editor screen. Signature confirmed from the CODE 22+0x02aa
caller (pushes `word *(struct+2)`, `&struct[8]`, `0L`): **jt249(short a8, long
*desc, long *p14)**, p14 = NULL from that caller, desc = &caller_struct[8].
CAUTION: fp@(10)=desc is read as a long pointer early (moveal; a0@) AND as a
WORD late (the 8× `movew fp@(10)` at 0x3e9c-0x4000 feed jt1161) — the late reads
are the desc pointer's HIGH word, transcribe faithfully when lifting the tail.

**Dispatch map:** main **JT[2] @0x334c** on `*desc & 15` (table @0x3350: count=2,
keys {1,2}, long-key format → {1:0x3360, 2:0x339e, default:0x33d6}) builds the
prompt pair. Two inner **JT[3]** sites: @0x3862 (jsr) and @0x3e5a (jsr, decode
with jt3_extract --jsr-at 0x3e5a). Internal helper **L3cbe** (jsr pc@ from
0x3558) — a coord/field sub-routine, lift as an lXXXX. Heavy JT leaves: jt1161
(10×, filled-rect top/left/bottom/right/fill), jt452 (8×, DLItem draw), jt1200
(7×, ==3 query), jt394 (4×, prompt fmt), jt1089/jt447/jt79/jt357/jt179/jt152/
jt148/jt423 singles.

**jt249a DONE — the prologue (0x333a-0x34e8).** JT[2] prompt build (type 1 →
buf52 "%s %s %s %s:"@0x2c46 + buf72 "%s %s"@0x2c54; type 2 → buf52 "%s %s %s?"
@0x2c5a + buf72 "%s %s"@0x2c64; default → common), *desc unpack (f8=bits 4-7,
f77=bits 8-11), the 16-entry display-reorder table arr94[1..16] =
{0,1,14,15,2..13} (3 fill loops), and the search that finds f6 = position of f77
in the table. STRS verified. Body deferred (level-2); f6/f8/f10 (void)-marked as
consumed-by-deferred-body. Codegen 1889, tests 129/1.

**jt249b DONE — L3cbe (0x3cbe-0x3d8e), the grid-cell coord helper.** `l3cbe(short
idx, short *out1, short *out2)`: writes the (row/Y, col/X) screen coords for slot
`idx`. Two layouts on jt1200() (==3 vertical, else horizontal); idx==0 = the
header slot (fixed 8001/8044 or 8014/8038). Non-zero: k=idx-1, out1 steps 20 per
row (k/5) off 8000/8014, out2 steps 19 or 14 per column (k%5) off 8006/8010. The
arg's low byte (fp@(9)) is the effective idx; the in-place `subqb #1,fp@(9)` is
just how THINK C reuses the slot for k. Called from jt249 at 0x3548/0x3da4/
0x3e3e/0x3f70. DCE'd (no live caller until jt249's body lands).

**jt249c DONE — draw setup + L3548 slot-outline loop (0x34ea-0x35dc).** jt447
setup, then the base rectangle (jt452 shape 5) with coords keyed on jt1200()
(==3 vertical 8012/8044, else 8014/8038). Then the L3548 loop outlines the 15
grid slots (for i=1..15): l3cbe(i,&c74,&c76) → jt452 shape 5 box (row axis +12
in the vertical layout). Then the selection cursor (jt452 shape 2, 16/35) at the
found position &f6 — this is where f6 is consumed. All jt452 args cast (long)
per the DLItem-stream convention.

**jt249d DONE — l3c18 helper + the L35ea slot-label loop (0x35ea-0x3698).**
l3c18(short idx, short *out1, short *out2): the label-coord sibling of l3cbe —
*out1 = column X (idx'*4 + 8020), *out2 = fixed row Y (8097 vertical / 8090
horizontal via jt1200()); slots 1..8 fold down by one (b=idx-1), 9..16 keep idx.
The loop (for i=1..16): j=(i==8)?7:i, strptr = g_a5_longs(-11296)[arr94[j]],
l3c18(i,&c74,&c76), len=jt423(strptr), off=jt397(0,15-len), then jt452 shape 3
(c74, c76, strptr, 38, 135, 42, off, 20, 0) — the string left-padded to width 15.

**jt249e DONE — prompt/list finalization (0x369c-0x370e).** jt423(buf52) →
jt452 shape 6 draws the prompt centred (x = (38-len)*4/2 + 8006 off 8004);
jt179(2) brackets; jt148(g_a5(-13952), buf72, 1) installs the list; jt452 shape
7 registers the &jt245 selection hook; jt79(); then clrb f7 (folded into the
deferred next block's setup). Now the whole static screen (frame + grid outlines
+ labels + cursor + prompt + list) is drawn.

**STRUCTURE CORRECTION.** jt249's MAIN body is 0x333a-0x3c16 (unlk/rts @0x3c14).
The region 0x3c18-0x4022 is a HELPER CLUSTER, not jt249's tail: l3c18 (0x3c18),
l3c5e (0x3c5e), l3cbe (0x3cbe), l3d90 (0x3d90), l3e1e (0x3e1e-0x4022). So the
"second JT[3] @0x3e5a" and the "jt1161 tail 0x3e6e-0x4022" are INSIDE l3e1e, not
jt249. That resolves the fp@(10) puzzle: l3e1e's `movew fp@(10)` reads its OWN
word arg, not jt249's desc pointer. jt249's main body has exactly ONE JT[3]
(@0x3862).

**jt249f DONE — l3c5e + l3d90 marker/cell helpers.** l3c5e(short idx, uchar
*arr): l3c18 coords → jt1161 4x4 box at (colX, rowY+60) fill=arr[idx]. l3d90
(short idx): l3cbe coords, o6=o2+16; vertical (jt1200()==3) shifts axes +12,
else a jt1161 box (o6-4,o4+4,o6,o4+8) fill=idx; then jt357(o2,o4,idx,3) places
the content and jt1134 commits. Both DCE'd (their redraw-block callers are
deferred).

**jt249g DONE — l3e1e, the highlight-outline helper.** l3e1e(short idx, short
fill): draws four thin jt1161 bars outlining grid slot idx. Vertical layout
(jt1200()==3) first nudges the column axis via JT[3] @0x3e5a on (idx-1)%5
(jt3_extract: min=0 max=4; cases 0/1 → o4+1, case 4 → o4-1, 2/3/default → none),
then draws off (o2, o4, o6=o2+16, o8=o4+17); horizontal draws off (o2, o4, o6,
o8=o4+12). fill = fp@(10) = the caller's word arg (15) — this is the fp@(10)
that read as a word, NOT jt249's desc. All 8 bar-coords verified push-by-push.
The whole redraw helper cluster (l3c18/l3c5e/l3cbe/l3d90/l3e1e) is now lifted.

**jt249h DONE — the dynamic redraw block (0x3714-0x37ea).** for i=0..15 l3d90(i)
draws each slot's cell; l3e1e(f8, 15) outlines the current selection; for i=1..16
l3c5e(i, arr94) draws the per-slot markers (horizontal layout only, jt1200()!=3);
then two "%ss:" column headers (jt1089 @8014,8096 / @8014,8010 with g_a5(-10744)/
(-10816)) and the active-field label (l3c18 slot 8 -> jt1089 @c74+4,c76+10 of the
-11284 buffer + 7). f8 is consumed here. All 5 helpers now have live callers.

**jt249i DONE — the modal input-loop skeleton (0x37ee-0x3c16).** jt117 + the
one-time jt1130 setup; the fetch loop (l2d3e=JT[456] event poll, retry via
jt1067); jt152 key translate; the Return(13)→fp2=0 / ESC(27)|backtick(96)→fp2=1
preprocess (gated on g_a5(-24139)); the JT[3] @0x3862 nav scaffold (keys 129-136
+ default jt1080); the SHARED selection-apply @0x3ae0 (fp4>=0 && (f10||!f9):
l3e1e(f8,8) unhighlight → f8=fp4 → l3e1e(f8,15) highlight; fp2=-1 to loop); the
L3ba6 loop test (fp2<0 loops, else exits); and the full L3bae *desc repack (low
nibble = 1-fp2, bits 4-7 = f8, bits 8-11 = f77 — mirrors the prologue unpack).
NOTE: jt456 is lifted as l2d3e (both-directions alias trap). DEFERRED: the 8
per-key nav computations (each sets fp4) and the L3b2a pointer/special handling.
jt249's main body is now STRUCTURALLY COMPLETE.

**jt249j DONE — the 8 JT[3] navigation arms.** Each computes the new slot fp4
from f8/f77/f6/f9/f10 then falls into the shared apply @0x3ae0. Direction pairs:
129/136 = up (f8-5, top row clamps to 0, f8==0 wraps to 13), 131/132 = down
(f8+5, bottom row clamps to 0, f8==0 -> 3), 130 = right (f8+1)&15 / 134 = left
(f8-1)&15 — both toggle the f9 field-mode in the non-f10 path (fp4 = f9?f6:f8),
133 = end (15/16), 135 = home (0/1). The f10!=0 and f9!=0 modes select f8-cell
navigation vs f6-position navigation vs field-toggle; all comparisons keep the
asm's signed(f6)/unsigned(f8-byte) mix. Verified each arm's fp4 assignment
push-by-push against 0x387c-0x3ada.

**jt249k DONE — L3b2a pointer/special block; jt249 is a FULL LIFT.** The fp2<0
handler: fp4<16 -> slot pick (if f8!=fp4: l3e1e(f8,8) unhighlight, f8=fp4,
l3e1e(f8,15) highlight); fp4==16 || fp4>32 -> the g_a5_word(-10372) key check
(13->fp2=0, 27->fp2=1); 17..32 -> field cycle (f77 = arr94[f6]). Slot-pick and
field-cycle leave fp2<0 so the loop continues; the key check can set fp2>=0 to
exit.

**jt249 COMPLETE** — full lift across 11 commits (jt249a prologue → jt249k). The
CODE 2 event-editor cluster now has jt254, jt253, jt248, and jt249 all lifted;
helpers l3c18/l3c5e/l3cbe/l3d90/l3e1e added. Whole cluster DCE'd (unreferenced
until the CODE 22 design-editor dispatcher @0x0096 is lifted), codegen holds
1889.

## jt258 (l0004, CODE 2+0x0004 → 0x20ee, frame -8, ~2100 insn) — STARTED

The event-editor MAIN — the largest CODE 2 function. Signature confirmed from
CODE 22+0x0146 (pushes `*(struct+2)` word, `&struct[8]`, `&struct[178]`):
**short jt258(short cmd, long *desc, void *out)** — desc = &struct[8] (same
convention as jt248/jt249), out = &struct[178] (the result record). NAMING:
existing l0004 is CODE 4's menu stub; this CODE 2 function is named `jt258`.

**Top-level dispatch = JT[1] @0x26** on `cmd` (jt1_extract: 3 cases + default):
- **case 5 → 0x003a** — the main open/edit path (the bulk): re-unpacks *desc into
  the out record, then a JT[3] @0xf4 sub-tree (1 case + default) fanning to the
  helper cluster (0x42a, 0xade, 0x2156, 0x910, ...).
- **case 11 → 0x0204**, **case 10 → 0x02a4**, **default → 0x030e** — smaller arms.

**Sub-switches (14):** JT[1] @0x26/0x2c8/0x32a/0x34c; JT[3] @0xf4/0x1d2/0x228/
0x376/0x480/0xc60/0xd2a/0xf30/0x13e8/0x1d80. **Helper cluster** (~50 pc@ targets,
mostly 0x1ad2-0x2156): heavy hitters 0x1ad2 (21×), 0x1b30 (9×), 0x1af8 (7×),
0xa32/0x207c (6×). Top JT leaves: jt444 (a5@3586, 17×), jt1089 (9×), jt1080
(5×), jt394 (3×).

**jt258a DONE — prologue + JT[1] top-level scaffold.** Early-exit on out==NULL
(return 0 @0x426), *out+0 = cmd store, and the 4-arm JT[1] dispatch scaffold
(arms deferred, returns 0 placeholder). DCE'd, codegen 1889.

**jt258b DONE — case-5 unpack + JT[3] @0xf4 scaffold.** The field unpack (0x3a-
0x82): rec[2]=cmd, rec[12] = (rec[12]&0x3FFF)|(*desc&0xC000), rec[4]=*desc>>16,
f3=*desc&0xFF. Then the bit-15 branch (*desc & 0x8000 -> l042a(rec, f3, (d>>8)&7,
(d>>11)&7), a PROBE stub for now). Else: f2=rec[10], rec[10]=0, f4=(*desc&0x3F00)
>>8, and the JT[3] @0xf4 (jt3_extract: cases 0/1 -> 0x102, default -> 0x1cc)
scaffold. NOTE: case-5 arms converge at 0x31a (post-process: *desc&=0xFFFF0000
then JT[1] @0x32a on rec[0] + JT[1] @0x34c on rec[10]&63) before the 0x426 rts —
that post-process is DEFERRED, so jt258 still returns the 0 placeholder.

**jt258c DONE — the L0102 rec-flag bit-test chain (JT[3] @0xf4 case 0/1).** A
linear cascade; the first set flag picks the handler: rec[13] bit 6 -> l0ade;
bit 5 -> l2156, and if it returns 0, l0910; rec[12] bit 0 -> l222c; rec[13] bit 2
-> f2=(signed char)l22b6, and if f2<=0, l09d6(rec, f2<0?1:0); rec[13] bit 1 ->
l0722; else -> l0622(rec, f4). All 8 handlers added as PROBE stubs (l2156/l22b6
return the status the chain branches on). Byte-addressed bit tests preserved
(rec+13 = low byte of the rec[12] short, rec+12 = high byte).

**jt258d DONE — the L01cc default arm (JT[3] @0xf4 default).** dispatch on
(f2>>8) via JT[3] @0x1d2 (jt3_extract: min=2 max=3): cases 2/3 -> l0524(rec,
*desc&0xFF, f4); default -> straight to the 0x31a post-process. l0524 added as a
PROBE stub. Case 5's JT[3] @0xf4 sub-tree is now fully structured (both arms
lifted); all paths converge at the deferred 0x31a post-process.

**jt258e DONE — the 0x31a shared post-process + top-level default arm + real
return.** 0x31a is the function's COMMON EPILOGUE, not a case-5 tail: verified
every command arm converges here (bras/braw 0x31a from case 5 @0xc6..0x200,
case 11 @0x292/0x2a0, case 10 @0x2e6/0x2fe/0x30c; the top-level default @0x30e
falls through). Restructured accordingly: the 0x31a repack now lives AFTER the
`switch (cmd)`, shared by all arms. Body: `*desc &= 0xFFFF0000` (0x31e) then
JT[1] @0x32a on rec[0] (jt1_extract: cases 10/11/5, default). case 10 (0x33e) =
degenerate JT[1] @0x34c (0 cases) -> `*desc |= (long)(short)rec@10`; case 11
(0x368) = JT[3] @0x376 (min=1,max=1): case 1 -> `*desc |= rec@10&15`, default ->
`*desc |= (short)rec@10`; case 5 (0x3a6) = the only arm doing the big repack
(k=(rec@10&0x3f00)>>8; if k∈{2,3} rec[6]=rec@10&0xff; then `*desc |=
(u16)rec@12&0xC000`, `*desc |= rec@10&0x3fff`, and if rec[12] byte bit5
`*desc |= 0x8000`); default -> no repack. The top-level default arm @0x30e
(`rec[0]=rec[2]`) is now lifted too. Return changed from the `0` placeholder to
`rec[0]` (0x420-0x424). Codegen holds 1889 (DCE'd), tests 129/1. NEXT: (2) cases
11 (0x204) / 10 (0x2a4); (3) the case-5 helper stubs lifted bottom-up
(l042a/l0ade/l2156/l0910/l222c/l22b6/l09d6/l0722/l0622/l0524).

**jt258f DONE — top-level case 11 (0x204).** rec[0]=rec[2] (0x20c), save+clear
rec@10 (0x214/0x21e), then JT[3] @0x228 (jt3_extract: min=1 max=1; case 1 ->
0x234, default -> 0x31a) on rec@10&15. case 1: if *desc&15 -> stash (*desc&0xff0)
>>4 into rec[14], call l1e8a(rec[19], rec[14]), rec@10=rec[19], retype rec[0]=5;
else l07c6(rec). Two new PROBE stubs added: l1e8a(short,short) (2 record-byte
word args, ABI-ordered rec[19] then rec[14]) and l07c6(void*). Codegen 1889,
tests 129/1. NEXT: case 10 (0x2a4), then the case-5/case-11 helper bodies.

**jt258g DONE — top-level case 10 (0x2a4).** Same shape as case 11: rec[0]=rec[2]
(0x2ac), save+clear rec@10 (0x2b4/0x2be), then JT[1] @0x2c8 (jt1_extract: keys
10 -> 0x2dc, 4 -> 0x2e8, 6 -> 0x2e8, default -> 0x31a) on rec@10&63. case 10 ->
l07c6(rec); cases 4/6 (shared arm) -> if *desc&15 then l0910(rec) else
l09d6(rec,0). All three helpers were already stubbed (l07c6 from jt258f, l0910/
l09d6 from jt258c). No new stubs. Codegen 1889, tests 129/1.

**jt258 TOP-LEVEL DISPATCH COMPLETE.** All four command arms (case 5 open/edit,
case 11, case 10, default) + the shared 0x31a epilogue are lifted; jt258 returns
a real rec[0]. What remains is the HELPER BODIES — the ~12 PROBE stubs still
faithful-lift bottom-up: case-5 (l042a/l0ade/l2156/l0910/l222c/l22b6/l09d6/
l0722/l0622/l0524) + case-11/10 (l1e8a/l07c6). These are event-record
serialization primitives; several are shared across the case arms.

**Helper bodies — lifting bottom-up (leaves first).** The 12 direct stubs each
call a deep subtree (l0a32/l0ade/l207c/l1ad2/l1b30/l174a/l1c10... none of the 12
is a shallow leaf), so the faithful order is deepest-primitive-first. True leaves
found (no pc@ calls, only a5@ JT-slot calls or none): l1ad2, l1af8, l1b30, l207c,
l6cc. **jt258h DONE — the event-array element-pointer accessor pair l1ad2/l1af8.**
Both operate on an array header {count@0 word, valid-flag@2 word; elements at
header+4, 4-byte stride}. l1ad2(p) = &elem[count] (append slot; NULL if word@2<0);
l1af8(p,mode) = &elem[count-1] (top slot; mode!=0 also decrements count in place =
pop, mode 0 = peek; NULL if count<=0). Confirmed callsite l0a32 @0xa66/0xa7c does
l1ad2(rec+22)/l1af8(rec+22,0) — the array header lives at rec+22. Both DCE'd
(callers still stubs); codegen 1889, tests 129/1. NEXT leaves: l1b30, l207c, l6cc.

**jt258i DONE — the remaining leaf primitives l1b30 / l207c / l6cc.** l1b30
(0x1b30) = the push counterpart of l1af8: &elem[count+1], mode!=0 increments
count in place (full when count>=capacity@2) — completing the array-accessor
trio (l1ad2 append-slot, l1af8 pop-top, l1b30 push-next). l207c (0x207c) seeds
the cursor/selection A5 state from (a,b): -12191/-12190 = anchor/current byte,
-12194/-12196 zeroed, -12192 bit0 = range-active flag (confirmed 2-word-arg ABI
via caller l910 @0x98a). l6cc (0x6cc) = "encounter" retype: rec@12 |= 0x82, then
b=jt228() (a5@1858 = JT[228], already lifted); if b -> rec[19]=b, rec@10=1,
rec[0]=11; else rec[19]=0, rec@10=10, rec[0]=10. All leaves DCE'd; codegen 1889,
tests 129/1. With these the bottom leaf layer is done; NEXT = the mid-level
helpers that call them (l0a32, l09d6, l0910, l06cc, l19f2, l1ba4, l1cf0, ...),
then the direct stubs.

**jt258j DONE — event-array structural primitives (2nd leaf tier).** Five more
pure leaves on the same array header {count@0, tail-index@2; elem@+4 stride 4}:
l19f2 (append *src at ++index@2, bounds -1..28), l196c (ordered insert at
count@0, shift tail up, index@2++), l1934 (secondary-cursor reset word@24=
word@22), l1b70 (rewind &elem[0]; mode resets count@0=0), l1ba4 (&elem[index@2];
mode commits count@0=index@2). CLASH: a CODE-16 spell helper already owns the
bare name l1ba4 -> lifted the CODE-2 one as **l1ba4_c2** (_cNN rule). All DCE'd;
codegen 1889, tests 129/1. The full array-primitive layer (l1ad2/l1af8/l1b30/
l19f2/l196c/l1934/l1b70/l1ba4_c2) is now lifted. NEXT mid tier: l1cf0 (l1ad2/
l1af8 + JT[3]@0x1d80), then l174a/l1c10/l178c/l169c/l1be4/l1e38 (all callees now
lifted or nearly), working up toward l0a32 and the direct stubs.

**jt258k DONE — l1cf0 (commit event-array entry into the area maps).** Reads the
append slot (l1ad2 rec+22) for type@0/value@1/coords@2,3 and peeks the current
top (l1af8 mode 0). top!=NULL: must be type 0 -> write value into the 20-stride
record map at g_a5_long(-13038) at (top[1]-1)*20+slot[2]; else return 0. top==
NULL: JT[3]@0x1d80 (jt3_extract min=1 max=4) on the type byte -> writes into the
current-area block g_a5_long(-12300): case1 cell-array stride6 +294 idx=col*
width[3]+row, case2 +48/stride4, case3 +80/stride4, case4 +110/stride1; default
+ all cases return 1. First mid-helper whose callees were all pre-lifted (l1ad2/
l1af8). Codegen 1889, tests 129/1. NEXT: l174a/l1c10/l178c/l169c/l1be4/l1e38.

**jt258l DONE — mid-tier l174a / l1c10 / l1e38.** l174a (0x174a): append the
staged coord rec+18 (l19f2), commit count (l1ba4_c2 mode 1), flush to area maps
(l1cf0). l1c10 (0x1c10, rec, mode word): propagate along the record-map link
chain at g_a5_long(-13038) (20-byte rows) — seed a 4-byte entry {0,cur,mode,pad}
from the append slot, walk map[cur-1][3] links (type becomes 3 after hop 1)
appending via l19f2 until link 0; returns 0 on mid-chain overflow, else appends
the terminal entry. Its fp@(-2) "row" byte is write-only (dead) -> omitted. l1e38
(0x1e38): flush head elem (l1b70 mode 0) with elem[0][1] suppressed across l1cf0,
then restored. CLASH: l174a calls the renamed l1ba4_c2. All DCE'd; codegen 1889,
tests 129/1. l1c10 unblocks l178c + l1be4. NEXT: l178c (needs l1a36 leaf),
l1be4, l169c (a5@3282), then l0a32 and the direct stubs.

**jt258m DONE — l169c / l1a36 / l1be4.** l169c (0x169c): ordered-insert staged
coord (l196c) then splice the record chain — jt406(slot,next,4) [swap ABI: Mac
top=next=src, slot=dst -> port jt406(dst=slot,src=next,4)] keeping slot's value
byte, set map[value-1][3]=next[1] at g_a5_long(-13038), reset next[0]=0/next[2]=3,
flush via l1cf0. l1a36 (0x1a36, leaf): remove elem at count@0, shift tail
[count+1..index@2] down, index@2--, recompute count@0=jt397(0,jt413(count,index));
guards index@2<0/count@0<0/count@0>=30. l1be4 (0x1be4): l196c insert + l1c10
propagate(type 3). All JT slots (jt406/jt397/jt413) pre-lifted. Codegen 1889,
tests 129/1. l1a36 unblocks l178c (last mid-tier blocker). NEXT: l178c, then
l0a32 and the 12 direct stubs.

**jt258n DONE — l178c (record unlink + chain repair), full lift ~424 insn.**
Stage the append slot into rec+18 (jt406 swap ABI), clear map[v-1][3]=0 unless
rec[12] bit5, l1a36 remove the element, then rebuild: if a next entry existed
re-stage rec+18 into the new append slot and l1cf0 flush; else when rec[18]==0
clear neighbour cell map[next[1]-1][rec[20]] and set rec[8]=1. Two jt406 copies
(both swap-ABI verified: call1 Mac src=slot -> port jt406(rec+18,slot,4); call2
Mac src=rec+18 -> port jt406(nxt,rec+18,4)). Returns the removed value byte on
success, 0 on failed remove/flush. Uses l1ad2/l1b30/l1934/l1c10/l1a36/l1cf0 (all
lifted) + jt406. Codegen 1889, tests 129/1. **MID TIER COMPLETE** — the array +
chain-edit layer is fully lifted. NEXT: the top helpers l0a32/l0ade/lce2/l1568/
l1084/l1686/l1e8a, then the 12 direct stub bodies.

**jt258o DONE — l1e8a (1st direct stub filled) + l1954 / l1686 leaves.** l1e8a
(0x1e8a): map[a-1][0] = b at g_a5_long(-13038) (20-byte rows), no-op if b==0 —
this is the FIRST of the 12 direct case stubs to get a real body (case-11's
l1e8a(rec[19], rec[14])). l1954 (0x1954, leaf): reset an array header (count@0=0,
index@2=-1). l1686 (0x1686): l1954(rec+22) — reset the record's event-array.
Both leaves DCE'd; l1e8a DCE'd transitively (jt258 unreferenced). Codegen 1889,
tests 129/1. NEXT: l1084 (a5@3586=jt444 + array prims), l1686 caller, then
l0a32/l0ade and the remaining 11 direct stubs.

**jt258p DONE — l0910 (2nd direct stub filled).** All its pc@ callees were
lifted (l1ad2/l1b70/l1ba4_c2/l1e38/l178c/l207c/l1cf0) + jt1080 (a5@8674, lifted).
l0910(rec): if the append slot is both elem[0] and elem[index@2] (single-entry
array) flush it (l1e38, flag=1); remove the top record (l178c); on a non-zero id
seed the cursor (l207c r,0), retype the record to 5 with rec@10 = id|0x500,
return 1; else flush pending (l1cf0 if flag) + jt1080(), return 0. Sig changed
void->short. Codegen 1889, tests 129/1. **2 of 12 direct stubs filled** (l1e8a,
l0910). NOTE for l1084/l0ade: a5@3586=jt444 is a 4-arg Dialog helper but the
CODE-2 grid-draw sites push only 2 words -> ABI care needed there (deferred).
NEXT: l09d6/l07c6 need l0a32; l0a32 needs l0ade (big). Or fill l2156/l22b6.

**jt258q DONE — the multi-select ring trio l2156 / l222c / l22b6 (3 direct stubs).**
All operate the selection ring at g_a5_buf(-12190) (cursor -12196, end/count
-12194, current byte -12191, anchor -12193). l222c (0x222c, LEAF): step the ring
— room -> advance cursor, load next entry, pack (byte|0x600) retype 5; else close
ring (reseed -12194 from rec[16], cursor=0, pack (count<<8)|6 retype 10). l2156
(0x2156, calls l1ad2+l222c): clear rec[13] bit5, patch map[slot[1]-1][3]=rec[15]
if differing, then on span (-12194 - -12196): <=0 retype 4/10; bit5 -> latch +
l222c; else pack (span<<8)|6 retype 10; ALWAYS returns 1 (so the L0102 chain's
`if(l2156==0) l0910` never fires via this arm — faithful). l22b6 (0x22b6): gated
advance via jt229(entry)+jt321 (both lifted); returns signed -1/0/1 (chain casts
(signed char)). Forward-declared l222c before l2156 (defined-later in block).
Codegen 1889, tests 129/1. **5 of 12 direct stubs filled** (l1e8a, l0910, l2156,
l222c, l22b6). Remaining 7: l042a, l0ade, l09d6, l0722, l0622, l0524, l07c6.

**jt258r DONE — l0722 (door/wall edit commit, 6th direct stub).** l1e8a(rec[19],
rec[14]) records the map row byte, jt321 tick, then: if rec[13] bit0 set AND
l1b30(rec+22,1) push slot full -> grow (l174a + l1c10 type 3), else ordered-
insert+splice (l169c). Finally clear rec[13] bit1, set bit4, pack (rec[19]|0x200)
into rec@10, retype 5. All callees lifted (l1e8a/jt321/l1b30/l174a/l1c10/l169c);
forward-declared l1e8a (case-11 block is later). Codegen 1889, tests 129/1. **6 of
12 direct stubs filled**. Remaining 6: l042a, l0ade, l09d6, l0622, l0524, l07c6 —
l0622 needs l0a32; l09d6/l07c6 need l0a32; l0a32 needs l0ade (the big grid helper,
jt444 2-vs-4-arg ABI question); l0524/l042a are the param-format/high-bit handlers.

**jt258 helper-tree note (jt444 RESOLVED):** a5@3586 = JT[444] entry (A5+0xE00)
+2 = jt444(item,a,b,c), the 4-arg DLItem-method dispatch (CODE 3+0x3056). The
CODE-2 grid-draw sites (l1084/l0ade) push only 2 words -> the dispatched method
uses just `a`; when lifting those, call jt444(item, a, 0, 0) with a comment (b/c
are unused-garbage on the Mac; jt444/its method + jt258 are all DCE'd anyway).

**jt258s DONE — l1fbe (row-cursor step, deep sub-helper).** All callees lifted
(l1af8/l1934/l1c10/l1b30). l1fbe(rec,dir,flag): no-op if rec[7]==0; flag==0 first
pops the array top (bail if empty); dir!=0 decrements rec[7] wrapping 0->-12193
anchor, dir==0 increments wrapping past -12194 count->1; then l1934 reset, l1c10
propagate with g_a5_buf(-12090)[rec[7]], l1b30 push. Caller l1eb2 @0x1ef0 =
l1fbe(rec,0,1). Codegen 1889, tests 129/1. Unblocks l1eb2 (l1fbe+l1ad2 both
lifted). NEXT ready sub-helpers: l1eb2, lefe (JT[3]@0xf30), l12e8/l31cc (JT-slot
only — verify jt1161/jt366/jt394/jt423/jt358 lifted), building toward l0ade.

**jt258t DONE — l1eb2 (row-cursor finalize / row search).** Callees l1ad2/l1fbe
lifted. Two paths: (a) rec[8] && rec[7] -> step cursor down (dec rec[7], wrap
0->-12193, l1fbe(rec,0,1)) when count -12194 > 0; zero rec[7] if -12194<=1. (b)
else when -12194 > 1 -> scan g_a5_buf(-12090) from index 1 for the first row
matching the append slot's coord slot[2] (l1ad2), leave rec[7] on the match or 0
if none. Always clears rec[8] at exit. Codegen 1889, tests 129/1. l1eb2 unblocks
nothing new directly but is a callee of l0ade's tree. NEXT: lefe (JT[3]@0xf30),
then l12e8/l31cc (JT-slot only), l1568/l1084/lce2, then l0ade -> l0a32 -> stubs.

**jt258u DONE — lefe (key -> nav-action translator).** Callees l1ad2/l1b70/
l1ba4_c2 lifted. lefe(rec,key): Return(13)->3, Esc(27)/`(96)->0, else JT[3]@0xf30
(jt3_extract min=130 max=136) on 130..136: 130/134 -> 6 if rec[7] else -1; 132 ->
2 if append!=elem[index@2] else -1; 133 -> if != commit(l1ba4_c2 m1) -2 else -1;
135 -> if append!=elem[0] reset(l1b70 m1) -2 else -1; 136 -> 1 if append!=elem[0]
else -1; 131/default -> -1. Always clears g_a5_byte(-24139); returns the action.
Codegen 1889, tests 129/1. lefe unblocks lce2. NEXT: lce2 (lefe+jt1080+JT[3]+
array prims), l12e8/l31cc (JT-slot), l1568, l1084, then l0ade -> l0a32 -> stubs.

Next target after jt249: jt258 (l0004, 2808, the event-editor MAIN — skeleton-
then-fill, last).

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
