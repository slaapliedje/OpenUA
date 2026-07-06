# Subsystem status register — the targeting dashboard

**One place to see every subsystem, whether it's implemented, and where its
scoped "wall" doc lives** (or that it still needs one). Use this to pick the
next target. It is the index over the queue of `docs/*-wall.md` scope docs.

- Companion views: `docs/milestone.md` (high-level accomplished/remaining
  burn-down — START HERE for the big picture), `docs/roadmap.md` (by CODE
  segment / layer), `docs/gap-analysis.md` (by play-flow the player
  experiences), `docs/jt-lift-progress.md` (auto-generated JT counts — the
  source of truth for numbers; rerun `python3 tools/jt_progress.py`).
- Counts as of 2026-07-06: **1174 done / 17 stub / 14 missing** of 1205 JT
  entries (~97% — 1050 lifted + 52 noop + 72 alias). Raw counts mislead — most
  of the 31 pending is demand-driven display/runtime paths the working code
  never calls, or SUPERSEDED shims (CODE 3 jt426/432/458 → GEMDOS, most of
  CODE 4 → VIDEL HAL). The one real remaining DEPTH block is the **CODE 11 GEO
  3D-map editor (jt243 5216 + jt242 1298 insn)**, followed by the foundational
  **CODE 8 jt335 (2598)**. The play runtime + the CODE 2 event editor + jt259
  art import are all lifted. Status below is by *player-facing subsystem*, not
  by count.

Status legend: ✅ done (works end-to-end) · 🟡 partial (lifted but
incomplete/buggy) · 🔴 not started (stub/missing) · ⏸ deferred (ADR-0008,
editor after runtime).

---

## 1. Foundation — the libraries everything sits on  ✅

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| Boot / A5-world init / entry | 1 | ✅ | `engine-bring-up.md`, `string-table-zero-expansion` (mem) |
| Mac Toolbox shim (QuickDraw/Dialog/Event/Menu) | 3 | ✅ | `toolbox-mapping.md` |
| Display HAL + QuickDraw low-level / blit math | 4 + `platform/` | ✅ | `architecture.md`, `milestone-v0.2.md`, `vbl-cursor-service` (mem) |
| Core runtime lib (format-VM, error modal) | 5 | ✅ | `jt400-format-vm.md` |
| File-group + GLIB art + **Resource Manager** | 6 | ✅ | `resource-manager-wall.md`, `glib-palette-subsystem.md` |
| DLItem widget toolkit (list dialog, buttons) | 7 | ✅ | `code07-wall.md` |
| Input / menu / file-prefix lib | 8 | ✅ | — (demand-driven; no wall needed) |

Pending counts here (CODE 4 = 63, CODE 5 = 51) are display/runtime paths the
working code never calls. **Demand-driven, not gaps — lift on demand, don't grind.**

## 2. Front door — boot → ready to play  ✅ (save/load 🟡)

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| Title → credits → main menu → **design-select picker** | 22 | ✅ | `code22-wall.md`, `faithful-main-menu-code22` (mem) |
| **Character generation** (create/modify/reroll/finalize) | 17 | ✅ | `code17-chargen-wall.md`, `chargen-finalize-wall.md`, `char-record-layout.md` |
| **Training Hall** + roster + party model | 12 | ✅ | `training-hall-wall.md`, `party-model-migration.md` |
| Character **sheet** view (jt904/jt886) | 19 | ✅ | `char-sheet-jt886` (mem) |
| **Save / Load** | 15 | 🟡 | `save-load-wall.md` — party round-trip done (#141); pending: ~10KB design-state block (jt580 TODO, the `-27920` pad), A–J slot pickers, jt159 load-confirm, boot auto-load |

## 3. In-game — dungeon traversal  🟡

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| Dungeon **walk / movement** loop (arrows + turn) | 15/19 | ✅ | `play-loop-wall.md`, `play-movement-chain` (mem) |
| Dungeon **3D render** (wall sets, perspective) | — | ✅ | `dungeon-view-wall.md` — #129 CLOSED 2026-07-02: frame-stomp fixed (stage 4), left-column clip not reproducible; residual chrome-gap residue → #144 |
| Dungeon **HUD chrome** (roster/clock/compass/cmd bar) | — | 🟡 | `dungeon-hud-chrome-arch` (mem) — renders; `port_draw_play_frame` over-blit stand-in remains |
| **Event dispatcher** (`l709e`, 39 arms) | 18/20 | 🟡 | `play-loop-wall.md` + per-event walls — 16 arms lifted, ~17 stub |

## 4. In-game — interactions / town  🟡

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| **Treasure / vault / pickup** (picker, reward, vault I/O) | 19/20 | ✅ | `treasure-event-wall.md` |
| **Tavern** (drink/listen/fight) | 20 | ✅ | `treasure-event-wall.md` (case 7 `l4f9a`) |
| **Temple** (take-text/donate) | 20 | ✅ | (case 9 `l216a`) |
| **Inn** (`l398a`, case 29) | 20 | 🔴 | gated on rest (`jt957`); shell clean, core is camp |
| **Rest / camp** (menu, REST action, clock/heal) | 21 | 🟡 | `code21-camp-wall.md` — menu + REST + MAGIC-menu lifted; spell screens stub |
| **Spell memorization** (4 magic screens) | 21 | 🔴 | `code21-camp-wall.md` — `l06d6`/`l0bc6`/`l0df2`/`l1374` + `l1e44` + Alter `l2d7e` stub |
| **Inventory / item-list / equip** (sheet items, ITEMS/TRADE/DROP) | 9 + 19 | 🔴 | `inventory-subsystem-wall.md` ← **NEXT FOCUSED TASK**. NB: `jt893` (ITEMS dispatcher) now has a structural body |
| **Shop / merchant** (sell/identify/value) | 20/19/7 | 🟡 | `shop-merchant-wall.md` — event + shop screen (`l5586`/`jt183`) LIFTED; only `jt189` SELL + `jt190` IDENTIFY stub (Mac bodies in CODE_07.s). No "buy" verb. `jt923` is NOT missing (= lifted overload gate). Untested live (HEIRS shop cell unconfirmed) |
| **Conditional / quest-flag events** (stat-check, set-flag, rumors, pass-time) | 18 | ✅ | `treasure-event-wall.md` (cases 15/27/37/38) |

## 5. In-game — combat  🟢 spine lifted (runtime-untested) / 🔴 effect handlers

Major advance: the combat **spine is wired top-to-bottom**, both turn-dispatch
sides are lifted, and **CODE 16's effect handlers are now ALL lifted (106/106,
2026-06-24)**, and the **physical-damage tier is now COMPLETE** (l29fc/l022c/
l030a/l1d0c/l14bc/l2b24): a weapon swing — melee and missile — computes damage,
deducts HP (jt39) and resolves death/XP. So a round now resolves **both** spell
and physical attacks for real. All still **breadth-first / runtime-untested**:
the next step is the Hatari bring-up. Trigger a fight via a type-1/33 event
cell → `l159a`, or call `l159a(ev,1)`. See `docs/milestone.md` §2 +
`docs/code14-wall.md` (physical-damage tier).

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| Combat **effects engine** (poison/cure/damage payloads) | 18 | ✅ | (band 9) — ~98% done, the hard payloads are lifted |
| Combat **main loop / spine** (`jt511`→`l076e` per-actor turn) | 13 | 🟢 | `code13-wall.md` — spine + per-turn tree lifted; live caller is `l159a`→`jt511`. 90% by JT. **Runtime-untested** |
| Combat **turn dispatch** (`l08b4` player, `l5008` monster-AI) | 13 | 🟢 | both sides + all 7 `l5008` executors lifted, stub-free. Turn Undead + Cast Spell are complete vertical slices |
| Combat **field render** (actor sprites, HP, targeting) | 14 | 🟡 | `code14-wall.md` — 81%; field-draw leaves `jt512`/`jt517`/`jt514`/`jt516`/`jt518`/`jt528`/`jt536`/`jt542` remain |
| Combat **effect handlers** (106 announce/apply) | 16 | ✅ | `code16-wall.md` — **COMPLETE 2026-06-24, all 106 lifted** (112/115 CODE-16 JT exports by name, 0 stub). Breadth-first / runtime-untested |
| Combat **physical-damage tier** (l29fc/l022c/l030a/l1d0c/l14bc/l2b24) | 14 | ✅ | COMPLETE 2026-06-24 — melee + missile swings deal damage, deduct HP (jt39), resolve death/XP; `code14-wall.md`. Runtime-untested |
| Encounter **narration** ("A battle begins…") | 20 | 🟢 | `l159a` cases 1/33 — lifted, drives `jt511` |

## 6. Cross-cutting media  🟡 / 🔴

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| **Event pictures / portraits** (PIC/SPRIT/CPIC/bigpic) | 20/6/5 | 🟡 | `event-pictures-wall.md` — runtime pipeline (`l442e`→…→`l6e58`) FAITHFUL + works; 2 open bugs are composition-ordering + buffer-sharing, NOT palette math. CODE 10 = the picture EDITOR (deferred), not the runtime path |
| **Audio / music / sound** (.slb engine) | 5/6 | 🔴 | `audio-wall.md` — dispatch + bank-load lifted, every output leaf stubbed → MUTED. FRUA uses the Device Manager (`_Write`), NOT the Sound Manager. Falcon DMA HAL already exists; needs the engine→HAL glue. Multi-part |

## 7. Editor / authoring tools  🟡 lifted-but-dormant / 🔴 GEO frontier

The authoring track opened (ADR-0008 phase-2). The CODE 2 event editor is fully
lifted; the CODE 11 GEO editor is the active frontier. All editor code is
DCE'd/dormant until jt315's selection dispatch (CODE 22+0x5180/0x5266) calls
`l0004_22` — the wiring step that makes the editor reachable at runtime.

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| Event / zone / map-step editing (jt254/253/248/249/258) | 2 | 🟡 | `event-editor-wall.md` — COMPLETE, dormant; command dispatcher `l0096` (CODE 22) wired |
| Art import (MacPaint/PICT, jt259) | 10 | 🟡 | lifted, dormant (dispatcher cmd 16) |
| **3D-map (GEO) editing + save (jt242/jt243)** | 11 | 🔴 | `geo-editor-wall.md` ← **NEXT FOCUSED TASK** (Phase C); both are PROBE stubs |
| Editor record panels (jt281/282/286) | 22 | ✅ | lifted (CODE 22 alias block) |

---

## Wall-doc coverage — every active subsystem now has a scope doc

The four former queue gaps were charted 2026-06-21 (each grounded in a code
trace; `inventory-subsystem-wall.md` was the model):

1. **Save / Load** → `save-load-wall.md` ✅ charted.
2. **Shop / merchant** → `shop-merchant-wall.md` ✅ charted (corrected: shop
   screen lifted, only sell/identify stub; `jt923` not missing).
3. **Event pictures / portraits** → `event-pictures-wall.md` ✅ charted
   (corrected: CODE 10 = editor, runtime pipeline is faithful).
4. **Audio / sound** → `audio-wall.md` ✅ charted (corrected: Device Manager,
   not Sound Manager; HAL already exists).

The editor segments are now charted: **CODE 2 = `event-editor-wall.md`** (event
editor COMPLETE), **CODE 11 = `geo-editor-wall.md`** (GEO editor, Phase C —
active). The CODE 22 command dispatcher (`l0096`) is lifted.

## Targeting priority (highest leverage first) — updated 2026-07-06

The play runtime + the CODE 2 event editor + jt259 art import are lifted. The
frontier is now the **design editor's last giant** and a short tail of runtime
polish. Per `completion-plan.md` (the master sequence):

1. **CODE 11 GEO 3D-map editor** (`geo-editor-wall.md`, Phase C) — jt242 (1298)
   then jt243 (5216, the biggest fn in the codebase). The single largest
   remaining block; both are PROBE stubs the `l0096` dispatcher already routes
   to. **The active task.**
2. **Wire the editor live** — lift jt315's selection dispatch (CODE 22 +
   0x5180/0x5266) to call `l0004_22`, making the whole editor reachable.
3. **CODE 8 foundational giant** jt335 (2598) + jt334/336/337/371/373 — may
   unblock the editor giants; pull early if C depends on it.
4. **Runtime polish** still genuinely open: inventory/equip
   (`inventory-subsystem-wall.md`), the remaining `l709e` event arms, rest/camp
   spell-memorize (`code21-camp-wall.md`), save/load slot pickers, audio
   (`audio-wall.md`, muted).
5. **Small-stub cleanup** — CODE 5 (6 leaves), CODE 12 Training Hall
   (jt916/919/927/931/933), CODE 4 (check HAL-superseded). Batch by segment.

## Bottom line

Foundation, front door, and the **in-game play runtime are done**; the port
boots, builds a party, picks a design, saves/loads, walks the dungeon, and the
combat spine + effect handlers are lifted. The **CODE 2 event editor and jt259
art import are lifted** (dormant). The remaining frontier is the **CODE 11 GEO
3D-map editor** (jt242/jt243 — the last big depth block) plus wiring the editor
into the menu, then a short tail of runtime polish (inventory, events, audio)
and small-stub cleanup.

> Maintenance: update this table whenever a subsystem's status changes or a new
> `*-wall.md` lands — same commit. Keep it to status + pointers; detail lives in
> the wall docs, counts in `jt-lift-progress.md`.
