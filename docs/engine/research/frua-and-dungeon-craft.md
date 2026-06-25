# FRUA and Dungeon Craft — Research Findings

> **Purpose:** Baseline feature/limits survey for our "FRUA but broader" authoring layer.
> **Written:** 2026-06-21. Researcher: goldbox-researcher agent.
> **Sources:** Local hackdocs (`hackdocs_extracted/`), Wikipedia, SourceForge, GitHub,
>   FRUA community FAQ (groups.google.com), CRPG Addict blog, uaf.sourceforge.net.
> **Confidence codes:** [V] = verified from primary source / local file;
>   [W] = web source cited; [I] = inferred; UNKNOWN = not found.

---

## 1. FRUA — Forgotten Realms: Unlimited Adventures (SSI, 1993)

### Overview [W]

- Published: March 17, 1993 by SSI for MS-DOS (IBM) and Mac.
- Executable: `CKIT.EXE` (587,843 bytes, version 1.2 final) — written in C, compiled
  with Microsoft C compiler. [V: `hackdocs_extracted/CKITFORM.TXT`]
- Ruleset: AD&D 1st Edition (races/classes/spells baked in from Pool of Radiance lineage).
- Resolution: 320×200, 256-colour EGA/VGA. [W: Wikipedia, CRPG Addict]
- Art container: `.TLB` (same HLIB-like format used by DQK). [V: `UATFILES.TXT`]

### 1.1 Design Structure (the "module" data model) [V: `UAFILES.TXT`]

A design lives in a `.DSN` folder:

| File/Pattern | Size | Content |
|---|---|---|
| `GAME001.DAT` | 388 B | Module settings (always present) |
| `GEOxxx.DAT` (001–040) | 12,962 B each | Map + events for one dungeon/overland |
| `MONSTxxx.DAT` | 450 B each | Per-monster/NPC data (one file per modified monster) |
| `STRG001.DAT` | 3,668 B | String data |
| `STRG003.DAT` | 574 B | 41 × 14-byte records (game variables) |
| `*.TLB` (in design dir) | varies | Any imported custom art |
| `SAVE/SAVGAMx.CSV` | varies | Save-game data (a–j slots) |
| `SAVE/VAULTx.DAT` | varies | Vault contents per save slot |
| `SAVE/*.CCH` | ≥398 B | Per-character file (name up to 8 chars) |

Global data shared across designs lives in the main `\frua` install:
`DISK1/ITEM.DAT`, `DISK1/ITEMS.DAT` (items), `DISK2/MONST.GLB` (default monsters),
`DISK3/GEO.GLB`, `DISK3/SCRIPT.GLB`, `DISK3/STRG.GLB`.

### 1.2 Hard Limits Table [V: local hackdocs; W: FRUA FAQ, CRPG Addict]

| Resource | FRUA Hard Limit | Source |
|---|---|---|
| Dungeons per design | 36 (`DUNGEON01`–`DUNGEON36`) | `SLOTSFRM.TXT` |
| Overland maps per design | 4 (`OVERLAND01`–`OVERLAND04`) | `SLOTSFRM.TXT` |
| Total "places" per design | 40 (36 dungeon + 4 overland) | `UAFILES.TXT` / FRUA FAQ |
| Map grid maximum | 576 tiles (≈ 24 × 24; minimum 11 × 11) | FRUA FAQ [W] |
| Events per map | ~100 events, ~8,500 text characters | Community FAQ [W] |
| Quests | 44 (values 1–100 each) | `SLOTSFRM.TXT` |
| Special items (visible in inventory) | 12 | `SLOTSFRM.TXT` |
| Keys (quest items, invisible) | 8 | `SLOTSFRM.TXT` |
| Monster icon slots | 84 (monsters 1–84) + NPC/complex sub-slots | `SLOTSFRM.TXT` |
| Pic (small portrait) slots | 231 across PIC[A-F].TLB | `UATFILES.TXT` |
| Big-pic slots | 13 (8 in BIGPIC.TLB + 5 in BIGPIX.TLB; 3 more "grown") | `UATFILES.TXT` |
| Sprite (overland/3D) slots | 38 | `UATFILES.TXT` |
| Dungeon wall sets | 16 (slots) across 8X8DB + 8X8DC; 3 usable per map | `UATFILES.TXT` |
| Combat backdrop slots | 4 per design (BACK.TLB, 19 sets globally) | FRUA FAQ / `UATFILES.TXT` |
| Dungeon combat wall sets | 5 (DUNGCOM.TLB) | `UATFILES.TXT` |
| Wildland combat scenery sets | 4 (WILDCOM.TLB) | `UATFILES.TXT` |
| Player combat icon slots | 49 selectable (CBODY.TLB), 57 total before wrap | `UATFILES.TXT` |
| Sound files | 3 XMI music tracks per sound card type; 1 VOC SFX | `UAFILES.TXT` |
| Backdrop palette colours | 32 colours (indices 144–175 only) | `UAPALETT.TXT` |
| Guided-tour steps per event | 24 (chainable) | FRUA FAQ [W] |
| Save game slots | 10 (a–j) | `UAFILES.TXT` |

### 1.3 Art Types and Pixel Dimensions [V: local hackdocs; W: FRUA FAQ]

| Art Type | Pixel Dimensions | Format | Container |
|---|---|---|---|
| Small pic ("Pic") | 88 × 88 max | PCX/LBM → TLB | PIC[A-F].TLB |
| Big pic | 304 wide (height varies) | TLB draw method 18 | BIGPIC.TLB / BIGPIX.TLB |
| Title screen | Up to 320 × 200 | TLB | TITLE.TLB |
| Combat icon (monster/NPC) | UNKNOWN (small, ~32×32 implied) | TLB draw method 23 (transparent) | CPIC.TLB |
| Player combat body icon | Same format as combat icon | TLB | CBODY.TLB |
| Sprite (3D view icon) | UNKNOWN | TLB (3 images per sprite) | SPRIT.TLB |
| Wall art | 47 images per set (5 groups, varying sizes) | TLB | 8X8DB / 8X8DC.TLB |
| Dungeon backdrop | 32-colour, palette 144–175 | TLB (drawn as Pic, renamed) | BACK.TLB |
| Overland map image | Full-screen | LBM | TOPVIEW.TLB |
| Combat background (dungeon) | 26 images per set | TLB | DUNGCOM.TLB |
| Combat background (wild) | 40 images per set | TLB | WILDCOM.TLB |
| UI frame | 28 sub-images | TLB | FRAME.TLB |

Pixel encoding: Draw method 18 = RLE, opaque, interleaved (every 4th pixel per sweep,
4 sweeps = 1 row). Draw method 23 = RLE, transparent, same interleave pattern. [V: DRAW18.TXT, DRAW23.TXT]

Palette ranges are tightly partitioned: ALWAYS.TLB uses 0–15; FRAME uses 16–31; Pics/BigPics/Overland use 32–255; Walls use 32–68; Backdrops use 144–175; Sprites ~176–255. [V: `UAPALETT.TXT`]

### 1.4 Event System ("Design Vocabulary") [W: FRUA FAQ; V: `SLOTSFRM.TXT`]

**35+ event types** (verbatim from the FRUA FAQ, verified in hackdocs):

`ADD NPC`, `CAMP`, `CHAIN`, `COMBAT`, `COMBAT TREASURE`, `DAMAGE`, `ENCOUNTER`,
`ENTER PASSWORD`, `GAIN EXPERIENCE`, `GIVE TREASURE`, `GUIDED TOUR`, `NPC SAYS`,
`PASS TIME`, `PICK ONE COMBAT`, `QUEST STAGE`, `QUESTION-BUTTON`, `QUESTION-LIST`,
`QUESTION-YES/NO`, `REMOVE NPC`, `SHOP`, `SMALL TOWN`, `SOUNDS`, `SPECIAL ITEM`,
`STAIRS`, `TAVERN`, `TAVERN TALES`, `TELEPORTER`, `TEMPLE`, `TEXT STATEMENT`,
`TRAINING HALL`, `TRANSFER MODULE`, `UTILITIES`, `VAULT`, `WHO PAYS`, `WHO TRIES`

**Event trigger conditions** (conditional chain logic):
- Fired once / multiple times
- Special item present/absent
- Daytime / nighttime
- Random percentage
- Party searching / not searching
- Facing direction
- Quest status (completed / failed / in-progress)
- Trap detected
- Invisible entity detected
- Specific race/class of party leader or whole party
- Chain control (unconditional jump to next event)

**Combat result caveat:** FRUA cannot distinguish "party killed all monsters" from
"party fled/survived." A fled party still triggers post-combat chain events (quest XP,
etc.). [W: FRUA FAQ]

### 1.5 What Designers COULD Change [W: FRUA FAQ, Wikipedia; V: hackdocs]

- Import custom pics (small 88×88), big pics, sprites, combat icons, wall sets, backdrops.
- Rename and re-stat any of the 127 default monsters (MONSTxxx.DAT).
- Place any of the 35+ event types on any map tile, with trigger conditions.
- Set monster morale %, turning difficulty.
- Customise wall properties: secret doors, one-way passages, collapsing walls.
- Rename special items and keys (8 keys + 12 special items per design).
- Define 44 quests with point values.
- Add NPCs by re-using monster slots.
- Replace title screens, intro sequence, frame art, fonts, UI buttons.
- Overhead (top-down) map view: toggle per module, custom LBM image.
- With **hacks** (UASHELL): new wall sets for all 3 wall slots; custom fonts, frames,
  intro sequences; new races/classes; new weapons/items; altered spells; genre "worldhacks"
  (sci-fi, superhero, Western, etc.). [W: Wikipedia]

### 1.6 What Designers COULD NOT Change (Unmodified) [V: `UAGIVENS.TXT`; W: multiple]

- **Spells:** could not create new spells or edit existing ones. [W: SSI dev quote]
- **Items:** could not create new items with special abilities. Only rename existing ones. (TSR licence cited.) [W: FRUA FAQ]
- **Races/classes:** hardcoded AD&D 1e restrictions (humans/half-elves only for clerics; dwarves/gnomes/halflings restricted to fighter/thief). [W: CRPG Addict]
- **Combat win/loss detection:** no way to distinguish "won" vs "fled" in event chains.
- **Big-pic count:** capped at 13 stock + 3 "grown" = 16 max.
- **Walls/backdrops/title screens:** could not change in unmodified game (hacks required).
- **Event count:** ~100 events and ~8,500 text chars per map — large designs hit this wall.
- **Sound:** no new sound import (hacks required to add custom music/SFX).
- **NPC behaviour:** editing an NPC's stats causes them to flee every combat.
- **Dual-class mechanics:** first class goes inactive until new class surpasses it (AD&D 1e rule, baked in). [V: `UAGIVENS.TXT`]
- **Thieving skills:** no way to transfer thieving skills to another class. [V: `UAGIVENS.TXT`]
- **Starting entry point:** always fixed at entry point 1.
- **Standalone play:** the base game (CKIT.EXE) was always required — no self-contained executable.

### 1.7 Audio [V: `UAFILES.TXT`; W: GOG forum]

- Music: `.XMI` files, three per sound card family (AdLib, PC Speaker, Roland).
- SFX: single `SFXDQ.VOC` file (Sound Blaster VOC format).
- Sound card support: AdLib, PC Speaker, Roland MT-32, Sound Blaster digital.
- Custom sound: not supported without hacks.

### 1.8 Community Hacking Ecosystem

- **UASHELL:** umbrella tool — applies hacks to CKIT.EXE byte offsets; manages alternate
  designs; enables community hack packs. [W: Wikipedia, UAShell wiki]
- **"Worldhacks":** total-conversion packs redefining races/classes/art/settings for
  sci-fi, superhero, Roman Empire, etc. themes. [W: Wikipedia]
- **CKITFORM.TXT:** 284 KB document cataloguing every known byte offset in CKIT.EXE with
  effect; our primary reverse-engineering reference. [V: local file]

---

## 2. Dungeon Craft (formerly UAForever / UAF)

### 2.1 Identity and Provenance [W: SourceForge, GitHub]

| Property | Value |
|---|---|
| Full name | Dungeon Craft (formerly Unlimited Adventures Forever, UAF) |
| Main site | https://uaf.sourceforge.net/ |
| Source repo | https://github.com/grannypron/uaf (GPL-2.0) |
| License | **GNU GPL v2.0** |
| Language | C++ (96.5%), C (3.5%) |
| Platform | Windows (DirectX 7+); WebGL/Unity browser build exists in repo |
| Latest stable release | v5.30 — January 23, 2024 [W: SourceForge project page] |
| SourceForge news last update | September 2014 (v1.00 milestone) — the SF news page is stale |
| GitHub activity | 79 commits on master; 36 open issues; last release Jan 2024 |
| Origin | Started ~1995 as DirectX learning project; inspired by two technical books on Gold Box pseudo-3D + tile combat |

> **Maintenance status:** The GitHub repo had a v5.30 release in January 2024, so the
> project has had recent activity despite the stale SourceForge news page. [I: read both]

### 2.2 What Dungeon Craft Broadened vs FRUA

| Constraint | FRUA | Dungeon Craft |
|---|---|---|
| Colour depth | 8-bit (256 colours, EGA/VGA) | 16/24/32-bit true colour |
| Screen resolution | 320×200 only | 640×480, 800×600, 1024×768 |
| Art formats accepted | .PCX, .LBM only | BMP, PCX, TGA, PNG, JPEG + video: AVI |
| Audio formats | XMI (MIDI), VOC (SFX) | WAV, MIDI, MP3, MOD, AVI |
| All art customisable | Partial (walls/backdrops/title locked without hacks) | Yes — all art can be swapped |
| Editable items | No (rename only) | Yes — full item database |
| Editable spells | No | Yes — full spell database |
| Editable monsters | Limited (rename + re-stat within slots) | Yes — full monster database (125+ entries) |
| Editable classes | No (AD&D 1e hardcoded) | Yes — classes.txt database |
| Editable races | No | Yes — races.txt database |
| Editable special abilities | No | Yes — 200+ special abilities with scripting |
| Items in database | ~fixed pool from FRUA | 400+ |
| Spells in database | ~fixed pool | 125+ |
| Levels per design | Limited (more dungeons needed workarounds) | Increased (UNKNOWN exact number) |
| Event/quest cap | ~100 events, ~8500 chars per map; 44 quests | "No FRUA event/special item/quest limitation" (unlimited) |
| Scripted dialogue | Simple text choice | Ultima-style scripted conversations |
| Map walls at runtime | Static | Can be altered at runtime |
| Auto-map | Fixed behaviour | Configurable (auto, full-reveal, hidden) |
| Standalone play | Requires CKIT.EXE | Self-contained game EXE per design |
| Databases shared | Global (pollute other designs) | Per-design, exportable/importable |
| FRUA module import | N/A | Partial: "Vanilla" FRUA .DSN imports supported; art requires DC-format art packs (FRUA art not imported directly) |

### 2.3 Dungeon Craft Data Model [W: SourceForge, GitHub repos]

A DC design is a `.dsn` **folder** (not a single file) containing:

```
MyDesign.dsn/
  Data/
    *.dat         — game settings / level data
    *.lvl         — level/map files
    *.txt         — database text files (monsters.txt, items.txt, spells.txt,
                    classes.txt, races.txt, ability.txt, baseclass.txt,
                    specialAbilities.txt)
  Resources/
    (art and audio files — BMP/PNG/PCX/WAV/MP3/etc.)
  DungeonCraft_*.exe   — self-contained game binary bundled with release
```

Default databases (per design, editor-importable):
`monsters.txt`, `items.txt`, `spells.txt`, `classes.txt`, `races.txt`, `ability.txt`,
`baseclass.txt`, `specialAbilities.txt`. [V: github.com/manikus/default_databases_for_Dungeon_Craft]

The default databases emulate FRUA + AD&D 1e, but are fully replaceable. They are
licensed AGPL-3.0 (the databases repo), not GPL-2.0 like the engine. [W: GitHub]

### 2.4 Architecture Notes [W: GitHub grannypron/uaf]

- Engine binary: `UAFWin.exe` (game runtime)
- Editor binary: `UAFWinEd.exe` (dungeon editor)
- Browser port: `WebGLBuild/` — Unity-based WebGL version (in same repo)
- Source: `src/` — C++ codebase

The hard-coded limit raises in DC are described as "many of the hard-coded limits have
been increased" — exact new ceiling values are **UNKNOWN** (would require reading DC
source or its help documentation). [W: GitHub README]

### 2.5 Special Abilities Scripting [W: SourceForge features page, DC help]

DC's "Event Attributes" system adds conditional menus and branching. Special abilities
include: dragon breaths, damage/weapon immunities, teleportation, invisibility, blink,
fear, level drain. Shop quantities and tavern tale counts are configurable up to 255.
Scripted conversations ("Ultima style") replace the flat text-choice events of FRUA.

### 2.6 License Analysis for Reuse

- Engine: **GPL-2.0** — copyleft. Reusing DC engine code would require our engine to also
  be GPL-2.0. That conflicts with any commercial or source-closed path.
- Databases (AGPL-3.0): even stricter copyleft.
- **Verdict:** DC is valuable as a **reference / prior art** — we can study its design
  decisions, data formats, and feature set without copying code. We should **not** fork or
  incorporate DC source code unless we commit to GPL-2.0 for our entire engine.
- DC's data model (folder-based `.dsn`, text databases) is a good **conceptual template**
  — we should design a compatible-but-independent format.

---

## 3. Feature/Limits Summary Table

| Feature | FRUA (1993) | Dungeon Craft (2024) | Our target ("broader") |
|---|---|---|---|
| Ruleset | AD&D 1e only (baked in) | AD&D 1e (data-driven, swappable) | Multiple rulesets as plug-ins |
| Map grid | Up to 576 tiles (≈24×24) | Larger (UNKNOWN exact) | Configurable; no hardcoded ceiling |
| Maps per design | 40 (36 dungeon + 4 overland) | More (UNKNOWN exact) | Unlimited |
| Events per map | ~100, ~8500 chars | Unlimited | Unlimited |
| Quests | 44 | Unlimited | Unlimited |
| Special items | 12 | Unlimited | Unlimited |
| Monsters (slots) | 127 (84 icon + NPC sub-slots) | 125+ in editable DB | Unlimited |
| Items | Fixed pool | 400+ | Unlimited |
| Spells | Fixed (new ones locked) | 125+ | Unlimited |
| Classes | 6 (AD&D 1e) | Editable | Unlimited, ruleset-defined |
| Races | 7 (AD&D 1e) | Editable | Unlimited, ruleset-defined |
| Art colour depth | 8-bit, 256 colours | 16/24/32-bit | 32-bit + HDR for modern modes |
| Art resolution | 320×200 | 640×480 to 1024×768 | Arbitrary (asset-independent) |
| Art formats | PCX/LBM | BMP/PCX/TGA/PNG/JPEG/AVI | PNG/WebP/SVG + original containers |
| Audio formats | XMI/VOC | WAV/MIDI/MP3/MOD/AVI | OGG/WAV/MP3 + XMI for authenticity |
| Scripting | None (event chain only) | Ultima-style scripted dialogue | Full scripting language (TBD) |
| Custom rulesets | No | Partial (data-driven AD&D 1e base) | Full plug-in rulesets |
| Multi-platform | DOS only | Windows only | Browser + Electron (Linux/Mac/Win) |
| Self-contained design | No (needs CKIT.EXE) | Yes (bundled EXE) | Yes (web URL or Electron) |
| FRUA import | N/A | Partial ("Vanilla" only) | Full (DAX/TLB/DAA decoded natively) |

---

## 4. Other Relevant Tools / Efforts

| Tool | What It Is | Relevance |
|---|---|---|
| **UASHELL** | Windows utility to apply CKIT.EXE byte-hacks; umbrella for all community patches | Reference for which limits community most wanted removed |
| **IceBlink Engine** | Cross-platform (PC/Android/iOS) Gold-Box-inspired RPG engine + authoring tool; turn-based combat + Storm-of-Zehir-style party chat | Independent active project; different design aesthetic (not FRUA-clone) |
| **Gold Box Companion (GBC)** | Windows overlay for all Gold Box games (including FRUA): automap, party HUD, journal, monster/item/spell DB, ECL tool, character editor, save editor | VISION.md names this as our "pillar 4" — fold GBC features into our engine as first-class panels |
| **FRUA community archives** | frua.rosedragon.org (modules), ua.reonis.com (forums + module list) | Module compatibility target for our FRUA-import path |
| **bsimser/frua** (GitHub) | Archive/mirror of FRUA resources | Reference only |

---

## 5. Broaden-It Opportunities for Our Engine

These are the top gaps our engine should exceed relative to both FRUA and DC:

1. **True multi-ruleset plug-in system.** Both FRUA and DC are architecturally married to
   AD&D 1e (DC made the data configurable but didn't abstract the rule engine). We should
   define a ruleset API: class definitions, spell resolution, combat math, XP tables,
   race/level-limit tables — all external to the core engine. This enables Dragonlance 2e,
   Buck Rogers Alternity, or custom systems.

2. **No hardcoded asset ceilings.** FRUA's slot-numbered TLB naming (BIGP0240.TLB → max
   255) and fixed GEO count (40 maps) should be replaced by content-addressed asset refs
   and unlimited map lists. Our manifest + HLIB decoder already points in this direction.

3. **Modern scripting layer.** FRUA's event chain has 35+ node types but no variables,
   no real branching, no arithmetic — complex plots require exploiting quest/special-item
   flags as ad-hoc boolean state. DC adds dialogue scripting but no general-purpose
   language. We need: local/global variables, arithmetic, proper conditionals, loops, and
   hook points for custom ruleset code.

4. **Live graphics-mode switching (our unique feature).** Neither FRUA nor DC can serve
   multiple graphical variants of the same logical asset. Our engine's asset-resolver model
   (DOS EGA ↔ Amiga ↔ VGA graphic sets, selectable at runtime) is novel in this space.

5. **Full combat win/loss detection in events.** FRUA cannot distinguish "combat won" from
   "combat fled." This was a widely cited design constraint. Our event system must expose
   combat outcome as a proper trigger condition.

6. **Browser + offline Electron delivery.** FRUA required DOS; DC requires Windows. We
   target browser-first + Electron, making designs instantly shareable as URLs.

7. **Richer NPC model.** FRUA NPCs were re-skinned monsters that fled when re-statted. A
   proper NPC entity (portrait, dialogue tree, inventory, faction, quest state) is a major
   quality-of-life improvement for module authors.

8. **Original-format import breadth.** DC imports "Vanilla" FRUA modules only (no art, no
   hacked designs). Our native DAX/DAA/TLB decoders enable full FRUA module + art import
   and potential conversion to our format.

---

## 6. Implications for Our Engine

- **Data model to adopt:** Folder-based design package (like DC's `.dsn`), but JSON-
  manifest + PNG assets + our HLIB/DAX binary blobs where original fidelity is needed.
  Text databases for monsters/items/spells/classes/races (DC proved this works).
- **Do not fork DC:** GPL-2.0 copyleft precludes mixing with our codebase unless we go
  fully open-source GPL. Learn from it; build independently.
- **ECL event types are our baseline:** the 35+ FRUA event types form the MVP authoring
  vocabulary. Port all of them; add scripting hooks on top.
- **Ruleset API design is the hardest problem:** DC shows it's possible to data-drive
  AD&D 1e; the leap to a pluggable multi-system API is architectural and has no prior art
  in this genre.
- **GBC fold-in:** the Gold Box Companion already documents the monster/item/spell database
  schema for all Gold Box titles — use its data as the seed for our in-engine databases.

---

## 7. Open Questions / Unknowns

| Question | Status |
|---|---|
| Exact DC limits for maps/events/monsters after raising FRUA ceilings | UNKNOWN — requires reading DC C++ source or help files |
| Exact pixel dimensions of FRUA combat icons (CPIC/CBODY) and sprites (SPRIT) | UNKNOWN — hackdocs describe encoding but not exact W×H |
| Whether DC's WebGL/Unity build is feature-complete vs Windows build | UNKNOWN |
| IceBlink license and whether its scripting model is a better reference than DC | Partially researched — needs dedicated pass |
| Exact FRUA map grid dimensions (is it truly 24×24 or can it be non-square up to 576?) | Partially confirmed: up to 576 tiles, min 11×11, can be non-square (e.g. 19×19, 20×28) [W: CRPG Addict] |
| Community consensus on the most-wanted features beyond what UASHELL gave | UNKNOWN — would require forum crawl |
| Whether frua.rosedragon.org module archive is still live and downloadable | UNKNOWN |
| FRUA Mac version differences (art files differ per UAFILES.TXT note) | UNKNOWN — low priority |

---

## Sources

- `hackdocs_extracted/UAFILES.TXT` — UA file structure (David Knott, local)
- `hackdocs_extracted/UAGIVENS.TXT` — unchangeable FRUA constraints (David Knott, local)
- `hackdocs_extracted/SLOTSFRM.TXT` — slot counts per design (local)
- `hackdocs_extracted/UATFILES.TXT` — imported art naming/limits (Dan Autery, local)
- `hackdocs_extracted/UAPALETT.TXT` — palette layout (local)
- `hackdocs_extracted/DRAW18.TXT` — TLB draw method 18 spec (Dan Autery, local)
- `hackdocs_extracted/DRAW23.TXT` — TLB draw method 23 spec (Dan Autery, local)
- `hackdocs_extracted/MONSTICN.TXT` — monster/icon reference list (local)
- `hackdocs_extracted/FRM_DESC.TXT` — UI frame images (John Rudy, local)
- `hackdocs_extracted/CKITFORM.TXT` — CKIT.EXE byte-offset hacking guide (local)
- Wikipedia — [Forgotten Realms: Unlimited Adventures](https://en.wikipedia.org/wiki/Forgotten_Realms:_Unlimited_Adventures)
- FRUA community FAQ — [groups.google.com](https://groups.google.com/g/comp.sys.mac.games/c/J4v_RLw6aCU)
- CRPG Addict blog — [Game 538: Unlimited Adventures (1993)](http://crpgaddict.blogspot.com/2025/02/game-538-unlimited-adventures-1993.html)
- Dungeon Craft home — [uaf.sourceforge.net](https://uaf.sourceforge.net/)
- Dungeon Craft features — [uaf.sourceforge.net/features.html](https://uaf.sourceforge.net/features.html)
- Dungeon Craft about — [uaf.sourceforge.net/about.html](https://uaf.sourceforge.net/about.html)
- Dungeon Craft SourceForge — [sourceforge.net/projects/uaf/](https://sourceforge.net/projects/uaf/)
- Dungeon Craft GitHub — [github.com/grannypron/uaf](https://github.com/grannypron/uaf)
- DC default databases — [github.com/manikus/default_databases_for_Dungeon_Craft](https://github.com/manikus/default_databases_for_Dungeon_Craft)
- GOG FRUA forum — [gog.com/forum/forgotten_realms_collection/...](https://www.gog.com/forum/forgotten_realms_collection/an_introduction_to_forgotten_realms_unlimited_adventures_frua_with_essential_links/page1)
- Gold Box Companion — [gbc.zorbus.net](https://gbc.zorbus.net/)
