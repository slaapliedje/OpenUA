> ⚠️ **REGENERATE, DO NOT TRUST THE STATUS COLUMN.** This table has now gone stale
> three times (the shop's STUB column, jt893's arms, the event dispatcher). The
> source of truth is `python3 tools/stub_audit.py --stubs` (engine gaps) and
> `tools/jt_progress.py` (JT coverage). Read the body, never the comment.

# Subsystem status register — the targeting dashboard

**One place to see every subsystem, whether it's implemented, and where its
scoped "wall" doc lives** (or that it still needs one). Use this to pick the
next target. It is the index over the queue of `docs/*-wall.md` scope docs.

- Companion views: `docs/milestone.md` (high-level accomplished/remaining
  burn-down — START HERE for the big picture), `docs/roadmap.md` (by CODE
  segment / layer), `docs/gap-analysis.md` (by play-flow the player
  experiences), `docs/jt-lift-progress.md` (auto-generated JT counts — the
  source of truth for numbers; rerun `python3 tools/jt_progress.py`).
- Counts as of **2026-07-30** (regenerate: `python3 tools/jt_progress.py`,
  `python3 tools/stub_audit.py --stubs`): **1201 done / 1 stub / 4 missing** of
  1206 JT entries (1072 lifted + 54 noop + 75 alias). Stub bodies: 52 total —
  39 faithful no-ops + 5 platform rulings, **0 live gaps**, 8 uncalled.
  `--arms`: 0 deferred, 0 bare case.
- ⚠️ **This is the FOURTH "0 live gaps" and the first three were artefacts** —
  see the table in `docs/enhancements.md` for how each of the five was settled.
  What is different: none was closed by the tool changing its mind. #108 lifted
  two; #111 enumerated all four `JT[392]` call sites and showed `l341a`'s guard
  unreachable; #112/#113 lifted `l4e8a`/`l501e` (the add/delete pair of the
  6-bit string pool — NOT the "lookup" and "list scroll" their comments
  claimed) with a round-trip harness; #114 read `jt1150`'s Mac body and found
  `linkw/unlk/rts`. **A zero still means no reachable PROBE stub body, NOT
  feature-complete.**
- ★ **A THIRD stub_audit parser bug (#111): a TRAILING comment on a `return`
  made a stub body read REAL.** It hid two more bodies, both benign — but it
  could have hidden a live gap. Fixed + pinned by four regression tests. Treat
  every count on this page as regenerate-only; that is three separate parser
  faults, all in the optimistic direction.
- ★ **Four of the five stubs above were described by GUESSING from the name.**
  Three of those descriptions were wrong outright, and the speculation is also
  what kept `jt1150` on the books — a comment that claims a real function stops
  `stub_audit` filing a stub where it belongs. Describe bodies you have read.
- ★ **The "0 LIVE GAPS" this line used to carry was partly a TOOL BUG**, fixed
  in #103: `stub_audit`'s call counter treated every multi-line definition as
  its own forward declaration, so a multi-line stub with exactly one caller
  read as "uncalled" instead of live. 12 moved buckets when that was corrected.
  They are not yet triaged — see the ★ block in `docs/enhancements.md`. This is
  the THIRD time this line has gone stale in the optimistic direction; rerun the
  tool, and treat a zero with suspicion.
- **THE last live gap is CLOSED: `jt933`** (2026-07-12) — it was NOT a "take-
  commit" as the port's comment claimed, but the temple's **SERVICES screen**
  (Cure Light Wounds .. Resurrection, each priced through the L46f6 payment gate).
  Lifted with its 13-function tree (~1400 insn) and Hatari-verified in the TEMPLE
  OF TYMORA. It also exposed an INVERTED gate in l216a that made the screen
  unreachable. See `treasure-event-wall.md`. *(It hid for several sessions because
  `stub_audit`'s parser dropped wrapped one-line bodies and reported "0 live gaps";
  fixed + regression-tested.)*
- The other 5 pending JT are NOT gaps: `jt426`/`jt432`/`jt458` are SUPERSEDED
  (Toolbox→GEMDOS / printing, needs a GDOS backend — out of scope); `jt955`
  (deferred jt948 arm) and `jt1064` (Mac Package Manager, no Falcon counterpart)
  are uncalled.
- The old "97% is misleading — the real work is DEPTH" caveat is now RETIRED:
  the depth blocks it named are done. CODE 11 (GEO 3D-map editor, jt243/jt242)
  is 12/12 with area-load CLOSED; CODE 8 jt335 is lifted; the CODE 2 event
  editor, jt259 art import and the play runtime are all lifted.
- Companion views: `docs/milestone.md`, `docs/roadmap.md`, `docs/gap-analysis.md`,
  `docs/jt-lift-progress.md` (auto-generated — the source of truth for numbers).

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
| **Save / Load** | 15 | ✅ | `save-load-wall.md` — party round-trip + A–J pickers + jt159 confirm live. 2026-07-12: the saved-character roster is now a real **.cch stream** (jt578/jt577), killing the raw-512 pointer-persistence crash; equipment persists. Residual: the ~10KB design-state pad (jt580 `-27920`) |

## 3. In-game — dungeon traversal  🟡

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| Dungeon **walk / movement** loop (arrows + turn) | 15/19 | ✅ | `play-loop-wall.md`, `play-movement-chain` (mem) |
| Dungeon **3D render** (wall sets, perspective) | — | ✅ | `dungeon-view-wall.md` — #129 CLOSED 2026-07-02: frame-stomp fixed (stage 4), left-column clip not reproducible; residual chrome-gap residue → #144 |
| Dungeon **HUD chrome** (roster/clock/compass/cmd bar) | — | 🟡 | `dungeon-hud-chrome-arch` (mem) — renders; `port_draw_play_frame` over-blit stand-in remains |
| **Event dispatcher** (`l709e`, 39 arms) | 18/20 | ✅ | `play-loop-wall.md` — **the "~17 arms stub" claim was STALE** (2026-07-13); `stub_audit --stubs` reports no event arm among either the live or the uncalled stubs (re-checked 2026-07-29 after #103 corrected the call counter — the 12 live gaps it now reports are elsewhere). Regenerate from stub_audit, never from this table. |

## 4. In-game — interactions / town  🟡

| Subsystem | CODE | Status | Wall / scope doc |
|-----------|:----:|:------:|------------------|
| **Treasure / vault / pickup** (picker, reward, vault I/O) | 19/20 | ✅ | `treasure-event-wall.md` — screen + reward + vault live; **jt933 TEMPLE SERVICES lifted + Hatari-verified 2026-07-12** |
| **Tavern** (drink/listen/fight) | 20 | ✅ | `treasure-event-wall.md` (case 7 `l4f9a`) |
| **Temple** (take-text/donate + **SERVICES**) | 20/12 | ✅ | `treasure-event-wall.md` — jt933 services screen + L46f6 payment gate LIVE (2026-07-12) |
| **Inn** (`l398a`, case 29) | 20 | 🔴 | gated on rest (`jt957`); shell clean, core is camp |
| **Rest / camp** (menu, REST action, clock/heal) | 21 | 🟡 | `code21-camp-wall.md` — menu + REST + MAGIC-menu lifted; spell screens stub |
| **Spell memorization** (4 magic screens) | 21 | ✅ | `code21-camp-wall.md` — `l06d6`/`l0bc6`/`l0df2`/`l1374` + `l1e44` + Alter `l2d7e` stub |
| **Inventory / item-list / equip** (sheet items, ITEMS/TRADE/DROP) | 9 + 19 | ✅ | `inventory-subsystem-wall.md` ← **NEXT FOCUSED TASK**. NB: `jt893` (ITEMS dispatcher) now has a structural body |
| **Shop / merchant** (buy/sell/identify/value) | 20/19/7 | ✅ | `shop-merchant-wall.md` — **COMPLETE + LIVE-VERIFIED 2026-07-13**, 0 stubs. BUY (`l3c7c`) was the last gap; SELL/IDENTIFY were already lifted and the doc's STUB column was just STALE. **All three payment arms executed** (can't-afford proved vs AGAINST THE GIANTS, 0 starting gold). ★"No buy verb" was WRONG — retracted. Shop cell: HEIRS GEO005 row 5, col 9 |
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
| **Audio / music / sound** (.slb engine) | 5/6 | ✅ | `audio-wall.md` — dispatch + bank-load lifted, every output leaf stubbed → MUTED. FRUA uses the Device Manager (`_Write`), NOT the Sound Manager. Falcon DMA HAL already exists; needs the engine→HAL glue. Multi-part |

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
| **Game Settings** record editor (jt251 → jt325_tail) | 9/22 | ✅ | `jt325-record-editor-wall.md` — renders + pages + commits, and the **field EDIT round-trip is verified** (#102, 2026-07-29): PLATINUM 100→777 and the design name both survive a cold reboot; byte-exact in `GAME001.DAT` |
| Event / zone / map-step editing (jt254/253/248/249/258) | 2 | ✅ | `event-editor-wall.md` — lifted, wired, and **DRIVEN END-TO-END 2026-07-30 (#115)**: MAP → EVENT PLACEMENT → EDIT → TEXT STATEMENT authors a real event, and FILE→SAVE writes it (verified with `tools/geo.py`: exactly one event 56 `empty`→`Message/Text`, exactly one string). The wall doc's "dormant" section is dead. This is also the only editor whose records carry pooled string fields (`nstr=5`; Game Settings and Monster are both 0) |
| **Art Gallery** / picture editor (jt269 + l040c; import jt259) | 10 | ✅ | l040c dialog tree lifted + LIVE-validated 2026-07-11 |
| **Monster / NPC** record editor (jt263 → jt325 type 54/57) | 10/9 | ✅ | opens + renders monster art, and the **field EDIT round-trip is verified** (#102, 2026-07-29): BASILISK HIT POINTS 28→44 and NAME→`BASILISKq` survive a cold reboot; the commit creates the design-local `MONST042.dat` (byte 129 = HP, 96..111 = name) |
| **Map (GEO) editor** (jt242/jt243) | 11 | ✅ | `geo-editor.md` — **the "OPEN bus-errors" claim is DEAD (re-driven live 2026-07-29, #101).** `E` opens the editor with FILE/MAP/UTILITIES, the automap, the 3D preview, WD/HT and the tool palette; FILE → OPEN.. switches areas (DUNGEON 01 19×19 → DUNGEON 06 28×20, new automap + new view). jt244/mode-19 really is never triggered, but nothing needs it — the load runs through `jt233` (l0096 case 9). Residual: the ENTRY picker's list body is EMPTY (see below) |
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

1. ~~**Map editor area-load**~~ — **CLOSED 2026-07-29 (#101).** It was never
   bus-erroring: all four editors open, the map editor loads areas, and an
   in-editor area switch works. jt244 is wired at `l0096` case 19 but no drivable
   path emits command 19; `jt233` (case 9) does the load. What IS still open is
   narrower and new: the map editor's **entry** picker renders an empty list
   (10 designs installed, 11 areas in HEIRS, zero rows drawn), so on entry you
   can only accept the current area — the in-editor FILE → OPEN.. list is fully
   populated. `geo-editor.md`.
2. **Player-facing runtime gaps** — inventory/equip
   (`inventory-subsystem-wall.md`), the remaining `l709e` event arms (~17), and
   **audio (MUTED)** = the CODE 5 sound cluster jt965/974/1064 (`audio-wall.md`):
   engine→Falcon-DMA-HAL glue, the biggest genuinely-open subsystem.
3. **Real stub subtrees** (not leaves): jt933 (CODE 12 item-take modal, ~400
   lines), jt955 (CODE 21 camp, 1014 B), jt1206 (CODE 4, small dispatcher).
4. ~~**Editor edit round-trips**~~ — **DONE (#102, 2026-07-29).** Both record
   editors edit for real: a numeric and a string field per editor, typed in,
   committed, and read back after a full engine reboot, byte-verified in the
   design files. The "mouse-gated" caveat is retired — clicks inject. NPC
   (jt325 type 57) is the same widget tree as monster type 54 and was not
   separately driven.
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
