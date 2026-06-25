# Gold Box All-in-One Engine — Build Roadmap (sliced)

> **▶ Autonomous loop:** the concrete, ordered worklist the full-auto loop pulls from is
> **`BUILD-QUEUE.md`** (top unchecked item next). This ROADMAP is the milestone/rationale view;
> the queue is the actionable to-do. Loop fires per `ScheduleWakeup`; resume phrase
> "resume goldbox full-auto".
>
> **Status:** active (rev. 2026-06-21). Build order for the architecture in `ARCHITECTURE.md`,
> under the decisions in `adr/`, driven by `ORCHESTRATION.md`. Slices are **small and
> independently verifiable**. Each lists: goal · acceptance (tied to ORCHESTRATION quality
> gates) · rough size · recommended role+model.
>
> **Lead game = Champions of Krynn (CoK)** (user decision, 2026-06-21). CoK boots on its
> already-decoded **DOS EGA** art first to get playable fast; the **Amiga art + live
> EGA↔Amiga switch** (the best-of-art headline) lands right after. DQK moves from lead to a
> **breadth/validation** target (its newer HLIB/Gen2 formats validate the engine against the
> other generation once CoK proves the stack).
>
> **Trade-off of CoK-first (accepted):** we solve the *older* generation's unknowns earlier —
> DAX GEO maps, the Krynn-Gen1 ECL opcode table, and two DAX tile-encoding edge cases — rather
> than DQK's better-documented Gen2. CoK's DAX graphics are already decoded, so only maps +
> Gen1 ECL + the tile gaps are on the critical path to "CoK playable (EGA)".

## Quality gates (from ORCHESTRATION.md — every slice's DoD)

- **G1 Typecheck:** `pnpm -r exec tsc --noEmit` clean (engine package stays DOM/Node-free).
- **G2 Unit:** Vitest green; new logic has tests.
- **G3 Golden-file:** any decoder is byte-/pixel-parity vs the Python oracle
  (`tools/*_decode.py`) or a known render — not eyeballed.
- **G4 Headless e2e:** UI/engine paths verified with puppeteer-core + Edge, **zero console
  errors / zero 4xx**; live `/goldbox/` redeploys re-verified against the real URL.
- **G5 No assets:** no copyrighted game data committed or bundled; engine runs on
  user-supplied files located via the Game Library (M0.S5).

## Roles (from ORCHESTRATION.md)

Researcher = Sonnet · Planner/Architect = Opus · Implementer = Opus (hard) / Sonnet
(mechanical) · Verifier = Opus. **No outreach to third parties** — the ECL VM and all formats
are reverse-engineered clean-room in-house (ADR-0003).

---

## Critical-path reverse-engineering unknowns (and where they gate)

| Unknown | Gates | First slice that attacks it |
| --- | --- | --- |
| **DAX GEO map decode (CoK/DoK)** | CoK exploration / automap | **M1.S4** |
| **DAX 8x8 tile plane-order + transparent sub-frames** | CoK dungeon walls/sprites on the EGA set | **M1.S3** |
| **Krynn-Gen1 ECL opcode table / dialect** | CoK events, dialogue, combat triggers | **M2.S0** → **M2.S1** |
| **Amiga DAA palette storage** | CoK Amiga GraphicSet (best art + the live-switch headline) | **M3.S1** |
| **Save format per game** | Companion save-editor, cross-game character import | **M4.S4** |
| **DQK Gen2 GEO/ECL deltas** | DQK as a breadth/validation target | **M3.S4** |

---

## Milestone 0 — Foundation: monorepo + Game Library (asset detection)

Goal: restructure today's `web-engine/` into the shared-core monorepo **without breaking the
live deploy**, and stand up the **"add asset folder reference" → auto-detect game** flow that
the shipping product loads games through. (ADR-0002; ARCHITECTURE.md §4.)

| Slice | Goal | Acceptance | Size | Role/Model |
| --- | --- | --- | --- | --- |
| **M0.S1** | `pnpm-workspace.yaml`; move `web-engine/` → `packages/engine/`; split `src/ui/` → `apps/web/`; root scripts | G1 across workspace; existing Vitest + e2e green; `/goldbox/` redeploys and re-verifies (G4) | M | Implementer / Sonnet |
| **M0.S2** | Add `apps/electron/{main,preload}` via electron-vite; preload `contextBridge`; `electronAssetSource.ts`; native folder picker | App boots, picks a folder, renders the **same** game the web shell does (G1, desktop smoke) | M | Implementer / Opus |
| **M0.S3** | Extract `packages/ui-kit` (indexed render surface + widgets); both shells consume it | Visual parity web vs electron; G1/G4 | S | Implementer / Sonnet |
| **M0.S4** | **Game profiles**: turn per-game manifests into *detection profiles* (signature files/sets, platform, graphicSets, ruleset, data, entrypoints); keep flat-`files[]` viewer back-compat | New profile loads in viewer; old manifests still load (G1/G2/G4) | S | Implementer / Sonnet |
| **M0.S5** | **Asset-folder detection ("Add asset folder reference")**: user points at an install folder → engine scans it, **fingerprints** against game profiles (filename sets + magic-byte signatures), identifies game + platform + available graphic sets, registers it in a local **Library**. Web = `showDirectoryPicker()` (fallback: multi-file input); Electron = native dialog + `fs`. | Unit: fingerprinter IDs CoK-DOS and CoK-Amiga sample file lists to the right profile, rejects junk (G2); E2e: pick the CoK folder → it appears playable in the Library (G4) | L | Implementer / Opus |

---

## Milestone 1 — Champions of Krynn: render + explore (DOS EGA art)

Goal: walk around a CoK map with the real **DOS EGA** art, with the live graphics-set switch
wired (even with one set). Proves render + Library + GraphicSet + map end-to-end on the lead game.

| Slice | Goal | Acceptance | Size | Role/Model |
| --- | --- | --- | --- | --- |
| **M1.S1** | WebGL palette-texture renderer (index tex + 256×1 palette tex; aspect 1.2×); canvas2d fallback retained | Pixel-parity vs `indexedToRGBA` golden output for sample DAX/HLIB frames (G3); G1/G4 | M | Implementer / Opus |
| **M1.S2** | `GraphicSetManager` (resolve + per-frame fallback + `setActive`/`change`); profile `graphicSets` build path | Unit: resolve hits active then fallback; bounds-safe `has()` (G2); switch button redraws (G4) | M | Implementer / Opus |
| **M1.S3** | Close the remaining **DAX graphics gaps for CoK**: 8x8 tile plane-order + shared palette, and the transparent sub-frame variant (walls/sprites) | Golden: CoK tile/wall/sprite RGBA parity vs the Python oracle (G3); update `docs/dax-format.md` | M | Implementer / Opus |
| **M1.S4** | **CoK DAX GEO map decode** (reverse the GEO container) → engine-neutral `GameMap` | Golden: decoded CoK map cells match a hand-verified sample (G3); update findings (G3) | L | Researcher→Implementer / Opus |
| **M1.S5** | Exploration loop on CoK: move/turn, wall collision, party state, `entrypoints`; first-person + overland viewport | E2e: load CoK from the Library, spawn at entry, walk N/E/S/W with EGA art, no console errors (G4); G1/G2 | M | Implementer / Opus |

**End state:** Champions of Krynn is **walkable** in browser + Electron with DOS EGA art.

---

## Milestone 2 — Champions of Krynn: ECL VM + combat + ruleset (fully playable)

Goal: events, dialogue, and combat run on CoK via the from-scratch VM and the AD&D-1e +
Dragonlance ruleset. Retires the highest-risk unknown (Gen1 ECL).

| Slice | Goal | Acceptance | Size | Role/Model |
| --- | --- | --- | --- | --- |
| **M2.S0** | **Research:** derive the **Krynn-Gen1 ECL opcode table** empirically — disassemble CoK `ECL*.DAX` `DATA` blocks, cross-check against in-game behavior; write `docs/engine/research/ecl-krynn-gen1-opcodes.md` | Doc enumerates opcodes with confidence labels + sample disassembly (research gate) | L | Researcher / Sonnet |
| **M2.S1** | ECL **disassembler** (`bytes`+`EclDialect` → `EclOp[]`); Krynn-Gen1 dialect table | Golden: stable disassembly of sample CoK blocks matching M2.S0 (G3/G2) | M | Implementer / Opus |
| **M2.S2** | `EclVM` interpreter + `EclHost` interface; flags, text, ask, teleport, passTime, give/take, encounter | Unit: scripted CoK-style programs run against a **mock EclHost** to expected effects (G2); G1 | L | Implementer / Opus |
| **M2.S3** | `RulesetPlugin` + registry; **`addnd1e`** core (THAC0, XP, saves, level-up, spell memorize/cast) | Unit: THAC0/XP/save tables match known AD&D-1e values (G2) | L | Implementer / Opus |
| **M2.S4** | **`addnd1e-dragonlance`** overlay (Solamnic Knight, Kender, Tinker Gnome, lunar/holy magic, CoK class/race set & good-aligned party rules) | Unit: Dragonlance-specific rules vs known values (G2) | M | Implementer / Sonnet |
| **M2.S5** | Combat loop (initiative grid, move/attack/cast via ruleset; seedable RNG); `host.startCombat` → outcome | Golden: seeded CoK combat produces a deterministic log matching a fixture (G3); G2 | L | Implementer / Opus |
| **M2.S6** | Wire VM+combat+ruleset into the CoK player; CoK `MON1CHA`/`ITEM1` DAX loaders feed `interpretMonster/Item` | E2e: trigger a CoK event → dialogue → combat → outcome, no console errors (G4) | M | Verifier+Implementer / Opus |

**End state:** **Champions of Krynn playable start-to-finish** (DOS EGA art) in browser +
Electron. *First public-milestone candidate (VISION "done enough to share").*

---

## Milestone 3 — Best-of art + breadth: Amiga live-switch, then DoK & DQK

Goal: deliver the headline **live EGA↔Amiga switch** on CoK (best-of art), then prove breadth
by bringing up DoK (same Gen1) and DQK (Gen2 — validates the engine on the newer generation).

| Slice | Goal | Acceptance | Size | Role/Model |
| --- | --- | --- | --- | --- |
| **M3.S1** ✅ | **Reverse DAA palette storage**; complete the Amiga loader to emit a correct `Palette` for CoK | Golden: CoK Amiga frame RGBA matches a known-good reference render (G3); update `docs/daa-format.md` | L | Researcher→Implementer / Opus | **Done** — DAA loader embeds the per-frame 32-color 12-bit Amiga palette (`loaders/daa.ts`); BIGPIC1 golden-verified vs DOS. |
| **M3.S2** ◐ | CoK profile with **Amiga + DOS-EGA** graphic sets; **live in-game switch** (the headline feature) | E2e: load CoK, walk, press switch → Amiga↔EGA with no reload/black-frame, no console errors (G4) | M | Implementer / Opus | **BIGPIC slice done** (live A/B switch in `?gswitch=1`, e2e-proven). *Full in-dungeon* wall switch blocked on the unsolved 6-byte DAA sub-frame format (`8X8D*.DAA`). |
| **M3.S3** | **Death Knights of Krynn** (Krynn-Gen1: reuse DAX GEO + Gen1 ECL + Dragonlance ruleset) → playable + Amiga set | E2e: DoK loads from Library, intro→first dungeon→first combat (G4) | L | Implementer / Opus |
| **M3.S4** | **The Dark Queen of Krynn** (Gen2: HLIB GEO + Gen2 ECL dialect deltas) → playable; VGA graphic set | E2e: DQK loads, explore + an event + combat (G4); Gen2 disasm sanity (G2) | L | Implementer / Opus |

**End state:** all three Krynn games playable; **live EGA↔Amiga switching demonstrated on CoK**;
DQK validates the engine against Gen2/HLIB.

---

## Milestone 4 — Gold Box Companion as native panels

Goal: fold the highest-value GBC features in as first-class panels (read-only first). Cheap
because we own engine state (gold-box-companion research §5.5).

| Slice | Goal | Acceptance | Size | Role/Model |
| --- | --- | --- | --- | --- |
| **M4.S1** | **Automap panel** (render engine-tracked visited tiles; notes in JSON sidecar) | E2e: explore CoK → map reveals; notes persist (G4); G2 | M | Implementer / Sonnet |
| **M4.S2** | **HUD panel** (party HP/XP/effects, always-visible) | E2e: HUD reflects live party state through combat (G4) | S | Implementer / Sonnet |
| **M4.S3** | **Journal panel** (decode CoK journal/string tables; removes copy-protection lookup) | Golden: decoded journal strings match expected text (G3); panel shows them (G4) | M | Implementer / Sonnet |
| **M4.S4** | **Character/Save editor panel** (edit in-memory party + our save; reader for original CoK save) | Unit: round-trip our save; read a sample original save (G2/G3) | M | Implementer / Opus |
| **M4.S5** | **ECL inspector panel** (live `step()` disassembly + flag view — GBC's killer feature, reuses M2 VM) | E2e: step through a running CoK script, flags update (G4) | M | Implementer / Opus |
| **M4.S6** | **Reference DBs** (monster/item/spell browser from decoded tables + ruleset interpretation) | Panel lists entries for CoK + DQK (G4); G2 | M | Implementer / Sonnet |

**End state:** VISION first-public-milestone fully met (CoK start-to-finish, ≥2 graphic sets,
read-only Companion panels). **Pause-for-user gate before any *public* release** (ADR-0001,
ORCHESTRATION "when to pause"; confirm the Library asset-gating flow first).

---

## Milestone 5 — Authoring (FRUA-superset) + full roster

Goal: create/edit modules; import FRUA; playtest through the same engine (ADR-0006); prove the
ruleset interface against the wider roster.

| Slice | Goal | Acceptance | Size | Role/Model |
| --- | --- | --- | --- | --- |
| **M5.S1** | `ModulePackage` model + loader (authored module loads via the same engine path) | Unit: a hand-authored minimal module loads + plays headlessly (G2/G4) | M | Implementer / Opus |
| **M5.S2** | **FRUA importer** (`.DSN` + decoded art → `ModulePackage`; 35+ event types mapped) | Golden: imported sample design's maps/events match FRUA structure (G3) | L | Implementer / Opus |
| **M5.S3** | Map editor (`apps/author`): tiles, walls, zones, entrypoints | E2e: place tiles/walls, save, reload, playtest (G4) | L | Implementer / Opus |
| **M5.S4** | Event editor + scripting (35+ types + variables/arithmetic/branching + combat-outcome trigger; compiles to VM IR) | E2e: author an event chain, playtest fires it correctly (G4); G2 | L | Implementer / Opus |
| **M5.S5** | Database editors (monsters/items/spells/classes/races) + ruleset picker / custom ruleset | E2e: edit a monster, see it in playtest; switch ruleset (G4) | L | Implementer / Opus |
| **M5.S6** | Broader roster (PoR, CoAB with `addnd1e`; Savage Frontier with `addnd2e`; Buck Rogers with `buckrogers`) — detection profiles + boot | Each loads from the Library + boots to exploration (G4); `buckrogers` ruleset unit tests (G2) | M | Implementer / Sonnet |

**End state:** authoring superset shipped; roster breadth proven; `buckrogers` validates the
ruleset interface against the most divergent system.

---

## Sequencing summary

```
M0 monorepo + Game Library/detection
   └─▶ M1 CoK render+explore (EGA)
          └─▶ M2 CoK ECL VM+combat+ruleset   ◀── FIRST PLAYABLE GAME (Champions of Krynn)
                 ├─▶ M3 Amiga live-switch (best-of art) + DoK + DQK (BREADTH/VALIDATION)
                 ├─▶ M4 Companion panels (read-only first)
                 └─▶ M5 Authoring + full roster
```

CoK leads per user decision; it ships on already-decoded DOS EGA art first, then gains the
Amiga set + live switch in M3. DQK is brought up in M3 to validate the engine against the
newer HLIB/Gen2 generation once the stack is proven on CoK.

---

## Decisions — status

| # | Decision | Status |
| --- | --- | --- |
| 1 | **Public product name** (trademark-safe; not "Gold Box/D&D/Dragonlance/Forgotten Realms/SSI/Krynn") | **Resolved: Greybox** (user, 2026-06-21); release folder renamed `/greybox/`. Final trademark clearance still wanted before a *public* launch. |
| 2 | Email Simeon Pilgrim (COAB) | **Resolved: NO** — clean-room in-house (ADR-0003) |
| 3 | Email Joonas Hirvonen (GBC) | **Resolved: NO** — clean-room in-house |
| 4 | First game | **Resolved: Champions of Krynn** (roadmap re-sequenced) |
| 5 | Code signing | **Resolved: NO** — open-source freeware, unsigned builds; document the SmartScreen/Gatekeeper warning (ADR-0002) |
| 6 | Tauri vs Electron | Electron baseline; revisit only if install size becomes a real complaint (post-alpha) |
| 7 | **Asset loading UX** | **Resolved: folder-reference + auto-detect** (M0.S5 Game Library) |

Still genuinely needing the user before a *public* release: final **product-name trademark
clearance** (#1) and the **asset-gating confirmation** at the M4 pause gate.

## Build progress (live)

Committed on branch `greybox` (newest first):
- **C4.2b-2b-2c-ii — The Dark Queen of Krynn: PARTYSTRENGTH (0x1d) = a strength gate, not a roster-filler.**
  A probe over the real DQK ECL (area 23 etc.) **refuted** the earlier Path-B hypothesis ("engine
  auto-populates the roster, scaled to party level"): `0x1d` is *consistently followed by* `IF_GE`/`IF_EQ`
  (e.g. `PARTYSTRENGTH c18:128 ; IF_GE`), so it behaves like `COMPARE` — it computes a party-strength
  scalar and leaves `{left: strength, right: threshold}` in the cmp register for the next `IF_*`. `vm.ts`
  `case 0x1d` now sets that cmp + emits a `partyStrength` effect (was a no-op → `unhandled`, which left the
  following branch reading a stale cmp), corroborated by `ecl-krynn-gen1-opcodes.md` ("check/compute party
  strength"). The strength *formula*, the operand's *units*, and the gate *direction* are **unrecoverable
  from disk**, so the host supplies the scalar via new `EclVmOptions.partyStrength` (**default 0** →
  conservatively fails `IF_GE` gates, deterministic, no spurious combat). The roster stays `LOAD_MONSTER`'s
  job; the monster-0 fallback for genuinely-empty rosters is unchanged. Research §4 correction box updated.
  Gates: tsc 0 · **540 engine tests** (+3: IF_GE open/shut at strength 200/0, memory-variable threshold) ·
  34/34 e2e (`vmRun1At` 5/18/66 still roster=0) · build + deploy live (HTTP 200).
- **C4.2b-2b-2c-i — The Dark Queen of Krynn: vmRun1 per-step main loop wired into the walk.** The DQK
  combat trigger (C4.2b-2b-2c) was SPLIT into three; this first slice wires the per-step **vmRun1**
  handler (header far-pointer slot 0) — the COAB engine runs it every walk step *before* SearchLocation
  (wandering-encounter tickers, area-exit triggers, the `[0x0097]` encounter cooldown). New engine
  `primeVmRun1` + `dialogueForVmRun1` mirror `primeSearchLocation`/`dialogueForCell`, both now backed by
  a shared `primeHandler` so they carry identical seed/lifecycle semantics. Host `fireSearchLocation`
  runs vmRun1 then SearchLocation on **one persisted `areaMemory`** (cooldown survives across steps),
  folds both into the event line, and picks the fight as `vmRun1.roster ?? searchLoc.roster`; the drain
  loop `choose(0)`s the few real areas whose vmRun1 pauses on input instead of blocking the walk.
  Probe-confirmed areas 5/18/22/23/30/66 expose an in-range vmRun1 pointer — matching research §5/§6
  exactly (66@0x8592, 18@0x833e, 5@0x8429) — and run to a terminal status with no spurious combat under
  baseline position-only seeding. PARTYSTRENGTH (-ii) and cell-descriptor/cooldown seeding (-iii) follow.
  Research: `docs/engine/research/combat-trigger-c4.2b-2b-2b.md`. Gates: tsc 0 · **535 engine tests**
  (+4: synthetic Gen2 LOAD_MONSTER→COMBAT, real-`ECL.GLB` terminal-status sweep) · 34/34 e2e (`vmRun1At`)
  · build + deploy live (HTTP 200).
- **C4.2b-2b-2b-ii — The Dark Queen of Krynn: MONCHA dec-path records (stat block survives in the
  decompressed member).** A probe over the remaining failing type-8 records found a clean root cause:
  several carry their ability/stat block **already expanded** in the decompressed member, so
  `reconstructDqkRecord`'s second `monchaRLE` pass *re-compresses* it into garbage and the reconstructed
  decode fails. A new **dec-path** fallback in `decodeFullStatMonster` anchors `findAbilityBlock`/
  `sniffAtAbility` directly on the non-reconstructed `dec` bytes (reached only after the clean and recon
  paths fail; gated identically so character/NPC rolled-ability records stay out). **decoded 37 → 46**
  (monster-framed 29 → 38; NPCs unchanged at 8). Hand-verified GIANT ANEMONE (AC 2 / HD 16 / 1d4) and
  GIANT SQUID (AC 3 / HD 12 / 1d6); the same strong stat gate also recovered 7 more monster-framed
  records (FIRESHADOW, 2 HEADED TROLL, GHAST, WYNDLASS, SIVAK DRACONIAN, ENCHANTED KAPAK…). **GHAST
  decodes to AC 4 / HD 4 — an exact AD&D Monster Manual match**, proving the dec-path reads real stat
  blocks rather than coincidental in-range runs. The genuinely-blocked remainder (SEA SNAKE damage=0,
  HUGE CROCODILE false block, EYE OF THE DEP / GORGON fragmented, SEA DRAGON THAC0 anomaly, 5 dragon
  HEAD sub-records, THENOL FNATIC / PRINCE ALHOOK character-framed) is split out as **C4.2b-2b-2b-iii**.
  Research: `docs/engine/research/moncha-decpath-c4.2b-2b-2b-ii.md`. Gates: tsc 0 · **531 engine tests**
  · 34/34 e2e · build + deploy live (HTTP 200).
- **fix(explore) — first-person EGA floor no longer turns black one step from the party.** The dungeon
  viewport filled the floor backdrop only from the buffer vertical midline (y=84) down, painting
  everything above as black ceiling; but the floor recedes *up* toward a horizon above center, so the
  receding floor band y[62,84) read as black. Decoded the original `Draw3dWorldBackground` (COAB ovr031:
  sky y[24,68), 2-px black divider y[68,70), floor from screen y=70) and anchored the ceiling/floor split
  at the true horizon — buffer `y=62` (= screen 70 − the WALL_VIEWPORT.y0 offset) — in both the RGBA and
  indexed render paths (still byte-identical). Verified on a real CoK dungeon (ECL1 #34): the floor band
  below y=62 is now 0 black pixels. +1 regression golden. 530 engine tests · 34/34 e2e · deploy live.
- **C4.2b-2b-2b — The Dark Queen of Krynn: MONCHA short-tail type-8 monsters (HYDRA + UMBERHULK).** The
  two fully-traced type-8 records whose standard ability-anchored stat block fails now decode; full-stat
  decode **35 → 37**. **HYDRA** uses a **compact / short-tail layout** — multi-attack monsters pack
  attack/damage/AC 21 bytes earlier than the single-attack layout (`COMPACT_DELTA`: A+42 attack count =
  8 heads, A+43 ndice, A+44 dsize = 1d12/head, A+45 bonus, A+46 ac_raw), with THAC0/HD/saves/XP still at
  the standard offsets → **AC 5 / THAC0 7 / HD 16 / 1d12**, an exact Monster Manual cross-check and all
  from data. New `sniffCompactMonster` gates on AC ∈ [-12,16] + HD ≥ 1, which cleanly rejects the dragon
  "HEAD" sub-records (their `A+46` is 0 → AC 60, HD reads 0). **UMBERHULK**'s raw AC byte `0x3a` equals a
  valid `monchaRLE` COPY opcode, so the compressor consumed it as an instruction length and it is absent
  from disk (a data-encoding pathology, not a decoder bug); THAC0 12 / HD 10 / 2d10 decode from data and
  AC is injected from canon (`CANON_AC` = AC 2, AD&D MM) with the record flagged **`acSource: 'canon'`**
  for audit honesty, gated on `sniffAtAbilityExceptAc` + the name being in the table. The damage flat
  bonus is now read as a **signed** byte (0xfd = −3, not +253) — fixes UMBERHULK's 2d10−3, a no-op for
  the bonus-0 monsters. `MonsterRecord` gains optional `acSource: 'canon'`. The remaining failing type-8
  records (SEA DRAGON THAC0 anomaly, 5 dragon HEAD records, 8 ability-block-not-found) are a distinct
  unsolved failure mode → split out as **C4.2b-2b-2b-ii**. Research:
  `docs/engine/research/moncha-shorttail-c4.2b-2b-2a.md`. Gates: tsc 0 · **529 engine tests** · 34/34
  e2e · build + deploy live (HTTP 200).
- **C4.2b-2b-2a — The Dark Queen of Krynn: MONCHA character/NPC class-based stat decode.** The
  character-framed records identified in C4.2b-2b-1 (rolled ability spreads — Black Ogre, Dark Wizard,
  the named bosses) no longer stop at name+category; they decode to real combat stats. New
  `decodeNpcRecord` (`loaders/moncha.ts`) reads from the ability block via **NPC_DELTA**: thac0
  `60−rec[A+15]`, **stored** hp `rec[A+17]` (not rolled), saves `A+19`, level `rec[A+25]`, ac
  `60−rec[A+67]`, class `rec[A+71]`; validated thac0∈[0,30]/hp∈[1,255]/level∈[1,40]. Routing: monster
  decode first, then an NPC branch gated on `isRolledAbility` (some current score > 10) **and**
  `!isMonsterDefaultAbility` — the latter extended to also reject the single-INT-18 intelligent-monster
  pattern (kills the UVWW. false-positive; the >10 signature kills the ZOMBIE undead false-positive).
  Result: full-stat decode **27 → 35**, exactly **8 NPCs** (BLACK OGUE, THENOL WIARD, DRK WIZARD, BAKAI
  SHAMAN, SHARMAN, GNOME TINKE, SELIA, TASLEHOFF). **BAKAI SHAMAN class 8 · level 9 · THAC0 14** is an
  exact AD&D 1e cleric match (key cross-check). Skipped a researcher's proposed "INT-18 dragon" fix
  after probing it — the 5 chromatic dragons already decode via the type-8 special-case, so it would
  solve a non-problem and risk regression. `MonsterRecord` gains optional `npcClass`/`npcLevel`;
  `decodeNpcRecord` exported. Research: `docs/engine/research/moncha-character-c4.2b-2b-2.md`. Gates:
  tsc 0 · **528 engine tests** · 34/34 e2e · build + deploy live (HTTP 200). Remaining of the old
  C4.2b-2b-2 split out → **C4.2b-2b-2b** (short-tail type-8 monsters) + **C4.2b-2b-2c** (combat trigger).
- **C4.2b-2b-1 — The Dark Queen of Krynn: MONCHA non-type-8 record framing split.** Resolved what the
  "non-type-8 character/NPC records (a distinct layout)" actually are: `MONCHA.GLB` mixes **two combat
  framings** that share the container, RLE scheme and name scan but differ at the stat block.
  **Monster-framed** records (type 8 *and* many type 0/3/12 — Sahuagin, Spectre, Wraith, Fire Giant,
  Skeleton Warrior…) carry the **monster-default ability block** (every score `0x0a`, undead a few `0x03`)
  followed by the real combat fields, and decode to canon via the same ability-anchored deltas the type-8
  monsters use — **Sahuagin AC 5/1d2, Spectre AC 2/1d8, Wraith AC 4/1d6, Fire Giant AC 3/5d6** — lifting
  full-stat decode **20 → 27**. **Character/NPC-framed** records (Black Ogre, Dark Wizard, the named
  bosses) carry a *rolled* ability spread + placeholder combat fields (their real combat is class/level/
  equipment based) and are honestly categorized `npc`, name + category only. Exact discriminator: **≥4 of
  6 current ability scores == 10 and none > 10**. Engine `isMonsterDefaultAbility` / `classifyCategory`
  (`loaders/moncha.ts`); a validated record reports `category: monster` regardless of its raw type byte.
  e2e `bestiaryCategories` seam + canon assertions. Research: `docs/engine/research/moncha-dqk.md`. Gates:
  tsc 0 · **526 engine tests** · 34/34 e2e · build + deploy live (HTTP 200). Remaining → **C4.2b-2b-2**
  (full character/NPC class-based stat model + short-tail type-8s + the combat trigger).
- **C5b-2b-2b — The Dark Queen of Krynn: `SOUNDS.GLB` `DIG4` digitized SFX decoded.** Reversed DQK's last
  SFX container — the in-engine `SOUNDS.GLB` (HLIB DATA, tag `DIG4`, 13 raw members). The 4-byte member
  record is a **big-endian `u16` sample rate** (7418/11127/5564 Hz, 1:1 with the VOC's 7407/11111/5556) + a
  zero high word, then **raw 8-bit unsigned PCM** (centre 0x80) — *not* ADPCM (the IMA theory gave full-scale
  noise and was rejected). Member order = `DQK_SFX_NAMES`: each decoded member's waveform shape
  cross-correlates with the same-id `SFXDQ.VOC` effect (lightning #6 r≈0.97, flame #1 0.83, walk #8 0.82,
  die #3 0.81, crackle #12 0.79), and the rate buckets land where the VOC predicts (high-rate = die #3 &
  lightning #6, low-rate = fireball #9 — independent confirmation of both codec and order). New engine
  decoder `audio/dig.ts` (`decodeSoundsGlb`/`decodeDigMember`/`sniffDig`); `gameAudio` prefers the in-engine
  DIG4 bank with a `SFXDQ.VOC` fallback; `previewDigSfx` seam + e2e bucket/centring assertions. Research:
  `docs/engine/research/audio-dqk.md`. Gates: tsc 0 · **525 engine tests** · 34/34 e2e · build + deploy live
  (HTTP 200; bundled `SOUNDS.GLB` 200). DQK audio (music + both SFX banks) now complete; optional chunked
  streaming / cycle-exact EG deferred. Next: Phase D (companion panels) or C4.2b-2b.
- **C5b-2b-2a — The Dark Queen of Krynn: OPL2 music → WebAudio, graphics-mode-bound, + XMIDI loops.**
  DQK's OPL2 music now actually **plays**, bound to the graphics switch like its SFX. `AudioManager`
  gained a looping **PCM music track**: `setPcmMusic` attaches a pre-rendered mixdown to a platform,
  `playMusic` plays it as a `loop=true` `AudioBufferSourceNode` (taking precedence over the SMUS
  square-wave path), and `musicUnavailableReason` now counts it. `gameAudio`'s `'dqk'` branch renders
  the first ADDQ song (`renderXmiSong` @22050) and arms it on the **DOS-VGA** platform **alongside the
  13 `SFXDQ.VOC` effects**, so switching the graphics mode to DOS DQK plays its music + digitized SFX
  together — the same platform binding CoK/DoK Amiga SMUS uses. Engine `expandXmiLoops` unrolls the
  XMIDI **for-loop controllers** (116 for-loop start / 117 next-break end; nested via a stack; an
  infinite count-0 loop bounded by `maxIterations`) into a flat absolute-tick timeline that drives the
  mixer, with the whole mixdown marked as the loop body (`repeatSamples = length`) so WebAudio loops it
  seamlessly. The ADDQ songs carry only CC#114 (no 116–119), so a synthetic 116/117 song unit-tests the
  unrolling (a count-3 loop yields 3× the body at the right tick shifts; an infinite loop caps at
  `maxIterations`). Research: `docs/engine/research/audio-dqk.md`. Gates: tsc 0 · **520 engine tests**
  (+2: finite-loop unroll + tick-shift, infinite-loop cap) · **34/34 e2e** (`?dqk=1` boot: `gameAudio`
  reports `musicArmed` on DOS-VGA with the 13 SFX, and enable/disable is idempotent) · built + deployed
  live (HTTP 200; bundled `ADDQ1.XMI` + `INSTR.AD` 200). C5b-2b-2 split → **C5b-2b-2b** (`SOUNDS.GLB`
  `DIG4` + optional chunked streaming / cycle-exact EG). Next: C5b-2b-2b.
- **C5b-2b-1 — The Dark Queen of Krynn: OPL2 full-song mixdown via a 9-channel mixer.** Turned the
  *score* into audio: a new `audio/oplPlayer.ts` (`renderXmiSong`) drives the C5b-2a pinned voice from
  a decoded XMI event stream to one PCM mixdown. A **tempo map** converts XMIDI ticks→seconds at
  **PPQN 60** (default 120 BPM); a **9-voice allocator** assigns notes in start order and **steals**
  the soonest-ending slot when all nine OPL2 channels are busy (cutting the stolen note's release), so
  polyphony is bounded by 9; per-channel **program** + **CC#7 volume** scale each note; **percussion**
  (MIDI channel 9) resolves its timbre by *note number in bank 100* — the `INSTR.AD` side of XMIDI's
  bank-127↔100 map (`ADDQ1`'s `TIMB` marks 38/36/46 = snare/bass/hihat as bank 127). Each voice runs the
  pinned 2-operator synth with a real **attenuation-domain envelope** (9-bit-style dB EG, KSR-scaled
  rates), `fnum`/`block`-quantized OPL pitch and KSL attenuation — replacing C5b-2a's amplitude-
  exponential approximation. **Proven offline without a reference WAV:** `ADDQ1` renders to **63.0 s,
  1853 notes** (490 on the percussion track), **peak polyphony 9/9** (146 stolen), **voiced 77% across
  the whole timeline** (not just the opening), and the length matches ticks×tempo; `ADDQ2` peaks at 8
  voices / 93% voiced, `ADDQ3` at 52 s. `?dqk=1` renders the first song live in the audio readout;
  `window.__dqk.previewDqkSong` seam. C5b-2b split → **C5b-2b-2** (WebAudio *streaming* playback + the
  XMIDI loop controllers 116–119 + binding the music to the graphics-mode switch + `SOUNDS.GLB` `DIG4`).
  Research: `docs/engine/research/audio-dqk.md`. Gates: tsc 0 · **518 engine tests** (+3: synthetic
  two-note song voiced+length+percussion, a 20-note cluster proving polyphony ≤ 9 with stealing, and a
  golden `ADDQ1` mixdown asserting length/voiced-across-timeline/poly≤9/percussion-on-bank-100) ·
  **34/34 e2e** (`?dqk=1` asserts a multi-second song voiced >50% across the timeline, polyphony ≤ 9,
  percussion present) · built + deployed live (HTTP 200; bundled `ADDQ1.XMI` 200). Next: C5b-2b-2.
- **C5b-2a — The Dark Queen of Krynn: OPL2 voice + the `INSTR.AD` operator-map pin.** Pinned the
  12-byte `INSTR.AD` operator field map as **interleaved `(modulator, carrier)` register pairs** — a
  leading pad byte, then `(0x20, 0x60, 0xE0, 0x40, 0x80)` per operator + `0xC0` feedback/connection —
  by column analysis over the bank's documented timbres. It's the *only* layout where the
  audibility-critical fields cohere: every carrier `0x40` total-level is **0** (full volume), every
  carrier `0x60` attack-rate is **≥ 1** (notes attack), both `0xE0` columns are confined to **0–3**
  (valid OPL2 waveforms), and `0xC0` stays 0–3; the drum (patch 117) reads back **additive** with a
  fast carrier decay, the clarinet (51) a **half-sine modulator** — exactly those timbres. SBI
  interleave is ruled out (carrier attack lands on a mostly-zero column → silent notes). A new pure-TS
  OPL2 2-operator voice (`audio/opl2.ts`: the four OPL2 waveforms, an exponential ADSR envelope,
  FM/additive routing, modulator feedback) + `parseAdLibPatch`/`renderOplNote`/`pcmRms`/`dominantHz`
  is the **validator** — fed a real timbre it renders **voiced PCM whose pitch tracks the note**
  (synthetic sine-FM locks 440 Hz ±30; all 18 documented patches render voiced + in-band). The map is
  **pinned-by-synthesis**, exactly as the brief required. `?dqk=1` renders patch 51 (clarinet) at
  middle C live in the audio readout; `window.__dqk.previewOplNote` seam. C5b-2 split → **C5b-2b**
  (9-channel full-song playback: exact OPL2 dB rate/KSL envelope tables + fnum/block pitch, the XMI
  event driver, XMIDI loop controllers, the bank-127↔100 percussion map, `SOUNDS.GLB` `DIG4`, and
  binding the music to the graphics-mode switch). Research: `docs/engine/research/audio-dqk.md`.
  Gates: tsc 0 · **515 engine tests** (+7: pinned-map field extraction, synthetic FM voiced-at-pitch,
  louder-TL-is-louder, and a golden rendering all 18 documented timbres voiced + the additive drum) ·
  **34/34 e2e** (`?dqk=1` renders patch 51 voiced+pitched and patch 117 additive offline) · built +
  deployed live (HTTP 200; bundled INSTR.AD 200). Next: C5b-2b.
- **C5b-1 — The Dark Queen of Krynn: digitized sound effects (`SFXDQ.VOC` → 13 named PCM effects,
  platform-bound).** Reversed DQK's sound-effect bank: a standard Creative Voice File whose **marker
  blocks 0..12** name the effects (cast/flame/sorcery/die/sling/hit/lightning/swing/walk/fireball/bow/
  sploosh/crackle — hackdocs `UASOUND.TXT`), each a type-1 sound at 7407/11111/5556 Hz with **codec
  1 = Creative 4-bit ADPCM**. The ADPCM model was the crux: ffmpeg's `ADPCM_SBPRO_4` tag for VOC
  codec 1 **railed every effect to the negative limit** (a probe showed min −1.0 / max +0.1 / mean far
  below 0 — wrong); the **hardware-accurate Sound-Blaster decoder** (DOSBox's symmetric
  `scaleMap`/`adjustMap`: 8-bit reference + signed delta table indexed by `nibble + scale`) gives
  DC-centred audio (|mean| < 0.01, peaks ±0.2…0.33) across all 13. New engine
  `decodeVoc`/`sniffVoc`/`DQK_SFX_NAMES` (`audio/voc.ts`) → normalized `Float32` `AudioClip`s (codec 0
  plain PCM also handled). Web: `AudioManager.registerDigitizedSfx` builds a platform straight from
  decoded clips, and `gameAudio` gains a **`'dqk'`** game that registers the 13 effects as the
  **DOS-VGA** platform's SFX and maps the action cues (melee→hit, arrow→bow, cast→cast,
  fireball→fireball, death→die, door→sploosh, miss→swing) onto them — so a DQK fight on the shared
  combat overlay plays DQK's **own** digitized sounds, platform-bound exactly like CoK/DoK's Amiga
  8SVX. Music stays honestly unarmed ("OPL2 synthesis is C5b-2"). `?dqk=1` reports the effects;
  `window.__dqk.sfxNames/sfxCount/gameSfxNames/playSfx` seams. C5b split → **C5b-2** (OPL2 music
  synthesis: pin the operator map, drive `opl3`/a pure-TS OPL2 from the XMI stream, XMIDI loops,
  `SOUNDS.GLB` `DIG4`). Research: `docs/engine/research/audio-dqk.md`. Gates: tsc 0 · **508 engine
  tests** (+4: synthetic VOC PCM/ADPCM/marker round-trip + a golden asserting the 13 named effects are
  centred & in-range) · **34/34 e2e** (`?dqk=1` asserts 13 SFX incl. cast/fireball/die/crackle, armed
  in gameAudio on DOS-VGA, an action cue fires) · built + deployed live (HTTP 200; bundled SFXDQ.VOC
  200). Next: C5b-2.
- **C5a — The Dark Queen of Krynn: audio containers (Miles XMIDI music + AdLib timbre bank decode).**
  Took the C5 fallback after confirming C4.2b-2b's remaining MONCHA reversing is genuinely blocked: a
  probe showed the still-failing type-8 records are **compact / variant layouts** (HUGE CROCODILE packs
  THAC0+HD but its AC/damage fall past its 103-byte re-expanded end; ENORMUS SPIDER/GIANT SQUID carry
  non-default ability blocks), and the non-type-8 records are a separate character layout that yields
  garbage (HD-22 / 1d2) under the monster anchor — with **no `.LST` field spec in the hackdocs**, pushing
  further would fabricate stats. So per the loop's "reversing blocked → take C5" rule, pivoted to DQK
  audio. DQK is a **Miles/AIL** title: one XMIDI score per device family (`ADDQ*`=AdLib/OPL2,
  `RODQ*`=Roland, `TYDQ*`=Tandy, `PCDQ*`=PC-speaker) resolved through a `.ADV` driver + the `INSTR.AD`
  AdLib timbre bank. Reversed both containers. **XMI** = EA-IFF `FORM XDIR`(+`INFO` count) + `CAT XMID`
  holding one `FORM XMID` per song (`TIMB` (patch,bank) list + `EVNT` stream); the **XMIDI event stream**
  differs from SMF in two ways — delay is a run of bytes `<0x80` **summed** (not a VLQ), and note-on
  carries an **explicit VLQ duration** (no note-off). **`INSTR.AD`** = `{u8 patch,u8 bank,u32 offset}`
  table → `0xFFFF` terminator → 199 × 14-byte OPL2 records (`u16` length + 12 operator bytes), banks 0
  (GM melodic) + 100 (percussion). New engine `decodeXmi`/`sniffXmi` (`audio/xmi.ts`) +
  `decodeInstrAd`/`sniffInstrAd`/`adLibKey` (`audio/instrAd.ts`), reusing the IFF substrate. Golden vs
  the real files: ADDQ1 = 1 song / 10 timbres / **1853 note-ons** / 120 BPM, **ADDQ2 = 6-song catalogue**
  (multi-FORM `CAT `), ADDQ3 = 1 song ~107 BPM, INSTR.AD = **199 instruments** at strict +14 offsets.
  Web `?dqk=1` reports the decoded music ("3 AdLib/OPL2 XMIDI files … N note-ons … 199 OPL2 timbres");
  `window.__dqk.audioLoaded/audioSongFiles/audioTotalNotes/audioBankInstruments/audioSongSummaries` seams.
  C5 split → **C5b** (OPL2 synthesis: pin the 12-byte operator-field map, drive a MIT `opl3` from the XMI
  stream → WebAudio, XMIDI loop controllers, VOC/`DIG4` SFX, graphics-mode binding). Research:
  `docs/engine/research/audio-dqk.md`. Gates: tsc 0 · **504 engine tests** (+8: synthetic XMI round-trip
  incl. summed-delay/VLQ-duration/meta, synthetic AdLib bank, + goldens for ADDQ1/ADDQ2/INSTR.AD) ·
  **34/34 e2e** (`?dqk=1` asserts the 3 files decode, >2000 note-ons, 199-instrument bank, ADDQ2's 6-song
  catalogue) · built + deployed live (HTTP 200; bundled ADDQ1.XMI + INSTR.AD 200). Next: C5b.
- **C4.2b-2a — The Dark Queen of Krynn: full monster stats via inline-stat-block reconstruction
  (type-8 decode 5 → 20).** Cracked the post-name framing the prior slice left open: the monster
  record's **stat block is itself `monchaRLE`-compressed, inline**. Opcode traces showed the "clean"
  records had it expanded as part of the member-level decompression, while the "short" records carry it
  as a literal run — so after one pass it is still compressed (the surviving `f5 0a fe 00 …` high bytes
  are that inner RLE: `f5 0a`→`0a×12` ability defaults, `fe 00`→`00×3` pad). New engine
  `reconstructDqkRecord` re-expands it (splice header+name + a second `monchaRLE` pass over the tail),
  reproducing the canonical layout the clean records already have. Because the record is variable-length,
  `findAbilityBlock` locates the 12-byte ability block (first `[1,25]×12` run before `00 00 00`) and the
  stats are read at fixed deltas from it (THAC0 +15, saves +19, HD +25, damage +61/+63/+65, AC +67 — the
  same deltas the clean records show at ability offset 40). `decodeDqkMonsterRecord` is now ability-offset
  parameterised; `decodeMonchaGlb` tries the fixed-offset path first (clean goldens byte-identical) then
  the reconstruction. **20 type-8 monsters now carry validated full combat stats** (up from 5), every one
  canon-cross-checked: IRON GOLEM AC 3 / HD 18 / 4d10 / XP 14550, RED DRAGON AC −1, PURPL WORM 2d12, AMPHI
  DRAGON AC −3, with BLACK PUDDING/ETTIN/SHAMBLING MOUND exact. XP (absolute offset 4, before the
  reconstructed block) matches canon independently, confirming the framing. C4.2b-2 split → **C4.2b-2b**
  (the non-type-8 character/NPC records + the short-tail type-8 records + the chained-block / `vmRun1`
  combat trigger). Research: `docs/engine/research/moncha-dqk.md`. Gates: tsc 0 · **496 engine tests**
  (+3: reconstruct+anchor round-trip on a synthetic inline-compressed record, and a golden asserting the
  widened decode incl. IRON GOLEM/RED DRAGON + all-in-range stats) · **34/34 e2e** (`?dqk=1` asserts
  decoded ≥ 18 and IRON GOLEM/RED DRAGON carry stats) · built + deployed live (HTTP 200). Next: C4.2b-2b
  or C5 (DQK audio).
- **C4.2b-1 — The Dark Queen of Krynn: full bestiary names (MONCHA.GLB 5 → 98/98 named).** A focused
  reversing pass (opcode-by-opcode RLE traces of clean vs short members) proved the earlier "5/98" cap
  was a *fixed-offset assumption*, not a decompressor bug: `monchaRLE` is faithful (BLACK PUDDING
  byte-exact), a **double-RLE layer is ruled out** (a second pass yields garbage), and the records are
  genuinely **variable-length + self-describing** — decompressed lengths span ~88…530 bytes and the
  *name offset itself varies 21…300+*. The high bytes (`fe`/`f9`/`f5`) that survive in the compact
  records are genuine field data inside legitimate literal-copy runs, not leaked opcodes. New engine
  `scanMonsterName` finds the name offset-independently (longest uppercase-ASCII run past the binary
  header), and `decodeMonchaGlb` now returns a two-tier result: `roster: MonchaEntry[]` (all **98**
  members, each with name + record-type category + a `hasStats` flag) plus `monsters` (the
  fixed-offset-aligned subset that still carries full validated combat stats THAC0/HD/damage/AC). The
  `?dqk=1` view reports "98 of 98 creatures named (N with full validated combat stats)" and the combat
  path keeps fighting the faithful-stat records. Recovered the whole DQK bestiary — the chromatic
  DRAGONs + HEADs, the draconians (AURAK/BOZAK/SIVAK), the Thenol troops, and the named bosses
  (CAPTAIN DAENOR, GRUNSCHKA, ELGYNORA, DAWNSHINE). Names are SSI's in-data abbreviations ("ENORMUS
  SPIDER", "GREATR OTYUGH", "PURPL WORM"), not decode errors. C4.2b split → **C4.2b-2** (full per-monster
  stats by reversing the post-name field framing + the chained-block / `vmRun1` combat trigger). Research:
  `docs/engine/research/moncha-dqk.md`. Gates: tsc 0 · **493 engine tests** (+4: `scanMonsterName` at an
  arbitrary offset / longest-run / null, and a golden asserting the 98-name roster + `hasStats` ↔
  `monsters` consistency) · **34/34 e2e** (`?dqk=1` asserts the full 98-name roster incl. named bosses,
  alongside the existing faithful-stat combat) · built + deployed live (HTTP 200, MONCHA.GLB 200). Next:
  C4.2b-2 or C5 (DQK audio).
- **C4.3 — The Dark Queen of Krynn on the shared Phase-A systems, driven by a DQK party.** The
  character-sheet / spell-memorization / inventory / town overlays were already entirely **party- +
  ruleset-driven** (they read `getParty()` and the engine's `addnd1e-dragonlance` ruleset), so "DQK
  support" was a roster + wiring slice, not new overlays. New `apps/web/src/ui/dqkParty.ts` installs
  the **Heroes of the Lance** — STURM (human knight 6), CARAMON (human fighter 7), TANIS (half-elf
  fighter 6), GOLDMOON (human cleric 6, Mishakal), RAISTLIN (human magic-user 6), TASSLEHOFF (kender
  thief 7) — each built and validated through the engine `createCharacter` ruleset (original-authored
  content under the clean-room rule, **not** lifted from save data; THAC0/AC/saves/spell-slots are all
  ruleset-derived, and any request that failed validation would be skipped rather than installing a
  malformed sheet). The `?dqk=1` view now boots that party (install-if-empty, so a real saved party is
  never clobbered) and adds a **(V)iew / (M)agic / (I)tems / (T)own** command bar + matching hotkeys
  that open the **same** `openCharSheet` / `openMemorize` / `openInventory` / `startTown` overlays
  CoK/DoK use — guarded so they never fire while a combat or another overlay is up. CoK EGA chrome
  (8X8D1.DAX block 202 frame + 8×8 font) is reused for the overlay border (the overlays are
  container-agnostic). Gates: tsc 0 · **489 engine tests** (unchanged — web-only slice) · **34/34 e2e**
  (`?dqk=1` now force-installs the 6-hero DQK party, asserts STURM/RAISTLIN present, opens the sheet /
  magic / items overlays in turn, and confirms the town roster shows the DQK party) · built + deployed
  live (HTTP 200, 8X8D1.DAX 200). Next: C4.2b (full-bestiary decode + combat trigger) or C5 (DQK audio).
- **C4.2a — The Dark Queen of Krynn: combat on the shared interactive screen with faithful decoded
  monsters.** Decoded `MONCHA.GLB` (MON**sters**+**CHA**racters), an HLIB DATA library of
  RLE-compressed creature records. Reversed the **DQK run-length variant** — the same signed-byte
  family as DAX/DAA but with the boundary one higher and the repeat count one larger (`c ≤ 128` →
  copy `c+1`; `c ≥ 129` → repeat `257−c`; hackdocs `MONSTDAT.TXT`); under the DAX `256−c` the records
  front-clip, under `257−c` they decode exactly. New engine `monchaRLE` (kept separate from
  `signedByteRLE` so no DAX/DAA golden moves). Reversed the **Gen2 monster record layout** (fixed
  16-byte name field at offset 24, type tag @2 = 8, THAC0 @55, 5 saves @59, HD @65, damage
  @101/103/105, AC @107 — AC/THAC0 stored `60−x` inverted as in Gen1) by correlating the
  cleanly-decoded records against canon: BLACK PUDDING (AC 6 / HD 10 / 3d8 / THAC0 10 / XP 3000) and
  BORING BEETLE (AC 0 / HD 5 / 5d4) decode byte-exact. Engine `decodeMonchaGlb` /
  `decodeDqkMonsterRecord` / `sniffDqkMonster` return the validated monster set as the shared
  `MonsterRecord` shape — **honest 5/98** today (the decompressor desyncs on dense-literal records and
  the library interleaves type-3 *character* records; both widen in **C4.2b**). Web (`?dqk=1`):
  `fireSearchLocation` now returns `{text, roster}` — it still folds the cell's description into the
  event line but **captures the queued combat roster** instead of swallowing it into a marker;
  `launchDqkCombat` builds the party + faithful DQK enemies (resolved through the bestiary) and routes
  the fight into the **same `launchCombat` tactical overlay** CoK/DoK use (treasure + HP-persist +
  return-to-dungeon on dismiss). A position-only SearchLocation sweep queued no direct combat — DQK
  launches fights from chained blocks / the `vmRun1` main loop, so the **trigger** path is the C4.2b
  follow-up; C4.2a delivers the screen + faithful monsters + return. Research:
  `docs/engine/research/moncha-dqk.md`. Gates: tsc 0 · **489 engine tests** (+6: `monchaRLE` boundary/
  repeat-count vs `signedByteRLE`, a synthetic Gen2 record round-trip + sniff rejecting type-3/garbled,
  and a golden on real `MONCHA.GLB` pinning BLACK PUDDING/BORING BEETLE canon stats) · **34/34 e2e**
  (`?dqk=1` now launches a DQK fight vs the decoded bestiary, resolves it through the shared stepper,
  and returns to the dungeon) · built + deployed live (HTTP 200, MONCHA.GLB 200). Next: C4.2b / C4.3.
- **C4.1 — The Dark Queen of Krynn: per-cell ECL events fire through the engine (Gen2 SearchLocation).**
  Reverse-engineered how DQK fires per-cell dungeon events. Gen1 (CoK/DoK) dispatches `SearchLocation`
  on the cell **backdrop byte** at the fixed address `0x7F79` via `AND & 63 → ON_GOTO`. DQK does the
  opposite: it dispatches on the **party coordinates**, and its data segment lives at *low* addresses.
  **CONFIRMED** `0xBF` = party X, `0xC0` = party Y, `0x1B` = area number — by two independent lines of
  evidence: the area-2 teleporter (`WHERE DO YOU WISH TO GO?` / `X POSITION : 0-27`) writes its X/Y
  inputs to those addresses, and a position sweep changes which event fires (DQK **area 30**: ZOMBIES
  at (0,0), a child's alarm at (5,5), DOGS at (3,12), nothing at (10,10) — distinct cells, distinct
  events). Engine: `primeSearchLocation`/`dialogueForCell` are now **dialect-aware**
  (`opts.dialect`, default Gen1 so every CoK/DoK event golden is byte-identical) — header length and
  vbase follow the dialect, the Gen1 backdrop seeding only runs for the magic-bearing dialect, and a
  generic `opts.seedMemory` pre-seeds arbitrary data addresses; new `DQK_PARTY_X_ADDR`/
  `DQK_PARTY_Y_ADDR`/`DQK_AREA_ADDR` + `dqkPositionSeed(x,y,area)`. Web (`?dqk=1`): each step runs the
  area's Gen2 SearchLocation for the party's live cell and surfaces the real position-dependent event
  (room descriptions, ambient text, auto-resolved scripted fights) on a per-cell event line — a DQK
  dungeon now **plays through the engine, not just walks**. C4 was bigger than one slice, so it was
  split in place: **C4.2** (DQK combat → the shared `launchCombat` overlay with faithful DQK monsters,
  needs `MONCHA.GLB` decoded) and **C4.3** (DQK on the shared Phase-A systems — magic/items/town —
  driven by a DQK party) are queued next. Research: `docs/engine/research/dqk-search-location.md`.
  Gates: tsc 0 · **483 engine tests** (+3: a synthetic Gen2 SearchLocation prime/seed + a golden on
  real `ECL.GLB` asserting area 30's three distinct coordinate-dispatched events and area 4's ambient;
  Gen1 `dungeonEvents` golden unchanged) · **34/34 e2e** (`?dqk=1` now also drives
  `searchAt(30, …)` at three cells and asserts ZOMBIES/CHILD/DOGS distinct + area 4 HUGE SHADOWS) ·
  built + deployed live (HTTP 200). Next: C4.2 — DQK combat.
- **C3 — The Dark Queen of Krynn: VGA 256-colour art through the render pipeline.** DQK's HLIB `TILE`
  decoder, per-leaf colour table, Chain-4 pixel layout and `indexedToRGBA` were all already verified
  golden — the missing piece was **palette compositing**, the open "multi-palette layering" question
  in `hlib-format.md`. A single HLIB leaf only stores a *slice* of the 256-colour DAC (a
  `first_col`/`ncolors` window). Measured on the real game: `ALWAYS.TLB` owns 0–15 (UI chrome),
  `GEN`/`FRAME` own 16–31, `BIGPIC.TLB` (overland maps) owns 32–255. So an overland picture rendered
  with only its own leaf palette paints 32–255 and leaves 0–31 black — standalone-correct but not the
  full image. The DQK runtime builds its live DAC by **layering** the co-resident leaves. New engine
  `mergePalettes(...)` + `definedColorCount` (`render/palette.ts`) composite partial palettes into one
  256-slot table (fill in argument order, last-wins, a `null` never clobbers a defined slot, cycle
  ranges concatenate); `compositeHlibPalette(archive)` (`loaders/hlib.ts`) merges one archive's leaves.
  The full overland DAC = `mergePalettes(compositeHlibPalette(ALWAYS), compositeHlibPalette(BIGPIC))`
  → 240 defined slots, and the picture renders as real full-colour VGA art. Web: `dqkExplore.ts`
  (`?dqk=1`) gains a **VGA art panel** — the 3 BIGPIC overland pictures (304×120, method 18) painted
  through the composited DAC on a canvas, with a prev/next stepper. Research:
  `docs/engine/research/vga-palette-compositing-dqk.md`. Gates: tsc 0 · **480 engine tests** (+5:
  `mergePalettes` last-wins/null-safe/cycle-concat + a golden against real `BIGPIC.TLB`+`ALWAYS.TLB`
  asserting the 224/16/240 slot ranges and >50 distinct rendered colours) · **34/34 e2e** (`?dqk=1`
  now also asserts the art panel loaded 3 frames at 304×120, ≥240 palette slots, >50 distinct colours
  off the painted canvas, and the stepper advances) · built + deployed live (HTTP 200; bundled
  BIGPIC.TLB + ALWAYS.TLB 200). Next: C4 — DQK content + systems on the Gen2 engine.
- **C2 — The Dark Queen of Krynn: Gen2 ECL dialect + run scripts through the VM.** Reverse-engineered
  how DQK's `ECL.GLB` event/dialogue/combat bytecode differs from the Gen1 (CoK/DoK) ECL the engine
  already runs — and the answer is **almost nothing**: the opcode table (0x00–0x40), the EclOpp
  operand framing, and the 6-bit inline-string packing are **identical**; the *only* delta is the
  per-block **header**. Gen1 blocks open with a `0x1388` magic word + five far pointers (22 bytes,
  `vbase 0x7FFE`); **Gen2 blocks drop the magic word** → a 20-byte header (the same five far pointers)
  with `vbase 0x8000`. Both still enter at vaddr `0x8014` (= the header end). Confirmed on the real
  `ECL.GLB`: **0 of 47** members carry the magic, every far-pointer segment is `0x0101`, **446 inline
  strings decode to clean English**, and running scripts through the VM fires real effects — area 2's
  init does `LOAD_FILES GEO 2` + a wall-type menu, and **area 4 prints the actual DQK intro
  narrative** (*"YOUR VOYAGE BEGINS PEACEFULLY. ONE NIGHT YOU SPY DAENOR SPEAKING INTO THE WATER…"*).
  Engine: a new `EclDialect` (`ECL_GEN1`/`ECL_GEN2`) threads through `ecl/disassemble.ts`,
  `ecl/vm.ts` and `ecl/program.ts` (defaulting to Gen1, so **every CoK/DoK ECL golden stays
  byte-identical**); new `loaders/eclGlb.ts` (`decodeEclGlb`) decodes the HLIB `DATA` library into a
  `BlockSource` keyed by logical area id; the Gen2 GEO loader now also exposes each map's logical area
  id so a map and its script pair by id (GEO area 34 ↔ ECL area 34). Web: `dqkExplore.ts` runs the
  entered area's script through the Gen2 VM on entry and surfaces the events it fires (the `LOAD_FILES`
  GEO id + the first decoded narrative line) on a live event line. Research: `docs/engine/research/
  ecl-gen2-dqk.md`. Gates: tsc 0 · **475 engine tests** (+5: Gen2 ECL synthetic header/disasm/VM +
  golden against the real ECL.GLB — no magic, loadFiles fires, the VOYAGE narrative decodes) ·
  **34/34 e2e** (`?dqk=1` now also asserts ECL.GLB loads 47 scripts, the boot area fires
  `LOAD_FILES GEO 2`, and area 4 prints the intro narrative) · built + deployed live (HTTP 200;
  bundled ECL.GLB 200). Next: C3 — DQK VGA 256-colour graphic set through the render pipeline.
- **C1 — The Dark Queen of Krynn: Gen2/HLIB boot + GEO map decode (opens Phase C).** DQK is the
  *newest* Gold Box engine and a **different container generation** from CoK/DoK — its dungeon grids
  live in `GEO.GLB`, a flat HLIB **DataLib** library, not the Gen1 DAX archives. Reverse-engineered
  the Gen2 GEO format: the library is `magic=1` (member[0] = a 20-entry id table, members[1..20] = the
  20 maps); each map member is `byte0=width, byte1=height, 6 reserved header bytes, then 4 planes of
  W×H bytes`. Unlike Gen1's fixed 16×16, Gen2 maps are **variable-sized** (10×10 … 39×20), but carry
  the **identical nibble wall semantics** (plane0=(N<<4)|E, plane1=(S<<4)|W, plane2=backdrop,
  plane3=doors) — proven by the same shared-edge symmetry signature that validated Gen1 (East-edge
  symmetry 0.85–1.00 across all 20 maps; every member satisfies `8 + 4·W·H` exactly). Engine: new
  `decodeGen2GeoBlock` / `decodeGen2GeoArchive` / `sniffGen2GeoBlock` in `loaders/geo.ts`, sharing a
  refactored `buildGeoResult` plane→`GameMap` helper with the Gen1 decoder so **every CoK/DoK GEO
  golden stays byte-identical**. The DQK detection profile already existed in `BUILTIN_PROFILES`
  (`dark-queen-of-krynn` / DOS / HLIB). Web: `apps/web/src/ui/dqkExplore.ts` fetches GEO.GLB, runs
  `decodeHlib` → `decodeGen2GeoArchive` → 20 walkable maps, and presents a map picker + automap +
  keyboard movement (`?dqk=1` route, sidebar entry, and the Library now boots a detected DQK/DOS
  install straight into it). The first-person VGA wall art (DQK's 256-colour HLIB `TILE` sets) is the
  separate C3 concern — this slice is the **decoded, walkable grid**. Gates: tsc 0 · **470 engine
  tests** (+5: Gen2 synthetic + golden against the real GEO.GLB — 20 maps, variable dims, wall
  symmetry) · **34/34 e2e** (`?dqk=1` boots the HLIB library, asserts 20 variable-size maps, walks
  map 1, and switches the picker to a 39×20 map) · built + deployed live (HTTP 200; bundled GEO.GLB
  200). Next: C2 — Gen2 ECL dialect deltas.
- **B1 — Death Knights of Krynn: detection + boot (opens Phase B).** DoK is the Krynn-Gen1 DAX sibling
  of CoK, so it runs the *same* `loadDungeon` pipeline — and feeding it real DoK bytes flushed out three
  Gen1-format assumptions the CoK-only loaders had frozen as constants. All three are now fixed in a
  CoK-preserving way (every CoK golden still byte-identical): **(1)** `decodeGeoBlock` validates on the
  1026-byte block length, not the CoK 1024 size-word header — DoK stores a different area/checksum word
  in those two bytes while keeping the four wall/backdrop/door planes at offset 2 (confirmed by the
  nibble wall-edge symmetry test: DoK GEO 32/34/36 score 0.70–0.89, same as CoK); a new `hasCokGeoHeader`
  helper still reports which header form a block carries. **(2)** The WALLDEF `Pieces` opcode sentinel
  `0x7F` ("default/blank wall set" → block 0) is honoured on *any* symbol set, not just set 1 — COAB's
  CoK dispatch only ever saw it on `var_3`, but DoK passes it on set 3 (GEO 34 pieces `[1,0xFF,0x7F]`);
  when the default block 0 isn't present (DoK's WALLDEF1 ships only block 1) the set is simply left
  empty instead of throwing. **(3)** The 8X8D tile sniff/decoder accept a 256-tile wall bank whose
  uint8 count byte saturates at `0xFF` (DoK 8X8D1 block 1, 8209 bytes = 17 + 256×32); the exact framing
  supplies the true count. A new `dungeon-dok.test.ts` golden pins all three on real DoK bytes (GEO 34
  loads a 16×16 walkable map, the 0x7F set resolves, the 256-tile bank decodes). On the web side a new
  `apps/web/src/ui/gameData.ts` registry holds each game's DAX paths (spelled as literals so the static
  bundler ships them) + ECL init block; `startExplore(gameId)` is parameterised (CoK stays the default),
  with a `?explore=dok` route, a sidebar entry, and the **Library now boots any detected playable Gen1
  DOS game (CoK *or* DoK) straight into its dungeon**. Gates: tsc 0 · 453 engine tests (+3) · 30/30 e2e
  (new DoK boot-and-walk case: loads DoK's GEO/ECL/WALLDEF, 256-cell map, moves run; 5 DoK archives now
  bundled) · built + deployed live (HTTP 200, DoK GEO asset 200). **Known follow-up:** DoK's
  first-person wall *tiles* don't render yet — its single 256-tile 8X8D bank needs different symbol-set
  routing than CoK's per-set blocks; that's tracked under **B4** (DoK graphic set). The map is fully
  walkable with a working automap meanwhile. Next: B2 — DoK content (maps, undead monsters, items, ECL).
- **B5 — DoK reuses the Phase-A systems; close the two deferred content gaps.** The Phase-A systems
  (combat, magic, items, menus, camp, town) are party + ruleset driven, so they already run on DoK
  content once its data loads — B1 gave maps, B2 monsters, and B5 finishes the set with **items**.
  **(1) DoK item table.** `ITEM0.DAX` decompresses to a single 1632-byte block, which is exactly
  **96 × 17** — the structural `06 00 01` marker recurs every 17 bytes, confirming a 17-byte record.
  It is the classic SSI item record (the FRUA `item.dat` is the same fields plus one trailing
  always-zero byte = 18 bytes; DoK drops it): base-type pointer @0, VOCAB-coded name @1–3 (read
  high→low), encumbrance u16 @4, value u16 @6, magic bonus @8, ready/identified/cursed flags, charges
  @14. New engine `decodeDokItemArchive` / `decodeDokItemRecord` / `sniffDokItemRecord` keep the
  leading 91 valid records (the 5-slot tail is unused/garbage, gated by a base-type/flag sniff). Names
  stay VOCAB-coded (the DoK vocabulary is a separate table), so the web item DB labels each item by its
  **base-type catalogue** entry (Vial / Arrow / Dart / …); a `window.__explore.items()` seam exposes it.
  **(2) CoK saved-character class code.** B3 left class/level reconstructed from abilities/HP; the class
  is in fact stored at **offset 90**. Reversed from the six real save characters by correlating with
  *independent* in-record signals: code **1 = Cleric** (both code-1 characters carry cleric spell slots
  at 79–81; nobody else does) and **2 = Magic-User** (the code-2 ISTAN HORBIN has the textbook weak MU
  saves). `SavedCharacter.classCode` + `dragonlanceClassForCode` map only those two validated codes, and
  the import uses them — fixing real mislabels the ability heuristic produced: ISTAN is a mage *with STR
  18* (heuristic → fighter), KAL a cleric *with DEX 19* (heuristic → thief). Codes 5/6 (Thief/Knight) are
  single-sample observations left to the heuristic; **level is still HP-derived** (offsets 206/214 hold
  small 1–3 enums, not a campaign level — documented, not guessed). Gates: tsc 0 · **465 engine tests**
  (+5: DoK item synthetic+golden, class-code golden) · **33/33 e2e** (DoK case asserts the item DB
  decodes ≥ 80 items incl. a named "Vial"; import case asserts ISTAN imports as a Magic-User) · built +
  deployed live (HTTP 200; bundled `ITEM0.DAX` 200). Phase B is complete. Next: Phase C — Dark Queen of
  Krynn (C1 detection + HLIB GEO).
- **B4 — DoK Amiga graphic set + multi-track SMUS + first-person wall routing.** Three deltas that
  finish DoK's *presentation* layer, all shipped in one slice. **(1) FP wall-tile routing** (the item
  deferred from B1): CoK packs each WALLDEF symbol set into its own `blockId*10+i+1` 8X8D block, but
  DoK packs all three sets into ONE 256-tile bank at `blockId` and selects a set by relocation offset.
  `loadDungeonWalls` now detects the single-bank case (k>1 records, no per-record block, a ≥128-tile
  bank present) and routes each set through the shared bank, relocating by its own slot offset
  (varA 0 / 0x46 / 0x8C) and resolving `lookupTile` by the global tile id — so DoK dungeons finally
  draw real wall tiles instead of an empty backdrop. **(2) DoK EGA↔Amiga live switch**: `gswitch.ts`
  is parametrized over the game (`SWITCH_CONFIG`), and `?gswitch=dok` runs the same "same scene, two
  graphic sets, swapped with no reload" proof on DoK's BIGPIC1.DAX/.DAA. **(3) DoK multi-track SMUS**:
  unlike CoK's single theme, DoK ships seven mood SMUS scores (krynn/action/horror/lordsoth/sorrow/
  undead/victory). `AudioManager` gained `registerAmigaTracks` + `setMusicTrack` (multiple named scores,
  one active), and `gameAudio.setGame('dok')` loads all seven, maps the scene onto a track (explore→krynn,
  combat→action) and exposes `playTrack()` for thematic cues (the undead crypt theme, Lord Soth, a victory
  sting); `?explore=dok` arms it. (DoK's packed `soundData` SFX bank is a later decode; for now DoK reuses
  the shared CoK Amiga 8SVX one-shots.) The static bundler now also scans `src/audio` so DoK's
  `disk1/sound` directory ships. Gates: tsc 0 · **460 engine tests** (+1 single-bank wall-routing golden) ·
  **33/33 e2e** (+DoK BIGPIC switch, +DoK 7-track SMUS scene/event switching) · built + deployed live
  (HTTP 200; bundled `undead.smu` 200). Next: B5 — DoK reuses the Phase-A systems; verify + fill gaps.
- **B3 — Character import from a CoK save (the series carry-over feature).** The thing that made the
  Krynn trilogy a *trilogy*: you finish Champions of Krynn and march the same party into Death Knights
  of Krynn. CoK writes each member to `SAVE/CHRDATA{n}.SAV`, and decoding one settled the format
  question immediately — it is the **same 409-byte `.CCH` record** the monster loader already reads
  (name@0, abilities@16, HP@98, saves@208, AC@275, THAC0@89), *not* the FRUA `.cch` layout (whose
  name@96 / class@89 / level@150+ positions are all zero in a real CoK save). New engine
  `loaders/savedCharacter.ts` decodes the fields that carry forward — name, the six ability scores,
  HP, AC, THAC0, the five saving throws, XP — with a synthetic + golden test that reads the real
  6-hero save party (SIR STRONGSWORD STR 18 / 27 HP / fighter-class saves; ISTAN HORBIN's textbook
  low-level Magic-User saves; GARIN). On the web side `importParty.ts` turns each save into a playable
  `CharacterSheet`: **every combat-relevant number is byte-faithful** to the save, while the primary
  **class and level are reconstructed** (class from the prime-requisite abilities, level from HP vs the
  class hit die) because CoK doesn't store the per-class level breakdown in a position we've pinned yet
  — the same kind of honest, documented gap as B2's monster HP/saves. A `?import=cok` route + an
  "⇪ Import CoK party → DoK" sidebar entry import the party, show who came across (with a "stats exact,
  class derived" note), and offer **"Continue into Death Knights of Krynn"** — which boots `?explore=dok`
  with the imported heroes as the active roster. The save files (`CHRDATA*.SAV`, a full 6-character
  party) are bundled by the static asset step. Gates: tsc 0 · **459 engine tests** (+4) · **31/31 e2e**
  (new `?import=cok` case: imports 6 heroes, asserts the named party loads and SIR STRONGSWORD keeps
  STR 18 / 27 HP straight from the save) · built + deployed live (HTTP 200; the bundled save asset 200).
  Next: B4 — DoK Amiga graphic set + EGA↔Amiga switch + multi-track SMUS music + the deferred wall-tile
  routing.
- **B2 — Death Knights of Krynn: content (monsters / ECL / items).** With DoK booting on the shared
  Gen1 path (B1), the question was which *content* the CoK-only loaders silently mishandle. Probing
  found: **ECL events already fire** on the Gen1 dialect (DoK ECL1 SearchLocation prints e.g. "A FEAST
  IS BEING PREPARED.") and **GEO maps load** (B1) — but **every** DoK monster was rejected. Cause: DoK
  ships a **216-byte compact monster record**, not CoK's 409-byte `.CCH`-family record, so the loader's
  hardcoded `length === 409` sniff dropped all 64. The DoK offset map was reverse-engineered by
  correlating the **three monsters present in both games** (RED DRAGON, GHOUL, SIVAK): their
  CoK-decoded AC/THAC0/damage values pinpoint the same fields in the DoK bytes → **THAC0@31, category@32,
  damage count@89/bonus@90/sides@91, AC@95** (abilities stay at the CoK offset 16; both AC and THAC0
  keep the 60−x inversion). Validated across all 64 DoK blocks — **0/64 AC out-of-range** and the
  canonical AD&D undead ACs land exactly (NIGHTMARE −4, LICH 0, WRAITH 4, WIGHT 5, SPECTRE 2; VAMPIRE
  keeps STR 18). `monster.ts` now length-dispatches a per-format `MonsterLayout`; CoK output is
  byte-identical. Hit-points / hit-dice / saves / XP aren't located in the compact record (DoK
  rebalances them and HP is rolled from HD in-engine) — reported as 0/empty and documented for B5.
  DoK's `ITEM0.DAX` is a *different* format again — a single 1632-byte VOCAB-coded binary table (not a
  multiple of 63), so the CoK 63-byte plain-ASCII item loader now **skips non-multiple blocks** instead
  of slicing junk out of it; the full DoK item decode is deferred to B5. Web: a `window.__explore.
  bestiary()` seam exposes the decoded roster. Gates: tsc 0 · **455 engine tests** (+2: DoK monster
  golden, DoK item-guard) · **30/30 e2e** (the DoK case now also asserts the undead bestiary: ≥60
  monsters, LICH + SKEL WARRIOR present, NIGHTMARE AC −4) · built + deployed live (HTTP 200). Next: B3
  — character import from a CoK save.
- **A6.3 — Full flow (`?play=cok`).** Closes Phase A6 (and Phase A's CoK vertical slice): a new
  `apps/web/src/ui/play.ts` `startPlay` orchestrator stitches the slices that until now were separate
  `?`-routes into one continuous session — the CoK **title sequence** → **character creation** → the
  **dungeon**, where the play loop is explore ↔ combat (scripted/random encounters → the playable
  overlay) ↔ camp (Encamp → rest / memorize / **Save**) ↔ **town** (shop/temple/training/tavern). Each
  phase is a real mount of the existing view; the orchestrator owns only the transitions plus a
  `window.__play` seam (`phase`/`begin`/`toTown`/`toExplore`). `startCreate` gained an optional
  `onBegin` hook that renders a **"Begin Adventure"** button once a party is mustered; the explore ↔
  town round-trip is owned by `play` (town's own Esc still exits to the wilderness). A returning party
  (roster already populated) skips creation and drops straight into the dungeon, exactly like resuming
  a campaign — and the platform-bound A6.2 audio rides along because the same view mounts are reused.
  New sidebar entry "▶▶ Play Champions of Krynn (full)" + `?play=` boot route. Gates: tsc 0 · 450
  engine tests · **29/29 e2e** (new full-flow case walks title → create a fighter → begin → fight →
  camp → town → back → save, asserting each phase transition and that the quick slot persists) · built
  + deployed live (HTTP 200). **Phase A's CoK slice is now end-to-end playable as one flow.** Next:
  Phase B — Death Knights of Krynn (B1 detection profile + boot from the Library).
- **A6.2 — In-game audio wiring.** Lifts audio from the standalone `?audio=1` demo into an app-wide
  service that actually scores the game. New `apps/web/src/audio/gameAudio.ts` `GameAudio` singleton
  owns one `AudioManager`, lazily loads Champions of Krynn's *own* Amiga sound once (the `krynn` SMUS
  theme + the 8SVX effects), and exposes a tiny scene/action API: `enterScene('title'|'explore'|
  'combat'|'silent')` (re)starts the score at a fitting SMUS tempo — title/explore 8, combat 12
  ticks-per-quarter, so battles run tenser on the same single available score — and `playAction(
  'melee'|'arrow'|'cast'|'fireball'|'death'|'door'|'miss')` fires the matching real sample
  (hit/arrow/cast2/fireb/dead/gate/swish). Audio **follows the platform switch exactly like graphics**:
  `gswitch.ts` mirrors its DOS-EGA ↔ Amiga toggle onto `gameAudio.setPlatform`, where Amiga arms the
  sampled SFX + theme and DOS honestly reports its `MIDI.DAX` music undecoded and falls silent rather
  than faking it. Music only sounds after a user gesture (autoplay policy): `intro.ts` arms the title
  theme and the first click/key enables + starts it; `explore.ts` enters the dungeon ambient on boot
  and enables on first canvas click; `combat.ts` enters the combat theme at battle start, fires
  melee/miss on attack hit/miss and death on a slain combatant from `logEvents`, and returns to the
  explore ambient when the battle overlay is dismissed. Everything is defensive — with no AudioContext
  (headless) the calls no-op but still advance the counters the new `window.__gameAudio` seam reads, so
  the *wiring* (scene transitions, SFX resolution, platform binding) is verifiable without real
  playback. Gates: tsc 0 · 450 engine tests · 28/28 e2e (new in-game-audio case: Amiga score armed +
  8 samples, scene transitions counted, action cues resolve + count, DOS reports silent) · built +
  deployed live (HTTP 200). Next: A6.3 full `?play=cok` flow (intro → create → play → save).
- **A6.1 — Save / Load.** Opens Phase A6 (persistence & polish). Engine: `world/saveGame.ts` defines a
  versioned, serialisable `SaveGame` snapshot of a campaign — the party (`CharacterSheet[]`), its
  per-character stores (memorised spells, spellbooks, packs, XP) and the shared gold pool, the journal,
  the party's world `SavePosition` (area / x / y / facing), the game clock, and an open-ended `flags`
  map for quest state. `makeSave` normalises and defaults every field; `serializeSave`/`deserializeSave`
  round-trip and **self-repair** a slot (a bad facing snaps to north, non-numeric flags are dropped, a
  partial blob is filled, a non-save returns `null`, and an unknown future version still loads
  best-effort rather than being rejected); `summarizeSave` yields a one-line slot summary for a load
  list. `Explorer.setPose` was added so a load can teleport the party to the saved cell. 7 engine tests
  (round-trip equality, minimal-input defaults, junk repair, non-save rejection). Web: `ui/saveStore.ts`
  bundles the build's scattered localStorage stores into one slot (`greybox.save.cok.<slot>`); `applySave`
  writes them back and refreshes the in-memory caches the live views hold (`reloadStores` in `party.ts`,
  `reloadJournal` in `journal.ts`) so a Load takes effect without a page reload. The explore **Encamp →
  Save** verb now writes the quick slot (party + position + clock); a **Load** restores it live (teleport
  + clock + HUD refresh) and `?explore=1&load=1` restores it on boot. Gates: tsc 0 · 450 engine tests
  (+7) · 27/27 e2e (the explore case gained a save → walk-away → load round-trip that asserts the pose
  returns to the saved cell, plus a fresh-boot `?load=1` persistence check) · built + deployed live
  (HTTP 200). Next: A6.2 in-game audio wiring.
- **A5.4 — Adventurer's Journal (copy-protection replacement).** The original Gold Box games print
  *"…and you record it in Journal Entry N"* and the player reads entry N from a physical booklet —
  there is **no journal opcode** (confirmed against the EclDump: the engine just `PRINT`s the number).
  The clean-room equivalent is a persistent in-game log. Engine: `world/journal.ts` `Journal` keeps
  numbered pages (`id`/`title`/`body`/`category`/`day`/`read`); `record` is **idempotent by entry
  number** (a re-firing event never duplicates a page) with an optional `replace`, auto-numbers when
  no id is given, filters by category, tracks read/unread, and round-trips through JSON for
  persistence. `render/journalScreen.ts` `composeJournalScreen` paints the bordered page — a scrolling
  entry list (cursor `>`, an unread `*` marker, per-category colour, the yellow entry number) above a
  word-wrapped reading pane (`wrapText`, a reusable greedy word-wrapper) and a command bar. Web:
  `ui/journal.ts` (`?journal=1`, sidebar "📖 Adventurer's Journal") persists the log in
  `greybox.journal.cok`, seeds three original-authored opening pages, and reads with ↑↓ / F (filter) /
  M (mark-all) / Esc; an exported `recordJournal` helper is the "an event logs a page" seam other
  views call — the **Tavern "Listen"** now records the rumour you hear, and a paid **hermit bribe** in
  `?dialogue=1` records the keep's muster as a quest page (wired through a new dialogue `onEnd` hook).
  Gates: tsc 0 · 443 engine tests (+20: 8 model, 12 screen/wrap) · 27/27 e2e (a new journal case
  seeds, reads to drop the unread count, records idempotently, filters to quests, and survives a
  reload) · built + deployed live (HTTP 200). Next: A6.1 save/load.
- **A5.3 — NPC dialogue through the live ECL VM.** Engine: `world/dialogue.ts` `DialogueRunner` wraps
  a minimal `EclMachine {run, resume}` surface — which the real `EclVm` *and* a scripted test stub
  both satisfy — and translates the VM's run/resume lifecycle into a stream of presentable
  `DialogueTurn`s: a `say` turn (accumulated NPC narrative + the choices/number/who the script paused
  for), a `combat` turn (the queued monster roster; the host runs the fight, then calls `afterCombat()`),
  a `chain` turn (a `NEWECL` jump — the host loads that block into a fresh runner), and an `end`.
  Branching is *emergent*: the value the player picks is exactly what the script's own IF/ON_GOTO reads
  next, so different picks walk different paths — the runner invents no logic. `dialogueForCell` builds
  a runner straight off a cell's SearchLocation handler. Web: `ui/dialogue.ts` (`?dialogue=1`, sidebar
  "💬 Talk to an NPC") presents each turn as NPC text + clickable choice buttons (or a number entry),
  hands `combat` turns to the live tactical overlay (`launchCombat`) and resumes the conversation when
  the battle is dismissed, follows `chain` into the next block, and closes on `end`; the standalone demo
  drives an original, hand-authored hermit encounter (greet → an omen chain · offer steel → a numeric
  bribe → resolution · draw steel → a real fight → the talk resumes). `explore.ts` now *peeks* each
  per-cell step event: a genuine multi-choice ASK opens the interactive overlay (with real MON-record
  foes via an injected `dungeonSpawn`), while room text / wandering-monster checks auto-drain inline
  exactly as before — one VM prime, no double-run. Gates: tsc 0 · 423 engine tests (+6 dialogue) ·
  26/26 e2e (a new dialogue case exercises ask/branch, the NEWECL chain, the numeric bribe, and the
  draw-steel → combat → resume path) · built + deployed live (HTTP 200). Next: A5.4 journal entries.
- **A5.2 — Town hub + locations.** Engine: `town/shop.ts` prices the item catalogue (explicit list
  prices + per-kind fallback, half buy-back) with pure `buyItem`/`sellItem` transactions over the gold
  pool; `town/services.ts` prices the temple (`healCost` per missing HP, level-scaled `raiseCost`) and
  the training hall (`trainingCost` + `checkTraining`, which gates level-up on the AD&D-1e `xpForLevel`
  threshold, plus a deterministic `levelHpGain` hit-die-average). `render/townScreen.ts`
  `composeTownScreen` is one generic bordered page (title, the party's steel, a scrolling option list
  with right-aligned prices + greyable lines, status, command bar) shared by the hub and every location.
  All costs are CANDIDATE, isolated for a later pin. Web: `ui/town.ts` (`?town=1`, sidebar "🏠 Town hub")
  drives a *Town of Solace* hub → **Shop** (buy from a stock list / sell from the leader's pack, both
  moving real gold + inventory), **Temple** (heal every wounded member per-HP, raise the first fallen),
  **Training Hall** (level a member up when XP + gold suffice: +HP, an improved THAC0 by the attack-table
  delta, the class level bumped), and a **Tavern** (rumours; the journal proper is A5.4). `party.ts`
  gained a persisted **XP store** with `awardPartyXp` (split among the living) — combat victory now
  awards it, closing the loop from killing monsters to training up — plus train/shop/temple helpers.
  Gates: tsc 0 · 417 engine tests (+13 economy, +7 compositor) · 25/25 e2e (a new town case buys + sells
  against the gold pool, trains a fighter 1→2 via granted XP, and heals + raises at the temple) · built +
  deployed live (HTTP 200). Next: A5.3 NPC dialogue through the live ECL VM.
- **A5.1 — Wilderness / overland travel.** Opens Phase A5 (world & content). Engine: `world/wilderness.ts`
  models the area-map layer purely — a `WildernessTravel` traveller steps the party 4-directionally over
  a terrain grid, blocked by the map edge and impassable water, accruing each terrain's travel-minutes
  and rolling a `checkEncounter` d100 wandering-monster check through the engine `Rng` (so a seed replays
  an identical journey); a `wildernessFromAscii` helper authors regions from ASCII art. The `TERRAIN`
  table (passable / minutes / encounter-% per terrain) is CANDIDATE tuning, isolated for a later pin.
  `render/wildernessScreen.ts` `composeWildernessScreen` paints the bordered travel page — region name,
  a terrain-coloured top-down area map (scroll-centred on the party, party marker + yellow site dots),
  a right info panel (location / clock / status), and a Move/Look/Camp/Map/Exit command bar. Web:
  `ui/wilderness.ts` (`?wild=1`, sidebar "⛰ Wilderness travel") drives a hand-authored demo region —
  *Vale of Solace* (original content, no copyrighted map data) — with arrow-key travel that advances the
  shared game clock + three-moons display and hands a wandering encounter straight to the live tactical
  combat overlay (`launchCombat`), returning to the map on dismiss; stepping onto a named town announces
  the arrival. Gates: tsc 0 · 397 engine tests (+12 travel, +7 compositor, −2 overlap with shared
  exports) · 24/24 e2e (a new wilderness case asserts edge + water blocking, terrain time-cost, town
  arrival, and the encounter→combat handoff) · built + deployed live (HTTP 200). Next: A5.2 town hub.
- **A4.3 — Combat-won treasure screen → into inventory.** Closes Phase A4. Engine: `items/treasure.ts`
  (`rollTreasure(foes, seed)` rolls gold ≈ ΣXP/2 with a seeded jitter and gives each foe a 1/3 chance
  to drop a base-catalogue item; `hasTreasure` gates the screen) and `render/treasure.ts`
  (`composeTreasure` paints a TREASURE header, a gold line that greys with a TAKEN tag once collected,
  cursor-marked item rows with their own TAKEN tags or a green "(NO ITEMS)", and a Take All / Money /
  Items / Exit command bar). Combat `finalize()` rolls a haul on a party victory and enters a new
  `'treasure'` phase; the party gold pool (`getGold`/`addGold`, persisted) and `giveItemsToMember`
  (merges stackables, else a new pack entry) bank the money and loot into the leader's inventory. Web:
  combat's treasure branch wires ←→ menu / ↑↓ item cursor / Enter + T·M·I·E hotkeys; explore gains a
  `winFight` seam (a 1-HP foe for a deterministic seeded victory). Gates: tsc 0 · 380 engine tests
  (+7 treasure model, +7 compositor) · 23/23 e2e (the explore drive now wins a battle and asserts gold
  conservation — pool grows by exactly the haul — plus that dropped items reach the leader's pack) ·
  built + deployed live (HTTP 200). Next: A5.1 wilderness/overland travel.
- **A4.2 — Trade between party members + encumbrance.** Built on the A4.1 inventory model. Engine:
  every base item now carries a `weightTenths`, with `inventoryWeightTenths`/`inventoryWeightLb`
  summing the pack and `transferItem(from, to, index)` moving a whole stack between two members'
  inventories — un-readying it as it leaves (the readied-weapon rule). New `items/encumbrance.ts`
  turns carried weight + Strength into an encumbrance level (unencumbered → light → moderate → heavy
  → overloaded, stepping at 1×–4× a Strength-scaled allowance) and a movement rate; the Strength
  allowance table + step fractions are CANDIDATE and isolated in that one file for a later
  oracle/screenshot pin. The pure `composeInventory` gained a WT + encumbrance line and a status line.
  Web: `ui/inventory.ts` shows the weight/encumbrance readout, adds a **Give** verb (G) that hands the
  cursor item to the next member (both packs persist via `setInventory`), and a new standalone
  **`?inv=1`** route (loads CoK tiles/font best-effort; usable with a created party). Gates: tsc 0 ·
  366 engine tests (15 inventory + 5 encumbrance + 9 compositor) · 23/23 e2e (a new fighter→cleric
  Trade case proves the giver loses the sword + weight drops while the recipient gains it; the explore
  drive also checks the weight/encumbrance readout) · built + deployed live (HTTP 200). Combat-won
  treasure into inventory is A4.3.
- **A4.1 — Inventory ("Items") screen.** The first slice of Phase A4. New engine modules:
  `items/baseItems.ts` (the base-item catalogue CoK keeps in its executable rather than per-record —
  pinned offset-42 ids 6–15 plus engine-internal ids ≥ 64 for armours CoK stores as base-type-0
  specials, each with damage / armour-AC / two-handedness) and `items/inventory.ts` (a pure,
  immutable inventory: `readyItem`/`unreadyItem`/`toggleReady` enforcing one weapon + one armour +
  one shield with the two-hander↔shield exclusion, `dropItem`, `useItem` for consumable stacks, and
  the `wornArmorAc`/`computeArmorClass(dexDefensiveAc)`/`computeThac0` recompute that readying gear
  drives — AC mirrors the ruleset's `baseAc + dex.defensiveAc`). Rendered by the pure
  `render/inventory.ts` `composeInventory` (bordered page, recomputed AC/THAC0 header, a scrolling
  item list with a cursor + READY tags, a Ready/Use/Drop/Next/Exit command bar). Web: `party.ts`
  gained per-character pack persistence (`getInventory`/`setInventory`) that seeds a class-appropriate
  starter kit on first open; `ui/inventory.ts` mounts the pack as a modal overlay opened from a new
  **Items** verb on the character sheet (↑/↓ move the cursor, R readies, U uses, D drops, N/P cycle
  members), and the sheet defers its capture-phase keyboard while the pack is up. Gates: tsc 0 · 356
  engine tests (12 model + 7 compositor) · 22/22 e2e (the explore View→Items flow drives a ready-flip,
  an AC recompute and a drop) · built + deployed live (HTTP 200). Deeper equip (encumbrance, trade,
  combat-won treasure) are A4.2/A4.3.
- **A3.4 — Live encounters land on the playable combat screen.** A script-triggered fight in the
  explorer (`?explore=1`) no longer auto-resolves to a summary panel — it now opens the real CS.9
  tactical battle. `apps/web/src/ui/combat.ts` grew an exported `launchCombat(cfg)` that mounts the
  combat screen as a **modal overlay** (reusing the `.sheet-overlay` chrome), sharing a single
  `beginBattle()` + `loadCombatAssets()` core with the standalone `?combat=1` view (`startCombat`
  is now a thin wrapper). `explore.ts` spawns the roster with the engine's `spawnEnemies` (same
  line-up the auto-resolver built), hands it to `launchCombat`, and on dismiss (`Continue` →
  `onEnd`) returns to the dungeon with surviving HP already persisted (`applyCombatHp` inside
  `finalize`) and the outcome on the status line. The overlay claims keys in the capture phase, so
  the dungeon is paused while it's up (explore's `onKey` also early-returns on `isCombatOpen()`).
  Removed the dead `showEncounterPanel`/`closeEncounterPanel` auto-resolve path. e2e reworked: a new
  `fightOnce()` helper drives `testFight → window.__combat.auto() → dismiss() → back to dungeon`,
  asserting a deterministic outcome+XP per seed and that the fight ran on the overlay. Gates: tsc 0
  · 335 engine tests · 22/22 e2e · built + deployed live (HTTP 200).
- **A3.3 — Rules-driven combat controller (CS.9).** Rewrote `apps/web/src/ui/combat.ts` from a
  navigation preview into a real, playable tactical battle driven by the CS.8 `createCombat`
  stepper. The active unit is whoever the stepper says acts: party units get a command menu —
  **Attack** (←/→ to pick a living enemy target, Enter to strike), **Guard**, **Wait**, **Cast**
  (stub) — while enemy units auto-resolve through `defaultAction()`. Every action's events stream
  into an on-screen combat log (`X hits Y for N`, `Y is slain!`); when a side falls the fight ends
  in victory/defeat, awards the accrued XP with a treasure-recovered banner, and **persists
  surviving party HP** back to the roster via `applyCombatHp`. The real created roster fights when
  present (so HP carries across), a demo party otherwise. No rules live here — all math is the
  stepper's. e2e (`?combat=1`) now drives an actual fight: a manual party Attack appends to the log,
  then auto-resolve runs the battle to a terminal `party`/`enemy`/`draw` outcome with consistent XP.
  334 engine / 22 e2e green. (Treasure is a message stub; in-combat spellcasting — feeding the A2.2
  `spellEffects` model through the stepper — is a later A3.x slice; explore→combat hand-off is A3.4.)
- **A3.2 — Interactive combat stepper (CS.8).** New `combat/combatStepper.ts`:
  `createCombat(combatants, opts)` returns a `CombatStepper` — `current()` (whose turn),
  `legalActions()`/`defaultAction()`, `apply(action?)` (one action, returns its events),
  `finish()`/`result()` — that drives combat one action at a time for the player-facing tactical
  screen (CS.9) **without duplicating any rules**: it reuses the exact `Rng`, initiative model,
  `bySideThenId`/`livingOf` targeting (now exported from `combat.ts` as the single source of
  truth), to-hit threshold and damage roll, in the **same RNG draw order** as `runCombat`. So
  `runCombatViaStepper` (drive with the default action each step) reproduces `runCombat`
  byte-for-byte. `runCombat` itself is left untouched, so every existing combat golden stays
  byte-identical; the equivalence is pinned by `combatStepper.test.ts` over 3 rosters + a 60-seed
  fuzz, alongside interactive-surface tests (current actor / legal actions, controller-chosen
  target, and the guard/wait turn-skip consuming no RNG). 334 engine / 22 e2e green. The web
  controller (real keyboard turns) wires onto this in A3.3.
- **A3.1 — Combat grid geometry re-pin (CS.0), CONFIRMED from the COAB oracle.** Recovered the
  real CoK tactical-grid cell→pixel mapping by reading the combat-draw call chain in the COAB
  clean-room decompile (`simeonpilgrim/coab`, MIT — same older Gold Box generation), not a
  screenshot: `ovr033.sub_7416E` → `ovr034.DrawIsoTile(tile, screenPos.y*3, screenPos.x*3)` →
  `OverlayUnbounded` (+1) → `seg040.draw_clipped_picture` (`minX = colX*8`) composes to cell
  `(c,r)` → pixel `(8 + c*24, 8 + r*24)`. So combat cells are **24×24 px** at **origin (8,8)**,
  clipped to the `draw_combat_picture` window **x/y [8,176) = 168×168 = a 7×7 visible cell window**
  (the logical map scrolls under it), with **no foreshortening** (`*3` is a straight orthogonal
  scale). Updated `COK_COMBAT_GEOMETRY` (8×8→24×24 cells, 15×15→7×7) and `BATTLE_BOX` (clip
  128→176) in `render/combatScreen.ts`; fixed the web demo deployment to the 7×7 window; added a
  geometry-pin unit test and promoted research §1.3 CANDIDATE→CONFIRMED with the full derivation.
  327 engine / 22 e2e green. The combat-vs-explore *chrome* reconciliation (does the right panel /
  vsep mask the 168-wide field?) stays CANDIDATE for the CS.10 screenshot gate.
- **A2.3 — Scribe (mage) / Pray (cleric) flow stub.** New ruleset module
  `ruleset/addnd1e-dragonlance/scribe.ts`: the AD&D 1e Intelligence tables — `chanceToLearnSpell`
  (% chance to know a spell), `maxSpellsPerLevel`/`minSpellsPerLevel` (spellbook caps), and a
  deterministic seeded `attemptScribe(int, seed)` — plus `prayerGrant(deity)` (a deity-flavoured
  clerical grant with the deity's bonus spells). The Encamp→Magic command bar is now caster-aware
  (`menuFor`): magic-users get a **Scribe** verb that copies the cursor spell into a persisted
  per-character spellbook (`party.ts` `getSpellbook`/`scribeToSpellbook`/`spellbookCount` under
  `greybox.spellbook.cok`; re-scribing a known spell is a no-op), clerics get **Pray** (petitions
  their deity → "Mishakal answers your prayers"). The Intelligence learn-chance math is wired and
  unit-tested, ready to gate real scroll-learning when item sources land. 326 engine / 22 e2e green
  (6 scribe unit tests + the Magic e2e extended: cleric pray message + mage scribe book 0→1).
- **A2.2 — Cast in camp + spell-effect model + cast SFX.** New ruleset module
  `ruleset/addnd1e-dragonlance/spellEffects.ts`: an engine-pure effect model keyed by spell id —
  `kind` (heal/cure/revive/buff/damage/utility), `target`, optional dice with per-level scaling, and
  a `campCastable` tag — plus `spellEffect(id)`, `isCampCastable(id)`, and a deterministic
  seed-driven `rollSpellAmount(effect, casterLevel, seed)` (cure line heals, Magic Missile/Fireball
  damage, Raise Dead revive, Bless/Prayer buff). Web `magic.ts` gains a **Cast** verb in the
  Encamp→Magic overlay: casting a memorized, camp-castable spell consumes that copy, resolves through
  the ruleset (heals the most-wounded living ally, Raise Dead lifts a fallen member, cures/buffs are
  acknowledged), and fires a zero-asset WebAudio cast cue (`audio/castSfx.ts`, counted for e2e). New
  party helpers `healMember`/`reviveMember`/`mostWoundedName`/`firstDeadName`. 320 engine / 22 e2e
  green (7 spell-effect unit tests + the Magic e2e extended: cleric memorizes Cure Light Wounds, casts
  it → slot consumed + SFX fired). The **combat** cast hook is deferred to A3.3, where the interactive
  tactical screen lands; the shared effect model is ready for it.
- **A2.1 — Spell memorization screen (Encamp → Magic).** New ruleset data + model:
  `ruleset/addnd1e-dragonlance/spellList.ts` (the CoK player spellbook — cleric + magic-user spells
  L1–5, stable ids) and `memorize.ts` (an immutable `Memorization` state machine: memorize/forget
  respecting slot counts). New pure compositor `render/spellbook.ts` (`composeSpellbook`): the
  bordered spell page grouped by spell level with a "used/total" header per level, the memorized
  ×N count beside each spell, a cursor, and a Memorize/Forget/Next/Exit bar (shares the new exported
  `drawOuterBorder`). Web `magic.ts` mounts it as a modal overlay over the explorer: slot counts
  come from `spellSlots` (mage) / `clericSpellSlots` (cleric, Wisdom bonus folded in), the cursor
  walks spells, casters cycle, and each character's memorized set persists (party.ts
  `getMemorization`/`setMemorization`). 313 engine / 22 e2e green (10 spell unit tests + a Magic
  e2e: memorize → forget → persists across reload).
- **A1.3 — View → character-sheet screen.** New pure engine compositor `render/charSheet.ts`
  (`composeCharSheet`): a bordered full-screen page drawn with the 8X8D frame tiles + font —
  name/race/class line, the six ability scores down the left, the derived AC/THAC0/HP line on the
  right, the five saving throws, a deity/Knight-order footer, and a Next/Prev/Exit command bar. The
  web `charSheet.ts` flattens each ruleset `CharacterSheet` into the compositor's view model and
  mounts it as a modal overlay over the explorer (own EGA `ScreenSurface`), cycling party members
  with arrows / N / P / buttons and closing on Esc/Enter/X. The dungeon-bar **View** verb and
  Encamp→View open it. 303 engine / 21 e2e green (6 compositor unit tests + an explore View/cycle/
  close assertion).
- **A1.2 — Rest wired to real effects (game clock + natural healing).** New pure engine modules:
  `world/gameClock.ts` (canonical running-minutes time → `timeFromMinutes`/`gameDayFromMinutes`/
  `formatTimeOfDay`/`formatGameTime`/`formatDuration`) and `world/rest.ts` (`planRest` +
  `daysToHealParty`, AD&D bed rest = 1 HP per living member per day; dead members stay down). The
  explorer now carries a real `clockMinutes` (06:00 day 0 start) instead of a bare day counter:
  Encamp→Rest (and quick-rest R) heal the party to full via `planRest`, advance the clock by the
  days it took, walk the three moons forward, and report elapsed time + HP recovered; the position/
  CAMP line and moonbar show the live Day/HH:MM clock. A fixed-duration camp (`rest(days)`) passes
  time even at full health. 297 engine / 21 e2e green (9 new rest/clock unit tests + an explore
  rest-wire assertion).
- **A1.1 — live command menus in `?explore=1`.** The static `FORWARD BACK TURN REST` prompt is
  replaced by the original CoK dungeon command bar `Area Cast View Encamp Search Look` (hotkey =
  first letter, driven by the explore key handler). Encamp opens the in-screen
  `Save View Magic Rest Alter Fix Exit` sub-menu as a modal (Left/Right move the cursor,
  Enter/Space or hotkey activate; Rest runs the real `restAndRefresh`, Exit returns). WASD/arrow
  movement stays live while the dungeon bar is active. Reuses engine `drawHorizontalMenu` +
  `GB_MENU_DUNGEON`/`GB_MENU_ENCAMP`; explore e2e now gates on `menuOk`. 288 engine / 21 e2e green.
- **Combat screen renderer `composeCombatScreen` (engine, CS.1–CS.5).** The original-look CoK
  tactical combat screen, mirroring `composeExploreScreen`. CS.1 factored the border/separator/
  inner-frame drawing into a shared `drawScreenChrome(fb, tiles, layout)` (explore goldens stay
  byte-identical, 10/10). `render/combatScreen.ts` composes: DUNGCOM/WILDCOM/RANDCOM 24×24
  backdrop tiles (clipped to the battle box `[8,128)×[8,128)`), SPRIT* combatant icons
  (magenta-keyed, bottom-anchored per grid cell, back-to-front), an active-unit cursor, the reused
  chrome + right party panel (active=white/down=gray), a command menu (selected highlighted), and
  the combat-log message region. Grid geometry is data-driven (`CombatGridGeometry`, CANDIDATE
  default) to be pinned vs a real combat screenshot (CS.0). Verified against real CoK assets
  (recognizable combat screen) + 5 synthetic-tile unit goldens; engine 280/280, tsc clean. Plan
  `docs/engine/research/combat-screen-plan-cok.md` + ADR-0008. Next: stepper (CS.8) + web
  controller (CS.9) for interactive play; CS.0 screenshot geometry pin.
- **Intro / title sequence (web, `?intro=1`).** The original CoK boot screens shown the way the
  game opened: decodes the game's own `TITLE.DAX` (block 1 = SSI/AD&D product screen, block 2 =
  the Champions of Krynn title art — three moons/sword/rose/crown), both full 320×200 EGA, each
  presented through `createScreenSurface(DOS_EGA)` at 4:3 (scaled like an emulator). Click/any-key
  advances; after the last it hands off to `?explore=1`. Sidebar "▶ Play Champions of Krynn
  (intro)". `window.__intro` e2e seam; new e2e Intro case → **18/18**. Deployed.
- **In-game command menus (engine + web, `?menus=1`).** The original CoK menus drawn the way the
  game drew them: the dungeon command bar **"Area Cast View Encamp Search Look"** and the Encamp
  sub-menu **"Save View Magic Rest Alter Fix Exit"** (verbs transcribed study-only from the Gold
  Box `displayInput` menu strings in the COAB decompile; Krynn-Gen1 reuses that menu engine). New
  pure renderer `render/menuBar.ts` `drawHorizontalMenu` — the shared hotkey-bar idiom (first
  letter highlighted, selected verb fully highlighted, returns per-item pixel spans for hit-test)
  plus the `GB_MENU_*` verb constants. `composeExploreScreen` gained an optional `menu` field that
  draws it on the bottom row (explore goldens byte-identical — additive). Web view renders it over
  the real exploration chrome + party panel; ←/→ choose, Enter selects, hotkey letter jumps, Encamp
  opens the camp sub-menu, Exit returns. 4 engine tests + e2e Menus case (dungeon→Encamp→Exit flow)
  → **288 unit / 21 e2e**. Screenshot-verified, deployed. **Next:** fold these menus into the live
  `?explore=1` loop (replace the static prompt bar) and wire the verbs to real effects (Save/Rest/
  Magic/View character sheet).
- **Automap floor fix (web).** The top-down nav aid dimmed every unvisited cell to near-black, so
  only the party's own cell read as floor (user-reported). Now the whole map is one floor colour
  with the current cell brighter + a faint walked-trail tint; walls/doors/arrow unchanged. Deployed.
- **Per-platform AUDIO — first slice shipped (web, `?audio=1`).** Music + SFX that switch WITH the
  graphics mode, played in-browser via Web Audio on an MIT-clean, zero-dependency stack. Research
  landed (`docs/engine/research/audio-formats-krynn.md`): the cleanest first target is the Amiga
  IFF path (pure TS, no GPL/WASM), since DOS CoK music (`MIDI.DAX`) is a still-undecoded custom SSI
  driver format. New **pure engine decoders** `packages/engine/src/audio/`: `iff.ts` (EA IFF-85
  chunk reader, big-endian), `svx8.ts` (`decode8svx` → normalized Float32 PCM), `smus.ts`
  (`parseSmus` → tempo + per-track SEvent streams; notes are MIDI pitches, durations are linear
  ticks). Validated against the real CoK assets (4 tests): `krynn` title theme = 4 tracks/7
  instruments/60 BPM with the hand-decoded middle-C→G→E opening; `arrow`/`soundData` 8SVX decode to
  the right rate + sample count. Browser `AudioManager` (`apps/web/src/audio/`) sequences the SMUS
  score as square-wave voices (the `in3` instrument the scores reference *is* the bundled square
  `soundData`) and triggers 8SVX SFX as one-shot `AudioBuffer`s, **bound to the platform identity**
  like `GraphicSetManager`: Amiga arms music + 8 sampled effects; DOS honestly reports
  `MIDI.DAX` as not-yet-decoded (never fakes DOS audio). Sidebar "♪ Music & sound (DOS ↔ Amiga)",
  tempo slider for the one timing constant SMUS leaves to the player. Dev-server middleware +
  recursive asset-bundler so the extensionless Amiga sound files serve raw and ship in the static
  build. New e2e Audio case (sfx=8, Amiga score parsed, DOS reason surfaced) → **20/20**. Deployed.
  **Next audio:** DQK XMI→OPL2 (the documented DOS path) and DoK Amiga multi-track SMUS; DOS
  CoK/DoK music need RE or a DOSBox DRO capture path.
- **Exact-screen compositor substrate + `composeExploreScreen` (engine) — original DOS screen,
  not a CSS look-alike.** Per the directive to restore the exact DOS/Amiga/C64 interface "as if
  running in an emulator", added an indexed-framebuffer pipeline: `render/screen.ts`
  (`IndexedFramebuffer`, `blitIndexed`/`blitIndexedClipped`/`blitTile`/`fillRectIndexed`,
  `framebufferToRGBA`), `renderWallViewIndexed` (the 168×168 first-person view as indices,
  byte-identical to the proven RGBA path), `render/textmode.ts` (1-bpp `BitmapFont` with optional
  folded `mapCode`), `loaders/cokFont.ts` (CoK text font = 8X8D1 block **201**, `ToUpper%64`
  addressing), and `render/exploreScreen.ts` (`composeExploreScreen` → full 320×200 indexed
  screen: outer border, separators, inner viewport frame, 3-D window, party panel, position line,
  message, prompt). The real CoK font + panel + layout render **perfectly** (`renders/explore/`).
  **Frame SOLVED & verified vs a real screenshot (MSE=0):** CoK's exploration border is block
  **202** (set 4) — a clean 6-tile cyan/gray frame (tiles 20 horiz-bar / 24 vert-column / 23,25
  junctions / 21,22 moon-brackets) with the three **moons** (tiles 26–37) overlaid on the top row
  at cols 8/19/30. Recovered by exact pixel-matching block-202 tiles against a genuine CoK DOS
  screenshot; the compositor render matches it **206/206** on cleanly-visible frame cells. This is
  `COK_FRAME_LAYOUT` (the default); `COAB_FRAME_LAYOUT` retained for COAB. (COAB's arrays point at
  tiles 30–39 = CoK's moons, so they drew moons as the border — the wrong path, now corrected;
  a sibling "block 203 / brown wood" research draft was a circular-method artifact, also corrected.)
  Spec `cok-frame-arrays.md`. Also added
  `render/displayScale.ts` — per-platform `PlatformProfile` + `fitDisplayRect`: every platform
  (DOS 320×200 / Amiga 320×256 / C64) shares a 4:3 display box so live mode-switching keeps the
  picture the **same size + correct aspect** (the requested scaler). 268 engine tests, tsc clean.
- **Web app now renders the exact screen LIVE** (`apps/web/src/ui/explore.ts` +
  `ui/screenSurface.ts`). Replaced the explore view's DOM/CSS layout (separate first-person canvas,
  HTML party roster, message DIVs) with a single `<canvas>` the engine paints `composeExploreScreen`
  onto — the whole 320×200 EGA screen (viewport via `renderWallViewIndexed`, block-202 frame, three
  moons, party panel, position line, message window, prompt) as one indexed framebuffer resolved
  through the EGA palette. New `ScreenSurface` host scales it with `fitDisplayRect(DOS_EGA, …)` to
  the canvas box at the platform 4:3 display aspect, nearest-neighbour, devicePixelRatio-aware, and
  repaints on resize — the requested scaler, made tangible. The moon strip + automap stay as Greybox
  companion aids below the emulated screen. Verified visually
  (`renders/explore/app-explore-screenshot.png`, and on production
  `renders/explore/live-explore-screenshot.png`) and via e2e (**17/17**); tsc purity clean.
  **Deployed live at https://dionysus.dk/greybox/?explore=1** (bundle `index-Cl_xVKlw.js`). This
  fulfils the "exact DOS interface, as if running in an emulator" directive end-to-end.
- **Scaler made user-adjustable** (`ui/explore.ts` `buildScalerBar` + `ScreenSurface.setFitOptions`).
  A control bar under the emulated screen exposes the two knobs the user asked for: **Aspect** —
  4:3 (CRT) vs Square pixels (1:1), toggling the profile's `displayAspect` so the same native
  buffer shows aspect-corrected or raw — and **Scaling** — Smooth vs Integer (whole-pixel multiples
  of the 200-line screen). A live readout shows native size, aspect, and scale (e.g. "320×200 native
  → 613×460 (4:3, ×2.30)"). Buttons + the `__scaler` e2e seam share one `select()` path. New e2e
  checks: the aspect toggle changes displayed height; integer mode snaps height to a multiple of 200.
  Deployed (bundle `index-YoeCEr32.js`).
- **Amiga `8X8D1.DAA` plane-order SOLVED + a key finding (`decodeDaaTileStrip`,
  `docs/engine/research/amiga-8x8d-planeorder.md`, committed):** the 4-plane EGA tile-strip
  decode is verified by the **cross-platform identity** — CoK Amiga block 202 decodes to the SAME
  16-colour EGA tiles as DOS, **all 38 tiles byte-identical (0 differing pixels, IoU=1.0)**.
  So CoK's 8×8 dungeon *frame* is NOT platform-distinct — an in-game DOS↔Amiga frame toggle would
  be a no-op (one was briefly wired and reverted). The real Amiga visual win is the **32-colour
  full-screen art** (BIGPIC/portraits/backdrops), already demonstrated by the asset-browser
  EGA↔Amiga BIGPIC live-switch (M3.S2 `?gswitch=1`). A researcher "richer 8×8 art / IoU 0.645" claim
  was a circular-method artifact, corrected. Invariant pinned by `daaTileStrip.test.ts`.
  Net: live platform switching = 32-colour art + display geometry, not the dungeon chrome.
- **8X8D tile header corrected: 17-byte header, not 9+8 — same format as DUNGCOM** (`tiles8x8.ts`,
  `dax.ts`, research `8x8d-tile-encoding-cok.md` rewritten). The `8X8D1/2/3` blocks are the SAME
  multi-item DaxBlock layout as DUNGCOM — a **17-byte header** (`…item_count@8, field_9[8]`), tiles
  at offset 17, **no trailer**. The previous "SOLVED" pass split it as a 9-byte header + 8-byte
  *trailer*, placing tiles 8 bytes too early and **rolling every tile down 2 rows**; that slipped
  through because it was only checked on dense wall blocks (a 2-row roll still looks wall-ish). The
  **UI block 202** (direction arrows/gauges/orbs) exposes it instantly — clean at offset 17, smeared
  at offset 9 (`renders/ui/_cmp_8x8d202_*`, `_wall_b31_*`, `_wall_b203_*`). Fixed `decode8x8dBlock`
  (`TILE8X8_HEADER` 9→17, `TILE8X8_TRAILER` 8→0) and added it to the generic `parseTileSet` path with
  magenta (13) keying for 8X8D. New cross-parity test locks the dedicated + generic paths
  pixel-for-pixel so a regression to offset 9 can't recur. No live regression: the FP viewport doesn't
  yet blit these tiles. 237 engine tests; tsc purity clean.
- **DAX "complex" tile-array format SOLVED — `DUNGCOM`/`WILDCOM`/`RANDCOM` combat backdrops**
  (`dax.ts` `parseTileSet`, `dax-animated.test.ts`, research `§D`). The last CoK block still
  flagged "complex" turned out to be the **multi-item DaxBlock** layout (17-byte header
  `height,width/8,x,y,item_count@8,field_9[8]`, then `item_count` equal-size tiles). CoK
  `DUNGCOM` = **25 tiles of 24×24** EGA (17 + 25·288 = 7217 exact); renders as coherent
  stone-wall pieces (`renders/ui/DUNGCOM_montage.png`). Decoder distinguishes it from a
  standard frame (8-byte header, pixels@8) by the exact-consume gate, tried after the
  frame/strip/animated paths. Triangulated three ways: COAB `DaxBlock.cs` read + the render
  + an independent **Codex** second opinion that reproduced the field table byte-for-byte
  (Gemini 403'd on auth). New `.claude/skills/{codex,gemini}` second-opinion skills drove the
  cross-check. 235 engine tests (golden test against the real `DUNGCOM.DAX`); tsc purity clean.
- **Original Gold Box dungeon screen + the three moons of Krynn** (`explore.ts`, `viewer.css`,
  `party.ts`, engine `moons.ts`) — recomposed the explore view from a modern web layout into the
  authentic CoK in-game screen: one EGA-blue framed panel with the first-person viewport (left), a
  party roster + the three moons (right), a scrolling message window, and a command line, in the EGA
  palette + monospace face. **Moons** (CoK signature, *"the current moon phase is shown at the top of
  the screen"* — `cok-dragonlance-rules.md §9a`): new engine `moonPhaseForDay`/`moonPhasesForDay`
  walk Solinari/Lunitari/Nuitari new→waxing→full→waning at distinct cadences (exact day-periods are
  documented presentation defaults — the Journal prints none; `moonPhaseMods` stays the sourced
  mechanic). Resting makes camp, advances a day, and the discs move. **Roster** shows name·class/
  level·AC·HP (wound-coloured). **Messages** collect area entry + per-cell events + combat outcomes.
  Automap demoted to a labelled Greybox aid below the screen. UI-frame note: `DUNGCOM.DAX` (dungeon
  frame) is still in the unsolved complex-DAX sub-format, and the original border is otherwise 8×8
  tiles + a bitmap font we don't yet extract, so the frame/text are faithful EGA-styled rebuilds for
  now, not the literal blitted asset. 232 engine tests + 17/17 e2e; deployed live.
- **First-person backdrop — attempted authentic `Draw3dWorldBackground`, REVERTED** (`renderWallView.ts`)
  — tried replacing the guessed full-half ceiling/floor with the original's centred sky/horizon/ground
  block (EGA 0 sky, EGA 8 ground). In practice the off-centre 88px block left hard black/gray seams
  ("buggy lines") across the viewport, and the prior simple backdrop read better, so this was reverted
  (commit `7931f91`). The real backdrop is the *textured* `SKY.DAX` strip (block 252 = cobble floor +
  sky bands), not a flat block — revisit only with that asset, wired to indoor/outdoor + time-of-day.
  The 1:1 *geometry* remains confirmed; this was a backdrop-only experiment.
- **Doors & gateways walkable + mapped** (`model/index.ts`, `loaders/geo.ts`, `world/explorer.ts`,
  `apps/web/src/ui/explore.ts`) — GEO plane-3 carries a 2-bit door flag per edge (0 plain wall,
  1 door, 2 gateway). Collision was ignoring it, so a door's wall graphic blocked the party even
  though the original lets you step through. New `MapCell.doors`, unpacked in `decodeGeoBlock`;
  `Explorer.edgeBlocks()` makes a walled edge passable when it carries a door flag (+3 unit tests).
  The automap now draws door edges (amber) and gateways (green) with an opening through the middle
  so they read as discovered passages, not solid cyan walls; header legend added. Deployed live.
- **M3.S2 (BIGPIC slice) live EGA↔Amiga switch** (`apps/web/src/ui/gswitch.ts`, `main.ts`,
  `viewer.css`) — the project's headline "best-of art, switch live" feature, demonstrated on CoK's
  full-screen BIGPIC scenes (the assets that decode in BOTH DOS DAX EGA-16 and Amiga DAA 32-color).
  `decodeDax`/`decodeDaa` feed two `GraphicSet`s into the existing (until now UI-unused)
  `createGraphicSetManager`; the **DOS·EGA / Amiga toggle is a `setActive()` pointer swap + redraw of
  the same `LogicalAssetRef`** — no reload, no second decode — the exact runtime mechanism the
  in-dungeon switch will use. Scenes are the 4 block ids common to both containers (112/114/115/121);
  ‹ ›  cycles them. New `?gswitch=1` route + sidebar entry. The `window.__gswitch` seam (active/
  setPlatform/checksum) backs an e2e that proves the rendered RGBA checksum differs between EGA and
  Amiga for the *same* logical id. **M3.S1 (DAA palette) was already satisfied** — the DAA loader
  embeds the per-frame 32-color 12-bit Amiga palette (`loaders/daa.ts` `readPalette`). The *full
  in-dungeon* wall switch remains blocked on the unsolved 6-byte DAA sub-frame format (Amiga
  `8X8D*.DAA` tiles). 17/17 e2e pass, 228 engine tests green, tsc clean.
- **M2.S7 party HP persistence + rest** (`apps/web/src/ui/party.ts`, `explore.ts`, `viewer.css`) —
  combat damage now carries forward: after an encounter `applyCombatHp()` writes each survivor's
  end HP back into the saved roster (matched by `p{i+1}` id, clamped to `[0,maxHp]`), so the next
  fight starts from the wounds the last one left. A new **party HUD** under the explore stage shows
  every member's name + HP bar (green→amber→red, struck-through "down" at 0), refreshed after each
  fight. **Rest** (the ⛺ header button or the `R` key) restores the party to full via `restParty()`
  — a simplified full-rest until per-spell/turn memorisation lands. New seams `partyHp()`/`rest()`
  back an e2e assertion that HP stays in range after a fight and Rest returns everyone to full.
  16/16 e2e pass, 228 engine tests green, tsc clean.
- **M2.S6b encounter result panel** (`apps/web/src/ui/explore.ts`, `viewer.css`) — combat in the
  explore view now surfaces a visible outcome instead of a silent state change: `resolveCombat`
  renders an `.encounter-overlay` modal with a win/lose/draw banner (+XP), a party column showing
  end-of-fight HP, an enemy column with monster groups (×count) and a defeated tally, and a
  round-by-round combat log that maps combatant ids back to names (party names + replayed `e0…`
  enemy ids via `encounterNames`). Movement keys are guarded while the panel is open; Esc/Enter or
  the Continue button dismiss it. Seams `encounterOpen()`/`closeEncounter()` drive the e2e assertion
  that the panel appears after `testFight` then clears. 16/16 e2e pass, 228 engine tests green, tsc clean.
- **M2.S4d in-browser character creation + party roster** (`apps/web/src/ui/create.ts`, `party.ts`) —
  a full CoK party builder on `createCharacter`, from the sidebar (✦ Create party) or `?create=1`:
  race/sex/class pickers filtered by the rules, racial-range-clamped ability inputs + 4d6 roller,
  armor/shield, deity picker, live validation + derived-sheet preview (THAC0/AC/HP/saves/spell
  slots/Knight order). Roster persists to localStorage; encounters in Explore now fight with the
  created party (demo four as fallback). New e2e case builds a valid char, rejects a Hill-Dwarf mage,
  and checks persistence across reload. Also added the **Greybox dragonhead favicon + branding**.
  16/16 e2e, tsc clean.
- **M2.S4c CoK character creation + validation** (`ruleset/addnd1e-dragonlance/character.ts`) —
  `createCharacter(req)` is the engine-pure core a char-create UI sits on: it validates a request
  (race/sex/classes+levels/abilities/deity) against the manual's rules (class allowed for race;
  multi-class non-humans only; level ≤ racial cap; abilities within racial ranges; cleric needs a
  deity, dwarves → Reorx) — returning every violation at once — and derives the full sheet (best
  THAC0/save across classes with Knight→fighter, STR + deity grant, AC from DEX, per-caster spell
  slots incl. the cleric Wisdom bonus, Knight order). Replaces the ad-hoc demo-party construction with
  a rules-enforcing builder. 12 golden tests (228 total), tsc clean.
- **M2.S4b spell-slot progression** (`ruleset/addnd1e/spells.ts`) — implements the long-deferred
  `RulesetPlugin.spellSlots`. `spellSlots(classId, level)` accumulates per-level PHB delta rows over a
  base 1st-level slot (the SSI engine's own scheme — verified against coab `ClericSpellLevels` /
  `MU_spell_lvl_learn`): magic-user/illusionist on the MU track, cleric/druid on the cleric track,
  others zero. Reproduces the canonical tables (MU L5 = 4/2/1, cleric L9 = 4/4/3/2/1). Adds the 1e
  Wisdom bonus spells (`clericWisdomBonusSpells`, gated like the engine). 10 golden tests (216 total),
  tsc clean.
- **M2.S4 `addnd1e-dragonlance` overlay** (`ruleset/addnd1e-dragonlance/`) — the Champions of Krynn
  ruleset, layered over `addnd1e` via `extends`. Sourced from the game's own Journal/Manual (mined by
  a pdfplumber pass into `docs/engine/research/cok-dragonlance-rules.md`): the **seven** CoK races
  (Human, Hill/Mountain Dwarf, Silvanesti/Qualinesti Elf, Half-Elf, Kender — **no Gnome**, correcting
  the slice's original guess) with verified per-ability ranges + sex STR caps + `clampToRace`; the
  race/class allowance matrix and the (low) level caps (Knight = Human/Half-Elf only, Mage denied to
  dwarves & kender, Mtn-Dwarf no Ranger, Silvanesti no Thief, Kender fighter/ranger STR-tiered 5/6/7);
  Knight-of-Solamnia orders (Crown/Sword/Rose + petition minimums; Knight fights on the **fighter**
  tables); the seven clerical deities with mechanical grants (Kiri-Jolith +1 THAC0, Mishakal +1
  healing die, Majere +2 turn levels; dwarves must take Reorx); and the three-moons mage modifiers
  (full-moon +1 effective level gated on level≥6 ∧ INT≥15). Registered in `defaultRulesets()`; plugin
  + grouped `dragonlance` surface exported. 19 golden tests vs the manual oracle (206 total), tsc
  clean. **This closes M2.S4.**
- **M2.S6 player-UI glue — combat in the browser** (`apps/web/src/ui/explore.ts`) — the explore view
  now *fights* a script-triggered encounter instead of just flagging `⚔ encounter`. It loads the
  area's `MON1CHA.DAX` records (keyed by block id), and when a SearchLocation script pauses with a
  `combat` effect it captures the `EncounterMonster[]` roster and runs `resolveEncounter` against
  those records with a pregenerated demo party (placeholder until char-create / save-import), folding
  a short cell-seeded **deterministic** outcome into the status line —
  `⚔ Victory +90 XP (3rd)` / `⚔ Defeated` / `⚔ Draw`, plus `[n unknown]` for unmatched ids. MON load
  is best-effort (missing data → `⚔ encounter`, never a dungeon failure). e2e gained a
  `window.__explore.testFight` seam asserting a fixed 3-goblin roster resolves to a real deterministic
  outcome in headless Edge. **This closes M2.S6.** 15/15 e2e pass, 187 engine tests green, tsc clean.
- **M2.S6 encounter resolution** (`combat/encounter.ts`) — `resolveEncounter(roster, lookup, party,
  opts)` closes the ECL→combat loop: it maps a VM combat roster's `monsterId`s (via
  `monsterLookupFromArchive`, keyed by `MON{n}CHA` block id) to decoded records, spawns `count`
  combatants per group, adds the party, and runs deterministic combat; unknown ids land in
  `unresolved` instead of throwing. **Full pipeline proven end-to-end**: an ECL block
  (LOAD_MONSTER 9 ×3 → COMBAT) → the VM's roster → `resolveEncounter` against the real MON1CHA
  archive → a deterministic outcome, then `vm.resume()`. Only the player-UI glue remains for M2.S6.
  3 new tests (187 total), tsc clean.
- **M2.S6 VM encounter roster** (`ecl/vm.ts`) — the ECL VM now models the encounter setup that
  precedes a fight: **LOAD_MONSTER (0x0B)** queues a `{monsterId, count, picBlockId}` group (operand
  semantics decoded from COAB `CMD_LoadMonster`: id, copies, CPIC block), **CLEAR_MONSTERS (0x1C)**
  empties the queue, and **COMBAT (0x24)** now surfaces the accumulated roster on its `combat` effect
  (`{ type:'combat'; monsters: EncounterMonster[] }`) and consumes it. This gives the host exactly
  what it needs to map ids → `MON{n}CHA` records → `monsterCombatant` → `runCombat`. 2 new VM tests
  (184 total), tsc clean. (Remaining for M2.S6: the host glue in the CoK player.)
- **M2.S5/S6 party combatant bridge** (`combat/party.ts`) — `partyCombatant(spec, ruleset)` turns a
  PC sheet (class→level, ability scores, weapon, armour) into the neutral `Combatant`, deriving the
  combat numbers through the ruleset: **THAC0** = best `ruleset.thac0(class,level)` across classes −
  STR hit − magic; **AC** = baseAc + DEX defensive mod; **damage** = weapon dice + STR damage +
  magic. With M2.S3b's tables this makes **real PC-vs-monster combat** run end-to-end. Golden: a
  2-fighter party (built via addnd1e, STR/DEX mods folded in) defeats four real CoK goblins
  deterministically and banks 4× the goblin XP, to-hit rule asserted on every swing. 4 new tests
  (182 total), tsc clean.
- **M2.S3b THAC0 + saving-throw tables** (`ruleset/addnd1e/combatTables.ts`) — the `addnd1e` plugin
  now supplies the **attack matrix (`thac0`) and the five-category saving throws (`savingThrows`)**,
  closing the PC-side combat-data gap. Transcribed from the SSI engine itself — the `simeonpilgrim/
  coab` decompile (`ovr018.cs thac0_table`, `ovr026.cs SaveThrowValues`, same engine generation as
  CoK/DoK) — then **cross-validated against CoK's own NPC-class monster THAC0** as a golden test:
  WARRIOR(HD3)→18, SOLDIER(HD7)→14, BLACK ROBE MAGE(HD3)→21 match fighter[3]/[7] & magicUser[3]
  exactly. The engine's `0x3C − hitBonus` THAC0 store (0x3C=60) independently confirms our monster
  `60 − x` inversion. Indexed [class][level] 0..12 (clamped); spell slots still deferred. The
  research sweep that surfaced coab is in `research/decompilation-sources.md`. 6 new tests (178
  total), tsc clean.
- **M2.S5 combat core** (`combat/combat.ts`, `util/rng.ts`) — deterministic AD&D-1e melee resolver
  on a neutral `Combatant` model (id/side/hp/thac0/ac/damage/xp), fed straight from the verified
  monster loader via `monsterCombatant()`. Models 1e initiative (per-round 1d10, low-first), stable
  targeting, to-hit (`d20 ≥ THAC0 − AC`, nat-1 miss / nat-20 hit), damage rolls (min 1), death/
  removal, and party XP accrual; ends on a side wipe or `roundCap` → draw. Seeded `Rng` (shared LCG
  with the ECL VM) makes battles fully reproducible. **Golden: real CoK GOBLIN ×3 vs OGRE** runs a
  realistic, deterministic 9-round fight (goblins need 16 vs the Ogre's AC 5; the Ogre's 1d10 drops
  them — Ogre wins) with the to-hit rule asserted on every event. PC combatants plug in once the
  ruleset THAC0/base-item damage land (M2.S3b). 7 new tests (172 engine total), tsc clean.
- **M2.S6(prereq) monster combat stats PINNED** (`loaders/monster.ts`) — the durable M2.S5 blocker
  is closed. Correlating MON1CHA bytes against canonical AD&D/CoK values mapped: **THAC0** = 60 −
  byte[89], **hit-dice** @214, **damage** = byte[269] d byte[271] + byte[270], **AC** = 60 −
  byte[275], **XP** = u16 @304. Verified across the full roster — AC matches canonical 15/18 (the 3
  deltas are CoK-specific authoritative values); XP is the exact CoK award (Goblin 10, Ghoul 65,
  Ogre 90, Hill Giant 1400, Red Dragon 1950); damage matches (Goblin 1d6, Hill Giant 2d8, Giant
  Snake 3d6, Ghoul 1d3+1, Red Dragon 1d8+3). The `60 − x` inversion is the engine's "to-be-hit"
  store (yields negative dragon ACs). `loaders/monster.ts` now decodes armorClass/thac0/hitDice/
  damage/xp; `docs/engine/research/cok-monster-format.md` updated.
- **M2.S6(prereq) item stack-count + structural finding** (`loaders/item.ts`) — stock-gear rows
  show base weapon damage / armour AC are **not** per-record: they key off the base-type id (off 42)
  into an engine base-item table (standard Gold Box arrangement). Pinned the clean per-record field:
  **offset 57 = stack count** ("20 Arrow"=20, "4 Dart"=4; 0 otherwise). The magic/effect block
  (47–53, 58–62: spell ids, charges) is documented as located-not-pinned. Next item step: extract
  the base-item table from `START.EXE` (ids 5–15) or encode the canonical stock-gear stats.
- **M2.S6(prereq) CoK item loader** (`loaders/item.ts`) — `ITEM{n}.DAX` packs 63-byte item
  records (block sizes are all ×63). Verified: length-prefixed plain-ASCII name and the base-type
  id at offset 42 (stock gear 5..15: vial/sling/bow/sword/mace/…; **0 = special/unique** — named
  magic, quest items, most potions/scrolls). Stat block (bonus/damage/AC/value/charges, offsets
  44–62) needs the CoK item reference; full record kept on `raw`. Golden vs real ITEM1 (39 items);
  `docs/engine/research/cok-item-format.md`. 4 new tests (164 engine total), tsc clean.
- **M2.S6(prereq) CoK monster loader** (`loaders/monster.ts`) — `MON{n}CHA.DAX` decodes as one
  409-byte "character" record per DAX block. Verified fields (correlated against known AD&D
  values across all 26 MON1CHA monsters): name (len-prefixed, ≤15), 6 ability scores (cur,max
  pairs — Ogre/Hill Giant STR 19, animals INT 3), category (7 monster / 6 humanoid / 5 NPC), hit
  points, and the **5 saving throws** (Goblin 16/17/18/20/19, Red Dragon 7/8/9/7/10 — also a
  verified oracle for the 1e monster save tables feeding M2.S3b). AC / HD-count / damage / XP are
  located-but-not-pinned and need the CoK bestiary reference; the full record is kept on `raw`.
  `docs/engine/research/cok-monster-format.md` has the offset map + confidence. Golden vs real
  MON1CHA; 6 new tests (160 engine total), tsc clean.
- **M2.S3a ruleset core** (`src/ruleset/`) — the pluggable rules layer: `RulesetPlugin` contract
  + `RulesetRegistry` (id→plugin, `extends` inheritance chains, cycle/duplicate guards) and the
  `addnd1e` base with the **HIGH-confidence 1e PHB tables**: ability-score modifiers (STR incl.
  exceptional-strength bands, DEX missile/AC/reaction, CON warrior high-CON HP) and class XP/level
  progression (fighter/cleric/magic-user/thief cumulative thresholds + post-name-level increments,
  inverse `levelForXp`, hit dice). Attack-matrix/saves/spell-slots are declared optional on the
  interface and **deferred to M2.S3b** (transcribe against a verified source, not memory) —
  `docs/engine/research/addnd1e-tables.md` records every table's source + confidence. Pure/DOM-free;
  31 new ruleset tests (154 engine total), tsc clean.
- **8X8D wall tiles SOLVED** (`loaders/tiles8x8.ts`) — the wall-tile colour-noise is fixed. The
  block header is **9 bytes, not 8**: the byte at offset 8 is the **tile count**, and reading
  tiles from offset 8 folded that count byte into tile 0, shifting every tile's pixels by a
  nibble. Reading from offset **9** aligns them and the existing chunky-4bpp unpack renders
  authentic CoK dungeon art (cyan-dithered stone, brown beams, magenta-13 transparency, roof
  perspective). The same 17 non-tile bytes were mis-split as 8-header+9-tail; correct split is
  **9-header + 8-trailer**. Validated by rendering blocks 31/203 (chunky vs planar) — chunky is
  unmistakable wall art, planar is noise (`research/8x8d-tile-encoding-cok.md`, now SOLVED). 139
  engine tests + 15/15 web e2e.
- **M2.S3 dungeon step-events** (`ecl/vm.ts`, `world/dungeonEvents.ts`, `apps/web/.../explore.ts`)
  — implemented `ON_GOTO`/`ON_GOSUB` dispatch in the VM (0-based `target[selector]`, out-of-range
  falls through; was a halt stub) and `primeSearchLocation`, which runs a block's `SearchLocation`
  handler with `mapWallRoof` (addr `0x7f79`) = the entered cell's backdrop. The web walk loop now
  fires the area script on each cell-enter and surfaces the result. CONFIRMED on real ECL1 block
  34 (`AND mapWallRoof & 63 → ON_GOTO [0x8287,…]`; open-floor selector 0 = the encounter check).
  138 engine tests + 15/15 web e2e.
- **M1.S5d web Explore = real EGA walls** (`apps/web/src/ui/explore.ts`) — the Explore view now
  calls `loadDungeon` (ECL1 block 34 → GEO 34 + WALLDEF/8X8D) and paints the first-person
  viewport from `renderWallView` (168×168 EGA framebuffer via `putImageData`), replacing the
  flat-shaded quads. 15/15 web e2e pass; screenshot confirms correct wall geometry. Known gap:
  8X8D tile pixels are colour-noise (plane-order/palette unsolved) — geometry is correct.
- **M1.S5d engine `loadDungeon` + `renderWallView`** (`world/dungeon.ts`, `world/renderWallView.ts`)
  — the two pure seams the host calls: ECL-init → {map, walls} orchestration, and compose →
  RGBA framebuffer compositor. GOLDEN vs real CoK ECL1 block 34; 4+4 new tests.
- **M1.S5c first-person real-data integration** (`test/firstperson-integration.test.ts`) —
  end-to-end on real CoK bytes: run ECL1 block 34 → take its `loadFiles`/`loadPieces` effects →
  load GEO 34 + WALLDEF/8X8D → `composeWallView` → resolve blits back to real decoded 8×8 tiles.
  This surfaced and fixed the **`CMD_LoadFiles` two-set dispatch** (below). 123 engine tests.
- **M2 LOAD_PIECES two-set dispatch** (`ecl/vm.ts`) — `resolvePieces` now applies COAB's
  `CMD_LoadFiles` logic (research §3.3): the `0x7F`→block-0 special and the **two-set area
  variant** (when CoK area flags `0x4be7`/`0x4be8` are both set, the set-2 CALL is skipped).
  `loadPieces` effect carries the resolved per-set triple + raw `operands` + `twoSet`. Verified:
  block 34 → operands [3,4,23], twoSet, resolved [3,0xFF,23]; block 3 (2 records) spills into
  slot 2 anyway → 8X8D 31/32/23, all present on disk.
- **M2 ECL dungeon-init effects** (`ecl/vm.ts`) — LOAD_FILES (0x21) → `loadFiles{geoBlockId}`
  and LOAD_PIECES (0x37) → `loadPieces{…}` surfaced as VM effects (the bridge from
  the ECL VM to `decodeGeoBlock`/`loadDungeonWalls`). GOLDEN vs real ECL1 block 34.
- **M1.S5c `loadDungeonWalls`** (`world/dungeonWalls.ts`) — replicates COAB `LoadWalldef` for
  symbol sets 1/2/3: `.Offset` relocation (varA 0/0x46/0x8C), 8X8D pairing (B or B*10+i+1),
  boot sets 0/4 (8X8D 0xCB/0xCA), Reset/0x7F sentinels → the `readId` for `composeWallView`.
- **M1.S5c `composeWallView`/`planWallView`** (`world/wallview.ts`) — far→mid→near cell walk
  (COAB Draw3dWorld/Far/Mid/Near, clean-room) turning a party pose + GEO walls into ordered
  wall-piece placements → tile blits. GOLDEN vs the §4 worked example. With `wallpieces.ts`
  (piece table / tile routing) + the 8X8D decoder + the GEO nibble fix, the first-person
  EGA wall pipeline is now complete end-to-end in the engine.
- **ECL framing correction** (`ecl/disassemble.ts`) — the header is **22 bytes** (0x1388 magic
  + 5 far pointers), entry at byte 22 = vaddr 0x8014 (`ECL_VBASE` 0x7FFE). Fixes a latent
  off-by-2 that shifted every jump target; lands inline 6-bit strings + count-driven
  ON_GOTO/menu operands. Goldens re-derived to EclDump ground truth.
- **WALLDEF table decoder** (`loaders/walldef.ts`) — WALLDEF*.DAX = N×780-byte records,
  each a 5×156 grid of 8×8-tile IDs (the first-person wall LAYOUT, not pixels). GOLDEN vs
  real WALLDEF1 block 23.
- **M1.S6** **animated DAX pictures decoded in-engine** (`loaders/dax.ts` parseAnimatedPicture)
  — SPRIT monster sprites (3 view-sizes, magenta mask) + PIC portraits (frame-0 XOR delta)
  now render. CoK-DOS whole-game 410→**643** frames; GOLDEN vs real SPRIT1 (5×3 frames);
  screenshot-verified (draconian warrior, casting mage).
- **complex-DAX CRACK** (research + `tools/dax_complex_decode.py`) — retires the project's
  biggest graphics unknown: the "complex" blocks are the M1.S6 multi-frame format + the
  WALLDEF tile-index tables, NOT a transparent-RLE. (CONFIRMED vs COAB + CoK/DoK bytes.)
- **M2.S2** **ECL VM** (`ecl/vm.ts`) — random-access interpreter: control flow, address-keyed
  memory, COMPARE/IF, SAVE/arith/RANDOM, NEWECL chaining; host opcodes emit `EclEffect`s and
  pause for input/combat. GOLDEN by stepping real ECL1 block 18 (default→exit; flag→chain 80).
- **M1.S5b** **first-person 3-D projection** (`world/viewport.ts`) — pure perspective
  `WallQuad`s from GameMap+Pose (occlusion, painter order); web renders flat-shaded corridor
  beside the automap. 8 geometry tests; screenshot-verified.
- **M1.S5a** **walkable CoK dungeon in-browser** — Explore view loads real GEO1 block 32,
  spawns the `Explorer`, renders a top-down automap, arrow-key movement with engine wall
  collision. e2e walks the 256-cell dungeon, 0 errors. (First-person WALLDEF viewport = next.)
- **M1.S4** **CoK GEO map decoder** (DAX GEO block → `GameMap`; N/E walls confirmed, S/W
  derived via shared edges; planes 2/3 kept raw). GOLDEN vs real GEO1 block 32.
- **M2.S1** **ECL disassembler** (Krynn-Gen1, 65 opcodes from EclDump IL) + `readDaxDataBlocks`.
  GOLDEN vs real ECL1 block 18 (25 instrs, NEWECL→[80,97,68], EXIT@0x8083).
- **M1.S2(build-path)** `buildGraphicSet` (loadGame frames → GraphicSet).
- **M1.S5(core)** GameMap model + `Explorer` movement engine (turn/step/collision/event) — 9 tests.
- **M1** Champions of Krynn **DOS-EGA graphic set** (`champions-dos.json`) renders from the
  Library (detect CoK DOS install → 410 frames). e2e 12/12.
- **M1.S2** `GraphicSetManager` (live switch + per-frame fallback) — 7 tests.
- **M0.S4+S5** game detection profiles + folder fingerprinter (engine) and the
  **"Add asset folder reference" Library UI** (web) — 11 detect tests + e2e detection case.
- **M0.S1** pnpm-monorepo migration (`packages/engine` + `apps/web`).

Engine unit tests: **164 pass**. Web e2e: **15/15**. Workspace `tsc --noEmit`: clean.

The hard RE unknowns are **retired** and golden-verified against real CoK bytes: GEO maps,
Krynn-Gen1 ECL (opcode table + working VM), and the "complex" DAX variant (SPRIT/PIC +
WALLDEF). Research in `research/{geo-map-format-cok,ecl-krynn-gen1-opcodes,dax-complex-subframe}.md`.
Remaining GEO unknown: planes 2/3 (S/W-walls vs backdrop/event) — not needed for movement.

The first-person EGA wall pipeline is now complete **end-to-end, engine + web**: the engine
exposes `loadDungeon` (ECL init → GEO + WALLDEF/8X8D) and `renderWallView` (compose → 168×168
RGBA framebuffer), and the **web Explore view drives them on real CoK bytes** (ECL1 block 34 →
GEO 34) — the first-person viewport now `putImageData`s real wall tiles in correct perspective
(far→mid→near, side walls, openings), replacing the flat-shaded quads. Verified: 139 engine
tests + 15/15 web e2e, plus a screenshot showing correct wall *geometry* **and texture**.

**M2.S3 (done):** per-step dungeon events are wired — `SearchLocation` runs on each cell-enter
and dispatches by the cell backdrop via `ON_GOTO` (`world/dungeonEvents.ts`). Event **text is
real and already decoded** — the room descriptions are inline 6-bit strings the disassembler
expands (e.g. backdrop 13 → "THIS ROOM IS STACKED TO THE CEILING WITH MOLDERING SKELETONS…",
backdrop 0 → "MONSTERS ATTACK!"). The web walk loop drains each script through its pauses and
shows the text. So **no separate string-table phase is needed for ECL event text.**

**M2.S6 (done):** a script-triggered encounter now *resolves to a real, deterministic combat* in
the browser explore view (`apps/web/src/ui/explore.ts`), against the area's decoded `MON1CHA`
records with a pregenerated demo party, surfacing the outcome (`⚔ Victory +90 XP (3rd)` / `Defeated`
/ `Draw`) in the status line. The whole ECL→roster→records→combat→outcome path is now exercised
end-to-end in headless Edge (`window.__explore.testFight`).

What remains for "first playable CoK" on this axis (now interactive polish, not plumbing): a real
**combat + menu/dialog UI** to *act* on the COMBAT/INPUT/menu pauses (the host still auto-picks
defaults to drain them, and the demo party is a placeholder until char-create / save-import); and
gating the open-floor wandering-monster check so it isn't checked every step.

**The 8X8D wall-tile gap is now closed** (see the top build-log entry): the first-person
viewport renders real CoK wall textures, not noise. The fix was a one-byte header mis-count, not
a plane-order problem — chunky-4bpp was always the right pixel encoding
(`docs/engine/research/8x8d-tile-encoding-cok.md`, SOLVED).

**Combat (M2.S5) — ready in structure, blocked on stat data.** The ruleset layer (`src/ruleset/`,
M2.S3a) and the CoK monster + item loaders (M2.S6 prereqs) are in: verified monster HP, ability
scores, category, and the **5 saving throws** (a real 1e-save oracle), plus verified item names +
base-type ids. Ability-score modifiers and class XP/level tables are golden. **What blocks a real
attack-resolution loop is the data the loaders can't yet pin without an external reference:**

- **Monster AC, hit-dice count, number of attacks, damage dice, XP award** — located in the 409-byte
  record but unassigned (`research/cok-monster-format.md`). Need known CoK bestiary values (the
  *Adventurer's Journal*) or a cross-check against DQK's `MONST.GLB`.
- **Item AC / damage / magic bonus / value / charges** — offsets 44–62, unassigned
  (`research/cok-item-format.md`). Need the CoK item list or the adapted FRUA `ITEM.TXT` semantics.
- **PC THAC0 + PC saving-throw tables by class/level** — deliberately deferred (M2.S3b); transcribe
  against a verified PHB/DMG source or extract from `START.EXE`, not memory
  (`research/addnd1e-tables.md`). The monster save data already verifies the *monster* save matrix.

Two faithful leads from the monster data: monster THAC0 ≈ `60 − record[89]` (Goblin→~21,
Ogre→15, Hill Giant→12, Red Dragon→10), and the monster save matrix by HD is now sampled from real
bytes — both worth confirming when the combat loop lands.

Next without new references: decode `MON*ITM` (monster inventories → item ids, cross-checkable
against the item loader) and `MON*SPC` (special abilities); or cross-map monster stats via DQK
`MONST.GLB`.

Deferred (need the user's desktop to smoke-test): **M0.S2** Electron shell, **M0.S3** ui-kit.
