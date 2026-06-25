# Web Gold Box Engine — Plan (EXTRA / future goal)

> **Status: planning only — do not build yet.** This is the *secondary* goal. The
> **primary** goal remains: two DOS executables built on the DQK engine that act as CoK
> and DoK and play the way those games originally did (see `../CLAUDE.md`). This document
> records the web-engine idea and, importantly, *where it makes sense to start*.

> **See [`docs/web-engine/`](web-engine/) for the full technical spec** (architecture,
> engine-neutral asset model, loader ports, palettes/rendering, game-data & ECL, manifest
> schema, and roadmap) that expands on this plan.

## The idea (from the user)

Once a good decompile exists, build a **browser version of the DQK engine** that:
- loads original game assets freely from their source files,
- works like *Unlimited Adventures (FRUA)* but **broader** — able to host the whole Gold
  Box series, not just one ruleset,
- lets you **load any existing Gold Box game** via a **simple per-game config** that points
  the engine at that game's source files, and play it in the browser **with the original
  graphics**,
- also supports **modern custom scenarios** (the FRUA-style authoring angle),
- **supports all the different asset/container types** used across the Gold Box series
  (DAX, Amiga DAA, HLIB/TLB/GLB, IFF/LBM, …).

This is a natural superset of the main goal: the main goal needs code that *reads* Amiga
DAA + DOS DAX and *writes* DQK HLIB; the web engine needs to *read everything and render
it*. Both sit on the **same format library**, which is why that library is the right place
to start for both goals.

## Why DQK is the right engine to base it on

DQK is the **newest and most capable** Krynn-era engine: VGA 256-color, the **HLIB**
container generation, and the lineage that FRUA itself descends from. Targeting the DQK
data model first means the web engine's native representation is already the richest, and
older games (EGA DAX, Amiga DAA) convert *up* into it losslessly.

## Recommended architecture (layered)

```
┌─────────────────────────────────────────────────────────────┐
│ UI / shell (browser): canvas+WebGL renderer, input, audio    │
├─────────────────────────────────────────────────────────────┤
│ Engine core: map/exploration, combat, party, ECL bytecode VM │
├─────────────────────────────────────────────────────────────┤
│ Game model (engine-neutral): tiles, sprites, frames+palette, │
│   maps/GEO, monsters, items, encounters, strings, scripts    │
├─────────────────────────────────────────────────────────────┤
│ Format library (the foundation — shared with the DOS goal):  │
│   loaders: DAX · DAA(Amiga) · HLIB(TLB/GLB) · IFF/ILBM        │
│   each decodes a container → the engine-neutral game model    │
├─────────────────────────────────────────────────────────────┤
│ Game manifest (JSON): per-game config — which files, which    │
│   loader/variant, palette/display profile, ruleset, entrypts  │
└─────────────────────────────────────────────────────────────┘
```

### The game manifest (the "simple config" the user described)
One JSON per game points the engine at source files and declares how to read them, e.g.:

```jsonc
{
  "id": "champions-of-krynn",
  "ruleset": "goldbox-krynn-1",      // mechanics/level ranges/spell tables
  "display": { "mode": "vga256", "width": 320, "height": 200 },
  "assets": {
    "container": "dax",               // dax | daa | hlib | mixed
    "endian": "little",
    "root": "games/cok-dos/",
    "graphics": ["BIGPIC1.DAX", "8X8D1.DAX", "SPRIT1.DAX"],
    "maps": ["GEO1.DAX"], "scripts": ["ECL1.DAX"],
    "monsters": ["MON1.DAX"], "items": ["ITEM1.DAX"]
  },
  "palette": "auto"                    // from-file (HLIB) or fixed (EGA/Amiga table)
}
```

Loading CoK-with-Amiga-art, or DQK, or a custom scenario becomes *just another manifest*.

## Where to start (concrete first milestones)

The right starting point is **not** the playable engine — it's the **format library +
in-browser asset viewer**, because (a) it is the shared foundation for BOTH goals, (b) it
forces us to reverse the unknown formats early (Amiga DAA), and (c) it gives an immediate,
verifiable artifact (you can *see* the decoded art) that de-risks everything downstream.

1. **Format library v0 (TS/JS), headless.** Decoders for **DAX** (known) and **HLIB
   `TILE`/`DATA`** (known from DQK's own files) → engine-neutral frames+palette+tilesets.
   Pure functions over `Uint8Array`; runs in Node and browser. *This is literally the same
   code the DOS-rebuild goal needs for reading/converting assets.*
2. **Browser asset viewer.** Drag-in a container (or load via manifest) → render every
   frame/tile/sprite to a canvas with its palette. This becomes the day-to-day verification
   tool for the whole project (including checking converted Amiga→VGA art).
3. **Reverse Amiga `.DAA`** using the viewer as the oracle (decode → does it look right?).
   Add the `daa` loader. Add `iff/ilbm` for the `.LBM` title screens (well-documented).
4. **Manifest loader + game model.** Formalize the JSON manifest; load a whole game's asset
   set; render maps/GEO and sprite sets statically (no gameplay yet).
5. **ECL bytecode VM.** Port an existing interpreter (Simeon Pilgrim's COAB reimplementation
   is the reference) to JS; start with non-combat scripted events.
6. **Exploration + combat + party** → first fully playable game in-browser.
7. **Authoring (the "broader than UA" part)** — editors for maps/encounters/scripts and a
   "new custom scenario" manifest. This is the FRUA-superset endgame.

Milestones 1–3 are the high-leverage start and directly serve the main DOS goal too.

## Tech notes
- **Rendering:** indexed-color frame → palette lookup → `ImageData` on `<canvas>`; WebGL/
  shader for integer scaling + optional CRT. Keep the native 320×200 model.
- **Audio:** XMI (XMIDI) → MIDI → WebAudio/soundfont; `DIG4`/VOC digitized SFX → PCM.
- **Stack suggestion:** TypeScript + Vite; the format library as a standalone package with
  golden-file tests (decode → PNG, compare to Gold Box Explorer output) so format changes
  can't silently regress.
- **Reuse, don't reinvent:** Gold Box Explorer (format reference / golden images), COAB
  reimplementation (ECL VM reference), `hackdocs_extracted/` (on-disk structure docs).

## Relationship to the main goal
- **Shared foundation:** the format library (read DAX/DAA/HLIB, write HLIB) is needed by
  both; build it once.
- **Divergence:** the main goal then writes **HLIB** + patches the real **DQK.EXE** content
  to run under DOS; the web goal instead feeds the neutral model into a **JS engine**. Same
  decoders, two backends.
- **Sequencing:** the web engine is explicitly *after* "a good decompile exists." Do the
  format library + viewer now (serves both), defer the JS engine/VM until the DOS rebuild
  has validated the formats end-to-end.
