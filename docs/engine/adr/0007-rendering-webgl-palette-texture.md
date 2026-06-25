# ADR-0007 — Rendering: WebGL palette-texture indexed pipeline

**Status:** Accepted
**Date:** 2026-06-21

## Context

Every Gold Box platform is indexed-color at 320×200 with non-square pixels: CGA-4, EGA-16,
Amiga-32 (12-bit palette), VGA-256 (6-bit DAC), Mac-256. The engine-neutral model already stores
images as **8-bit indices + a Palette** (never pre-rendered RGBA) specifically so palette
inheritance, color-cycling, and live mode-switching happen at draw time (asset-model.md). We need
a render path that handles all color depths, supports instant palette swaps (live GraphicSet
switch, ADR-0004), and applies authentic 1.2× aspect correction (graphics-modes research §3.4,
§7).

## Options

1. **Pre-render to RGBA on canvas2d** — simplest; already have `indexedToRGBA`. But every palette
   change (cycling, set switch) re-converts all pixels on the CPU — wasteful and laggy for
   live-switch/cycling.
2. **WebGL palette-texture** — upload pixel **indices** as a single-channel texture and the
   **palette** as a 256×1 RGBA texture; a fragment shader does `palette[index]`. `NEAREST`
   filtering. Palette swap = re-upload 256×4 bytes (one tiny `texImage2D`); color-cycling = same,
   per frame. One pipeline serves all color depths (different palette range filled).
3. **WebGPU** — future-proof but unnecessary now and narrows browser support.

## Decision

**WebGL palette-texture as the primary path; canvas2d `ImageData` (the shipped `indexedToRGBA`)
as the fallback** for environments without WebGL and for the static asset viewer. Aspect
correction (1.2× vertical) and integer scaling happen in the final blit; an optional CRT/scanline
post-pass is a later add. The engine returns indices + palette; the **host** owns the GL surface
(keeps `packages/engine/render` free of a hard canvas/DOM dependency — it produces the data and a
small shader-ready descriptor).

## Consequences

- Live GraphicSet switch and color-cycling are O(palette re-upload), not O(repaint-all-pixels) —
  matches the Tomb Raider-class feel ADR-0004 targets.
- One shader handles EGA-16 / Amiga-32 / VGA-256 / Mac-256 with only the palette texture content
  differing; no per-depth code.
- The canvas2d fallback keeps the existing viewer and headless golden-file tests working
  (golden parity is computed on RGBA output regardless of GL).
- VGA 6-bit→8-bit (×4) and Amiga 12-bit→8-bit scaling are done at load into the Palette (already
  the model's contract), so the shader always sees 8-bit RGB.
- WebGPU remains a future option; the index/palette-texture concept ports directly if we migrate.
