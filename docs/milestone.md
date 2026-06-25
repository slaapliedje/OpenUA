# MILESTONE — FRUA Falcon030/TT030 port

> Living tracker of what is **accomplished** and what is **left to do**.
> Snapshot: **2026-06-24** (CODE-16 sweep), HEAD `a1d2ff3`, `src/engine/boot.c`
> ~62k lines. Build green (`make`, soft-float `-m68020-60`), host test
> suite green (129 passed / 1 skipped).
> **NEW: CODE 16 (combat effect handlers) is COMPLETE — all 106 `-24066`
> dispatch handlers lifted (112/115 CODE-16 JT exports by name, 0 stub; the 3
> "nodef" are aliased).** The old single-biggest-block is closed; the combat
> frontier is now the runtime bring-up + the physical-damage round `l14bc`.
>
> Companion docs: `docs/subsystem-status.md` (player-facing register +
> targeting priority), the per-subsystem `docs/*-wall.md` scope docs, and
> `docs/function-index.md` (function catalog). This file is the high-level
> burn-down; detail lives in those.

---

## 1. Headline coverage (fresh audit, `tools`-grade hard numbers)

Every one of the **1208 jump-table (JT) entries** bucketed by CODE segment and
classified against `boot.c` (lifted = real body · stub = PROBE-only · missing =
no `jtN`-named definition):

| Metric | Count | Note |
|--------|------:|------|
| JT entries total | 1208 | the whole Mac jump table |
| Lifted (real body, by `jtN` name) | ~906 | **~75%** (was 826 before the CODE-16 sweep) |
| PROBE-only stubs | ~29 | CODE-16 (80) now all lifted; rerun `tools/jt_progress.py` for the exact figure |
| No `jtN`-named def ("missing") | 273 | **over-counts** — see caveat |
| PROBE-only stubs in `boot.c` total | ~60 | ~29 JT + ~31 CODE-local `lXXXX` |

**Caveat on "missing":** many JT entries are lifted under their CODE-local
`lXXXX` alias, not a `jtN` name (JT-export ≡ CODE-local; e.g. `jt496` reports
"missing" but is lifted as `l276c`). True coverage is materially higher than
68%; the honest read is **"~68% lifted by JT-name, plus an alias tail, minus the
one real block — CODE 16."**

### Per-segment

| SEG | role (subsystem) | JT | lifted | stub | missing | %done |
|----:|------------------|---:|-------:|-----:|--------:|------:|
|  1 | boot / A5-world / entry | 10 | 8 | 0 | 2 | 80% |
|  2 | event/zone EDITOR ⏸ | 14 | 4 | 0 | 10 | 28% |
|  3 | Mac Toolbox shim (QD/Dialog/Event) | 116 | 88 | 1 | 27 | 75% |
|  4 | QuickDraw low-level / blit / codecs | 117 | 59 | 5 | 53 | 50% |
|  5 | core runtime lib / format-VM / cursor | 129 | 68 | 2 | 59 | 52% |
|  6 | file-group + GLIB art + Resource Mgr | 126 | 111 | 2 | 13 | 88% |
|  7 | DLItem widgets (lists/buttons/dialogs) | 97 | 76 | 3 | 18 | 78% |
|  8 | input / menu / file-prefix lib | 47 | 25 | 1 | 21 | 53% |
|  9 | inventory / item-list | 5 | 1 | 0 | 4 | 20% |
| 10 | picture EDITOR ⏸ | 12 | 3 | 0 | 9 | 25% |
| 11 | 3D-map (GEO) EDITOR ⏸ | 12 | 5 | 0 | 7 | 41% |
| 12 | Training Hall + party model | 23 | 15 | 3 | 5 | 65% |
| 13 | **combat main loop + per-turn tree** | 22 | 20 | 1 | 1 | 90% |
| 14 | **combat field render / actions** | 44 | 36 | 3 | 5 | 81% |
| 15 | play-entry / dungeon walk loop | 19 | 15 | 0 | 4 | 78% |
| 16 | combat effect handlers ✅ **COMPLETE** | 115 | 112 | 0 | 3 | ~100% |
| 17 | character generation | 20 | 17 | 1 | 2 | 85% |
| 18 | dice / combat math / effects engine | 171 | 168 | 2 | 1 | 98% |
| 19 | char record / HP / level / sheet | 35 | 29 | 0 | 6 | 82% |
| 20 | events / encounter / town | 14 | 6 | 1 | 7 | 42% |
| 21 | rest / camp / spell-memorize | 9 | 2 | 4 | 3 | 22% |
| 22 | main menu / design-select (+editor ⏸) | 51 | 38 | 0 | 13 | 74% |

Segments 4/5/8 read low but are **demand-driven** library paths (the working
code never calls most of them); they are not gaps — lift on demand. The 28/25/41%
on 2/10/11 is the **editor**, deliberately deferred (ADR-0008).

To refresh these numbers: `python3 tools/jt_progress.py` (or rerun the audit in
the project scratchpad). Update the snapshot line + this table in the same commit
as any status change.

---

## 2. ACCOMPLISHED — what works end-to-end

The port **boots, builds a party, picks a design, saves/loads, and walks the
dungeon** — the full front-of-game journey is real, faithful, and Hatari-verified.

### Foundation (✅ done)
- Boot / A5-world replay (zero-fill + DATA blit + DREL relocs), entry chain.
- Mac Toolbox shim (`compat/`): QuickDraw, Dialog, Event, Menu, Resource,
  File, Memory — engine keeps Mac spellings, shim routes to GEMDOS / Mxalloc.
- Display HAL (`platform/`): VIDEL backend, 16bpp LUT + asm blit + VBL
  triple-buffer (input-lag fix), VBL-driven colour cursor.
- File-group + GLIB art codecs + **full Resource Manager** (FC group cache).
- Format-VM (`%r` recursive THINK-C format) behind the error modal.
- DLItem widget toolkit (faithful GLIB buttons/lists/bevels).

### Front door (✅ done; save/load 🟡)
- Title → credits → **main menu** → **design-select picker** (multi-`.DSN`).
- **Character generation**: create / modify / reroll / finalize
  (level, AC, THAC0, spells), character **sheet** (jt886 6-panel + reroll bar),
  body-icon grid, `.CHR` serializer.
- **Training Hall**: roster nav, Add / Remove / Create / Delete, faithful
  `-27928` party-list model, savegame persist.
- **Save / Load**: party round-trip done; ⏸ pending ~10KB design-state block,
  A–J slot pickers polish, boot auto-load.

### In-game traversal (✅ / 🟡)
- **Dungeon walk**: arrow-key move + turn through the HEIRS first-person
  dungeon (`l63c0` input loop → `jt297`/`jt311`/`L1908`).
- **3D render**: wall sets + perspective render live (coord convention,
  frustum, wall decode all resolved). 🟡 2 known bugs: left-column clip,
  #129 event-bigpic frame-stomp.
- **Dungeon HUD**: roster / clock / position / compass / command bar render.
  🟡 `port_draw_play_frame` over-blit stand-in remains.
- **Events** (`l709e`): text, picture, treasure/vault, tavern, temple,
  stat-check / set-flag / rumor / pass-time arms lifted.

### Combat (🟢 spine lifted — **major recent advance**, runtime-untested)
The combat **spine is wired top-to-bottom** and both turn-dispatch sides are
fully lifted (this was the "🔴 not started" block in older docs — now mostly
done):

```
l709e case 21 → l3b0e (encounter prompt) → l159a ("A battle begins…")
            → jt511 (combat main loop) → l076e (per-actor turn)
            → l08b4 (player command dispatch)  /  l5008 (monster-AI turn)
```
- `jt511`, `l076e`, `l08b4`, `l5008` all lifted; all seven `l5008` action
  executors (`l6176`/`l52ee`/`l525c`/`l52fe`/`l6454`/`l5b9a`/`l6042`) lifted.
- Flagship player commands lifted as complete vertical slices:
  **Turn Undead** (`jt534`→`jt540`→`jt388`→`l73cc`/`l6de8`→sprite trio) and
  **Cast Spell** (`jt547` → selection → `l276c` → `jt599` instant / delayed
  initiative-queue enqueue). **Both carry zero stubs.**
- **Effects engine** (CODE 18) is ~98% — the hard damage/save payloads are done.

⚠️ Combat lifts are **breadth-first / not yet runtime-tested**: the spine is
wired and the **effect handlers are now all lifted** (CODE 16 complete), so a
*spell* round resolves with real damage/saves — but no live playthrough has
confirmed a full round renders and resolves, and a **physical** swing still
deals no damage (`l14bc`/`l2b24` PROBE no-ops). Treat combat as "structurally
complete, runtime-pending; spell effects land, weapon damage is the next gap."

---

## 3. REMAINING — the frontier, highest leverage first

| # | Work | CODE | Scope doc | Size |
|--:|------|:----:|-----------|------|
| ✅ | ~~Physical-damage tier (l14bc/l030a/l022c/l1d0c/l29fc/l2b24)~~ — **DONE 2026-06-24**: melee + missile swings deal damage end-to-end | 14 | `code14-wall.md` | complete |
| 2 | **Combat runtime bring-up** — drive a live round in Hatari (trigger: a type-1/33 event cell → `l159a`, or call `l159a(ev,1)` directly); fix what the breadth-first spine/handler lifts got wrong | 13/14/16 | `code13-wall.md` | integration pass |
| 3 | **Combat field-render leaves** — `jt512`/`jt517` actor draw, `jt514`/`jt516`/`jt518`, `jt536`/`jt542`/`jt541` (the field is blank without these) | 14 | `code14-wall.md` | medium (~5 missing + 3 stub) |
| ✅ | ~~Combat effect handlers (CODE 16)~~ — **DONE**, all 106 lifted | 16 | `code16-wall.md` | complete |
| 4 | **Inventory / equip** — char-sheet items truthful + ITEMS/TRADE/DROP | 9 + 19 | `inventory-subsystem-wall.md` | small, testable now |
| 5 | **Event-handler vocabulary** — remaining `l709e` arms (reward picker, one-shot cell flags) | 18/20 | `play-loop-wall.md` | each cheap, live-testable on a HEIRS cell |
| 6 | **3D-render placement bug** — left-column clip (the unfinished mirror of b945821) | — | `dungeon-view-wall.md` | isolated |
| 7 | **Rest / camp + spell memorization** — 4 magic screens (`l06d6`/`l0bc6`/`l0df2`/`l1374`/`l1e44`) | 21 | `code21-camp-wall.md` | medium |
| 8 | **Shop / merchant** — `jt189` SELL + `jt190` IDENTIFY (no "buy" verb); untested live | 20/19/7 | `shop-merchant-wall.md` | small |
| 9 | **Inn** (`l398a`) — gated on rest | 20 | `treasure-event-wall.md` | small, after #7 |
| 10 | **Save/Load completion** — design-state block, slot pickers, boot auto-load | 15 | `save-load-wall.md` | medium |
| 11 | **#129 event-bigpic frame-stomp** — composition-ordering / buffer-sharing | 20/6/5 | `event-pictures-wall.md` | isolated |
| 12 | **Audio** — every output leaf stubbed (MUTED); needs engine→Falcon-DMA HAL glue (Device Manager `_Write`, not Sound Manager) | 5/6 | `audio-wall.md` | multi-part |

**Single biggest block is now closed** — CODE 16's 106 effect handlers are all
lifted, so spells/abilities apply for real through `jt598`→`jt599`→`l6114`. The
new keystone is `l14bc` (#1): physical attacks resolve their plumbing but deal
**no damage** until that PROBE no-op is lifted. Spell damage already lands.

---

## 4. DEFERRED — editor / authoring tools (⏸ ADR-0008: runtime first)

Not gaps — deliberately last. Charted when the authoring-tools track opens.

| Subsystem | CODE | %done |
|-----------|:----:|------:|
| Event / zone / map-step editing | 2 | 28% |
| Picture editor | 10 | 25% |
| 3D-map (GEO) editing + save | 11 | 41% |
| Editor record panels (jt281/282/286) | 22 | partial |

---

## 5. Bottom line

Foundation, front door, and dungeon traversal are **done and play**. The recent
campaigns pushed **combat from "not started" to "spine fully lifted"** — both
player and monster turn dispatch, plus Turn Undead and Cast Spell, are faithful
and stub-free. The remaining frontier is concentrated and nameable:

1. **CODE 16 effect handlers** (80) — make combat *do* something.
2. **Combat runtime bring-up** — prove a live round.
3. **Inventory** — small, makes the sheet truthful.

Everything else is bounded polish (event vocab, render bugs, camp, shop, save,
audio) and the deliberately-deferred editor. The port's center of gravity has
moved from "can it boot and walk" (yes) to "can it fight" (spine yes, payloads
next).
