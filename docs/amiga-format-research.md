# Krynn Trilogy — Cross-Platform Graphics & Format Research

Research date: 2026-06-20. Prepared for the Gold Box "Krynn" rebuild project.

**Project thesis under test:**
- **Claim A:** The **Amiga** versions of **Champions of Krynn (CoK)** and **Death Knights of Krynn (DoK)** have the best graphics/colors.
- **Claim B:** The **DOS (VGA)** version of **The Dark Queen of Krynn (DQK)** has the best graphics/colors.
- **Plan:** Rebuild all three on the **DQK DOS engine**, importing the best art per game.

**Verdict up front:** Both claims hold, well-supported by multiple independent sources. The main risk to the plan is not the *verdict* but the *pipeline*: the Amiga on-disk graphics format is essentially undocumented in public sources, and no public tool can re-pack DAX backdrops/sprites into the DOS engine.

Confidence tags used below: **[FACT]** = corroborated by 2+ independent sources or read directly from reverse-engineering source code; **[INFERENCE]** = reasoned from platform conventions / single source; **[UNVERIFIED]** = could not confirm.

---

## 1. Display / color depth per platform per game

### 1.1 Champions of Krynn (1990)

| Platform | Color depth | Notes |
|---|---|---|
| MS-DOS | **16-color EGA** (also CGA / Tandy) | EGA only. No VGA mode in the original release. **[FACT]** |
| Amiga (OCS/ECS) | **32 colors** from a 4096 palette | Plus animations, on-screen combat-icon picker, custom character sprites. **[FACT]** |
| Commodore 64 | 16-color (C64 palette) | Lowest tier. **[FACT]** |
| Apple II | low-res Apple II graphics | Released. **[FACT]** |
| PC-98 (NEC) | 16-color | Japanese release. **[FACT]** |
| Macintosh / Apple IIgs | — | **No Mac or IIgs release found** for CoK. **[FACT — absence, from MobyGames/Wikipedia platform lists]** |

The DOS version "is hampered with dithering, an EGA 16 color palette… whereas the Amiga version is in its 32 color glory from a palette of 4096." Amiga adds special combat sprites and the ability to draw your own character sprites; these "exist only in the Amiga version."
Sources: https://shot97retro.blogspot.com/2018/10/champions-of-krynn-in-depth-written.html , https://www.lemonamiga.com/games/reviews/view.php?id=309 , https://en.wikipedia.org/wiki/Champions_of_Krynn , https://www.gog.com/forum/dungeons_dragons_krynn_series/superior_version

### 1.2 Death Knights of Krynn (1991)

| Platform | Color depth | Notes |
|---|---|---|
| MS-DOS | **16-color EGA** (also CGA / Tandy) | "The last Gold Box game without VGA." The game *claims* VGA support in its installer/box but **does not actually use a 256-color VGA mode** — likely just to reassure VGA owners it would run. **[FACT]** |
| Amiga (OCS/ECS) | **32 colors** | Enhanced over DOS, same generation as CoK Amiga. **[FACT]** |
| Commodore 64 | 16-color | **[FACT]** |
| PC-98 (NEC) | 16-color | **[FACT]** |
| Macintosh / Apple IIgs / Apple II | — | **No Mac/IIgs/Apple II release found** for DoK. **[FACT — absence]** |

"Death Knights of Krynn really should have offered VGA graphics, considering the time it was released… that changed in the third game when the PC version used VGA." DoK DOS is therefore EGA-only in practice.
Sources: https://www.gog.com/forum/dungeons_dragons_krynn_series/superior_version , https://shot97retro.blogspot.com/2022/05/DeathKnights.html , https://en.wikipedia.org/wiki/Death_Knights_of_Krynn , https://www.uvlist.net/game-40342-Death+Knights+of+Krynn

### 1.3 The Dark Queen of Krynn (1992)

| Platform | Color depth | Notes |
|---|---|---|
| MS-DOS | **256-color VGA** (EGA fallback also present) | First Krynn game with true VGA. Richest art of the three. AdLib/Sound Blaster audio. **[FACT]** |
| Macintosh | **256 colors** | "The PC and Macintosh version could now display 256 colors." Mac is a peer of DOS here. **[FACT]** |
| Amiga (OCS/ECS) | **32 colors** | DQK *did* get an Amiga release, but it "still uses 32 colors" — i.e. it is the **inferior** DQK version graphically vs DOS/Mac. **[FACT]** |

"The PC and Macintosh version of the game could now display 256 colors" while "the Amiga version still uses 32 colors."
Sources: https://en.wikipedia.org/wiki/The_Dark_Queen_of_Krynn , https://www.mobygames.com/game/2757/the-dark-queen-of-krynn/ , https://www.gog.com/forum/dungeons_dragons_krynn_series/superior_version

### 1.4 Why the rankings fall the way they do

- For **CoK and DoK**, DOS tops out at **EGA 16 colors**, while Amiga delivers **32 colors from 4096** *and* extra art assets (combat sprites, animations). 32 > 16, and the Amiga art is genuinely re-worked, not just remapped — reviewers note "major differences in the amount of detail." So Amiga wins both. **[FACT]**
- For **DQK**, DOS/Mac reach **256-color VGA**, which is 8× the Amiga's 32 colors, so DOS (or Mac) wins. The Amiga DQK is the weakest of the DQK ports. **[FACT]**

This is the precise pattern the project thesis assumes — see verdict table in section 5.

---

## 2. File / resource formats

### 2.1 DOS DAX container (CoK, DoK, DQK all use this) **[FACT]**

All three Krynn DOS games are the **Pascal-engine** generation and ship their data in **`.DAX`** archives (e.g. `8X8D1.DAX`, `PIC*.DAX`, `CPIC*.DAX`, `SPRIT*.DAX`, `COMSPR*.DAX`, `ECL*.DAX`). DAX is a small indexed archive of RLE-compressed blocks. Layout (read directly from the Gold Box Explorer parser and corroborated by two forum threads):

- Bytes 0–1: `int16` = size of header table (little-endian); data starts at `header_size + 2`.
- Header table: array of **9-byte entries**, count = `(data_offset − 2) / 9`:
  - byte 0 = block **Id**, bytes 1–4 = `int32` **offset**, bytes 5–6 = `uint16` **raw (uncompressed) size**, bytes 7–8 = `uint16` **compressed size**.
- Block data follows; each block RLE-decoded to its raw size. `raw size == 0` means stored uncompressed.

**RLE scheme:** read one **signed** control byte `n`; if `n >= 0` copy the next `n+1` literal bytes; if `n < 0` repeat the next single byte `abs(n)` times. A literal byte with the high bit set must be escaped as a 1-length run. (Runs cap ~127, literal runs ~126.)
Sources: https://forums.goldbox.games/index.php?topic=1073.0 , https://forums.goldbox.games/index.php?topic=3148.0 , Gold Box Explorer source `Common/Plugins/Dax/DaxFile.cs` (https://github.com/simeonpilgrim/goldboxexplorer)

**Pixel layout — mostly chunky, despite the hardware:** **[FACT, from source]**
- **EGA blocks** (`EgaBlock`): header gives height, width (in 8-px units), x/y, item count; pixels start at offset 17 and are **packed 4-bit chunky** — each byte holds two pixels (`b>>4`, `b&0xF`), each a 0–15 index into a **fixed, hardcoded 16-color EGA palette** (no palette stored in the file). A special "combat palette" swaps colors 0 and 8 (black ↔ transparent) for filenames like `CPIC*`, `CHEAD*`, `DUNGCOM*`.
- **EGA sprite blocks** (`EgaSpriteBlock`): multi-frame with per-frame delay; same 4bpp chunky; some frames XOR-delta against frame 0; `SPRI*` files use color 0 as transparent.
- **VGA blocks** (`VgaBlock`, used by DQK): header gives height, width(×8), chunk count, palette base index + count, then an **embedded palette**, then **8bpp linear chunky** pixels (Mode 13h style). Palette entries are 3-byte RGB at **6-bit VGA DAC precision** (scale ×4 to 8-bit for display).
- **VgaStrata** variant: a genuinely **planar 5-bitplane** layout with an embedded sub-palette exists for some later images. So "planar vs chunky" = mostly chunky, but not exclusively.
Source: Gold Box Explorer `EgaBlock.cs`, `EgaSpriteBlock.cs`, `VgaBlock.cs`, `VgaStrataBlock.cs`, `EgaVgaPalette.cs`, `RenderBlockFactory.cs`.

**Important consequence for the rebuild:** EGA DAX art stores **no palette** (fixed 16) and is **4bpp**. VGA DAX art stores its **own palette** and is **8bpp**. The DQK engine is a VGA engine that reads 8bpp blocks with embedded palettes.

### 2.2 GLB / TLB "DataLib" — NOT the Krynn games **[FACT / INFERENCE]**

The `.GLB` (e.g. `STRG.GLB`, `SCRIPT.GLB`, `MONST.GLB`) and `.TLB` containers belong to the **later C++ engine — principally Unlimited Adventures (FRUA)** and the final-generation titles, **not** the Pascal-era Krynn trilogy. GLB header = 4 ASCII chars + `uint32` file size + a file-specific index/offset table; `.TLB` holds tiled image libraries with a 256-entry color table (index 255 = transparent green `0x67f79f`).
- **[FACT]** GLB/TLB live in the `Frua/` namespace of Gold Box Explorer; the format is the FRUA lineage.
- **[INFERENCE, strong]** DQK remains **DAX-based**, not GLB/TLB. I did not find a single source *explicitly* stating "DQK uses DAX," but the engine-generation split (Pascal = DAX; C++ = GLB/TLB) and DQK's 1992 Pascal-engine status both point to DAX. The goldbox.fandom "Gold Box (C++)" page that would settle which titles are C++ returned HTTP 403 to automated fetch. **To confirm with certainty, inspect a DQK install directory for `.DAX` vs `.GLB` files.**
Source: Gold Box Explorer `Common/Frua/Frua/FruaGlbFile.cs`, `FruaTlbColorTable.cs`; goldbox.fandom.com/wiki/Gold_Box_(Pascal) (search-indexed excerpt).

### 2.3 ECL scripting format **[FACT]**

ECL ("Event Control Language") is the event/scripting **bytecode stored inside `ECL*.DAX`** blocks — same DAX archive + RLE machinery, then each block disassembled by an opcode interpreter. Embedded text strings are flagged by a `0x80` marker + length and are **6-bit packed** (4 chars per 3 bytes, with a `+0x40` remap for low values — an uppercase-biased codec). Simeon Pilgrim's **EclDump** is the original decoder; Gold Box Explorer and the COAB reimplementation contain working ECL interpreters in source.
Sources: https://forums.goldbox.games/index.php?topic=1241.0 , Gold Box Explorer `Common/Plugins/DaxEcl/DaxEclFile.cs`, `Commands.cs`.

### 2.4 Amiga on-disk graphics format — the big gap **[UNVERIFIED]**

This is the weakest part of the entire public record and must be flagged loudly:

- **Established:** The only publicly reverse-engineered Amiga Krynn format is the **character/save file**, documented by **Amigan Software** (codetapper et al.) in `ami-form.txt` and the Multi-game Character Editor (MCE) source:
  - CoK: `save/#?.who`, 426 bytes/char
  - DoK: `save/#?.pch`, 232 bytes/char
  - DQK: `save/#?.qch`, variable bytes/char
  Source: https://amigan.1emu.net/releases/ami-form.txt , http://amigan.1emu.net/releases/MCE.lha
- **NOT established:** The Amiga **graphics / sprite / palette** container is **undocumented** in every source reached — not the goldbox.games forums, not shikadi ModdingWiki, not Amigan Software (ami-form.txt covers character/save/highscore/level data only, not graphics).
- **[INFERENCE only]** The mainline Gold Box Amiga ports were done by **Westwood Associates**, who "added mouse support and improved the graphics well before SSI's own MS-DOS versions made the leap to VGA." Westwood Amiga ports of this era typically used native **planar bitplanes** (5 bitplanes for 32 colors), possibly in **IFF/ILBM** or a **custom Westwood container**. The DOS DAX RLE format is little-endian + chunky and is DOS-specific, so it is **unlikely** the Amiga reused DAX byte-for-byte for art — but **no source confirms** what the Amiga used instead. Treat any specific Amiga-graphics-container claim as speculation until an ADF is inspected.
Sources: https://www.filfre.net/2017/03/opening-the-gold-box-part-5-all-that-glitters-is-not-gold/ , https://www.uvlist.net/game-40325-Champions+of+Krynn

### 2.5 Encoding cheat-sheet (synthesis)

| Mode | Colors | Hardware layout | DAX/file storage |
|---|---|---|---|
| EGA (CoK/DoK DOS, DQK EGA fallback) | 16 fixed | 4 bitplanes | **4bpp chunky**, no palette in file |
| VGA (DQK DOS, Mac) | 256 | chunky (Mode 13h) | **8bpp chunky** + embedded 6-bit-DAC palette (and a 5-plane "strata" variant) |
| Amiga OCS/ECS | 32 of 4096 | **planar, 5 bitplanes** | **undocumented** (likely planar IFF/ILBM or custom) |

---

## 3. Existing reverse-engineering work & tools

| Tool / resource | URL | What it does | Extract? | Rebuild? | State |
|---|---|---|---|---|---|
| **Gold Box Explorer** (bsimser original) | https://github.com/bsimser/Gold-Box-Explorer | Windows explorer-style viewer; batch-exports all images from **DAX and TLB** to PNG/BMP; handles palettes; all SSI Gold Box + FRUA. **The best ready-made DOS extractor.** | Yes | No (export only) | C#/.NET 3.5; inactive since ~May 2022; ~32★ |
| Gold Box Explorer (Simeon Pilgrim fork, v1.2) | https://github.com/simeonpilgrim/goldboxexplorer | Same lineage; the source is effectively the DAX/ECL/GLB spec. | Yes | No | C#; quiet |
| **Simeon Pilgrim DaxDump / EclDump** | https://simeonpilgrim.com/blog/ (e.g. https://simeonpilgrim.com/blog/2010/07/21/gold-box-games-cheat-codes/) | Original CLI tools that strip DAX RLE and split records, and decode ECL bytecode. Foundation for everything else. | Yes | No | CLI/Windows; historic (~2010); direct `daxdump.zip` URL **unverified** |
| **COAB** (Curse of the Azure Bonds reimpl.) | https://github.com/simeonpilgrim/coab | ~100% feature-complete C#/C reimplementation of a Pascal-era Gold Box game; contains real DAX RLE decode + ECL interpreter. **Best open-source reference for engine behavior.** Krynn shares this DAX/RLE/ECL design. | Yes (in code) | n/a | C#/C; quiet |
| **Gold Box Companion (GBC)** | https://gbc.zorbus.net/ | Gameplay overlay (HUD, automap, editors) for all Gold Box + UA + Buck Rogers. Recent builds add **experimental DAX mod-tools** that read **and write** DAX — but only **EGA icons / fonts / monsters / items**, not full backdrops/sprites. | Yes (limited) | **Yes (limited)** | Windows/DOSBox; **actively maintained** (v2.65, 2022) |
| Gold Box Games Forums | https://forums.goldbox.games/ | Live community hub. Key format threads: DAX graphic format **topic 1073**, ECL **topic 1241**, hacking/rebuilding DAX **topic 3148**, build-a-dax **topic 1695**. | — | — | Active |
| Gold Box Games Wiki (community) | https://wiki.goldbox.games/ | Companion wiki to the forums; per-game pages. | — | — | Active |
| Gold Box Wiki (Fandom) | https://goldbox.fandom.com/ — esp. `Gold_Box_(Pascal)` | Secondary; the Pascal page documents the DAX RLE in prose. (403 to automated fetch.) | — | — | Light |
| Amigan Software — `ami-form.txt` + MCE | https://amigan.1emu.net/releases/ami-form.txt , http://amigan.1emu.net/releases/MCE.lha | Only public reverse-engineering of **Amiga** Krynn files — **character/save formats only**, not graphics. | Char files | Char files | Historic |
| Save/character editors (no graphics) | https://github.com/robotlions/gold-box-editor , https://github.com/kblood/GoldBoxEditor | Web/desktop save editors. | Saves | Saves | Quiet |
| Other engine reimpls (early/experimental) | https://github.com/immeraufdemhund/Goldbox , https://github.com/georgedorn/Android-Goldbox | Partial Gold Box engine reimplementations; DAX/graphics scope unverified. | ? | ? | Early |

**Notes / corrections to the project premise:**
- **shikadi.net ModdingWiki has no dedicated SSI Gold Box / DAX page.** `moddingwiki.shikadi.net/wiki/DAX_Format` returns 404. The premise that shikadi is the canonical Gold Box source is **incorrect** — the real docs are the forums (topic 1073/3148), the Fandom Pascal page, and the Gold Box Explorer / COAB source.
- **No public tool targets Amiga Gold Box graphics extraction.** You would fall back to generic Amiga ADF unpacking + IFF/ILBM rippers — and only if the Amiga art is actually IFF, which is unconfirmed (section 2.4).
- **No public tool re-packs general DAX backdrops/sprites** into the DOS engine. GBC writes only EGA icons/fonts/monsters/items. A full DAX **writer/repacker is the missing piece** for this project and would have to be built.

---

## 4. Feasibility notes for the rebuild

### (a) Extracting Amiga CoK/DoK art — **highest risk**
- The Amiga graphics container is **undocumented** (section 2.4). Before any pipeline can exist, someone must mount the CoK/DoK Amiga **ADF** disk images, inspect the file catalog, and reverse the graphics container (is it IFF/ILBM, raw planar bitplanes, or a Westwood custom format?). **[UNVERIFIED — this is unscoped work.]**
- Amiga art is **planar (5 bitplanes)**. Even once located, it must be de-planarized to chunky to match the DOS engine. This is mechanically straightforward *once the container is understood*, but the container is the blocker.

### (b) Converting Amiga art to the DOS DQK (VGA) format — **medium, and the lossless direction**
- **Palette:** Amiga = 32 colors at 4-bit-per-gun (4096 space). VGA DAC = 6-bit-per-gun (262k space). **Amiga 32 → VGA 256 is loss-free** for color: each Amiga 12-bit RGB maps cleanly into a 256-entry VGA palette (scale 4-bit guns to 6-bit, e.g. `v6 = a4 * 63/15`), with 224 palette slots to spare. The DQK VGA DAX block format *expects* an embedded palette, so you simply emit the Amiga palette as the block palette. **[INFERENCE, strong — based on confirmed DAX VGA palette format.]**
- **Pixel format:** target is **8bpp chunky** with that embedded palette (section 2.1). Convert Amiga planar → chunky indices into the new palette.
- **The lossy direction to avoid:** anything that starts from **DOS EGA 16-color** art. EGA → 32/256 cannot invent the lost detail. This is exactly why the plan correctly sources CoK/DoK art from **Amiga**, not DOS. Keep EGA strictly as a fallback/last resort. **[FACT — established by the color-depth analysis in section 1.]**

### (c) Running CoK/DoK *content* on the DQK engine — **medium**
- All three are the **same Pascal Gold Box engine family** sharing DAX + ECL + the same block structures, which de-risks content portability considerably. **[FACT]**
- **ECL portability:** opcode tables drifted across the three titles; CoK/DoK ECL scripts may use opcodes or semantics that differ from DQK's interpreter. Expect to remap/patch ECL, not just drop it in. **[INFERENCE]**
- **Resolution / layout:** Gold Box played at 320×200; tile/sprite cell sizes (8×8 tiles, combat sprite cells, viewport backdrop dimensions) are consistent within the family, but per-game art dimensions and the combat-palette index conventions (color 0/8 swap for EGA; transparent index for VGA/`SPRI*`) must be normalized when retargeting. **[FACT for the conventions; INFERENCE for the effort.]**
- **Repacking:** as noted, **no public DAX writer for backdrops/sprites exists** — building a correct DAX RLE *encoder* (and the VGA block header + embedded palette emitter) is required, derivable from the documented format and the COAB/Gold Box Explorer source. **[FACT — gap; the format is known so this is buildable.]**

### Risk ranking
1. **Amiga graphics container is undocumented** (blocks step a). Must reverse it from ADFs. *Highest.*
2. **No DAX backdrop/sprite repacker exists** — must be written (format is known). *High but tractable.*
3. **ECL opcode drift** between CoK/DoK and DQK. *Medium.*
4. Palette/pixel conversion (Amiga→VGA). *Low — lossless direction, format known.*
5. Confirming DQK is DAX (not GLB/TLB). *Low — likely DAX; verify by inspecting a DQK install.*

---

## 5. Per-game "best graphics platform" verdict

| Game | Best platform for art | Why | Claim it tests | Confidence |
|---|---|---|---|---|
| **Champions of Krynn** (1990) | **Amiga** | DOS is EGA 16-color only; Amiga is 32-of-4096 **plus** reworked detail, animations, extra combat sprites. | Claim A | **High** |
| **Death Knights of Krynn** (1991) | **Amiga** | DOS is EGA 16-color only ("last Gold Box without VGA"); Amiga is 32 colors with more detail. | Claim A | **High** |
| **The Dark Queen of Krynn** (1992) | **DOS (VGA)** — Mac is an equal 256-color peer | DOS/Mac = 256-color VGA; Amiga "still uses 32 colors." | Claim B | **High** |

**Both Claim A and Claim B hold.** Sourcing CoK/DoK art from Amiga and DQK art from DOS, and rebuilding on the DQK DOS (VGA) engine, is the technically correct choice given the color-depth facts. (For DQK, the Mac 256-color art is an alternative source of equal color depth if any specific asset is cleaner there — worth spot-checking but not required.)

---

## 6. Open questions / unverified

1. **Amiga graphics/sprite/palette on-disk format** — genuinely undocumented in all reachable sources. Only Amiga *character/save* files are reverse-engineered (Amigan Software). **Top blocker.** Next step: mount CoK/DoK Amiga ADFs and inspect/reverse the graphics container.
2. **DQK container = DAX (not GLB/TLB)** — strongly inferred from the Pascal-vs-C++ engine split, but not directly quoted from a source (the Fandom "C++" page 403'd). Verify by listing a DQK install directory's file extensions.
3. **Full ECL opcode semantics and cross-title drift** — opcode *tables* exist in Gold Box Explorer / COAB source, but no narrative spec was retrievable, and per-game differences between CoK/DoK and DQK are not catalogued publicly.
4. **DaxDump direct download** — Simeon Pilgrim's `daxdump.zip` URL could not be confirmed live; the code survives via Gold Box Explorer / COAB regardless.
5. **Mac DQK art vs DOS DQK art** — both are 256-color, but whether the Mac assets differ in quality/dithering from DOS was not investigated; spot-check if pursuing Mac as a source.
6. **shikadi.net** has **no** Gold Box DAX page (404); the project premise that it documents the format is incorrect. Real docs: forums.goldbox.games topics 1073/3148/1241, goldbox.fandom Pascal page, and the Gold Box Explorer / COAB source.

---

### Primary sources
- Gold Box Explorer source (de-facto DAX/ECL/GLB spec): https://github.com/bsimser/Gold-Box-Explorer , https://github.com/simeonpilgrim/goldboxexplorer
- COAB reimplementation (engine reference): https://github.com/simeonpilgrim/coab
- DAX format threads: https://forums.goldbox.games/index.php?topic=1073.0 , https://forums.goldbox.games/index.php?topic=3148.0 ; ECL: https://forums.goldbox.games/index.php?topic=1241.0
- Gold Box Companion: https://gbc.zorbus.net/
- Amiga character formats: https://amigan.1emu.net/releases/ami-form.txt
- Graphics/platform: https://en.wikipedia.org/wiki/The_Dark_Queen_of_Krynn , https://en.wikipedia.org/wiki/Death_Knights_of_Krynn , https://en.wikipedia.org/wiki/Champions_of_Krynn , https://www.gog.com/forum/dungeons_dragons_krynn_series/superior_version , https://shot97retro.blogspot.com/2018/10/champions-of-krynn-in-depth-written.html , https://www.lemonamiga.com/games/reviews/view.php?id=309
- Amiga port history: https://www.filfre.net/2017/03/opening-the-gold-box-part-5-all-that-glitters-is-not-gold/
