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
| **jt282** | l2f24 | 283 | design-list entry painter, kind 1. **deps clean** (L0674 + jt1089/1161/117/1173/1193/1200/353). Live parent = jt278. **← recommended first lift** |
| jt286 | l2aaa | 366 | entry painter kind 0; ~6 CODE-22 local deps (verify) |
| jt281 | l329c | 390 | entry painter kind 2; ~6 CODE-22 local deps (verify) |

These four painters (l2aaa/l2f24/l329c/l347a) are dispatched by the
already-lifted **jt278** (CODE 22+0x294e) on the entry's kind byte. l347a
(kind 3) is the 4th; check its JT mapping. Finishing the trio completes
the design-list paint path — a coherent, self-contained deliverable.

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

## Status — painter trio 2/3 done

- **jt282 — LIFTED** (2026-07-05). Faithful goto-mirror of 0x2f24..0x329a;
  jt278 case 1 rewired. Dormant (mouse-gated).
- **jt286 — LIFTED** (2026-07-05). kind 0, 366 insn + its 3-local dep tree
  (l0524_c22 / l48b2_c22 / l475e_c22, all lifted this pass; L48ca resolved
  to the already-lifted jt291, L4900=jt273, L05ca=jt293). jt278 case 0
  rewired. Dormant.
- **jt281 (l329c) — NEXT, verified deps-clean (a pure single lift).** kind
  2, ~390 insn. All callees already lifted: L0716=jt306, L22c4=jt308,
  L294e=jt278, L4900=jt273, L475e=l475e_c22, L04d6. Uses -11312 (not
  -11316). **Care-points for the transcription** (new primitives vs jt282/
  jt286 — verify each arg convention first):
  - **jt406**(cell,scratch,16,...) formats a decimal into a fp@(-18)
    buffer (the -12300 design struct's byte 134+cell*16 record).
  - **jt488**(-10604, count) — the width/pad helper feeding the "%*s".
  - **jt1089 "%*s"** (STRS 0x30de) — a width-formatted string draw; confirm
    the port's jt1089 vsnprintf handles `%*s` (width + string args).
  These make jt281 a genuine focused transcription, not rote — hence
  deferred rather than rushed at the tail of a 13-commit session.

The coordinate convention (Mac order, direct push-order transcription) is
settled. Kind 3 (l347a) remains a stub after the trio; check its JT id.
Everything here is dormant/mouse-gated — verify against the disasm, not
the smoke harness.
