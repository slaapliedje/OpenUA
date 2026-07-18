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

### Phase 2 findings (2026-07-15) — the B&W mode DECODED

The architecture is now fully understood, and the old model was BACKWARDS:

- **jt1200()==3 IS the B&W mode** (not "deep/colour" as an earlier note said).
  color_mode_init (boot.c:669, lifted from CODE 4 @0x44b8/@0x47b4) derives it:
  `g_a5_1315 = (screen depth == 1)`, `g_a5_2347 = !g_a5_1315`, and jt1200
  returns 3 when g_a5_2347==0. In colour it returns 0 (8-bit deep — the
  port's mode) or 1 (4-bit shallow). The value doubles as the ART-DEPTH
  SHIFT: log2(pixels per art byte) — 0 for 8bpp art, 3 for 1bpp (l2d4e clips
  with `bpp_w << jt1200()`).
- **Art selection is per-mode by extension** (the PIC arm, boot.c ~78893):
  mode 3 -> "PIC%c1%03d.tlb" = 176x176 1bpp (2x logical scale, drawn 1:1 into
  the Mac's 640x400 window); colour -> ".ctl" = 88x88 8bpp with a 224-colour
  palette (PICA.CTL sub0 = the palette — verified offline). Flags bit 0x40:
  set = colour family, clear = 1bpp/compact.
- **The B&W arms are ALREADY LIFTED** throughout the engine (whole-function
  lifts brought both branches): e.g. l534a's jt1200()==3 arm draws the pic
  slot via l3804 instead of l3880. Flipping `g_a5_1315 = 1` when the display
  is mono turns on the Mac's own B&W behaviour at all ~187 jt1200 sites.
- **Offline decode PROVEN**: PICA.TLB = 28-item GLIB, each pic a sub-GLIB
  {meta, mode-2 per-row-PackBits 1bpp 176x176}; decoded a crisp hand-dithered
  portrait. TITLE.TLB sets are 480x300 (compact-Mac-sized splash screens);
  its plain 0x92 pieces decode clean; the 0x93 pieces are MODE 3 (masked,
  indirect: header via L289a + per-row offsets via JT[1170]) — the port's
  l2d4e mode-3 arm is deferred and needs that lift.

**Remaining for the runtime flip (the campaign):**
1. Lift the 1bpp decode leaves l2d4e needs in mode 3 (flags bit 0x40 clear):
   raw (mode 0), row-PackBits (mode 2), and the masked mode-3 format
   (L289a + JT[1170] + JT[1185] row blitter).
2. Decide the mono surface: the B&W art is 2x logical scale, so either a
   640x400 byte-per-pixel surface with jt1198-style x2 coordinate scaling in
   the shim draw leaves (the Mac's own model), or a native-blit side channel
   in the sthigh backend for art while text/chrome stay 2x2-dithered.
3. Audit the jt1200()==3 arms actually reachable in play for PROBE stubs.
4. Wire g_a5_1315 to dsp_is_mono() and validate screen by screen.

### Phase 2 runtime — FIRST LIGHT (2026-07-15, FRUA_BWMODE)

The gate flip WORKS: `make CPU68K=68000 EXTRA_CFLAGS=-DFRUA_BWMODE` boots the
engine's own B&W mode on ST High (Hatari --monitor mono + TOS 2.06) — crisp
black-on-white button text, plate outlines, and QD stipple patterns from the
Mac's own lifted arms, on the 480x300 B&W window centred in 640x400.

Landed: color_mode_init derives depth from the backend (g_dsp_mono_active);
sthigh's engine-B&W surface (480x300 byte-per-pixel, 1:1 bit-pack present);
l2d4e's 1bpp PackBits leaf (mode 2 + flags bit 0x40 clear).

The iterative remainder (screen-by-screen, on screenshots):
1. The half-drawn menu: misplaced plates/labels — colour-oriented leaves
   (the port's stand-in menu painter draws at 320x200 constants; the B&W
   arms want the Mac's own menu draw) + B&W clip bounds (-3050..-3056).
2. l2d4e mode-1 leaf for B&W (2bpp compact pieces: FRAME/ALWAYS/TOPVIEW).
3. The mode-3 masked format lift (L289a + JT[1170] + JT[1185]): TITLE
   screens, SPRIT sprites — the intro and cursor art.
4. Then play-mode screens: dungeon (8X8DB.TLB 2bpp walls), events (PIC%c.TLB
   176x176 pictures — the flagship), combat.
5. Input mapping in the 480x300 window (mouse clamps read the surface size
   already; verify click coords land).

### Phase 2 runtime — THE MENU RENDERS (2026-07-15, second leg)

The B&W menu is fully drawn and interactive-geometry-correct. What it took:
the seven port-side g_a5_2347 forcings now seed from the display mode
(g_port_2347); the planar codec's odd-address .TLB reads are byte-safe on the
68000; l2d4e gained the 2bpp-compact leaf; and the ink model settled: the
engine-B&W surface stays COLOUR-INDEXED with the present thresholding by
palette luminance — the engine's inverse-video menu bars (mode-3 colour 503)
then render as the Mac's white-on-black enabled items for free.

### Phase 2 runtime — THE PLANAR CODEC IS LIVE (2026-07-16, third leg)

The jt995/l05dc planar-codec port landed: the menu's command bars now render
from the REAL FRAME.TLB pieces (16x17 two-plane masked glyphs — white face,
black frame lines, rounded caps) composed by the faithful jt1189/jt1191
writers, bit-for-bit.

The design — the MONO PLANAR PAGE shim (boot.c, FRUA_BWMODE): the codec gets
a real 1-bit page (s_mono_page, 60x300 bytes); each jt995 dispatch is
bracketed — SYNC the word-aligned window chunky->bits through the backend's
exported luminance ink LUT (g_dsp_ink, shared with the present so the two
views always agree), run the UNTOUCHED faithful writers, EXPAND the glyph
span bits->chunky (ink 0 / paper 15). The Mac's mono cursor seed adds +1 to
the byte address (L053e 0x5c6 — an arm the colour-only jt1177 lift had
dropped) and jt995's mode-3 shift is l21d0(left ^ 8) (eoriw #8, CODE 5
0x241c); together they land every glyph at page bit (col + 8) uniformly —
the shim absorbs it as a fixed +8-bit bias in the chunky<->page mapping.
Verified numerically against the -4650/-4646/-4614 mask tables from the
DATA pool, then live.

TWO PORT BUGS the bring-up flushed out (both latent in COLOUR too):
- The GLIB codec's dest ROW STRIDE -3084 was NEVER SEEDED — the Mac sets it
  in the window-open tail (CODE 4 0x4884-0x4898: L04de >> L04f0); the port
  had lifted the function's head but not this line, so every jt1165/jt1181/
  jt1191-family blit advanced rows by ZERO (all glyph rows XOR-composited
  onto one page row). Seeded in color_mode_init. Colour consumers
  (jt119/jt122 editor cursor, jt1192 pattern fill, jt1126 scroll-blit) had
  been running with stride 0 all along.
- TENTATIVE-DEFINITION MERGE: two file-scope `static long g_frame_base;`
  declarations (the group-1 FC-pool cache AND the stand-in menu chrome's
  resident-buffer base) are ONE variable in C. The chrome loader's
  "keep FRAME.CTL resident" write hijacked jt468(1) for the whole session —
  in mono the codec drew FRAME.CTL's 8px colour pieces on FRAME.TLB's 12px
  step (a comb). The chrome-side write is deleted (nothing read it); group 1
  always resolves through port_frame_load's mode-correct pool load.

Colour build regression-checked (Falcon TOS 4.04 menu, pixel-clean); host
suite 175 passed. Debug knob: -DFRUA_MONO_TRACE = solid-ink expand tracer +
per-expand rect logging to DBG.LOG.

### Phase 2 runtime — MODE-3 MASKED LIFT (2026-07-16, fourth leg)

The mode-3 masked two-stream RLE format is LIVE: the B&W intro renders
(SSI/Micro Magic verified on Hatari ST mono; the UNLIMITED ADVENTURES
hand-dithered title verified through the engine's own jt1185 offline).

The format (from CODE 5 0x2f26 + CODE 4 0x94e): the payload = a 4-byte
header (L289a: w0 = CONTROL stream length, w1 = per-plane DATA advance),
a CONTROL stream (0xC0|n opaque copy, 0x80|n one-byte fill run, 0x40|n
per-byte masked RMW with the masks INLINE in the control stream, n
transparent skip, 0x00 row end) and a DATA stream. The #151 jt1185
goto-mirror lift was already in the tree (dormant) — l2d4e's mode-3 arm
now wires it: faithful clip derivation (byte-granular in mono), jt1177
cursor, per-plane loop, the mono sync/expand bracket ([xx & ~7, +vis*8),
no window slop — byte-granular cursor). TITLE/SPRIT top-level items are
NESTED SUB-GLIBs (item 0 = a mode-8 meta/order record; the metrics read
as 'GLIB' if parsed flat). ui_glib_blit now DELEGATES mode-3 pieces and
all 1/2-bpp B&W leaves to the faithful l2d4e dispatcher (its own arms
are 8bpp-only) — this also turned the B&W menu backdrop/window frame
into the real 1bpp art. port_show_intro grew its mono arm: TITLE.TLB,
NO palettes (a .TLB set's item 0 is meta, not RGB), paper-white field.

Bring-up finds:
- The -3050..-3056 CLIP RECT globals are ZERO before the first screen
  seeds them — the intro draws through l2d4e which clip-rejects against
  them, so EVERYTHING vanished. port_show_intro now calls jt1193() (clip
  = full screen). The colour intro never noticed: ui_glib_blit's own
  arms don't consult the clip.
- SELF-INFLICTED HAZARD, hours lost: the -DFRUA_MONO_TRACE debug build
  had the solid-ink expand tracer active, so every debug screenshot of
  the intro showed tracer output, not the pipeline's. The tracer now
  lives on its own knob (-DFRUA_MONO_TRACE_INK); FRUA_MONO_TRACE also
  holds each intro screen ~33 s with key-skip disabled for captures.
- The per-expand INK-BIT COUNT trace (expink in DBG.LOG) matched the
  host-harness pixel count exactly (10611) — the decisive instrument:
  page content proven correct live, which localized the fault to the
  clip/present path, not the writers.

Colour regression: menu byte-identical; colour mode-3 pieces (97 in
SPRIT.CTL + several PIC*.CTL event-picture pieces, silently dropped
before) now render through the same arm. Host suite 175 passed.

### Phase 2 runtime — PLAY-SCREEN HUD (2026-07-16, fifth leg)

The B&W walk screen (FRUA_AREATEST harness) has its HUD: the party
roster (inverse-highlighted leader), the verb bar, the compass surround
+ dial faces, position + clock — all at the 480x300 layout. Two fixes:

- The l2d4e 1bpp leaves (mode-2 PackBits + the mode-0 raw tail) painted
  set bits in the port's CURRENT PEN colour — a colour-mode concept (the
  pen-expansion of 1bpp art). On the Mac's B&W screen these are OR
  writers: a set bit IS ink. With the pen left white by a prior text
  draw, the play chrome painted invisibly. In mono the leaves now always
  paint ink (0).
- port_frame_load CACHED g_frame_base once — after the play-entry FAR
  pool loads compacted/evicted group 1, the cache went stale, l37aa
  failed its magic check, and every FRAME chrome piece silently vanished
  (l2856 returns 0 -> l2d4e never called: zero drawn, zero logged). It
  now refreshes from the live pool tables on every call. This also
  completed the intro's decorative border frame. Latent in colour
  (fail-safe: pieces vanish, not corrupt).

Debug-hold fixes: the FRUA_MONO_TRACE intro hold is ~33 s per screen
with the key-skip disabled (headless synthetic events skip instantly),
and actually honours the deadline now.

### Phase 2 runtime — B&W DUNGEON VIEW, first cut (2026-07-16, sixth leg)

The mono first-person view's FOUNDATION is in: the real .TLB wall tiles
load, synthesize, remap, and DRAW in B&W. What landed (all
jt1200()==3-guarded, colour byte-identical):

- l6eea .TLB PLACEHOLDER SYNTHESIS (Mac 0x6f84-0x7042): the .TLB wall
  sets ship one side of each L/R pair; items 4,7,10,14,...,47 are
  10-byte stubs rebuilt as a JT[111] copy of the prior item then
  H-mirrored (JT[123] -> jt992, whose jt1164 1bpp bit-reverse was
  already lifted). The size<16 guard no-ops for the all-present .CTLs,
  so it's faithful in every mode.
- l5b42 MONO DEEP REMAP: in jt1200()==3, ((v-8012)<<2)+8 pre-scales the
  8000-space coords to the 480x300 window (8032 -> 88, verified live);
  jt200's `deep && left<8000` +16 slot step then goes live (exactly why
  the Mac guards it on <8000).
- jt114 MONO LEAF: routes to l309c (jt1001-equivalent) -> l2d4e's 1/2bpp
  mono leaves, no colour band rebase, no cwf viewport clip (the GLIB
  clip globals rule, as on the Mac).
- render_3d_faithful MONO ARM: a line-for-line transcription of jt312's
  CODE 22 deep branch (0x2440-0x24f2) — l6148 wall load, the frame's
  JT[219] state-only pass, the FRAME piece-9 viewport, jt199 the frustum
  walk, present.
- The 8TH g_a5_2347 forcing found: jt918's post-dungeon menu restore hard
  -set g_a5_2347=1, knocking the mono walk back into the colour renderer;
  now g_port_2347-seeded.

DEFERRED (the tessellation sub-campaign): the tiles render but don't yet
compose into a clean corridor — this is the faithful jt199/l5b42/jt200
PIXEL path, which the COLOUR campaign (#124-#129) ultimately abandoned
for the pragmatic render_3d_raycast because the runtime layout-globals
composition was never fully solved. The colour view (render_3d_view /
_raycast, a clean framed corridor with starfield) is the reference for
what the mono view should become. Next: either solve the mono pixel-path
composition or adapt render_3d_raycast to 176x176 scale-3 1bpp. The B&W
perspective BACKDROP is also deferred (l6ea2's numbered path hunts per-id
back1NNN files the port folded into BACK.TLB sub-GLIB sets; needs the
same combined-file mapping the walls got, and to fit the pool alongside
them). Walls render over paper-white for now.

### Phase 2 runtime — B&W DUNGEON CORRIDOR COMPOSED (2026-07-16, 7th leg)

The tessellation sub-campaign is DONE: the mono first-person view now
composes a real receding corridor — cobblestone side walls converging to
a centered vanishing point with a door ahead (matching the colour view's
structure). Two fixes cracked it:

- THE .TLB TILE SCALE: the .TLB wall tiles are authored at 2x the colour
  .CTL tiles' resolution (640x400-native art), so they need the Mac's
  DEEP transform — l5b42's ((v-8012)<<2)+8 with jt200's +16 slot step (a
  16px grid), NOT the colour path's scale-3/+12px. Placing 2x-size tiles
  on a 1.5x grid was the overlap. Restored the deep pre-scale (mono only,
  jt1200()==3-guarded); the tiles land at direct screen coords (< 6000,
  jt1135 passthrough), so the viewport clip is set in direct coords too:
  jt1173(8,8,184,184) = the 176x176 hole (= colour's 88x88 x2, the user's
  key insight — the mono view is full 640x400-native resolution).
- THE 2bpp MODEL WAS WRONG: the flags-0x91 pieces (wall sides, compass,
  bar caps) are NOT "2bpp tone" — they are TWO-PLANE 1bpp MASK+DATA
  (verified: item 6 = exactly 2x448 bytes; plane 0 mask, plane 1 data).
  Per the Mac's inverted-mask compositor (jt1191): mask SET = transparent
  (the ~70% of the side-wall piece that is the opening beyond), mask CLEAR
  = draw the data bit (set=ink 0 / clear=paper 15). The old tone leaf
  painted level 0 as solid white, blocking the perspective AND hollowing
  the compass. The rewrite fixed both at once.

The view clips to its hole (roster/HUD stay clean), the compass renders,
and colour is byte-identical (menu + dungeon verified). Backdrop (sky/
ceiling) still white — deferred with the BACK.TLB work.

### Phase 2 runtime — B&W BACKDROP wired (2026-07-16, 8th leg)

mono_dungeon_backdrop loads BACK.TLB DIRECTLY (like load_backdrop opens
BACK.CTL) — NOT through l6ea2's FC binder, whose numbered "back1NNN"
path hunts per-id files this combined library folded into sub-GLIB sets.
It resolves the party cell's set (cell_backdrop_id -> ua_backdrop_to_back;
HEIRS start = the night sky, set 19), decodes item 1 (176x176 1bpp, mode
0 raw or mode 2 PackBits) and blits it into the view hole at (8,8),
BEFORE jt199 (the faithful Mac order — jt118 backdrop then walls).

TWO traps this leg:
- l2d4e's own qd_screen_pixels() lookup lands on a DIFFERENT back buffer
  than the `px` the caller threads down (verified: 9487 pixels written
  via l2d4e, 0 survived to the frame). mono_dungeon_backdrop blits
  DIRECTLY into `px` instead.
- The 2-plane mask+data DATA-bit polarity: data set = ink (0), clear =
  paper (15) — matches the walls (dark stone on white); the inverse
  hollowed the walls (they are mostly data-clear surface).

~~HONEST STATE: the backdrop draws every frame but is OCCLUDED...~~
RETRACTED by the 9th leg: the "occlusion" (and the "different back
buffer" note) were misdiagnoses. The real bug was a TRANSPARENT blit.

### Phase 2 runtime — ★★THE SKY + THE INK MODEL (2026-07-16, 9th leg; 5735d50 over-rotated, corrected same day)

The visible-sky leg became a root-cause hunt into the mono mode's
foundation, with a WRONG intermediate conclusion worth recording.

FACTS PROVEN LIVE (Hatari ST mono + TOS 2.06), all still true:
- The backdrop's pixels WERE in the presented buffer every frame
  (per-frame counts), the present blitted them (row-diff logs, forced
  full blits), and the screen still showed nothing — resolved by a RAW
  STRIPE TEST (memset 0xFF/0x00 straight into screen memory): **ST-high
  SET bit = BLACK**. hi_blit_rows' "SET renders WHITE (live-verified)"
  comment was FALSE.
- So the present maps chunky indices by the luminance INVERSE: BRIGHT
  index -> black, DARK -> white. That inversion is deliberate-in-effect:
  it renders the port's light-on-dark colour UI (pen-7 text, dark
  plates) as the Mac's dark-on-light mono UI with zero colour-arm
  changes.
- There is ONE buffer (px == s_chunky == qd-attached, pointer-proven);
  leg-8's "different back buffer" note was false. And the wall masks'
  ceiling/floor triangles are SET=transparent (8X8DB.TLB planes decoded
  host-side) — the deep Mac path needs NO l57f2/jt116 region fills.

THE WRONG TURN (5735d50): from the stripe test alone I concluded the
ART writers were sign-flipped ("the whole mode renders a NEGATIVE") and
flipped sync/expand + the l2d4e mono leaves + the backdrop to ink=15.
The result LOOKED convincing (the corridor read as plausible line art)
— but the USER's checks were sharper: the menu bevels rendered
pushed-in, and a night sky must be DARK with LIGHT specks; the flip had
produced an actual negative.

THE TRUTH: in this .TLB art family a SET data bit is **art-WHITE**
(DOS-derived convention), not the classic Mac set=black. Chained with
the display inversion, art-set must land in chunky as the DARK class
(canonical 0, renders white) — exactly what the ORIGINAL writers did.
They were right all along. The ONLY real bug was mono_dungeon_backdrop
blitting TRANSPARENTLY: it painted the set bits (the STARS, art-white)
onto the already-white field and skipped the clear bits that are the
black sky — white-on-white. Fix: revert the writer flips, keep the
backdrop OPAQUE with set->0 / clear->15 (the Mac JT[118] backdrop
covers its hole).

RESULT: the TRUE mono corridor — BLACK night sky with WHITE stars,
light coursed-stone walls, dark door, light tiled floor with dark grid;
menu bevels raised again, marble bars textured. Colour untouched
(mono-gated), make test 175.

DEBUGGING LESSONS:
- When a pixel provably in the buffer and provably blitted does not
  show, stripe-test the DISPLAY's polarity before inventing buffers.
- A polarity conclusion needs BOTH ends anchored: the display chain (a
  raw stripe test) AND the art convention (a semantically forced image
  — a night sky, a bevel direction). One anchor plus plausible-looking
  output is how 5735d50 shipped a negative as a "fix".
- Never trust a "live-verified" claim in a comment; re-verify it.

REMAINING worklist:
1. B&W event pictures (PIC%c.TLB 176x176) via a bigpic composer mono arm
   — also covers the user-sighted "bigpic loads then unloads" at entry.
2. Polish: per-cell wall variety, walk-test the corridor turns; a
   BasiliskII Mac-mono eyeball pass (open questions: the near-invisible
   play-frame chrome, the solid-black gated menu buttons DELETE/UNLOCK).
3. Codec loose ends (jt1165/jt1202/jt1197/jt1192 cursor family lacks
   expand brackets at its call sites) and a colour-side sighting of the
   newly-live SPRIT/PIC mode-3 pieces.

### Phase 2 runtime — B&W EVENT PICTURES LIVE (2026-07-16, 10th leg, 5725669)

The l2d4e mode-2 1bpp PackBits leaf drew SET BITS ONLY (transparent
clear) — every mono event picture ghosted over screen residue. The
HEIRS entry portrait rendered as an eroded smear (mistaken at first
for entirely different art — a host-side decode of PICB.TLB id 133
via its entry-0 id directory proved the runtime had the RIGHT art,
sub-GLIB 30, drawn wrong), and the take-treasure screen's picture
drew white-on-white: the post-event "blank viewport" and the
user-sighted bigpic load/unload weirdness were BOTH this.

DISASM TRUTH: the Mac's mode-2 handler (CODE 5 L2bfc) unpacks each
row (jt1171) and writes it with jt1165 — a verbatim `movew` row copy.
OPAQUE, no OR. The mono leaf now paints both classes (set = art-white
-> chunky 0, clear -> 15); colour keeps its historic pen expansion.

Verified live through the whole HEIRS entry chain: portrait -> typed
text pages -> XP/treasure announcement -> take screen (treasure-pile
picture + VIEW/TAKE/POOL/SHARE/EXIT chips) -> "treasure left?" YES/NO
-> farewell page. BONUS: the FRAME marble chrome bars render now (they
had been painting invisibly — the "near-invisible play-frame chrome"
open question closes). PIC id->library mapping documented: PIC%c letter
bands A<76 B<138 C<164 D<193 E<227 F, each library's entry 0 = an
{id,index} directory consulted by jt1013.

Colour: menu byte-identical (AE=0), make test 175.

REMAINING polish (new):
- mono message-text WRAP WIDTH overflows the message viewport into the
  roster (the "EACH CHARACTER RECEIVES..." page) — the mode-3 text
  layout width needs its Mac value.
- typewriter text is SLOW at 8MHz ST (per-glyph bracket+flush) — fine
  on Falcon/TT, painful on a real ST; candidates: span-batched expand,
  present_rect coalescing.

### Phase 2 runtime — the "wrap overflow" was a LEAKED CLIP RECT (2026-07-16, 11th leg, a7aead4)

The polish item "mono message text wrap width overflows the viewport"
dissolved under investigation: there was no wrap bug. The XP/treasure
page (l3806_c12) prints separate jt94 lines at rows 5/7/10 after a
jt76 full-page clear — and the clear only covered the view hole
because the mono render arm's port-added 176x176 clip (jt1173 before
jt199) was never restored. Every later QuickDraw FILL clipped to the
hole (half-cleared page, clock-plate fragments, missing walk verb
chips) while the mono TEXT path (jt995 planar, no clip consult)
escaped — producing the "text overflowing its box" illusion.

Fix: jt1193() after jt199 brackets the port-added clip (the Mac walks
the frustum under the full clip; narrowing it was our addition, so
restoring it is our obligation). The walk screen gained its verb-chip
bar + marble chrome; the XP page lays out cleanly. Colour AE=0, 175.

LESSON (3rd sighting of the class): a port-added global-state change
(clip, palette, canvas base) MUST be bracketed in the same function
that makes it. The colour arm saves/restores these four clip globals
explicitly; the mono arm forgot. And: "text overflows its box" is not
necessarily a layout-width bug — check whether the BOX (the erase
behind it) is the thing that's clipped short.

### Phase 2 runtime — walk test PASSED + the 5Hz present gate (2026-07-16, 12th leg, e67b029)

MONO WALK TEST (HEIRS, live): turns rotate the view + compass, steps
advance (door close-up at 12,8), doors open in place (faithful),
wall sets change (stone corridor -> wood-panelled hall), the backdrop
follows the cell zone (night sky -> beamed ceiling), and the AREA
automap renders (top-down wall lines + party arrow) — first mono
sighting of the automap. No render garbage found. (The position label
lags a repaint behind — cosmetic, the HUD repaint order.)

TYPEWRITER ROOT CAUSE (PC-sampled via Hatari's debug fifo): jt1134's
port-concession qd_present ran per pump call; on ST mono one present
(300-row diff memcmp + blit + cursor bake) costs MORE than a tick, so
the l435a per-glyph busy-wait ran presents back-to-back — 13/15 PC
samples inside the diff memcmp, ~3 chars/sec, every modal wait at
full burn. Gated to one present per 12 ticks (~5Hz): typing renders
at the design's pace, a keypress skips to the full page (faithful),
input latency unaffected (the pump still runs full-rate), and real
frame commits are not gated. Colour + mono menus AE=0, 175 tests.

DEBUGGING NOTE: the Hatari cmd-fifo PC-sampling recipe (echo
"hatari-debug r" > /tmp/frua-ui/cmd.fifo; resolve PCs via
m68k-atari-mint-nm + the load base from a known symbol) turned a
mush of theories into one answer in minutes — use it before
theorizing about any perf/hang symptom.

### Phase 2 runtime — ★★★THE FINAL INK MODEL, REAL-MAC VERIFIED (2026-07-16, 13th leg, 9bbf56d)

The user booted FRUA in BasiliskII B&W and captured the merchant event
and the 3D walk — the campaign's first true Mac-mono ground truth. The
Mac mono play screen keeps the COLOUR game's design language: BLACK
panels + WHITE text (roster, clock, message area), WHITE chips + BLACK
text (NAME/AC-HP, selection, verbs), marble stipple chrome, art as we
render it. (And the Mac game window is 480x300 — compact-Mac sized —
exactly this port's mono surface.)

THE MODEL (fourth and final sign combination):
- hi_blit_rows: DIRECT luminance — dark chunky index -> black ink,
  bright -> white paper. (The inverse mapping had silently inverted
  every panel/chip for twelve legs; the pure-art writers cancelled it,
  so screenshots kept "verifying".)
- Art writers: .TLB SET bit = art-white -> chunky 15 (sync/expand,
  l2d4e mode-1/mode-2, backdrop). Mode-0 pen/OR glyphs: ink = 0.
- g_dsp_ink[v]==1 = dark = INK at last; the naming-trap comments died.

Verified: walk screen + menu mirror the Mac captures; colour AE=0, 175.

RESIDUAL DELTAS (next campaign — mono HUD chrome coverage, not
polarity): the Mac clock plate is BLACK (ours unpainted white — likely
a mono FRAME piece we don't draw); NAME/AC-HP are white CHIPS (ours
bare text); the PRESS/RETURN/TO CONTINUE prompt is white chips + black
text (ours renders a dark bar — trace the event-prompt fill/style
path, expected l177a style 7 -> white). See the jt304/L3fd8 chrome map
(dungeon-hud-chrome-arch) for the Mac mono piece inventory.

MORAL, final form: a polarity conclusion needs THREE anchors — the
display (raw stripe), the art (a semantically forced image), and a
REAL-MACHINE UI reference. The first two cannot detect a uniform
UI inversion; only the Mac photos could.

### Phase 2 runtime — the L4e12 text plate + delta triage (2026-07-16, 14th leg, 2a02432)

Chasing the reference deltas found most were already right: the clock
plate (jt938 -> jt103 fill 8 -> black) and the row-24 prompt plate
(l177a style 7 -> white) render correctly under the final ink model —
the "deltas" were thumbnail misreads plus a stale dimmed-state frame.
The REAL missing piece was the header/verb CHIPS: the Mac's 1-bit text
renderer is CODE 4 **L4e12** (the mono sibling of L4fae), with its own
plate algorithm now lifted into jt1089's mono arm:

  white plate iff P==15 || T==0; black iff P==0 || T==15;
  else tbl3016[P]==0x3f -> white; tbl3016[T]==0 -> white;
  tbl3016[P]==0 -> black; tbl3016[T]==0x3f -> black; else P>T.
  Glyphs = TextMode XOR (always the plate's opposite).
  tbl3016 (A5 -3016, shipped DATA) = 00 for {0,8,11,12}, 3f else.

Worked examples all match the Mac captures: headers (8|12) white chip;
roster names (8|7) black plate; prompt (7|0) white; messages (8|15)
black. NOT lifted: L4e12's grey-pattern DIM overlay (PenPat -3430,
gated on g_a5_-932 whose setters aren't lifted; inert with the shipped
table). RESIDUAL: the RETURN DLItem chip paints ~2.5 chars too wide
over the prompt text (cap/tile geometry — menu-button-rendering #105
territory).

Colour AE=0, 175 tests.

### Phase 2 runtime — MONO PLAY-LOOP SWEEP (2026-07-16, 15th leg)

Drove every mono play screen (HEIRS, Hatari ST mono + TOS 2.06). ALL
render and are playable:
- VIEW character sheet: full stats, PLATINUM/EXPERIENCE, ITEMS list
  (LONG SWORD +1 / PLATE MAIL +1), TRADE/DROP/EXIT chips.
- ENCAMP: the campfire event picture + "THE PARTY MAKES CAMP..." +
  the camp bar (VIEW/MAGIC/REST/ALT/FIX/LOAD/SAVE/EXIT).
- MAGIC submenu (CAST/MEMORIZE/SCRIBE/DISPLAY/REST/EXIT); DISPLAY
  correctly empty for a fighter (no spells).
- COMBAT (via the 'k' CBTSND hook): the encounter INTRO composites the
  thief sprite over the 3D corridor with typed event text ("THIEVES
  DISCOVER THE PARTY..."), then the TACTICAL MAP renders — 1-bit combat
  sprites (party + monsters), the active-unit info panel (name /
  HITPOINTS / AC / weapon / status via jt38), and the full combat bar
  (AIM/USE/CAST/GUARD/QUICK/DELAY/VIEW/SPEED/END).

TWO combat follow-ups (cosmetic, not blocking play):
1. A vertical column of small glyph fragments sits in the map|panel
   GAP (~x330, right at the tactical viewport's right edge). Structural
   (survives a QUICK turn), aligns with the panel's HP/AC rows shifted
   ~1 cell left — smells like a scale-2-vs-3 coordinate slip in a jt38
   sub-draw (jt18 " " / jt28 held-item icon) or a map-viewport right
   clip that leaks the rightmost cell column into the gap. The main
   panel text (name/HP/AC/weapon) is correct.
2. The tactical FLOOR is pure black in mono vs the colour grey stone
   texture. Plausible for 1-bit (sprite contrast), but UNVERIFIED — no
   Mac-mono combat reference yet. Worth a BasiliskII combat capture.

Also observed: the combat tactical map DRAW is slow in mono (the
sprite blit over the whole viewport), a few seconds to fill — the same
8MHz-ST cost class as the typewriter, not a correctness issue.

### Phase 2 runtime — combat panel FRAGMENT solved: uncleared inter-panel seam (2026-07-16, 16th leg, eb7a59e)

The glyph-fragment column beside the mono combat info panel was STALE
encounter-intro content, not a live draw. jt77 lays two combat panels
— map (jt103(1,1,21,21)) and info (jt103(23,1,38,21)) — and neither
fills the col 21..23 SEAM between them (12px at mono scale-3, chunky
x264..276). That seam sits exactly where the encounter-intro roster
drew its AC/HP columns, so its middle glyphs survived into combat
(".)IAC" = the roster's stale "AC" header).

Method (what ruled out the wrong suspects, in order): a jt1089 trace
proved all panel TEXT draws at chunky x>=276 (none at the fragments);
a pre-clip of the 7x7 grid (l6652/l78fa) had NO effect (not the grid);
a jt57 position+clip trace showed all unit sprites at x<=240, clipped
at x248 by l744e (not the sprites). That left the x264..276 seam as
the only unaccounted region, and its width + position matched the
intro roster columns exactly. Lesson: when every live draw is
accounted for, the artifact is STALE — look for the region no clear
covers, not another drawer.

Fix: clear the seam in l276c after jt77 (mono only, jt1161 fill 8).
The Mac's frame chrome covers it; the port's mono FRAME pieces don't
reach it. Colour skips the arm (already covered), AE=0, 175 tests.

### Phase 2 runtime — MONO COMBAT-ROUND CRASH fixed: save-under page overflow (2026-07-16, 17th leg, 0ba7073)

Mono combat ROUNDS crashed intermittently (the tactical map renders
fine; the crash is in round execution). Caught live via Hatari
--debug-except: a bus error in _sthigh_present's row-diff memcmp
(s_chunky vs s_shadow) — but the present was the VICTIM, not the cause;
its buffer pointers had been corrupted by an earlier OOB write.

Chain: the hit/miss effect animation (jt503) save-unders the map under
a 12x12 spark via jt119/jt122 -> jt1177 (page cursor) -> jt1197/jt1202
(walk ~24 rows). jt1177's mono arm indexes s_mono_page at 60*row +
(col>>3) + 1; a low combatant's transformed `row` exceeds the fixed
60x300 page, so the multi-row save-under walk writes PAST s_mono_page
into adjacent BSS, clobbering the sthigh backend's s_chunky/s_shadow
pointers -> next present's memcmp bus-errors.

Fix: clamp the byte offset so the cursor + its 24-row span stay in the
page. Verified: combat + repeated QUICK rounds now complete where they
reliably crashed (3/3 + a 5-round run). Colour AE=0, 175 tests.

METHOD NOTE: `--debug-except bus,address,illegal,nohandler` + resolving
the PC/stack via `hatari-debug disasm` with `symbols prg` loaded is the
way to catch these — the stack return address (memcmp caller = the lea
that cleans 12 bytes) named _sthigh_present immediately.

RESIDUAL: the mono save-under is still fidelity-imperfect — the planar
page isn't sync/expand-bracketed like jt995, so an effect's restore
paints from an unsynced page (cosmetic, off-page effects only). A
proper fix would make jt119/jt122 save/restore the CHUNKY region
directly in mono (bypassing the page indirection entirely).

### Phase 2 runtime — MONO COMBAT VERIFIED END-TO-END (2026-07-16, 18th leg)

With the round crash fixed (leg 17), drove the FULL combat path in mono
(HEIRS, the 'k'+AUTOWIN harness) and confirmed every screen renders:
- encounter INTRO: the thief sprite composited over the 3D corridor +
  the typed "THIEVES DISCOVER THE PARTY..." event text;
- TACTICAL MAP: 1-bit combat sprites, the jt38 active-unit panel (clean
  seam after leg 16), the AIM/USE/CAST/GUARD/QUICK/... bar;
- ROUNDS: multiple QUICK passes + AI turns run to completion (the
  save-under page-overflow crash is gone);
- VICTORY/XP: "THE PARTY HAS WON. / EACH CHARACTER RECEIVES N /
  EXPERIENCE POINTS. / THE PARTY HAS FOUND TREASURE!" on a clean
  full-width panel (the leg-11 clip fix holds — no wrap overflow);
- TREASURE: the BIGPIC treasure-pile picture + roster + the
  VIEW/TAKE/POOL/SHARE/EXIT bar.

Also completed a MONO PAGE OOB AUDIT: jt1177 is the shared page-cursor
setter for every mono codec/save-under draw (5985/6699/33894/33922/
78887.. and the jt119/jt122 family), so the leg-17 bounds clamp there
protects ALL of them, not just jt503. The direct writers (mono_span,
max byte 18000 < the 18008 page) are in-bounds. The mono planar-page
path is now crash-safe.

Mono status: menu, intro, walk (turns/steps/doors/wall-sets/backdrops/
automap), event pictures, VIEW/ITEMS/ENCAMP/MAGIC, and full combat all
render faithfully and play. Remaining: verify the combat FLOOR (black
vs colour grey) against a Mac-mono reference; the cosmetic save-under
fidelity (redraw-masked); mouse-only menu-editor screens (harness can't
click).

### Phase 2 runtime — REAL-MAC MONO GROUND TRUTH (2026-07-16, 19th leg)

Ran the ACTUAL Mac FRUA in native 1-bit: Mini vMac (built from 36.04
source, `~/minivmac/`), emulated Mac Classic (68000), System 6.0.7,
FRUA launched from a clean machfs-built HFS volume (`frua-clean.dsk`,
sourced from `data/frua-mac/joined` + the verified rfork). Played from
the title through the HEIRS caravan chain, town walk, and into a live
random street combat (SEARCH-mode pacing fires the town's type-33 zone
event; REST is disabled town-wide so rest-interrupts are unavailable).
Reference screenshots: `data/mac-mono-ref/` (menu, caravan event,
treasure, 3D view, area map, encounter intro, combat map x2, roster,
title).

Findings against the port's mono build:
- CONFIRMED faithful: main menu (white chips / black panels / dithered
  disabled buttons), event screens (stippled BIGPIC + black text
  panels), treasure screen, 3D view (black night sky + white star
  specks, white-brick walls), encounter intro, combat info panel and
  verb bars, roster. The final ink model matches the real machine.
- DIVERGENCE (task #15): the combat tactical-map FLOOR. The real Mac
  renders the combat backdrop art as a dithered cobble/rubble texture;
  the port renders pure black. The combat backdrop path must go
  through the same 1-bit art conversion as the dungeon backdrop.

Two dead theories buried on the way: "FRUA needs Color QuickDraw /
68020" (wrong — trap presence is runtime-guarded; the box sticker says
B&W = 1 MB + System 6.0.7) and the error -50 launch failures (a
CORRUPT basilisk-40.img copy — the app rfork was zero-filled from byte
2; the known-good rfork diff was the smoking gun).

### Phase 2 runtime — the mono COMBAT FLOOR fix (2026-07-17, 20th leg)

Task #15 closed. The black tactical-map floor was NOT a missing erase —
the terrain layer itself drew nothing visible. DUNGCOM/WILDCOM's mono
tiles are 32x32 1bpp MODE-0 pieces (flags 0x90), and l2d4e's mode-0 arm
modelled the Mac as a transparent ink-stroke OR (ink 0 = black): black
strokes on the black panel = the void. The Mac disasm says otherwise —
L2970's mono arm writes mode-0 pieces through JT[1165]/JT[1202], the
VERBATIM OPAQUE row copy (CODE_05 0x29f8/0x2a3c): set = bright = 15,
clear = 0. The tiles paint their own white field with black cobble
strokes, exactly the real-Mac reference (mac_mono_combat_map.png).

One-line model correction, wide payoff: the FRAME chrome (8 mode-0
pieces) now composes as true granite stipple on every screen (menu,
play frame) instead of the flat fill; TOPVIEW's area-map terrain and
the GEN chargen stone are mode-0 too. The transparent mono UI pieces
are all mode 1 (mask+data) — untouched, verified by the piece-type
census across ALWAYS/FRAME/MENU/TOPVIEW/GEN/WILDCOM.TLB. Colour arm
unchanged (Falcon menu correct, make test 175).

Verified live: menu -> load A -> walk -> 'k' thieves fight: the
tactical map renders the rubble wall band + textured floor under the
sprites, matching the real-Mac model. (Also: the harness driver MUST
run with DISPLAY unset so it uses its Xvfb — with an inherited :0 it
runs Hatari on the real desktop and keys/clicks land only when that
window happens to hold focus. And pkill -x truncates at 15 chars:
"minivmac-classi".)

### Phase 2 runtime — mono COMMAND-BAR investigation (2026-07-17, 21st leg, OPEN)

The user flagged the command bar: the real Mac draws every verb as a
white chip with a black outline + black text (both combat rows and the
walk bar — see mac_mono_3d_view.png / mac_mono_combat_map.png); the
port's mono build renders the walk bar and combat row 2 as bare glyphs
on the granite. Combat row 1 (AIM/USE/...) is correct — a different
painter. Task #17 carries the full findings; the short version:

- The (8,15) colour pair the bar painter uses resolves BLACK under the
  disasm-verified L4e12 waterfall — so the Mac's white play-bar chips
  come from the DEEP-mode text path (L148a -> jt995: GLIB font-cell
  art with built-in white plates + border rows), NOT from L4e12, which
  is the dialog-text renderer. The port collapsed deep-mode bar text
  to DrawString (no plates) — that collapse is the gap.
- Instrumented jt382 cmd-1 never fired during bar draws (and dbg_file
  wrote nothing) — which port code paints the visible verbs is still
  unidentified. A jt382 mono white-chip branch was written and
  REVERTED as unverifiable this session.
- Debug traps for next time: display_sthigh has TWO s_chunky symbols;
  $18b354 is dead — the live triple-buffer pointers live at $18b67a
  ($1add52/$1a6000/$1a5f52). And SDL keyboard focus requires the
  pointer INSIDE the Hatari window (the driver's `key` warps it; raw
  xdotool does not — the source of this session's flaky navigation).

### Phase 2 runtime — mono command-bar PAINTER FOUND (2026-07-17, 22nd leg)

Chased the command-bar painter to ground (task #17). The verb/button
bars are ALL painted by **jt137**, the faithful Mac bar-button DLItem
handler that jt151 installs as the shape-1 method — REPLACING jt382,
which is why every jt382 / l1aea / l2c60 trace came up empty. jt137
msg=1 draws:

1. **jt448 -> jt995 mode 2** — the FRAME.CTL cap pieces item pal+10
   (left), pal+11 (middle, one per label char), pal+12 (right); pal=3
   for a highlighted button uses 13/14/15. This is the chip molding.
2. **jt1089** — the label, mono colour 0xF0 (503 when highlighted),
   which L4e12 turns into a white plate + black glyphs (the plate IS
   painted, full clip — confirmed).

FRAME.TLB 10/11/12 are mask+data (16x17); item 11 is 71% opaque = the
white chip body. Menu buttons render as white chips because the white
menu panel shows through the caps' transparent edges; the walk/combat
bars render granite because the port's port_draw_play_frame lays a
GRANITE frame under the bar and it shows through — the Mac has no
granite there. Fix (next session, verify first): in mono either fill
each button rect white before jt448, or skip the granite frame under
the bar strip. Also seen: the mono walk HUD (roster/clock/bar) flickers
black frame-to-frame — a separate present/double-buffer bug (~#16).

Method note: __builtin_return_address is unreliable under
-fomit-frame-pointer, but the disasm at the reported addr showed the
real `jsr jt1089`; TEXT base = runtime(jt1089) - nm(_jt1089) = 0x18872,
then nm-lookup of (caller - base) resolved jt137.

### Phase 2 runtime — mono flicker post-mortem + present profiler (2026-07-17, 23rd leg)

The 22nd leg's "walk HUD flickers black frame-to-frame" was chased with
frame-accurate capture (Hatari --avirecord PNG frames + wall-clock X
grabs, FRUA_AREATEST entering the walk directly): **in real time the
mono walk HUD is rock-stable** — idle AND moving. The AVI's white/black
stretches were emulated-time dilation (boot fast-forward), not anything
a player sees. Two real defects surfaced instead:

1. **#147 (fixed, f970ff8)** — the full play-frame rebuild was not
   atomic on the single-buffered ST High backend: port_draw_play_frame's
   grey-stone wipe is BRIGHT in the 1-bit ink model, and l3994's jt1128
   commit mid-rebuild flashed the white half-frame. `qd_present_hold`
   (nesting, DISCARD-on-release so jt312's trailing double present — the
   videl two-page #103 guard — still runs in full) brackets both jt312
   full-recompose branches.
2. **#16 profiled (eaa402e)** — FRUA_MONOPROF counters in the sthigh
   backend. Numbers from a walk: ~0 viewport rect-presents (every
   refresh full-screen, 144 KB memcmp each), whole 40-present windows
   packing 0 rows, 11 setpal force-fulls/window. The throttle matching
   the user's report ("should be FASTER than a 7.83 MHz Mac"): the
   SOFTWARE cursor (no hardware sprite off-videl) makes qd_cursor_track
   / qd_cursor_refresh run a FULL present per mouse-move event.

**#150 (in tree, UNCOMMITTED — needs a live-mouse check):** present only
the union of the cursor's old+new 16x16 rects via the backend rect hook;
g_cursor_save_x/y names the last-drawn position across both present
paths, so no trail. Headless verification is impossible — Hatari ignores
synthetic X mouse MOTION (warp/click only) — so it waits for a real
mouse session before committing. Same wastes (present-per-poll, double
present, cycle force-fulls) burden the colour videl path behind its
double buffer; carry the fixes across once proven here.

### Phase 2 runtime — the 8 MHz throttle found and killed (2026-07-17, 24th leg)

Present-caller attribution (extending FRUA_MONOPROF) at REAL emulated
speed found where the "unplayable at 8 MHz, fine at 32" time goes: the
engine's idle loops emit ~1.6 clean presents/second (flushed through the
event pump's #144 safety net), and each one cost **~310 ms** in the
sthigh backend's 144 KB full-screen diff scan — roughly HALF of all CPU
time spent scanning an unchanged screen. Fix (#152, e96e57b): a
`g_qd_touched` gate in the shim — set by every write path (fill/blit/
glyph primitives, qd_screen_pixels pointer grabs, palette installs),
cleared after a full present — lets a clean present on a single-buffered
backend return immediately. Cursor-move presents force through (a moved
pointer changes the composited output without touching the surface);
page-flipped videl never skips. Plus: sthigh set_palette arms its
force-full re-pack only when an index's dither/ink CLASS actually
changed, so the fire-cycle rotations and the recurring identical band
installs (HUD text, FRAME chrome) no longer trigger 480x300 re-packs.

Measured: boot + 25 s idle + 12-key walk dropped from 120+ real presents
to fewer than 40; idle scans eliminated. Verified: walk/steps/AREA
toggle/menu all correct, Falcon colour + host suite unaffected. #151
(commit c1f0196) preceded this leg: dsp_backend_t.pages (1 vs videl's
2) replaced the seven unconditional double presents, and l63c0's whole
initial compose now lands atomically under a #147 hold — the "editor
panels pop up then vanish" flash is gone. #150 (cursor dirty-rect)
remains in-tree, uncommitted, pending the live-mouse trail check.
Remaining candidates if 8 MHz still lags: the possible double jt312
per step (l1908 tail + loop tail), and the full 144 KB scan a SMALL
draw still pays (a dirty-rect accumulator would shrink it).

### Phase 2 runtime — the walk-step render, 6.5 s -> 1.6 s (2026-07-17, 25th leg)

Step timing at real 8 MHz (MONOPROF TickCount stamps) split the ~390-
tick (~6.5 s) walk step: FRAME piece 9 (static!) 93, backdrop 44, jt199
wall tiles 102-153, present ~9, remainder the internal full present.
Fixed in two commits:

- **#154 (f871d7d)**: latch the static viewport frame — piece 9 draws
  once, re-armed by port_draw_play_frame's wipe. 93 ticks -> 0 on steps.
- **#155 (3814d29)**: the 1bpp blit inner loops went byte-wise. The old
  arms paid a variable shift + two bounds tests + a long-mul address
  per PIXEL; now bounds hoist per piece, a 0xFF mask byte skips 8 px in
  one test, full-visible bytes expand unrolled (mono_expand8, solid-
  byte fast paths), only edges go per-bit. Plus: the mono deep render
  presents only the 176x192 view hole instead of a full present, and
  jt312's colour-geometry 88x88 tail rect is skipped in mono. Output
  pixel-identical to pre-optimization goldens (compare AE=0).

Result: backdrop 44 -> 14, walls 102-153 -> 38-43, whole step 93-99
ticks (~1.6 s at 8 MHz, ~0.4 s at 32). Remaining if more is wanted:
the hi_blit_rows pack loop (per-pixel ink-LUT, could unroll ~1.6x),
jt199's non-blit remainder (~35 ticks: l6eea loads/synthesis +
geometry), and the l63c0 command-exit full recompose (~10 s, dominated
by the same now-fixed paths — re-measure). The AREA toggle-back black
(task #21) predates all of this.

### Phase 2 runtime — the "toggle-back black screen" was a held-open curtain (2026-07-17, 26th leg)

Task #21 closed (#157, 8d8c8b8). The black screen after an AREA toggle-
back was never a stuck state: instrumented runs proved every compose
completes and presents. The visible black was jt240's MID-REBUILD frame
— l429c's wiped panels, the chrome-prelude plates, the bare bar —
flushed to the screen by jt117's l3994 commit with no hold active, then
left showing for however long the l63c0 re-entry compose takes (~25 s
pre-#155, ~9 s after). Return "fixed" it only by forcing a recompose;
30 s of idle also self-resolved it. The colour build disproved the
event-prompt hypothesis (same sequence, no event, no black).

Fix: three #147-family brackets — jt240's whole rebuild holds and
DISCARDS its mid-state, l40f8's toggle transition holds across jt221,
and qd_present_rect drops rects while a hold is active (the mono view-
hole present no longer flashes compose internals). The screen now keeps
the previous complete frame across the transition and swaps straight to
the finished play screen. Verified at real 8 MHz: six 3-second samples
across the failing sequence, zero black states, both machines.

Lesson for the log: on a single-buffered target, ANY un-held present is
a curtain-up — and a "black screen bug" whose repro window scales with
compose speed is a mid-state leak, not a state bug.
