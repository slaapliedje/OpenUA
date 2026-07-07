# FRUA port — completion plan (session roadmap)

Authored 2026-07-05. The single source of truth for "what's left and in what
order." Each **session** below is scoped to be a self-contained pickup: a fresh
Claude Code session can read this + the named wall doc and start.

## Honest status — where we actually are

The **JT scoreboard reads 1174/1205 done (~97%)** — but that number is
misleading, and it matters that we say so. The remaining 31 entries are not the
easy tail; they include **the single largest functions in the whole codebase**,
deliberately left for last under ADR-0008 ("runtime first").

> **Update 2026-07-06:** Phase A (jt259 art import) and Phase B (the entire
> CODE 2 event editor) are now COMPLETE, and the CODE 22 command dispatcher
> (`l0096`) that routes to them is lifted. The frontier is now **Phase C**
> (CODE 11 GEO 3D-map editor — jt242/jt243) plus the foundational/misc giants.

Two very different truths:

- **The PLAY runtime is essentially done and playable.** Dungeon walk, 3D
  render, combat (CODE 16/18), char-gen (CODE 17), inventory (CODE 9), the
  area-map renderers (CODE 13/14), save/load, Training Hall, the main menu
  (CODE 22) — all lifted and exercised. This is the ADR-0008 phase-1 goal and
  it is met. If "finished" means *play SSI's UA modules*, we are close.

- **The DESIGN EDITOR (phase 2) is still the remaining frontier, but smaller.**
  With the CODE 2 event editor and the jt259 art-import now lifted, the pending
  editor work is ~**10,000 instructions**, concentrated in the **CODE 11 GEO
  3D-map editor** (jt243 5216 + jt242 1298 ≈ 6,500 insn — the biggest single
  block left) plus the foundational **CODE 8 jt335** (2598) and a few smaller
  giants (jt896, jt916, jt1206). If "finished" means *the full design editor
  works*, realistically **12–25 focused sessions**.

So: close on breadth (nearly every function touched), not on depth (the editor
giants are barely begun). Don't let "97%" set the expectation.

## The remaining giants (measured, by size)

| JT | CODE | ~insn | Subsystem | Wall doc |
|----|------|------:|-----------|----------|
| ~~jt243~~ | 11 | **5216** | GEO 3D-map editor — ✅ DONE (Phase C, all 20 arms) | geo-editor-wall |
| ~~jt335~~ | 8 | ~~2598~~ 1396 | list-widget LDEF — ✅ DONE (Phase D1, l3686+l3cb4+l3bfa) | — |
| ~~jt242~~ | 11 | **1298** | GEO 3D-map editor — ✅ DONE (Phase C) | geo-editor-wall |
| jt896 | 19 | **666** | character sheet / party container | — |
| jt916 | 12 | **392** | Training Hall | training-hall-menu-keystone |
| jt1206 | 4 | **314** | display low-level (may be HAL-superseded — check first) | — |

**Phase C (GEO editor) + Phase D1 (jt335) DONE (2026-07-07):** jt243 — all 20
JT[3]@0x0b48 tool arms + finalize + subtree — completes the CODE-11 GEO editor
(jt242+jt243).  jt335 (l3686) — the CODE-8 list-widget LDEF dispatcher — plus its
two missing helpers l3cb4 (viewport layout) and l3bfa (row paint); its other
callees were already present (l34d6, l3416, jt332=l4a16).  **The whole CODE-8
list/menu widget cluster is now DONE (D1a-h):** jt334 (l3f2e TRACK core) + jt335
(l3686 dispatch) + jt336 (l45c6 PAINT core) are all full lifts, along with every
helper — l3cb4, l3bfa, l4ede, l43f6, l4cb4, l4cb4_row_rect (l33ce was already
lifted; l34d6/l3416/l3266/l41de=jt337/l3658=jt333 pre-existing).  Faithful jt334
(-1 = "keep tracking") now makes the l3d1a GEO pulldown branch + jt335 case-3
`do-while(r<0)` loop live — both were dead against the old stub's 0.  ALIAS_LIFTED
updated for jt334/jt336; jt_progress now 1180 done / 15 stub / 10 missing.

**DONE since this plan was authored (2026-07-06):** jt259 (art import, 2732 —
Phase A) and the entire CODE 2 event editor — jt254 (422) / jt253 (644) /
jt248 (890) / jt249 (1102) / jt258 (2808) — Phase B. The CODE 22 command
dispatcher `l0096` now routes to them (cmd 6→jt258, 16→jt259, 17→jt254,
3→jt253, 4→jt251, 5→jt250, 7→jt263, 8→jt269, …). The editor is fully lifted but
**dormant (DCE'd)** until jt315's selection dispatch (CODE 22+0x5180/0x5266) is
wired to call `l0004_22` (which exists, `unused`).

Plus the smaller misc missing (CODE 8 jt334/336/337/371/373, CODE 12 jt916/927,
CODE 19 jt896, CODE 20 jt939) and **17 stubs** (mostly small PROBE leaves: CODE 5
jt965/974/985/1008/1064/1081, CODE 4 jt1144/1178, CODE 12 jt919/931/933,
CODE 3 jt428, CODE 8 jt365, CODE 15 jt587, CODE 21 jt955, CODE 11 jt242/jt243).
Several CODE 3/4 "missing" (jt426/432/458, most of CODE 4) are **SUPERSEDED** —
GEMDOS / the VIDEL HAL replaces them; close them out, don't lift.

## Session sequencing

Work **giants bottom-up** (leaves → mid helpers → sub-giants → main as a
level-2 skeleton, then fill), the method proven on jt266. Order the giants by
(a) unblocking value and (b) warm context. Rough plan — each line is ~1 session
unless noted:

### Phase A — jt259 (art import) — ✓ COMPLETE (2026-07-06)
The MacPaint/PICT art import (jt259, 2732) is lifted (boot.c). Was the "warm
context" continuation of the CODE 10 work.

### Phase B — event editor (CODE 2) — ✓ COMPLETE (2026-07-06)
The whole CODE 2 cluster is lifted, bottom-up per giant, across ~40 commits:
jt254 (422) → jt253 (644) → jt248 (890) → jt249 (1102) → jt258 (2808, the
event-editor main). Scope doc `docs/event-editor-wall.md` written. **Wired** by
the CODE 22 dispatcher `l0096` (this session) — command 6 → jt258, etc.
Remaining B-adjacent task: wire jt315's selection dispatch to `l0004_22` so the
editor is reachable at runtime (currently DCE'd/dormant).

### Phase C — GEO 3D-map editor (CODE 11), ~8-14 sessions  [NEXT / ACTIVE]
The single biggest function (jt243, 5216) + jt242 (1298); both currently PROBE
stubs referenced by `l0096` (cmd 2 → jt243, cmd 20 → jt242). See
`geo-editor-wall.md` (being written in C1).
- C1  scope CODE 11, shared-helper inventory, wall doc; leaves  ← current step
- C2-C4  jt242 (1298) — skeleton then fill
- C5-C14 jt243 (5216) — the big one; multiple skeleton+fill passes

### Phase D — foundational + misc giants, ~4-6 sessions
- D1-D3  CODE 8 jt335 (2598) — foundational UI/file lib (may unblock others;
         consider pulling EARLIER if B/C depend on it)
- D4  CODE 19 jt896 (666) — char sheet/party
- D5  CODE 12 jt916 (392) + jt927 + Training Hall stubs (jt919/931/933)
- D6  CODE 4 jt1206 (314) — first confirm it isn't already HAL-superseded

### Phase E — stub + small-missing cleanup, ~2-3 sessions
The 15 stubs + CODE 8 jt334/336/337/371/373 + CODE 20 jt939 + CODE 5 stubs.
Mostly small leaves; batch by segment. Finish with a full `jt_progress.py`
sweep to confirm 0 missing / 0 stub / 0 stand-in.

## Standing constraints (every session)

- **Faithful lifts only** — real Mac-asm transcription, never gate-flips or
  stand-ins that fake success (`feedback-no-shortcuts`).
- **Alias-check every `defs=0`** against `docs/lxxxx-jt-aliases.md` +
  `jt_progress.py`'s ALIAS_LIFTED before calling anything MISSING.
- **JT[1]/JT[3] switches via `tools/jt1_extract.py` / `jt3_extract.py`** —
  never hand-decode (#122). Raw-decode the case-0 hidden lead-ins.
- **(CODE, offset) name clashes** → suffix `_cNN`.
- **Validation:** the editor is **mouse-gated and unvalidatable headless**
  (Hatari doesn't inject mouse). Verify against the disasm, not the smoke
  harness. Every commit still: `make` (codegen count stable), `make test`
  (129 pass), and a boot smoke to prove the play path is unregressed (dormant
  `__attribute__((unused))` statics ⇒ GCC drops them ⇒ boot provably unchanged
  until the giants are wired in).
- **Commit cadence:** one focused commit per step, push after each.

## Definition of done

1. `jt_progress.py`: 1205/1205 done, 0 stub, 0 stand-in, 0 missing.
2. All 23 CODE segments at **0 pending** in the per-segment table.
3. The design editor drives end-to-end (needs a human at the mouse to validate,
   or a mouse-injection harness — see the run-falcon-port skill gotchas).
4. `docs/subsystem-status.md` shows every subsystem GREEN.
