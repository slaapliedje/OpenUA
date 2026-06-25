# Web Gold Box Engine — Technical Spec

> **Status: design / implementation-ready spec for the SECONDARY goal.** This folder expands
> the high-level [`../web-engine-plan.md`](../web-engine-plan.md) into an implementation-ready
> document set. The plan stays the source of *intent and sequencing*; these files are the
> *contracts and algorithms*. Where a file supersedes a plan point, it says so inline.

The deliverable is a **DQK-class browser engine in TypeScript**: it loads the original game
assets of any Gold Box / Krynn-era title via a small per-game **manifest**, decodes every
container/graphics format the series used, renders with the original graphics at native
320×200, and (later) plays them through an ECL bytecode VM plus a FRUA-style authoring layer.

All format claims here are grounded in the repo's **verified** docs:
[`../findings.md`](../findings.md), [`../dax-format.md`](../dax-format.md),
[`../daa-format.md`](../daa-format.md), [`../hlib-format.md`](../hlib-format.md), and the
working Python decoders in [`../../tools/`](../../tools/). The TypeScript loaders are direct
ports of `dax_decode.py`, `daa_decode.py`, `hlib_decode.py`. Anything not proven in those
docs is labelled **UNSOLVED** / **ASSUMED** here too.

---

## Layered architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│ 5. UI / render layer  (browser)                                        │
│    <canvas> ImageData + optional WebGL integer scaling, input, audio   │
├──────────────────────────────────────────────────────────────────────┤
│ 4. Engine core + ECL VM                                                │
│    exploration, combat, party, encounters; the ECL bytecode interp     │
├──────────────────────────────────────────────────────────────────────┤
│ 3. Engine-neutral GAME MODEL  (the contract)                           │
│    Palette, IndexedImage, TileSet, SpriteSet, Map, Monster/Item        │
│    tables, StringTable, Script, Sound  →  see asset-model.md           │
├──────────────────────────────────────────────────────────────────────┤
│ 2. FORMAT LIBRARY / loaders   (shared with the PRIMARY DOS goal)       │
│    DAX · DAA(Amiga) · HLIB(TLB/GLB) · IFF/ILBM  →  the neutral model   │
│    pure functions over Uint8Array/DataView  →  see loaders.md          │
├──────────────────────────────────────────────────────────────────────┤
│ 1. Per-game MANIFEST  (JSON)                                           │
│    which files, which loader+variant, palette/display profile,         │
│    ruleset, entrypoints  →  see manifest.md                            │
└──────────────────────────────────────────────────────────────────────┘
```

The decoupling rule: **loaders never know about the engine; the engine never knows about
file formats.** Layer 3 (the neutral model) is the only shared vocabulary. A loader's whole
job is `bytes → neutral model`; the engine consumes only the neutral model. This is what lets
one engine play CoK-from-Amiga-art, DoK-from-Amiga-art, DQK-from-DOS-art, and custom
scenarios with *no engine changes* — only a different manifest and the matching loaders.

---

## Document set

| File | Layer | What it nails down |
| ---- | ----- | ------------------ |
| [`asset-model.md`](./asset-model.md) | 3 | The engine-neutral in-memory data model as TypeScript interfaces — the contract every loader decodes *into*. |
| [`loaders.md`](./loaders.md) | 2 | How to port each Python decoder to TS over `Uint8Array`/`DataView`: DAX, DAA, HLIB, IFF/ILBM — exact read sequences, RLE, planar/Chain-4, drawing-method dispatch, graceful degradation of unsolved variants. |
| [`palettes-and-rendering.md`](./palettes-and-rendering.md) | 2→5 | The cross-platform color model (EGA-16 / Amiga 12-bit×17 / VGA per-leaf UAPALETT ranges + inheritance + cycling) and the indexed→RGBA→canvas/WebGL render pipeline plus audio (XMI→MIDI, VOC/DIG4 PCM). |
| [`game-data-and-ecl.md`](./game-data-and-ecl.md) | 4 | The non-graphics data a playable engine needs: the ECL bytecode VM, GEO maps, MON/ITEM tables, 6-bit packed strings, save format — known vs to-decode, and a recommended port path (COAB reimplementation as the ECL reference). Future work; documents the *shape*. |
| [`manifest.md`](./manifest.md) | 1 | The per-game manifest JSON schema + concrete examples for CoK (Amiga DAA), DoK (Amiga), DQK (DOS HLIB), and a custom-scenario stub. |
| [`roadmap.md`](./roadmap.md) | all | Phased build plan, milestone acceptance criteria, recommended stack (TypeScript+Vite, golden-file tests vs the Python renders), prior-art/reference list. |

---

## Relationship to the PRIMARY (DOS) goal

The **primary** goal — two DOS executables on the DQK engine that act as CoK/DoK with Amiga
art (see [`../../CLAUDE.md`](../../CLAUDE.md)) — and this **secondary** browser goal share
**layer 2, the format library**, byte-for-byte:

- Primary needs: *read* Amiga DAA + DOS DAX, *write* DQK HLIB.
- Web needs: *read everything and render it* (DAX, DAA, HLIB, IFF/ILBM).

Both read the same formats with the same algorithms; they only diverge at the backend
(primary writes HLIB + patches `DQK.EXE`; web feeds the neutral model into a JS engine). The
format library + asset viewer is therefore the **first** thing to build for either goal — it
de-risks both and is independently verifiable (you can *see* the decoded art). See
[`roadmap.md`](./roadmap.md) for the phasing.

The neutral model (layer 3) is deliberately DQK/HLIB-shaped (256-color, per-image palette,
per-frame placement offsets) because the older EGA/Amiga formats convert *up* into it
losslessly — the same "newest engine is the richest target" rationale as the DOS rebuild.
