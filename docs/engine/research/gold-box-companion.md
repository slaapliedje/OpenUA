# Gold Box Companion — Feature Inventory & Engine Implications

> Research date: 2026-06-21
> Researcher: goldbox-researcher agent
> Topic: Complete feature inventory of Gold Box Companion (GBC) for folding into the
> all-in-one engine (see `docs/engine/VISION.md` Pillar 4).

---

## 1. What It Is

**Gold Box Companion (GBC)** is a Windows companion utility for the SSI AD&D Gold Box
CRPG series (and related titles). It runs as a separate native Windows process alongside
DOSBox, reads and writes the emulated game's RAM in real time, and overlays an HUD on
top of the DOSBox window. It is also a save-file editor and a suite of game-data modding
tools.

- **Author / maintainer:** Joonas Hirvonen (`joonas@zorbus.net`)
- **Website / primary download:** <https://gbc.zorbus.net/>
- **Current version:** v2.65 (15 Apr 2022)
- **License:** Freeware — no explicit open-source license stated. Accepts PayPal
  donations. The tool has been *licensed* (not open-sourced) to Valve/Steam and GOG for
  inclusion in official releases of the Gold Box games.
- **Related tool by same author:** All-Seeing Eye (companion for the Eye of the Beholder
  trilogy — same architecture, different games).
- **Source availability:** UNKNOWN — no public source repository found. Closed-source
  freeware.

Sources: [gbc.zorbus.net](https://gbc.zorbus.net/), [eXo Wiki](https://wiki.retro-exo.com/index.php/Gold_Box_Companion),
[Gold Box Games Wiki](https://wiki.goldbox.games/index.php/Using_Gold_Box_Companion)

---

## 2. Supported Games

| Series | Titles |
|---|---|
| Forgotten Realms | Pool of Radiance, Curse of the Azure Bonds, Secret of the Silver Blades, Pools of Darkness |
| Krynn (Dragonlance) | Champions of Krynn, Death Knights of Krynn, The Dark Queen of Krynn |
| Savage Frontier | Gateway to the Savage Frontier, Treasures of the Savage Frontier |
| Authoring | Forgotten Realms Unlimited Adventures (FRUA / UAShell) |
| Buck Rogers | Countdown to Doomsday, Matrix Cubed |

**Total: all ~14 SSI AD&D Gold Box titles + FRUA + 2 Buck Rogers games.**

The Steam and GOG packaged versions ship a restricted GBC build (editor features
disabled). The full version with editing tools is downloaded separately from
`gbc.zorbus.net`.

---

## 3. Technical Mechanism

GBC is a **native Windows EXE** (Windows XP+, 32/64-bit). It does **not** run inside
DOSBox. Instead:

1. The user starts DOSBox with the target game running.
2. GBC launches a wizard that **scans the DOSBox process's virtual address space** for
   known character-data signatures. The user either types character names or points GBC
   at a save file to bootstrap the scan.
3. Once anchored, GBC **reads and writes DOSBox's emulated RAM** directly (via Windows
   `ReadProcessMemory` / `WriteProcessMemory` — inferred from behavior; source not
   public). This gives instantaneous edits — no game restart required.
4. GBC paints an overlay HUD above the DOSBox window (Windows layered/topmost window).
5. Map data is also read from DOSBox memory and rendered in GBC's own map panel.

**DOSBox version dependency:** GBC was built against **DOSBox 0.74** and does NOT work
with DOSBox Staging (which changed the internal layout). DOSBox Staging's maintainers
later added an HTTP API to expose emulated memory, explicitly inviting someone to
reimplement GBC against it — "first person to reimplement Gold Box Companion using this
API gets a prize."

**During combat:** the character editor cannot be used (memory layout changes during
combat).

**Save-file editing** is a secondary pathway: the Save Game Editor reads/writes the game's
save slot files on disk rather than live RAM, usable outside a running game session.

Sources: [gbc.zorbus.net](https://gbc.zorbus.net/), [GOG forum](https://www.gog.com/forum/forgotten_realms_collection/question_on_running_gold_box_companion),
[RPGWatch](https://rpgwatch.com/forum/threads/pool-of-radiance-gold-box-companion.41936/),
[eXo Wiki](https://wiki.retro-exo.com/index.php/Gold_Box_Companion)

---

## 4. Full Feature Inventory

### 4.1 Feature Table

| # | Feature | Category | Type | Notes |
|---|---|---|---|---|
| 1 | **HUD overlay** — character icons with HP bars, XP meters, status effects (green = good, red = bad), level drain indicator | Display | Play aid | Overlaid above DOSBox window; updates from live RAM |
| 2 | **Party stats bar** — condensed view of all party members across the top of the HUD | Display | Play aid | |
| 3 | **Automap — dungeon** — reveals dungeon tiles as the party explores; optional full-reveal ("cheat") mode; shows enemies, treasures, doors | Navigation | Play aid / mild cheat | Reads map data from DOSBox RAM; constrained to engine's 16×16 tile limit; unreliable when game uses teleporter tricks for "large" areas |
| 4 | **Automap — world map** — shows party location on the overworld map in supported games | Navigation | Play aid | |
| 5 | **Automap notes** — user can annotate map squares | Navigation | Play aid | |
| 6 | **Combat view** — shows positions of party members and monsters on the combat grid; shows held/helpless status | Combat | Play aid | |
| 7 | **Journal entry viewer** — in-game journal entries readable without a PDF; avoids copy-protection lookup | Reference | Play aid | Essential for games that use numbered journal codes as copy protection |
| 8 | **Monster databases / Monster Manual** — included in Resources folder | Reference | Play aid | Static reference data bundled with GBC |
| 9 | **Level progression tables** — XP tables and thresholds per class | Reference | Play aid | Bundled reference |
| 10 | **Spell memorization store/restore** — snapshot current spell list; restore with one click | Convenience | Automation | Reads/writes spell slots from RAM; fixes a known save-corruption bug in some Gold Box titles |
| 11 | **Instant heal fix** — heals all characters to full HP instantly; optionally restores level-drained HP | Convenience | Cheat / Fix | Writes HP values directly to RAM |
| 12 | **Level drain restoration** — restores character levels lost to undead drain | Convenience | Fix | Writes level data to RAM |
| 13 | **Level-up anywhere** — trigger level-up without traveling to a training hall; optionally ignore race/game level caps | Convenience | Automation / rule-bend | |
| 14 | **Race conversion (temp.)** — temporarily changes a demihuman's race flag to Human, bypassing XP caps | Convenience | Rule-bend | Reversible; useful for importing characters into sequels |
| 15 | **Auto-identify items** — automatically identifies all magic items in inventory | Convenience | Automation | Removes need for Identify spell / scroll |
| 16 | **Auto-ammo replenishment** — keeps arrows/bolts/darts at a configured quantity | Convenience | Automation / cheat | |
| 17 | **Quickfight auto-disable** — automatically turns off Quickfight mode after each combat encounter | Convenience | Automation | QoL: prevents accidentally missing loot |
| 18 | **Teleport — dungeon** — teleport the party to any tile coordinate in the current dungeon | Navigation | Cheat / debug | Writes party position to RAM |
| 19 | **Teleport — world map** — teleport to any location on the world map (select games) | Navigation | Cheat / debug | |
| 20 | **Character editor (live RAM)** — edit stats, HP, XP, class, level, alignment, effects of any party member in real time | Editor | Mutates state | Cannot be used during combat |
| 21 | **Save Game Editor** — edit party composition (swap members in/out), inventory, and effects via save files on disk | Editor | Mutates state | File-based; no live game required |
| 22 | **Save game backup** — automatic or manual backup of save slots | Editor | Utility | |
| 23 | **Save game listing** — chronological display of saves with location descriptions | Editor | Play aid | |
| 24 | **Item editor** — edit item properties in save data (Dark Queen / FRUA) | Editor | Mutates state | Added in v2.60 |
| 25 | **ECL Tool** — browse and edit the ECL bytecode scripts of the games; live ECL monitor showing current script and flags while game runs; map display; global search; flag lists; memory address comments (PoR/CotAB annotated) | Modding | Mutates game data | The most advanced power-user feature; reads/writes ECL files and monitors live execution |
| 26 | **Monster Modder** — browse and edit monster data tables | Modding | Mutates game data | |
| 27 | **Paladin / Ranger for Pool of Radiance** — enables classes not normally selectable in PoR; characters are exportable to sequels | Modding | Rule extension | |
| 28 | **Experimental Monk class** | Modding | Rule extension | Described as experimental |
| 29 | **Font modding** — tools to replace the in-game font | Modding | Mutates game data | |
| 30 | **FRUA Module Manager** — install Unlimited Adventures modules from the UA File Archive without UAShell | Authoring | Utility | Exclusive to FRUA variant |
| 31 | **Background music add-on (GBC_Audio)** — plays external music files during gameplay | Presentation | Enhancement | Optional; 40 MB and 430 MB music packs available |
| 32 | **GOG/Steam setup wizard** — detects and configures DOSBox settings, scaling, aspect ratio, cloud saves | Setup | Utility | Part of GOG/Steam packaging |

### 4.2 Feature Categories Summary

| Category | Count | Notes |
|---|---|---|
| Display / Play aid (read-only) | 9 (features 1–9) | Pure information; no game state mutation |
| Convenience / QoL fixes | 9 (features 10–19) | Mix of automation and light cheating; mutate RAM |
| Editors (save / char) | 5 (features 20–24) | Directly mutate save files or RAM |
| Modding tools | 6 (features 25–30) | Mutate game data files |
| Presentation / Setup | 2 (features 31–32) | Non-gameplay |

---

## 5. Implications for Our Engine

Our engine **owns the game state** directly — there is no DOSBox emulation layer, no
external process to poke, no opaque save files to parse. This radically changes the
cost of every GBC feature.

### 5.1 Features That Become Native Panels (trivial / free)

These are expensive for GBC because it must reverse-engineer and scan RAM; for us they
are zero-extra-cost reads of engine state we already maintain:

| GBC Feature | Our engine equivalent |
|---|---|
| HUD (HP/XP/effects) | First-class UI panel; always available |
| Party stats bar | Ditto |
| Automap (dungeon) | The engine tracks visited tiles; render them any time |
| Automap (world map) | Same — we own the world map state |
| Automap notes | Persist user annotations in a JSON sidecar |
| Combat view | The engine runs combat; the grid is just a render call |
| Teleport (dungeon / world) | A debug/cheat panel that writes engine coordinates |
| Character editor | A panel that exposes the in-memory Party object directly |
| Save listing with location | A first-class save-browser panel |
| Spell store/restore | Engine-level snapshot of spell slots |
| Instant heal | One-liner in the party model |
| Level drain restore | Same |
| Level-up anywhere | Remove the "must be at training hall" check |
| Auto-identify | Skip the Identify-required flag on item resolution |
| Auto-ammo | Engine hook on ranged attacks |
| Quickfight auto-disable | Engine hook post-combat |
| Race XP cap bypass | A config flag on the ruleset |

### 5.2 Features That Require Data Work (medium effort)

| GBC Feature | Our effort |
|---|---|
| **Journal entry viewer** | Decode the journal entry data from each game's asset files (DAX/HLIB DATA chunks). Format is per-game; hackdocs `VOCAB.TXT` / `GAMEDAT.TXT` document the string tables. One-time data-extraction work per game, then trivial to display. |
| **Monster / spell / item databases** | Extract from MON/ITEM/SPEL tables (hackdocs documented). Becomes a reference panel. |
| **Level progression tables** | Decode CLASS tables; static per-ruleset. |
| **Save Game Editor (party swap, inventory)** | We own the save format — editing is a Panel call into our save model. |
| **Item editor** | Same — expose item property model in a panel. |

### 5.3 Features That Are Straightforward Modding Tooling (higher effort but well-defined)

| GBC Feature | Our effort |
|---|---|
| **ECL Tool** | We are already building an ECL VM. A browser-based ECL browser/editor is a developer panel on top of the decoded bytecode model. The live ECL monitor maps naturally to a VM debugger/inspector panel. This is the most valuable GBC feature to reimplement natively — it unlocks scenario authoring. |
| **Monster Modder** | An editor panel over the decoded MON tables. Medium effort; data format is documented in `hackdocs_extracted/MONST*.TXT`. |
| **Paladin/Ranger/Monk class additions** | Implement as ruleset extensions in the engine's class model. |
| **Font modding** | The engine should allow texture/font swaps as part of the graphics-mode system (already part of our live-switching pillar). |
| **FRUA Module Manager** | Out of scope for Krynn-first milestone; relevant for authoring pillar later. |

### 5.4 Features Obviated by Owning Game State

The following GBC mechanisms are entirely **artifacts of the external-tool architecture**
and have no direct equivalent because we don't need them:

- DOSBox process memory scanning and attaching
- DOSBox version pinning (0.74 only)
- RAM offset lookup / character name search wizard
- "Cannot edit during combat" restriction (GBC limitation, not a design choice)
- External Windows overlay window management
- GOG/Steam DOSBox configuration wizard

### 5.5 Top 5 Features Most Worth Building First

In priority order for our first public milestone ("play one game start-to-finish with
a read-only Companion panel"):

1. **Automap panel** — the single highest-value play aid; the engine already tracks
   visited tiles so this is mostly a render task. Addresses the #1 complaint about
   original Gold Box games.
2. **Journal entry viewer** — removes the copy-protection friction of numbered journal
   codes; requires decoding string tables from the asset files (one-time data work,
   then trivial to surface).
3. **HUD (HP / XP / effects)** — always-visible party status is table stakes; gives the
   game a modern feel without touching gameplay.
4. **ECL Tool / VM Inspector** — the most powerful authoring feature; natively building
   this as a developer panel directly supports the authoring pillar and unlocks scenario
   debugging. GBC's live ECL monitor is the killer feature for modders.
5. **Character / Save editor panel** — combines GBC's character editor + save game editor
   into one panel; since we own the state, implementation cost is minimal and it covers
   a large QoL surface (inventory editing, stat tweaking, party management).

### 5.6 Legal / Clean-Room Note

GBC is **closed-source freeware**. We must not copy its code or reverse-engineer its
binary. Our implementations must be derived from:
- The original game data (our own copies or user-supplied)
- `hackdocs_extracted/` community format documentation
- Our own format decoders (`tools/dax_decode.py`, `tools/daa_decode.py`,
  `tools/hlib_decode.py`)
- Independent knowledge of the AD&D ruleset

The *feature ideas* (automap, journal, editor panels) are not copyrightable. Reimplementing
them natively is fully legal.

---

## 6. Open Questions / Unknowns

| # | Question | Impact |
|---|---|---|
| 1 | **GBC's exact memory offsets** — what RAM structures it reads for each game. Source is not public; offsets would accelerate our understanding of save-file / in-memory layouts (but we should derive these independently from the format docs). | Low — we derive from hackdocs |
| 2 | **ECL Tool internals** — does GBC's ECL tool parse ECL bytecode using the same opcode table as `hackdocs_extracted/OPCODES.TXT`? Any extensions? | Medium — affects our ECL VM design; check `OPCODES.TXT` against observed game behavior |
| 3 | **Automap data source** — does GBC reconstruct the map by tracking player movement through RAM, or does it read a pre-existing map tile array from the GEO data? The 16×16 limit and teleporter-confusion bug suggest it reads the GEO tile array directly. | Medium — we should use GEO map data (already documented in `hackdocs_extracted/GEO*.TXT`) rather than movement tracking |
| 4 | **Journal entry format per game** — confirmed to exist in game data, but exact byte layout per game not yet decoded. Needed before journal panel can be built. | High for journal feature |
| 5 | **Monster Modder data format** — which fields does it expose? Cross-check with `hackdocs_extracted/MONST*.TXT`. | Low — hackdocs covers this |
| 6 | **GBC's "experimental Monk class"** — is this a data patch to the EXE or a pure save-file trick? | Low — nice-to-have class extension |
| 7 | **Joonas Hirvonen's stance on engine reuse** — GBC is not open-sourced. Would the author be willing to share format research or collaborate? Contact: `joonas@zorbus.net`. | Potentially high — he has deep format knowledge |
| 8 | **DOSBox Staging HTTP API** — does this API expose enough to trivially port GBC features if we ever need a DOSBox fallback path? | Low for our engine (we don't use DOSBox), but relevant for a compatibility shim |

---

## 7. Sources

- [Gold Box Companion — official site](https://gbc.zorbus.net/)
- [Gold Box Companion — tutorial](https://gbc.zorbus.net/tutorial/index.html)
- [Gold Box Companion — Steam extras](https://gbc.zorbus.net/steam/index.html)
- [Gold Box Games Wiki — Using Gold Box Companion](https://wiki.goldbox.games/index.php/Using_Gold_Box_Companion)
- [eXo Wiki — Gold Box Companion](https://wiki.retro-exo.com/index.php/Gold_Box_Companion)
- [GOG forum — Question on running GBC](https://www.gog.com/forum/forgotten_realms_collection/question_on_running_gold_box_companion)
- [GOG forum — Automap 16×16 limit thread](https://www.gog.com/forum/forgotten_realms_collection/is_gold_box_companions_automap_crap_for_all_games_withs_maps_over_16x16_in_size)
- [GOG forum — GBC v2.60 thread](https://www.gog.com/forum/forgotten_realms_collection/gold_box_companion_v260)
- [Gamebanshee news — GBC and All-Seeing Eye](https://www.gamebanshee.com/news/118369-gold-box-companion-and-the-all-seeing-eye-tools-for-ssi-enthusiasts.html)
- [RPGWatch forums — PoR + GBC](https://rpgwatch.com/forum/threads/pool-of-radiance-gold-box-companion.41936/)
- [Gold Box forum thread — GBC topic, page 10](https://forums.goldbox.games/index.php?topic=1913.135)
- [PC Gamer — Gold Box on Steam](https://www.pcgamer.com/the-classic-gold-box-dd-games-are-on-steam-but-whats-special-about-them/)
- Inference labels: features described as **(inferred)** where source was user reports rather than official documentation.
