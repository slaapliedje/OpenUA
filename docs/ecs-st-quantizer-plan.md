# ECS / ST palette-quantizer plan

*Status: the runtime reducer is BUILT (`platform/include/quantize.h`,
host-tested); the display backends for ECS Amiga (native bitplanes) and Atari
ST/STE that consume it do not exist yet. This note scopes the palette reduction
they both need, informed by a host-side viability prototype
(`tools/palette_preview.py`) and now realised as portable C.*

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
machine** (1 MB ECS Amiga / 1 MB STE — not a bone-stock 512K box).

Measured (2026-07-15, Atari `.prg` GEMDOS header, `-O2`): the **static image is
1.39 MB** = **968 K code + 454 K bss** (+ ~2 K data), on top of which the heap
working set (FAR pool ~450 K, play/load buffers) sits. Two structural facts:

- **BSS is very attackable** — it was almost entirely whole-file resident load
  buffers. Converting the two biggest (TITLE.CTL 195 K, BACK.CTL 160 K) to
  lazily-allocated transients cut BSS 800 K → 454 K (resident 1.75 MB → 1.39 MB).
  Remaining BSS candidates: `g_frame_file` 40 K / `g_gen_file` 28 K /
  `g_menu_file` 16 K (the "kept resident" GLIB loaders — need a shared-scratch
  or lazy pass), `g_glib_dec` 64 K, `g_chunky`/page buffers (backend-owned).
- **The 968 K of code is the hard wall.** Even with zero BSS and zero heap, code
  alone nearly fills a 1 MB machine. Genuine 1 MB needs code **overlays**
  (mirroring the Mac's 23 on-demand CODE segments) or a stripped play-only build
  (drop the editors/GDOS printing) — a large structural effort. **A realistic
  near-term target is 2 MB** (BSS shrink + heap fits comfortably); 1 MB is a
  later push. The 68000-clean build (`CPU68K=68000`) already links.

## Suggested order of work

1. ✅ Host viability prototype — `tools/palette_preview.py` (done).
2. ✅ Per-region (banded) prototype — confirmed banding is a large win,
   transformative at ST-16; per-region model settled as horizontal bands.
3. ✅ Runtime reducer in C — `platform/include/quantize.h` (median cut over
   the CLUT, counting-partition, 256->N remap LUT), host-tested
   (`tests/test_quantize.py`), compiles clean under both cross toolchains.
   Not yet wired into a backend.
4. **Memory footprint to 1 MB** — the real gate for ECS/ST; shrink the FAR
   pool + play/load working set (see the memory co-blocker above). Do this in
   parallel with, or before, the native backends.
5. ✅ Native ECS/OCS bitplane backend (`platform/amiga/display_ecs.c`,
   32-colour) + **per-band copper palette** (`quant_banded`, 25 bands, WAIT +
   32 COLOR per band — free on the copper). **VERIFIED in amiberry on a bare
   OCS A2000**: the granite chrome that was purple/green speckle under a global
   palette now renders as clean grey stone, near the AGA look. Remaining:
   runtime detection split (`-DFRUA_FORCE_ECS` forces it today; the bare-ECS-vs-
   graphics-card probe is still to wire); cycling only re-quantises on loads.
6. ✅ Native Atari ST/STE backend (`platform/display_ste.c`, 16-colour ST-low)
   + **per-band palette via an MFP Timer-B raster split** (event-count on
   display-enable, self-phase-locking, VBL resets band 0). **VERIFIED under
   Hatari --machine ste + EmuTOS on a 68000 STE**: the granite renders as grey
   stone (was heavy speckle). Faint band-boundary seams remain at 16 colours.
   Built via `CPU68K=68000` (runs on every Atari).
7. Polish, each a later push: reduce ST band seams (**layout-aligned bands** so
   boundaries fall at natural edges, and/or dithering), EHB on ECS, plain-ST
   3-bit tuning, the ECS runtime detection split.

## Banding — DONE (2026-07-15)

`quant_banded` (quantize.h): one full-frame presence histogram, then a per-band
median-cut over just the colours each strip uses (via `quant_reduce_n`); colours
absent from a band at build time fall back to the nearest-LUMINANCE reduced
entry (the composited cursor / post-reband content must not go black). Both
backends band; the difference is only how the hardware reloads the palette —
**copper** (ECS, free) vs **Timer-B raster interrupt** (ST).

Live testing on the ST (2026-07-15) then drove four more fixes, all landed:
- **Timer-B phase**: armed once, the counter free-ran and (200 % 8 == 0) kept a
  random line offset forever. The VBL re-phases it every frame.
- **Border-exact switch**: the timer fires one line EARLY, the handler pre-loads
  d0-d7 during that line, spins on TBDR (the MFP decrements it at each display
  line's end), and movem-stores all 16 registers inside the border — no
  mid-line palette switch, no boundary dashes.
- **Re-band policy**: only substantial palette loads (count >= 32) mark dirty,
  and re-banding runs ONLY at a full present (a complete frame). Palette-cycle
  steps had been forcing full re-band+re-blits against half-drawn frames —
  the intro corruption and apparent freeze at 8MHz. Cycling therefore doesn't
  animate on the quantized targets (reserved cycle slots = future work).
- **Row-diff presents**: modal loops full-present every pass; ~1s of remap+c2p
  per pass at 8MHz read as "frozen input". st_present diffs rows against a
  shadow and converts only changes — idle passes collapse to a memcmp scan.
  (The ECS backend still converts fully per present — 020-class machines carry
  it; port the row diff there if an A500 test says otherwise.)

## Future: ST-High monochrome backend (the Mac B&W mode, natively)

The Mac version ships a full 1-bit mode with its own B&W art (both art sets are
real — see the resource inventory). ST High is 640x400 monochrome — the Mac
window's EXACT size — and 1-bit is where the planar tax vanishes: 1-bit chunky
IS planar, so present() is a straight copy (32KB screen, no quantizer, no
bands, no c2p). A mono backend would likely be the FASTEST Atari presentation
of the game and pixel-faithful to the Mac's mono mode at native resolution,
for SM124-monitor owners. The work is ENGINE-side: lift how the Mac selects
B&W vs colour resources (the Toolbox depth checks feeding the art loads), then
the display backend is trivial. (Prompted by the user asking whether mono
cursors would speed the ST up — cursors don't, the mode would.)

### ST-High status (2026-07-15)

**Phase 1 SHIPPED** (`platform/display_sthigh.c`): 640x400 1-bit, the engine's
320x200 frame as 2x2 cells with a 5-level Bayer dither from two luma LUTs.
Auto-detected (ST/STE + Getrez()==2 = mono monitor), same binary. Verified
under Hatari --monitor mono + TOS 2.06 to the in-dungeon play screen. The
cheapest present of any backend (no quantizer/bands/raster IRQs; ~4x less
work per pixel than banded ST-low) — and palette changes cost nothing, so the
scene-change hitches of the colour path don't exist here.

**Phase 2 — the Mac's real B&W mode (the "native to native" goal):**
- The 1-bit art is REAL and at 640x400 SCALE: PICA portraits are 176x176 =
  exactly double the colour art's 88x88 (see the art-formats memory/docs).
  DUNGCOM/WILDCOM/TOPVIEW/SPRIT/BACK/TITLE/MENU/FRAME all have 1-bit sets.
- The Mac gates the mode on display depth: JT[1200]() == 3 -> colour
  (g_a5_2347 == 0); B&W Macs take the other arm and load the .TLB art.
- The BIG lift: the port's engine/shim world is 320x200 — a native-res mono
  mode needs the drawing surface at 640x400 (fonts, rects, the 3D compose)
  with art blitted 1:1 instead of the colour path's logical pixels. That is
  a coordinate-model project, not a backend: scope it as its own campaign.
- Cheaper intermediate worth considering first: keep the 320x200 world but
  load the 1-bit art DOWNSCALED 2:1 (or draw the 1-bit art into the 2x2
  cells at present time for the picture regions) — most of the crispness,
  none of the coordinate surgery.
