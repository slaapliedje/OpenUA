# Roadmap — Phased Build Plan

Phasing for the browser engine. The ordering principle (from
[`../web-engine-plan.md`](../web-engine-plan.md)) holds: **the format library + asset viewer
come first**, because they are the shared foundation for both project goals, force the unknown
formats early, and give an immediately verifiable artifact (you can *see* the art). This file
makes the phases concrete with acceptance criteria.

Sequencing constraint from `../../CLAUDE.md`: the web engine starts *after* a good DOS
decompile validates the formats end-to-end. Phases 0–1 below, however, ARE that shared format
work and can proceed in lockstep with the primary goal.

---

## Recommended stack
- **TypeScript + Vite** app; the **format library as a standalone package** (`@goldbox/formats`)
  with zero DOM deps — pure functions over `Uint8Array`/`DataView`, so it runs in Node (tests,
  the DOS-goal tooling) and the browser unchanged.
- **Rendering:** `<canvas>` `ImageData` for the simple path; optional WebGL for integer
  scaling + CRT shader (see [`palettes-and-rendering.md`](./palettes-and-rendering.md)).
- **Audio:** WebAudio; a JS SF2 soundfont synth for XMI→MIDI music; `AudioBuffer` for PCM SFX.
- **Tests:** **golden-file** — decode → render → compare to the Python decoders' PNGs in
  `renders/{dax,daa,hlib}/`. Any format regression fails CI. The Python tools
  (`tools/*_decode.py`) are the oracle; the TS port must match byte-for-byte on the solved
  subset.

---

## Phase 0 — Format library v0 (headless) + golden tests
Port `dax_decode.py` and `hlib_decode.py` (both fully solved) to TS over `Uint8Array`.
Produce the [asset-model.md](./asset-model.md) `IndexedImage`/`FrameSet`/`TileSet`/`Palette`.
Shared `rleDecode`, EGA-16 table, HLIB Chain-4 + methods 16/17/18/21/23.

**Acceptance:** for every solved (file, block) in CoK/DoK DAX and DQK HLIB, the TS render is
**byte-identical** to the Python PNG. Unsolved variants (DAX complex frames, HLIB method 25)
are listed/skipped exactly as Python does. Runs in Node with no browser.

## Phase 1 — Browser asset viewer
Drag-in a container (or load via a manifest); render every frame/tile/sprite to canvas with
its palette; toggle scale and "merge with context palette X" for HLIB inheritance preview.

**Acceptance:** a non-developer can open `BIGPIC.TLB`, `TITLE.DAX`, etc. and see correct art;
unsolved blocks show a clear "known-unsolved" placeholder; the viewer is the day-to-day
verification tool for both project goals (incl. checking converted Amiga→VGA art).

## Phase 2 — DAA (Amiga) loader + IFF/ILBM
Port `daa_decode.py`: big-endian container with the 9-byte-TOC-vs-6-byte-index disambiguation
heuristic, 5-plane planar combine, ×17 palette, IFF/ILBM reader for `.LBM` titles. Use the
Phase-1 viewer as the oracle (decode → does it look right?).

**Acceptance:** CoK `BIGPIC1` b114 (Amiga) matches the DOS DAX render of the same block
(known cross-validation); `scrn1b.lbm` / `Title1.LBM` render pixel-perfect. 6-byte sub-frame
files and `8X8D*` tiles are listed with their known-unsolved caveat (sub-frame inner encoding;
tile plane-order/palette) — not faked.

## Phase 3 — Manifest loader + game model
Formalize the [manifest.md](./manifest.md) schema; load a whole game's asset set into
`LoadedGame`; register context palettes; statically render maps/GEO and sprite sets (no
gameplay). Add the non-graphics loaders that are cheap and documented: 6-bit string unpack,
ITEM+VOCAB tables (see [`game-data-and-ecl.md`](./game-data-and-ecl.md)).

**Acceptance:** the CoK-Amiga, DoK-Amiga, and DQK-DOS manifests each load end-to-end into a
`LoadedGame`; a "gallery" view shows all art per game with correct palettes; item names
reconstruct via vocab.

## Phase 4 — ECL VM (disassembler → interpreter)
Decode GEO maps (DQK documented, then CoK/DoK reversing pass vs COAB). Build the ECL
**disassembler** validated against the **Simeon Pilgrim COAB reimplementation**, then the
**interpreter** (non-combat events first), ruleset-parameterized for opcode drift.

**Acceptance:** a disassembled `ECL1#0` matches COAB's listing; scripted non-combat events
(text, questions, transfers, flags) run for at least one map.

## Phase 5 — Exploration + combat + party
Wire the engine core: dungeon/overland movement on GEO maps, the combat engine consuming
`MonsterTable`/cbody-cpic art, party state, encounter triggers from ECL.

**Acceptance:** one game (DQK recommended — its formats are fully solved) is **playable
start-to-an-encounter** in-browser with original graphics and music.

## Phase 6 — Authoring (the "broader than UA" endgame)
Editors for maps/encounters/scripts; a "new custom scenario" manifest + writers that emit the
engine's native (HLIB-shaped) format. This is the FRUA-superset goal.

**Acceptance:** a custom scenario authored in-tool loads and plays via its own manifest with
no original game files.

---

## Prior art / reference list
- **Gold Box Explorer** (bsimser) — batch DAX/TLB → PNG; **golden-image source** and format
  cross-reference for the loaders.
- **Simeon Pilgrim's COAB reimplementation / fork** — the **de-facto DAX + ECL spec**; the ECL
  VM and CoK/DoK GEO reference to port (see game-data-and-ecl.md). Do NOT use hackdocs
  OPCODES.TXT as the ECL spec (it's x86 for the editor exe).
- **Gold Box Companion** — the only tool that *writes* DAX (EGA icons/fonts/items) — relevant
  to the authoring/repacker side.
- **forums.goldbox.games** (topics 1073 / 3148 / 1241) — the real community format docs.
- **`hackdocs_extracted/`** (local) — on-disk structures: `TLBFORM/DRAW18/DRAW23/UAPALETT/
  FRM_DESC` (graphics), `GEODATA/SCRIPT` (maps/events), `ITEM/MONSTDAT/VOCAB/STRGFORM`
  (tables/strings), `SAVGAM` (saves), `TUNES/UASOUND` (audio).
- **Repo specs (source of truth):** [`../findings.md`](../findings.md),
  [`../dax-format.md`](../dax-format.md), [`../daa-format.md`](../daa-format.md),
  [`../hlib-format.md`](../hlib-format.md); decoders [`../../tools/`](../../tools/).

## Cross-cutting: what must be decoded next (engine blockers)
Ranked by how much they block a *playable* engine, the still-unsolved/undocoded items:
1. **ECL bytecode VM** — nothing scripted runs without it (port from COAB).
2. **CoK/DoK GEO cell encoding** — needed for exploration on the older games.
3. **MON field layout per game** — needed for combat.
4. **DAA 6-byte sub-frame inner encoding** — blocks Amiga `SPRIT*`/`PIC*`/`HEAD*` (most
   sprites/scenes) for CoK/DoK-with-Amiga-art.
5. **8X8D tile plane-order + shared palette** (DAX & DAA) — dungeon walls render but with
   possibly-wrong colors.
6. **HLIB DIG4 sound codec** and **method 25** — audio SFX + a rare draw mode.
