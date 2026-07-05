# FRUA port — completion plan (session roadmap)

Authored 2026-07-05. The single source of truth for "what's left and in what
order." Each **session** below is scoped to be a self-contained pickup: a fresh
Claude Code session can read this + the named wall doc and start.

## Honest status — where we actually are

The **JT scoreboard reads 1168/1205 done (~97%)** — but that number is
misleading, and it matters that we say so. The remaining 37 entries are not the
easy tail; they include **the single largest functions in the whole codebase**,
deliberately left for last under ADR-0008 ("runtime first").

Two very different truths:

- **The PLAY runtime is essentially done and playable.** Dungeon walk, 3D
  render, combat (CODE 16/18), char-gen (CODE 17), inventory (CODE 9), the
  area-map renderers (CODE 13/14), save/load, Training Hall, the main menu
  (CODE 22) — all lifted and exercised. This is the ADR-0008 phase-1 goal and
  it is met. If "finished" means *play SSI's UA modules*, we are close.

- **The DESIGN EDITOR (phase 2) is a large remaining frontier.** The pending
  work is ~**18,000+ instructions** concentrated in a handful of giants, almost
  all in the authoring/editor segments (CODE 2 event+map editing, CODE 11 GEO
  3D-map editing), plus the CODE 10 art-import (jt259) and two foundational
  giants (CODE 8 jt335, CODE 19 jt896). If "finished" means *the full design
  editor works*, we are **not** close — realistically **25–50 focused
  sessions**.

So: close on breadth (nearly every function touched), not on depth (the editor
giants are barely begun). Don't let "97%" set the expectation.

## The remaining giants (measured, by size)

| JT | CODE | ~insn | Subsystem | Wall doc |
|----|------|------:|-----------|----------|
| jt243 | 11 | **5216** | GEO 3D-map editor (biggest fn in the codebase) | geo-editor-wall |
| jt258 | 2 | **2808** | event-editor main | event-subsystem-campaign |
| jt259 | 10 | **2732** | MacPaint/PICT art import — **STARTED** | geo-editor-wall |
| jt335 | 8 | **2598** | foundational UI/file library | — |
| jt242 | 11 | **1298** | GEO 3D-map editor | geo-editor-wall |
| jt249 | 2 | **1102** | event editor | event-subsystem-campaign |
| jt248 | 2 | **890** | event editor | event-subsystem-campaign |
| jt896 | 19 | **666** | character sheet / party container | — |
| jt253 | 2 | **644** | event editor | event-subsystem-campaign |
| jt254 | 2 | **422** | event editor | event-subsystem-campaign |
| jt916 | 12 | **392** | Training Hall | training-hall-menu-keystone |
| jt1206 | 4 | **314** | display low-level (may be HAL-superseded — check first) | — |

Plus the smaller misc missing (CODE 8 jt334/336/337/371/373, CODE 12 jt927,
CODE 20 jt939) and **15 stubs** (mostly small PROBE leaves: CODE 5
jt965/974/985/1008/1064/1081, CODE 4 jt1144/1178, CODE 12 jt919/931/933,
CODE 3 jt428, CODE 8 jt365, CODE 15 jt587, CODE 21 jt955).

## Session sequencing

Work **giants bottom-up** (leaves → mid helpers → sub-giants → main as a
level-2 skeleton, then fill), the method proven on jt266. Order the giants by
(a) unblocking value and (b) warm context. Rough plan — each line is ~1 session
unless noted:

### Phase A — finish jt259 (art import), ~6-8 sessions [IN PROGRESS]
Context is warm. Follow the jt259 roadmap in `geo-editor-wall.md`.
- A1  ✓ L4924 + L4970 leaves + roadmap (2026-07-05, committed)
- A2  L6606 (after the jt1170 void-vs-word-arg check) + L4eda + L4f9c
- A3  L66a2 + L67a0 + L6892 + L4924-family tie-ins
- A4  L4cae + L49f8 (mid helpers)
- A5  L509e (mid) + L4970-family
- A6  L42f2 (~528 sub-giant) — 1-2 sessions
- A7  L53b0 (sub-giant) — 1-2 sessions
- A8  L36e0 main skeleton → fill; wire jt259 wrapper

### Phase B — event editor (CODE 2), ~8-12 sessions
The largest cluster. See `event-subsystem-campaign` memory + a CODE-2 wall doc
(create `docs/event-editor-wall.md` in B1). Bottom-up per giant.
- B1  scope CODE 2, enumerate shared helpers, write the wall doc; lift the
      shared leaves
- B2-B4  jt254 (422) + jt253 (644) — the smaller editor screens first
- B5-B7  jt248 (890) + jt249 (1102)
- B8-B12 jt258 (2808) — the event-editor main; skeleton then fill

### Phase C — GEO 3D-map editor (CODE 11), ~8-14 sessions
The single biggest function (jt243, 5216). See `geo-editor-wall`.
- C1  scope CODE 11, shared-helper inventory, wall doc; leaves
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
