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
| jt254 | l4c5a | 0x4c5a | 422 | smallest editor screen — **START (B2-B4)** |
| jt253 | l44cc | 0x44cc | 644 | editor screen |
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
| L4c92 | 0x4c92 | JT[207]+JT[397] (both lifted) | B2 — cluster leaf |
| L4dcc | 0x4dcc | JT[207]+JT[397]+JT[3] switch | B2 — cluster leaf |
| L4e3e | 0x4e3e | JT[207]+JT[397]+JT[3] switch | B2 — cluster leaf |
| L514e | 0x514e | (small, frame 0) | B2 |
| L5194 | 0x5194 | JT[193]… (frame −10) | B2 |
| L541c | 0x541c | jt254 entry's direct callee | B3 |
| L4eb2 | 0x4eb2 | calls L4c92/L4dcc/L4e3e + JT[384/394/404/1076/201] | B3 — worker (after its callees) |
| l4c5a | 0x4c5a | JT[358] + L541c — the jt254 ENTRY | B3 — wire last |

Plan:
- **B2** — lift the cluster leaves (L4c92, L4dcc, L4e3e, L514e, L5194); JT[3]
  switches via `tools/jt3_extract.py`.
- **B3** — lift L541c + the L4eb2 −102 worker, then the l4c5a entry; verify the
  whole screen builds.

## Method (same as jt259 / #153 so far)

Faithful transcription from `data/work/disasm/CODE_02.s`. Read the exact stack
pushes for arg order; JT[1]/JT[3] switches via the extract tools (never
hand-decode, #122); check `docs/lxxxx-jt-aliases.md` before treating any lXXXX
as unlifted (three "blocked/needs-trace" flags on jt259 all dissolved that
way). Each leaf/helper = one build + commit; codegen stays 1889, tests 129/1.
Dormant (mouse-gated, unvalidatable headless) — verify vs disasm, not smoke.

STATUS 2026-07-05: B1 (scope) DONE. Leaf audit corrected — all 9 jt254 leaves
were already lifted (the "3 absent" were lXXXX-aliased). B2 STARTED: L4c92
(map-cell glyph picker) lifted. Next B2: L4dcc, L4e3e, L514e, L5194.
