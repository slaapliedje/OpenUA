# Engine-Neutral Asset Model

This is **layer 3** — the in-memory data model that every loader decodes *into* and the
engine consumes. It is the single shared vocabulary between the format library (layer 2) and
the engine core (layer 4). No type here references a file format; no loader emits anything
else.

Design rule: the model is shaped after the **richest** generation (DQK/HLIB — 256-color,
per-image palette, per-frame placement offsets, transparency). The older EGA (DAX) and Amiga
(DAA) formats convert *up* into it losslessly: a 16-color EGA frame is just an
`IndexedImage` whose palette has 16 used entries; a 32-color Amiga frame, 32. See
[`palettes-and-rendering.md`](./palettes-and-rendering.md) for the color details and
[`loaders.md`](./loaders.md) for how each format populates these structures.

All images are stored as **8-bit indices + a palette** (never pre-rendered RGBA), so the
render layer can apply palette inheritance, color-cycling, and platform display profiles at
draw time. Index **255 is the convention for transparent** (inherited from HLIB methods
17/21/23); EGA/Amiga loaders that have a real index-0-is-transparent rule remap to this
convention or set `transparentIndex` explicitly.

---

## Core graphics types

```ts
/** An RGB color, 0..255 per channel (already scaled — no DAC/×17/×4 left to do). */
export interface RGB { r: number; g: number; b: number; }

/**
 * A 256-entry palette. Entries the source didn't define are null (so a renderer can
 * detect "inherit from previously loaded palette" — see HLIB first_col/ncolors slicing).
 * EGA loaders fill 0..15; Amiga fill 0..31; HLIB fill only [first_col, first_col+ncolors).
 */
export interface Palette {
  /** 256 slots; null = undefined-by-this-source (inherit at render time). */
  colors: (RGB | null)[];
  /** Optional color-cycling ranges (HLIB only; empty elsewhere). */
  cycles?: ColorCycle[];
  /** Provenance for debugging / layering decisions. */
  source: 'ega-fixed' | 'amiga-12bit' | 'hlib-range' | 'iff-cmap';
  /** For hlib-range: the slice this palette actually defines. */
  firstColor?: number;   // index of first defined color
  count?: number;        // number of defined colors
}

export interface ColorCycle {
  dir: number;     // direction
  speed: number;   // ticks per step
  start: number;   // first palette index in the band
  count: number;   // band length
}

/**
 * One decoded image / frame. Pixels are palette indices, row-major, width*height bytes.
 * Placement offsets are the source's signed hints (DAX/DAA x/y_offset, HLIB v/h offset);
 * the engine uses them to position sub-frames; the bare viewer ignores them.
 */
export interface IndexedImage {
  width: number;
  height: number;
  /** width*height palette indices, row-major, top-left origin. */
  indices: Uint8Array;
  /** Placement hints from the frame header (default 0). */
  xOffset: number;
  yOffset: number;
  /** Index treated as transparent (default 255). null = fully opaque. */
  transparentIndex: number | null;
  /**
   * Palette to draw this image with. May be partial (inherit nulls). If null, the engine
   * uses the currently-loaded contextual palette (cpic/TOPVIEW carry no palette).
   */
  palette: Palette | null;
  /** Source drawing method / encoding, kept for diagnostics (e.g. HLIB 16/17/18/21/23). */
  encoding?: string;
}

/** A named container of frames, addressed by the source block/image id. */
export interface FrameSet {
  /** Stable id: source filename stem, e.g. "BIGPIC1". */
  name: string;
  /** Keyed by the source's sparse block_id (DAX/DAA) or positional image index (HLIB). */
  frames: Map<number, IndexedImage>;
  /** Coarse category from the manifest / filename heuristic. */
  kind: AssetKind;
}

export type AssetKind =
  | 'bigpic' | 'pic' | 'cpic' | 'back' | 'sprite' | 'comspr'
  | 'tile8x8' | 'walldef' | 'title' | 'frame-ui' | 'topview'
  | 'cbody' | 'chead' | 'cursor' | 'sky' | 'unknown';
```

---

## Tiles & sprites (engine-level groupings over `IndexedImage`)

```ts
/**
 * A set of fixed-size tiles (8x8 dungeon tiles, wall textures). DAX/DAA store these as
 * vertical strips that the loader splits into uniform cells; HLIB stores them per image.
 * NOTE: 8x8 tile plane-order / shared palette is UNSOLVED for DAX & DAA (see loaders.md);
 * the loader still produces correctly-sized cells, only the colors may be wrong.
 */
export interface TileSet {
  name: string;
  tileWidth: number;   // 8 (or 16-wide DAX strip pairs) / wall width
  tileHeight: number;
  tiles: IndexedImage[];
  palette: Palette | null;
}

/** A logical sprite (one creature/NPC), possibly multiple animation frames. */
export interface Sprite {
  id: number;
  frames: IndexedImage[];   // each already transparency-keyed
}

export interface SpriteSet {
  name: string;
  sprites: Map<number, Sprite>;
  palette: Palette | null;
}
```

---

## Maps (GEO)

Shape follows the FRUA-era `geo*.dat` layout documented in `hackdocs_extracted/GEODATA.TXT`
(verified for the DQK/FRUA lineage). CoK/DoK DAX `GEO*` payloads are **structurally similar
but not byte-identical** and are not yet fully decoded — see
[`game-data-and-ecl.md`](./game-data-and-ecl.md). The model captures what an engine needs;
the loader fills what it can decode and leaves the rest `undefined`.

```ts
export interface GameMap {
  name: string;
  width: number;
  height: number;
  kind: 'dungeon' | 'overland';
  /** width*height cells, row-major. */
  cells: MapCell[];
  /** Art slot references resolved against the manifest's asset sets. */
  slots: {
    walls: number[];          // wall art slots (1-3; 255 = none, overland)
    backdrops: number[];      // dungeon backdrops 1-4 / overland bigpic slot
    dungeonCombatArt: number;
    wildernessCombatArt: number;
  };
  entries: MapEntry[];        // up to 8 entry points
  zones: ZoneInfo[];          // up to 8 zones (names, rest/step events)
  events: MapEvent[];         // encounter/event table (see ECL doc)
  monsterSummoned?: number;
}

export interface MapCell {
  /** Per-direction wall/edge codes packed by the source (6-byte GEO record). */
  walls: [number, number, number, number]; // N,E,S,W (interpretation TBD)
  eventId: number;                          // index into events[] (0 = none)
}

export interface MapEntry { col: number; row: number; facing: number; }
export interface ZoneInfo { name: string; restEvent?: number; stepEvent?: number; }
export interface MapEvent { type: number; raw: Uint8Array; } // 20-byte record; typed by ECL doc
```

---

## Tables (monsters, items) & strings

```ts
/**
 * Monster record. CoK/DoK split these across MON*CHA/ITM/SPC/WIZ DAX files; DQK packs
 * MONCHA.GLB. Field layout differs per game and is only PARTIALLY decoded — the loader
 * fills decoded fields and keeps the raw record for the engine/ruleset to interpret.
 */
export interface Monster {
  id: number;
  name?: string;            // 6-bit packed (see StringTable note)
  hp?: number; ac?: number; thac0?: number;
  combatIconSlot?: number;  // links to a cbody/cpic FrameSet
  raw: Uint8Array;          // full record, ruleset-interpreted
}
export interface MonsterTable { name: string; monsters: Map<number, Monster>; }

/** Item record — 18-byte layout per hackdocs ITEM.TXT (FRUA/DQK; CoK/DoK close). */
export interface Item {
  id: number;
  nameCodes: [number, number, number];  // 3 vocab indices (read byte3..byte1)
  encumbrance?: number; pricePP?: number; magicBonus?: number;
  charges?: number; cursed?: boolean; readied?: boolean;
  raw: Uint8Array;
}
export interface ItemTable { name: string; items: Map<number, Item>; vocab?: string[]; }

/**
 * Decoded strings. The series packs names/text 6 bits/char (see STRGFORM.TXT / GEODATA.TXT):
 * values 32-63 verbatim; 1-31 add 64 (→65-95); 0 terminates. The loader expands to UTF-8.
 */
export interface StringTable {
  name: string;
  strings: string[];        // already expanded from 6-bit packing
}
```

---

## Scripts (ECL) & sound

```ts
/**
 * ECL = the event/dialogue/combat bytecode. The VM is FUTURE WORK (see game-data-and-ecl.md);
 * this model just carries the decoded program so the loader and VM are decoupled. The VM
 * spec of record is the Simeon Pilgrim COAB reimplementation, not hackdocs OPCODES.TXT
 * (which is an x86 reference for the editor, NOT the ECL VM).
 */
export interface Script {
  name: string;
  id: number;
  bytecode: Uint8Array;      // raw ECL program for one block
  /** Decoded form once the VM/disassembler exists; null until then. */
  ops?: EclOp[] | null;
}
export interface EclOp { opcode: number; args: number[]; offset: number; }

/** Decoded audio in a neutral form the WebAudio layer can play. */
export interface Sound {
  name: string;
  kind: 'pcm' | 'midi';
  /** pcm: mono samples; midi: a normalized MIDI event list. */
  pcm?: { sampleRate: number; samples: Int16Array };
  midi?: MidiTrack;
}
export interface MidiTrack { ticksPerBeat: number; events: MidiEvent[]; }
export interface MidiEvent { tick: number; status: number; data: number[]; }
```

---

## The loaded-game aggregate

What a manifest load produces — the engine boots from exactly this and nothing else:

```ts
export interface LoadedGame {
  id: string;                         // manifest id
  ruleset: string;                    // mechanics profile name (engine-side)
  display: DisplayProfile;            // native size + scaling intent
  frameSets: Map<string, FrameSet>;   // by stem: BIGPIC1, SPRIT2, TITLE, ...
  tileSets: Map<string, TileSet>;
  spriteSets: Map<string, SpriteSet>;
  maps: Map<string, GameMap>;
  monsters: Map<string, MonsterTable>;
  items: Map<string, ItemTable>;
  strings: Map<string, StringTable>;
  scripts: Map<string, Script[]>;
  sounds: Map<string, Sound>;
  /** Palettes by context name for inheritance/layering (WILDCOM, DUNGCOM, ...). */
  contextPalettes: Map<string, Palette>;
}

export interface DisplayProfile {
  width: number;   // 320
  height: number;  // 200
  mode: 'ega16' | 'amiga32' | 'vga256';
}
```

> **Verification anchor:** a loader is "correct against the neutral model" when the
> `IndexedImage` it produces, rendered through `palette` (or the inherited context palette),
> is byte-identical to the corresponding Python decoder's PNG. The model carries indices +
> palette precisely so that golden-file comparison is exact, not approximate. See
> [`roadmap.md`](./roadmap.md) golden-file testing.
