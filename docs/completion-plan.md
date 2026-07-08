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
| ~~jt896~~ | 19 | ~~666~~ 564 | temple donation/tithe screen — ✅ DONE (+l3fd2 numeric field, +jt939=l4218 LE write) | — |
| ~~jt916~~ | 12 | **392** | Training Hall key handler — ✅ DONE (level-2; + jt927 cursor redraw) | training-hall-menu-keystone |
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
- ~~D5  CODE 12 jt916 (392) + jt927~~ ✅ DONE (jt916 level-2 handler, jt927 cursor
        redraw); Training Hall stubs jt919/931/933 remain (leaf stubs)
- D6  CODE 4 jt1206 — **VERIFIED 2026-07-07: a real ~52 B CODE-4 dispatcher**
       (l04a0/l04f0/l0370, returns fp@14 pass-through), NOT HAL-superseded.
       Small but has 3 local deps to check first.

### Phase E — stub + small-missing cleanup

**2026-07-07 verification (before starting E):**
- **jt373** (CODE 8 +0x0004) — NOT superseded/aliased.  A real **~2362 B widget
  dispatcher** structurally identical to jt335's LDEF (NULL-guard rec, data=
  rec[8], JT[1] switch on the message).  A genuine giant — its own multi-step
  lift, not a stub.  (The l0004 at boot.c:19014 is a *different* CODE-4/6 menu
  dispatcher — collision; do not repoint jt373 to it.)
- **jt426 / jt432 / jt458** (CODE 3) — CONFIRMED **SUPERSEDED / dead**: Mac
  indexed-catalog OPEN / READ-NEXT / volume-enum, whose only callers (jt990/
  jt991/jt12) the port reimplements over GEMDOS Fsfirst/Fsnext/boot.c.  Never
  reached — count as done.
- **jt428** (CODE 3+0x4868, "OPEN the print job") — VERIFIED 2026-07-07
  **Printing-Manager-unmappable, HAL-moot** (the jt1065 Pack15 / jt1159 Palette
  class).  Every Pr* call (PrOpen/PrValidate/PrStlDialog/PrJobDialog/PrOpenDoc/
  PrError/PrClose) funnels through the PrGlue trampoline `L551c` → trap **0xA8FD
  (_PrGlue)**; GetFNum is trap 0xA900.  The Falcon/TT have **no Printing
  Manager**, the port ships **no print backend** (zero PrGlue refs in compat/ or
  platform/; no Printing row in toolbox-mapping.md — never in scope), and the
  whole print subsystem (jt428 → jt1075 → jt256/jt1074/jt1072, all
  `__attribute__((unused))`) is **dead on the port**: `-9162` (the TPrint
  record) never opens, so jt433 emit / jt434 close / L4806 rollover stay inert
  as they already document.  The faithful port body is the documented no-op — a
  C body would be either no-op zero-stores (gaming the classifier) or calls into
  unshimmed traps.  Moved to the `NOOP` set in jt_progress.py (done); reaching a
  real lift would need an out-of-scope printer backend.

**The 15 "leaf" stubs are NOT leaves — each roots a multi-function SUBTREE**
(scoped 2026-07-07).  They stay stubs precisely because their sub-helpers are
unlifted, several across (CODE,offset) collisions.  Real per-subtree work, not a
batch.  Rough map (√ = sub-dep lifted, ✗ = missing sub-dep):
- jt1008 (5, 32 B) → l0ab6 (280 B, box-panel draw: jt1141/jt1161/jt394 "%r"/
  l0334=jt1089√) — intricate centre/truncate coord math.
- jt587 (15, 76 B) → l08ba (46 B) → **l0006_c15** (84 B, 3-arg modal proc runner;
  ✗ collides with a void l0006 at boot.c:48620).
- ~~jt985 (5, 80 B, "play song N")~~ ✅ DONE 2026-07-07 — range-checked song
  play. Full lift: l0f1e (done via jt1081) + l11a2 (song-loader: -4770/-4756
  voice-table walk arming the 5×14-byte mixer records at -4848) + l0fc4
  (period*105/7200 reload calc). m68k big-endian = Mac, so resource words read
  natively. (l0f48/l0faa were already lifted, not ✗.)
- ~~jt1081 (5, 138 B, 4 sites)~~ ✅ DONE 2026-07-07 — the jt69 fatal-error
  teardown chain.  Full lift: the only real sub-helper is l0f14 → l0f1e
  (5×14-byte sound-table reset at -4848 + jt1151) + jt1127; L27bc/L35f8(CODE5)/
  L01ac are bare rts and jt1156/jt1119 are the bare-rts NOOP class — elided (as
  jt69 elides l4d7a).  Live releases: jt466/jt1114/l0f14/jt1158.
- jt919 (12, 152 B) → l192c/l19d4/l1aea. ✅ DONE.
- **SOUND-DRIVER CLUSTER (DEFERRED 2026-07-08 per user — do after the other 5).**
  jt965 / jt974 / jt1064 (all CODE 5) = the digitized-sound engine, NOT clean
  transcriptions.  jt965 (5+0x7dee) is the LIVE sfx path (-17444=1 on the port,
  so dispatcher cmds 3-15 route here) → l7ee0 (5+0x7ee0), which bottoms out in
  the Mac **Sound Driver**: L5716/L59ee are Device-Manager `_Write`/`_Read` trap
  glue (`PBWrite`/`PBRead` to the `.Sound` driver, refnum -4).  l7ee0 fills a DM
  param block (-3138) with the sample buffer (snd+8), rate (snd[0]*35944/12207
  reload), duration (snd[2], clamped to a 740-multiple) and PBWrites it.  The
  faithful port maps this to the Falcon DMA sound HAL (`plat_sound_play_mono8`)
  via a NEW low-level compat/sound.c primitive (engine→compat→platform) + a
  Mac-trace pass to pin the rate/format (jt964 = the group-18 sfx converter).
  Multi-file HAL integration — its own session.  l0088/l37aa/jt468/jt1154 done.
- jt365 (8, 460 B) — all-JT file/catalog (jt990/991/384/130/133/404/419/431/435);
  no locals — the most tractable, but real 460 B logic.
- jt428 (3, 234 B) ✅ NOOP; ~~jt931 (12, 634 B, rule-book copy-protection
  prompt)~~ ✅ DONE 2026-07-08 (full lift + l4280 word deobfuscator; -5702
  20-byte challenge-record table; g_22231 widened to [5] for the -22227 flag;
  gate-dormant on HEIRS so boot unchanged); jt933 (12, take-commit); jt974 (5,
  632 B).
- LARGE (real functions, not leaves): jt955 (21, 1014 B), jt1064 (5, 2076 B),
  jt1144 (4, 1236 B), jt1178 (4, 1710 B).

Sequence each as its own subtree lift (bottom-up, collisions suffixed `_cNN`),
verifying against the disasm.  Finish with a full `jt_progress.py` sweep to
confirm 0 missing / 0 stub / 0 stand-in.

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
