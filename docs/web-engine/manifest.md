# Per-Game Manifest Schema

**Layer 1** — the "simple config that points the engine at the right files." One JSON per
game (or per art-source variant). Loading CoK-with-Amiga-art, DoK-with-Amiga-art, DQK, or a
custom scenario is *just another manifest* — the engine and loaders never change, only this
file. Supersedes the sketch in [`../web-engine-plan.md`](../web-engine-plan.md) "game
manifest" section with a fuller schema and real examples.

The manifest tells the loader system three things per asset: **where** the file is, **how** to
decode it (container + endianness + variant), and **what** it is (category → which neutral
type and which palette/context it belongs to).

---

## Schema

```jsonc
{
  "id": "champions-of-krynn-amiga",   // unique; also the LoadedGame.id
  "title": "Champions of Krynn",
  "ruleset": "goldbox-krynn-1",       // mechanics/level-ranges/spell tables (engine-side)
  "engineGeneration": "dax",          // "dax" (older CoK/DoK) | "hlib" (DQK) — drives ECL opcode table

  "display": {
    "mode": "amiga32",                // "ega16" | "amiga32" | "vga256"  (DisplayProfile)
    "width": 320, "height": 200
  },

  "assets": {
    "root": "games/cok-amiga/",       // base path for all files below
    "container": "daa",               // default container: "dax" | "daa" | "hlib" | "iff" | "mixed"
    "endian": "big",                  // "little" | "big" (default per container; explicit wins)
    "palette": {
      "strategy": "embedded",         // "fixed-ega" | "embedded" | "hlib-range"
      "global": "globals.daa"         // optional shared-palette source (8X8D* tiles)
    },

    // Per-category file lists. Each entry: a filename (uses defaults above) OR an object
    // overriding container/endian/loader/kind/palette for that file.
    "graphics": [
      "BIGPIC1.DAA", "bigpic2.daa", "CPIC1.DAA", "cpic2.daa",
      { "file": "SPRIT1.DAA", "kind": "sprite", "note": "6-byte sub-frame index — UNSOLVED, viewer lists only" },
      { "file": "8X8D0.DAA", "kind": "tile8x8", "palette": "globals.daa" },
      { "file": "scrn1b.lbm", "container": "iff", "endian": "big", "kind": "title" },
      { "file": "Title1.LBM", "container": "iff", "kind": "title" }
    ],
    "maps":     ["GEO1.DAX", "GEO2.DAX"],          // note: maps/scripts/tables stay DOS DAX on Amiga
    "scripts":  ["ECL1.DAX", "ECL2.DAX"],
    "monsters": ["MON1CHA.DAX", "MON1ITM.DAX", "MON1SPC.DAX"],
    "items":    ["ITEM1.DAX"],
    "sound":    [{ "file": "Sound/krynn", "container": "raw-amiga-8bit", "kind": "music" }]
  },

  // asset-name (stem) → loader hint overrides, when filename heuristics aren't enough.
  "loaderMap": {
    "8X8D0":  { "loader": "daa-tiles",   "tile": [8, 8] },
    "SPRIT1": { "loader": "daa-subframe", "supported": false }
  },

  "entry": { "startMap": "GEO1#0", "startScript": "ECL1#0" }  // optional; for playable mode
}
```

### Field notes
- **`container` + `endian`** seed the loader; individual files override (an Amiga game still
  ships DOS-format `GEO/ECL/MON/ITEM` — see CoK below — so `"container":"mixed"` at top with
  per-file specs, or per-category defaults).
- **`palette.strategy`**: `fixed-ega` (DAX → EGA16 table), `embedded` (DAA per-frame / IFF
  CMAP), `hlib-range` (HLIB per-leaf slices + inheritance). `global` names a shared-palette
  file for tiles that carry none.
- **`kind`** maps to `AssetKind` (asset-model.md) and decides which neutral type the file
  becomes (`FrameSet`/`TileSet`/`SpriteSet`) and which **context palette** it contributes to
  (e.g. WILDCOM/DUNGCOM for HLIB layering).
- **`supported:false` / `note`**: declares a known-UNSOLVED variant so the engine degrades
  gracefully (lists/skips instead of crashing) — DAA 6-byte sub-frames, HLIB method 25, etc.
- **`ruleset` vs `engineGeneration`**: `ruleset` is game mechanics; `engineGeneration` selects
  the ECL opcode table / GEO variant (handles opcode drift, see
  [`game-data-and-ecl.md`](./game-data-and-ecl.md)).

---

## Example — Champions of Krynn (Amiga DAA art, best graphics)

Art from Amiga (DAA + LBM); game logic/data from the DOS DAX files (Amiga ships DOS-format
`GEO/ECL/MON/ITEM` — confirmed in `../amiga-inventory.md`).

```jsonc
{
  "id": "champions-of-krynn-amiga", "title": "Champions of Krynn",
  "ruleset": "goldbox-krynn-1", "engineGeneration": "dax",
  "display": { "mode": "amiga32", "width": 320, "height": 200 },
  "assets": {
    "root": "games/cok-amiga/", "container": "mixed",
    "palette": { "strategy": "embedded", "global": "globals.daa" },
    "graphics": [
      "BIGPIC1.DAA", "bigpic2.daa", "CPIC1.DAA", "cpic2.daa", "comspr.daa",
      "DUNGCOM.DAA", "WILDCOM.DAA", "SKY.DAA",
      { "file": "8X8D0.DAA", "container": "daa", "kind": "tile8x8", "palette": "globals.daa" },
      { "file": "SPRIT1.DAA", "container": "daa", "kind": "sprite", "supported": false },
      { "file": "HEAD1.DAA",  "container": "daa", "kind": "cpic",   "supported": false },
      { "file": "scrn1b.lbm", "container": "iff", "endian": "big", "kind": "title" },
      { "file": "scrn2.lbm",  "container": "iff", "endian": "big", "kind": "title" }
    ],
    "maps":     [ { "file": "GEO1.DAX", "container": "dax", "endian": "little" } ],
    "scripts":  [ { "file": "ECL1.DAX", "container": "dax", "endian": "little" } ],
    "monsters": [ { "file": "MON1CHA.DAX","container":"dax","endian":"little" },
                  { "file": "MON1ITM.DAX","container":"dax","endian":"little" },
                  { "file": "MON1SPC.DAX","container":"dax","endian":"little" } ],
    "items":    [ { "file": "ITEM1.DAX", "container": "dax", "endian": "little" } ]
  }
}
```

## Example — Death Knights of Krynn (Amiga DAA art)

```jsonc
{
  "id": "death-knights-of-krynn-amiga", "title": "Death Knights of Krynn",
  "ruleset": "goldbox-krynn-2", "engineGeneration": "dax",
  "display": { "mode": "amiga32", "width": 320, "height": 200 },
  "assets": {
    "root": "games/dok-amiga/", "container": "mixed",
    "palette": { "strategy": "embedded", "global": "globals.daa" },
    "graphics": [
      "BIGPIC1.DAA", "BIGPIC2.DAA", "BACK1.DAA", "COMSPR.DAA", "DUNGCOM.daa", "WILDCOM.daa",
      { "file": "CPIC1.daa", "container": "daa", "kind": "cpic" },
      { "file": "PIC1.DAA",  "container": "daa", "kind": "pic", "supported": false },
      { "file": "PIC2.DAA",  "container": "daa", "kind": "pic", "supported": false },
      { "file": "SPRIT1.DAA","container": "daa", "kind": "sprite", "supported": false },
      { "file": "8x8d1.daa", "container": "daa", "kind": "tile8x8", "palette": "globals.daa" },
      { "file": "Title1.LBM","container": "iff", "endian": "big", "kind": "title" },
      { "file": "Title1A.LBM","container":"iff", "endian": "big", "kind": "title" }
    ],
    "maps":     [ { "file": "GEO1.DAX", "container": "dax", "endian": "little" } ],
    "scripts":  [ { "file": "ECL1.DAX", "container": "dax", "endian": "little" } ],
    "monsters": [ { "file": "mon1cha.dax","container":"dax","endian":"little" } ],
    "items":    [ { "file": "ITEM0.DAX", "container": "dax", "endian": "little" } ]
  }
}
```

## Example — The Dark Queen of Krynn (DOS HLIB, best graphics)

DQK uses HLIB throughout: `.TLB` for graphics (masters + leaves), `.GLB` for ECL/GEO/sound.
Note the **context palettes** (WILDCOM/DUNGCOM) so combat-icon leaves (which carry no palette)
inherit correctly.

```jsonc
{
  "id": "dark-queen-of-krynn-dos", "title": "The Dark Queen of Krynn",
  "ruleset": "goldbox-krynn-3", "engineGeneration": "hlib",
  "display": { "mode": "vga256", "width": 320, "height": 200 },
  "assets": {
    "root": "games/dqk-dos/", "container": "hlib", "endian": "little",
    "palette": { "strategy": "hlib-range" },
    "graphics": [
      "TITLE.TLB", "ALWAYS.TLB", "FRAME.TLB", "GEN.TLB",
      "BIGPIC.TLB", "PICA.TLB", "PICB.TLB", "PICC.TLB",
      "8X8DB.TLB", "8X8DC.TLB", "BACK.TLB", "SPRIT.TLB", "CPIC.TLB", "CBODY.TLB",
      { "file": "WILDCOM.TLB", "kind": "back", "contextPalette": "WILDCOM" },
      { "file": "DUNGCOM.TLB", "kind": "back", "contextPalette": "DUNGCOM" },
      { "file": "COMSPR.TLB", "kind": "comspr" },
      { "file": "TOPVIEW.TLB", "kind": "topview", "note": "no palette — inherits context" }
    ],
    "maps":     [ { "file": "GEO.GLB", "container": "hlib", "memberTag": "DATA" } ],
    "scripts":  [ { "file": "ECL.GLB", "container": "hlib", "memberTag": "DATA" } ],
    "monsters": [ { "file": "MONCHA.GLB", "container": "hlib", "memberTag": "DATA" } ],
    "items":    [ { "file": "ITEM.DAT", "container": "raw", "kind": "item-table" } ],
    "sound":    [ { "file": "SOUNDS.GLB", "container": "hlib", "memberTag": "DIG4", "supported": false },
                  { "file": "SFXDQ.VOC", "container": "voc", "kind": "sfx" },
                  { "file": "RODQ1.XMI", "container": "xmi", "kind": "music", "device": "mt32" } ]
  }
}
```

## Example — custom scenario stub (FRUA-superset)

A brand-new scenario authored in the engine's own format (no original game files). Uses the
richest generation natively; authoring tools (roadmap phase 6) write these.

```jsonc
{
  "id": "my-scenario", "title": "The Forgotten Vale",
  "ruleset": "goldbox-krynn-3", "engineGeneration": "hlib",
  "display": { "mode": "vga256", "width": 320, "height": 200 },
  "assets": {
    "root": "scenarios/forgotten-vale/", "container": "hlib", "endian": "little",
    "palette": { "strategy": "hlib-range" },
    "graphics": ["custom-bigpic.tlb", "custom-sprites.tlb"],
    "maps":     ["module01.geo.json"],     // authored maps may use a JSON sidecar form
    "scripts":  ["module01.ecl"],
    "monsters": ["bestiary.json"],
    "items":    ["loot.json"]
  },
  "entry": { "startMap": "module01#0", "startScript": "module01#0" }
}
```

---

## Loading algorithm (manifest → `LoadedGame`)
1. Resolve each file path against `root`; fetch as `Uint8Array`.
2. Pick the loader from `container`/`memberTag` (or per-file override / sniff).
3. Decode → neutral type; assign to the right `LoadedGame` map by `kind`.
4. Register `contextPalette` entries so HLIB inheritance/layering works at render time.
5. Honor `supported:false` — list/skip the asset, surface it in the viewer as
   "known-unsolved," never abort the load.
