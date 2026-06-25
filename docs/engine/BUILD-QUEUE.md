# Build Queue — autonomous loop worklist

> **This is the loop's single source of truth.** The full-auto loop (resume phrase
> "resume goldbox full-auto") pulls the **top unchecked `[ ]` item**, builds it end-to-end
> per the ORCHESTRATION quality gates (engine + web + `tsc` + `vitest` + e2e + deploy to
> `/greybox/`), checks it off with a one-line result + date, updates `ROADMAP.md` and the
> project memory, then schedules the next iteration. Goal: **recreate all three Krynn games
> and every system.** Order = dependency + value. Re-order freely; never skip the gates.
>
> Pause for the user only on the ORCHESTRATION forks (public release, copyrighted-asset
> distribution, which game ships first, licensing). Everything below is pre-authorized.

## Conventions
- One item ≈ one shippable slice (a few hundred LOC + tests + an e2e case). If an item is
  bigger, split it in place and do the first sub-item.
- Engine logic stays **pure** in `packages/engine`; browser/DOM in `apps/web`. New original
  screens go through the indexed-framebuffer compositors (`composeExploreScreen` family).
- Every slice: `pnpm -r exec tsc --noEmit` (0) · `vitest` green · `node apps/web/test/e2e.mjs`
  (all pass) · `pnpm build:app` + deploy + live HTTP 200 check · commit with the required
  trailers.

---

## Phase CT — Real tactical (grid) combat ⭐ TOP PRIORITY (user-directed 2026-06-23)

> **User feedback:** "The game is trying to be a D&D board game — characters walk around in
> squares and can cast spells, shoot projectiles or melee attack and use other abilities. But
> when you do combat you show the game UI." Correct: the old `combat.ts`/`combatStepper.ts` model
> is **positionless** (fixed layout, `move` is a no-op, everyone hits "the first living foe", Cast
> is a stub). Target chosen by the user: **FULL FAITHFUL** Gold Box tactical combat, **CoK first**.
> Build in layers toward the complete system; keep e2e green throughout; rewire the screen once the
> engine is real. The legacy positionless path stays only for overland auto-resolve.

- [x] **CT.1** Tactical combat **engine core** (pure). New `packages/engine/src/combat/tactical.ts`:
      positions as first-class state — grid + terrain (blocked / blocks-sight / per-cell cost),
      8-directional movement with a **reachability** solve (Dijkstra, budget = move allowance),
      **Chebyshev** melee reach + multi-attack, **missile/thrown** fire (range + **line-of-fire**
      occlusion by bodies/terrain + ammo), guard/wait, **flee-off-edge**, the per-round initiative +
      turn loop, a built-in AI (`aiAction`) and `finish()` auto-resolve. Same 1e to-hit/damage as
      `combat.ts`. Spells/abilities/morale typed into the model as extension points. 14 tests
      (geometry, reach w/ cost+blockers, LoF shadowing, range gating, flee, deterministic AI battle).
      ✅ 554 engine / tsc 0 / 34 e2e. Commit a8…/dc408d0. **[2026-06-23]**
- [x] **CT.1b** **Verify the engine against the COAB decompile** (user: "verify it against the decompiled
      game files"). Research `docs/engine/research/combat-mechanics-coab.md`; five fidelity corrections
      applied to `tactical.ts`: (1) **death threshold** — 0 HP = unconscious/revivable, dead only at
      `damage ≥ hp+10` (`sub_32200`), new `unconscious`/`dead` flags + `dead` slain field; (2) **diagonal
      movement** — budget in half-steps (orth 2 / diag 3 = 1.5 cells, the 5-10-15 rule), `movementLeft()`
      still in cells; (3) **initiative** — stored per-combatant value (×2 hasted/÷2 slowed), sorted
      high-first, stable across rounds, NOT a per-round d10 (`calc_group_inituative`); (4) **attacks/round**
      — new `halfAttacks` (COAB half-units), 3/2 fighter swings 2,1,2,1… (`sub_3EDD4`/`sub_3EF0D`); (5)
      **morale** — percentage (0..102) failing below %HP-lost, broken monster flees (`sub_3637F`). +5
      tests. ✅ 560 engine / tsc 0 / 34 e2e. Commit dc408d0. **[2026-06-23]**
- [ ] **CT.2** Wire the tactical engine into the **combat screen** (`apps/web/src/ui/combat.ts`):
      replace the `createCombat` stepper for the player-facing battle with `createTacticalCombat`
      on a real CoK encounter map (DUNGCOM/WILDCOM terrain → blocked/sight/cost), the 7×7 viewport
      **scrolling** to follow the active unit (`mapScreenTopLeft`), a **movement cursor** (arrow to
      walk, shows reachable cells), **Move/Attack(target pick)/Aim(missile line)/Guard/Wait/Flee**
      commands, and per-action SFX. Keep `launchCombat`'s overlay + treasure/return flow. Update the
      e2e combat cases to drive movement + a missile shot. (DoK/DQK adopt it after CoK.)
- [ ] **CT.3** Build the **encounter map** from the real terrain: derive the battle grid + obstacles
      from the dungeon cell / wilderness tile the fight starts on (walls block move + LoF), place the
      party + monster groups at faithful start ranks, scale grid to the encounter.
- [ ] **CT.4** **In-combat spellcasting**: memorized-spell list → target/aim, spell **range**, **area
      of effect** (burst/line/cone), **saving throws** + half/negate, damage/heal/condition (magic
      missile, sleep, stinking cloud, fireball, cure wounds, hold). Consumes the memorized slot.
- [ ] **CT.5** **Class abilities + status**: multiple attacks already in CT.1; add **turn undead**
      (cleric), thief **backstab** (facing/positioning), and the status conditions (asleep/held/
      slowed/hasted/feared) flowing through the turn loop + to-hit/AC.
- [ ] **CT.6** **Morale + monster AI**: 2d6 morale checks (bloodied / leader slain) → flee or
      surrender; smarter target selection (focus fire, casters hang back, archers kite).
- [ ] **CT.7** **Fidelity pass**: opportunity/"guarded" reactions, terrain effects (difficult ground,
      web/cloud zones), large (>1 cell) monsters, ranged-into-melee penalties, fleeing party via map
      edge, and the post-battle bring-up (who's dead/unconscious, XP, treasure) on the real grid.

## Phase A — Champions of Krynn: complete the playable game (lead deliverable)

### A1. Menus & camp made live
- [x] **A1.1** Fold the command menus into the live `?explore=1` loop: replace the static
      `FORWARD BACK TURN REST` prompt with the real `Area Cast View Encamp Search Look`
      bar (engine `menuBar`), driven by the explore key handler; Encamp opens the
      `Save View Magic Rest Alter Fix Exit` sub-menu in-screen; Exit returns. Keep WASD/arrow
      movement when the dungeon bar is active.
- [x] **A1.2** Wire **Rest/Encamp** to real effects: pass game-time, natural healing, and the
      three-moons phase advance (reuse the existing `rest()` + moon engine); show elapsed time.
- [x] **A1.3** **View** verb → the original **character-sheet screen** (`composeCharSheet`):
      name/class/level/AC/HP/stats/THAC0/saves from the ruleset, drawn on the bordered screen
      with the 8X8D font. Cycle party members. New pure compositor + e2e.

### A2. Magic system (player-facing)
- [x] **A2.1** Spellbook + **memorization** screen (Encamp→Magic→Memorize): list known spells
      by level, slots from `clericSpellSlots`/`spellSlots`, choose memorized set. Pure model
      already exists in `ruleset/addnd1e/spells.ts`; add the screen + state.
- [x] **A2.2** **Cast** in camp (healing/utility) and the cast hook in combat; spell effects
      resolve through the ruleset. SFX on cast (audio layer). *(2026-06-22: engine
      `spellEffects.ts` model — heal/cure/revive/buff/damage, per-level dice scaling, camp-castable
      tags, deterministic `rollSpellAmount`; Encamp→Magic→Cast consumes a memorized copy, heals the
      most-wounded ally / raises a fallen member, fires a WebAudio cast cue. 320 engine tests, 22 e2e
      pass. Combat cast hook deferred to A3.3 — interactive combat screen lands there.)*
- [x] **A2.3** **Scribe** (mage) / pray (cleric) flow stub → message + slot bookkeeping.
      *(2026-06-22: engine `scribe.ts` — AD&D 1e Intelligence learn-chance + min/max spells-per-level
      caps + seeded `attemptScribe`, plus `prayerGrant` (deity-flavoured cleric grant). Magic overlay
      command bar is now caster-aware: mages get **Scribe** (copies the cursor spell into a persisted
      per-character spellbook, INT math ready for scroll sources), clerics get **Pray** (deity message).
      326 engine tests, 22 e2e pass — e2e extended: cleric pray → Mishakal message, mage scribe → book
      0→1 with a no-op re-scribe.)*

### A3. Combat made playable (finish the tactical screen)
- [x] **A3.1** **CS.0** geometry re-pin: recover the real combat grid origin/cell size from a
      CoK combat screenshot (MSE=0 method used for `COK_FRAME_LAYOUT`); replace the CANDIDATE
      `COK_COMBAT_GEOMETRY`. *(2026-06-22: recovered from the COAB clean-room oracle instead of a
      screenshot — `ovr033`/`ovr034`/`seg040` combat-draw chain gives cell (c,r) → pixel
      (8+c·24, 8+r·24), 24×24 cells, clip x/y [8,176)=168px=7×7 window, no foreshortening.
      `COK_COMBAT_GEOMETRY` 8×8→24×24, 15×15→7×7; `BATTLE_BOX` 128→176; geometry-pin test +
      research §1.3 CANDIDATE→CONFIRMED. 327 engine / 22 e2e pass. Combat-vs-explore chrome
      reconciliation still CANDIDATE → CS.10.)*
- [x] **A3.2** **CS.8 stepper**: additive `combat/combatStepper.ts` driving the EXISTING
      `combat/combat.ts` + `world/encounter.ts` math — turn order, move/attack/cast/guard/wait,
      hit/damage/death applied to combatant state. No rules duplication. Unit goldens.
      *(2026-06-22: `createCombat(combatants, opts)` → `{current, legalActions, defaultAction,
      apply, finish, result}` replays runCombat's exact Rng/initiative/targeting/to-hit/damage in
      the same draw order; `runCombatViaStepper` re-expresses runCombat over it. runCombat left
      untouched (goldens byte-identical); equivalence pinned over 3 rosters + 60-seed fuzz, plus
      player-chosen-target + guard/wait no-RNG-skip tests. Shared `bySideThenId`/`livingOf` exported
      from combat.ts (one source of truth). 334 engine / 22 e2e pass.)*
- [x] **A3.3** **CS.9 controller** in `?combat=1`: real turns (cursor move + attack target +
      cast), enemy turns via the stepper, combat log, victory/defeat, then XP + treasure award.
      *(2026-06-22: `apps/web/src/ui/combat.ts` rewritten onto the CS.8 `createCombat` stepper —
      active unit = stepper's current actor; party turns choose Attack (←/→ target select + Enter) /
      Guard / Wait / Cast (stub), enemy turns auto-resolve via `defaultAction`; events stream into a
      combat log; victory/defeat awards XP + a treasure-recovered message and persists party HP via
      `applyCombatHp`. Real roster used when present (HP persists), demo party otherwise. e2e drives
      a manual attack (log grows) then auto-resolves to a terminal outcome with XP. 334 engine / 22
      e2e pass. Treasure is a message stub; in-combat casting deferred to a later A3.x.)*
- [x] **A3.4** Hook live `?explore=1` encounters into the playable combat screen (replace the
      auto-resolved `testFight`); return to the dungeon on victory.
      *(2026-06-22 — `launchCombat(cfg)` overlay in `ui/combat.ts` (shared `beginBattle`/asset-load
      core with the `?combat=1` view); `explore.ts` now spawns the roster with `spawnEnemies` and
      hands off to the modal interactive battle (capture-phase keys pause the dungeon), returning
      via `onEnd` with HP persisted + the outcome on the status line. Removed the old auto-resolve
      `showEncounterPanel`. Gates: tsc 0 · 335 engine · 22/22 e2e (explore fight drives the overlay
      `auto()`→`dismiss()`) · deployed live 200. Commit `d54a213`. Follow-ups: combat visuals
      `e8ce5cd` (flagstone floor instead of wall-tiling, narrowest SPRIT frame for enemy icons);
      DOS EGA de-roll `65e2b28` (TITLE block 1 was stored rolled +13px — un-rolled on decode in
      both TS + Python decoders, synthetic test added).)*

### A4. Items & inventory
- [x] **A4.1** **Inventory screen** (from the character sheet): list items via `loaders/item.ts`,
      ready/equip (AC + THAC0 recompute), use, drop. Pure compositor + state.
      *(2026-06-22 — engine base-item catalogue `items/baseItems.ts` (CoK ids 6–15 pinned + internal
      ids ≥64 for armours CoK stores as specials; damage/armorAc/twoHanded) + immutable model
      `items/inventory.ts` (ready/unready/toggle with single-weapon/armour/shield + two-hander↔shield
      rules, drop, use-consumable, `wornArmorAc`/`computeArmorClass(dexDefensiveAc)`/`computeThac0`) +
      pure `render/inventory.ts` `composeInventory` (bordered page, AC/THAC0 line, scrolling item list
      with cursor + READY tags, command bar). Web: per-character pack persistence + class starter kits
      in `party.ts` (`getInventory`/`setInventory`); `ui/inventory.ts` overlay opened via a new **Items**
      verb on the char sheet (↑/↓ cursor, R ready, U use, D drop, N/P cycle members) — sheet defers its
      keyboard while the pack is up. Gates: tsc 0 · 356 engine (12 model + 7 compositor) · 22/22 e2e
      (explore View→Items: ready flips + AC recompute + drop) · deployed live 200.)*
- [x] **A4.2** **Trade** between party members; encumbrance/readied-weapon rules.
      *(2026-06-22 — engine: `weightTenths` on every base item + `inventoryWeightTenths`/`Lb` +
      `transferItem(from,to,index)` (moves the whole stack, un-readies it on hand-off = the
      readied-weapon rule) + new `items/encumbrance.ts` (`encumbrance(weightTenths,str,strPct)` →
      level/label/moveRate stepping unencumbered→light→moderate→heavy→overloaded at 1×–4× a
      Strength-scaled allowance; thresholds CANDIDATE, isolated for a later pin). `composeInventory`
      gained a WT/encumbrance line + a status line. Web: `ui/inventory.ts` shows weight+encumbrance,
      a **Give** verb (G) hands the cursor item to the next member (both packs persist), and a new
      standalone **`?inv=1`** route. Gates: tsc 0 · 366 engine (15 inventory + 5 encumbrance + 9
      compositor) · 23/23 e2e (new Trade case: fighter→cleric give, giver loses + recipient gains the
      sword, weight drops; plus weight/encumbrance readout in the explore drive) · deployed live 200.)*
- [x] **A4.3** Combat-won **treasure** screen (Money/Items/Exit menu) → into inventory.
      *(Done — `items/treasure.ts` (`rollTreasure`/`hasTreasure`: gold ≈ ΣXP/2 jittered, 1/3
      per-foe item drop from the base catalogue) + `render/treasure.ts` (`composeTreasure`:
      TREASURE header, gold line yellow→grey when taken, item rows w/ cursor + TAKEN tag,
      Take All/Money/Items/Exit bar) + 14 engine tests. Combat `finalize()` rolls a haul on
      victory and enters a `'treasure'` phase; party gold pool (`getGold`/`addGold`) +
      `giveItemsToMember` bank it. Explore e2e drives a guaranteed win and asserts gold
      conservation + items land in the leader's pack. 380 vitest, 23/23 e2e · deployed live 200.)*

### A5. World & content
- [x] **A5.1** **Wilderness/overland travel** screen (CoK area map): step between locations,
      time passage, random-encounter checks → combat.
      *(Done — `world/wilderness.ts` (`WildernessTravel`: 4-dir steps over a terrain grid, edge +
      impassable-water blocking, per-terrain travel-minutes, `checkEncounter` d100 wandering-monster
      roll via the engine Rng; `wildernessFromAscii` map authoring) + `render/wildernessScreen.ts`
      (`composeWildernessScreen`: region name, terrain-coloured area map w/ party marker + site dots,
      info panel, Move/Look/Camp/Map/Exit bar) + 17 engine tests. Web `ui/wilderness.ts` (`?wild=1`):
      a hand-authored demo region "Vale of Solace", arrow-key travel that advances the clock + moons
      and hands wandering encounters to the live combat overlay; arrival announced at named towns.
      397 vitest, 24/24 e2e (overland edge/water blocking, time-cost, town arrival, encounter→combat)
      · deployed live 200.)*
- [x] **A5.2** **Town hub** + locations: shop (buy/sell via item DB), temple (heal/cure/raise),
      training hall (level-up via ruleset progression), tavern (rumors/journal).
      *(Done — `town/shop.ts` (catalogue list prices + half buy-back, pure buy/sell txns) +
      `town/services.ts` (temple `healCost`/`raiseCost`, training `checkTraining`/`trainingCost`/
      `levelHpGain` keyed off the 1e XP tables) + `render/townScreen.ts` (`composeTownScreen`: shared
      title/steel/option-list/price/status/bar page) + 20 engine tests. Web `ui/town.ts` (`?town=1`):
      hub → Shop (buy/sell vs the gold pool + leader pack), Temple (heal wounded per-HP, raise dead),
      Training Hall (XP-gated level-up: +HP, THAC0 delta, class level), Tavern (rumours). party.ts
      gained an XP store + `awardPartyXp` (combat victory now feeds it) + train/shop/temple helpers.
      417 vitest, 25/25 e2e (buy/sell gold+pack, level-up, temple heal/raise) · deployed live 200.)*
- [x] **A5.3** **NPC dialogue** through the live ECL VM in the dungeon: events fire text/ask/
      branch/give/encounter against the running explorer (the VM + host already exist).
      *(Done — engine `world/dialogue.ts` `DialogueRunner` wraps a minimal `EclMachine {run,resume}`
      (real `EclVm` + test stubs both satisfy it) and translates the run/resume lifecycle into a
      `DialogueTurn` stream: `say` (text + menu/input/who choices), `combat` (queued roster → host
      runs it, then `afterCombat()`), `chain` (NEWECL → next block), `end`. Branching is emergent —
      the chosen value is what the script's own IF/ON_GOTO reads next. `dialogueForCell` builds one
      off a cell's SearchLocation handler. 6 engine tests. Web `ui/dialogue.ts` (`?dialogue=1`):
      overlay presents NPC text + clickable choices / number entry, hands `combat` turns to
      `launchCombat` and resumes after; standalone demo drives an original hand-authored hermit
      conversation (greet→omen chain · bribe→numeric input · draw-steel→live fight→resume).
      `explore.ts` now peeks each step event: a genuine multi-choice ASK opens the interactive
      overlay (real MON-record foes via `dungeonSpawn`); everything else auto-drains as before.
      423 engine / 26 e2e (new dialogue case: ask/branch/chain/bribe/fight→resume) · deployed live 200.)*
- [x] **A5.4** **Journal** entries decoded + viewable (copy-protection replacement) — M4.S3.
      *(Done — the original prints "record it in Journal Entry N" and you read a physical booklet (no
      journal opcode — confirmed against the EclDump). Clean-room equivalent: engine `world/journal.ts`
      `Journal` — a persistent, serialisable log of numbered pages (id/title/body/category/day/read),
      `record` idempotent by entry-number (+ optional replace) so a re-firing event never dupes a page,
      auto-numbering, category filter, read/unread tracking, JSON round-trip. `render/journalScreen.ts`
      `composeJournalScreen` paints the bordered page — entry list (cursor '>', unread '*', category
      colour, yellow number) + a word-wrapped reading pane (`wrapText`) + command bar. 20 engine tests.
      Web `ui/journal.ts` (`?journal=1`, sidebar "📖 Adventurer's Journal"): persisted store
      (`greybox.journal.cok`) + original-authored demo seed, ↑↓ read / F filter / M mark-all / Esc; a
      `recordJournal` helper other views call — the **Tavern "Listen"** now logs the rumour you hear and
      a paid **hermit bribe** in `?dialogue=1` records the keep's muster as a quest page (the "ECL event
      records a journal entry" path, via a new dialogue `onEnd` hook). 443 engine / 27 e2e (journal:
      seed, read/unread, idempotent record, filter, persist) · deployed live 200.)*

### A6. Persistence & polish
- [x] **A6.1** **Save/Load** our party + position + flags (Save verb writes; a Load picks it up);
      round-trip unit test.
      *(Done — engine `world/saveGame.ts`: a versioned, serialisable `SaveGame` snapshot (party
      `CharacterSheet[]` + per-character stores (memorize/spellbook/inventory/xp) + gold + journal +
      `SavePosition` (area/x/y/facing) + clock + open-ended `flags`). `makeSave` normalises/defaults
      every field, `serializeSave`/`deserializeSave` round-trip + self-repair (bad facing→north,
      non-numeric flags dropped, partial blobs filled, non-saves → null, forward versions still load),
      `summarizeSave` for a load list. `Explorer.setPose` added so a load can teleport the party. 7
      engine tests (round-trip, defaults, junk-repair, reject). Web `ui/saveStore.ts` bundles the
      scattered localStorage stores into a slot (`greybox.save.cok.<slot>`) + `applySave` writes them
      back and refreshes the live caches (`reloadStores` in party.ts, `reloadJournal` in journal.ts).
      Explore **Encamp → Save** now writes the quick slot; a **Load** restores it live (teleport +
      clock + HUD) and `?explore=1&load=1` restores it on boot. 450 engine / 27 e2e (the explore case
      gained a save→walk-away→load round-trip + a reload-persistence check) · deployed live 200.)*
- [x] **A6.2** **In-game audio wiring**: app-wide `gameAudio` service (scenes title/explore/combat
      at tempos 8/8/12 + action SFX), wired into intro/explore/combat; audio follows the gswitch
      platform; DOS honestly silent. (2026-06-22)
- [x] **A6.2b** **SMUS playback fidelity** (user: "the music is pretty much all off — the tempo, the
      tones used"). Research `docs/engine/research/amiga-smus-playback.md` + a byte-probe. Three fixes:
      (1) **tempo** = SHDR `tempoRaw / 128` not `/256` (the /256 ran every score at HALF speed —
      CoK krynn 60→120 BPM, DoK 148); (2) **durations** — the SNote `data` byte is a packed note-value
      code (`chord|tie|nTuplet|dot|division`), decoded by new `durationBeats()` to real quarter-note
      beats, NOT raw ticks; (3) **timbre** — voices resolve their oscillator from the named instrument
      (in1 square / in2 triangle / in3-7 sawtooth) instead of forcing square. Inline SID_Tempo honored;
      `ticksPerQuarter` guess → `tempoScale` global stretch (scene speeds 1/1/1.15; UI slider = percent).
      ✅ 555 engine / tsc 0 / 34 e2e. Commit 843e10b. **[2026-06-23]** (Deferred-optional: authentic
      8SVX PeriodicWave timbre, instrument envelopes, SMUS looping, svx8 multi-octave bug.)
- [x] **A6.3** **Full flow**: `apps/web/src/ui/play.ts` orchestrator stitches intro → create →
      explore (↔combat↔camp↔town) → save as one `?play=cok` entry; `window.__play` seam; e2e walks
      the whole chain (29 e2e). (2026-06-22)

## Phase B — Death Knights of Krynn (Krynn-Gen1 sibling)
- [x] **B1** DoK **detection profile** + boot from the Library; DoK DAX/GEO/ECL load path. Engine:
      3 Gen1-format deltas fixed (GEO non-1024 header, 0x7F WALLDEF sentinel on any set, 8X8D 256-tile
      saturated count) + DoK load-path golden. Web: `gameData.ts` registry, `startExplore(gameId)`,
      `?explore=dok` + Library→boot. 453 engine / 30 e2e. FP wall-tile routing deferred to B4.
      (2026-06-22)
- [x] **B2** DoK content: maps, undead **monsters**, items, ECL events on the Gen1 dialect. DoK
      monsters use a **216-byte** compact record (vs CoK 409) — reverse-engineered the offset map by
      correlating the 3 monsters shared with CoK (RED DRAGON/GHOUL/SIVAK), validated across all 64
      blocks (AC 0/64 out-of-range; canonical undead ACs exact: NIGHTMARE −4, LICH 0, WRAITH 4,
      WIGHT 5, SPECTRE 2, VAMPIRE STR 18). `monster.ts` now length-dispatches CoK/DoK layouts. ECL
      events already fire on the Gen1 dialect (confirmed); DoK GEO maps load (B1). DoK `ITEM0` is a
      different VOCAB-coded binary table (not 63-byte) — loader hardened to skip it, full decode
      deferred to B5. Web `bestiary()` seam. 455 engine / 30 e2e (DoK undead roster assertion).
      (2026-06-22)
- [x] **B3** **Character import** from a CoK save (the series feature). CoK writes each party member
      to `SAVE/CHRDATA{n}.SAV` as the **same 409-byte `.CCH` record** the monster loader decodes (FRUA
      `.cch` offsets are all zero — CoK uses the SSI layout). New engine `savedCharacter.ts` decodes
      the verified carry-over fields (name, abilities, HP, AC, THAC0, saves, XP); golden test reads the
      real 6-hero party (SIR STRONGSWORD/ISTAN HORBIN/GARIN…). Web `importParty.ts` builds a playable
      `CharacterSheet` per save — **combat stats byte-faithful**, class/level reconstructed from
      abilities+HP (CoK doesn't store them in a mapped position — documented gap). `?import=cok` route
      + sidebar entry imports the party then offers "Continue into Death Knights of Krynn"; the imported
      heroes become the active roster. 459 engine / 31 e2e (imports the 6-hero party, SIR keeps STR 18 /
      27 HP). (2026-06-22)
- [x] **B4** DoK **Amiga graphic set** + live EGA↔Amiga switch; DoK Amiga **multi-track SMUS**
      music (action/horror/lordsoth/sorrow/undead/victory) in the AudioManager. Three parts shipped:
      (1) **FP wall-tile routing** deferred from B1 — `loadDungeonWalls` detects DoK's single 256-tile
      8X8D bank (no per-record `blockId*10+i` blocks) and routes all 3 symbol sets through it, each
      relocated by its own slot offset (varA 0/0x46/0x8C), `lookupTile` resolving by global tileId.
      (2) **DoK EGA↔Amiga switch** — `gswitch.ts` parametrized (`?gswitch=dok`, DoK BIGPIC1.DAX/.DAA).
      (3) **DoK multi-track SMUS** — `AudioManager.registerAmigaTracks`/`setMusicTrack`; `gameAudio.setGame('dok')`
      loads all 7 tracks, scene→track (explore→krynn, combat→action), `playTrack()` for thematic cues;
      `?explore=dok` arms it. 460 engine / 33 e2e (+DoK BIGPIC switch, +DoK 7-track SMUS). Live, deployed.
- [x] **B5** DoK reuses Phase-A systems (combat/magic/items/menus/camp/town) — verify + fill gaps.
      Two deferred format gaps closed: (1) **DoK item table** — `ITEM0.DAX` is one block of **96 × 17-byte**
      records (the SSI/FRUA item record minus the trailing always-zero byte); reversed the field layout
      (base-type ptr@0, VOCAB name@1-3, encumbrance@4, value@6, magic bonus@8, flags, charges@14).
      New `decodeDokItemArchive`/`sniffDokItemRecord` (91 valid items; tail slots gated). Wired into the
      explore item DB (DoK names resolve via the base-type catalogue) + `window.__explore.items()` seam.
      (2) **CoK class code** — pinned at **offset 90** (1=Cleric validated by cleric spell-slots@79-81,
      2=Magic-User by mage saves); `dragonlanceClassForCode` + `SavedCharacter.classCode`. Import now uses
      it, fixing caster mislabels (ISTAN STR-18 mage was read as a fighter; KAL DEX-19 cleric as a thief).
      Level stays HP-derived (206/214 are small enums, not level — documented). 465 engine / 33 e2e
      (+DoK item DB assert, +ISTAN-is-mage assert). Phase-A systems are party/ruleset-driven and already
      pass on DoK content (B1 maps, B2 monsters, B5 items). Live, deployed.

## Phase C — The Dark Queen of Krynn (Gen2, the target engine)
- [x] **C1** DQK **detection profile** + boot; **HLIB GEO** map decode (Gen2 container). Engine:
      reverse-engineered the Gen2 GEO format — `GEO.GLB` is a flat HLIB **DATA** library (magic=1:
      member[0] = a 20-entry id table, members[1..20] = the maps). Each map is `byte0=W, byte1=H,
      6 header bytes, then 4 planes of W×H` — variable-sized (10×10 … 39×20), unlike Gen1's fixed
      16×16, but with the **identical nibble wall semantics** (plane0=(N<<4)|E, plane1=(S<<4)|W,
      plane2=backdrop, plane3=doors; East-edge symmetry 0.85–1.00 across all 20 maps). New
      `decodeGen2GeoBlock`/`decodeGen2GeoArchive`/`sniffGen2GeoBlock` (`geo.ts`, sharing a
      `buildGeoResult` helper with the Gen1 decoder → every CoK/DoK golden byte-identical). The DQK
      detection profile already existed in `BUILTIN_PROFILES`. Web: `apps/web/src/ui/dqkExplore.ts`
      fetches GEO.GLB, `decodeHlib`→`decodeGen2GeoArchive` → 20 walkable maps, with a map picker +
      automap + keyboard movement; `?dqk=1` route, sidebar entry, and the Library boots
      `dark-queen-of-krynn/DOS` straight into it. VGA wall art is C3 (this slice is the decoded,
      walkable grid). 470 engine / 34 e2e (golden against real GEO.GLB; `?dqk=1` boot+walk case).
- [x] **C2** **Gen2 ECL** dialect deltas (disassemble DQK `DATA` blocks; opcode drift vs Gen1).
      Reversed the DQK ECL dialect: `ECL.GLB` is an HLIB **DATA** library (magic=1, id table +47
      scripts) pairing scripts to GEO maps by the shared logical area id. **The ONLY delta is the
      block header** — Gen2 drops the Gen1 `0x1388` magic word, so the header is **20 bytes** (5 far
      pointers) not 22, and `vbase = 0x8000` not `0x7FFE`; the opcode table, operand framing and
      6-bit string packing are **identical** (proof: 0/47 members carry the magic, 446 inline strings
      decode to English, area inits fire `LOAD_FILES`, area 4 prints the real *"YOUR VOYAGE BEGINS
      PEACEFULLY…"* intro). Engine: `EclDialect` + `ECL_GEN1`/`ECL_GEN2` thread through
      `disassemble.ts`/`vm.ts`/`program.ts` (default Gen1 → every CoK/DoK golden byte-identical); new
      `loaders/eclGlb.ts` (`decodeEclGlb`) → `BlockSource` by area id; GEO loader now exposes each
      map's logical area id for GEO↔ECL pairing. Web: `dqkExplore.ts` runs the entered area's script
      through the Gen2 VM and shows the fired events (`LOAD_FILES` GEO + first decoded text). Docs:
      `docs/engine/research/ecl-gen2-dqk.md`. 475 engine / 34 e2e.
- [x] **C3** DQK **VGA 256-color** graphic set through the render pipeline (HLIB TILE + palette).
      The HLIB TILE decoder + per-leaf colour table + Chain-4 layout + `indexedToRGBA` were already
      verified; the gap was **palette compositing** (the open "multi-palette layering" question). A
      DQK leaf only owns a *slice* of the DAC (measured: `ALWAYS`=0–15, `GEN`/`FRAME`=16–31,
      `BIGPIC`=32–255), so a picture rendered with only its own leaf palette leaves the other slots
      black. New engine `mergePalettes(...)`/`definedColorCount` (`render/palette.ts`) +
      `compositeHlibPalette(archive)` (`hlib.ts`) layer leaves into one 256-colour table (last-wins,
      null never clobbers, cycles concat). The full overland DAC =
      `mergePalettes(composite(ALWAYS), composite(BIGPIC))` → 240 defined slots. Web `dqkExplore.ts`
      (`?dqk=1`) now shows a **VGA art panel**: the 3 BIGPIC overland pictures (304×120, method 18)
      painted through the composited DAC with a prev/next stepper — DQK's real 256-colour art running
      through the render pipeline in the browser. Docs: `docs/engine/research/vga-palette-compositing-dqk.md`.
      480 engine / 34 e2e.
- [x] **C4.1** DQK **per-cell ECL events** fire through the engine (Gen2-dialect `SearchLocation`).
      Reverse-engineered DQK's per-cell event dispatch: unlike Gen1 (which dispatches on the cell
      backdrop byte at `0x7F79`), DQK dispatches on the **party coordinates** at low data addresses —
      **CONFIRMED `0xBF`=party X, `0xC0`=party Y, `0x1B`=area** (the area-2 teleporter writes its X/Y
      inputs there, and a position sweep changes which event fires: area 30 → ZOMBIES at (0,0), CHILD
      at (5,5), DOGS at (3,12), nothing at (10,10)). Engine: `primeSearchLocation`/`dialogueForCell`
      are now dialect-aware (`opts.dialect`, default Gen1 → every CoK/DoK golden unchanged) with a
      generic `opts.seedMemory`; new `DQK_PARTY_X_ADDR`/`DQK_PARTY_Y_ADDR`/`DQK_AREA_ADDR` +
      `dqkPositionSeed`. Web `?dqk=1`: each step runs the area's Gen2 SearchLocation for the live cell
      and surfaces the real position-dependent event (room text, ambient, auto-resolved fights) on a
      per-cell event line — a DQK dungeon now plays through the engine, not just walks. Docs:
      `docs/engine/research/dqk-search-location.md`. 483 engine / 34 e2e.
- [x] **C4.2a** DQK **combat** routed into the shared interactive `launchCombat` overlay with
      **faithful decoded DQK monsters**, returning to the dungeon on dismiss. Decoded `MONCHA.GLB`
      (MON**sters**+**CHA**racters): an HLIB DATA library of creature records compressed with the
      **DQK run-length variant** (`≤128`-copy / `257−c`-repeat — one higher than DAX/DAA; hackdocs
      MONSTDAT.TXT) — new engine `monchaRLE`. Reversed the Gen2 record layout (fixed-width name at
      offset 24, type tag @2, THAC0 @55, saves @59, HD @65, damage @101/103/105, AC @107, all the
      `60−x` inversions) by correlating against canon — BLACK PUDDING (AC 6/HD 10/3d8/THAC0 10,
      XP 3000) and BORING BEETLE (AC 0/HD 5/5d4) decode exactly. Engine `decodeMonchaGlb`/
      `decodeDqkMonsterRecord`/`sniffDqkMonster` return the validated monster set (honest **5/98**
      today — see C4.2b). Web `?dqk=1`: `fireSearchLocation` now captures a queued combat roster
      (instead of an auto-resolve marker) and `launchDqkCombat` routes it — or a scripted fight vs
      the bestiary — into the shared tactical screen; on victory the treasure/return flow runs and
      the dungeon resumes. Docs: `docs/engine/research/moncha-dqk.md`. 489 engine / 34 e2e.
- [x] **C4.2b-1** Full DQK bestiary **names** — widen `MONCHA.GLB` from ~5 to **98/98 named**.
      **Shipped.** Reversing pass (RLE opcode traces) established the blocker is **not** a decompressor
      bug: `monchaRLE` is faithful, double-RLE is ruled out, and the records are genuinely
      **variable-length + self-describing** (name offset varies 21…300+, lengths 88…530). New engine
      `scanMonsterName` (offset-independent: longest uppercase-ASCII run past the header) decodes a
      name + category for every member; `decodeMonchaGlb` now returns `roster: MonchaEntry[]` (all 98)
      alongside `monsters` (the fixed-offset-aligned subset that still carries full validated stats).
      `?dqk=1` reports "98 of 98 named (N with full combat stats)". Recovered the whole bestiary —
      chromatic DRAGONs/HEADs, draconians, Thenol troops, named bosses (CAPTAIN DAENOR, GRUNSCHKA,
      ELGYNORA, DAWNSHINE). 493 engine / 34 e2e.
- [x] **C4.2b-2a** Full DQK monster **stats** via inline-stat-block reconstruction — **shipped**.
      Cracked the framing: the monster record's stat block is **`monchaRLE`-compressed inline**; "clean"
      records got it expanded by the member-level pass, the rest carried it as a literal run. New engine
      `reconstructDqkRecord` re-expands the post-name tail (splice header+name + 2nd `monchaRLE` pass) →
      the exact canonical layout; `findAbilityBlock` locates the 12-byte ability block (`[1,25]×12`
      then `00 00 00`); stats read at fixed deltas from it (THAC0 +15, saves +19, HD +25, dmg
      +61/+63/+65, AC +67). Full-stat **type-8 decode 5 → 20**, all validated + canon-cross-checked
      (IRON GOLEM AC 3 / HD 18 / 4d10 / XP 14550, dragons negative AC, BLACK PUDDING/ETTIN unchanged;
      XP at the absolute offset confirms the framing). 496 engine / 34 e2e.
- [x] **C4.2b-2b-1** DQK non-type-8 record **framing split** — **shipped (2026-06-22).** The 78
      non-type-8 records are two combat framings, not one alien layout. **Monster-framed** (type 0/3/12
      with a monster-default ability block — every score `0x0a`, undead a few `0x03`) decode to canon via
      the same ability-anchored deltas the type-8 monsters use — **Sahuagin AC 5/1d2, Spectre AC 2/1d8,
      Wraith AC 4/1d6, Fire Giant AC 3/5d6, Skeleton Warrior AC 2/1d12+3** — lifting full-stat decode
      **20 → 27**. **Character/NPC-framed** (Black Ogre, Dark Wizard, named bosses) carry *rolled* ability
      scores + placeholder combat fields and are honestly categorized `npc`, name + category only. Exact
      discriminator: ≥4 of 6 current ability scores == 10 and none > 10. Engine `isMonsterDefaultAbility`
      / `classifyCategory` (`loaders/moncha.ts`); validated records report `category: monster` regardless
      of type byte. +1 engine test (canon AC/damage for the new creatures + NPC categorization), e2e
      `bestiaryCategories` seam. 526 engine / 34 e2e. research/moncha-dqk.md.
- [x] **C4.2b-2b-2a** DQK **character/NPC class-based stat decode** — **shipped**. Character-framed
      MONCHA records (rolled ability spreads) decode to real combat stats via `decodeNpcRecord`
      (`loaders/moncha.ts`): thac0 `60−rec[A+15]`, stored hp `rec[A+17]`, saves `A+19`, level
      `rec[A+25]`, ac `60−rec[A+67]`, class `rec[A+71]` (NPC_DELTA). Routed by `isRolledAbility`
      (some score >10) AND `!isMonsterDefaultAbility` (extended to reject the single-INT-18
      intelligent-monster pattern → removes UVWW. false-positive; >10 signature removes ZOMBIE
      false-positive). decoded 27→35 (+8 NPCs: BLACK OGUE/THENOL WIARD/DRK WIZARD/BAKAI SHAMAN/
      SHARMAN/GNOME TINKE/SELIA/TASLEHOFF). BAKAI SHAMAN class8·L9·THAC0 14 = exact AD&D 1e cleric.
      Skipped a researcher's "INT-18 dragon" fix (probed: 5 chromatic dragons already decode via the
      type-8 special-case — non-problem). `MonsterRecord` +`npcClass`/`npcLevel`; `decodeNpcRecord`
      exported. +2 engine tests, e2e `monchaDecoded≥35` + statNames BAKAI SHAMAN/SHARMAN +
      `bestiaryCats.npc≥8`. 528 engine / 34 e2e. research/moncha-character-c4.2b-2b-2.md.
- [x] **C4.2b-2b-2b** Short-tail type-8 monsters (HYDRA + UMBERHULK) — **shipped**. The two
      fully-traced type-8 records whose standard ability-anchored stat block fails now decode; MONCHA
      full-stat decode **35 → 37**. **HYDRA** = compact / short-tail layout: multi-attack monsters pack
      attack/damage/AC 21 bytes early at `COMPACT_DELTA` (A+42 attack count=8 heads, A+43 ndice, A+44
      dsize=1d12/head, A+45 bonus, A+46 ac_raw); THAC0/HD/saves/XP stay standard → **AC 5 / THAC0 7 /
      HD 16 / 1d12**, exact MM, all from data. New `sniffCompactMonster` (AC∈[-12,16] + HD≥1 gate
      rejects the dragon "HEAD" sub-records). **UMBERHULK** = its raw AC byte `0x3a` equals a valid
      `monchaRLE` COPY opcode → consumed during compression, absent from disk; THAC0 12 / HD 10 / 2d10
      decode from data, AC injected from canon (`CANON_AC` AC 2) and flagged **`acSource:'canon'`** for
      audit honesty (gated on `sniffAtAbilityExceptAc` + name in table). Also: damage flat bonus now
      read **signed** (0xfd = −3, not +253). `MonsterRecord` +`acSource?:'canon'`. +1 engine golden,
      e2e `monchaDecoded≥37` + statNames HYDRA/UMBERHULK + HEAD/GORGON-stay-out. 529 engine / 34 e2e.
      research/moncha-shorttail-c4.2b-2b-2a.md.
- [x] **C4.2b-2b-2b-ii** DQK MONCHA **dec-path** records — **shipped**. A probe found the root cause for
      a chunk of the failing records: they carry their ability/stat block **already expanded** in the
      decompressed member, so `reconstructDqkRecord`'s second `monchaRLE` pass *re-compresses* it to
      garbage and the recon decode fails. New **dec-path** fallback in `decodeFullStatMonster` anchors
      `findAbilityBlock`/`sniffAtAbility` on the non-reconstructed `dec` bytes (gated identically to the
      recon path → character/NPC records stay out). **decoded 37 → 46** (monster-framed 29 → 38).
      Hand-verified GIANT ANEMONE (AC2/HD16/1d4) + GIANT SQUID (AC3/HD12/1d6); the same gate also
      recovered 7 more monster-framed records (FIRESHADOW, 2 HEADED TROLL, GHAST, WYNDLASS, SIVAK
      DRACONIAN, ENCHANTED KAPAK…). **GHAST AC 4 / HD 4 = exact AD&D canon match** (proves real stat
      blocks, not in-range noise). +1 engine golden, e2e `monchaDecoded≥46` + GIANT SQUID/GHAST.
      531 engine / 34 e2e. research/moncha-decpath-c4.2b-2b-2b-ii.md.
- **C4.2b-2b-2c** Reverse the **combat trigger** — SPLIT (research/combat-trigger-c4.2b-2b-2b.md
      §9 names three engine gaps: vmRun1 never runs per step, PARTYSTRENGTH no-op, `[0x0097]` cooldown).
  - [x] **C4.2b-2b-2c-i** Wire the **vmRun1 per-step main loop** into the walk. ✅ Engine `primeVmRun1` /
      `dialogueForVmRun1` (header slot 0, Gen2) added — a shared `primeHandler` now backs both
      SearchLocation and vmRun1 with identical seed/lifecycle semantics. Host `fireSearchLocation` runs
      vmRun1 **before** SearchLocation each step on one persisted `areaMemory` (so `[0x0097]` cooldown
      survives), folding both into the event line; `vmRun1.roster ?? searchLoc.roster` picks the fight.
      Drain loop `choose(0)`s the `awaiting-input` paths instead of blocking. Probe-confirmed areas
      5/18/22/23/30/66 expose an in-range vmRun1 (matches research §5/§6: 66@0x8592, 18@0x833e, 5@0x8429)
      and run to a terminal status with no spurious combat under baseline seeding. +4 engine golden
      (synthetic Gen2 LOAD_MONSTER→COMBAT + real-ECL.GLB terminal-status sweep), e2e `vmRun1At` on
      areas 5/18/66. 535 engine / 34 e2e. Cell-descriptor/ticker seeding → -iii.
  - [x] **C4.2b-2b-2c-ii** Implement **PARTYSTRENGTH (0x1d)**. ✅ Probing the real DQK ECL (area 23
      etc.) **refuted** the "auto-fills the roster" hypothesis: 0x1d is consistently *followed by*
      `IF_GE/IF_EQ` (e.g. `PARTYSTRENGTH c18:128 ; IF_GE`), so it is a **strength *gate*, like COMPARE**,
      not a queue op. `vm.ts` now sets `cmp = {left: hostPartyStrength, right: operand}` + emits a
      `partyStrength` effect (was a no-op falling to `unhandled`), so the following `IF_*` is well-defined
      instead of reading stale cmp. Strength formula/units/gate-direction are **unrecoverable from disk**
      → host supplies the metric via new `EclVmOptions.partyStrength` (default **0** = conservatively
      *fails* `IF_GE` gates, deterministic, no spurious combat: e2e `vmRun1At` 5/18/66 still roster=0).
      Roster population stays LOAD_MONSTER's job; monster-0 fallback unchanged. 540 engine / 34 e2e.
  - [ ] **C4.2b-2b-2c-iii** Persist/decrement the encounter **cooldown `[0x0097]`** + seed the GEO
      cell-descriptor `[0x002d]` / facing `[0x0011]` from the live explorer pose so vmRun1's
      `COMPARE_AND`-gated fight subroutines actually reach a real combat cell.
- [ ] **C4.2b-2b-2b-iii** (low priority — likely **unrecoverable from disk**) The genuinely-blocked
      remainder. **SEA SNAKE** (ability @35 in dec gives valid THAC0/HD/AC but **damage = 0** at the std
      delta — damage location unknown); **HUGE CROCODILE** (the 12-byte run @39 is a **false** ability
      block, HD reads 248); **EYE OF THE DEP** / **GORGON** (no clean 12-byte [1,25]+`00 00 00` ability
      run in dec or recon — block fragmented); **SEA DRAGON** (ability @40 found, THAC0 reads −2 — a
      different THAC0 delta or compact variant); the **5 dragon "HEAD" sub-records** (BLACK/BLUE/GREEN/
      RED/WHITE HEAD — false block, HD 0; likely breath/head sub-entries, not standalone combatants);
      **THENOL FNATIC** / **PRINCE ALHOOK** (run followed by `254` not `00` → character-framed NPCs, not
      monsters). Needs a fragmented-block heuristic + a damage-location trace; confirm whether the HEAD
      records are real monsters. Pure byte-level RE (research/moncha-decpath-c4.2b-2b-2b-ii.md §"root
      cause 2").
- [x] **C4.3** DQK on the shared Phase-A **systems** (magic/items/town/character sheet) driven by a
      DQK party — **shipped**. New `apps/web/src/ui/dqkParty.ts` installs the **Heroes of the Lance**
      (STURM knight6 / CARAMON fighter7 / TANIS halfElf fighter6 / GOLDMOON cleric6·mishakal /
      RAISTLIN magicUser6 / TASSLEHOFF kender thief7), each built + validated by the engine
      `createCharacter` ruleset (original-authored, not save-lifted). `?dqk=1` now boots that party
      (install-if-empty) and adds a **(V)iew/(M)agic/(I)tems/(T)own** command bar + hotkeys that open
      the same `openCharSheet`/`openMemorize`/`openInventory`/`startTown` overlays CoK/DoK use — they
      were already party+ruleset-driven, so this was the install-roster + wire-openers slice. CoK EGA
      chrome (8X8D1.DAX block 202 + font) reused for the overlay frame. e2e proves the 6-hero install,
      each overlay opening (sheet/magic/items) + town roster showing the DQK party.
- [x] **C5a** DQK **audio containers** — decode the Miles **XMIDI** music + the **AdLib timbre
      bank** (engine, pure). `ADDQ1..3.XMI` are EA-IFF `FORM XDIR`+`CAT XMID` trees; reversed the
      XMIDI event stream (summed-byte delay; note-on with an explicit VLQ duration, no note-off;
      `0xFF` meta tempo/timesig/end). `INSTR.AD` is a Miles AdLib bank: `{u8 patch,u8 bank,u32
      offset}` table → `0xFFFF` term → 199 × 14-byte OPL2 records (`u16` len + 12 operator bytes).
      New engine `decodeXmi`/`sniffXmi` (`audio/xmi.ts`) + `decodeInstrAd`/`sniffInstrAd`/`adLibKey`
      (`audio/instrAd.ts`); golden against the real files (`ADDQ1` 1853 note-ons / 120 BPM, `ADDQ2`
      6-song catalogue, `INSTR.AD` 199 instruments banks 0+100). `?dqk=1` reports the decoded music +
      `window.__dqk.audio*` seams. C5 split → **C5b** (OPL2 synthesis + VOC/`DIG4` SFX + graphics-mode
      binding). Docs: research/audio-dqk.md. 504 engine / 34 e2e. (2026-06-22)
- [x] **C5b-1** DQK **digitized SFX** — decode `SFXDQ.VOC` (Creative Voice File) → the 13 named DQK
      effects and bind them to the graphics-mode switch. Reversed the container: marker blocks 0..12 =
      cast/flame/sorcery/die/sling/hit/lightning/swing/walk/fireball/bow/sploosh/crackle (hackdocs
      UASOUND.TXT), each a type-1 sound at 7407/11111/5556 Hz, **codec 1 = Creative 4-bit ADPCM**. The
      ADPCM model matters: ffmpeg's `ADPCM_SBPRO_4` railed every effect negative; the **hardware-accurate
      SB `scaleMap`/`adjustMap` decoder** (DOSBox) gives DC-centred symmetric audio. New engine
      `decodeVoc`/`sniffVoc`/`DQK_SFX_NAMES` (`audio/voc.ts`) → normalized `AudioClip`s. Web:
      `AudioManager.registerDigitizedSfx` + `gameAudio` gains a **`'dqk'`** game that registers them as
      the **DOS-VGA** platform's SFX and maps action cues (melee→hit, arrow→bow, cast→cast,
      fireball→fireball, death→die, door→sploosh, miss→swing) onto them — DQK fights play DQK's own
      sounds; music stays honestly unarmed. `?dqk=1` reports the SFX; `window.__dqk.sfx*`/`gameSfxNames`/
      `playSfx` seams. C5b split → **C5b-2** (OPL2 music synthesis + SOUNDS.GLB DIG4). Docs:
      research/audio-dqk.md. 508 engine / 34 e2e. (2026-06-22)
- [x] **C5b-2a** DQK **OPL2 voice + operator-map pin** — pinned the 12-byte INSTR.AD operator field map
      as **interleaved `(mod,car)` register pairs** (`0x20/0x60/0xE0/0x40/0x80` per op + `0xC0` fb/conn;
      leading pad byte) — the only layout where carriers are full-volume (TL=0), every note attacks
      (AR≥1), waveforms are valid (0–3), the drum (117) is additive + fast-decaying and the clarinet (51)
      a half-sine modulator. Wrote a pure-TS OPL2 2-op voice (`audio/opl2.ts`: 4 waveforms, ADSR env, FM/
      additive, feedback) + `parseAdLibPatch`/`renderOplNote`/`pcmRms`/`dominantHz`; **proven offline** —
      real timbres render voiced PCM whose pitch tracks the note (synthetic sine-FM locks 440 Hz ±30; all
      18 documented patches voiced + in-band). `?dqk=1` readout renders patch 51 live; `__dqk.previewOplNote`
      seam. Map **pinned-by-synthesis**. 515 engine / 34 e2e. Docs: research/audio-dqk.md. (2026-06-22)
- [x] **C5b-2b-1** DQK **OPL2 full-song mixdown** — built a **9-channel OPL2 mixer** (`audio/oplPlayer.ts`:
      `renderXmiSong`) that drives the pinned voice from a decoded XMI event stream to one PCM mixdown:
      tempo-map tick→seconds (PPQN 60), 9-voice allocation with soonest-ends-first **voice stealing**,
      per-channel program/CC#7-volume tracking, **percussion routing** (MIDI ch 9 → note-in-bank-100, the
      `INSTR.AD` side of XMIDI's 127↔100 map), and a real **attenuation-domain EG** + `fnum`/`block`-quantized
      pitch + KSL (replacing 2a's amplitude approximation). **Proven offline:** ADDQ1 → 63.0 s, 1853 notes
      (490 percussion), peak polyphony 9/9 (146 stolen), voiced 77% across the whole timeline; length matches
      ticks×tempo. `?dqk=1` renders the song live; `__dqk.previewDqkSong` seam. 518 engine / 34 e2e.
      research/audio-dqk.md. (2026-06-22)
- [x] **C5b-2b-2a** DQK **OPL2 music → WebAudio, graphics-mode-bound + XMIDI loops** — added a looping
      **PCM music track** to `AudioManager` (`setPcmMusic` → `pcmMusic`/`pcmMusicBuffer`, played as a
      `loop=true` buffer that takes precedence over SMUS); `gameAudio` `'dqk'` now renders the first ADDQ
      song to a mixdown (`renderXmiSong` @22050) and arms it **on DOS-VGA alongside the 13 SFX**, so a
      graphics-mode switch to DOS DQK plays music + SFX together. Engine: `expandXmiLoops` unrolls the XMIDI
      for-loop controllers (**116** for-loop / **117** next-break, infinite = bounded) into a flat
      absolute-tick timeline (drives `renderXmiSong`; whole clip marked as the loop body). 520 engine / 34
      e2e. research/audio-dqk.md. (2026-06-22)
- [x] **C5b-2b-2b** DQK **`SOUNDS.GLB` `DIG4` digitized SFX** — **solved + shipped (2026-06-22).** The
      `1c fa 00 00`/`2b 77 00 00`/`15 bc 00 00` record header is a **big-endian `u16` sample rate** (`+ u16 0`):
      7418 / 11127 / 5564 Hz — 1:1 with the VOC rates (7407/11111/5556). Payload is **raw 8-bit *unsigned* PCM**
      (centre 0x80), **not** ADPCM (the IMA theory gave full-scale noise and was rejected). Member order matches
      `DQK_SFX_NAMES`: cross-correlating each decoded member's waveform shape against the verified `SFXDQ.VOC`
      bank confirms the same 13 effects (lightning r≈0.97, flame 0.83, walk 0.82, die 0.81, crackle 0.79), and
      the rate buckets land exactly where the VOC predicts (high-rate = die #3 & lightning #6, low-rate = fireball
      #9). New engine decoder `audio/dig.ts` (`decodeSoundsGlb`/`decodeDigMember`/`sniffDig`) + 5 tests; gameAudio
      now prefers the in-engine DIG4 bank (VOC fallback); `previewDigSfx` seam + e2e. 525 engine / 34 e2e.
      research/audio-dqk.md. *(Optional refinements — chunked/progressive WebAudio render + cycle-exact dbopl EG
      counter — deferred; the one-shot mixdown + 8-bit SFX are sufficient.)*

## Phase D — Companion panels & cross-cutting (M4)
- [ ] **D1** Automap panel (engine-tracked visited tiles + JSON notes) — partially present in explore.
- [ ] **D2** HUD panel (party HP/XP/effects, always visible).
- [ ] **D3** Character/Save editor panel (+ reader for an original CoK save).
- [ ] **D4** ECL inspector panel (live `step()` disassembly + flags).
- [ ] **D5** Reference DBs (monster/item/spell browser from decoded tables + ruleset).

## Phase E — The PRIMARY native goal (DOS executables, after the decompile matures)
- [ ] **E1** HLIB **TILE/DATA writer** (repacker) — write the DQK container format.
- [ ] **E2** Amiga art → DQK HLIB `TILE` converter (planar→chunky, 32→256 color).
- [ ] **E3** Port CoK/DoK content (GEO/MON/ITEM/ECL) onto the DQK engine; handle ECL opcode drift.
- [ ] **E4** Build + verify the two DOS executables (CoK, DoK on the DQK engine) under DOSBox.

---

## Done (newest first)
*(loop appends one line per completed item: `- [x] A1.1 — <result> (YYYY-MM-DD)`) *
- [x] C4.2b-2b-1 — DQK non-type-8 record framing split (2026-06-22). The 78 non-type-8 MONCHA records are
  two combat framings: monster-framed (monster-default ability block, all 0x0a) decode to canon stats via
  the type-8 ability-anchored deltas (Sahuagin AC5/1d2, Spectre AC2/1d8, Wraith AC4/1d6, Fire Giant AC3/5d6,
  Skeleton Warrior AC2/1d12+3) — full-stat decode 20→27; character/NPC-framed (rolled abilities + placeholder
  combat fields: Black Ogre, Dark Wizard, bosses) honestly categorized 'npc', name-only. Discriminator: ≥4 of
  6 ability scores ==10, none >10. Engine isMonsterDefaultAbility/classifyCategory; validated records report
  category 'monster' regardless of type byte. 526 engine / 34 e2e. Live at /greybox/.
- [x] C5b-2b-2b — DQK SOUNDS.GLB DIG4 digitized SFX decoded + shipped (2026-06-22). Reversed the member record:
  a 4-byte header = **big-endian u16 sample rate** (7418/11127/5564 Hz, 1:1 with the VOC's 7407/11111/5556) +
  u16 0, then **raw 8-bit unsigned PCM** (centre 0x80) — NOT ADPCM (the IMA theory produced full-scale noise and
  was rejected). Member order = `DQK_SFX_NAMES`: each decoded member's waveform shape cross-correlates with the
  same-id `SFXDQ.VOC` effect (lightning 0.97, flame 0.83, walk 0.82, die 0.81, crackle 0.79) and the rate buckets
  land where the VOC predicts (high-rate = die#3/lightning#6, low-rate = fireball#9). New engine `audio/dig.ts`
  (`decodeSoundsGlb`/`decodeDigMember`/`sniffDig`, 5 tests); gameAudio prefers the in-engine DIG4 bank with VOC
  fallback; `previewDigSfx` seam + e2e bucket/centring assertions. 525 engine / 34 e2e. Live at /greybox/.
- [x] C5b-2b-2a — DQK OPL2 music → WebAudio, graphics-mode-bound, + XMIDI loop unrolling (2026-06-22). Added a
  looping PCM music track to AudioManager (`setPcmMusic` attaches a pre-rendered mixdown to a platform;
  `playMusic` plays it as a `loop=true` AudioBufferSourceNode that takes precedence over the SMUS square-wave
  path; `musicUnavailableReason` counts it). gameAudio's 'dqk' branch now renders the first ADDQ song
  (`renderXmiSong` @22050) and arms it on the **DOS-VGA** platform alongside the 13 SFXDQ.VOC effects, so a
  graphics-mode switch to DOS DQK plays its OPL2 music + digitized SFX together — exactly how CoK/DoK Amiga
  SMUS is platform-bound. Engine `expandXmiLoops` unrolls the XMIDI for-loop controllers (116 for-loop start /
  117 next-break end; nested via a stack; infinite count 0 bounded by maxIterations) into a flat
  absolute-tick timeline that drives renderXmiSong, and the whole mixdown is marked as the loop body
  (repeatSamples = length) so WebAudio loops it seamlessly. The ADDQ songs use only CC#114 (no 116-119), so a
  synthetic 116/117 song unit-tests the unrolling (3× body + correct tick shift; infinite capped). e2e: after
  ?dqk=1 boot, gameAudio.musicArmed is true on DOS-VGA with the 13 SFX, and enable/disable is idempotent.
  C5b-2b-2 split → C5b-2b-2b (SOUNDS.GLB DIG4 + optional chunked streaming / cycle-exact EG). 520 engine / 34 e2e.
- [x] C5b-2b-1 — DQK OPL2 full-song mixdown via a 9-channel mixer (2026-06-22). New `audio/oplPlayer.ts`
  (`renderXmiSong`) turns a decoded XMI song + the INSTR.AD bank into one PCM mixdown: a tempo map converts
  ticks→seconds (PPQN 60, default 120 BPM); a 9-voice allocator assigns notes in start order and **steals**
  the soonest-ending slot when all 9 are busy (cutting the stolen note's tail) so polyphony never exceeds the
  OPL2's 9 channels; per-channel program + CC#7 volume are tracked; **percussion** (MIDI ch 9) resolves its
  timbre by note number in **bank 100** (the INSTR.AD side of XMIDI's bank-127↔100 map — TIMB marks those
  bank 127); each voice runs the pinned 2-op synth with a real **attenuation-domain EG** (9-bit-style dB
  envelope, KSR-scaled rates), `fnum`/`block`-quantized OPL pitch and KSL (replacing C5b-2a's amplitude
  approximation). Proven offline without a reference WAV: ADDQ1 → 63.0 s / 1853 notes (490 on the percussion
  track) / peak polyphony 9 (146 stolen) / voiced 77% across the whole timeline, and the length matches
  ticks×tempo; ADDQ2 maxPoly 8 / voiced 93%; ADDQ3 52 s. `?dqk=1` renders the first song live in the readout;
  `__dqk.previewDqkSong` seam; e2e asserts a multi-second song, voiced >50% across the timeline, polyphony
  ≤9, percussion present. C5b-2b split → C5b-2b-2 (WebAudio streaming + loop controllers + graphics-mode
  binding + SOUNDS.GLB DIG4). 518 engine / 34 e2e.
- [x] C5b-2a — DQK OPL2 voice + the INSTR.AD operator-map pin (2026-06-22). Pinned the 12-byte operator
  layout as **interleaved (mod,car) register pairs** — pad byte, then (0x20,0x60,0xE0,0x40,0x80) per
  operator + 0xC0 fb/conn — by column analysis over the real bank: it's the *only* order where carriers
  are full-volume (TL=0), every carrier attacks (AR≥1), both 0xE0 columns stay 0–3 (valid waveforms), the
  drum (patch 117) is additive with fast carrier decay and the clarinet (51) a half-sine modulator. SBI
  order is ruled out (it puts carrier attack on a mostly-zero column → silent notes). New pure-TS OPL2
  2-op voice (audio/opl2.ts): the 4 OPL2 waveforms, an exponential ADSR envelope, FM/additive routing,
  modulator feedback; + parseAdLibPatch / renderOplNote / pcmRms / dominantHz. **Proven offline without a
  reference WAV**: a synthetic sine-FM patch locks 440 Hz ±30; all 18 documented timbres render voiced PCM
  (rms>0.01) at a detectable in-band pitch; 117 reads additive. ?dqk=1 renders patch 51 (clarinet) live in
  the readout; __dqk.previewOplNote seam; e2e asserts voiced+pitched+additive-drum. Map pinned-BY-synthesis.
  C5b-2 split → C5b-2b (9-channel full-song playback + exact OPL2 envelope tables + XMI driver + DIG4).
  515 engine / 34 e2e.
- [x] C5b-1 — DQK digitized SFX: SFXDQ.VOC → 13 named effects, platform-bound. Reversed the Creative
  Voice File: marker blocks 0..12 (cast/flame/sorcery/die/sling/hit/lightning/swing/walk/fireball/bow/
  sploosh/crackle per hackdocs UASOUND.TXT), each a type-1 sound at 7407/11111/5556 Hz, codec 1 = Creative
  4-bit ADPCM. Key fix: ffmpeg's ADPCM_SBPRO_4 model railed every effect negative (min −1/max +0.1); the
  hardware-accurate SB scaleMap/adjustMap decoder (DOSBox) gives DC-centred symmetric audio (|mean|<0.01).
  New engine decodeVoc/sniffVoc/DQK_SFX_NAMES (audio/voc.ts) → normalized AudioClips (codec 0 PCM also
  handled). Web AudioManager.registerDigitizedSfx + gameAudio gains a 'dqk' game registering them as the
  DOS-VGA platform's SFX with action cues mapped (melee→hit/arrow→bow/cast→cast/fireball→fireball/death→die/
  door→sploosh/miss→swing) — DQK fights play DQK's own sounds; music honestly unarmed (OPL2 = C5b-2).
  ?dqk=1 reports the SFX; window.__dqk.sfx*/gameSfxNames/playSfx seams. C5b split → C5b-2 (OPL2 music +
  SOUNDS.GLB DIG4). Docs: research/audio-dqk.md. 508 engine / 34 e2e. (2026-06-22)
- [x] C5a — DQK audio containers: Miles XMIDI music + AdLib timbre bank decode. `ADDQ1..3.XMI` are
  EA-IFF `FORM XDIR`+`CAT XMID` trees; reversed the XMIDI event stream (delay = summed bytes <0x80;
  note-on carries an explicit VLQ duration, no note-off; `0xFF` meta tempo/timesig/end). `INSTR.AD` =
  Miles AdLib bank: `{u8 patch,u8 bank,u32 offset}` table → `0xFFFF` terminator → 199 × 14-byte OPL2
  records (`u16` length + 12 operator bytes), banks 0 (GM melodic) + 100 (percussion). New engine
  `decodeXmi`/`sniffXmi` (`audio/xmi.ts`) + `decodeInstrAd`/`sniffInstrAd`/`adLibKey` (`audio/instrAd.ts`);
  golden vs the real files (ADDQ1 1853 note-ons/120 BPM, ADDQ2 6-song catalogue, INSTR.AD 199 instruments).
  `?dqk=1` reports the decoded music; `window.__dqk.audio*` seams. Decision: C4.2b-2b's remaining MONCHA
  reversing is genuinely blocked (compact/variant records past the re-expanded end; no `.LST` field spec) →
  took the C5 fallback. C5 split → C5b (OPL2 synthesis + VOC/DIG4 SFX + graphics-mode binding). Docs:
  research/audio-dqk.md. 504 engine / 34 e2e. (2026-06-22)
- [x] C4.2b-2a — Full DQK monster stats via inline-stat-block reconstruction (type-8 5→20). Cracked
  the post-name framing: the stat block is monchaRLE-compressed inline (clean records got it expanded
  at member level; the rest carry it as a literal run). New engine `reconstructDqkRecord` re-expands
  the post-name tail to the canonical layout; `findAbilityBlock` locates the 12-byte ability block;
  stats read at fixed deltas from it (THAC0 +15, saves +19, HD +25, dmg +61/63/65, AC +67). 20 type-8
  monsters now carry validated full stats, canon-cross-checked (IRON GOLEM AC3/HD18/4d10/XP14550,
  dragons negative AC, BLACK PUDDING/ETTIN unchanged; XP at the absolute offset confirms framing).
  C4.2b-2 split → C4.2b-2b (non-type-8 records + short-tail type-8 + combat trigger). Docs:
  research/moncha-dqk.md. 496 engine / 34 e2e. (2026-06-22)
- [x] C4.2b-1 — Full DQK bestiary names (MONCHA.GLB 5→98/98 named). A reversing pass (RLE opcode
  traces of clean vs short members) proved `monchaRLE` is faithful, ruled out double-RLE, and showed
  the records are genuinely variable-length + self-describing (name offset 21…300+, lengths 88…530) —
  so the "5/98" cap was a fixed-offset assumption, not a decode bug. New engine `scanMonsterName`
  (offset-independent longest-uppercase-run scan) + `decodeMonchaGlb` now returns `roster:
  MonchaEntry[]` (all 98, name + record-type category + `hasStats`) alongside `monsters` (the
  fixed-offset subset that keeps full validated stats). `?dqk=1` reports "98 of 98 named (N with full
  combat stats)"; recovered the whole bestiary incl. named bosses. C4.2b split → C4.2b-2 (full stats +
  combat trigger). Docs: research/moncha-dqk.md. 493 engine / 34 e2e. (2026-06-22)
- [x] C4.3 — DQK on the shared Phase-A systems driven by a DQK party. New `apps/web/src/ui/dqkParty.ts`
  installs the **Heroes of the Lance** (STURM/CARAMON/TANIS/GOLDMOON/RAISTLIN/TASSLEHOFF), each built +
  validated by the engine `createCharacter` ruleset (original-authored, not save-lifted; THAC0/AC/spell
  slots all ruleset-derived). `?dqk=1` boots that party (install-if-empty) and adds a (V)iew/(M)agic/
  (I)tems/(T)own command bar + hotkeys opening the **same** `openCharSheet`/`openMemorize`/
  `openInventory`/`startTown` overlays CoK/DoK use — they were already party+ruleset-driven, so the slice
  was install-roster + wire-openers (CoK EGA chrome 8X8D1.DAX#202 + font reused for the frame). e2e proves
  the 6-hero install, each overlay opening, and the town roster showing the DQK party. 489 engine / 34 e2e.
  (2026-06-22)
- [x] C4.2a — DQK combat on the shared interactive screen with faithful decoded monsters. Decoded
  `MONCHA.GLB` (HLIB DATA library) — reversed the DQK run-length variant (`≤128`-copy / `257−c`-repeat,
  one higher than DAX/DAA; new engine `monchaRLE`) and the Gen2 monster record layout (name@24,
  type@2, THAC0@55, saves@59, HD@65, dmg@101/103/105, AC@107, all `60−x` inverted), verified exactly
  against canon (BLACK PUDDING AC6/HD10/3d8/XP3000, BORING BEETLE AC0/HD5/5d4). Engine
  `decodeMonchaGlb`/`decodeDqkMonsterRecord`/`sniffDqkMonster` return the validated set (honest 5/98 —
  widen in C4.2b). Web `?dqk=1`: `fireSearchLocation` captures a queued roster and `launchDqkCombat`
  routes a DQK fight into the shared `launchCombat` overlay (faithful enemies, treasure/return flow),
  back to the dungeon on dismiss. C4.2 split → C4.2b (full-bestiary decode + the chained/`vmRun1`
  combat trigger). Docs: research/moncha-dqk.md. 489 engine / 34 e2e. (2026-06-22)
- [x] C4.1 — DQK per-cell ECL events fire through the engine (Gen2 SearchLocation). Reverse-engineered
  DQK's coordinate-based per-cell dispatch (vs Gen1's backdrop-byte ON_GOTO): CONFIRMED party X=0xBF,
  Y=0xC0, area=0x1B by the area-2 teleporter inputs + a position sweep (area 30 fires ZOMBIES/CHILD/
  DOGS/nothing at different cells). `primeSearchLocation`/`dialogueForCell` made dialect-aware
  (default Gen1 → CoK/DoK goldens unchanged) + generic `seedMemory`; new `dqkPositionSeed` +
  `DQK_PARTY_{X,Y}_ADDR`/`DQK_AREA_ADDR`. Web `?dqk=1` fires the live cell's SearchLocation each step
  and shows the real position-dependent event on a per-cell line. C4 split: C4.2 (DQK combat/MONCHA),
  C4.3 (shared systems on a DQK party) deferred. Docs: research/dqk-search-location.md. 483 engine /
  34 e2e. (2026-06-22)
- [x] C3 — DQK VGA 256-colour art through the render pipeline. The HLIB TILE decode/render path was
  already verified; the gap was palette compositing (the doc's open "multi-palette layering"). A DQK
  leaf only owns a DAC slice (measured ALWAYS=0–15, GEN/FRAME=16–31, BIGPIC=32–255), so a picture
  drawn with its own leaf palette leaves other slots black. New `mergePalettes`/`definedColorCount`
  (render/palette.ts) + `compositeHlibPalette` (hlib.ts) layer leaves into one 256-colour table
  (last-wins, null-safe, cycles concat). Web `?dqk=1` now shows a VGA art panel — the 3 BIGPIC
  overland pictures (304×120, method 18) painted through `mergePalettes(composite(ALWAYS),
  composite(BIGPIC))` = 240-slot DAC, with a prev/next stepper. Engine golden asserts the slot ranges
  + >50 distinct rendered colours; e2e reads the painted canvas. Docs:
  research/vga-palette-compositing-dqk.md. 480 engine / 34 e2e. (2026-06-22)
- [x] B1 — Death Knights of Krynn detection + boot (opens Phase B). DoK is a Krynn-Gen1 DAX sibling,
  so it runs the SAME `loadDungeon` path as CoK — its real bytes exposed three Gen1-format deltas the
  CoK-only loaders had baked in as constants, all now fixed CoK-preservingly: (1) `decodeGeoBlock`
  keys off the 1026-byte length, not the CoK 1024 size-word (DoK stores a different area/checksum word
  — new `hasCokGeoHeader` helper); (2) the WALLDEF `Pieces` 0x7F sentinel ("default/blank wall set")
  is honoured on ANY symbol set, not just set 1 (DoK GEO 34 = `[1,0xFF,0x7F]`), leaving the set empty
  when block 0 is absent; (3) the 8X8D sniff/decoder accept a 256-tile wall bank whose uint8 count
  saturates at 0xFF (DoK 8X8D1 block 1). New `dungeon-dok.test.ts` golden pins the load path on real
  DoK bytes (3 tests). Web: new `apps/web/src/ui/gameData.ts` per-game registry (literal DAX paths so
  the bundler ships them + ECL init block), `startExplore(gameId)` parameterised (CoK default),
  `?explore=dok` route + sidebar entry, and the Library now boots any detected playable Gen1 DOS game
  (CoK/DoK) straight into its dungeon. 453 engine / 30 e2e (new DoK boot+walk case; 5 DoK archives
  bundled). FP wall-TILE routing (DoK's single 256-tile bank across 3 symbol sets) deferred to B4.
  (2026-06-22)
- [x] A6.3 — Full flow: new `apps/web/src/ui/play.ts` `startPlay` orchestrator stitches the standalone
  slices into one `?play=cok` session — title sequence → character creation (a new "Begin Adventure"
  hook on `startCreate`) → the dungeon, where the loop is explore ↔ combat (random encounters) ↔ camp
  (Encamp/Save) ↔ town (a `__play`-owned round-trip), with the platform-bound A6.2 audio along for the
  ride. A returning party (roster non-empty) skips creation and drops straight into the dungeon.
  `window.__play` seam (phase/begin/toTown/toExplore) + sidebar entry + `?play=` route. New e2e case
  walks the whole chain (title→create a fighter→begin→fight→camp→town→back→save). 450 engine / 29 e2e.
  (2026-06-22)
- [x] A6.2 — In-game audio wiring: new app-wide `apps/web/src/audio/gameAudio.ts` `GameAudio`
  singleton over the existing `AudioManager` (lazy-loads CoK's Amiga SMUS theme + 8SVX effects once;
  scenes title/explore/combat at SMUS tempos 8/8/12 + `silent`; actions melee/arrow/cast/fireball/
  death/door/miss → real samples hit/arrow/cast2/fireb/dead/gate/swish). Wired: `intro.ts` arms the
  title theme (sounds on first gesture), `explore.ts` enters the explore ambient, `combat.ts` enters
  the combat theme on battle start + plays melee/miss/death from `logEvents` + returns to explore on
  dismiss, `gswitch.ts` mirrors the platform (Amiga = sound, DOS = honestly silent). `window.__gameAudio`
  seam + new e2e case. 450 engine / 28 e2e. (2026-06-22)
- [x] A6.1 — Save/Load: engine `world/saveGame.ts` (versioned `SaveGame` snapshot — party + stores +
  journal + position + clock + flags; `makeSave`/`serializeSave`/`deserializeSave` round-trip +
  self-repair; `summarizeSave`) + `Explorer.setPose`; 7 tests. Web `ui/saveStore.ts` bundles the
  localStorage stores into a slot + `applySave` (refreshes party/journal caches); explore Encamp→Save
  writes the quick slot, Load + `?explore=1&load=1` restore it. 450 engine / 27 e2e. (2026-06-22)
- [x] A5.4 — Adventurer's Journal: engine `world/journal.ts` `Journal` (persistent numbered pages,
  idempotent `record` by entry-number, category filter, read/unread, JSON round-trip) +
  `render/journalScreen.ts` `composeJournalScreen` + `wrapText`; 20 tests. Web `?journal=1` viewer
  (persisted `greybox.journal.cok` + demo seed) + `recordJournal` hooks (tavern rumour, hermit bribe
  via new dialogue `onEnd`). 443 engine / 27 e2e (seed/read/record/filter/persist). (2026-06-22)
- [x] A5.3 — NPC dialogue via the live ECL VM: engine `world/dialogue.ts` `DialogueRunner`
  (translates `EclMachine` run/resume → say/combat/chain/end turns; branching emergent) +
  `dialogueForCell`; 6 tests. Web `?dialogue=1` overlay (text + choices / number entry, combat
  handoff→resume, original hermit demo) + `explore.ts` interactive-ask integration (real MON foes).
  423 engine / 26 e2e (ask/branch/chain/bribe/fight→resume). (2026-06-22)
- [x] A5.2 — Town hub: `town/shop.ts` (list prices + half buy-back, pure buy/sell) + `town/services.ts`
  (temple heal/raise costs, training `checkTraining`/`levelHpGain` off the 1e XP tables) +
  `composeTownScreen` (shared title/steel/list/price/bar page) + 20 tests; web `?town=1` hub →
  shop/temple/training/tavern against the live party economy (XP store + `awardPartyXp` from combat).
  417 engine / 25 e2e (buy/sell, level-up, heal/raise). (2026-06-22)
- [x] A5.1 — Wilderness/overland travel: `world/wilderness.ts` (`WildernessTravel` 4-dir steps over a
  terrain grid, edge/impassable blocking, per-terrain travel-minutes, `checkEncounter` d100 roll;
  `wildernessFromAscii`) + `composeWildernessScreen` (terrain-coloured area map, party marker, info
  panel, command bar) + 17 tests; web `?wild=1` "Vale of Solace" demo region — arrow travel advances
  the clock/moons and drops wandering encounters into the live combat overlay. 397 engine / 24 e2e. (2026-06-22)
- [x] A4.3 — Treasure: `rollTreasure`/`hasTreasure` (gold ≈ ΣXP/2 jittered, 1/3 per-foe item drop)
  + `composeTreasure` screen (gold/item rows, TAKEN tags, Take All/Money/Items/Exit) + 14 tests;
  combat victory enters a `'treasure'` phase, party gold pool + `giveItemsToMember` bank the haul;
  explore e2e asserts gold conservation + items reach the leader's pack. 380 engine / 23 e2e. (2026-06-22)
- [x] A4.2 — Trade + encumbrance: item weights + `transferItem` (un-readies on hand-off) + an
  `encumbrance` model (Strength-scaled level/move-rate, CANDIDATE thresholds); `composeInventory`
  shows a WT/encumbrance line + status; web inventory gains a **Give** verb and a standalone `?inv=1`
  route. 366 engine / 23 e2e green (new fighter→cleric Trade case). (2026-06-22)
- [x] A4.1 — Inventory ("Items") screen: engine base-item catalogue + immutable inventory model
  (ready/equip with slot rules, drop, use; AC/THAC0 recompute) + pure `composeInventory`; web
  `ui/inventory.ts` overlay opened from the char sheet's new **Items** verb, per-character starter
  kits persisted in `party.ts`. 356 engine / 22 e2e green (explore View→Items drives ready-flip,
  AC recompute and drop). Also shipped same day: live explore→combat hook (A3.4 `d54a213`), combat
  visuals fix (`e8ce5cd`), DOS EGA de-roll (`65e2b28`). (2026-06-22)
- [x] A2.1 — Spell memorization screen: new CoK spell DB (`spellList.ts`, cleric+mage L1–5) +
  immutable `memorize.ts` model + pure `composeSpellbook` compositor; Encamp→Magic overlay
  (`magic.ts`) lists known spells by level with slot headers (`spellSlots`/`clericSpellSlots`),
  memorize/forget on a cursor, cycles casters, persists per character. 313 engine / 22 e2e green
  (10 spell unit tests + a Magic e2e: memorize/forget/persist-across-reload). (2026-06-22)
- [x] A1.3 — View → character-sheet screen: new pure `composeCharSheet` compositor (bordered page,
  8X8D font; name/race/class, six ability scores, AC/THAC0/HP, five saving throws, deity/order
  footer, Next/Prev/Exit bar) + a `charSheet.ts` overlay that cycles party members on the emulated
  EGA surface; View opens it from the dungeon bar + Encamp. 303 engine / 21 e2e green (6 compositor
  unit tests + an explore View/cycle/close assertion). (2026-06-22)
- [x] A1.2 — Rest wired to real effects: engine `gameClock` (running minutes → Day/HH:MM) +
  `planRest` natural healing (1 HP/living member/day, dead stay down) drive Encamp→Rest; clock
  advances by the days it took, the three moons walk forward, elapsed time + HP shown. Explore now
  runs a real clock (CAMP/position lines + moonbar) instead of the fake day→hour. 297 engine / 21
  e2e green (rest-wire + 9 rest/clock unit tests). (2026-06-22)
- [x] A1.1 — live command bar in `?explore=1`: dungeon `Area Cast View Encamp Search Look`
  hotkey bar + in-screen `Save View Magic Rest Alter Fix Exit` Encamp sub-menu (cursor + hotkeys),
  WASD/arrows still walk; explore e2e now gates on `menuOk`. 288 engine / 21 e2e green. (2026-06-22)
- Pre-queue, already shipped: intro `?intro=1`, combat **screen** `?combat=1`, command **menus**
  `?menus=1`, **audio** first slice `?audio=1`, live EGA↔Amiga `?gswitch=1`, explore `?explore=1`,
  create-party `?create=1`, automap, the indexed-framebuffer compositor substrate, and the full
  format library (DAX/DAA/HLIB/ILBM/8SVX/SMUS decoders). See `ROADMAP.md` "Build progress (live)".
