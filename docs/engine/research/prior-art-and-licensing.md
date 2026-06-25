# Prior Art & Licensing — Gold Box All-in-One Engine

**Research date:** 2026-06-21
**Scope:** Open-source Gold Box reimplementations / decompilations; ECL bytecode VM references;
tools and format docs we should reuse; licensing / legal landscape for a public release.
**Status of claims:** URLs cited inline; inferred conclusions labeled **(inferred)**;
unknowns labeled **UNKNOWN**.

---

## 1. Prior-Art Inventory

### Summary table

| Project | URL | License | Language | What it solves | Reuse value |
|---|---|---|---|---|---|
| **COAB — Curse of the Azure Bonds reimplementation** (Simeon Pilgrim) | https://github.com/simeonpilgrim/coab | **UNKNOWN** — no LICENSE file in repo | C (60%) + C# (40%) | Near-complete reimplementation of CoAB: game logic, combat, rendering — translated from 8086 Pascal-compiled assembly via IDA Pro + memory-dump technique. Also authored DaxDump/EclDump. | **Highest** — the de-facto Gold Box engine reference; ECL decryption credit belongs here |
| **Gold Box Explorer** (bsimser) | https://github.com/bsimser/Gold-Box-Explorer | **MIT** | C# | Batch DAX/TLB read + PNG export; covers all SSI Gold Box titles including Unlimited Adventures files | Medium — read-only extractor; good format reference for C# paths; MIT means free reuse |
| **Gold Box Explorer fork** (simeonpilgrim) | https://github.com/simeonpilgrim/goldboxexplorer | Unknown (fork of MIT original) | C# | Fork of v1.2; includes ECL display feature with Pilgrim's ECL decryption code | Medium — adds ECL content display on top of bsimser's base |
| **Gold Box Companion** (jhirvonen / Joonas Hirvonen) | https://gbc.zorbus.net/ | **UNKNOWN** — closed source (inferred) | Unknown (binary distribution only) | HUD overlay, automapping, character/save editor, DAX+TLB extraction tools, **ECL-Monitor** (live script tracing + flag editing), ECL-Tool (bytecode browser/editor) | High reference value for ECL semantics; cannot directly reuse code (closed source) |
| **ECL-Monitor** (jhirvonen, part of GBC) | https://forums.goldbox.games/index.php?topic=4110.0 | Part of GBC — UNKNOWN | Part of GBC | Live PC tracing through ECL scripts during gameplay; flag monitoring and editing; demonstrates GBC parses and interprets ECL byte-by-byte | High for understanding ECL execution flow; no source code available |
| **Dungeon Craft / UAF** (grannypron et al.) | https://github.com/grannypron/uaf / https://uaf.sourceforge.net/ | **GPL-2.0** | C++ (97%) | Full FRUA/UA-engine reimplementation: scripting (GPDL), combat, maps, monsters/items/spells editors, 16/24/32-bit color, browser WebGL build | Medium-high — GPL-2.0 means any code reuse requires our engine to be GPL-2.0 or compatible; covers FRUA engine generation (HLIB/GLB), not the older DAX engine |
| **Dungeon Craft (GaidamakUA fork)** | https://github.com/GaidamakUA/DungeonCraft | Unknown (fork of GPL-2.0) | C++ | Same as above — updated emulator of FRUA | Low additional delta; inherits GPL requirement |
| **gold-box-editor** (robotlions) | https://github.com/robotlions/gold-box-editor | **UNKNOWN** | JavaScript/React | Web-based save-game file editor for Gold Box series | Low for engine; useful reference for save-game (SAVGAM) structure |
| **PoR_L10n** (grannypron) | https://github.com/grannypron/PoR_L10n | Unknown | Unknown | Localization patches for Pool of Radiance; involves string extraction from DAX files | Low — narrow scope |
| **goldbox-rpg** (opd-ai) | https://github.com/opd-ai/goldbox-rpg | **MIT** | Go + TypeScript | Modern RPG framework *inspired by* Gold Box design — NOT a reimplementation; no ECL, no original format support | Negligible for our purpose |
| **Pool of Radiance Amiga dev wiki** | http://amiga-dev.wikidot.com/project:pool-of-radiance | Unknown | N/A (wiki doc) | Early-stage reverse engineering documentation; data file format notes; item generation mechanics disassembled at machine code level | Low — incomplete, dormant |
| **Hackdocs** (community, 1990s–2003) | `hackdocs_extracted/` (local) | Community-produced; no explicit license stated | Plain text | Format specs for DAX, HLIB/TLB, GEO, MON, ITEM, SPELL, SAVGAM, ECL event form schemas (SCRIPT.GLB layout); x86 opcode table for CKIT.EXE hacking | **High** — the foundational reference corpus for everything in the FRUA/UA generation |
| **ScummVM** | https://www.scummvm.org/ | GPL-2.0+ | C++ | **No Gold Box engine support** — confirmed absent from supported game list as of v2026.1.0 | None for Gold Box; architecture model only |

---

## 2. ECL Bytecode VM — Reference Assessment

### What "ECL" means in Gold Box

ECL (Event Control Language) is the scripting layer embedded in every Gold Box game.
Scripts are stored as binary bytecode in:
- **DAX engine (CoK/DoK):** `ECL*.DAX` archive blocks
- **HLIB engine (DQK/FRUA):** `DATA` chunks inside `ECL.GLB` (confirmed by `SCRIPT.TXT`
  in hackdocs, which maps SCRIPT.GLB's 38 event-form types: Combat=1, Text=2,
  Treasure=3, Stairs=5, Shop=8, Encounter=15, Teleporter=34, etc.)

### What is publicly documented

| Source | Coverage | Completeness | Notes |
|---|---|---|---|
| `hackdocs_extracted/SCRIPT.TXT` | SCRIPT.GLB format — the *schema* of each event form (field offsets, types, ranges for Combat/Shop/Encounter etc.) | Good for FRUA/UA generation only | Documents the *editing form* structure, not the runtime VM dispatch table |
| `hackdocs_extracted/OPCODES.TXT` | **8086 x86 machine opcodes** (for CKIT.EXE binary patching) | Complete x86 ISA reference | NOT Gold Box ECL opcodes — this is the host CPU instruction set |
| `hackdocs_extracted/CKITBYTE.TXT` | CKIT.EXE binary patches (bytecode hacks to the FRUA editor executable) | Partial — example hacks only | Again x86 machine code patches, not ECL VM opcodes |
| `hackdocs_extracted/CKITFORM.TXT` | CKIT.EXE internal layout, spell/damage byte offsets | Partial, FRUA editor only | No ECL VM opcode table |
| GBC ECL-Monitor (jhirvonen) | Live PC tracing, flag monitoring during gameplay | Functional implementation (closed source) | Best proof that ECL is fully parseable; ECL-Tool in GBC is a bytecode browser |
| Simeon Pilgrim COAB + ECL display in goldboxexplorer | ECL decryption; displays ECL block content | Source available (C); covers CoAB engine generation | The ECL *decryption* scheme is in the COAB C source; the opcode semantic table is embedded in his C# reimplementation |
| forums.goldbox.games topic 1241 | ECL file content discussion | Incomplete — forum thread, not a spec | References Pilgrim's decryption code; no standalone opcode table published |
| Stephen S. Lee's FAQs | ECL opcode semantics (referenced by jhirvonen as the source for ECL-Monitor's parser) | UNKNOWN — could not locate the primary document; referenced second-hand | **Gap: these FAQs are cited but not found in hackdocs or web search** |

### Assessment

**The ECL VM opcode table is NOT fully documented in any single public text document.**

What exists:
- The COAB C# source (github.com/simeonpilgrim/coab) contains a working implementation
  of the CoAB (older DAX-engine) ECL interpreter — this is the **best available ECL VM
  reference** for the CoK/DoK engine generation. License is UNKNOWN (no LICENSE file),
  which creates a reuse risk (see §4).
- Dungeon Craft / UAF (GPL-2.0) implements the FRUA-generation GPDL scripting system,
  which is functionally analogous but architecturally distinct from binary ECL. It does
  NOT implement raw ECL bytecode interpretation.
- GBC's ECL-Tool + ECL-Monitor (closed source) implement a parser for both generations
  (inferred from forum posts), but source is unavailable.
- The hackdocs `SCRIPT.TXT` is the best *structural* reference for the HLIB/DQK
  generation's ECL event forms, but documents the editor schema, not the VM instruction
  dispatch.

**What must still be reversed:**
- Complete ECL opcode table for the DQK HLIB-engine generation (distinct from CoAB's).
- Opcode drift between CoK/DoK ECL and DQK ECL (the critical blocker for content porting).
- The HLIB `DATA` chunk layout for ECL scripts (separate from the editing form schema).

**Recommended approach:** Use COAB source (with license clarification) as the starting
reference for CoK/DoK engine opcodes; cross-reference GBC's ECL-Monitor behavior (via
dynamic analysis with GBC running against real game files) to fill gaps and extend to
the DQK generation.

---

## 3. Tools / Format References to Reuse

| Tool | What we get | How to use |
|---|---|---|
| `DaxDump.exe` + `EclDump.exe` (local, `daxdump_extracted/`) | Ground-truth DAX block extraction + ECL block dump | Already in repo; use as reference decoder and for byte-identical verification |
| `tools/dax_decode.py` (our own, verified) | Python DAX decoder | Already done; byte-identical to DaxDump output |
| `tools/daa_decode.py` (our own, verified) | Amiga DAA decoder | Already done; BIGPIC1 block 114 matches cross-platform |
| `tools/hlib_decode.py` (our own) | HLIB reader | Already started; extend for TILE/DATA chunk writing |
| Gold Box Explorer (MIT) | C# DAX/TLB read code + format knowledge | MIT — freely usable; port logic or study format handling |
| COAB source (C + C#, license TBD) | ECL interpreter logic, DAX parsing, combat math, save format | **Clarify license before use**; read for reference regardless |
| Hackdocs corpus (local) | GEO, MON, ITEM, SPELL, SAVGAM, VOCAB, GFX specs | No license stated; community-produced for reverse engineering; fair use for reference |
| GBC ECL-Tool / ECL-Monitor | ECL opcode behavior (dynamic tracing only, no source) | Run GBC against original game files, trace ECL execution to map opcodes empirically |
| Dungeon Craft / UAF (GPL-2.0) | FRUA scripting (GPDL), combat engine, map editor in C++ | **GPL-2.0: any C++ code excerpted forces our engine to GPL-2.0.** Study architecture; do not copy code unless willing to GPL the engine |
| forums.goldbox.games topics 1073, 1241, 3148, 4110 | DAX format, ECL contents, HLIB details, ECL-Monitor | Community research; cite as reference |

---

## 4. Legal Do / Don't for a Public Release

### The model: engine + user-supplied assets

The established legal pattern — used by ScummVM (GPL-2.0+), Dungeon Craft (GPL-2.0),
and DOSBox — is to **ship only the engine/tools; the user provides their own legally
purchased copy of the game data**. This is well-precedented and has not been successfully
challenged as of 2026.

ScummVM explicitly states it "replaces the original executable" and requires users to
supply game files from a legitimate source (https://docs.scummvm.org/en/latest/help/faq.html).
The project has operated under this model since 2001 with no successful legal action.

For our engine this means:
- The web player at `dionysus.dk/goldbox/` must gate all asset display behind a
  "point me at your install directory" flow for a public release. The demo currently
  serving the user's own assets locally is fine for private testing only.
- The Electron shell reads game files off disk — this is cleanly within the model.
- No original DAX/GLB/TLB/DAA asset files may be committed to the repo or hosted
  on any server.

### Copyright owners

| IP layer | Owner | Status |
|---|---|---|
| Gold Box game engine code / assets | Original SSI code; SSI was acquired, assets passed through several hands | **Wizards of the Coast / Hasbro** currently holds the re-release rights (evidenced by GOG/Steam re-releases in 2015/2022 via WotC licensing) |
| Forgotten Realms IP (PoR, CoAB, etc.) | **Wizards of the Coast / Hasbro** | Active trademark; used in GOG/Steam re-releases |
| Dragonlance IP (CoK, DoK, DQK) | **Wizards of the Coast / Hasbro** hold the D&D side; Dragonlance setting rights were subject to litigation (Weis/Hickman sued WotC 2020; settled); WotC retains game rights | Active trademark: DRAGONLANCE registered by WotC LLC |
| "Dungeons & Dragons", "D&D" | **Wizards of the Coast / Hasbro** | Active trademark |
| "Advanced Dungeons & Dragons", "AD&D" | **Wizards of the Coast / Hasbro** | Active trademark (used in Gold Box games) |
| "Gold Box" | Listed as WotC trademark (inferred from trademark databases) | Active; avoid using "Gold Box" as the name of our public product |

### Legal DO list

- Ship engine + authoring tools only; require user to point at their own game install.
- Reverse engineer formats for interoperability (standard "clean room" / compatibility
  exemption; well-established precedent — cited in ScummVM's own legal defense rationale).
- Reference community-produced format documentation (hackdocs) — these are in the public
  domain of knowledge; no copyright claim has been advanced against them.
- Use **MIT-licensed** Gold Box Explorer code freely (attribution required per MIT).
- Use **GPL-2.0** Dungeon Craft code only if the engine is also released GPL-2.0 or
  compatible; otherwise study it without copying source.
- Choose a permissive license (MIT/Apache 2.0) for engine core, or GPL-2.0 if happy to
  copyleft — both are fine so long as no SSI assets are bundled.
- Do not name the public product "Gold Box," "D&D," "Dungeons & Dragons," "Dragonlance,"
  "Forgotten Realms," or "AD&D" in the product name or logo without a license. Descriptive
  use in documentation ("plays Gold Box games") is generally acceptable fair use.

### Legal DON'T list

- **Do not ship any original game files** (DAX, GLB, TLB, DAA, EXE, IFF art, midi, etc.)
  in the repo, on the website, or as a download.
- **Do not auto-download game assets** from any server — including GOG/Steam cached
  installs — without the user explicitly triggering the copy from their own purchase.
- **Do not copy source code from COAB** until its license is clarified. Pilgrim's repo
  has no LICENSE file, meaning default copyright law applies — **all rights reserved**
  until/unless Pilgrim grants a license. Reach out to confirm before incorporating logic.
- **Do not use Dungeon Craft (GPL-2.0) source** in the engine if distributing under a
  non-GPL license. Study only.
- **Do not use trademarked names** (Dragonlance, Forgotten Realms, AD&D, D&D, Gold Box,
  SSI) in the product name, app store listings, or domain names.
- **Do not present the tool as a replacement for purchasing the games** — maintain clear
  messaging that the original games must be owned.
- **Do not redistribute the hackdocs ZIP** if it contains any copyrighted SSI text
  extracted from game files — the community docs themselves are community-authored, but
  verify no embedded game text was lifted verbatim.

### GPL implications if we reuse COAB or Dungeon Craft

- Dungeon Craft is GPL-2.0. Any C++ copied into our engine requires the engine to be
  GPL-2.0 (or GPLv3 if both parties agree). Our TypeScript engine core does not trigger
  GPL just from *studying* the C++ code, only from *copying* it.
- COAB has no stated license. If we want to port any of Pilgrim's logic: email him,
  request MIT or Apache 2 grant. This is the cleanest path.
- If the engine is released MIT/Apache 2, we can still document the COAB-derived ECL
  opcode table as a separate reference doc (facts/data are not copyrightable), implement
  independently, and credit Pilgrim in the documentation.

---

## 5. Open Questions / Unknowns

1. **COAB license:** Pilgrim's GitHub repo has no LICENSE file. Default = all rights
   reserved. **Action: contact Simeon Pilgrim** to request a permissive license grant
   before incorporating any ECL/combat logic.

2. **Complete ECL opcode table for DQK engine generation:** Not found in any public
   document. Must be derived by: (a) reading COAB source for CoAB-generation opcodes,
   (b) cross-referencing GBC's ECL-Monitor dynamic traces, (c) disassembling DQK.EXE.

3. **Stephen S. Lee's Gold Box FAQs** (cited by jhirvonen as ECL-Monitor's source): Could
   not locate a primary copy. May be on archived USENET or a defunct website. Worth
   hunting — may contain an opcode table. **UNKNOWN — needs further search.**

4. **Hackdocs redistribution:** The hackdocs corpus is community-authored format research;
   no SSI copyright claim has been asserted. However, verify no embedded game strings or
   art are present before committing to repo or publishing.

5. **ScummVM Gold Box engine:** No evidence of any ScummVM Gold Box engine project as of
   June 2026. Confirmed absent from v2026.1.0 release notes and compatibility list. No
   open PR or branch found. Treat as a **gap** — no cross-project collaboration opportunity
   there.

6. **GBC source code:** Joonas Hirvonen's Gold Box Companion appears to be closed source
   (binary-only distribution). License is UNKNOWN. Its ECL-Tool and ECL-Monitor are the
   most complete practical ECL references available, but the source cannot be reused.
   **Action: contact jhirvonen** to explore any collaboration or source access.

---

## 6. Implications for Our Engine

- **ECL VM must be built from scratch** (no clean open-source ECL interpreter to adopt),
  but COAB provides the best reverse-engineering base. Contact Pilgrim before coding.
- **Dungeon Craft** is the closest thing to what we're building architecturally (FRUA-
  superset, browser build, GPL-2.0, C++). We should study its combat and scripting
  architecture even though we can't copy code under a non-GPL license.
- **Gold Box Explorer (MIT)** is the cleanest reusable format reference — study its DAX/
  TLB reader C# code freely.
- **Our engine license decision matters:** MIT/Apache 2 = maximum ecosystem openness but
  cannot incorporate GPL Dungeon Craft code. GPL-2.0 = can incorporate Dungeon Craft
  code but restricts downstream. **Recommendation: MIT for engine, with clear "assets not
  included" policy, and independent ECL implementation informed by (not copied from) COAB.**
- **ScummVM's absence means no existing Gold Box engine community to join.** We are
  pioneering this space; the FRUA/Dungeon Craft community (forums, uaf.sourceforge.net,
  ua.reonis.com) is the closest adjacent community.
- **Trademark usage:** call the product something other than "Gold Box [anything]" in the
  final product name. Descriptive doc references are fine.
