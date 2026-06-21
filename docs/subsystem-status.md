# Subsystem status register — the targeting dashboard

**One place to see every subsystem, whether it's implemented, and where its
scoped "wall" doc lives** (or that it still needs one). Use this to pick the
next target. It is the index over the queue of `docs/*-wall.md` scope docs.

- Companion views: `docs/roadmap.md` (by CODE segment / layer),
  `docs/gap-analysis.md` (by play-flow the player experiences),
  `docs/jt-lift-progress.md` (auto-generated JT counts — the source of truth
  for numbers; rerun `python3 tools/jt_progress.py`).
- Counts as of 2026-06-21: **844 done / 142 stub / 219 missing** of 1205 called
  JT entries (758 lifted + 20 noop + 66 alias = done). Raw counts mislead —
  most "missing" is demand-driven display/runtime paths the working code never
  calls. Status below is by *player-facing subsystem*, not by count.

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
| **Save / Load** | 15 | 🟡 | **NEEDS WALL** — party round-trip done (#141); pending: ~10KB design-state block, A–J slot pickers, jt159 load-confirm, boot auto-load. Notes in `next-session-saveload` (mem) |

## 3. In-game — dungeon traversal  🟡

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| Dungeon **walk / movement** loop (arrows + turn) | 15/19 | ✅ | `play-loop-wall.md`, `play-movement-chain` (mem) |
| Dungeon **3D render** (wall sets, perspective) | — | 🟡 | `dungeon-view-wall.md` — 2 bugs: left-column clip (jt199/jt200 mirror), #129 frame-stomp |
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
| **Shop / merchant** (buy/sell/value) | 19/7 | 🟡 | **NEEDS WALL** — `jt893` ITEMS-side lifted (structural); buy/sell `jt189`/`jt190`, add-money `jt923` still stub. `shop-subsystem-scope` (mem) is partly STALE (it predates the jt893 body) |
| **Conditional / quest-flag events** (stat-check, set-flag, rumors, pass-time) | 18 | ✅ | `treasure-event-wall.md` (cases 15/27/37/38) |

## 5. In-game — combat  🔴 (the big frontier)

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| Combat **effects engine** (poison/cure/damage payloads) | 18 | ✅ | (band 9) — ~96% done, the hard payloads are lifted |
| Combat **main loop / spine** (`l076e` per-actor turn) | 13 | 🔴 | `code13-wall.md` ← **THE KEYSTONE**; `jt511` loop lifted but has no live caller |
| Combat **field render** (actor sprites, HP, targeting) | 14 | 🔴 | `code14-wall.md` — gated on the spine |
| Combat **effect handlers** (~81 announce/apply) | 16 | 🔴 | `code16-wall.md` — gated on the spine |
| Encounter **narration** ("A battle begins…") | 20 | 🟡 | `l159a` cases 1/33 — gated on the spine |

## 6. Cross-cutting media  🟡 / 🔴

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| **Event pictures / portraits** (PIC/SPRIT/CPIC/bigpic) | 10 | 🟡 | **NEEDS WALL** — bigpic composer works (`l442e`); intro caravan black-frame + palette-clobber open. `bigpic-composer-129`, `resource-manager-bigpic-pickup` (mem) |
| **Audio / music / sound** (.slb engine) | — | 🔴 | **NEEDS WALL** — `jt52` dispatcher lifted but leaves (`jt965`/`jt974`/`jt985`) stubbed; sound is MUTED by design. Whole subsystem is its own session |

## 7. Editor / authoring tools  ⏸ deferred (ADR-0008: runtime first)

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| Event / zone / map-step editing | 2 | ⏸ | — (deferred) |
| 3D-map (GEO) editing + save | 11 | ⏸ | — (deferred) |
| Editor record panels (jt281/282/286) | 22 | ⏸ | — (deferred) |

---

## Queue gaps — subsystems that still need a scoped wall doc

These are tracked only in memories / scattered notes today. Chart one when it
becomes the active target (the `inventory-subsystem-wall.md` is the model):

1. **Save / Load** (§2) — 🟡 active-ish; design-state block + slot pickers.
2. **Shop / merchant** (§4) — 🟡 partly lifted; needs a wall to consolidate
   `jt893` (done structural) + `jt189`/`jt190`/`jt923` (stub) and retire the
   stale `shop-subsystem-scope` memory.
3. **Event pictures / portraits** (§6) — 🟡 only in memories.
4. **Audio / sound** (§6) — 🔴 whole subsystem, no scope doc yet.

## Targeting priority (highest leverage first)

1. **Inventory / equip** (`inventory-subsystem-wall.md`) — the active task;
   makes the char sheet truthful + unlocks ITEMS/TRADE/DROP. Small, testable now.
2. **Event-handler vocabulary** (`l709e` remaining arms) — each cheap +
   live-testable on a HEIRS cell; makes designed dungeons actually play.
3. **3D-render placement bug** (`dungeon-view-wall.md`) — isolated; the
   unfinished mirror of the b945821 right-side fix.
4. **Combat spine** (`code13-wall.md`, `l076e`) — the big multi-session unlock;
   effects engine (CODE 18) is already done, so it's orchestration + field
   render, not payloads. Ungates CODE 14 + the 81 CODE-16 handlers.
5. **Rest/camp completion + spell memorize** (`code21-camp-wall.md`).
6. **Polish:** save/load slot pickers, #129 frame-stomp, audio, editor (last).

## Bottom line

Foundation + front door are **done**; the port boots, builds a party, picks a
design, saves/loads, and walks the dungeon. The frontier is the **in-game
vocabulary**: inventory (next), the rest of the event handlers, then **combat**
(one keystone — `l076e`). Combat is the single largest remaining block; the
editor is deliberately last.

> Maintenance: update this table whenever a subsystem's status changes or a new
> `*-wall.md` lands — same commit. Keep it to status + pointers; detail lives in
> the wall docs, counts in `jt-lift-progress.md`.
