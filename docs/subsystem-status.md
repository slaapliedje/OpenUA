# Subsystem status register — the targeting dashboard

**One place to see every subsystem, whether it's implemented, and where its
scoped "wall" doc lives** (or that it still needs one). Use this to pick the
next target. It is the index over the queue of `docs/*-wall.md` scope docs.

- Companion views: `docs/milestone.md` (high-level accomplished/remaining
  burn-down — START HERE for the big picture), `docs/roadmap.md` (by CODE
  segment / layer), `docs/gap-analysis.md` (by play-flow the player
  experiences), `docs/jt-lift-progress.md` (auto-generated JT counts — the
  source of truth for numbers; rerun `python3 tools/jt_progress.py`).
- Counts as of 2026-06-24 (HEAD e892f8e): **826 lifted / 109 stub / 273
  no-jtN-def** of 1208 JT entries (~68% by JT-name; true coverage higher — much
  of "missing" is lifted under its CODE-local `lXXXX` alias). Raw counts mislead
  — most remaining "missing" is demand-driven display/runtime paths the working
  code never calls. The one real block is **CODE 16 (80 stub effect handlers)**.
  Status below is by *player-facing subsystem*, not by count.

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
| **Shop / merchant** (sell/identify/value) | 20/19/7 | 🟡 | `shop-merchant-wall.md` — event + shop screen (`l5586`/`jt183`) LIFTED; only `jt189` SELL + `jt190` IDENTIFY stub (Mac bodies in CODE_07.s). No "buy" verb. `jt923` is NOT missing (= lifted overload gate). Untested live (HEIRS shop cell unconfirmed) |
| **Conditional / quest-flag events** (stat-check, set-flag, rumors, pass-time) | 18 | ✅ | `treasure-event-wall.md` (cases 15/27/37/38) |

## 5. In-game — combat  🟢 spine lifted (runtime-untested) / 🔴 effect handlers

Major advance since 2026-06-21: the combat **spine is wired top-to-bottom** and
both turn-dispatch sides are lifted. The remaining real block is CODE 16's 80
effect handlers (what abilities *do*). See `docs/milestone.md` §2 for the chain.

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| Combat **effects engine** (poison/cure/damage payloads) | 18 | ✅ | (band 9) — ~98% done, the hard payloads are lifted |
| Combat **main loop / spine** (`jt511`→`l076e` per-actor turn) | 13 | 🟢 | `code13-wall.md` — spine + per-turn tree lifted; live caller is `l159a`→`jt511`. 90% by JT. **Runtime-untested** |
| Combat **turn dispatch** (`l08b4` player, `l5008` monster-AI) | 13 | 🟢 | both sides + all 7 `l5008` executors lifted, stub-free. Turn Undead + Cast Spell are complete vertical slices |
| Combat **field render** (actor sprites, HP, targeting) | 14 | 🟡 | `code14-wall.md` — 81%; field-draw leaves `jt512`/`jt517`/`jt514`/`jt516`/`jt518`/`jt528`/`jt536`/`jt542` remain |
| Combat **effect handlers** (80 announce/apply) | 16 | 🔴 | `code16-wall.md` ← **THE FRONTIER**; `jt595`/`jt599` entry points lifted, 80 payloads stub |
| Encounter **narration** ("A battle begins…") | 20 | 🟢 | `l159a` cases 1/33 — lifted, drives `jt511` |

## 6. Cross-cutting media  🟡 / 🔴

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| **Event pictures / portraits** (PIC/SPRIT/CPIC/bigpic) | 20/6/5 | 🟡 | `event-pictures-wall.md` — runtime pipeline (`l442e`→…→`l6e58`) FAITHFUL + works; 2 open bugs are composition-ordering + buffer-sharing, NOT palette math. CODE 10 = the picture EDITOR (deferred), not the runtime path |
| **Audio / music / sound** (.slb engine) | 5/6 | 🔴 | `audio-wall.md` — dispatch + bank-load lifted, every output leaf stubbed → MUTED. FRUA uses the Device Manager (`_Write`), NOT the Sound Manager. Falcon DMA HAL already exists; needs the engine→HAL glue. Multi-part |

## 7. Editor / authoring tools  ⏸ deferred (ADR-0008: runtime first)

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| Event / zone / map-step editing | 2 | ⏸ | — (deferred) |
| 3D-map (GEO) editing + save | 11 | ⏸ | — (deferred) |
| Editor record panels (jt281/282/286) | 22 | ⏸ | — (deferred) |

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

The only subsystems without a wall are the ⏸ **editor** segments (CODE 2/11/22),
deferred by ADR-0008 — chart them when the authoring-tools track opens.

## Targeting priority (highest leverage first)

1. **Inventory / equip** (`inventory-subsystem-wall.md`) — the active task;
   makes the char sheet truthful + unlocks ITEMS/TRADE/DROP. Small, testable now.
2. **Event-handler vocabulary** (`l709e` remaining arms) — each cheap +
   live-testable on a HEIRS cell; makes designed dungeons actually play.
3. **3D-render placement bug** (`dungeon-view-wall.md`) — isolated; the
   unfinished mirror of the b945821 right-side fix.
4. **CODE-16 effect handlers** (`code16-wall.md`) — the 80 announce/apply
   payloads. The combat spine is now lifted, so this is what makes abilities
   *do* something. The single biggest remaining block.
5. **Combat runtime bring-up** — drive a live round; fix what the breadth-first
   spine/field lifts got wrong. The integration pass once a few handlers land.
6. **Rest/camp completion + spell memorize** (`code21-camp-wall.md`).
7. **Polish:** save/load slot pickers, #129 frame-stomp, audio, editor (last).

## Bottom line

Foundation + front door are **done**; the port boots, builds a party, picks a
design, saves/loads, and walks the dungeon. The frontier is the **in-game
vocabulary**: inventory (next), the rest of the event handlers, then **combat**
(one keystone — `l076e`). Combat is the single largest remaining block; the
editor is deliberately last.

> Maintenance: update this table whenever a subsystem's status changes or a new
> `*-wall.md` lands — same commit. Keep it to status + pointers; detail lives in
> the wall docs, counts in `jt-lift-progress.md`.
