# Gold Box Game Roster & Engine Generations

**Research by:** goldbox-researcher agent
**Date:** 2026-06-21
**Status:** Initial research pass — web + local hackdocs synthesis.

---

## 1. Master Table — All Gold Box Titles

> "Gold Box" means games that share SSI's Gold Box engine lineage. Dark Sun and
> Ravenloft are excluded (different engines entirely — see §4). Neverwinter Nights
> AOL is included as a heavily-modified Gold Box derivative; Spelljammer is
> included but is C-based (not Pascal), so it is a cousin rather than a core target.

| Game | Year | Developer | Ruleset | Engine Gen | Container Format(s) | Display | Platforms |
|------|------|-----------|---------|------------|---------------------|---------|-----------|
| Pool of Radiance (PoR) | 1988 | SSI | AD&D 1e | Gen 1 (Pascal) | DAX | CGA/EGA | DOS, C64, Amiga, Apple II, Atari ST, Mac, NES, PC-98 |
| Curse of the Azure Bonds (CoAB) | 1989 | SSI | AD&D 1e | Gen 1 (Pascal) | DAX | CGA/EGA | DOS, C64, Amiga, Apple II, Atari ST, Mac |
| Champions of Krynn (CoK) | 1990 | SSI | AD&D 1e (Dragonlance) | Gen 1 (Pascal) | DAX (DOS); DAA (Amiga) | EGA | DOS, Amiga, C64 |
| Buck Rogers: Countdown to Doomsday (BR1) | 1990 | SSI | Buck Rogers XXVc (~AD&D 2e sci-fi) | Gen 1 (Pascal) | DAX | EGA | DOS, Amiga, C64, Genesis |
| Secret of the Silver Blades (SotSB) | 1990 | SSI | AD&D 1e | Gen 1 (Pascal) | DAX | CGA/EGA | DOS, C64, Amiga, Mac |
| Gateway to the Savage Frontier (GatSF) | 1991 | Beyond Software (Stormfront) | AD&D 2e | Gen 1-VGA (Pascal+VGA) | DAX | **VGA (first in series)** | DOS, Amiga, C64 |
| Death Knights of Krynn (DoK) | 1991 | SSI | AD&D 1e (Dragonlance) | Gen 1 (Pascal) | DAX (DOS); DAA (Amiga) | EGA | DOS, Amiga |
| Neverwinter Nights AOL (NWN) | 1991 | Beyond Software / AOL | AD&D 1e | Gen 1 (Pascal, online mod) | DAX (inferred) | EGA/VGA (UNKNOWN) | DOS (AOL only) |
| Pools of Darkness (PoD) | 1991 | SSI | AD&D 1e | Gen 1-VGA (Pascal+VGA) | DAX | **VGA** | DOS, Amiga, Mac, PC-98 |
| Buck Rogers: Matrix Cubed (BR2) | 1992 | SSI | Buck Rogers XXVc | Gen 1 (Pascal) | DAX | EGA/VGA (UNKNOWN) | DOS |
| Spelljammer: Pirates of Realmspace | 1992 | Cybertech Systems | AD&D 2e (custom) | **C (not Pascal)** | UNKNOWN | VGA 256-color | DOS only |
| Treasures of the Savage Frontier (TrSF) | 1992 | Beyond Software (Stormfront) | AD&D 2e | Gen 1-VGA (Pascal+VGA) | DAX | VGA | DOS, Amiga |
| The Dark Queen of Krynn (DQK) | 1992 | MicroMagic | AD&D 1e (Dragonlance) | **Gen 2 (C++)** | **HLIB / TLB / GLB** | **VGA 256-color** | DOS, Mac |
| Forgotten Realms: Unlimited Adventures (FRUA) | 1993 | MicroMagic | AD&D (1e base, designer-variable) | **Gen 2 (C++)** | **HLIB / TLB / GLB** | VGA | DOS, Mac |

### Notes on inclusion boundaries
- **Dark Sun: Shattered Lands** (1993) and **Dark Sun: Wake of the Ravager** (1994): different
  engine ("Dark Sun engine"), developed by a different team. *Not Gold Box.*
  Source: [Dark Sun engine — Gold Box Fandom wiki](https://goldbox.fandom.com/wiki/Dark_Sun_(engine))
- **Ravenloft: Strahd's Possession** (1994): DreamForge Intertainment, early 3D engine.
  *Not Gold Box.* Source: [Wikipedia](https://en.wikipedia.org/wiki/Ravenloft:_Strahd%27s_Possession)
- **Hillsfar** (1989): Different engine, character import only. *Not Gold Box.*
- **Order of the Griffon** (1992, TurboGrafx): turn-based combat similar but separate engine.
- **NES Pool of Radiance**: developed by Marionette (Japan), separate engine.

---

## 2. Engine Generations — What Differs

### Gen 1 — Pascal (Pool of Radiance family, 1988–1992)

The original Gold Box engine was designed for the Commodore 64 in 6502 assembly, then
ported to DOS in Pascal. Nearly all games through 1992 are in this generation.

**Key sub-milestones within Gen 1:**

| Milestone | First game | What changed |
|-----------|-----------|--------------|
| Launch | Pool of Radiance (1988) | Baseline: DAX containers, EGA 16-color, grid combat, ECL scripting, character import/export |
| Overland map | Champions of Krynn (1990) / Gateway (1991) | External travel layer added (not in PoR/CoAB) |
| VGA 256-color | Gateway to the Savage Frontier (1991) | First VGA Gold Box; still DAX containers |
| VGA Forgotten Realms | Pools of Darkness (1991) | VGA + DAX; adds multi-planar travel, clone mechanic |
| Savage Frontier enhancements | Treasures of the Savage Frontier (1992) | Weather system, NPC emotions, best VGA in Gen 1 |
| Online multi-user | Neverwinter Nights AOL (1991) | Heavy engine adaptation; 300 bps modem constraints; not released retail |

**Container format:** All Gen 1 DOS releases use **DAX** archives.
Structure: little-endian `uint16 entry_count`, `uint16 table_offset`, TOC, then
signed-byte-RLE block data. No embedded palette (fixed EGA-16; or VGA loaded separately).
Source: local `docs/dax-format.md` (verified by byte inspection); community forum
[forums.goldbox.games topic 1073](https://forums.goldbox.games/index.php?topic=1073.0).

**Amiga counterpart:** DAA archives (big-endian DAX cousin). Used by CoK and DoK on
Amiga. Format now decoded; see `docs/daa-format.md`.

**C64/Apple II:** 6502 assembly; different asset format entirely. Not a priority target
for this engine but the ECL event model is the same conceptually.

**ECL scripting (Gen 1):** Embedded in DAX blocks; event bytecode interpreted at runtime
by the Pascal engine. ECL opcodes tie to hardcoded engine memory addresses; cross-game
compatibility within Gen 1 is partial but not universal — CoAB and GatSF are confirmed
compatible; CoK and DoK share a format; PoR is the common ancestor.
Source: [forums.goldbox.games topic 3079](https://forums.goldbox.games/index.php?topic=3079.0).

**Ruleset:**
- Most Gen 1 games use **AD&D 1st Edition** rules: class level limits for demihumans,
  slower THAC0 progression (Fighters improve every 2 levels), pay-to-train level-up,
  no non-weapon proficiencies as core system.
- Gateway and Treasures of the Savage Frontier use **AD&D 2nd Edition** rules:
  faster THAC0 progression, proficiency system more prominent, updated spell lists.
  Source: [CRPG Addict / GOG community threads](https://www.gog.com/forum/forgotten_realms_collection/re_the_whole_gold_box_collection_games).
- Krynn trilogy (CoK/DoK/DQK) adapts AD&D 1e to **Dragonlance** setting rules:
  no Paladins (replaced by Solamnic Knights), Kender/Tinker Gnome races, lunar magic
  cycle for mages, god-granted cleric specials, Draconians as monsters.
- Buck Rogers games adapt AD&D to **Buck Rogers XXVc** sci-fi ruleset (~AD&D 2e
  base): classes become careers (Warrior/Rogue/Medic/etc), spells become psionics,
  zero-gravity combat added. Not AD&D-licensed.
  Source: [Wayne's Books blog](https://waynesbooks.games/2025/11/30/buck-rogers-xxvc-gamma-world-4e-add-2e-sci-fi-prototype-d20/).

### Gen 2 — C++ / HLIB (MicroMagic, 1992–1993)

MicroMagic rewrote the Gold Box engine in C++ for The Dark Queen of Krynn (1992) and
then used the same codebase for Forgotten Realms: Unlimited Adventures (1993). This is the
"newest Gold Box engine" targeted by this project.

**Container format:** **HLIB "DataLib"** — `.TLB` and `.GLB` files.
Magic bytes `HLIB` at offset 0, followed by typed sub-chunks:
- `TILE`: graphics frames with **embedded 6-bit-DAC VGA palette** (RGB values stored
  as 3-byte triples, palette count at offset, color cycling supported)
- `DATA`: ECL scripts, GEO maps, generic data; also used in `.GLB` "master library" files
- `DIG4`: digitized sound
`.GLB` files (e.g. `ECL.GLB`, `GEO.GLB`, `SOUNDS.GLB`) are master library variants with
larger index tables.
Source: local `docs/hlib-format.md` + `hackdocs_extracted/TLBFORM.TXT` (detailed spec).

**ECL scripting (Gen 2):** Stored in `DATA` chunks within GLB files. The SCRIPT.GLB
file defines the editing form schema. Event types include: Combat, Text, Give Treasure,
Damage, Stairs, Training Hall, Tavern, Shop, Temple, Question variants, Transfer Module,
Guided Tour, Add/Remove NPC, Encounter, Utilities, Sounds, Vault, Gain XP, Pass Time,
Camp, Teleporter, Quest Stage, Tavern Tales, Special Items (38 total event types).
See `hackdocs_extracted/SCRIPT.TXT`.

**Key differences from Gen 1:**
- TLB/GLB replaces DAX entirely
- Embedded VGA palette per tile file (Gen 1 EGA had no palette in file)
- C++ engine (not Pascal) — different memory layout, different hardcoded addresses
- FRUA/DQK engine is what the community calls "the Gold Box engine" for modding/creation
- The Gold Box Companion tool notes TLButil2 for TLB extraction vs DAXDump for DAX —
  separate extraction toolchains needed
  Source: [gbc.zorbus.net](https://gbc.zorbus.net/)

### Spelljammer — Isolated C branch (1992)

Spelljammer: Pirates of Realmspace (Cybertech Systems, 1992) was **written in C (not
Pascal), except graphic primitives in x86 assembly**. It uses a custom engine for
semi-3D ship combat layered over turn-based melee. File formats are UNKNOWN (not DAX,
not HLIB; not documented in hackdocs). VGA 256-color, 320×200.
**Assessment:** Spelljammer is a Gold Box *cousin* but not a first-class target for
this engine — its unique ship combat requires separate architecture.
Source: [Wikipedia — Spelljammer: Pirates of Realmspace](https://en.wikipedia.org/wiki/Spelljammer:_Pirates_of_Realmspace).

---

## 3. Platform Matrix

| Platform | Games ported (known) | Notes |
|----------|---------------------|-------|
| MS-DOS | All 14 titles | Primary platform for this engine project |
| Amiga | PoR, CoAB, CoK, DoK, BR1, GatSF, TrSF, PoD (partial) | 32-color Amiga OCS/ECS; DAA containers; best art for CoK/DoK |
| Commodore 64 | PoR, CoAB, SotSB, CoK, BR1, GatSF | 6502 assembly; 16-color; not a format target |
| Apple II | PoR, CoAB, SotSB | 6502 assembly; very limited color |
| Atari ST | PoR, CoAB | Limited ports; same era as Amiga |
| Macintosh | PoR, CoAB, SotSB, PoD, DQK, FRUA | Mac DQK = peer to DOS VGA (256-color); CTL art format (IFF-derived) |
| NES / Famicom | PoR only | Japanese developer Marionette; separate engine |
| Sega Genesis | BR1 only | Console port; separate engine |
| PC-98 | PoR, PoD | Japanese market; format unknown |
| AOL online | NWN only | Dial-up modem; not a distribution target |

---

## 4. ECL Opcode Drift — What We Know

The ECL (Event Control Language / scripting bytecode) interpreter is embedded in the
main game executable. Opcodes are tied to hardcoded memory addresses, which differ
between:

1. **PoR family (Gen 1 core):** PoR, CoAB — confirmed compatible with each other;
   the COAB reimplementation by Simeon Pilgrim is the primary reference implementation.
   ECL is stored in DAX blocks.
2. **Krynn Gen 1 sub-family:** CoK and DoK share ECL formats; distinct from PoR family.
3. **Savage Frontier sub-family (Gen 1-VGA):** GatSF and TrSF — likely compatible with
   each other; VGA engine variant; some overlap with PoR family structure.
4. **Gen 2 (MicroMagic C++):** DQK and FRUA — ECL stored in `DATA` chunks in GLB
   files; different opcode set; 38 documented event types in SCRIPT.GLB schema.

**Known cross-family incompatibilities:**
- "Dark Queen of Krynn uses a different GLB format (like FRUA)" vs. older DAX games.
- ECL memory addresses differ between Pascal (Gen 1) and C++ (Gen 2) — any cross-port
  of CoK/DoK content onto DQK requires opcode translation.
- Specific opcode tables have **not been publicly enumerated** in searchable form
  (the hackdocs OPCODES.TXT covers x86 CPU opcodes for CKIT.EXE hacking, not ECL
  bytecode). The Simeon Pilgrim COAB GitHub source (`engine/ovr008.cs`) is the best
  available reference for Gen 1 ECL VM internals.

**UNKNOWN:** Whether Savage Frontier and PoR families share the same ECL opcode table.
**UNKNOWN:** Full ECL opcode enumeration for any game (no public doc found).

Source: [forums.goldbox.games topic 3079](https://forums.goldbox.games/index.php?topic=3079.0),
[simeonpilgrim/coab on GitHub](https://github.com/simeonpilgrim/coab/blob/master/engine/ovr008.cs).

---

## 5. Rulesets — Engine Abstraction Requirements

The engine must be able to load **per-game ruleset configs** that define:

| Ruleset | Games | Key differences from baseline |
|---------|-------|-------------------------------|
| AD&D 1e (FR baseline) | PoR, CoAB, SotSB, PoD, CoK, DoK, DQK | Non-human level limits; pay-to-train; slower THAC0 progression; no NWP core |
| AD&D 1e + Dragonlance | CoK, DoK, DQK | Solamnic Knight class; Kender/Tinker Gnome; lunar magic (Lunitari/Solinari/Nuitari); Draconian monsters; god-specific cleric abilities |
| AD&D 2e (Savage Frontier) | GatSF, TrSF | Faster THAC0; NWP system; updated bard; no assassin/monk core |
| Buck Rogers XXVc | BR1, BR2 | Non-AD&D license; sci-fi classes (Warrior, Rogue, Medic, Rocketjock, Engineer, Tinker, Scoundrel); psionics-as-spells; zero-G combat; no clerics/mages |

---

## 6. What the Gold Box Companion Supports

The Gold Box Companion tool (gbc.zorbus.net) supports:
- Pool of Radiance, Curse of the Azure Bonds, Secret of the Silver Blades, Pools of Darkness
- Champions of Krynn, Death Knights of Krynn, The Dark Queen of Krynn
- Gateway to the Savage Frontier, Treasures of the Savage Frontier
- Forgotten Realms: Unlimited Adventures
- Buck Rogers: Countdown to Doomsday, Buck Rogers: Matrix Cubed

Uses DAXDump/ECLDump for Gen 1 extraction; TLButil2 for Gen 2 (DQK/FRUA).
**Not supported:** Spelljammer, Neverwinter Nights AOL.

---

## 7. Open Questions / Unknowns

1. **Spelljammer container format:** What file format does it use? Not DAX, not HLIB.
   No documentation found. (Low priority — Spelljammer is not in scope.)
2. **Neverwinter Nights AOL asset format:** How were assets distributed over dial-up?
   What container format? How was the engine modified for multiplayer? UNKNOWN.
3. **ECL opcode tables per game:** No public enumeration of the bytecode instruction set
   for any game found. Must be extracted from disassembly or from the COAB source.
   This is the **highest-risk gap** for the CoK/DoK → DQK port.
4. **Buck Rogers Matrix Cubed (BR2) display mode:** EGA or VGA? No confirmation found.
5. **Amiga DQK assets (Disk 3):** Disk 3 is unrecoverable from our ADF set.
   Low priority (we use DOS VGA for DQK).
6. **PC-98 asset formats:** Gold Box PC-98 versions (PoR, PoD) may use custom formats.
   Not investigated. Low priority for this engine.
7. **Apple II / Atari ST asset formats:** 6502 assembly ports; format presumably different
   from DOS DAX. Low priority but affects "any platform" asset-switching feature.
8. **Mac CTL format:** Mac versions of DQK noted to be a "peer to DOS VGA" in color.
   The Mac uses CTL (IFF-derived) art containers per FRUA hackdocs reference. Medium
   priority if we want Mac-quality DQK art as an alternative graphic set.
9. **Savage Frontier ECL compatibility with PoR family:** Confirmed that GatSF/TrSF use
   the same Pascal engine lineage, but whether their ECL opcode tables are identical to
   PoR/CoAB is unconfirmed.
10. **Order of character creation mechanics changes:** When exactly did the engine add
    the FIX command, automatic spell-list memory, and unified MOVE/AIM input? Per-game
    confirmation needed for mechanic completeness flags.

---

## 8. Implications for Our Engine

1. **Two container formats are the primary target:** DAX (Gen 1, 12 games) and
   HLIB/TLB/GLB (Gen 2, DQK + FRUA). Both are now decoded (`tools/dax_decode.py`,
   `tools/hlib_decode.py`). The engine loader layer needs two reader paths.

2. **The Gen 2 C++ engine (DQK/FRUA) is the rebuild target.** All three Krynn games
   will run on this engine. Writing HLIB `TILE`/`DATA` chunks is already planned.

3. **ECL is the hardest abstraction.** Four known ECL sub-dialects (PoR, Krynn-Gen1,
   Savage Frontier-VGA, DQK-Gen2). The VM must be parameterizable per game, with opcode
   tables loaded from config rather than hardcoded. The COAB reimplementation
   (`github.com/simeonpilgrim/coab`) is the best prior-art reference for Gen 1 ECL VM
   construction.

4. **Ruleset abstraction:** Four distinct rulesets (AD&D 1e, AD&D 1e + Dragonlance,
   AD&D 2e Savage Frontier, Buck Rogers XXVc). The engine needs a ruleset plugin model
   to handle class sets, THAC0 tables, level limits, and spell/psionic systems per game.

5. **Graphics-mode switching requires two palette models:** Gen 1 uses EGA-16 fixed
   palette (no file-embedded palette; palette is engine-side); Gen 2 HLIB TILE embeds
   a 6-bit-DAC VGA palette per file. The asset model must handle both at load time.

6. **Spelljammer, NWN AOL, and console ports are out of scope for v1** — they require
   separate engine branches or too many unknowns. Plan as stretch goals only.

7. **Beyond Software games (GatSF, TrSF, NWN)** use the Pascal Gold Box engine but
   with AD&D 2e rules and VGA. They are Gen 1-VGA and should load with the DAX reader
   plus the 2e ruleset config. No special engine branch needed.

---

## Sources

- Wikipedia — Gold Box: https://en.wikipedia.org/wiki/Gold_Box
- Wikipedia — Pool of Radiance series: https://en.wikipedia.org/wiki/Pool_of_Radiance_(series)
- Wikipedia — Spelljammer: Pirates of Realmspace: https://en.wikipedia.org/wiki/Spelljammer:_Pirates_of_Realmspace
- Wikipedia — Forgotten Realms: Unlimited Adventures: https://en.wikipedia.org/wiki/Forgotten_Realms:_Unlimited_Adventures
- Wikipedia — Neverwinter Nights (1991): https://en.wikipedia.org/wiki/Neverwinter_Nights_(1991_video_game)
- Wikipedia — Ravenloft: Strahd's Possession: https://en.wikipedia.org/wiki/Ravenloft:_Strahd%27s_Possession
- Wikipedia — Buck Rogers XXVC: https://en.wikipedia.org/wiki/Buck_Rogers_XXVC
- Gold Box Games Forums (engine hacking discussion): https://forums.goldbox.games/index.php?topic=3079.0
- Gold Box Games Forums (DAX format): https://forums.goldbox.games/index.php?topic=1073.0
- Simeon Pilgrim — Gold Box Cheat Codes (engine generation notes): https://simeonpilgrim.com/blog/2010/07/21/gold-box-games-cheat-codes/
- Simeon Pilgrim — COAB engine source (Gen 1 ECL VM): https://github.com/simeonpilgrim/coab/blob/master/engine/ovr008.cs
- Gold Box Companion (supported games, tool list): https://gbc.zorbus.net/
- Dark Sun engine (non-Gold Box): https://goldbox.fandom.com/wiki/Dark_Sun_(engine)
- Wayne's Books (Buck Rogers XXVc ruleset): https://waynesbooks.games/2025/11/30/buck-rogers-xxvc-gamma-world-4e-add-2e-sci-fi-prototype-d20/
- Local: `hackdocs_extracted/TLBFORM.TXT` — HLIB/TLB format spec (community docs)
- Local: `hackdocs_extracted/SCRIPT.TXT` — SCRIPT.GLB / ECL event type schema
- Local: `docs/dax-format.md`, `docs/daa-format.md`, `docs/hlib-format.md` — verified decoders
- Local: `docs/findings.md` — authoritative Krynn-specific format verdicts
