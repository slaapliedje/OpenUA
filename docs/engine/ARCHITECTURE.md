# Gold Box All-in-One Engine & Authoring Suite — Master Architecture

> **Status:** implementation-ready (2026-06-21). This is the master design that the
> ADRs (`adr/`) and the build order (`ROADMAP.md`) hang off. Read `VISION.md` for the
> *why*, this for the *how*, `ROADMAP.md` for the *when*, `ORCHESTRATION.md` for *who builds it*.
>
> Authoritative inputs: `findings.md` (verified formats), `research/*` (Companion, FRUA/DC,
> roster, graphics modes, electron-web, prior-art/licensing), and the shipped
> `web-engine/` TypeScript format library (the seed of layer 2/3).

---

## 0. Design tenets (the rules every module obeys)

1. **One shared TS core; shells diverge only behind the asset-source seam.** The engine,
   loaders, model, VM, rulesets, and UI components are platform-neutral TypeScript. The web
   and Electron shells differ *only* in how raw bytes are fetched (`FetchBytes`) and in
   process/packaging concerns. (ADR-0002, ADR-0007.)
2. **Loaders never know the engine; the engine never knows file formats.** The
   engine-neutral model (layer 3, already shipped in `web-engine/src/model/`) is the only
   shared vocabulary. A loader's whole job is `bytes → neutral model`. This is what lets one
   engine play CoK-from-Amiga, DoK-from-Amiga, DQK-from-DOS with *no engine changes*.
3. **Mechanics live in rulesets, not the engine.** The core engine runs exploration,
   turn order, rendering, the ECL VM, and state; *what a hit roll resolves to* is a ruleset
   plug-in call. AD&D 1e, 1e+Dragonlance, 2e (Savage Frontier), Buck Rogers XXVc, and custom
   systems are data+code plug-ins (ADR-0005).
4. **Clean-room, MIT, no bundled assets.** The ECL VM, combat math, and all logic are
   reimplemented from format docs + our own decoders + independent rules knowledge. We study
   but never copy GPL Dungeon Craft or unlicensed COAB source. No copyrighted game data ever
   enters the repo or a build (ADR-0001).
5. **Everything machine-verifiable.** Per `ORCHESTRATION.md` quality gates: `tsc --noEmit`,
   Vitest unit tests, golden-file parity against the Python decoders for any format work,
   and headless browser e2e (puppeteer-core + Edge, zero console errors / 4xx) for UI/engine.

---

## 1. Layered module map

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  SHELLS  (the only shell-specific code)                                        │
│                                                                                │
│  apps/web                         apps/electron/{main,preload}                 │
│   • Vite SPA, PWA service worker   • BrowserWindow, fs, native folder picker   │
│   • webAssetSource (fetch)         • preload contextBridge → AssetAPI          │
│   • fileSystemAccessSource (opt)   • electronAssetSource (IPC → fs.readFile)    │
│        └──────────────┬────────────────────────┬─────────────────────┘         │
│                       │   implements AssetSource / FetchBytes seam   │         │
└───────────────────────┼──────────────────────────────────────────────┼────────┘
                        ▼                                              ▼
┌────────────────────────────────────────────────────────────────────────────────┐
│  packages/ui-*  (SHARED UI — identical bytes in both shells)                     │
│                                                                                  │
│   ui-player        Player viewport, party HUD, input, GraphicSet switch button   │
│   ui-companion     Automap, journal, monster/item/spell DB, char/save editor,    │
│                    ECL VM inspector  (Gold Box Companion features as panels)      │
│   ui-author        FRUA-superset module editor: maps, events, db editors,        │
│                    ruleset picker, art importer, playtest                         │
│   ui-kit           Shared widgets, indexed<canvas>/WebGL surface, palette UI      │
└───────────────────────────────────────┬──────────────────────────────────────────┘
                                        ▼ consumes engine state + render API
┌────────────────────────────────────────────────────────────────────────────────┐
│  packages/engine  (@goldbox/engine — SHARED CORE, zero DOM / Node / Electron)    │
│                                                                                  │
│  ── Layer 5  render/        indexed→RGBA, EGA/Amiga/VGA palettes, WebGL palette-  │
│               (host-driven)  texture pipeline, color-cycling, aspect correction   │
│                                                                                  │
│  ── Layer 4  engine/        ExplorationLoop, CombatLoop, Party/State, Encounter   │
│               core          GraphicSet manager (live switch), SaveManager;        │
│               + ecl/        EclVM (dialect-parameterized) + EclHost interface     │
│                                                                                  │
│  ── ruleset/  RulesetPlugin registry: addnd1e, addnd1e-dragonlance, addnd2e,      │
│               buckrogers, custom — combat math, classes, XP, spell/psionic res.   │
│                                                                                  │
│  ── Layer 3  model/         engine-neutral types  ← ALREADY SHIPPED               │
│               Palette, IndexedImage, FrameSet, TileSet, SpriteSet, GameMap,       │
│               Monster/Item/StringTable, Script, Sound, LoadedGame                 │
│                                                                                  │
│  ── Layer 2  loaders/       dax · daa · hlib · ilbm  ← ALREADY SHIPPED            │
│               (+ to add)    geo · mon · item · ecl-disasm · save · xmi/voc/dig4   │
│               pure fns over Uint8Array → neutral model                            │
│                                                                                  │
│  ── Layer 1  manifest/      GameManifest schema + loadGame(manifest, fetchBytes)  │
│               graphicSets, ruleset id, entrypoints  ← evolve from shipped seed    │
│                                                                                  │
│  ── seam     AssetSource    FetchBytes type — the ONE I/O dependency, injected     │
└────────────────────────────────────────────────────────────────────────────────┘
```

### Shared vs shell-specific (the dividing line)

| Concern | Where | Shared? |
| --- | --- | --- |
| Format decoding (DAX/DAA/HLIB/IFF/GEO/MON/ITEM/ECL) | `packages/engine/loaders` | **Shared** |
| Engine-neutral model | `packages/engine/model` | **Shared** |
| Engine core, ECL VM, combat, save | `packages/engine/engine`, `ecl` | **Shared** |
| Rulesets | `packages/engine/ruleset` + `packages/ruleset-*` | **Shared** |
| Render pipeline (palette texture, indexed→RGBA) | `packages/engine/render` | **Shared** (host supplies a GL/canvas surface) |
| Player / Companion / Author UI | `packages/ui-*` | **Shared** |
| Reading bytes (fetch vs fs) | shell `*AssetSource.ts` | **Shell-specific** |
| Window, menus, native dialogs, auto-update | `apps/electron/main` | **Shell-specific** |
| PWA service worker, URL routing | `apps/web` | **Shell-specific** |

The engine package is kept pure by **excluding `@types/node` and DOM libs from its
`tsconfig`** and gating it with `tsc --noEmit` in CI (per electron-web research §2).

---

## 2. Key interface contracts (the seams that matter most)

These are the load-bearing seams. Sketches are real TypeScript; field-level detail is filled
in during implementation but the *shape* is fixed here.

### 2.1 Asset-source seam (`AssetSource` / `FetchBytes`)

The single I/O dependency. Already shipped as `FetchBytes`; we widen it to an `AssetSource`
so a shell can also *enumerate* a folder (needed for "point at your install" auto-detection)
without the engine ever touching `fetch`/`fs`.

```ts
/** Resolve a manifest-relative (or absolute, for Electron) path to raw bytes. */
export type FetchBytes = (path: string) => Promise<Uint8Array>;

/** Optional richer source: enumeration + existence, for install auto-detection. */
export interface AssetSource {
  /** Read one file's bytes. Rejects on not-found / outside root. */
  read: FetchBytes;
  /** List entries under a manifest-relative dir (for game auto-detect). Optional. */
  list?: (dir: string) => Promise<string[]>;
  /** Cheap existence check used by manifest probing. Optional. */
  exists?: (path: string) => Promise<boolean>;
  /** Human label of where bytes come from (debug/UI). */
  label: string;
}
```

Web shell implements it over `fetch` (+ optional File System Access API / OPFS). Electron
implements `read` over the preload `contextBridge → ipcRenderer.invoke('fs:readFile')` path,
and `list` over `fs.readdir`, with main-process path-traversal validation against the chosen
game root (electron-web research §3–4). **`loadGame` and every loader accept the source by
injection and call nothing else for I/O.**

### 2.2 GraphicSet abstraction + live-switch mechanism

A **GraphicSet** is a named collection of decoded, engine-ready frames for one platform
variant of one game (DOS-EGA / Amiga / DOS-VGA / future Mac, PC-98). The engine holds an
*active set pointer* and a *per-frame fallback chain*; switching swaps the pointer and
redraws — both sets stay resident (Tomb Raider Remastered / Diablo II model, graphics-modes
research §3). Fallback is **per logical frame, not per file** (Amiga has exclusive frames DOS
lacks, and vice versa).

```ts
export type GraphicSetId = 'DOS-EGA' | 'DOS-VGA' | 'Amiga' | 'PC-98' | 'Mac' | string;

/** A logical, platform-independent handle to one renderable asset. */
export interface LogicalAssetRef {
  category: AssetKind;          // 'pic' | 'bigpic' | 'sprite' | 'cpic' | 'tile8x8' | ...
  index: number;                // block/entry index within the category
}

/** One platform's decoded assets for one game. */
export interface GraphicSet {
  id: GraphicSetId;
  label: string;                // "Amiga (32 colors)"
  display: DisplayProfile;      // native size + pixelAspect + color mode
  /** category → (index → frame). Sparse; missing index ⇒ fall back. */
  frames: Map<AssetKind, Map<number, IndexedImage>>;
  /** True once every referenced container decoded without gaps (UI badge). */
  complete: boolean;
}

export interface GraphicSetManager {
  readonly available: GraphicSetId[];
  readonly active: GraphicSetId;
  readonly fallback: GraphicSetId[];          // priority order, e.g. ['Amiga','DOS-EGA']
  /** Swap the active set; engine state untouched; emits 'change' → host redraws. */
  setActive(id: GraphicSetId): void;
  /** Resolve a logical ref through active set then fallback chain. */
  resolve(ref: LogicalAssetRef): IndexedImage | null;
  /** Does the active set actually have this frame (for index-bounds safety)? */
  has(setId: GraphicSetId, ref: LogicalAssetRef): boolean;
  on(ev: 'change', cb: (id: GraphicSetId) => void): () => void;
}
```

The render layer never reads a container directly — it asks `resolve(ref)` and draws the
returned indexed image through the active palette. Switching is `setActive` + redraw; no
reload (graphics-modes research §3.2, §7).

### 2.3 Ruleset plug-in interface

The hardest abstraction and the one with no prior art (FRUA/DC are both married to AD&D 1e).
The engine calls *into* the ruleset for every mechanics decision; the ruleset never drives the
loop. A ruleset is registered by id and selected by the manifest's `ruleset` field.

```ts
export interface RulesetPlugin {
  id: string;                              // 'addnd1e-dragonlance', 'buckrogers', ...
  label: string;
  /** Class / race / level-limit / XP definitions (data-driven; DC proved this works). */
  classes: ClassDef[];
  races: RaceDef[];
  /** Character creation + advancement. */
  rollAbilities(rng: Rng, race: RaceId, cls: ClassId): AbilityScores;
  xpForLevel(cls: ClassId, level: number): number;
  levelUp(ch: CharacterState, cls: ClassId): CharacterState;
  /** Combat math. */
  thac0(ch: CharacterState): number;
  resolveAttack(attacker: Combatant, defender: Combatant, weapon: Weapon, rng: Rng): AttackResult;
  resolveDamage(res: AttackResult, rng: Rng): number;
  savingThrow(ch: CharacterState, kind: SaveKind, rng: Rng): boolean;
  /** Magic / psionics — same hook serves spells and Buck Rogers psionics. */
  spellbook: SpellDef[];
  castSpell(caster: Combatant, spell: SpellId, targets: Combatant[], ctx: CombatCtx): SpellEffect[];
  memorize(ch: CharacterState, picks: SpellId[]): CharacterState;
  /** Item semantics the ruleset owns (THAC0 adj, class usability, identify rules). */
  itemRules: ItemRules;
  /** Maps an opaque game data record (Monster.raw / Item.raw) to typed mechanics. */
  interpretMonster(raw: Uint8Array, game: string): MonsterStats;
  interpretItem(raw: Uint8Array, game: string): ItemStats;
}

export interface RulesetRegistry {
  register(p: RulesetPlugin): void;
  get(id: string): RulesetPlugin;          // throws if missing
  list(): { id: string; label: string }[];
}
```

`addnd1e` is the base; `addnd1e-dragonlance` extends it (Solamnic Knight, Kender/Tinker Gnome,
lunar magic, god-cleric specials); `addnd2e` overrides THAC0/proficiency; `buckrogers` swaps
classes→careers and spells→psionics. Authoring lets a designer pick a registered ruleset or
supply a custom one (ADR-0005).

### 2.4 ECL VM host interface

The ECL VM is built from scratch (ADR-0003), parameterized by an **opcode dialect** table
(PoR / Krynn-Gen1 / Savage-Frontier / DQK-Gen2), and it talks to the rest of the engine only
through a narrow `EclHost`. This keeps the VM testable in isolation and lets the same VM serve
play, the Companion ECL inspector, and authoring playtest.

```ts
export interface EclDialect {
  id: string;                              // 'krynn-gen1' | 'dqk-gen2' | ...
  /** opcode byte → handler name + operand decoder. Loaded from config, not hardcoded. */
  ops: Map<number, EclOpSpec>;
  decrypt?(block: Uint8Array): Uint8Array; // Gen1 ECL is obfuscated; Gen2 differs
}

/** Everything the VM can do to the world — implemented by the engine, mocked in tests. */
export interface EclHost {
  // flags / variables
  getFlag(id: number): number;
  setFlag(id: number, v: number): void;
  // narrative + UI (async: VM suspends until host resolves)
  showText(s: string): Promise<void>;
  ask(question: string, options: string[]): Promise<number>;
  // world / party mutation
  giveTreasure(t: TreasureSpec): Promise<void>;
  startCombat(enc: EncounterSpec): Promise<CombatOutcome>;   // outcome IS exposed (fix FRUA gap)
  teleport(map: string, x: number, y: number, facing: number): void;
  passTime(mins: number): void;
  transferModule(target: string, entry: number): void;
  party(): PartyView;
  // ruleset bridge
  ruleset: RulesetPlugin;
}

export interface EclVM {
  load(script: Script, dialect: EclDialect): EclProgram;
  /** Run a program/event to completion or suspension; cooperative + async. */
  run(prog: EclProgram, host: EclHost, entry?: number): Promise<EclResult>;
  /** Single-step for the Companion inspector. */
  step(prog: EclProgram, host: EclHost): Promise<EclStep>;
  disassemble(prog: EclProgram): EclOp[];  // powers ECL-Tool panel
}
```

`startCombat` returning a real `CombatOutcome` is a deliberate improvement over FRUA's
inability to distinguish "won" from "fled" (FRUA/DC research §5.5).

### 2.5 Authoring "module" data model

A folder-based design package (conceptually like DC's `.dsn`, independently designed — ADR-0006),
JSON-manifest + PNG/own-binary assets + text databases. It is a *superset* of the loaded-game
model: a playable module loads through the *same* `loadGame`/engine path as an original game.

```ts
export interface ModulePackage {
  schema: 1;
  id: string;
  title: string;
  ruleset: string;                          // a registered RulesetPlugin id
  display: DisplayProfile;
  graphicSets: GraphicSetSpec[];            // 1..n; authored modules usually 1
  entrypoints: EntryPoint[];                // replaces FRUA's fixed "entry point 1"
  maps: ModuleMapRef[];                     // unlimited (no FRUA 40-place cap)
  databases: {                              // text/JSON, per-module (DC model)
    monsters: string; items: string; spells: string;
    classes: string; races: string; abilities: string;
  };
  scripts: ModuleScriptRef[];               // events compiled to ECL-compatible bytecode OR our IR
  strings: string;                          // string table file
  assets: ModuleAssetRef[];                 // art/audio (PNG/WebP/own HLIB; OGG/XMI)
}

export interface ModuleMap {
  name: string;
  width: number; height: number;            // no hardcoded ceiling
  kind: 'dungeon' | 'overland';
  cells: MapCell[];
  events: ModuleEvent[];                    // >100 allowed; variables + branching
  zones: ZoneInfo[];
  slots: MapArtSlots;
}

/** A FRUA event type + modern extensions (variables, arithmetic, combat-outcome branch). */
export interface ModuleEvent {
  id: number;
  type: EventType;                          // superset of FRUA's 35+ types
  trigger: TriggerCondition;                // FRUA conditions + general boolean expr
  body: EventBody;                          // typed per EventType; compiles to VM bytecode
}
```

FRUA import maps the 36-dungeon/4-overland `.DSN` layout and 35+ event types into this model
(ADR-0006). Authoring writes both the editable `ModulePackage` and, on export, the
engine-runnable artifacts.

---

## 3. Manifest evolution (layer 1)

The shipped manifest (`web-engine/games/*.json`, `GameManifest` in `loadGame.ts`) is
single-platform: a flat `files[]` with a `category`. We evolve it **without breaking the
viewer** by adding optional fields, then making `graphicSets` the primary axis.

```jsonc
{
  "id": "champions-of-krynn",
  "title": "Champions of Krynn",
  "ruleset": "addnd1e-dragonlance",          // NEW — selects RulesetPlugin
  "display": { "width": 320, "height": 200, "pixelAspect": 1.2 },

  "graphicSets": {                            // NEW — replaces flat files[] as primary
    "Amiga": {
      "label": "Amiga (32 colors)",
      "display": { "mode": "amiga32" },
      "containers": {                         // category → files (per-game, per-platform)
        "pic":    ["pic1.daa", "pic2.daa"],
        "sprite": ["SPRIT1.DAA", "SPRIT2.DAA"],
        "bigpic": ["BIGPIC1.DAA", "BIGPIC2.DAA"]
      },
      "palette": "embedded-daa"               // or "ega16-fixed" / "embedded-tlb"
    },
    "DOS-EGA": {
      "label": "DOS EGA (16 colors)",
      "display": { "mode": "ega16" },
      "containers": { "pic": ["PIC1.DAX"], "sprite": ["SPRIT1.DAX"] },
      "palette": "ega16-fixed"
    }
  },
  "graphicSetFallback": ["Amiga", "DOS-EGA"], // NEW — per-frame fallback order

  "data": {                                   // NEW — non-graphics, shared across sets
    "geo":   ["GEO1.DAX"],
    "ecl":   { "files": ["ECL1.DAX"], "dialect": "krynn-gen1" },
    "mon":   ["MONCHA.DAX"],
    "item":  ["ITEM.DAX"],
    "strings": ["GLOBAL.DAX"],
    "save":  { "format": "krynn-gen1" }
  },

  "entrypoints": [{ "id": 1, "map": "GEO1", "x": 10, "y": 10, "facing": 0 }]
}
```

`loadGame` is extended to: (a) build one `GraphicSet` per `graphicSets` entry, (b) load shared
`data`, (c) attach the `ruleset` and `entrypoints`. The existing flat-`files[]` form is kept as
a back-compat path so the current viewer/deploy keeps working through the migration (ADR-0002).
Per-game manifests for the broader roster (PoR, CoAB, Savage Frontier, Buck Rogers) are just
data — no engine change.

---

## 4. Monorepo layout & migration path

Target (pnpm workspaces + electron-vite + electron-builder + optional Turborepo — electron-web
research §2, §5; ADR-0002):

```
goldbox/
├── pnpm-workspace.yaml          packages/*, apps/*, apps/electron/*
├── package.json                 root scripts: dev, build:all, test, release
├── turbo.json                   (optional caching)
├── packages/
│   ├── engine/                  @goldbox/engine  ← was web-engine/ (pure: no DOM/Node)
│   │   └── src/{loaders,model,render,engine,ecl,ruleset,manifest}/
│   ├── ruleset-addnd1e/         @goldbox/ruleset-addnd1e (+ -dragonlance, -addnd2e, -buckrogers)
│   ├── ui-kit/                  shared widgets + indexed render surface
│   ├── ui-player/               player viewport + HUD + graphic-set switch
│   ├── ui-companion/            automap, journal, DBs, editors, ECL inspector
│   └── ui-author/               FRUA-superset editor
├── apps/
│   ├── web/                     @goldbox/web-shell — Vite SPA + PWA; *AssetSource.ts
│   └── electron/
│       ├── main/                BrowserWindow, IPC, fs (path-validated)
│       └── preload/             contextBridge → typed AssetAPI
├── docs/  tools/  test/  games/ (manifests only — NO assets)
```

**Migration (each step a standalone, independently verifiable PR — preserves the live
`dionysus.dk/goldbox/` deploy throughout):**

1. Add `pnpm-workspace.yaml`; move `web-engine/` → `packages/engine/`; split the current
   `src/ui/` out to `apps/web/`. Gate: `pnpm -r typecheck` + existing Vitest + e2e green;
   redeploy `/goldbox/` and re-verify headless (zero console errors / 4xx).
2. Add `apps/electron/` via electron-vite; renderer points at `apps/web` build; implement the
   preload bridge + `electronAssetSource.ts`. Gate: app boots, picks a folder, renders the
   same game the web shell does.
3. Extract `ui-kit` from `apps/web/src/ui`; both shells consume it. Gate: visual parity.
4. Introduce `ruleset-*` packages + the registry; wire `manifest.ruleset`. Gate: ruleset unit
   tests (THAC0/XP/save tables) vs known AD&D values.
5. Land engine subdirs (`engine/`, `ecl/`) as they're built per ROADMAP — additive, no move.

The seed is already correctly shaped (`FetchBytes` injected; loaders pure `Uint8Array` fns;
Vite+Vitest in place), so migration is mechanical, not a rewrite (electron-web research §9).

---

## 5. Cross-cutting concerns

- **Rendering** (ADR-0007): one WebGL indexed pipeline — an `ALPHA`/`R8` index texture +
  256×1 RGBA palette texture, `NEAREST` filtering, fragment shader does the palette lookup.
  One pipeline serves CGA-4 / EGA-16 / Amiga-32 / VGA-256 with only the palette texture
  changing; color-cycling = per-frame palette re-upload; aspect correction = 1.2× vertical in
  the final blit. Canvas2D `ImageData` is the fallback path. The engine returns indices +
  palette (already shipped); the host owns the GL surface.
- **Audio**: XMI→MIDI (WebAudio/soundfont) and VOC/DIG4→PCM decoders are loaders into
  `Sound`; the player has a thin WebAudio sink. Deferred until exploration is playable.
- **Save/load**: `SaveManager` owns engine state serialization (our own JSON format for
  authored play) plus *readers* for original SAVGAM/.CCH formats (Companion save-editor +
  character import). We own the state, so the Companion "edit char/save" features are panel
  calls, not RAM scans (gold-box-companion research §5.1).
- **Determinism / RNG**: a seedable `Rng` is threaded through ruleset + combat so combats are
  reproducible (testability + golden combat logs).
- **Testing**: format work → golden-file pixel/byte parity vs `tools/*_decode.py`; engine/VM →
  Vitest with a mock `EclHost`; UI → puppeteer-core+Edge headless e2e; combat → seeded golden
  logs. (ORCHESTRATION quality gates.)

---

## 6. Critical-path unknowns (where they gate which layer)

| Unknown | Gates | Mitigation / first probe |
| --- | --- | --- |
| **ECL opcode tables / dialect drift** (no public enumeration) | Any *gameplay* (events, dialogue, combat triggers). Highest risk. | Build VM scaffold + disassembler first against DQK-Gen2 `DATA` chunks; derive opcodes empirically (cross-check GBC ECL-Monitor behavior); COAB as *reference only* pending license (ADR-0001, ADR-0003). |
| **Amiga DAA palette storage** (where the 32-color CLUT lives) | The **Amiga GraphicSet** — i.e. CoK/DoK "best art" and the live-switch demo. | Reverse `pic1.daa`/`SPRIT1.DAA` against known DOS renders; inspect `globals.daa`. #1 decoder gap (graphics-modes research §4.3, findings §5.1). |
| **GEO map decode for DAX (CoK/DoK)** (HLIB GEO documented; DAX GEO "similar not identical") | Exploration on CoK/DoK; automap. | Decode DQK GEO first (documented), then diff CoK/DoK; model already stubs `GameMap`. |
| **Save format per game** (SAVGAM/.CCH layouts only partially decoded) | Companion save-editor, character import/export between games. | Lower priority; engine owns its *own* save first, original-format readers follow. |
| **DAX 8x8 tile plane-order + transparent sub-frame variants** (unsolved) | Dungeon wall rendering on DAX sets; some sprites. | Known gap in shipped loaders; solve when dungeon exploration needs it. |

Sequencing principle (see ROADMAP): make **DQK playable first** — it has the documented HLIB
formats and the richest data — so the engine/VM/combat/ruleset stack is proven on the
best-documented game *before* tackling the DAA-palette and DAX-GEO unknowns that the
CoK/DoK/Amiga path depends on.
