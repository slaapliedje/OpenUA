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
