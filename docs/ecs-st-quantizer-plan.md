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

## Per-region (banded) findings (2026-07-15)

`tools/palette_preview.py --banded` (the `banded()` model) splits the frame
into horizontal strips, each with its own palette — modelling the achievable
hardware: a colour-register reload at **horizontal blank**, so each *scanline*
can carry its own 16/32 colours (free on the Amiga copper, an HBL handler on
the ST). On the fireplace frame:

- **Banding is transformative, especially at ST-16.** Global ST-16 muddies the
  fire and merges the text colours; **per-line ST-16 restores the orange fire,
  brown wood, grey granite, and the cyan/red text** — it looks nearly as good
  as *global ECS-32*. Per-line ECS-32 ≈ the 256-colour original.
- **It works because FRUA's colour-hungry regions are vertically stacked**
  (viewport / panels / message / command bar), so a per-band palette gives each
  what it needs. The viewport and roster *share* scanlines, but the roster is
  grey + a few text colours that fit alongside the viewport palette, so the
  shared-scanline limit doesn't bite in practice.
- **Cost scales with band count.** 4–12 bands already captures most of the win
  (cheap enough for an ST HBL handler); per-line is the ceiling and **free on
  the Amiga** — the copper does per-scanline colour writes for nothing, exactly
  what the AGA backend's copper list already does.

**Decision:** per-region = **horizontal bands**. Amiga backends use per-line
(copper, free); ST/STE uses layout-aligned bands (viewport / hud / message /
command-bar boundaries) via an HBL palette reload, with per-line as a later
push if the HBL budget allows.

## Design forks (remaining)

1. ~~Global vs per-region~~ — **settled: banded (horizontal bands).**
2. **Algorithm.** Median cut (cheap, 68000-friendly, what the prototype used)
   vs k-means (better, slower). Median cut almost certainly wins for a runtime
   pass on a 68000.
3. **Dither?** Ordered/Bayer rescues gradients but muddies crisp pixel art and
   complicates the remap (spatial, not per-colour). Default: no dither for v1.
4. **Colour-cycling.** The fireplace rotates a palette *range*; naive reduction
   can collapse it. Cycle ranges (the `-3258`/`-3394` entries jt1067 drives)
   may need reserved slots so animation survives. Note banding helps here too:
   the cycle range only needs to survive in the *band(s)* that show it.
5. **EHB constraint (ECS).** Pick 32 base colours; entries 32..63 are their
   automatic half-bright twins — free shading/3D depth, but the quantizer must
   respect the ×0.5 relationship. Simpler v1: plain 32-colour, no EHB. (With
   per-line banding, plain 32 already looks near-original, so EHB is optional.)

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

## The bigger co-blocker: memory (target = 1 MB)

Colour is not the only wall, and it's not the hard one. **The target is a 1 MB
machine** (1 MB ECS Amiga / 1 MB STE — not a bone-stock 512K box). The current
build assumes a ~4 MB floor ([[port-memory-vs-mac-1mb]]), so hitting 1 MB is a
real footprint investigation — shrinking the FAR pool (~450K today), the play
buffers, and the load-time working set — and it is arguably **more** gating for
ECS/ST than the quantizer. The 68000-clean build (`CPU68K=68000`) already
links; memory is the remaining structural piece.

## Suggested order of work

1. ✅ Host viability prototype — `tools/palette_preview.py` (done).
2. ✅ Per-region (banded) prototype — confirmed banding is a large win,
   transformative at ST-16; per-region model settled as horizontal bands.
3. **Memory footprint to 1 MB** — the real gate for ECS/ST; shrink the FAR
   pool + play/load working set (see the memory co-blocker above). Do this in
   parallel with, or before, the native backends.
4. Native ECS Amiga bitplane backend (32-colour) with the runtime quantizer +
   **per-line copper palette** + remap LUT — testable in amiberry ECS, no
   graphics card. (The copper per-line palette is the same mechanism the AGA
   backend already uses, so this is largely a depth/colour-count change.)
5. STE backend (16-colour, 4-bit) with **layout-aligned HBL bands** — testable
   in Hatari `--machine ste`.
6. EHB, per-line-on-ST, plain-ST, dithering — optional polish, each a later push.
