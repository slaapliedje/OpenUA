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

### CODE 22 — design-list rendering (the tractable start)
| JT | addr | ~insn | notes |
|---|---|---|---|
| ~~jt282~~ | l2f24 | 283 | **LIFTED** — entry painter kind 1 (jt278 case 1) |
| ~~jt286~~ | l2aaa | 366 | **LIFTED** — entry painter kind 0 (jt278 case 0) |
| ~~jt281~~ | l329c | ~120 | **LIFTED** — entry painter kind 2 (jt278 case 2) |
| jt??? | l347a | ~200 | entry painter kind 3 (jt278 case 3) — **only one left**; check JT id |

These four painters (l2aaa/l2f24/l329c/l347a) are dispatched by the
already-lifted **jt278** (CODE 22+0x294e) on the entry's kind byte. The trio
(kinds 0/1/2) is DONE; **l347a (kind 3) is the last** — check its JT mapping,
then the design-list paint path is complete.

### CODE 2 — the design-record editor ("recorder")
| JT | addr | ~insn | notes |
|---|---|---|---|
| jt258 | l0004 | tiny | segment entry (rts / thin dispatcher — check first) |
| jt246 | l311a | 161 | record-field painter (jt1089/1161/1200/358/367/394) |
| jt254 | l4c5a | 367 | jt1076 modal + jt201/207/358/384/394 |
| jt253 | l44cc | 569 | JT[1] switch dispatcher (jt273/317/319/320/325) |
| jt248 | l26aa | 773 | list editor (jt117/147/167/168/169) — own session |
| jt249 | l333a | 1025 | big dispatcher — own session |

### CODE 10 — module viewers (editor-adjacent)
| JT | addr | ~insn | notes |
|---|---|---|---|
| jt269 | l0004 | tiny | segment entry (check first) |
| jt267 | l1a14 | 129 | viewer helper; local deps L116a/L2ebe (verify) |
| jt264 | l6316 | 203 | jt135/356/361/370/389 |
| jt265 | l65be | 224 | jt1026/1030/1032/1033/1086/1089 |
| jt270 | l3262 | 294 | jt1067/1089/108/112/117/148 |
| jt266 | l1bc2 | 1692 | big — own session |
| jt259 | l368a | 2757 | the giant — own session |

### CODE 11 — area/geo editor (continuing earlier work)
| JT | addr | ~insn | notes |
|---|---|---|---|
| jt242 | l589a | 1122 | cell-edit committer; JT[3] over 5 unlifted painters (L5a06/L6136/L61c6/L5ee2/L5b0e) — lift painters first |
| jt243 | l0b26 | ~800 | the big CODE-11 dispatcher — own session |

(jt233/jt239/jt244 + l4d24/l49dc already lifted this campaign — see
docs/area-map-wall.md.)

## Recommended attack order

1. **CODE 22 painter trio** (jt282 → jt286 → jt281): deps mostly lifted,
   live dispatcher, coherent. Start with **jt282** (verified clean).
2. **CODE 2 small painters** (jt258, jt246, then jt254): the record
   editor's leaf renderers.
3. **CODE 10 small viewers** (jt269, jt264, jt265, jt270).
4. **CODE 11 jt242** (after its 5 painter locals).
5. **The giants** (jt259/266/249/248/243) — one focused session each.

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
