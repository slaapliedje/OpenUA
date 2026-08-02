> ⚠️ **COUNTS STALE — snapshot 2026-06-26. See `docs/function-audit-2026-07-24.md`
> for measured current numbers** (1201/1206 JT done, boot.c 99.4k lines, 358
> tests). The structure of this burn-down is still useful; the figures are not.

# MILESTONE — FRUA Falcon030/TT030 port

> Living tracker of what is **accomplished** and what is **left to do**.
> Snapshot: **2026-06-26** (Training Hall / View-character / display polish),
> HEAD `9c6efb5`, `src/engine/boot.c` ~65.7k lines. Build green (`make`,
> soft-float `-m68020-60`), host test suite green (129 passed / 1 skipped).
> JT coverage (`tools/jt_progress.py`): **943 / 1205 done** (862 lifted +
> 20 noop + 61 alias), 65 stub, 197 "missing" (over-counts — see §1 caveat).
>
> Companion docs: `docs/subsystem-status.md` (player-facing register +
> targeting priority), the per-subsystem `docs/*-wall.md` scope docs,
> `docs/function-index.md` (function catalog), `docs/jt-lift-progress.md`
> (auto-generated JT counts — source of truth). This file is the high-level
> burn-down; detail lives in those.

---

## 0. Task burn-down (`#100`–`#144`)

The numbered `#NNN` tasks reconciled against reality (2026-06-26). **45 tasks:
36 done, 6 in-progress, 3 pending** — most "in-progress" are long-running
umbrella campaigns or polish, not blockers.

### ✅ Completed (36)

| # | Task |
|--:|------|
| 101 | Character generation (CODE 17) — create / modify / reroll / finalize / sheet (icon-grid polish tracked separately as #137) |
| 102 | Command-bar / DLItem render (stripes) |
| 103 | Dungeon→menu round-trip black redraw |
| 104 | GLIB glyph blitter (L309c + L2d4e) |
| 105 | Faithful GLIB menu + command-bar buttons |
| 107 | Colour mouse cursor |
| 108 | Char-gen / menu UI alignment (320×200, no 640×400) |
| 109 | jt21 derived-stats recompute + helper tree |
| 110 | jt875 spell-effect / magic-resistance |
| 111 | jt521 area-map render tree (CODE 14) |
| 112 | jt501 area-map line/region renderer (CODE 13) |
| 113 | Play HUD: text shift + command bar |
| 114 | Dungeon HUD frame chrome + layout |
| 116 | (v,h) coordinate migration |
| 117–120 | Bands 2–5 (ranks 101–500) JT lift campaigns |
| 121 | jt290 editor click tool + jt327 record-edit dispatcher |
| 123 | HEIRS.DSN save-A dungeon demo (staging + multi-char loader) |
| 124 | Dungeon movement in jt240/l63c0 walk loop |
| 125 | Event-picture CLUT (merchant colours) |
| 126 | jt199/L6234 walk re-derived vs the 25-slot Mac trace |
| 127 | Resource Manager (FC group cache) + art-loader routing |
| 128 | GEO005 (FORM/AMOD) map cell-data loading |
| 130 | Display perf: 16bpp LUT + asm blit + VBL triple-buffer |
| 133 | Training Hall → Create Character → char-gen wiring |
| 134 | Char-gen character sheet (jt886 6-panel + reroll) |
| 135 | Char-gen finalize chain (level / AC / THAC0 / spells) |
| 136 | FRUA reference MCP server |
| 138 | L618c Modify Character stat editor |
| 139 | L0848 Training Hall roster selection (arrow-key nav) |
| 140 | Add Character screen (jt904 family) — saved pool → party |
| 141 | Party data-model migration (cg_pool → faithful −27928 list) |
| 142 | FAR-pool stage 4: purgeable dispose/reload |
| 143 | Screen-refresh "smear" — cursor save-under |

### 🟡 In progress (6) — umbrella campaigns / polish

| # | Task | Reality |
|--:|------|---------|
| 100 | Play-entry chain (CODE 15/19) | Front-of-game flow works (design → Hall → Load → walk; empty-boot party + faithful View done this session). Remaining = save/load completion + full CODE 15/19. |
| 106 | DOS `.DSN` compatibility | Enhancement, late (ADR-0001 is Mac-first). |
| 115 | Combat / encounter subsystem | Spine + CODE-16 handlers lifted; **runtime-untested**, physical damage + field-render leaves pending. |
| 129 | 3D-view: event-bigpic frame-stomp + left-column clip | 3D view *renders* (l579e blocker resolved); down to two isolated render bugs. |
| 132 | Band 6 (ranks 501–600) JT campaign | Partial (~69/100); demand-driven tail, not load-bearing. |
| 137 | Char-gen icon grid (silhouettes / speed) | Renders; interactivity + draw-speed polish. |

### ⬜ Pending (3)

| # | Task |
|--:|------|
| 122 | Audit hand-decoded JT[1]/JT[2] switches for the off-by-one arm shift |
| 131 | Display: sample input after vsync for 1-frame latency |
| 144 | Off-screen compose: present once per logical screen (faithful jt1146/jt1153 double-buffer) |

> Note: the old `#1`–`#99` IDs predate this tracker; their work is folded into
> the subsystem tables below and the `docs/*-wall.md` scope docs.

---

## 1. Headline coverage (fresh audit, `tools`-grade hard numbers)

Every one of the **1208 jump-table (JT) entries** bucketed by CODE segment and
classified against `boot.c` (lifted = real body · stub = PROBE-only · missing =
no `jtN`-named definition):

| Metric | Count | Note |
|--------|------:|------|
| JT entries called | 1205 | distinct entries the code reaches |
| Done | **943** | **~78%** — 862 lifted + 20 noop + 61 alias |
| PROBE-only stubs | 65 | mostly CODE-13/14 combat-field-render leaves |
| No-def ("missing") | 197 | **over-counts** — see caveat (demand-driven + editor) |

(Numbers from `tools/jt_progress.py`, refreshed 2026-06-26 / HEAD `9c6efb5`.)

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
  `-27928` party-list model, savegame persist. **View Character** opens the
  faithful `jt904` record sheet on the *selected* member (2026-06-26); the
  two-column menu's label↔JT[3]-case remap is decoded (Add↔View were crossed),
  roster names state-coloured (grey / blue-selected) via the faithful `jt25`.
- **Play-entry flow** (2026-06-26): boot lands in an **empty** Hall (Mac-faithful);
  the player builds the party via Load Saved Game / Add — no more boot auto-load.
- **Save / Load**: party round-trip done; ⏸ pending ~10KB design-state block,
  A–J slot pickers polish, boot auto-load.
- **Display polish** (2026-06-26): cursor **save-under** ends the mouse-move
  "smear" (the VBL pointer no longer erases from the live compose buffer).

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

## 3. REMAINING — rewritten 2026-08-02

The old table is gone. It had gone stale in a way that actively cost time: it
still named `l14bc` the keystone (lifted 2026-06-24), `jt512` a blocking stub
(`stub_audit` classes it a FAITHFUL no-op — the Mac body is empty too), audio
"every output leaf stubbed (MUTED)" (713 lines of `sound_falcon.c`, ear- and
video-verified), and save/load's design-state block "the main gap" (it writes a
10 284-byte save; verified live). Four wrong rows in one table is a planning
hazard, so the table was replaced rather than patched.

**The tools are the authority, not this page:** `stub_audit --stubs` / `--arms`
report **0 live gaps, 0 deferred arms**. No reachable PROBE stub exists, so
nothing here can be "blocked by a stub". What follows is graded by EVIDENCE.

### A. Verified working — driven live, by the maintainer or headlessly

Boot -> title -> menu -> design select -> Training Hall -> build/load party ->
dungeon walk (arrows, turns, per-step events) -> 3D view -> AREA automap ->
event text + BIGPIC chains -> treasure -> ENCAMP (VIEW MAGIC REST ALT FIX LOAD
SAVE EXIT) -> **save to an A-J slot (10 284-byte design-state block)** ->
combat (headless auto-turn, `FRUA_CBTPLAY`) -> magic end-to-end (capacity ->
memorize -> rest -> cast, 2026-07-14) -> audio. Five targets build and boot:
Falcon, TT, ST/STE, Amiga AGA, Amiga ECS. Mono boots too, with the Mac art set.

### B. Lifted but NOT live-verified — the real frontier

Nothing here is known broken; nobody has driven it. Each is a drive, not a lift.

| Work | Where | How to verify |
|---|---|---|
| Shop SELL / IDENTIFY | `jt189` / `jt190` | HEIRS shop event cell |
| Inn | `l398a` | gated behind rest, which works |
| Inventory ITEMS / TRADE / DROP | `jt904` submenu | char sheet; the DISPLAY is done |
| `jt251` case 5 (mode-5 redraw hint) | CODE 2 | correct by construction + sibling `jt253`; never seen to fire |
| Save/load on the NON-Falcon ports | CODE 15 | same ENCAMP -> SAVE drive, per target |

### C. Genuinely open

| Work | Note |
|---|---|
| **Boot auto-load** | the one save/load piece still missing |
| **Mono's six chrome families** | ALWAYS/FRAME/GEN/MENU/TITLE/TOPVIEW — lifts the Mac-only caveat (see `docs/TODO.md`) |
| **Present-cost narrowing** | glyph (2 557) + fill (1 653) touch-all announcements dominate; 135 of 200 rows are presented per present on the TT. Palette and cursor are already at ZERO |
| **Play-loop planar measurement** | the "~47% of rows convert" figure is a BOOT number; post-menu screens converted zero |
| **Smooth-scroll + move sound** | cosmetic, deferred |

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

**The game plays, on real hardware, on five targets.** Boot -> party -> dungeon
-> events -> combat -> camp -> save. A Falcon 030@50 runs it from an installed
hard-drive image; engine + data media exist for Falcon/TT, Mega ST, Amiga AGA
and Amiga ECS.

The centre of gravity has moved again — from "can it fight" (yes) to **"has
anyone actually driven every screen?"** Section B is the honest frontier: code
that is lifted, stub-free and unexercised. That is a testing campaign, not a
lifting one, and it is why the editor end-goal (author a small dungeon and play
it) is a better acceptance test than any coverage percentage.

⚠️ **Do not plan from a percentage on this page.** The per-CODE table in §1
counts JT entries, and anything lifted under an `lXXXX` name counts as
"missing" — the editor's 25-41% figures are undercounts of unknown size.
