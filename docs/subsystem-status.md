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

## 5. In-game — combat  ✅ PLAYABLE end-to-end (render nits remain)

The combat subsystem is **lifted AND runtime-validated end-to-end** (07-02→07-04,
re-confirmed 2026-07-11 on the current build). A HEIRS spider fight was driven
via the FRUA_ENTRY harness: L10 entry → spiderweb prompt → CUT WEB → spider
bigpic → tactical field (6 party sprites vs spiders) → win → **548 XP** page →
clean return to the dungeon HUD. Manual turns (move/attack/cast/target/guard/
flee/quick), saves, ammo, real damage, and the AUTOWIN path all execute. The
combat hot path is fully lifted (`combat-audit.md`, 754 reachable fns verified).
Repro: `make EXTRA_CFLAGS="-DFRUA_ENTRY_LEVEL=10 -DFRUA_ENTRY_COL=4
-DFRUA_ENTRY_ROW=12 -DFRUA_ENTRY_FACING=2 [-DFRUA_AUTOWIN]"`, then Play → Load
save A → Begin Adventuring. Remaining = render polish, not logic.

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| Combat **effects engine** (poison/cure/damage payloads) | 18 | ✅ | (band 9) — hard payloads lifted; live in fights |
| Combat **main loop / spine** (`jt511`→`l076e` per-actor turn) | 13 | ✅ | `code13-wall.md` — runs end-to-end, round loop + turns validated |
| Combat **turn dispatch** (`l08b4` player, `l5008` monster-AI) | 13 | ✅ | both sides live; move/attack/cast/target/guard/flee/quick all drive |
| Combat **field render** (actor sprites, HP, targeting) | 14 | 🟡 | `code14-wall.md` — renders live; nits: marker residue (jt119/jt122), RANGE=0 readout, occasional CPIC strip garble |
| Combat **effect handlers** (106 announce/apply) | 16 | ✅ | `code16-wall.md` — all 106 lifted; spell payloads fire live |
| Combat **physical-damage tier** (l29fc/l022c/l030a/l1d0c/l14bc/l2b24) | 14 | ✅ | melee + missile swings deal real damage, resolve death/XP live |
| Encounter **narration** ("A battle begins…") | 20 | ✅ | `l159a` cases 1/33 — drives `jt511`; validated |
| Combat **render nits** (2nd-cycle wall re-clobber, marker residue) | 14/22 | 🟡 | cosmetic; `combat-encounter-gateway` (mem) 07-03/04 cards |

## 6. Cross-cutting media  🟡 / 🔴

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| **Event pictures / portraits** (PIC/SPRIT/CPIC/bigpic) | 20/6/5 | 🟡 | `event-pictures-wall.md` — runtime pipeline (`l442e`→…→`l6e58`) FAITHFUL + works; 2 open bugs are composition-ordering + buffer-sharing, NOT palette math. CODE 10 = the picture EDITOR (deferred), not the runtime path |
| **Audio / music / sound** (.slb engine) | 5/6 | 🔴 | `audio-wall.md` — dispatch + bank-load lifted, every output leaf stubbed → MUTED. FRUA uses the Device Manager (`_Write`), NOT the Sound Manager. Falcon DMA HAL already exists; needs the engine→HAL glue. Multi-part |

## 7. Editor / authoring tools  ✅ LIVE + menu-wired (map-editor area-load 🔴)

The authoring track is now LIVE (2026-07-11). jt315's selection dispatch is
wired AND label-corrected (JT[452] index decode): all four editor menu items
open their correct editor with the right title — verified live (driver key
g/e/a/m + dump). The record-editor engine (jt325_tail) and the jt269 picture
gallery's l040c dialog tree are lifted and runtime-validated. The one remaining
editor DEPTH gap is the map editor's area bring-up.

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| **Editor menu wiring** (Game Settings/Edit Modules/Art Gallery/Monster Editor) | 22 | ✅ | `jt325-record-editor-phase-d` (mem) — 2026-07-11, all 4 open correctly (JT[452] index decode) |
| **Game Settings** record editor (jt251 → jt325_tail) | 9/22 | ✅ | `jt325-record-editor-wall.md` — renders + pages + commits |
| Event / zone / map-step editing (jt254/253/248/249/258) | 2 | ✅ | `event-editor-wall.md` — lifted; l0096 dispatcher wired |
| **Art Gallery** / picture editor (jt269 + l040c; import jt259) | 10 | ✅ | l040c dialog tree lifted + LIVE-validated 2026-07-11 |
| **Monster / NPC** record editor (jt263 → jt325 type 54/57) | 10/9 | 🟡 | opens + renders monster art; edit round-trip untested |
| **Map (GEO) editor** (jt242/jt243) | 11 | 🟡 | `geo-editor-wall.md` — lifted; picker opens, **OPEN bus-errors on the empty -12300 area block** (jt244/mode-19 area-load never triggered) ← the editor's last DEPTH gap |
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

## Targeting priority (highest leverage first) — updated 2026-07-11

The JT scoreboard is at **1196/1205 done** (5 stub, 4 missing; 3 of those 9 are
superseded-dead). The GEO editor giants, jt335, wiring the editor live, the menu
label fix — all DONE. And combat is **runtime-validated end-to-end** (re-confirmed
2026-07-11, §5). The frontier is now a short tail of DEPTH gaps + player-facing
polish:

1. **Map editor area-load** — the last editor DEPTH gap. 3 of 4 editors open
   live; the map editor's OPEN bus-errors on the empty `-12300` block because the
   jt244/mode-19 area-load is never triggered. Trace the Mac's mode-2 first-entry
   (how jt248's pick reaches jt244). `geo-editor-wall.md` / `geo-editor-phase-c`.
2. **Player-facing runtime gaps** — inventory/equip
   (`inventory-subsystem-wall.md`), the remaining `l709e` event arms (~17), and
   **audio (MUTED)** = the CODE 5 sound cluster jt965/974/1064 (`audio-wall.md`):
   engine→Falcon-DMA-HAL glue, the biggest genuinely-open subsystem.
3. **Real stub subtrees** (not leaves): jt933 (CODE 12 item-take modal, ~400
   lines), jt955 (CODE 21 camp, 1014 B), jt1206 (CODE 4, small dispatcher).
4. **Editor edit round-trips** — the record editors OPEN + render; validate an
   actual field EDIT → save for Game Settings / monster / NPC (mouse-gated).
5. **Combat/camp render nits** — marker residue (jt119/jt122), 2nd-cycle wall
   re-clobber, RANGE=0 readout, camp repaint overprint. Cosmetic.
6. **Close-out** — mark jt426/432/458 superseded-done in the tracker; save/load
   slot pickers + boot auto-load; shop sell/identify (jt189/jt190); l2d7e camp
   Alter.

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
