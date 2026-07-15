# ECS / ST palette-quantizer plan

*Status: planning. The display backends for ECS Amiga (native bitplanes) and
Atari ST/STE do not exist yet; this note scopes the palette reduction they
both need, informed by a host-side viability prototype
(`tools/palette_preview.py`).*

## The problem

The engine renders into one 256-colour chunky buffer. These targets can't show
256 colours at 320×200:

| Machine | On-screen | Palette depth |
|---|---|---|
| Atari ST | 16 | 512 (3 bits/gun) |
| Atari STE / Mega STE | 16 | 4096 (4 bits/gun) |
| ECS Amiga (lores) | 32, or **64 via EHB** | 4096 (4 bits/gun) |

So we need a **quantizer**: reduce the live 256-entry palette to the machine's
budget, load those into the hardware colour registers, and remap every pixel to
the nearest survivor. FRUA changes its palette per context (design, scene, big
picture), so this runs at **runtime**, whenever the engine calls
`set_palette` — not once at build time.

## Prototype findings (2026-07-15)

`tools/palette_preview.py` applied the reductions to real 256-colour frames
(the combat scene and the tavern-fireplace scene). What it showed:

1. **The cliff is the 3-bit ST palette, not the colour count.** Plain ST turns
   the mottled granite chrome into garish purple/blue speckle; STE — the *same
   16 colours*, 4-bit palette — reads as grey stone. **Target STE/Mega STE
   first; plain ST is possible but rough.**
2. **The chrome border is a palette hog.** Under one global palette the subtle
   granite gradients eat a big share of the budget and starve the fire and the
   coloured text. Strong argument for a **per-region palette** (or at least a
   fixed cheap sub-palette for the chrome, real budget for viewport+sprites).
3. **Colour-coded text is at risk at 16.** FRUA uses colour semantically (cyan
   active-character highlight, red AC/HP headers, green monster names); at 16
   those merge. **ECS-32 keeps them and is the visual sweet spot;** EHB-64 is
   near-original.

## Design forks (to settle before writing C)

1. **Global vs per-region palette.** Global = one palette for the whole screen
   (simple, and the prototype shows it's *playable*). Per-region = the 3D
   viewport / roster panel / message area each get their own palette via a
   mid-frame colour-register reload (much better, but fragile hardware timing).
   Leaning: ship global first (it works), leave room for a per-region pass.
2. **Algorithm.** Median cut (cheap, 68000-friendly, what the prototype used)
   vs k-means (better, slower). Median cut almost certainly wins for a runtime
   pass on a 68000.
3. **Dither?** Ordered/Bayer rescues gradients but muddies crisp pixel art and
   complicates the remap (spatial, not per-colour). Default: no dither for v1.
4. **Colour-cycling.** The fireplace rotates a palette *range*; naive reduction
   can collapse it. Cycle ranges (the `-3258`/`-3394` entries jt1067 drives)
   may need reserved slots so animation survives.
5. **EHB constraint (ECS).** Pick 32 base colours; entries 32..63 are their
   automatic half-bright twins — free shading/3D depth, but the quantizer must
   respect the ×0.5 relationship. Simpler v1: plain 32-colour, no EHB.

## Runtime integration (the clean part)

It drops into the existing pipeline with one added LUT:

```
engine set_palette(256 colours)
  -> quantizer: reduce to N colours -> load N hardware registers
                build a 256->N nearest-colour remap LUT (256 bytes)
present()  -> chunky->planar, indexing each pixel through the remap LUT
              before packing bitplanes
```

The chunky→planar transpose (`platform/include/c2p32.h`, already shared by the
Amiga and TT backends) takes the *remapped* indices unchanged — only a
256-entry LUT lookup is added ahead of it, and the hardware only ever sees N
registers. Rebuild cost is per-`set_palette`, not per-frame.

## The bigger co-blocker: memory

Colour is not the only wall. A stock 512K A500 or ST can't hold the working set
regardless of palette (the current build assumes a 4 MB floor —
[[port-memory-vs-mac-1mb]]). The quantizer serves **expanded** machines (1 MB+
STE/Mega STE, 1–2 MB ECS Amiga); a bone-stock 512K box needs a separate
memory-footprint investigation, not a better palette.

## Suggested order of work

1. ✅ Host viability prototype — `tools/palette_preview.py` (done).
2. Per-region prototype: split the frame chrome-vs-content, quantize each,
   confirm it beats global before committing to the hardware complexity.
3. Native ECS Amiga bitplane backend (32-colour, no EHB) with the runtime
   quantizer + remap LUT — testable in amiberry ECS with no graphics card.
4. STE backend (16-colour, 4-bit) — testable in Hatari `--machine ste`.
5. EHB, per-region palettes, plain-ST, dithering — polish passes, each optional.
