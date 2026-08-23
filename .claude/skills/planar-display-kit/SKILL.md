---
name: planar-display-kit
description: The 16/32-colour bitplane display stack — banded quantizer, palette groups, palette caches, raster splits, draw-time planar writers, c2p. Use when working on ST/STE/ECS/AGA rendering, palette/quantizer performance, band seams, or bringing 256-colour art to a bitplane machine.
---

How OpenUA puts 256-colour chunky art on machines with 16 (ST) or 32 (ECS)
hardware colours, and every trap that path holds. The code is shared:
`platform/include/quantize.h` (one banded median-cut for all backends),
`platform/display_ste.c` (ST), `platform/amiga/display_ecs.c` (ECS),
`third_party/c2p-68k` (chunky→planar, a shared subtree — edit upstream).

## The pipeline (per backend)

1. Engine draws 8-bit chunky into the QuickDraw surface.
2. **Re-band** (on genuine palette/content change only): `quant_banded` cuts
   the frame into N raster bands (ST 25×16-colour via Timer B splits, ECS
   25×32-colour via copper), producing per-band palettes + 256→slot remaps.
3. Rows convert chunky→remapped→planar; ADR-0016 draw-time writers stamp
   planes directly and the present skips conversion for rows they own.
4. Present with row diffing + announced-row scan narrowing (#63).

## The cost model — measure before optimizing

`QUANT_PROF` (in quantize.h, wired under `FRUA_AMIGAPROF`/`FRUA_STPROF`)
attributes phases: hist / keep / **cut** / remap / **buckets**. History says
the bottleneck is never where reasoned: three successive theories were wrong
before profiling showed multiplies at 44% (fixed: 512-byte squares table,
bit-identical), and after that cut+buckets at 86% — fixed not by asm but by
**cutting fewer times**:

- **Palette groups (#139, both backends now)**: split only where content
  really changes — the 3D viewport's top/bottom edges — and run ONE cut per
  group (chrome / viewport / chrome). ST: 25 bands → 1–3 groups. ECS: walk
  re-band 9.5 s → ~1.1 s on the 7 MHz 68000 (fad56696). Screens without a
  viewport keep per-band cuts (pictures deserve the fidelity; they're cached).
- **Never split on arbitrary boundaries** — that is the #40 seam (a flat
  panel striping at a band edge). Boundaries must sit on content edges.
- **ECS learns the viewport via `dsp_viewport_note`** (planar.c) — the commit
  hook is GATED on a scratch existing and never fires on scratchless backends.
  Staleness is decided by CONTENT (16-pixel fingerprint of the rendered
  viewport, re-checked at re-band), not by present ordering — order between
  render, CLUT install, and presents is not stable; content is.

## Caches (both are load-bearing)

- **CLUT guards** (ST + ECS): a re-install of an identical CLUT skips the
  quant; changed-RGB/same-content is a repalette (rewrite colour words, keep
  remaps); a slot SPLIT (two indices sharing a slot moving apart) forces the
  full quant — a repalette can never un-merge a slot (invisible-HUD-text bug).
  On the ST, 74 of 85 walk re-bands were cache-served CLUT installs.
- **Disk palette cache** (ECS, `PROGDIR:PALCACHE.ECS`): content-keyed
  (hash of CLUT + frame), keys in RAM, 8.8 KB blobs on disk, append-only.
  Each scene quantizes ONCE EVER. Traps already paid for: a bare relative
  path wrote it to the CWD (`CD SYS:` boots → never found, every boot cold);
  a 16-entry RAM cap silently STOPPED caching when full (the "converts every
  screen" misery); in-place `r+b` header rewrites don't stick under Amiga
  ncrt0 stdio — derive the count from file SIZE and append with plain `ab`.

## Discipline

- **Runtime switches, one binary**: every lever here has a video.cfg token
  (`vpbands`, `vpgroups`, `palcache`, `inkhold`...). A compile-time A/B is two
  builds and has misled this project repeatedly (#91: four probe configs
  masked the bug entirely).
- **A change priced on one backend is not priced** — quantize.h is shared;
  the squares-table change cost the ECS 25× what it cost the ST.
- **The re-band trigger is dominance, not presence** (56dd490b); slot 0 is
  reserved for the border; count the BORDER when hunting stray bands.
- Verification tools: `tools/quantsim.py` (host-side quant), edge-map+Jaccard
  screenshot comparison (`st-viewport-composite-trap` memory), amiberry
  captures BLEND colours (filter to multiples of 17 + population floor).
