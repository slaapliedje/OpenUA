# Per-Platform Graphics Modes for Gold Box Games

**Research date:** 2026-06-21  
**Scope:** Krynn trilogy (CoK/DoK/DQK) as primary; broader Gold Box series noted where relevant.  
**Purpose:** Design the live graphics-mode-switching feature described in `docs/engine/VISION.md`.

---

## 1. Platform Taxonomy

### 1.1 Platform × Capability Table (Krynn Trilogy)

| Platform | Resolution | Colors shown | Palette depth | Pixel geometry | Memory model | CoK | DoK | DQK |
|---|---|---|---|---|---|---|---|---|
| **DOS — CGA** | 320×200 | 4 (fixed sets) | 4 from 16 | chunky | n/a | fallback | fallback | no |
| **DOS — Tandy/PCjr** | 320×200 | 16 | 16 from 16 | chunky | n/a | fallback | fallback | fallback |
| **DOS — EGA** | 320×200 | 16 | 16 from 64 | chunky (packed) | 4-plane | primary | primary | fallback |
| **DOS — VGA/MCGA (Mode 13h)** | 320×200 | 256 | 256 from 262,144 (6-bit DAC) | chunky (linear) | linear | not supported | not supported | **primary** |
| **Amiga OCS/ECS — standard** | 320×200 (NTSC) / 320×256 (PAL) | **32** | 32 from 4,096 (12-bit) | **planar** (5 bitplanes) | chip RAM | **best** | **best** | inferior |
| **Amiga OCS — EHB** | 320×200 | 64 | 32+32 half-brite | planar (6 bp) | chip RAM | not used (inferred) | not used (inferred) | n/a |
| **Amiga OCS — HAM** | 320×200 | up to 4,096 | 16 base + modify | planar (6 bp) | chip RAM | not used (inferred) | not used (inferred) | n/a |
| **Commodore 64** | 320×200 (multicolor) | 16 | 16 (fixed palette) | chunky / multi-color | n/a | port | port | not released |
| **Apple II** | 280×192 | 6 | fixed | planar (hi-res) | n/a | port | not released | not released |
| **Apple IIgs** | 320×200 (SHR) | up to 256 | 16 per scanline from 4,096 | chunky | n/a | UNKNOWN | UNKNOWN | not released |
| **NEC PC-98** | 640×400 | 16 | 16 from 4,096 (12-bit analog) | planar | n/a | port (1990) | port (1991) | not released |
| **Macintosh (classic)** | variable | 256 (color) / grayscale (b/w) | 256 from system palette | chunky | n/a | 16-color | UNKNOWN | 256-color (= DOS) |

Sources: Wikipedia CoK/DoK/DQK articles; [Lemon Amiga CoK review](https://www.lemonamiga.com/games/reviews/view.php?id=309); [GOG forum comparison](https://www.gog.com/forum/dungeons_dragons_krynn_series/superior_version); [shot97retro Amiga CoK in-depth](https://shot97retro.blogspot.com/2018/10/champions-of-krynn-in-depth-written.html); [Macintosh Repository DQK](https://www.macintoshrepository.org/4337-the-dark-queen-of-krynn); [Amiga Halfbrite Wikipedia](https://en.wikipedia.org/wiki/Amiga_Halfbrite_mode); verified locally via `KRYNN.CFG`, `DKK.CFG`, `DQK.CFG`, `START.EXE`, `INSTALL.EXE` strings (see `docs/dos-inventory.md`).

### 1.2 Key Technical Distinctions

**DOS EGA (CoK/DoK primary)**
- 320×200, 16 simultaneous colors selected from a 64-color palette (2 bits per RGB channel).
- Pixel aspect ratio: 1.2:1 (pixels taller than wide on a 4:3 CRT). 320×200 displayed at 4:3 = non-square pixels. Correct display requires vertical stretch to 320×240 equivalent.
- Art stored in DAX archives as RLE-compressed 4-plane bitmaps. No palette embedded in file — engine uses fixed EGA-16 system palette at runtime.
- Source: `docs/dax-format.md`; `docs/dos-inventory.md` §5.

**DOS VGA Mode 13h (DQK primary)**
- 320×200, 256 simultaneous colors selected from 262,144 (6-bit per channel DAC = 18-bit color, often called "64-level" per channel).
- Same non-square pixel aspect ratio as EGA (1.2:1 on 4:3 CRT).
- Art stored in HLIB TLB containers. **Palette embedded per TLB file** as 3-byte-per-entry RGB table (see `hackdocs_extracted/TLBFORM.TXT` — color table header at variable offset, magic byte 8 or 24; `# of colors` 0–256). The 6-bit DAC values (0–63) map to 8-bit by left-shift-2 (multiply by 4) for modern display.
- Source: `docs/hlib-format.md`; `hackdocs_extracted/TLBFORM.TXT`.

**Amiga OCS/ECS Standard Mode (CoK/DoK best)**
- 320×200 NTSC (or 320×256 PAL — most game art authored for NTSC proportions per [shot97retro review](https://shot97retro.blogspot.com/2018/10/champions-of-krynn-in-depth-written.html): "best in NTSC mode with 4:3 aspect ratio").
- **32 colors** selected from a **12-bit palette** (4 bits per RGB channel = 4,096 total). This is strictly richer than EGA's 64-color palette for the same display count.
- **Planar (bitplane) memory layout**: 5 bitplanes interleaved in chip RAM. Each pixel is encoded as 5 bits spread across 5 planes; color index (0–31) indexes the 32-entry CLUT.
- Art in `.DAA` container (SSI-proprietary, DAX-adjacent, big-endian 68000). Title screens as IFF/ILBM `.LBM` files (standard Amiga format, palette embedded). Source: `docs/daa-format.md`; `docs/amiga-inventory.md`.
- **Amiga-exclusive art**: CoK Amiga has custom character selection sprites not present on any other platform. Both CoK and DoK Amiga have richer skin tones, superior shading, and (CoK only) in-game sprite animations absent on DOS. Source: [shot97retro](https://shot97retro.blogspot.com/2018/10/champions-of-krynn-in-depth-written.html); [Lemon Amiga CoK](https://www.lemonamiga.com/games/reviews/view.php?id=309).

**NEC PC-98**
- 640×400 resolution, 16 simultaneous colors from a 4,096-color (12-bit) analog palette — same palette depth as Amiga but double the resolution. Source: [9800guide.uwu.ai](https://9800guide.uwu.ai/); [degenaura PC-98 overview](https://degenaura.wordpress.com/2020/03/05/feature-understanding-the-nec-pc-98/).
- Whether PC-98 CoK/DoK uses the higher resolution for artwork vs EGA ports is UNKNOWN — no screenshots or technical breakdowns found. The PC-98 version may simply be the DOS EGA art repackaged in PC-98 display format.

**Commodore 64**
- Fixed 16-color palette (not selectable from a larger set). 320×200 multi-color or 320×200 high-res.
- Quality: significantly below Amiga and DOS EGA per community consensus. [GOG forum](https://www.gog.com/forum/dungeons_dragons_krynn_series/superior_version): "The Commodore 64 versions aren't even close, in terms of graphics." Not a target graphic set.

**Apple II**
- 280×192 hi-res, 6 colors (artifact coloring). Lowest fidelity of any Krynn port. Not a target graphic set.

**Apple IIgs**
- Super Hi-Res mode: 320×200, up to 256 colors (16 per scanline from 4,096-color palette, up to 16 palettes = 256 on screen). Source: [Apple IIGS Graphics Modes](https://archive.org/stream/Apple_IIGS_Graphics_Modes/Apple_IIGS_Graphics_Modes_djvu.txt). Whether CoK was ported to IIgs specifically (vs. base Apple II) is UNKNOWN — Wikipedia lists "Apple II" without specifying IIgs.

**Macintosh (DQK)**
- Two variants shipped: a black-and-white version and a color version (extra floppy). Color version: 256 colors, matching DOS VGA in color depth. Source: [Macintosh Repository](https://www.macintoshrepository.org/4337-the-dark-queen-of-krynn); [Wikipedia DQK](https://en.wikipedia.org/wiki/The_Dark_Queen_of_Krynn).
- Whether Mac DQK color art is pixel-identical to DOS VGA or re-rendered is UNKNOWN.

---

## 2. "Best Art" Verdict (confirmed and expanded)

| Game | Best platform | Runner-up | Reasoning |
|---|---|---|---|
| CoK (1990) | **Amiga** | PC-98 (UNKNOWN) | Amiga: 32 of 4,096 colors vs. DOS EGA 16 of 64; exclusive character selection sprites; animations; superior skin tones. |
| DoK (1991) | **Amiga** | PC-98 (UNKNOWN) | Same as CoK: EGA-only DOS, Amiga 32-color with richer art. "Last Gold Box without VGA" on DOS. |
| DQK (1992) | **DOS VGA** (= Mac color) | Amiga 32-color (weakest) | DOS/Mac: 256 of 262,144 colors; Amiga still 32-color — significant step down. |

This confirms the project's stated plan in `CLAUDE.md` and `docs/findings.md`. The Amiga's superiority on CoK/DoK is **color palette depth** (12-bit vs. 6-bit) not just count (32 vs. 16), plus exclusive artwork. For DQK the DOS VGA jump to 256 colors is decisive.

---

## 3. Live Switching Design: The Graphic-Set Abstraction

### 3.1 Prior Art for Live Art Switching

**Tomb Raider I–III Remastered (2024, Aspyr):** Press F1 (PC) / Options (PS) / Plus (Switch) to instantly swap between original and remastered geometry+textures at any point during gameplay, including the title screen. No loading screen. Both asset sets appear to be resident in memory simultaneously. Source: [Sportskeeda guide](https://www.sportskeeda.com/esports/how-switch-graphics-tomb-raider-1-3-remastered); [GosuNoob guide](https://www.gosunoob.com/guides/how-to-change-classic-modern-graphics-in-tomb-raider-remastered/).

**Diablo II: Resurrected (2021, Blizzard):** Press G to toggle between remastered and legacy (software-rendered) graphics. Lead Designer confirmed "it really is the old game running underneath" — two full render paths co-resident. Source: [Game Rant](https://gamerant.com/diablo-2-resurrected-legacy-mode-toggle/).

**ScummVM:** Ctrl+Alt+0/9 cycle graphical filters at runtime; Ctrl+Alt+A toggles aspect ratio correction. These are render-pipeline switches (filter/scale), not asset-set switches — but the pattern of live rendering parameter change is identical. Source: [ScummVM graphics docs](https://docs.scummvm.org/en/latest/advanced_topics/understand_graphics.html).

**Key lesson:** Successful live switchers keep both asset sets resident and toggle a pointer/flag, not a reload. The engine's game state and game logic remain constant; only the render path reads from a different asset slot.

### 3.2 Proposed Graphic-Set Abstraction

A **GraphicSet** is a named collection of decoded, engine-ready assets (ImageData / GPU textures) for one platform variant of one game. The engine maintains an **active set pointer**; switching replaces the pointer and issues a redraw.

#### Logical Asset → Platform Set Mapping

Every renderable asset is identified by a **logical key**: `(game, category, index)`.

```
LogicalAsset {
  game:     "CoK" | "DoK" | "DQK"
  category: "PIC" | "BIGPIC" | "SPRIT" | "CPIC" | "CBODY" | "8X8D"
             | "WALLDEF" | "BACK" | "WILDCOM" | "DUNGCOM" | "COMSPR"
             | "TITLE" | "CURSOR" | "FRAME" | "ALWAYS" | ...
  index:    integer (block/entry index within the container)
}
```

Each logical asset has a **set map**: a dictionary from `GraphicSetId` to the decoded pixel buffer + metadata for that platform:

```
AssetSetMap {
  "DOS-EGA"   → { pixels: Uint8Array, width, height, palette: Palette16, ... }
  "Amiga"     → { pixels: Uint8Array, width, height, palette: Palette32, ... }
  "DOS-VGA"   → { pixels: Uint8Array, width, height, palette: Palette256, ... }
  // ... future: "PC-98", "Mac", "CGA", ...
}
```

A **Palette** is a typed array of RGB triples (already 8-bit; 6-bit VGA values upscaled on load).

**Fallback chain**: if the active set does not contain an entry for a given logical key, the engine falls back to a priority-ordered list: e.g., `Amiga → DOS-EGA → DOS-VGA`.

#### Switching at Runtime

```
function setActiveGraphicSet(id: GraphicSetId) {
  engine.activeSet = id;
  // All pending draw calls on next frame pick up from activeSet
  requestRedraw();
}
```

No asset reload is needed. All sets are decoded to a common engine-neutral format on load (or lazy-decoded and cached). The active-set pointer is a single read per draw call.

#### Palette Handling in WebGL

Implement indexed-color rendering with a palette texture:

- **Index texture**: pixel values = color indices (0–31, 0–255, etc.) stored as a single-channel `ALPHA` texture.
- **Palette texture**: 256×1 RGBA texture. Each texel is one palette entry. For a 32-color Amiga set, only entries 0–31 are meaningful.
- **Fragment shader**: `gl_FragColor = texture2D(u_palette, vec2((index + 0.5) / 256.0, 0.5));`
- Both textures use `gl.NEAREST` filtering.
- **Palette swap = re-upload** the 256×4-byte palette texture. Cost: one `texImage2D` call per frame if animated (color cycling); one call total for a static set switch.

Source: [WebGL Fundamentals palette-based graphics](https://webglfundamentals.org/webgl/lessons/webgl-qna-emulating-palette-based-graphics-in-webgl.html).

This naturally accommodates all color depths: EGA (16 entries active), Amiga (32 entries), VGA (256 entries), with the same shader.

#### Aspect Ratio and Scaling

All platforms use 320×200 as the logical game resolution (PC-98 at 640×400 is a scaled variant). Pixels are non-square: on a 4:3 CRT, 320×200 displays with a 1.2:1 pixel aspect ratio (pixels are 20% taller than wide). Correct reproduction:
- Render internally at 320×200.
- Upscale to display: multiply height by 1.2 → output 320×240 (or any integer multiple maintaining the 4:3 ratio).
- Integer scaling: target display sizes are 320×240, 640×480, 960×720, 1280×960 for clean pixels.
- The "correct" size for a 1080p display: 1440×1080 (4.5× width, 5.4× height — non-integer, so use 1280×960 = 4× for clean pixels, or use WebGL bilinear for fractional).
- **CRT filter** (optional): scanline / phosphor shaders applied as a post-pass.

Source: [VOGONS VGA PAR discussion](https://www.vogons.org/viewtopic.php?f=9&t=17110); [ScummVM aspect ratio docs](https://docs.scummvm.org/en/latest/advanced_topics/understand_graphics.html).

---

## 4. Mismatch Risks and Hard Problems

### 4.1 Frame-Count / Index Mismatches (HIGH RISK)

The biggest structural risk: the Amiga has **Amiga-exclusive art** that has no DOS equivalent, and vice versa.

**Confirmed Amiga-exclusive content (CoK):**
- Custom player character icon sprites ("special combat sprites added for the players to choose for their characters, which exist only in the Amiga version"). Source: [Lemon Amiga review](https://www.lemonamiga.com/games/reviews/view.php?id=309).
- In-game sprite animations ("Only the Amiga version features any kind of animations"). Source: [shot97retro](https://shot97retro.blogspot.com/2018/10/champions-of-krynn-in-depth-written.html).
- A character-selection screen that shows all icons simultaneously ("first implemented with Champions and only on the Amiga").

**Entry-count discrepancies observed in our assets:**
- DOS CoK `SPRIT1.DAX`: 45 entries. Amiga CoK `SPRIT1.DAA`: 128,875 bytes (vs. 8,500 bytes DOS). Amiga holds far more data per file. The entry structure within `.DAA` is not yet decoded (see `docs/daa-format.md`), so the exact frame count is UNKNOWN.
- DoK `CPIC1.DAX`: 954 entries. Amiga DoK `CPIC1.daa`: 74,929 bytes. Comparison pending decode.

**Implication for the engine**: the fallback chain handles the case where a platform set lacks an asset entirely (return fallback set's version). But when the Amiga has *more* frames than DOS for the same logical category, the engine must not attempt to index beyond the active set's frame count. The logical-asset model must store each set's frame count separately and the game logic must query it from the active set.

### 4.2 Pixel Geometry: Planar (Amiga) vs. Chunky (DOS)

Amiga art is stored in 5-bitplane planar format; DOS EGA in 4-plane, DOS VGA in chunky (linear byte = pixel index). All must be decoded to chunky (one byte = one color index) before loading into the WebGL index texture. This is a **load-time conversion**, not a runtime concern. Both decoders exist (`tools/daa_decode.py`, `tools/dax_decode.py`).

The "Chain 4 / interlaced quarter-row" format of HLIB TLB images (documented in `hackdocs_extracted/TLBFORM.TXT`) also requires a de-interleave step at decode time.

### 4.3 Palette Normalization

- **EGA**: 16-color, fixed indices 0–15 map to fixed EGA colors. No palette in file. The engine must provide the standard EGA-16 palette as a constant for the DOS-EGA graphic set.
- **Amiga IFF/LBM**: palette embedded in ILBM CMAP chunk (standard). Already 8-bit RGB per entry.
- **Amiga DAA**: palette storage in `.DAA` files is UNKNOWN — the DAA format header/palette block has not yet been fully reversed (see `docs/daa-format.md`, step 3 in roadmap). If DAA frames are indexed against a companion palette file or a fixed Amiga CLUT, this must be established before the Amiga graphic set can be loaded. **This is the #1 decoder gap.**
- **VGA TLB**: 6-bit DAC values (0–63 per channel). Upscale: `v8 = v6 * 4` (or `v6 << 2`). Color cycling supported via `hackdocs_extracted/TLBFORM.TXT` color-cycling header (direction, speed, start index, count). The engine should implement color cycling as a timed palette-texture re-upload.

### 4.4 Resolution Mismatch (PC-98)

PC-98 runs at 640×400 — double 320×200. If we ever target PC-98 assets as a graphic set, they cannot be displayed at 1:1 pixel-map with 320×200 sets. Two approaches: (a) store PC-98 at 640×400 and downscale at decode time to 320×200, (b) tag PC-98 assets as "2x" and render at higher resolution when that set is active. Approach (b) would require an engine render-resolution switch, not just a palette swap — significantly more complex. **Defer until PC-98 art decode is attempted.**

### 4.5 The Amiga 32-Color → VGA 256-Color Conversion (DQK rebasing)

The primary project goal (CoK/DoK on the DQK/HLIB VGA engine) requires converting Amiga 32-color art into VGA 256-color HLIB TILE chunks. This is the "lossless direction" (more color slots available). The mapping:
- Amiga 32-color palette → embed as the first 32 entries of the VGA 256-color HLIB palette block.
- Pixel indices 0–31 pass through unchanged.
- Indices 32–255 unused (transparent or background color).

The browser engine's live-switching version of this is simpler: keep the Amiga palette as 32 entries and use the 256-entry WebGL palette texture with only 32 active slots. No conversion needed for display — conversion is only needed when producing actual HLIB TLB files for the DOS rebuild track.

### 4.6 Black-and-White Mac Variant

DQK Mac shipped both a color and a b/w version. If we add a Mac graphic set, the b/w variant would need grayscale-palette rendering. The "Amiga → EGA" style downgrade path suggests a b/w graphic set could use the same index texture with a grayscale palette texture — straightforward.

---

## 5. Asset-Set Manifest Schema (Proposed)

Each game's manifest (`CoK.json`, `DoK.json`, `DQK.json`) should declare its graphic sets and the fallback order:

```jsonc
{
  "graphicSets": {
    "DOS-EGA": {
      "label": "DOS EGA (16 colors)",
      "containers": {
        "PIC": ["PIC1.DAX", "PIC2.DAX", "PIC3.DAX"],
        "SPRIT": ["SPRIT1.DAX", "SPRIT2.DAX", "SPRIT3.DAX"]
        // ...
      },
      "palette": "ega16-fixed"
    },
    "Amiga": {
      "label": "Amiga (32 colors)",
      "containers": {
        "PIC": ["pic1.daa", "pic2.daa"],
        "SPRIT": ["SPRIT1.DAA", "SPRIT2.DAA"]
        // ...
      },
      "palette": "embedded-in-lbm-or-daa"
    }
  },
  "graphicSetFallback": ["Amiga", "DOS-EGA"]
}
```

The `palette` field for the Amiga DAA case will need a concrete value once the DAA palette storage is decoded.

---

## 6. Open Questions / Unknowns

| # | Question | Impact | How to resolve |
|---|---|---|---|
| 1 | **Amiga DAA palette storage**: where is the 32-color CLUT stored in `.DAA` files (per-file header, separate companion file, or hardcoded in game binary)? | Blocks Amiga graphic-set loading entirely | Reverse `SPRIT1.DAA` or `pic1.daa` against known renders; check `globals.daa` in DoK which may hold palette data |
| 2 | **Amiga DAA frame count**: for each DAA, how many frames are inside, and do frame indices match DOS DAX 1:1? | Determines whether logical-key indexing is safe cross-platform | Decode inner structure of `.DAA`; compare rendered outputs |
| 3 | **Amiga-exclusive frame ranges**: which sprite/picture indices exist only on Amiga (character icons, animations)? | Engine must handle "set doesn't have this index" gracefully | Full decode + inventory of Amiga frame sets |
| 4 | **PC-98 art quality**: is PC-98 CoK/DoK distinct from DOS EGA, or a straight port at higher resolution? | Determines whether PC-98 is worth adding as a graphic set | Find and compare PC-98 screenshots |
| 5 | **Mac DQK color art**: pixel-identical to DOS VGA or separately rendered? | Determines whether Mac is a meaningful third DQK set | Compare screenshots or extract Mac assets |
| 6 | **Apple IIgs release**: was CoK ported to IIgs (not just base Apple II)? | IIgs could offer 256-color art — interesting if so | Check archive.org / MyAbandonware listings |
| 7 | **Color cycling in CoK/DoK DAX art**: DAX has no palette block — does EGA art use any color animation? | If so, cycling is a DAX-engine feature driven by ECL, not a data field | Check ECL opcodes (`hackdocs_extracted/OPCODES.TXT`) |
| 8 | **Amiga DQK Disk 3 recovery**: can a clean image be sourced? | Opens Amiga DQK as a third set for DQK (currently low priority since DOS VGA is best) | Seek alternative image dump; currently `unadf` segfaults |

---

## 7. Implications for Our Engine

1. **Unified indexed-color renderer**: use the WebGL palette-texture technique (index texture + 256×1 palette texture). This one pipeline handles CGA-4, EGA-16, Amiga-32, VGA-256 and Mac-256 with only the palette texture changing. Implement now; don't defer.

2. **GraphicSet as a first-class concept**: model it in the manifest and in the TypeScript asset layer from day one. Retrofitting set-awareness onto a single-platform loader will be painful.

3. **Fallback-by-index, not fallback-by-file**: the fallback must operate at the individual frame level, not at the container level. A given sprite index may exist in the Amiga set but not in the DOS set (exclusive animation frames), or vice versa. The resolver must check "does the active set have index N of category C?" before falling back.

4. **Aspect ratio correction is mandatory for authenticity**: all 320×200 platforms used non-square pixels on 4:3 CRTs. The engine must apply the 1.2× vertical stretch by default. Offer a "square pixels" toggle for modern monitors that prefer it.

5. **Palette normalization at load time**: VGA 6-bit DAC values → 8-bit on load (×4). Amiga 12-bit → 8-bit per channel on load (×17 or `v<<4 | v>>0` depending on precision goal). EGA fixed palette as a code constant. All sets present 8-bit RGB to the shader.

6. **DAA palette decode is the critical path**: without it the Amiga graphic set cannot be loaded. This is a higher priority than it appears from the roadmap.

7. **Live switching UX reference**: Tomb Raider Remastered (press F1 to toggle) is the gold standard for game-feel. Diablo II Resurrected (G key) is another. Aim for the same: one button, instant, no black frame, works anywhere in the game.

---

## Sources

- [Wikipedia: Champions of Krynn](https://en.wikipedia.org/wiki/Champions_of_Krynn)
- [Wikipedia: Death Knights of Krynn](https://en.wikipedia.org/wiki/Death_Knights_of_Krynn)
- [Wikipedia: The Dark Queen of Krynn](https://en.wikipedia.org/wiki/The_Dark_Queen_of_Krynn)
- [Lemon Amiga: Champions of Krynn review](https://www.lemonamiga.com/games/reviews/view.php?id=309)
- [Lemon Amiga: Death Knights of Krynn review](https://www.lemonamiga.com/games/reviews/view.php?id=308)
- [GOG Forum: Krynn superior version comparison](https://www.gog.com/forum/dungeons_dragons_krynn_series/superior_version)
- [shot97retro: Champions of Krynn in-depth Amiga review](https://shot97retro.blogspot.com/2018/10/champions-of-krynn-in-depth-written.html)
- [shot97retro: Death Knights of Krynn in-depth Amiga review](https://shot97retro.blogspot.com/2022/05/DeathKnights.html)
- [Macintosh Repository: Dark Queen of Krynn](https://www.macintoshrepository.org/4337-the-dark-queen-of-krynn)
- [Wikipedia: Amiga Halfbrite mode](https://en.wikipedia.org/wiki/Amiga_Halfbrite_mode)
- [Wikipedia: Hold-And-Modify (HAM)](https://en.wikipedia.org/wiki/Hold-And-Modify)
- [Apple IIGS Graphics Modes (archive.org)](https://archive.org/stream/Apple_IIGS_Graphics_Modes/Apple_IIGS_Graphics_Modes_djvu.txt)
- [Wikipedia: Apple II graphics](https://en.wikipedia.org/wiki/Apple_II_graphics)
- [NEC PC-98 intro guide](https://9800guide.uwu.ai/)
- [degenaura: Understanding the NEC PC-98](https://degenaura.wordpress.com/2020/03/05/feature-understanding-the-nec-pc-98/)
- [WebGL Fundamentals: emulating palette-based graphics](https://webglfundamentals.org/webgl/lessons/webgl-qna-emulating-palette-based-graphics-in-webgl.html)
- [ScummVM: understanding graphics settings](https://docs.scummvm.org/en/latest/advanced_topics/understand_graphics.html)
- [VOGONS: True aspect ratio of VGA mode 13h](https://www.vogons.org/viewtopic.php?f=9&t=17110)
- [GosuNoob: Tomb Raider Remastered graphics toggle guide](https://www.gosunoob.com/guides/how-to-change-classic-modern-graphics-in-tomb-raider-remastered/)
- [Game Rant: Diablo II Resurrected legacy mode](https://gamerant.com/diablo-2-resurrected-legacy-mode-toggle/)
- [MobyGames: original/remaster graphic mode switch group](https://www.mobygames.com/group/14236/originalremaster-graphic-mode-switch/)
- Local: `hackdocs_extracted/TLBFORM.TXT`, `docs/dos-inventory.md`, `docs/findings.md`, `docs/amiga-inventory.md`, `docs/dax-format.md`, `docs/daa-format.md`, `docs/hlib-format.md`
