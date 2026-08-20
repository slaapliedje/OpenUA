# Native planar rendering plan (ADR-0016)

> **STATUS 2026-07-26 — DONE AND SHIPPING; this file is now a working log, not a
> plan.** The draw-time plane path is the default on `CPU68K=68000` and ships in
> the ST/STE and Amiga ECS zips. For current state read the **2026-07-26 ADR-0016
> status update in `docs/decisions.md`** — start there, not here. What follows is
> the exploration that got there, including several conclusions later overturned
> (most importantly the Phase-0 "native writers are not worth it" verdict, which
> the new-ink re-quant trigger routed around). Read it for the measurements and
> the dead ends; do not read it for what the code does today. Work landed on the
> `planar-native` branch, retired 2026-07-26 — `main` carries all of it.

Goal: the bitplane machines (ST/STe, Amiga ECS/OCS)
render **natively in planar bitplanes** — no per-present chunky→planar (c2p).
Falcon/TT keep the shared chunky path (VIDEL is 8bpp; they have the headroom).

This plan is grounded in a full read of the current render path (see the
reconnaissance in the ADR-0016 commit trail). Line anchors are `boot.c:NNNN`
against `src/engine/boot.c` unless noted.

## What the recon changed about the ADR

- **There is NO runtime wall scaling.** Pieces blit **1:1** (`l309c_tile`
  copies `h×w` bytes; the x2-scale experiment was tried and reverted,
  `boot.c:12686`). Perspective = the *choice* of a pre-authored trapezoidal
  piece per depth slot, not a resample. So ADR-0016's "pre-scale art to planar"
  is really just **pre-convert** each piece's chunky bytes to plane bits once at
  wall-set load; the piece is already the right on-screen size.
- **The fork is small.** The whole geometry + tile-selection pipeline
  (`jt312`→`render_3d_faithful`→`jt199`→`l5b42`→`jt200`→`jt200_layer`→`jt114`)
  is machine-agnostic and reused unchanged. Only two things fork:
  1. the **pixel leaf** `l309c_tile` (`boot.c:12653`) / `cw_blit_piece`
     (`boot.c:12774`) — the one place chunky bytes hit the surface;
  2. the **backend present** — replace `quant_banded`+c2p with a page flip
     (`display_ecs.c:305`, `display_ste.c:206`).
- **Everything shares one chunky surface** — walls, HUD (`l2c60`/`jt937`/
  `jt938`), text (`DrawChar`, `compat/quickdraw.c:2212`), menus, cursor all
  write indexed bytes via `qd_screen_pixels()`. A fully planar frame therefore
  needs each *leaf* converted — but there are only a handful, and the mono
  `FRUA_BWMODE` path (`s_mono_page`, `boot.c:6789`) is a working precedent for
  planar-leaf drawing.

## Strategy

- **Build flag `FRUA_PLANAR`.** The planar path develops behind it so `main`
  stays shippable and the chunky backends keep working. ST/STe/Amiga release
  builds flip to it once a phase is proven; Falcon/TT never define it.
- **Correctness first, blitter second.** Each leaf gets a CPU plane-write that
  is host-testable, then the hardware blitter is dropped in underneath the same
  interface (measurable win, no behaviour change).
- **Palette: apply the band-remap ONCE, not per present.** `quant_banded`
  (`platform/include/quantize.h:202`) stays — a planar frame still needs the
  256→N per-band map — but it is computed at scene/wall-set setup and consumed
  at **blit time** (the leaf writes `remap[index]`'s plane bits), eliminating
  the per-present 64000-pixel c2p. Wall pieces are authored against each set's
  own ~37-entry palette at CLUT base 32 (`boot.c:11700`), which maps cleanly to
  the per-band copper palette the ECS backend already runs (viewport band = the
  wall-set palette; HUD bands = the UI palette).

## The blitter (per the ST-family wrinkle)

Plane blits are the hot primitive. Hardware support varies:

- **Amiga** (OCS/ECS/AGA): blitter is core chipset — **always present**. Use
  `OwnBlitter`/`BltBitMapRastPort` or raw blitter regs with a cookie-cut mask
  (minterm for `(src AND mask) OR (dst AND NOT mask)`).
- **Atari STe / Mega ST**: BLiTTER standard — **always present**.
- **Plain ST**: blitter was an optional socket (standard only from the Mega ST
  / STe on) → **runtime-detect**. Probe via XBIOS **`Blitmode(-1)`** (XBIOS 64,
  TOS ≥1.2): bit 1 of the returned word = blitter present. If absent (or on
  pre-1.2 TOS where the call is unsafe), fall back to the **CPU plane blit**.
- **Detection lives in a HAL query** — `plat_have_blitter()` — set once at
  init. The planar blit primitive dispatches on it: blitter path vs CPU path.
  Amiga's query returns true unconditionally; Atari's runs `Blitmode(-1)`
  guarded by a machine/TOS check.

## Phases (each a focused, committable, verifiable step)

**Phase 1 — planar substrate + blitter detection (host-testable core).**
- Define the planar piece format: N bitplanes + a 1-bit transparency mask,
  word-aligned rows. New `platform/include/planar.h`.
- `chunky_to_planar_piece()` — convert a decoded chunky indexed piece (+ its
  transparency keys: global 255, per-set magenta `g_cw_strans`, `boot.c:11700`)
  through a supplied 256→N remap into planes+mask. Reuses the `c2p32.h`
  transpose network.
- `planar_blit_cpu()` — masked plane blit of a planar piece into a planar
  surface at (x,y), with clipping. The CPU fallback + the reference the blitter
  path must match byte-for-byte.
- `plat_have_blitter()` HAL hook (Amiga stub → 1; Atari `Blitmode(-1)`).
- **Host tests** (`tests/`): converter round-trips a known chunky piece to the
  expected planes; masked blit composites correctly over a background;
  transparency keys drop the right pixels. No emulator needed.

### Approach B — fixed-palette FULL-FRAME planar (chosen 2026-07-19)

Scoping the viewport-only hybrid surfaced FOUR escalating wrinkles, the last
fundamental: (1) the viewport has THREE writers (perspective fills via `map_px`
`boot.c:11167`, the BACK.CTL backdrop `g_back_img`, wall pieces via
`l309c_tile`/`cw_blit_piece`); (2) pre-converting pieces needs the palette before
the scene renders; (3) `g_cwf_body[]` is a single-set cache `jt200_layer` thrashes
per frame; and **(4) one palette per scanline** — the ST raster-split and the
Amiga copper both allow only one palette per scanline, and the viewport SHARES
scanlines with the roster, so the viewport cannot have an independent palette.

**(4) kills the hybrid but *enables* B.** The resolution to (4) — a single
**fixed per-scene palette** that both the planar walls and the chunky roster in
the same rows share — is exactly the end-state. With one fixed per-scene palette
(computed ONCE on scene load, not per present), **chunky and planar regions
coexist consistently**, so writers can convert to planar one region at a time
against the SAME shared remap, with no palette conflict. The hybrid fought the
constraint; B works with it.

**The model.** On scene load, quant once → the per-band palette + a `remap[256]`
(clut index → slot), exposed to the engine. Every writer draws native-planar
against `remap`; during the transition, un-converted writers stay chunky and the
backend **composites**: row-diff-c2p the chunky surface (same fixed `remap`), then
overlay the planar regions. Because the palette is fixed per scene, the two agree.
When the last writer is planar, the chunky surface and c2p are removed — present
is a flip. No per-present c2p, no reband.

**Per-band piece conversion.** The per-band palettes mean a clut index can map to
different slots in different bands, and a wall piece spans several viewport bands
(rows 24-112 = bands 1-5). Either (a) **pin the wall colours to consistent slots
across the viewport bands** in the quant (so a piece converts once regardless of
Y), or (b) cache converted pieces by (set-id, piece-idx, band). Prefer (a) — it
also steadies the roster/HUD colours and removes the #40 banding shimmer.

**Steps (each build + STE screenshot-diff + stprof):**
- **B1 — fixed per-scene palette + engine-exposed remap.** DONE across three
  commits: `dsp_planar_remap()` HAL query (foundation); the reband-skip guard
  (`c96e59f` — skip when the CLUT is byte-identical; measured low-value, only
  1/9 boot rebands are redundant); and the **fixed per-scene palette**
  (`7d02cd2`) — `st_reband` now does ONE global reduce replicated to all bands
  instead of 10 independent per-band median-cuts. That retires the visible **#40
  banding** (flat panels were striped brown/green/olive because a spanning colour
  quantised differently per band); live-verified seam-free on the menu +
  roster screens. Post-B2.1 the per-band scheme was moot (its beneficiary, the
  viewport, is composited separately), so global is both the seam fix and
  approach B's target palette model. The raster-split hardware is retained.
  - **No-regret refinement (future):** per-band ANCHORING — pin global-common
    colours to fixed slots, fill the rest per-band — restores >16 colours for
    art-heavy screens WITHOUT reintroducing seams. Not needed while the flat HUD
    is the only shared-surface content; revisit if an event picture looks
    posterised. This is also where the composited walls would get their own
    colours pinned (they currently map via the global luma fallback).
- **B2 — composite plumbing + planar dungeon viewport.** Add the planar-region
  composite to `st_present`/`st_present_rect` (`planar_blit_stlow`); convert the
  three viewport writers (fills = planar rect-fill, backdrop = pre-converted
  planar, walls = `planar_blit_cpu` of pieces cached per the (a) scheme) into a
  separate-plane viewport buffer the composite overlays. The per-step walk cost
  drops from viewport-c2p to a plane blit.
  - **B2.1 DONE** (commit 24b9a7a): composite plumbing + the whole viewport as a
    composited planar region. `render_3d_faithful` renders the three writers into
    a backend scratch (`dsp_viewport_scratch`, absolute coords) instead of the
    shared surface and commits the rect (`dsp_viewport_commit`); the STE backend
    converts scratch→ST-Low planes through the SAME per-band remap and
    `planar_blit_stlow`s it into the hole each present (one-shot, auto-cleared).
    The shared surface's viewport rows are frozen → row-diff skips the roster/HUD
    sharing them. Dispatch via `planar_viewport_register` in shared `planar.c` so
    both build trees link (Amiga keeps chunky until its own B2). Chunky backends:
    `vp == NULL`, byte-identical. Verified live on the STE (walls/floor/backdrop
    in the hole across walk+turn; roster/chrome clean; menu path unchanged).
  - **B2 remainder:** the conversion is still a per-render chunky→plane of the
    whole viewport (backend-side). B2.2 = push the plane writes into the leaves
    (pre-convert wall pieces at load via `chunky_to_planar_piece` + the (a)
    stable-slot pinning from B1; fills → planar rect-fill; backdrop → pre-converted
    planar), dropping the chunky scratch. Then Amiga's `dsp_viewport_scratch`.
### B3 — shrink/eliminate the full-frame c2p (the #41 bottleneck). SCOPED 2026-07-19.

**Profiling result (STE, after B1+B2):** the full-frame c2p (`st_blit_full`) is
~100% of `st_present` cost on recomposes — ~125 ticks ≈ **2.1s EMULATED per full
64000px present**, i.e. the ~12s/key menu lag. Reband = 0 during nav (B1 tamed
it); composite = ~0 (B2 is free). So the c2p is the whole story.

**Key architectural fact:** the ST backend is **single-buffered** — `st_blit_rows`
writes `s_screen`, the LIVE displayed page (`Setscreen(...,s_screen,0)`), so the c2p
runs alongside the video shifter's DMA and the Timer-B raster-split interrupts. The
open question was how much of the cost is compute vs that contention — **B3.0b
answered it: ~100% compute, contention ≈ 0** (see the RESULT below). The strategy
follows from that.

Do these CHEAP, DECISIVE steps before converting any writer:

- **B3.0a — why is a menu present full-frame? — RAN 2026-07-19, `-DFRUA_STPROF`
  per-present row log (`b30a …`), STE (tos206, `--machine ste`).** RESULT: **the
  row-diff already works and there is no spurious force-full.** Steady-state menu
  keypresses / partial redraws convert only the changed rows (measured 18, 29, 114,
  176). A full 200-row present fires ONLY on a genuine full-screen recompose: the
  `FORCED-full` path (after a re-band, i.e. a real palette/scene change — 9× during
  the boot intro's screen sequence) or a genuine all-rows-changed diff (4×). B1's
  re-band-skip already killed the redundant re-bands, so the surviving force-fulls
  are LEGITIMATE (the palette genuinely moved and every LUT changed). **There is no
  cheap "force-full fix" to be had.** Second finding: **`st_present` (the full path)
  is called RARELY** — the dungeon walk goes through `st_present_rect` (the viewport
  rect, made ~free by B2.2a) and the modal menus are idle; the full c2p only fires
  on scene/palette *transitions*. So the ~12s/key lag is not per-keypress c2p — it's
  the burst of full presents on a transition (the intro alone did ~13), each ~2.2s
  emulated of pure c2p, starving the event poll (the STE key-drop symptom).
- **B3.0b — compute vs contention? — RAN 2026-07-19, `-DFRUA_STPROF` `st_prof_b30b`
  (identical full c2p ×4 to the live `s_screen` vs a non-displayed ST-RAM page).**
  RESULT, **decisive: live = 525 ticks, off-screen = 525–526 ticks — IDENTICAL.**
  The c2p to the LIVE displayed page costs the same as to a page the shifter never
  fetches. **Contention is ~ZERO; the cost is ~100% COMPUTE** (≈131 ticks ≈ **2.2s
  emulated per full 64000px present**). This is physically sound: the ST/STE bus
  arbitration hands the CPU its cycles independent of address, so writing screen RAM
  is not specially penalised, and the Timer-B raster split fires in both cases and
  cancels. **This REFUTES the single-buffered-contention theory.**

**Decision (from B3.0a+B3.0b): the cheap fixes do NOT capture the #41 win.**
Double-buffering removes contention, which is ~0 here — so **B3.1 is NOT a #41
perf fix**; keep it only as the B4 substrate / anti-tear, not on the perf path. The
c2p is compute-bound and legitimately full on palette changes, so the win must come
from **doing less / faster conversion**: native-planar writers (B3.2+) — the static
granite chrome is re-c2p'd on every full present yet never changes within a scene,
the single highest-value region — and/or a **BLiTTER c2p** (Phase 4), since the
bottleneck is raw conversion throughput. A cheaper sub-lever worth a look first: the
per-pixel LUT indirection in `st_c2p_span`/`c2p4st` is likely a large slice of the
131 ticks — a tighter CPU c2p (or pre-converted planar pieces) helps every path
before any writer moves.

- **B3.1 — double-buffer + flip. DEFERRED off the perf path (B3.0b: contention≈0).**
  Not a #41 fix; belongs to B4 (present = flip) and anti-tear only. Two ST-RAM pages;
  c2p to the back (Logbase) page; flip Physbase on VBL. Handle: (a) the raster-split
  palette is display-timed → works across a flip unchanged; (b) row-diff needs BOTH
  pages current → adopt the VIDEL 2-page pattern (`pages`-many presents, or per-page
  dirty tracking); (c) the viewport composite targets the same back page.

**B3.2 flat-span fast path — DONE 2026-07-19 (commit 1c9ccaf), the FILLS win.**
A flat 16/32-px span (one repeated chunky value) converts to four CONSTANT plane
words, so the transpose collapses to four stores. `c2p4st_32_flat` +
`c2p4st_is_flat` (early-exit) in `c2p4st.h`, chosen per span in `st_c2p_span`;
byte-identical to the transpose (tests/test_c2p4st.py). Overlay-safe at 16px
granularity — a text word over a fill is not flat and takes the transpose. This
IS the "blitter-accelerated fills" idea, done on the CPU: the flat panels/
backgrounds convert cheap without any writer change.

**Measured (STE, b30b, identical full c2p ×4):**
- transpose baseline (content-independent): **525** (≈131 ticks/present)
- real flat-heavy menu: **336** (≈84/present) — **−36%**
- pure flat-store ceiling (all groups forced flat): **85** (≈21/present)
So the menu present decomposes as ~85 flat stores + ~251 non-flat transpose. The
**non-flat granite chrome texture + text glyphs are now ~75% of the menu present**;
flat stores are only ~25%, and cheap.

**Consequence — the BLiTTER fill is NOT worth wiring now.** Its entire headroom is
the ~21 ticks/present of flat stores (and it can't drive them to zero — setup +
the stores themselves). The CPU flat path already harvested the fills win. The
BLiTTER's real payoff is B4/native-planar (fill/copy PLANES with no chunky
source); defer it there (Phase 4). A standalone blitter c2p is impossible anyway
(the ST BLiTTER can't bit-transpose chunky→planar).

**Chrome-first-as-a-plate was investigated and REJECTED (2026-07-19).** The B2.1
viewport-plate pattern doesn't transfer to chrome: (a) the row-diff + B1 CLUT-guard
already leave the static granite alone in steady state (chrome only re-transposes on
the rare full presents — transitions/re-bands); (b) a pre-converted plate is
INVALIDATED by a re-band (the median-cut renumbers slots), which is exactly the
present where it would help — unless the chrome colours are pinned to STABLE SLOTS.
So the right tool is slot stability + a re-band-aware skip, not a plate.

**DONE instead — stable-slot alignment + re-band smart-skip (commit c13324d).**
- ALIGNMENT (`st_reband`): after the median-cut, permute the fresh 16 slots to
  best-match the PREVIOUS palette's positions (greedy nearest RGB). A colour that
  persists keeps its slot number → its remap entry is unchanged. Pure renumber; the
  frame is byte-identical (planes encode a slot, the palette supplies the colour).
- SMART-SKIP (`st_blit_full`): replace the post-re-band force-full with a per-row
  test — re-c2p only rows whose content changed OR that hold a value whose slot moved
  (`s_remap_dirty`, early-exit scan, runs only on a re-band pass). The static
  chrome/HUD (slots preserved) is left alone across a scene change.
Correctness is robust to alignment quality (a poorly-matched colour is just marked
dirty and re-converts). VERIFIED on the STE: menu, hall, and roster render
byte-identically; boot dropped from 9 forced-fulls to 1. Also the stable-palette
groundwork B4 needs.

**Gated to the MENU/non-viewport path for now; the dungeon still force-fulls.** The
re-band borrows `s_shadow` as its wall-pin overlay temp, which the smart-skip needs
intact. The un-gate (a dedicated `s_quant_tmp` buffer so the dungeon/combat re-band
skips the static HUD too — where the bigger win is) is written and correct-by-the-
same-argument, but the STE dungeon is unreachable headless (input lag drops the
party-add keys), so it's held back UNVERIFIED per the emulator-validated rule. Land
it once combat is reachable (better harness or a pre-seated-party design), verifying
walls+HUD render across a combat entry.

**Next transpose-bound lever (if more is needed): TEXT** (planar glyph blit) — the
other ~half of the residual ~251 ticks the flat path can't touch. Or go structural:
**B4** (fixed palette → no re-band → drop the c2p → present = flip), which the
alignment above is the groundwork for.

- **B3.2+ (native-planar writers) — the transpose-bound regions.**
  Convert the chunky writers to write plane bits directly against the fixed
  per-scene `remap` (B1), region by region, shrinking the c2p until B4 drops it.
  Writer inventory (all funnel through the 8bpp `s_chunky` via `qd_screen_pixels`):
  1. **Chrome** — the FRAME.CTL granite frame + panel dividers (`port_draw_play_frame`,
     GLIB `CopyBits`). STATIC, drawn once per screen; biggest single region.
     Convert once → the frame stops being c2p'd every present. *Highest value.*
  2. **Fills** — `PaintRect` / `qd_pixmap_fill` (quickdraw.c:671/564): solid rects
     → planar rect-fill of `remap[colour]`'s bits.
  3. **Text** — `DrawChar`/`DrawString` (quickdraw.c:2102/2212) + `jt94`/`jt1089`
     label paints (roster names, stats, menu labels). Needs a planar glyph blit
     (1bpp font → N planes via `remap[fg]`/`remap[bg]`). *The fiddly one.*
  4. **DLItem shape paints** — button/list/field cells (`l14d0` shape-3, `jt377`
     shape-7 label, `l1676`) that draw the menu chrome + rows.
  5. **Cursor** — the software sword/shield (plat_cursor inactive on ST → shim
     composites into `s_chunky`). Planar sprite blit, or move to the VBL path.
  6. **GLIB art / event pictures** — `CopyBits` of decoded art (BIGPIC etc.).
     Pre-convert once + planar blit; OR keep these chunky+c2p (rare, art-heavy —
     the anchoring caveat), c2p only their rect.
  7. **Automap** — the top-down AREA overlay.
  Order by c2p-area removed per unit effort: **chrome → fills → text → DLItem →
  cursor/automap/art.** Each conversion removes its region from the per-present
  c2p; measure the c2p-tick drop after each.

- **B4 — drop chunky + c2p.** When the last writer is planar, `s_screen` (planar,
  double-buffered from B3.1) IS the surface; delete `s_chunky` + the c2p; present
  = flip.

### B4 fidelity-preserving — SCOPED 2026-07-19 (user picked this path)

**Decision:** keep the PER-SCENE median-cut palette (no colour regression — the
fidelity-losing fixed-global-palette variant was rejected), convert the writers to
emit plane bits at DRAW TIME against the current scene's `remap` (`dsp_planar_remap`,
B1), then drop `s_chunky` + the per-present c2p and make present a VBL page-flip.
Draw-time conversion is the key vs a cached plate: a native writer re-emits when the
engine redraws on a scene change, so it's never reband-invalidated (the flaw that
killed chrome-as-a-plate).

**This is effectively a BIG-BANG, not incrementally decomposable.** The B2.1
freeze+composite pattern only works for a SELF-CONTAINED region (nothing draws over
it) — true for the viewport, false for almost everything else: the direct writers are
BASE+OVERLAY mixed (`port_draw_play_frame` fills grey clut-21 + draws the granite
chrome, then the roster/clock/text/viewport draw ON TOP into the same surface). Freeze
a base layer and the un-converted overlay on it is lost (proven by the fills-first
analysis). So the c2p can only be dropped once the LAST writer is planar — there is no
clean partial state. Scope:
- **33 direct `qd_screen_pixels` sites** (grep) write raw chunky bytes to the surface;
  all must switch to plane writes. Plus the shim primitives `PaintRect`/`qd_pixmap_fill`
  (rect → planar rect-fill of `remap[c]`'s bits), `CopyBits` (chrome/art → per-pixel
  planar scatter or a pre-converted planar piece), `DrawChar`/`DrawString` (1bpp font →
  N planes via `remap[fg]`/`remap[bg]`), the cursor composite, automap.
- A `remap`-changed (reband) forces a full engine REDRAW (the writers re-emit) — the
  engine already redraws on scene changes, so this is mostly free; verify no scene
  re-installs the palette without redrawing.

**UPDATE 2 (2026-07-19): 3 of 4 dungeon bugs FIXED; roster HUD blank remains. WIP
on branch `b4-pageflip-wip` (pushed), NOT merged — single-buffer stays default.**
Verified via FRUA_AUTOPLAY. Three fixes past the first attempt:
1. **`$820D`** (STE video base LOW byte) must be written on the flip — without it
   the flip was imprecise and the 3D viewport came up BLACK. (The menu worked
   anyway because both pages held identical static content.) Writing hi/mid/low
   ($8201/$8203/$820D) → the flip is exact and the **viewport renders** (torch,
   stone walls, floor).
2. **`st_vp_composite` blits the viewport into BOTH pages** (was one-shot on the
   one page being drawn → the other page's hole stayed black).
3. **On a re-band, force-full BOTH pages** (`s_force_full=NPAGES`), NOT the
   smart-skip. `s_remap_dirty` is computed once per re-band, but the two pages were
   last drawn with DIFFERENT remaps (they alternate), so that single dirty map left
   the other page's granite in stale slots — the **"brown chrome"**. Force-full
   re-converts both pages against the current palette → **granite grey (correct)**.
   (Costs the smart-skip's modest re-band saving; the flat-fill already tamed the
   c2p, so it's fine.)
RESULT: menu perfect; dungeon renders grey granite + torch/stone walls + compass +
chrome. **ONE BUG LEFT: the party-roster HUD line blank.**

**PAGE-FLIP COMPLETE 2026-07-19 — all four dungeon bugs fixed (branch
`b4-pageflip-wip`, commit c245904). Verified via FRUA_AUTOPLAY: the dungeon renders
IDENTICALLY to the single-buffer — grey granite, torch/stone walls + floor, compass,
and the GOLD roster "NAME AC HP / LADY ILLIS -4 84" + clock. Menu perfect.** The
final fix: force-full BOTH pages in the SAME present on a re-band (see the roster
trace below for why). NOT yet merged to `planar-native` — MERGE DECISION: the
page-flip removes the single-buffered progressive-update tearing and is the required
B4 substrate (present = flip), but it costs **2 c2p's per re-band** (both pages;
normal presents stay 1, the dungeon walk/present_rect is untouched), so a scene
TRANSITION is ~+60% c2p there. That's a small #41 regression on transitions in
exchange for anti-tear — net-neutral until the native-planar writers drop the
per-present c2p entirely (then the flip is pure win). Recommendation: keep on the
branch and merge once the writer conversion makes it a clear win, OR merge now if
anti-tear is wanted and the transition cost is acceptable.

**ROSTER BUG TRACED + FIXED 2026-07-19 — it was GREY-ON-GREY, not a page/writer
problem.** FIX: force-full both pages in one present on a re-band (see the page-flip
note above). The trace that led there:
Path: `jt937`→`l02dc`→`jt25`→`jt94`→`jt1089`→`PaintRect`+`DrawString` on the current
GrafPort → `s_chunky`. Instrumented `jt1089`: the roster IS drawn to `s_chunky`
("Name"/"AC HP"/"LADY ILLIS" at py=22-38, px=136-296, read-back pixel = clut **23**).
So it's not a missing writer or missing page. The blank is a COLOUR collapse: logged
`s_band_remap` shows rebands where **remap[21] == remap[23]** (e.g. both slot 5) with
**clut21 RGB == clut23 RGB** (both R=255 or R=131) — the HUD-text colour (clut 23)
equals the panel-grey (clut 21) in the CLUT, so the median-cut maps them to ONE slot
and the text is invisible. Other rebands have them distinct (clut21 R=119 vs clut23
R=91 = the gold text). The **single-buffer consistently displays a gold-23 frame**
(roster visible, verified 2×); the **double-buffer consistently displays a grey-23
frame** (invisible, verified 4×) — so it IS a double-buffer regression, driven by a
present-cadence × HUD-text-CLUT-timing interaction (the same family as the known
`port_hud_text_clut` "grey-on-grey" HUD issue, jt1089 comment ~8484: the HUD text
CLUT is only distinct after the l63c0 compose installs it). The double-buffer's extra
presents/rebands land the displayed page on a transient grey-23 CLUT.
**FINISH direction:** ensure the DISPLAYED page's palette is the gold-23 one — either
don't reband/flip onto a transient CLUT where clut23≈clut21 (skip that reband, keep
the last distinct palette), or re-assert `port_hud_text_clut` before the final
present. NOT a "mirror the roster to both pages" fix (it's already in s_chunky).

**First step ATTEMPTED 2026-07-19 (attempt 1) — menu works, dungeon palette bug.**
Implemented: `NPAGES=2` ST-RAM pages (256-aligned; SCREEN_BYTES=32000 is a 256
multiple), `pages=1` (present ONCE — the backend double-buffers INTERNALLY so the
shown page is always freshly drawn; presenting twice would double the c2p),
`st_flip_full()` draws the HIDDEN page then latches Physbase via a supervisor
hi/mid base-register write (non-blocking, latches next VBL), `present_rect` draws
the SHOWN page IN PLACE and does NOT flip (an earlier flip-on-present_rect showed
the back page's stale/blank HUD), per-page `s_shadow_pg[2]`, and `s_force_full`/
`s_remap_changed` as COUNTS set to `NPAGES` on init/re-band (both pages owe the
treatment; consumed one per full present). RESULT: **the MENU renders perfectly
(anti-tear, correct)** — stable palette, no rebands during nav, so both pages agree.
**The DUNGEON is broken:** wrong colours (granite comes up BROWN not grey) + black
viewport + blank roster HUD. ROOT CAUSE (to fix next): the raster-split hardware
palette is GLOBAL (one `st_band_stpal`), but with two pages a shown page can hold
planes drawn against a different reband's slot assignment than the palette currently
installed — and the dungeon's partial updates (`present_rect` viewport/HUD, which
never reband) desync from the full-present palette. The menu never rebands mid-nav so
it's fine; the dungeon rebands on wall loads. FIX DIRECTION: ensure the SHOWN page's
planes always match the installed palette — e.g. on any reband, force the shown page
to re-c2p before it can be displayed (not just count both pages), and/or re-apply the
viewport/HUD partial updates to the shown page after a palette change. **WIP diff was
saved (session scratchpad `b4-pageflip-wip.diff`, 232 lines) — re-implement from this
design; it is ~90% there, only the palette-coherency bug remains.** The design below
still stands; add the shown-page-palette-coherency guard:
  - **Two-page row-diff:** a change must reach BOTH pages. Keep a shadow PER PAGE
    (`s_shadow[2]`); each present converts rows where `s_chunky != s_shadow[back]` and
    updates that page's shadow — so each page independently tracks to the current frame.
    A reband invalidates both pages' shadows → 2 full converts (rare; acceptable).
  - The alignment/smart-skip (committed) assumes ONE shadow — extend it to the
    per-page shadow carefully (this is where subtle bugs hide; verify menu+dungeon via
    FRUA_AUTOPLAY, like the un-gate bug that blanked the HUD).
  - Raster-split palette is display-timed (reads `st_band_stpal`) → works across a flip
    unchanged. The viewport composite targets the same back page.
Then convert writers (fills → text → chrome/CopyBits → cursor/automap), verifying each
in the dungeon via FRUA_AUTOPLAY, until the c2p can be deleted and present = flip only.

**Reality check:** the incremental #41 wins are largely BANKED already (flat-fill −36%
+ the stable-slot alignment/smart-skip that stops re-transposing static chrome on
rebands). B4's remaining payoff is dropping the per-present c2p ENTIRELY (present =
flip) — the ADR-0016 end state — but it's a large dedicated effort, best begun fresh
with the page-flip substrate, not rushed. Verify everything in the dungeon now that
FRUA_AUTOPLAY makes it reachable.

**Verdict (B3.0a+B3.0b ran 2026-07-19): compute is the floor — the cheap fixes are
out.** No spurious force-full (B3.0a); contention ≈ 0 (B3.0b). So there is no
force-full quick win and double-buffering does not help #41. **Next session goes
straight to B3.2, chrome first** (convert the static granite frame to a pre-computed
planar region composited like the B2.1 viewport, so it stops being re-c2p'd on every
full present — the largest single region), measuring the full-present c2p tick drop
with `-DFRUA_STPROF` after it. In parallel, evaluate (a) a tighter `st_c2p_span`/
`c2p4st` inner loop and (b) the Atari BLiTTER c2p (Phase 4) — both attack the same
compute wall and may beat the per-writer grind. The `b30a`/`b30b` instrumentation is
committed under `FRUA_STPROF` for exactly this before/after measurement.

**Amiga/ECS:** same c2p cost, never got B2.1 (ST-only). Its double-buffer story
differs (copper palette, separate planes) — a parallel track once the ST path is
proven.

### B4 WRITER-CONVERSION SCOPING — 2026-07-19

Full read of every screen writer (the 33 `qd_screen_pixels` grabs + the QuickDraw
shim primitives). Result: **the writers are NOT 33 independent rewrites — they
funnel through a small set of shared chokepoints.**

**The two doors every ST pixel goes through:**

*Door 1 — QuickDraw shim primitives* (`compat/quickdraw.c`, machine-agnostic;
each writes `pm->baseAddr` = `s_chunky`):
- `qd_pixmap_fill` (`:564`) — all `PaintRect`/`EraseRect`/pen fills; solid + pattern,
  modes patCopy/Or/Xor/Bic.
- `DrawChar` (`:2102`) — glyph store (fg/bk), Mac-font + 8x8 fallback paths.
- `CopyBits` (`:1230`) — art/chrome rectangular blit (srcCopy).
- `cursor_composite` (`:1886`) + `cursor_restore` — software cursor, save-under.

*Door 2 — 30 real direct `qd_screen_pixels` sites* (grep shows 35 hits: 1 is the
definition, 4 are comments). Of the 30: **5 are READ-ONLY** (jt81/cg_char_sheet/
cg_body_repro grab only sw/sh for the clip rect; jt94 is FRUA_ROW24TRACE; qd_dbg_draw_mark
is FRUA_CLICKMARK) — no conversion needed. **2 are dev/mono-only** (frua_spilltest;
mono_span/mono_rows are FRUA_BWMODE, not the colour path). **1 is a pointer seeder**
(jt1177 stores the `-3076` base for the jt1192/1194/1197/1202/1126 + jt119/122 family;
already has an FRUA_BWMODE branch redirecting to a 1-bit page — the natural planar hook).
The remaining ~22 funnel through a handful of engine primitives:
- `l2d4e` (`:6185`) — the **GLIB piece blitter** (RLE/t7 decode → `px[dy*pitch+dx]`);
  the general BASE+OVERLAY primitive most art/pieces route through.
- `port_draw_play_frame` — the play-frame BASE (grey stone fill + granite chrome via
  CopyBits). Drawn once per screen; the largest static region.
- full-screen `memset` / `fill_backdrop` / `draw_plate` / `menu_button_bevel` — flat
  fills (encounter_screen, port_show_intro ×2, menu_run, jt574, jt904, jt918, cg_train).
- `ui_glib_blit` (`:25239`), `jt357`→`jt200` (`:69945`) — UI art / 3D-art pieces.
- viewport (jt221/jt312 3D leg) — **already planar** (B2.1).

**Value ranking (from B3.2 profiling — the transpose is the cost, flats are cheap):**
1. **CopyBits granite chrome + DrawChar text glyphs** = ~75% of a menu present (the
   non-flat transpose). *Highest value by far.*
2. **Flat fills** (memset/fill_backdrop/draw_plate) = ~25%, and the flat-fill c2p
   fast path (commit 1c9ccaf) already makes them cheap → **low value to convert.**
3. GLIB/UI art pieces (l2d4e/ui_glib_blit) — medium; art-heavy, rarer.
4. Cursor — small, but needs slot-space save-under (see below).

**Three confirmed hard problems (independent of strategy):**
1. **Slot-space semantics.** Planes encode a 4-bit *slot* (`remap[index]`), and the
   256→16 remap is LOSSY (not invertible). The bitwise modes (`patXor` pen, `patOr`/
   `patBic` patterns, cursor XOR-under) and every readback (`cursor_composite` save-under
   `*sv=d[dx]`, `CopyBits` src read) must be reworked to operate in slot/plane space.
   Per-plane XOR/OR/BIC are well-defined; the loss only bites where code reads a pixel
   back expecting its index (cursor save-under → save 4 plane bits instead).
2. **The reband→redraw contract (THE make-or-break risk).** Today a palette change marks
   bands dirty and the present re-c2p's the retained `s_chunky` to reapply the new remap
   *without the engine redrawing*. Drop `s_chunky` and any palette change NOT accompanied
   by a full engine redraw leaves stale planes under a new palette. Mitigating evidence:
   `port_draw_play_frame` commits palette AND redraws together; B1's CLUT-skip already
   kills defensive same-palette re-installs. **De-risk task #1: audit every `qd_set_palette`
   (via l6e58 SetEntries) for a change that is NOT followed by a full redraw.**
3. **Base+overlay z-order** — the "big-bang" claim. TRUE for the batch-c2p present model
   (planar base + chunky overlay on it = wrong z-order, since c2p is deferred to present
   while planar writes are at draw time). See the two strategies below for the escape.

**The strategic fork (which the incremental-vs-big-bang question reduces to):**

- **STRATEGY A — Region-composite (extend B2.1), big-bang for the final c2p drop.**
  Convert self-contained regions to composited planar overlays like the viewport; the
  backend keeps c2p'ing the shrinking chunky remainder. Matches the committed viewport
  and the mono precedent. Hits the base+overlay wall: the c2p can only be DELETED once
  the LAST writer is planar (no clean partial end-state), so present=flip arrives only at
  the very end. Lower per-step risk; the intermediate wins are just "this region stops
  being re-c2p'd," which the row-diff + smart-skip already largely deliver for static
  chrome → **modest marginal #41 gain until the whole thing lands.**
- **STRATEGY B — Draw-time direct-to-planar (rework the present model), INCREMENTAL.**
  Drop the batch c2p: every primitive writes `s_screen` at DRAW time. Converted primitives
  emit `remap[val]` plane bits; not-yet-converted primitives do an immediate small c2p of
  just their own output. Everything lands in draw order → z-order preserved → writers
  convert ONE AT A TIME, and the c2p work shrinks continuously toward zero (present becomes
  cursor-composite + vsync; single-buffered first, page-flip from b4-pageflip-wip layered
  on later for anti-tear). This is the genuinely incremental path and reaches the ADR-0016
  end state cleanly. Cost: upfront rework of the present model (batch→draw-time) and it
  stands or falls on de-risk task #1 (the reband→redraw contract). The row-diff
  optimization is retired, but a draw-time model only touches what's actually drawn, so an
  idle modal pass converts NOTHING (strictly cheaper than today's re-c2p-on-idle).

**Recommendation:** Strategy B, but gated on de-risk task #1 FIRST (cheap: instrument
`qd_set_palette` for redraw-less palette changes; if any exist and can't be made to
redraw, B is unsafe and A is the fallback). Then convert in value order: **CopyBits chrome
+ DrawChar text first** (the 75%), fills last (already cheap). Each step verified in the
dungeon via FRUA_AUTOPLAY. Everything develops behind `FRUA_PLANAR`; single-buffer stays
default until a phase is proven.

### DE-RISK #1 RESULT — RAN 2026-07-19 (task #57): redraw-less/partial rebands EXIST

Instrumented `st_present` (FRUA_STPROF `b4audit`): per genuine reband, log rows of
`s_chunky` content changed since the last present (= the redraw signal, sampled before
`st_reband` borrows `s_shadow`) + CLUT bytes moved. STE, tos206, `--machine ste`,
FRUA_AUTOPLAY through intro → menu → dungeon entry. 14 rebands:

| reband | content rows | CLUT moved | class |
|---|---|---|---|
| #1 | 0 | 593 | boot init (nothing drawn) — benign |
| #2,3,4,5,7,9 | 200 | 3–713 | full redraw — SAFE |
| #6 | 176 | 657 | **partial** (24 rows not redrawn) |
| #8 | 0 | 667 | **redraw-less** (palette swap in place) |
| #13 | 23 | 581 | **partial** (177 rows not redrawn) |
| #14 | 0 | 88 | **redraw-less** (palette swap in place) |
(#10–12 were reband-SKIPS — CLUT byte-identical, B1 guard — inherently safe.)

**Verdict: naive Strategy B ("drop `s_chunky`, writers re-emit") is UNSAFE.** Rebands
#6/#8/#13/#14 change the palette while leaving 24–200 rows un-redrawn. In the chunky
model the post-reband force-full re-derives those rows' planes from the retained
`s_chunky` under the new remap; drop `s_chunky` and there is no source to re-derive them —
they'd display old planes under a new palette (wrong colours). These are intrinsic (intro
cross-fades + within-scene palette settles), not removable.

**BUT the fix is clean and is an extension of B1, not a chunky-retention hack.** For a
content-UNCHANGED region the index→slot remap is a pure function of the pixel *indices*
(unchanged) — so the planes are ALREADY correct; only the slot→RGB hardware palette
(`st_band_stpal`, loaded by the raster split) changed. So a within-scene palette change
needs only a **hardware-palette-register reload, no plane rewrite** — PROVIDED the backend
keeps the index→slot `remap` FIXED across the scene (never re-quantises mid-scene and
shuffles slots; the partial-redraw rows then stay valid too, since their indices→slots are
unchanged and only the RGB moved). That is exactly B1's "fixed per-scene palette" taken one
step further: pin the remap for the whole scene, make every within-scene reband a pure
palette-register reload, and planes are NEVER invalidated.

**Consequence for the B plan: the FIRST implementation step is NOT writer conversion — it
is "within-scene reband = palette-register-only (fixed remap)".** Concretely: distinguish a
NEW-SCENE reband (re-quantise; comes with a full redraw — the content=200 cases) from a
WITHIN-SCENE palette change (content ≈ 0 or partial: keep the remap, rebuild only
`st_band_stpal` from the new CLUT). A "surface touched since last present" signal already
exists at the shim (`g_qd_touched`, quickdraw.c). With that guard, Strategy B is clean and
incremental: writers convert one at a time against a scene-stable remap, redraw-less
rebands recolour via the hardware registers for free, and `s_chunky` can finally be dropped.
Without it, B corrupts on the intro fades — so this guard is the true Phase-0 of B.

### B4 PHASE-0 DONE — scene-stable remap / palette-register-only reband (2026-07-19)

Implemented in `display_ste.c`. On a palette change whose surface CONTENT is byte-unchanged
since the last present (`memcmp(s_chunky, s_shadow) == 0`, and no viewport pending), the
present takes a new `st_repalette()` path instead of `st_reband()`: it keeps the fixed
index→slot remap (so the on-screen planes stay valid) and rebuilds only the slot→RGB
hardware palette (`st_band_stpal`) from the new CLUT. Mechanism: `st_reband` now captures
`s_slot_rep[16]` — one representative CLUT index per slot (the used index nearest the slot's
centroid) — and `st_repalette` re-derives each slot's colour by tracking that actual palette
entry through the new CLUT (faithful for a fade). Shared `st_build_hw_palette()` does the
band replicate + STE encode for both paths. Discriminator is conservative: requires a prior
re-quant (`s_banded_valid`), an exact content match, and NO active viewport (the dungeon's
wall-pin still re-quants) — so it only fires on unambiguous within-scene palette changes.

VERIFIED on the STE (tos206, FRUA_AUTOPLAY, FRUA_STPROF path log): reband #8 (the intro
fade, content=0, no viewport) took `-> repalette (registers only)` — replacing its ~2.2s
force-full c2p with a register reload; the post-intro MENU renders pixel-correct (granite
chrome, olive panels, all labels) and the DUNGEON renders identically to the reference (grey
granite, torch/stone walls+floor, compass, GOLD roster "LADY ILLIS -4 84" + clock). All
content>0 rebands and #14 (fires under an active viewport) correctly fall back to re-quant.

This is an unconditional improvement to the shippable ST build (correct + skips the
force-full c2p on within-scene palette changes — a #41 win on fade/transition-heavy screens)
AND it establishes the invariant Strategy B needs. NOT behind FRUA_PLANAR — it's safe in the
current chunky model. Open item for later: partial-redraw rebands (#6/#13) still re-quant
(correct in the chunky model); when `s_chunky` is dropped they'll need their un-redrawn rows
handled (force a full redraw on those scenes, or retain indices only for reband recovery).

### B PHASE-1 TEXT-OVERLAY ATTEMPT — mechanism proven, LIFECYCLE WALL (2026-07-19)

Branch `b-text-overlay-wip` (commit 0763436, NOT merged; `planar-native` stays clean).
Built the first native-planar TEXT writer as a composited overlay (the viewport pattern
applied to glyphs), all FRUA_PLANAR-gated so default builds are byte-identical (verified):
- HAL `dsp_text_scratch/commit/clear/clear_rect` + `planar_text_register` (planar.c) —
  parallel to the viewport hooks.
- Backend: a screen-sized chunky text scratch (key 0xFF) that the present converts to planes
  and composites ON TOP of the c2p'd base (keyed, via the scene-stable per-band remap).
- `DrawChar` (compat): on the on-screen port, glyphs render into the scratch instead of
  `s_chunky`; commit the cell.
- Region-scoped invalidation: `qd_pixmap_fill` (so PaintRect/EraseRect/`jt1161` panel boxes)
  clears the overlay under a fill so text drawn AFTER survives; plus whole-scratch clears at
  the full-screen wipes.

**Mechanism PROVEN:** the main MENU renders pixel-perfect through the overlay — every label
crisp, drawn as composited planes. **But the cross-screen / modal LIFECYCLE is a wall:**
- whole-scratch clear (hooked at a chrome composer like `jt76`) either ghosts the previous
  screen's text or blanks the dungeon HUD — the correct spot is order-dependent;
- region-scoped clear kills the ghost, but a MODAL that re-fills its panels without redrawing
  its (static) text then wipes that text — the party hall lost all but its last-drawn labels.

**Decisive root cause:** generic text and the batch-c2p composite fight over **draw order**.
A separate overlay cannot reconstruct the per-pixel draw-order composition the single-surface
c2p gets for free (fills and glyphs interleave per screen/modal). This is the SAME
shared-surface wall **chrome** hit — the B2.1 viewport works ONLY because it is a fully
self-contained rectangle nothing else draws into. **Conclusion: incremental single-writer
conversion in the batch-c2p model does not work for shared-surface writers (chrome OR text).**
The infrastructure is sound and reusable; the composition model is the blocker.

**Two ways forward (the real fork):**
1. **Draw-time present model (Strategy B proper).** Every writer — fills, glyphs, chrome —
   writes `s_screen` at draw time in draw order (converted → planes; not-yet-converted →
   immediate small c2p bridge). Draw order is preserved by construction, so any writer
   converts cleanly and the batch c2p is retired toward present=flip. This is the larger
   rework but the ADR-0016 end state; the text-overlay HAL/composite code feeds into it.
2. **Self-contained-region overlays only.** Keep the composite model but apply it ONLY to
   rectangular regions nothing else draws into and with a controlled redraw lifecycle — the
   viewport (done) and, plausibly, the dungeon HUD panel if its redraw is gated. Generic text
   and chrome are out of scope for this path.

FRUA_AUTOPLAY did not advance past the party hall this session, so the dungeon HUD was not
re-verified under FRUA_PLANAR (a harness issue, separate from the overlay).

### DRAW-TIME REMAP-BOOTSTRAP WALL — measured 2026-07-19 (the third wall)

Starting the draw-time conversion surfaced a hard prerequisite: draw-time writers convert
`index -> remap[index]` at the MOMENT they draw, so the per-scene `remap` must be ready BEFORE
the frame is drawn. Today it is computed AT PRESENT from the drawn `s_chunky` (the median-cut
histograms which indices actually appear). So the first domino is remap-from-CLUT — derive the
16-slot palette from the CLUT, which exists at set_palette time.

**Tried it (FRUA_PLANAR, quantise a synthetic 1x256 frame of every CLUT index). MEASURED: a
clear QUALITY REGRESSION** — the menu came up blue background / brown panels / cyan text.
Cause: the scene CLUT holds ~32 meaningful menu entries plus ~224 STALE entries (leftover wall
palettes in clut 32+); reducing over all 256 spends the 16 slots on colours that never appear,
so the visible colours get poorly-matched slots. Deduping/limiting to a range needs knowledge
the CLUT alone doesn't carry (which indices the frame uses) — exactly what the frame histogram
provides and the CLUT does not. **The remap genuinely needs the drawn frame for quality.**

**Consequence — the draw-time model cannot cleanly drop the scene-transition c2p.** A new
scene's first frame needs a remap it can't have until it's drawn; the quality remap requires
histogramming the drawn frame. So the transition still needs a "draw -> histogram -> convert"
pass — i.e. the ~2.2s full c2p we wanted to remove stays (for the remap bootstrap). Draw-time
could still remove the PER-FRAME changed-region c2p (HUD/walk updates become plane writes) —
but that is already minimised by the row-diff + the B2.1 viewport composite. So the draw-time
model's remaining #41 payoff is **small**, against a large, risky big-bang.

**This is the THIRD wall this session** (chrome: base+overlay z-order; text: overlay draw-order
lifecycle; draw-time: remap bootstrap). Together they say the incremental composite path is
exhausted beyond self-contained rectangles AND the full draw-time rewrite's payoff is limited
by the remap bootstrap. **#41's wins are largely BANKED** — flat-fill (-36%), stable-slot
alignment + smart-skip (static chrome not re-transposed on rebands), and Phase-0 (within-scene
palette change = register reload, no force-full). Recommendation: treat the native-writer
pursuit as closed at Phase-0; if more #41 is wanted, attack the c2p THROUGHPUT directly (a
tighter `st_c2p_span`/`c2p4st` inner loop, or the Atari BLiTTER for plane FILLS on transitions)
rather than the writer-conversion grind.

### DRAW-TIME PRESENT MODEL — chosen 2026-07-19 (user picked option 1)

Both incremental composite attempts (chrome, text) hit the SAME wall: to skip the transpose
for a converted writer, its pixels must be excluded from the batch c2p, which is only possible
if nothing un-converted draws over them — false for every shared-surface writer (base+overlay).
The B2.1 viewport is the sole clean fit because it is a fully self-contained rectangle. So the
composite path is EXHAUSTED beyond self-contained regions; the remaining #41 payoff needs the
draw-time model. It is a **big-bang**: correctness cannot be verified per-writer incrementally
(a converted writer's plane output is invisible while the batch c2p still overwrites its
region, and excluding the region re-opens the z-order wall). The unit of verification is the
whole conversion.

**Model.** The 8bpp `s_chunky` + deferred batch c2p is replaced by DRAW-TIME conversion: every
surface writer emits ST-Low plane bits into `s_screen` at the moment it draws, in draw order,
through the scene-stable per-band remap (`dsp_planar_remap`, Phase-0). Draw order is preserved
by construction (each write lands in `s_screen` when it happens), so base+overlay compose
correctly with no separate overlay and no z-order reconstruction. When the last writer is
converted, `s_chunky` and the c2p are deleted; present becomes a VBL page-flip (the
`b4-pageflip-wip` substrate).

**Writers to convert (all must land before the c2p is dropped):**
- Shim primitives (compat/quickdraw.c), centrally hookable: `qd_pixmap_fill` (fills + pen),
  `DrawChar` (glyphs), `CopyBits` (art/chrome blits), `cursor_composite`/`cursor_restore`.
- Engine-direct writers (the ~22 non-read-only `qd_screen_pixels` sites): the GLIB piece
  blitter `l2d4e`/`l309c_tile`, `port_draw_play_frame` grey fill, the full-screen memsets
  (`jt904`, `encounter_screen`, `port_show_intro`), `draw_bevel`/`jt1161` panel boxes,
  `ui_glib_blit`, `jt357`→`jt200` art. These write bytes to `px[y*pitch+x]` today and are the
  bulk of the work — each must switch to a plane store.
- Viewport (B2.1) already planar.

**The engine-direct writers are the crux.** They assume a chunky byte array. Two options to
convert them without rewriting all the index math: (a) each site switches its `px[i]=v` store
to a plane-store helper `planar_put(screen, x, y, remap[v])`; or (b) they keep writing a small
chunky scratch and immediately c2p just that primitive's rect into `s_screen` (an "immediate
c2p bridge") — mechanical, preserves the index math, and shrinks to nothing as (a) lands.
Prefer (b) for the blitters (keeps the RLE/transparency decode intact) and (a) for the flat
fills (trivial constant plane words).

**Substrate to build first (host-testable, no integration risk):** the plane-store primitives
this all rests on — `planar_fill_stlow` (rect → constant plane words for a slot),
`planar_put_stlow` (one pixel's slot bits), and a `planar_glyph_stlow` (1bpp mask → planes via
remap[fg]/remap[bg]). Host tests assert each against a naive reference (same discipline as
tests/test_c2p4st.py). These are correct and testable in isolation; integration (routing the
writers through them) is the big-bang that follows, verified by screenshot-diffing the fully
converted FRUA_PLANAR build against the current batch build across menu / hall / dungeon /
combat. The text-overlay HAL/composite code on `b-text-overlay-wip` is superseded by this
(draw-time text is a `DrawChar` plane store, no separate overlay/scratch/lifecycle).

Then Phase 4 (blitter) and Phase 5 (palette polish, folds in #40) as below.

**Phase 4 — blitter acceleration.**
- Drop the hardware blitter under `planar_blit` (Amiga blitter; Atari BLiTTER),
  gated by `plat_have_blitter()`, CPU path as the proven fallback. Measure the
  ECS/ST walk-step render cost before/after.

**Phase 5 — palette + banding polish (folds in #40).**
- Native per-band palettes chosen per scene remove the median-cut band
  artifacts that show as banding on ST/STe (#40). Tune band boundaries and the
  wall-set-vs-UI palette split.

## Verification harness

- Host: `make test` (pytest) covers Phase 1's converter/blit and the c2p
  transpose already tested in `tests/test_c2p_amiga.py`.
- ECS: `.claude/skills/run-amiga-port` + `openua-ecs.uae` (KS3.2). Budget
  ~105 s+ to menu on the 7 MHz 68000.
- ST/STe: `.claude/skills/run-falcon-port` with `--machine st`/`ste`.
- Baseline to beat: capture the current ECS/ST walk-step render cost before
  Phase 2 lands so the win is a real number (same discipline as the mono
  profiling).

## Non-goals

- Falcon/TT are **not** touched — they keep chunky (ADR-0016).
- No new dungeon geometry or perspective work — `jt199` & friends are reused
  verbatim.
- AGA (256-colour, 8 planes) can adopt the planar leaves later; ECS/ST are the
  priority since they feel the c2p tax most.

### CONVWHY ATTRIBUTION — RAN 2026-07-26 (after #48)

The question this was built to answer: of the rows still paying a chunky→planar
span, how many are **coverage holes** (no native writer ever stamped them) and
how many are **stamp mismatches** (a direct writer clobbered shim-stamped
pixels)? Holes mean "convert another writer"; mismatches mean "an existing
writer is being overwritten". They imply completely different work.

**First finding: the instrument had never fired.** `st_prof_hot_dump()` was
gated on a 64-present window, and a scripted headless drive produces under 20
presents — so in every session to date the hot-row and why dumps emitted
nothing at all. Window lowered to 16, and per-class totals added (the existing
per-row dump is a *sample*: threshold count ≥ 2, first 24 rows only).

**Result over a boot → dungeon → 14 step/turn drive, ST/STe planar:**

| class | rows |
|---|---|
| coverage HOLE only | 0 |
| stamp MISMATCH only | 0 |
| **BOTH, within the same 16-present window** | **200 (all of them)** |

and every mismatch reported its first differing pixel at **x = 0**.

**That is not a writer-coverage story.** A genuine direct-writer clobber starts
at the writer's own left edge, so mismatches would spread across varying x. x=0
on all 200 rows means the comparison baseline is wholesale stale — the entire
surface is being invalidated, repeatedly.

The counters in the same window say why:

    stprof: presents      = 16
    stprof: rebands       = 11
    stprof: reband skips  = 0

**Eleven rebands in sixteen presents, and the repalette fast path never once
took.** Every palette change went to a full re-quantise, which force-fulls and
resets the ownership epoch — so the draw-time model barely gets two consecutive
presents in which to own anything. The ~47% of rows still converting are
overwhelmingly rows whose ownership was thrown away by a reband, not rows
nobody wrote natively.

**Consequence for #63.** The next lever is NOT "convert more writers" — that
was the assumption going in and the data does not support it. It is: stop
rebanding. Two threads, in order:
  1. Why does the DE-RISK #1 repalette path (scene-stable remap, palette-
     register-only reload) report **zero** skips here? It exists precisely to
     absorb palette changes that leave content alone. Either the drive genuinely
     changes scene 11 times, or the skip predicate is too strict.
  2. If some rebands are unavoidable, make the epoch reset partial — a reband
     that only moves a few slots need not invalidate rows whose pixels map to
     unmoved slots.

CAVEAT: `s_prof_convwhy` accumulates with `|=` across the whole window, so a row
flagged BOTH may have been a hole in one present and a mismatch in another. Per-
present resolution needs a per-present reset; the x=0 signature and the reband
count are what carry the conclusion, not the BOTH classification on its own.

### TT030 — DONE BY A CHEAPER ROUTE (#99, 2026-07-29): -94% CONVERSION WORK

**Result: the same drive went from 95,092 converted rows to 5,792 — a 16.4x
reduction — with all six verification frames PIXEL-IDENTICAL.** Two changes, and
the smaller one is the interesting one.

Where it started: `platform/display_tt.c` converted **every row of every frame**,
unconditionally, and none of ADR-0016's machinery reached it.

**1. The dirty-row present (the obvious half).** The shim already maintains a
per-row dirty set — `qd_touch_rows`/`qd_touch_all`, storage in
`platform/planar.c` — and `planar.h` already sized it for this backend
(`PLANAR_DIRTY_MAX` is 512 "because TT-low is 320x480"). Nothing consumed it
here. The TT can go FURTHER than the ST with it, too: this backend is
single-buffered, so a row nobody wrote still holds correct planes and can be
skipped entirely, where the ST must rebuild a row once per page before it counts
as clean.

**2. ★ THE PALETTE WAS FORCING A FULL FRAME, AND THAT WAS THE REAL BLOCKER.**
Landing (1) alone changed almost nothing: measured in play, **456 of 480 presents
were still full** at ~198 rows each. The `QDT` attribution counters (the #63
instrument, previously dumped only by the ST backend) named the culprit
immediately — over 480 presents:

| site | hits |
|---|---|
| 3 **palette** | **521** |
| 5 glyph | 1260 (mostly `qd_touch_rows` — #63's pass-1 work) |
| 1 fill | 266 |
| 0 grab | 10 |
| 4 cursor | 0 |

`qd_set_palette` called `qd_touch_all()` **unconditionally** — more than once per
present. On most backends that is right, because the on-screen bytes encode the
COLOUR: VIDEL blits through a LUT into a 16bpp screen, and the ST/ECS backends
quantise 256 indices down to 16/32 slots so a new palette changes the remap and
every plane bit is suspect. **The TT is neither.** TT-low is 8 planes = 256
colours, `tt_c2p_span` transposes the raw chunky index with no remap, and
`tt_set_palette` gives CLUT entry i the colour of index i — so plane value ==
chunky index == CLUT slot, and `EsetPalette` alone makes the change visible. Same
identity that made the AGA port (#86) short.

So `dsp_backend_t` gained **`hw_palette`** ("a palette change is applied by
hardware and does not invalidate converted pixels"); only the TT sets it. Every
other backend's initialiser is a positional literal that stops before the new
field, so they all get 0 = the old behaviour, by construction.

**The second-order effect is bigger than the first-order one.** `qd_touch_all`
also sets `g_qd_touched`, which gates the #152 clean-present skip
(`!g_qd_touched && g_present_pages == 1`). That gate already existed and already
applied to single-buffered backends — the palette site was simply keeping the TT
permanently "touched" so it could never fire. With the flag in place, for an
identical drive:

| metric | before | after |
|---|---|---|
| palette `touch_all`s | 521 | **0** |
| presents reaching the backend | 480 | **32** |
| presents skipped clean (QDT 6) | 0 | **619** |
| rows converted, total | 95,092 | **5,792** |
| rows per backend present | ~198 | 181 |

Most of the win is whole presents never happening; the dirty-row scan contributes
the ~10% on the 32 that remain.

**Verified** (TT, Hatari `--machine tt`, TOS 3.06, 32 MHz 030 + 68882): six frames
— main menu, Game Settings, monster list, monster record editor, the HEIRS
caravan BIGPIC event, and a 3D walk frame with the roster/compass HUD — all
**0 differing pixels** against the pre-change binary. Falcon main menu also 0
differing pixels (it shares the binary; `hw_palette` is 0 there so the shim path
is byte-for-byte the old one), and the STE 68000 planar build still boots its
quantised path. Four targets build, 417 tests pass.

**★ DONE 2026-08-01 — see "#160 THE TT WRITER HALF" at the end of this file.**
The text below records why it was deferred; it is history now. The TT registers
a draw-time target, the shim writers stamp its planes, and 87% of the rows the
present still handles need no conversion at all.

**WHAT WAS NOT DONE AT THE TIME, DELIBERATELY: the draw-time WRITER half of
ADR-0016.** On the TT the writers still paint chunky and the present converts;
they do not stamp planes. Two reasons, both worth recording:

- It is not a display-file change. The whole draw-time writer layer in
  `compat/quickdraw.c` is `#ifdef FRUA_PLANAR`, which the Atari 020 build does
  not define — and that build is ONE BINARY shared by the Falcon and the TT, so
  it cannot be selected per machine at compile time. The runtime contract already
  handles this correctly (`dsp_planar_draw_target()` returns 0 on VIDEL, so
  writers keep their chunky path), and the AGA release proves an 020 build can
  carry `-DFRUA_PLANAR`. So the route exists; it just enables a large amount of
  code in the Falcon's binary and needs its own verification pass.
- The headroom left is small. 94% of the conversion work is already gone, and
  #96 measured the display layer at only 3.7% of the ST play path — a 32 MHz 030
  has far more slack than that.

**NOT measured: wall-clock on the TT.** Everything above counts conversion WORK
(rows, presents). Under Hatari the host, not the emulated CPU, sets the frame
pace, so a wall-clock claim would be worthless here; it wants real hardware.

★ Trap found on the way: **`FRUA_STPROF` does not compile without
`FRUA_PLANAR`** — its ST profiling block references the planar-only `s_dt`,
`s_pend`, `st_row_differs` and `st_dt_build_row`. It has only ever been used on
the 68000 ST build, where PLANAR is implied. So the `QDT` instrument in
`compat/quickdraw.c` now answers to `FRUA_STPROF` **or** `FRUA_TTPROF`; the
display_ste.c breakage itself is left alone (not this task's scope).

Reproduce: `make EXTRA_CFLAGS=-DFRUA_TTPROF`, then
`HATARI_ARGS="--machine tt" driver.sh start` + `beginplay`; the counters land in
`data/work/gamedata/DBG.LOG` every 16 presents (cumulative — difference
successive windows, and remember the BOOT is ~16 presents of legitimate
whole-screen repaints, so never read window 1 as steady state).

### #89 REBAND ATTRIBUTION — RAN 2026-07-26. BOTH PLANNED THREADS CLOSED NEGATIVE.

#89 was opened on the convwhy finding ("11 rebands in 16 presents, zero
repalette skips") with two hypotheses. Instrumenting the decision killed both,
and surfaced a third that looks better than either.

**Thread 1 — "the repalette skip predicate is too strict." WRONG.**

Added per-conjunct attribution to the reband/repalette decision (which of
`s_banded_valid`, `!s_vp_active`, content-unchanged, `!st_remap_split()`
vetoed the cheap path). Across 11 rebands, **content differed every single
time** — 9 of them with all 200 rows changed. The predicate is not too strict;
the content genuinely changed. DE-RISK #1's redraw-less rebands are a
menu/fade phenomenon, not a play-loop one, so in a dungeon drive there is
simply nothing for the skip to catch.

One reband looked like pure waste — CLUT byte-identical (`clut moved = 0`) yet
still re-quantising — and is not: that is the NEW-INK trigger deliberately
clearing `s_banded_valid` so the CLUT-guard cannot skip, which is what stops
post-reband inks rendering invisible. Working as designed.

**Thread 2 — "make the epoch reset partial." DEAD ON ARRIVAL.**

The idea: a reband that moves only a few palette slots need not invalidate
rows whose pixels map to unmoved slots. Measured it — snapshot band 0's
index→slot remap, re-quant, count how many indices landed elsewhere:

    reband over a real scene change:  121 used indices, 114 moved slot = 94%

At 94% churn essentially no row ownership could survive, so the bookkeeping to
preserve it would buy nothing. Not worth building.

**Thread 3 (NEW) — rebands over near-blank frames.**

Of 3 fully instrumented rebands, **2 quantised a frame using a single colour
index** (`used idx = 1`), one of them while reporting all 200 rows changed —
i.e. the screen had just been CLEARED to one colour and the real content had
not been drawn yet. Quantising that produces a palette derived from nothing,
which the next frame's real content must then immediately re-quantise.

If that pattern holds, deferring the quant until the frame carries meaningful
content would remove a whole class of rebands. Unlike threads 1 and 2 it
attacks the reband COUNT rather than the per-reband cost, which is where the
leverage is.

**Sample-size caveat, stated plainly:** the 94% churn figure is ONE scene
change, and the blank-frame observation is 2 of 3 rebands. The 11-reband run
that produced the veto attribution predates the remap-churn instrumentation.
Before building anything on thread 3, re-run with both instruments over a
longer drive and count what fraction of rebands see `used idx` below, say, 8.

**Consequence for #63.** With threads 1 and 2 closed, the remaining levers are
thread 3 (fewer rebands) and the one the 2026-07-19 plan already named: attack
c2p THROUGHPUT directly — a tighter `st_c2p_span` inner loop. The copy path is
done (#48, 3.4%), ownership is capped at ~53% by rebands, and the rebands are
mostly legitimate.

### #90 THREAD-3 RE-RUN — RAN 2026-07-26. THE 47% IS THE BOOT, NOT THE PLAY LOOP.

The #89 write-up asked for one thing before anyone built on thread 3: re-run
with BOTH instruments over a longer drive, because the blank-frame figure was
2-of-3 and the 94% churn figure was a single scene change. Done. The re-run
answered thread 3 — and incidentally overturned something bigger.

**Thread 3, on 12 rebands with both instruments (was 3).**

| reband | content rows | clut moved | used idx | used moved |
|--:|--:|--:|--:|--:|
| 1 | 0 | 593 | **1** | 0 (0%) |
| 2 | 200 | 713 | 121 | 114 (94%) |
| 3 | 200 | 126 | **1** | 0 (0%) |
| 4 | 200 | 455 | 61 | 53 (86%) |
| 5 | 125 | 3 | 130 | 80 (61%) |
| 6 | 200 | 657 | 223 | 214 (95%) |
| 7 | 200 | 659 | 32 | 29 (90%) |
| 8 | 200 | 667 | **7** | 7 (100%) |
| 9 | 200 | 74 | 22 | 16 (72%) |
| 10 | 200 | 0 | 25 | 11 (44%) |
| 11 | 7 | 512 | 22 | 9 (40%) |
| 14 | 138 | 0 | 23 | 7 (30%) |

- **Near-blank rebands: 3 of 12 (25%)**, not the 2-of-3 the small sample
  suggested. Real, worth having, but a quarter of the rebands — not most.
- **Churn is NOT uniformly 94%.** It ranges 0–100%, median ~72%, and reband 14
  moved only 10 of 256 indices. Thread 2 stays closed (a row survives only if
  EVERY index it uses is unmoved, and at a 72% median that is rare), but the
  "94%" in the #89 entry was the worst case, not the typical one. Recorded so
  nobody re-derives thread 2 from a figure that was one sample.

**The finding that matters more: split the same run at `menu: modal up`.**

| phase | presents | rows CONVERTED | rows skipped | rebands |
|---|--:|--:|--:|--:|
| pre-menu (boot) | 16 | 2976 | 200 | **11** |
| post-menu (menus/hall/roster) | 12 | **0** | 549 | **0** |

Every reband, and every converted row, was the BOOT SEQUENCE. Once past the
menu the draw-time path skipped 100% of rows on the screens driven here, and
re-banded zero times. A subsequent 36-key dungeon-nav drive added no rebands
either.

**This reframes the CONVWHY entry above.** `st_prof_hot_dump()` fires every 16
presents — and the boot IS 16 presents. The window that produced "200 rows
BOTH, mismatch at x=0, 11 rebands in 16 presents" was therefore window #1, the
boot, with the drive's own presents landing in a later window that was never
dumped. The convwhy conclusion ("ownership is thrown away by rebands") is true
OF BOOT. The steady state it was taken to describe measures zero here.

The ~47% figure quoted in CLAUDE.md carries the same caveat: it is dominated by
transitions, not by the play loop. Nobody has yet measured the walk itself with
this instrument, because the walk is unreachable headlessly right now (see
below).

### #90 CLOSED 2026-08-06 — THE WALK IS MEASURED, ON BOTH BITPLANE TARGETS

The measurement this section asked for ("nobody has yet measured the walk
itself") is done. It needed the INSTRUMENT fixed first, which is why it stayed
open: `pdpf l67ca tk` sampled `TickCount()` at LOG time, after three
`dbg_log_num` calls that are each an Fopen + Fseek + 3x Fwrite + Fclose through
GEMDOS. It was timing the logger. The tell was the value — **222 ticks on 40 of
43 samples** — and the contradiction that exposed it was one bracket claiming
3.7 s inside a step the wall clock timed at 2.4 s. Structurally, J76_T/L67_T log
inline, so ANY outer bracket spanning them measures their logging too; the
per-piece detail is now opt-in behind `-DFRUA_STEPPROF_DEEP` and plain
`-DFRUA_STEPPROF` gives clean outer totals.

Twelve walk actions on `WALKTEST.DSN`, 24 redraw samples each, ticks are 60 Hz
on both targets (Atari `_hz_200 * 3/10`; Amiga `vbl * 6/5` on PAL). Both took
the same `jt312 RECT path (viewport-only)`, both with a seated party.

| target | render | present | total | ms/redraw | ms/action | present share |
|---|--:|--:|--:|--:|--:|--:|
| Atari STE 8 MHz (4 planes, 16 col) | 17 | 18 | **35** | 583 | **~1170** | 51% |
| Amiga ECS 7 MHz (5 planes, 32 col) | 11 | 22 | **34** | 567 | **~1130** | 65% |

**They are the same speed.** The expectation going in was that ECS would be
clearly worse — slower CPU, more bitplanes — and it is not; the totals differ by
one tick, inside the sample spread (STE 34-42, ECS 31-37). What differs is the
SPLIT: ECS renders faster (11 vs 17) and presents slower (22 vs 18).

**Present dominates on both** — 51% on the STE, 65% on ECS. That is the lever,
not the renderer, and it is where the next work belongs. Two redraws are issued
per walk action on both targets; halving that would be worth as much as any
present optimisation.

HYPOTHESIS, not measured: ECS renders cheaper because 32 colours needs less
quantisation than the STE's 16 (cf. #121, `qd_nearest_color` at 26-34%). Do not
build on it without an A/B.

Two traps for the next run. The play screen ANIMATES (torch/fire), so a
"wait for two identical frames" settle detector NEVER fires and silently
reports its poll limit as the step time — it reported 26 s on 3 of 4 samples
here. And fast-forward is safe for TICK measurements (the emulated timer tracks
emulated time) but not for wall-clock ones; mixing the two is what surfaced the
instrument bug, so it is worth keeping both instruments and comparing them.

**Consequence for #63.** Reband work is boot/transition work. If boot time is
the target it is worth attacking (deferring the quant on near-blank frames
would remove ~25% of them); if the 8 MHz PLAY loop is the target, the evidence
says rebands are not where the time goes, and the untouched lever is the one
the 2026-07-19 plan named — `st_c2p_span` throughput — plus an actual
measurement of the dungeon walk once it is drivable again.

**Blocked on #91.** Reaching the dungeon headlessly on STE needs
`-DFRUA_AUTOPLAY` (external keys drop into the roster modal). That flag now
wedges the boot 100% of the time — not because of anything autoplay does, but
because it adds BSS to `platform/input.c`, which is enough to make the #91
boot hang deterministic. The play-loop measurement waits on that fix.

### #90 COMPOSITE INTERNALS — MEASURED 2026-08-08 (Mega STe, HEIRS slot I walk)

The present dominates the walk (51% above); this breaks the viewport composite
itself down. Driven headlessly by autoloading a first-person save (autoload.dat
='I') straight into the corridor, walking with injected arrows, reading the
`b63play` census (`-DFRUA_STPROF`). A span census plus two stub A/Bs on a
STEADY 8-composite scene (`tex32 ≈ 2590`, `col8 = 4224`, ~239 t200):

| part of the composite | t200 (8-comp) | share | isolated by |
|---|--:|--:|---|
| block compute (`c2p4st_32`, mostly textured walls) | ~136 | 57% | `239 − block_store` |
| **edge columns** (`st_c2p8`, 528 calls/comp) | ~102 | **43%** | NOCOL8 stub (`137` w/o edges) |
| block **video stores** (aligned words) | **~1** | ~0% | DUMMYSTORE stub (block c2p → RAM = `238`) |

Static structure per composite (both pages): `352` 32-blocks + `528` 8px edge
columns; the 32-block split is **~8% flat / ~92% textured** — the floor is a
tiled stone pattern and the ceiling a starfield, so `c2p4st_is_flat` almost
never fires and the fills composite as texture.

**Consequences (each measured, not argued):**

1. **ADR-0016 Stage A (draw-time plane-stamp the trapezoid FILLS) is not worth
   doing.** The fills are ~8% of blocks and ~1% of cost (the flat-detect
   already makes them ~free). Skipped.
2. **The edges (43%) are overhead-bound, not compute-bound.** Routing them
   through the word-parallel `c2p4st_8` (vs the scalar per-bit scatter) dropped
   the composite only ~4% — the cost is the 528 calls + strided single-byte
   video stores, not the arithmetic. Landed anyway (byte-identical, cleaner);
   the real edge lever is ELIMINATING the columns by aligning the composite
   (render/​composite a 16-aligned x=16..112 span incl. the static border), not
   converting them faster.
3. **Video stores are ~free** (block store ≈ 1 t200; aligned word writes, the
   shifter is not stealing much at 4-plane ST-Low). So **Stage C is not
   store-capped** — a pre-computed plane blit writes to the pages for ~nothing.
   BUT for TEXTURED walls the c2p transpose does not vanish under draw-time
   planar, it MOVES into the 3D renderer (stamping 4 plane bits per varying
   pixel is the same work earlier). Stage C's net win is the intermediate
   chunky buffer + read-back it removes, not the 57% block-compute figure.

Next levers, cheapest first: (a) align the composite to kill the 43% edge
overhead; (b) halve the two-redraws-per-action (named above); (c) Stage C
draw-time planar walls, net-win-bounded per (3).

**LANDED (a) 2026-08-08 — composite 249 -> 195 t200, −22%, pixel-identical.**
`st_vp_composite_fast` now converts a 16-aligned x=16..112 span (three
`c2p4st_32` blocks/row, `col8 = 0`); the ≤15px border strips the alignment adds
are seeded into the scratch from `s_chunky` (static frame chrome), so the result
is byte-identical — the resume frame md5-matched the pre-alignment build whole
(status bar included), game-area AE 0 px. This is the edge-column 43% converted
from scalar overhead to a fast aligned block, not removed work: block count
went 352 -> 528/present but the 528 scalar `st_c2p8` calls disappeared. Commit
c7f88aa6; the earlier `c2p4st_8` (2b9fac49) is now used only by the exotic
trailing-16px fallback the live viewport never hits. Remaining: (b) the two
redraws per action, then (c) Stage C.

**LANDED (b) 2026-08-08 — the two-redraws lever, and it is the BIG one: renders
AND presents HALVE per action.** The play walk rendered the view twice per step:
`l1908` (inside `jt297`) draws the new cell, then `l63c0` snaps the view cell
(#124) and re-renders it at 18357 — the first overpainted (l1908's own comment
admits it). `jt297` restores the view cell after l1908, so l63c0's snap+render
is the one that shows the step; l1908's is pure waste. Suppress it on the play
walk via `g_walk_render_deferred` (set across l63c0's movement dispatch,
`a_deep && !editor`); safe because movement never sets `exitflag` on a_deep, so
l63c0 always re-renders. A/B via `FRUA_NO2REDRAW`, 24 turns on a real Mega STe:
**renders 47 -> 23, rect presents 48 -> 24 — a clean 2:1.** Both halves of the
redraw (the ~17-tick render AND the ~18-tick present, per the #90 table) drop
once per rendering action; moves that bonk a wall render zero either way. Resume
frame md5-identical, turned frame correct. Commit ee06d2f9. This is a bigger win
than the composite work — it removes a whole redraw, not a slice of one.
Remaining: (c) Stage C draw-time planar walls, net-win-bounded per (3) above.

### #91 — THE SHIPPING ST/STE PLANAR BUILD WEDGES AT BOOT (found 2026-07-26)

Found while trying to drive the #90 soak. `make CPU68K=68000` — the default,
the one `release-ste` ships — hangs at the title screen before the menu on a
large fraction of boots.

| build | deadline | result |
|---|--:|---|
| shipping planar | 200 s | **2 of 5 wedged** (successes take 17–19 s; failures ran the full 202 s with the log frozen) |
| shipping chunky (`PLANAR=0`) | 90 s | 0 of 6 |
| planar + one BSS `int` in `input.c` | 150 s | **4 of 4 wedged** (two different source placements) |
| chunky + the same BSS `int` | 120 s | booted |
| planar + a 4 KB `const` array (TEXT, not BSS) | 150 s | booted |
| planar + the BSS `int` in `dbglog.c` instead | 150 s | booted |

So it is **layout-sensitive** (a single unused `volatile int` in one specific
translation unit's BSS turns a ~40% hang into a 100% hang; the same int
elsewhere, or 4 KB of TEXT, does nothing), **timing-sensitive** (it is
intermittent without the perturbation), and it **requires `FRUA_PLANAR`**.

When wedged: the title screen is up, timer/VBL interrupts still fire (a
`--trace cpu_exception` run shows only autovectors 28 and 30 — **no bus or
address error**), and neither an injected key nor a mouse click advances it.
The last log line is always the churn dump of reband #6, the title-screen
re-quantise.

Two suspects worth checking first: the per-band **Timer-B palette interrupt**
racing `st_reband()` (which rewrites the band tables the handler reads), and
the `Supexec`-from-interrupt hazard that `g_plat_in_super` exists to guard in
`platform/input.c` — the file whose BSS layout flips this from intermittent to
certain. It may well be the same root cause as **#61**.

A caveat worth stating: an intermittent real-speed measurement (2 of 2 "wedged"
at a 240 s deadline) was **retracted** — a good real-speed planar boot takes
232 s, so that deadline was measuring itself. The numbers in the table above all
come from runs where a success is an order of magnitude faster than the
deadline.

#### #91 ROOT CAUSE AND FIX — 2026-07-26. It was a Timer-B / VBL deadlock.

**Isolating it needed a RUNTIME switch, not a build flag.** Every compile-time
probe moved the goalposts: the bug is layout-sensitive, so `-DFRUA_STPROF`,
`ENGINE_PROBE=1`, a bounded spin, even a `--trace gemdos` run all changed the
odds, and several masked it entirely. The experiment that settled it reads a
marker file at `st_init` and clears `s_use_blt` if present, so both arms are the
**same binary** and layout is held constant:

| arm (identical binary) | boots | wedged |
|---|--:|--:|
| BLiTTER on | 10 | **7** |
| BLiTTER off | 10 | **0** |

**The deadlock.** `st_vbl_handler` re-phases Timer B by stopping it
(`TBCR = 0`), reloading, and restarting. It runs at IPL 4, so the level-6
Timer-B ISR can preempt it — and that ISR spins on TBDR waiting for the display
line to end:

    1: cmpib #20,0xFFFFFA21
       jeq   1b

TBDR only moves while the timer is COUNTING. Preempt the VBL inside its
stopped-timer window and the ISR waits forever for a register that cannot
change, at an IPL that blocks the only code that could restart it. The machine
looks alive from outside — interrupts were serviced right up to the deadlock,
so a `--trace cpu_exception` run shows nothing but ordinary autovectors, no bus
error — and the title screen just sits there.

**Why #48 made it fire.** A force-full seeds two pages: 2 x 32000 plane bytes +
2 x 64000 shadow bytes, in HOG mode, in one `Supexec`. That is ~24 ms during
which the CPU cannot service any interrupt. The VBL and a Timer-B request then
come due together and land in the window. The window is only ~10 cycles wide,
which is why the pre-#48 build hit it rarely enough to look fine.

**The fix** (both parts, `platform/display_ste.c`):

1. `st_vbl_handler` masks Timer B in **IERA bit 0** (Compendium B.38 p.758 —
   Timer B is MFP channel 8, so it lives in the *A* register set, not B) across
   the re-phase, clears anything latched via IPRA, then re-enables. Disabling an
   MFP channel also clears its pending bit, so the cost is at most one band's
   palette arriving a frame late.
2. The ISR spin gains a **liveness test** rather than a counter: if TBCR is 0
   the timer is stopped and TBDR is frozen, so fall through and store the
   palette a line early. Cosmetic band offset instead of a hang. This should now
   be unreachable; it stays because an unbounded spin inside an interrupt
   handler is a landmine, and teardown stops the timer too.

**Verification.** Shipping config 10/10 clean. With the BSS amplifier that
previously wedged 4 of 4 boots, 8/8 clean. Menu renders correctly (bands
intact). ST chunky 2/2. 410 tests pass; falcon / TT-FPU / ST-chunky / Amiga AGA
/ Amiga ECS all build.

**Left for #61.** The 24 ms bus hog is still there — the fix makes it harmless
rather than absent. Interrupts starved for 1.4 frames means band palettes land
late on every transition, which is a strong candidate for the "occasional STe
redraw glitch". Chunking each `st_blt_copy` into ~1 KB pieces would bound the
starvation for well under 1% of the copy cost (about 15 register writes per
chunk against 512 word moves). Not done here: it is a different symptom, and
folding it into the hang fix would muddy the attribution.

### #61 BLITTER BUS-HOG — FIXED THE STARVATION, DID NOT FIND THE GLITCH (2026-07-26)

Following the #91 lead: #48's HOG-mode blits halt the CPU for the whole
transfer, so a force-full (2 pages x 32000 plane + 64000 shadow bytes) blocks
every interrupt for ~24 ms. On a display whose 16 colours come from a per-band
Timer-B palette split, that should wreck the bands. It does — and it turns out
not to matter visually.

**Measuring it.** Added an STPROF counter (`b61`): the Timer-B ISR increments a
per-frame fire count, and the VBL checks it against ST_NBANDS before resetting.
A frame short of fires rendered its lower bands with a stale palette. Both arms
of the blitter A/B ran from ONE binary (marker file at `st_init`), per the #91
lesson.

| arm | starved frames | band fires lost | worst frame | real-speed boot |
|---|--:|--:|--:|--:|
| blitter, unbounded HOG (pre-#61) | 107 | 448 | **0 of 10 bands** | 371 s |
| blitter, 2048-word chunks | 10 | 10 | 9 of 10 | 360 s |
| **blitter, 512-word chunks** | **0** | **0** | — | **356 s** |
| no blitter (memcpy reference) | 0 | 0 | — | — |

So the starvation was real and total — at least one frame during every boot got
NO band switches at all. Chunking each blit to 512 words removes it completely,
matching the memcpy reference, and is if anything FASTER end to end (356 s vs
371 s): the CPU is halted for the whole of a HOG blit anyway, and the per-chunk
cost is ~4 register writes per 512 word moves. Shipped unconditionally.

**But it is not the #61 glitch.** Recorded the whole boot to AVI (22310 frames)
and looked for the flash: **zero single-frame transients** — no frame differs
from neighbours that agree with each other. Only 32 frames in the entire boot
differ from their predecessor at all, so the detector had nothing to hide.

The reason is more interesting than the search. Disabling the per-band palette
split ENTIRELY (an early `rte` in the Timer-B ISR — verified in the disassembly,
not just the source) leaves:

- the **title screen pixel-identical**, 0 of 489216 pixels different;
- the **menu** different by 0.2%, which is the mouse cursor having moved.

**On these screens the raster split is doing nothing at all** — the quantiser's
per-band reduce lands on the same 16 colours for all ten bands. A frame that
misses its band interrupts is therefore indistinguishable from a correct one,
and cannot be the artefact the user reported.

**What this does and does not settle.** The starvation is fixed and a 24 ms
interrupt blackout — which also stalls sound DMA, the keyboard ACIA and every
timer, and is what made #91's deadlock fire — is gone. #61 itself stays OPEN:
this lead is closed, so nobody re-derives it. The screens that could not be
tested are exactly the ones the split was built for (#40's banding work): the
dungeon view, combat and BIGPIC art, all still unreachable headlessly on STE.
If the glitch is a band artefact at all, it will be there.

**Spin-off for #63.** If all ten band palettes are identical — which they are on
every screen measured here — the Timer-B handler is pure overhead: ~500 ISRs a
second, each saving eight registers and then SPINNING to the end of a display
line. Detecting an all-identical band set at re-band time and simply not arming
Timer B would buy that back on menu-heavy screens for a few lines of code.
Nobody has measured what it costs, which is the first step.

### #92 THE STE DUNGEON IS DRIVABLE — and the first walk frame caught a bug

Two things were blocking it, neither of them the engine.

**1. The driver drops fast-forward at the menu.** `hatari_ui.sh start` sends
`hatari-option --fast-forward no` once it sees `menu: modal up`. Autoplay's
schedule is in emulated ticks, and at real 8 MHz each screen takes ~40 s to
build, so a 24-key script needs ~15 minutes and looks exactly like a stall. It
had been read as "autoplay stops firing after key 5". Send
`hatari-option --fast-forward yes` back down the fifo after `start` and the same
script finishes in ~90 s. Drop it again before screenshotting.

**2. HEIRS opens a modal chain on its entry cell**, which eats movement keys —
the trap `tools/mk_walktest_design.py` was written for. Point the engine at
WALKTEST.DSN (`--current`) and the walk keys reach the walk code.

Recipe, start to walk frame:

    python3 tools/mk_walktest_design.py data/work/gamedata --current
    make CPU68K=68000 EXTRA_CFLAGS="-DFRUA_AUTOPLAY -DFRUA_AUTOWALK"
    env -u DISPLAY HATARI_ARGS="--machine ste " driver.sh start
    echo "hatari-option --fast-forward yes" > /tmp/frua-ui/cmd.fifo
    #   poll conout.log until "autoplay: send key" hits 24  (~90 s)
    echo "hatari-option --fast-forward no"  > /tmp/frua-ui/cmd.fifo
    driver.sh shots walk.png

Verified by STATE, not key count: the clock reads **12:06 AM**, six minutes for
the script's six Up steps.

**What the first frame showed: the walk screen was CORRUPT** — red bars across
it, a roster name painted over the viewport, no command bar, content squashed
into the top half, and completely static frame to frame. Three runs of the same
drive localised it, each changing one thing:

| build (same design, same 24 keys) | walk frame |
|---|---|
| `PLANAR=0` (chunky) | correct — the reference |
| planar, blitter forced off | **pixel-identical to the reference** |
| planar, blitter on (shipping default) | **27.2% of pixels differ** |
| planar, blitter on, UNCHUNKED (0.5.7 behaviour) | **pixel-identical to the reference** |

So: not the draw-time path, and not #48 — **the chunking added hours earlier in
3fc0a8cb**. `BLT_YCOUNT = 1` had been hoisted out of the chunk loop, and the
blitter DECREMENTS Y count as it runs, so it reads 0 after a completed blit and
every chunk after the first ran with YCOUNT = 0. One word write per chunk, moved
back inside the loop.

**0.5.7-beta is unaffected** — the bug only ever existed on `main`, between
3fc0a8cb and its fix.

Worth noting how narrowly it escaped: the earlier #61 verification (6/6 clean
boots, menu pixel-compared, 0 starved frames, 410 tests) all passed on the
broken build. Every one of those checks looked at the boot and menu, and the
menu's copies are small enough to fit in a single 512-word chunk. The bug lived
entirely in copies big enough to need a second chunk, which is the walk screen —
the one frame no ST harness had ever rendered.

### #61 ON THE PLAY SCREENS — band theory fully closed, glitch still not found

The STE dungeon became reachable (#92), so the screens #61's band-artefact
theory pointed at could finally be photographed. Repeating the bands-ON vs
bands-OFF comparison there (an early `rte` in the Timer-B ISR, verified in the
disassembly):

| screen | pixels differing, band split ON vs OFF |
|---|--:|
| title art | 0.0% |
| main menu | 0.2% (the mouse cursor moved) |
| **dungeon walk** | chrome **0.0%** — command bar, roster and marble frame all identical; only viewport and clock differ, and those differ by one step of game state |
| **BIGPIC / treasure art** | **0.0%** |

**The per-band palette split makes no pixel difference on ANY screen we can
reach**, including the colourful BIGPIC art it was presumably built for. The
band-artefact theory for #61 is closed, not merely unsupported: the mechanism
cannot produce a visible change here even when disabled outright.

That hands #63 a concrete lever — see its entry. ~500 ISRs a second, each
saving eight registers and spinning to the end of a display line, currently buy
nothing measurable.

**Transient sweeps found nothing, but the sample is weak and it is worth being
precise about why.** Two play recordings (13549 and 13986 frames) yielded ZERO
single-frame transients — but also only FOUR changed frames each, all of
magnitude 1, which were the clock digit. The walk was real (12:00 → 12:06) and
the viewport still never changed, because **WALKTEST is a bare square chamber
viewed from its centre**: every facing is identical by symmetry, and a plain
wall four cells away looks much like one three cells away. You cannot see a
glitch in a picture that never differs.

**Two attempts to give it geometry, both inconclusive:**

1. Pillars in the interior corners — no effect. The first-person view is a
   narrow forward cone and a block three cells off-axis never enters it.
2. Pillars FLANKING the walk axes (`--pillars`, now the shipped variant) — the
   viewport differs 5.6% from the bare room, but that is exactly the figure two
   runs ending one step apart also produce, so the comparison is confounded by
   position and **the pillars' effect is UNPROVEN**. Stated plainly rather than
   claimed.

Real modules were tried too and are a separate problem: BEOWOLF and GIANTS both
open with a multi-screen story chain, and at 8 MHz the typewriter runs ~1.4 s a
character, so clearing the intro takes far more than the six Returns the script
sends. `-DFRUA_AUTOWALK_LONGINTRO` adds ten more (both input layers — Atari and
Amiga have SEPARATE arrays); BEOWOLF was still mid-intro after them.

**The right next step is to stop guessing at generated geometry** and author a
room whose walls the game itself renders — build it in the module editor
(unblocked by #88) and export it as the walk-soak area. That doubles as the
first end-to-end exercise of the editor's map UI.


### CORRECTION — the engine was never broken; use HEIRS + the jump points

Recorded because I got this wrong and it cost a session's worth of dead ends.

I concluded from a `geo.py`-authored WALKTEST room that "the play viewport does
not reflect party position". **False.** Driven against HEIRS through the
`FRUA_ENTRY_LEVEL/COL/ROW/FACING` jump points — which exist exactly to drop the
party into a real module's dungeon past its entry-event chain — the Falcon build
of the same commit gives:

    entry vs after two steps: 28.6% of the VIEWPORT differs
    visually: a corridor receding into darkness -> a close-up brick wall

Position tracking, wall rendering and the walk all work, on the current build.

**The error was methodological**, and worth naming: I tested with a synthetic
room I had authored and modified twice in the same hour, and never once with a
known-good module, then blamed the engine. A harness that cannot reproduce a
known-good case is not evidence about the engine.

**The recipe for any play-screen work is therefore:**

    make CPU68K=68000 EXTRA_CFLAGS="-DFRUA_AUTOPLAY -DFRUA_AUTOWALK \
        -DFRUA_ENTRY_LEVEL=5 -DFRUA_ENTRY_COL=1 -DFRUA_ENTRY_ROW=6 \
        -DFRUA_ENTRY_FACING=0"

with HEIRS as the current design. No intro chain to clear, no synthetic
geometry, and the walk starts in real dungeon art. Pick the start cell by
reading the GEO with `tools/geo.py` first — the cell used above has three clear
cells north of it, and even then the autowalk script's turns walked the party
into a dead end after three steps, so a longer straight run is worth choosing
deliberately.

What remains genuinely wrong is narrower and is a TOOLING gap, not an engine
one: generated areas still do not render their walls in play (see task #94).

**Two conventions, measured, because I had both wrong:**

- **The HUD readout is `row,col`** — as geo-format.md always said. Asked for
  `ROW=6 COL=1`, the HUD reads `6,1`. An earlier claim in this document's
  history that it prints col,row was wrong and is retracted.
- **`FRUA_ENTRY_FACING=0` walks WEST**, not north. HEIRS `6,1 -> 6,0` (one step
  west, then stopped at column 0, the map edge); WALKTEST `5,5 -> 5,3 -> 5,1`
  (six Ups netting four cells west, row unchanged). **This is why every walk in
  this session was one or two steps** — the party was heading for the nearest
  west edge, not along the corridor I thought I had built. The facing encoding
  does not match the `0=N 2=E 4=S 6=W` reading of the passage-event field;
  HEIRS' own entry is `row=10 col=8 facing=2` and that start faces EAST, so
  `2 = E` at least. Run a four-value sweep (`FACING=0/2/4/6`, one step, read the
  HUD delta) before authoring any more geometry against it.

And note the entry cell itself is the caravan (`special=2`), so landing exactly
on `10,8` fires the chain the jump points exist to skip — land PAST it.

### #63 THE RASTER SPLIT COSTS 14% OF THE CPU AND HAS BOUGHT NOTHING SINCE B1

The #61 write-up above closed the band-artefact theory by disabling the split
and finding **no pixel changed on any reachable screen** — title, menu, dungeon
walk, BIGPIC. That was recorded as a spin-off lever for #63. Measured, and it
is the biggest single lever anyone has found on this target.

**Why the split is idle is structural, not situational.** `st_build_hw_palette`
opens with

    for (b = 1; b < ST_NBANDS; b++)
            memcpy(s_band_pal + b * ST_NCOL * 3, s_band_pal, ST_NCOL * 3);

— it REPLICATES band 0 to every band, and `st_reband` calls the quantizer with
`nbands = 1`. That is Strategy B (B1/B4 Phase-0) doing exactly what it was
designed to do: one scene-stable palette for the whole frame, which is what
makes the remap stable enough for draw-time writers. The per-band machinery
from #40 survived it as dead weight. So the bands are not "identical on the
screens we happened to test" — they are identical **by construction**, and
Timer B spends every frame loading ten times the colours the VBL already
loaded.

**The cost, measured A/B/A inside ONE boot** (`st_prof_tbcost`, STPROF only).
Sixteen full-frame c2p passes per arm, 200 Hz ticks, the arming forced through
`s_tb_force` and applied by the VBL:

| arm | ticks | band fires |
|---|--:|--:|
| split ON  #1 | 4513 | 13550 |
| split OFF    | **3869** | **0** |
| split ON  #2 | 4511 | 13540 |

- The two ON arms agree to **0.04%**, so order/warm-up is not the effect.
- The OFF arm fires **zero** times — the disarm is total, not partial.
- Tax = **16.6% of the c2p**, i.e. 19,010 cycles of every 133,333-cycle frame,
  **14.3% of the whole machine**.
- **1901 cycles per band interrupt.** A handler that does interrupt entry, two
  movems and one line of spin should cost ~600. Recorded because it matters
  IF the split ever returns: the extra ~1300 is the TBDR spin running long, and
  that is a bug in the handler rather than a cost of banding. Not chased — the
  handler is now disarmed.

**End-to-end, real speed, shipping binaries, HEIRS:** boot-to-menu **231 s ->
200 s**, 13.4% faster. Two independent measurements (one micro, one macro) that
agree to within a point. For scale, #48's blitter work — a much larger change —
was 3.4%.

**Shipped as a runtime check, not an #ifdef.** `st_build_hw_palette` compares
the encoded `st_band_stpal` rows; all equal sets `s_tb_uniform` and the VBL
stops arming Timer B (once, via `s_tb_live`, from supervisor code that already
owns the hardware). Restore a per-band quantizer and the rows stop matching,
the flag clears and the split comes back on its own. The alternative — deleting
the handler, or gating it on a build flag — would have to be manually undone by
whoever revives #40's per-band work, and would be silently wrong until they
noticed.

**Verification.** The walk frame — the frame that caught the YCOUNT bug when
6/6 clean boots and a pixel-compared menu had missed it — is **pixel-identical**
across the change, 0 of 489216 pixels, on the same 24-key drive. Menu renders
correctly. 412 host tests pass. Both the 68000 and the default 020 builds link.

**Side effect worth recording: this retires the #91 hazard class in the
shipping build.** #91 was the Timer-B ISR preempting the VBL's re-phase window
and deadlocking on a stopped timer. With the split disarmed there is no Timer-B
ISR to preempt anything. The fix stays — it is still load-bearing the moment
per-band palettes return — but the shipping configuration no longer contains
the race at all.

**What this does NOT do.** It does not touch the actual play loop, which is
still unmeasured (the walk harness has the facing bug described above). The
14% is machine-wide, so the play loop gets it too, but the *distribution* of
where play-loop time goes remains unknown. The 2026-07-19 plan's other named
lever — `st_c2p_span` throughput — is still untouched.

### #63 THE DUNGEON WALK, MEASURED AT LAST — and it was the viewport composite

With the raster split gone the next lever needed a number nobody had: what does
the PLAY loop cost? Every profile on this target had measured the boot or a menu,
for a structural reason — `st_prof_hot_dump`'s window counts FULL presents, and
a dungeon walk issues RECT presents, so the walk fell through every instrument
ever written here. `b63play` (STPROF only) counts the two things the walk
actually runs, in 200 Hz ticks, and dumps every 8 rect presents so a 24-key
scripted drive reports three windows instead of none.

**The walk's display cost, WALKTEST, 8 composites per window:**

| phase | per composite | share of composite |
|---|--:|--:|
| chunky -> separate planes, bit at a time | 1.03 s | 22% |
| `planar_blit_stlow` x2 pages, bit per pixel per plane | **3.72 s** | **78%** |
| total | 4.75 s | **31-36% of the walk step** |

Four point seven five seconds of emulated time, per step, to move an 88x88
viewport. `planar_blit_stlow` alone was **~1900 cycles per pixel** — it walks
pixels, recomputes a shift mask and a destination pointer for each one, and does
a byte read-modify-write per plane, with a bounds test per pixel, twice (once
per page). The optimized c2p in the same file runs at 151 cycles/pixel.

**The fix is that the viewport is 8-PIXEL ALIGNED.** It sits at x=24, 88 wide,
and an ST-Low 16-pixel group is four words whose high/low bytes are pixels 0-7
and 8-15. So every destination BYTE belongs wholly to the viewport: no masking,
no read-modify-write, no separate-plane intermediate. `st_vp_composite_fast`
converts chunky straight to interleaved planes — `c2p4st_32` (flat-span path
included) for the 32-pixel bulk, a small `st_c2p8` for the ragged 8-pixel
columns at each edge.

**Result, same drive, same content:**

| | before | after |
|---|--:|--:|
| per composite | 951 t200 (4.75 s) | **152 t200 (0.76 s)** |
| display share of the walk | 31-36% | **8-10%** |
| walk wall clock (3 windows, 24 rect presents) | 63730 t200 | **44722 t200** |

**~30% off the dungeon walk**, on top of the 13.4% from the raster split.

**The measurement validates itself.** Window by window, the drop in wall time
matches the drop in composite time to within 0.5% — 6386/6355, 6386/6334,
6392/6319. Nothing else moved, which is what you want from a single-variable
change, and it is why the absolute numbers can be trusted despite looking
implausibly large at first sight. (They are not implausible: `stprof b30b`
independently puts a FULL-frame c2p at 1.21 s on this machine. An 8 MHz 68000
really is that slow, which is the whole content of #63.)

**Verification: the walk frame is pixel-identical**, 0 of 489216, on the same
24-key drive — and that frame exercises all three sub-paths (lead-in `st_c2p8`,
two `c2p4st_32` blocks, two trailing `st_c2p8`) over real textured dungeon art,
so the byte layout of each is checked against the implementation it replaces.
412 host tests pass; the 68000 and 020 builds both link.

**Scope.** ST/STE only — `planar_viewport_register` has exactly one caller and
the Amiga backends never register a viewport hook, so neither ECS nor AGA was
paying this. `planar_blit_stlow` keeps its general per-pixel body for the
unaligned fallback, which nothing issues today.

**Still on the table for #63**, in rough order of size:

1. **The walk is now ~90% ENGINE.** The display layer is down to 8-10% of a
   step, so the remaining ceiling is the engine's own 3D render. That is where
   the next measurement has to go, and it is not a c2p problem.
2. Convert once and copy the bytes to the second page instead of converting
   twice — halves what is left of the composite (~0.4 s/step, ~4% of a step).
3. `st_c2p8` still handles 24 of the viewport's 88 columns; a 16-pixel variant
   would fold two of them into the fast transpose.
4. `st_c2p_span` throughput itself — the 2026-07-19 lever, still untouched, and
   now the thing behind every remaining conversion.

### THE LONG-INTRO DRIVE ON HEIRS — and what real geometry does to the #63 numbers

`FRUA_AUTOWALK_LONGINTRO` did not work on HEIRS, and failed in the way this
harness always fails: **reporting success**. The drive fired all 34 keys, the
log read 34 of 34, and the party never left the entry cell — because HEIRS'
entry chain does not end on a message. It ends on the **TREASURE screen**
(`VIEW TAKE POOL SHARE EXIT`), which ignores Return entirely, so all sixteen
Returns and then all eighteen walk keys went into a modal that ate them.

Driven by hand on a live STE, the real chain is:

    ~6-16 Return   BIGPIC story messages
    e              EXIT the treasure screen
    n              NO to "THERE IS STILL TREASURE LEFT. DO YOU WANT TO GO
                   BACK AND CLAIM IT?"
    ~10 Return     the caravan-farewell messages
    -> walk command bar at 10,8, 12:00 AM

That is now `FRUA_AUTOWALK_TREASURE`, in BOTH `platform/input.c` and
`platform/amiga/input_amiga.c` (separate `g_ap[]` arrays — editing one fires
nothing on the other machine, which has cost a soak before). It is a SEPARATE
flag from LONGINTRO on purpose: **`e` is ENCAMP on the walk command bar**, so
firing it at a module with no treasure screen opens the camp screen and the
walk samples that instead.

Verified fully headless: 46 keys, ending at **11,7 12:04 AM** — four steps
walked through the tavern, viewport changing at every one, with a real event
firing when the party walked into a guest room ("GED OUD!").

**And a diagnostic bonus: the viewport genuinely CHANGES now.** Consecutive
steps 10,8 -> 11,8 -> 12,8 gave 8.5%, 85.9% and 87.9% frame differences (the
large ones are scene rebands). That is the thing #61's soak never had — every
previous walk sampled WALKTEST, a bare symmetric chamber whose viewport cannot
differ. A transient hunt is now worth running.

**★ THE CORRECTION THIS FORCES ON THE #63 COMPOSITE ENTRY ABOVE.**

The composite's own cost reproduces exactly on real geometry — **150.7 t200 per
composite on HEIRS vs 152 on WALKTEST**, as it should, since it converts a fixed
88x88 rect whatever is in it. But its SHARE of the play loop does not:

| | WALKTEST (bare, event-free) | HEIRS (real module) |
|---|--:|--:|
| viewport composite, share of wall | 31-36% | **0.1-0.6%** |
| FULL presents in the run | ~0 | **1008** |
| time inside the full-present path | — | **32.5% of all wall time** |

WALKTEST is a room where nothing happens, so the viewport update IS the frame.
A real module spends its time somewhere else entirely: 1008 full presents at
~1.6 s each, plus BIGPIC art, plus 47 rebands. The composite rewrite is still
real and still worth having — it is content-independent and 6.25x — but on a
module anyone would actually play it buys well under a percent, not a third.
Stated plainly because the earlier entry, read alone, oversells it.

**So #63's real target is now named and measured: the FULL PRESENT path, 32.5%
of play, ~1008 presents at ~1.6 s each.** Consistent across three windows
(95880/293935, 97053/297896, 98227/301859 ticks). Worth knowing before anyone
starts: a full-frame c2p is 1.21 s, so a present costing 1.6 s looks like it is
paying nearly a whole conversion — even though #90 measured post-menu presents
converting ZERO rows. Those two facts do not fit together yet, and reconciling
them is the next piece of work, not a foregone conclusion.

### RECONCILED: a present costs 1.6 s with ZERO converted rows because it MEMCMPs 64 KB to find out nothing moved

The HEIRS drive left two facts that could not both be true: full presents were
32.5% of play at ~1.6 s each, and #90 had measured post-menu presents converting
ZERO rows — with a whole-frame c2p costing only 1.21 s. Phase-timed the present
(`b63pr`, STPROF), 960 presents:

| phase | t200 | share of in-present |
|---|--:|--:|
| **pass 1 — the 200-row diff** | **240834** | **76%** |
| reband / repalette branch | 35945 | 11% |
| run copies (blitter) | 1534 | 0.5% |
| viewport composite | 608 | 0.2% |
| rows CHANGED | 9827 | 10.2 per present |
| rows CONVERTED | **776** | **0.8 per present** |

**#90 was right and nothing contradicts it.** The present converts essentially
nothing. It spends its life running `memcmp` over all 64000 bytes of the chunky
surface against the page shadow to discover that about ten rows moved.

**And `memcmp` is the wrong primitive on this target.** Timed back to back in
one boot, 16 sweeps of the real 64000-byte surface, row by row exactly as pass 1
does it:

| | t200 | cycles/byte |
|---|--:|--:|
| `memcmp` | 2395 | **93** |
| the same compare as a `long`-wise C loop | **776** | **30** |

3.09x, for a ten-line function. (An earlier version of this log line divided by
64 instead of 640 and printed a nonsense 938 cycles/byte — the 93 is the
corrected figure.)

**Shipped as `st_row_differs`**, used by both row comparisons (pass 1's diff and
`st_dt_ready_row`'s stamp check). Measured A/B on the identical HEIRS drive with
identical instrumentation, the two arms one `FRUA_ROWDIFF_MEMCMP` flag apart:

| | memcmp arm | long-compare arm |
|---|--:|--:|
| per present | 322.0 t200 (1.61 s) | **167.7 t200 (0.84 s)** |
| pass 1 per present | 250.4 | **90.4** |
| rows converted | 776 | 776 |

**The full present is HALVED**, and in-present fell from 32.5% to 19.1% of the
run's wall clock — worth ~13% of total play time on a real module. `rows
converted` identical in both arms, which is the semantic check: same answer,
a third of the time.

**Verification.** The WALKTEST walk frame — the byte-exact reference that caught
the YCOUNT bug — is **pixel-identical, 0 of 489216**, with the change in, on the
shipping flag set. 412 host tests pass; 68000 and 020 both link.

**One honest loose end, chased and cleared.** The HEIRS end frame in the
phase-timed runs shows a BLANK viewport where an earlier run showed guest-room
art, which looked like a rendering regression. It is not the row-diff change:
the memcmp arm and the long-compare arm produce **pixel-identical frames**
(0 differing), so both instrumented builds do it and the plain build does not.
It is the phase timers' ~10 extra Supexec per present perturbing a
timing-sensitive path — the same sensitivity #91 documented. STPROF-only; it
does not exist in anything that ships. Recorded rather than swept up because
the next person to phase-time this will see it too.

**Where #63 stands after this.** The remaining big item is to stop scanning at
all: the writers already know which rows they touched (`s_dt_rowcov`) and the
shim knows its dirty rects, so a dirty-row bitmap would replace even the fast
scan with a 200-bit test. That is worth roughly the remaining 19%, and it
reaches into the shim rather than living inside the backend.

### THE DIRTY-ROW BITMAP: NOT LANDED — the blocker is measured, and so is the prize

Investigated the obvious next #63 step and stopped short of shipping it, because
what the measurement found changes the plan.

**The mechanism already exists.** `#152` built `g_qd_touched` in the shim —
"has anything drawn to the surface since the last full present?", set by every
write path — precisely so a clean present could skip the backend's scan. It was
added for the mono build, where "a clean present still cost ~310 ms in the
backend's full-screen diff scan". The skip is gated on a single-buffered
backend, and **the STE backend declares `pages = 1`, so it qualifies.** It was
simply never firing.

**Why it never fires — counted over 960 presents on the HEIRS drive:**

| write path that marks the surface touched | hits |
|---|--:|
| **`qd_screen_pixels` — the direct-writer POINTER GRAB** | **3606 (3.8 per present)** |
| `DrawChar` | 4116 |
| `qd_pixmap_fill` | 2238 |
| `qd_set_palette` | 85 |
| `CopyBits`, `qd_cursor_track` | 0 |

The grab is the problem, and it is structural: it marks on GRAB, not on write,
because it cannot know what the caller will do with the pointer. Giving the
other primitives per-rect dirty rows therefore buys nothing — the grab would
still mark the whole screen 3.8 times a present. The grab has **29 call sites,
nearly all in `src/engine/boot.c`** (the direct writers bridged in #76), and
migrating lifted decompiled code to announce row ranges is where the actual work
is. Getting one wrong leaves a stale row on a screen nobody tests.

**So the prize was sized instead** (`FRUA_QDT_NOGRAB`, guarded in
release_guard.h — it is behaviour-altering and must never ship). Assume every
grab is a READ and drop its mark:

- **299 presents skipped entirely**, ~23% of all `qd_present` calls.
- **The end frame is PIXEL-IDENTICAL to the reference**, 0 of 489216 — on this
  drive, no grab site wrote anything that another primitive had not already
  announced.

That is the number to build against: roughly a quarter of full presents are
avoidable outright, before any row-granularity work. It is NOT proof the grabs
are read-only in general — one drive, one module — but it says the 29 sites are
worth auditing individually, and that most of them will turn out not to need a
mark at all.

**Recommended order for whoever picks this up:** audit the 29 `qd_screen_pixels`
call sites, mark the ones that WRITE with an explicit row range, drop the
blanket mark from the grab itself, and keep the full scan behind a diag flag
that reports any row which changed without being announced. That validator is
the whole safety story and should exist before the blanket mark comes off.

### A PROBABLE #61 MECHANISM, found sideways

Worth recording because it is the first reproducible handle on #61.

The phase-timed builds end the HEIRS drive with a **blank dungeon viewport**
where the plain build shows the guest-room art. It is not the row-compare
change (both compare arms blank identically) — and it **disappears completely
under `FRUA_QDT_NOGRAB`**, where the frame comes back pixel-identical to the
reference.

The single variable between those two states is HOW MANY FULL PRESENTS RUN. The
composite is ONE-SHOT: `st_vp_composite` clears `s_vp_active` on its first call,
so it paints the viewport into both pages exactly once per commit. An extra full
present arriving between the commit and the flip converts the viewport's rows
from the FROZEN chunky surface — which is stale there by design — straight over
the composited planes, and nothing repaints them.

That is a page-flip/redraw interaction that produces exactly #61's reported
symptom, and it is now reproducible on demand: add presents (the phase timers
do it accidentally; grab-marking does it by design) and the viewport goes.
Not yet proven — the next step is to instrument the commit/composite/flip
ordering directly rather than infer it from two builds that differ in present
count.

### THE 28 POINTER-GRAB SITES, AUDITED

Every `qd_screen_pixels` call site classified by what it does with the pointer.
The question each one answers is "must this mark the surface dirty, and over
which rows?"

| verdict | sites | notes |
|---|--:|---|
| **Dimensions only — never touches a pixel** | **3** | `boot.c:5383`, `:40286`, `:92629`. All three seed the A5 clip rect from `sw`/`sh`. **FIXED** — see below. |
| Read-only, diag build only | 1 | `boot.c:8140` (`FRUA_ROW24TRACE`) reads pixels into a log string. |
| Diag build only, writes | 2 | `quickdraw.c:1606` (`FRUA_CLICKMARK`), `boot.c:19645` (`FRUA_SPILLTEST`). |
| Already net-neutral | 2 | `quickdraw.c:2130`/`:2178` — cursor composite/restore, and `qd_present` already saves and restores the flag around them. |
| **Writes the WHOLE screen** | **10** | `4842`, `14850` (`port_draw_play_frame`), `18815` (`encounter_screen` memsets), `26466`, `26583`, `26838`, `29146`, `29315`, `91504`, `93035` (backdrop fills / memsets). Row granularity gains nothing here — but these are SCREEN TRANSITIONS, not per-frame. |
| **Writes a BOUNDED rect** | **9** | `3673` (`render_3d_faithful`, viewport), `6439` (`l2d4e`, and it has already clipped `top`/`bottom`), `14888` (jt312 3D view), `25111` (menu button plate), `26214` (GLIB blit, `y..y+h`), `26899` (banner `decorate`), `72986` (`jt200` at top/left), plus `7066`/`7206` (mono span/blit, mono builds only). **These are the per-frame ones, and every one of them knows its rows.** |
| **Indirect — the hard one** | 1 | `boot.c:36057` `jt1177` computes and CACHES a pixel address into A5 `-3076` for ~7 downstream consumers (the jt119/jt122 save-under pair, jt1192/1194/1197/1202, the jt1126 scroll). It cannot know whether the eventual use reads or writes; the mark belongs at the consumers. |

**Landed from the audit: the three dimension-only sites now pass NULL** for the
pixel pointer. `qd_screen_pixels` already skips the dirty mark when `pixels` is
NULL, so no new API was needed, and each site's `px` was dead immediately after
the call (checked over the whole enclosing function).

**Measured, same drive, one variable:**

| | presents | skipped clean |
|---|--:|--:|
| before the audit fix | 960 | **146** |
| after | 976 | **157** |
| `FRUA_QDT_NOGRAB` (every grab assumed a read) | 1008 | **299** |

**+11 presents, ~0.7 points.** Small, and it should be: those three run once per
screen entry, not per frame. Correct and free, so it stays, but it is not the
prize.

**★ THE PRIZE IS SMALLER THAN THE PREVIOUS ENTRY IMPLIED, and this is the
correction that matters.** The skip was ALREADY firing 146 times — 13.2% of
`qd_present` calls — before any of this. So NOGRAB's 23% is not 23 points of
new headroom; it is **~9 points**, held by the grabs that genuinely write. At
~19% of play spent in presents, converting all nine bounded writers would be
worth on the order of **1-2% of total play time**. That is not nothing, but it
does not justify migrating lifted decompiled code with a stale-row risk.

**So the recommendation changes.** Skipping whole presents is close to tapped
out. The value in the bounded-writer row ranges is not the skip — it is
NARROWING THE SCAN on the presents that still run: pass 1 currently diffs all
200 rows, and the nine per-frame writers between them touch a small fraction of
the screen. That is the version worth building, and it needs the same row
ranges, but it is measured against pass 1's ~90 t200 per present rather than
against the present count. Build the validator first either way.

### NARROWING THE SCAN: infrastructure landed, and it is honest about what it buys so far

Built the dirty-row machinery, migrated what can be migrated safely, and
measured. The infrastructure is in; the payoff is not, and the reason is
precise enough to act on.

**Where the row set lives: `platform/planar.c`, not the shim.** The layer rule
runs compat -> platform, and both sides need it — the Toolbox shim is what
knows the rects, a display backend is what would otherwise scan 200 rows to
rediscover them. `planar_touch_rows()` / `planar_touch_all()` /
`planar_dirty_rows()` in `planar.h`; `qd_touch_rows()` / `qd_touch_all()` are
thin shim recorders that also keep #152's boolean in step.

**Per-page pending sets in the backend.** The subtlety that makes this more
than a bitmap: the two pages catch up independently, so a row dirtied while
page A was the target must still be rebuilt when page B next comes round. The
shim's report is folded into BOTH pages' sets (`s_pend[NPAGES][ST_H]`) and each
page clears only its own as it handles them. That is the same invariant the
per-page shadows already encode — the set just avoids reading 64000 bytes to
rediscover it.

**Conservative by default, and the shipping build is a NO-OP.** Anything that
cannot name its rows calls `planar_touch_all()` and the backend scans
everything exactly as before. Verified: the WALKTEST walk frame is
**pixel-identical, 0 of 489216**, on the shipping flag set; four targets build;
412 tests pass.

**`FRUA_DIRTYCHECK` is the police** (guarded in release_guard.h — it re-scans
everything the set skipped, which is the entire cost the set exists to avoid).
It re-runs the old unconditional diff on every skipped row and reports any that
moved. It works, and it earned its keep immediately:

| marks in place | unannounced rows over the drive |
|---|--:|
| shim primitives announcing their CLIP | 17 (all in ONE present) |
| shim primitives announcing the GLYPH BOX | **175, across 73 distinct rows** |

**Migrated so far:** `qd_pixmap_fill` (exact clipped rect) and `DrawChar`
(glyph box). DrawChar first announced its CLIP, which was correct and useless —
a text port's clip is most of the screen, so 4251 calls a drive marked nearly
every row and the scan narrowed by 2%. The glyph box is the right unit.

**Measured, with the grab silenced (`FRUA_QDT_NOGRAB`, experiment only):**
pass 1 per present **90.4 -> 88.8 t200**. About 2%. The scan does not shrink
yet, and the validator says exactly why: the 73 missed rows fall in three
bands — **0-7** (frame top), **46-113** (the dungeon viewport), **184-197**
(command bar). Those are the un-migrated ENGINE writers, and they are the ones
that run on the frames that matter.

**So the remaining work is named and now safe to do incrementally**: migrate
`3673`/`14888` (the 3D view — rows 46-113), `6439` (`l2d4e`, which has already
computed `top`/`bottom`), `72986` (`jt200`), `26214` (the GLIB blit). Each one
is a two-line change — `qd_screen_pixels_nomark()` plus a `qd_touch_rows()` —
and after each, the DIRTYCHECK run must still read zero for the rows that site
owns.

**Worth finishing?** Pass 1 is ~90 of a ~168 t200 present, so ~54% of the
present and ~10% of all play time. Removing most of the scan is worth up to
~8-9% of play — several times what the pointer-grab present-skipping was worth
(~1-2%), which is why this is the branch to pursue and that one is not.

### MIGRATING THE ENGINE WRITERS: two land, the 3D-view group is REVERTED

Two of the five migrated and verified. The third attempt produced a real
rendering regression, and the way it was caught is the useful part.

**Landed (both provably bounded, both verified):**

- **`l2d4e` (`boot.c:6439`).** All four of its row loops reject `dy` outside
  `[top, bottom)`, and neither bound is reassigned afterwards, so the span it
  has ALREADY clipped is a proven superset. Announced right after the clamps.
- **The GLIB blit (`boot.c:26214`).** Its row loop skips `dy` outside
  `[0, sh)`, so the piece can only touch `[y, y + h)`.

**Reverted: the 3D-view group** — `render_3d_faithful` announcing `VT..VB`,
plus the non-marking grabs at `3673` / `14888` (jt312) / `4842` / `14850` and
`port_draw_play_frame` claiming everything.

The reasoning was that the renderer's chrome all goes through `l2d4e`, which now
announces its own rows, leaving only the view itself — the rect it already hands
to `dsp_viewport_commit`. **That is wrong.** `render_3d_faithful` writes outside
`VT..VB`, so with its callers no longer marking, those rows went stale: the
WALKTEST walk frame came back **2168 pixels different**, in a band across the
compass and the bottom of the viewport. Bisected by reverting the 3D group
alone — l2d4e + GLIB then measured **pixel-identical, 0 of 489216**, which
pins the fault on the 3D group and clears the two blits.

**★ THE LESSON, AND IT IS ABOUT THE VALIDATOR.** `FRUA_DIRTYCHECK` reported
**ZERO** unannounced rows on the HEIRS drive with the broken migration in
place. It was not lying: on HEIRS those rows WERE announced — by other
writers, the clock and roster text going through `DrawChar`. WALKTEST is an
event-free room where nothing else paints there, so the same code left them
stale. **A dirty-set validator only proves the drives you run it on**, and a
second, sparser design caught what the busier one structurally could not. Any
future site migration must be checked on BOTH, and the sparse one is the
sensitive instrument.

**Shipping impact: still none, measured.** pass 1 is **91.4 t200 per present**
against the 90.4 baseline — unchanged, because a single blanket grab marks the
whole surface and roughly two still fire per present. The migrations did halve
the grab count (3606 -> 1911), but that does not help until the LAST one on a
given frame is gone. This is an all-or-nothing threshold per present, which is
worth stating plainly: partial migration buys exactly nothing.

**What would finish it.** The remaining per-frame markers are `72986` (`jt200`)
and `36057` (`jt1177`, the cached-address site whose ~7 consumers are the real
writers), plus the 3D-view group done properly — which needs
`render_3d_faithful`'s true write extent established, not inferred. That means
reading what it draws outside the viewport rect (the compass and frame chrome
are the visible candidates) rather than assuming l2d4e covers it. Until all of
them go, pass 1 stays at 200 rows.

### RENDER_3D_FAITHFUL'S REAL WRITE EXTENT: ZERO ROWS — and the regression was mine, not its

Measured instead of reasoned (`FRUA_R3DEXTENT`, guarded): snapshot the whole
surface, run the render, diff every row.

    r3dext VT*1000+VB   = 24112        (the rect it commits: rows 24..112)
    r3dext rows changed = 0            (every call, WALKTEST)

**It writes NOTHING to the chunky surface.** Obvious in hindsight and worth
stating plainly: under ADR-0016 the viewport renders into the planar SCRATCH
via `dsp_viewport_commit`, and the chunky viewport rows are deliberately
FROZEN — `display_ste.c` says so in as many words. So the previous entry's
diagnosis, "`render_3d_faithful` writes outside `VT..VB` and those rows went
stale", was **wrong**. It writes outside nothing; it writes nothing at all.

**The actual bug was in the API I added.** `qd_screen_pixels_nomark` suppressed
TWO signals when it should have suppressed one:

| question | who answers it | what nomark did |
|---|---|---|
| "does this frame need presenting at all?" | `g_qd_touched` (#152) | **wrongly suppressed** |
| "WHICH rows changed?" | the dirty set | correctly deferred to the caller |

With both suppressed, `qd_present()` skipped the frame outright, so the backend
never ran and the pending viewport composite never happened — which is exactly
the compass-and-lower-viewport band that came back 2168 pixels different.

**★ And this is why FRUA_DIRTYCHECK reported ZERO on both drives while the
screen was visibly wrong.** It was not a coverage gap and WALKTEST-vs-HEIRS had
nothing to do with it — the previous entry's lesson about sparse drives was
drawn from a false premise and is **retracted**. The truth is structural: the
validator polices rows WITHIN a present. **It cannot police a present that
never happened.** Any future dirty-set work needs a second check at the present
level, not just the row level.

Fixed: a grab now always sets `g_qd_touched`, marking or not. **All five engine
writers are migrated and the walk frame is pixel-identical, 0 of 489216** —
including the whole 3D-view group the previous entry reverted.

**Shipping impact: still nil, and now I can say exactly why.** pass 1 is
**91.3 t200 per present** against the 90.4 baseline, with **1911 grabs over 960
presents — still ~2 per present** from sites nobody has migrated. The threshold
is unchanged: one blanket grab marks all 200 rows, so the scan only narrows
when the LAST marker on a frame is gone. Five down, and the count did not move
because the remaining two per frame are elsewhere.

**The next step is a tool, not a guess.** `QDT(0)` lumps all 28 grab sites into
one counter, which is why "~2 per present" cannot be attributed. Give
`qd_screen_pixels` a call-site id (a `__LINE__` argument behind a macro) and
dump per-site counts; that names the two per-frame markers in one run instead
of the four this entry spent inferring. The prime suspects remain `72986`
(`jt200`) and `36057` (`jt1177`, whose ~7 consumers are the real writers).

### THE PER-SITE COUNTERS FOUND IT IN ONE RUN: THE CURSOR. pass 1 −44%

Gave `qd_screen_pixels` a `__LINE__` call-site id and dumped per-site counts.
The "~2 blanket grabs per present" that four rounds of inference had failed to
attribute showed up immediately:

| site | hits over 960 presents |
|---|--:|
| `quickdraw.c:2251` — `cursor_composite` | **~950** |
| `quickdraw.c:2299` — `cursor_restore` | **~950** |
| everything else | single digits |

**One grab each, every present, each marking all 200 rows.** And it was
self-inflicted: `qd_cursor_track` has carried a net-neutral bracket since #152
that saves and restores `g_qd_touched` around the composite/restore pair — but
that bracket covers the BOOLEAN only, and the dirty-row set added here is a
second piece of state it knew nothing about. The cursor is 16x16 at a known
origin, so both paths now take the non-marking grab and announce their band:
`[oy, oy+16)` and `[save_y, save_y+16)`, exact rather than conservative.

**Measured, HEIRS drive, 960 presents:**

| | before | after |
|---|--:|--:|
| marking pointer grabs | 1911 | **10** |
| pass 1 per present | 91.4 t200 | **51.5 t200** |
| in-present per present | 169.5 t200 | **130.0 t200** |
| rows changed / converted | 9984 / 776 | 9984 / 776 |

**pass 1 is down 44% and the whole present down 23%.** Rows changed and rows
converted are byte-for-byte the same, which is the semantic check: the scan
looks at less and reaches the identical answer. In-present was 19.1% of the
run's wall clock, so this is worth roughly **4-5% of total play time** on a real
module — and unlike everything in the three preceding entries, it actually
lands in the SHIPPING build. No `FRUA_QDT_NOGRAB`, no caveat.

**Verification, all four:**

- WALKTEST walk frame **pixel-identical**, 0 of 489216.
- **Main menu pixel-identical**, 0 of 489216 — the screen where the cursor is
  actually visible, and therefore the one that would show a smear or a trail if
  the 16-row band were wrong.
- `FRUA_DIRTYCHECK` **zero unannounced rows on BOTH drives**.
- Four targets build; 412 tests pass.

**The lesson is about instrumentation, not the cursor.** Three entries above
this one spent their effort inferring which sites marked — reverting a group,
mis-blaming `render_3d_faithful`, retracting a lesson about sparse drives. One
`__LINE__` argument answered it in a single run, and the answer was a site
nobody had suspected, in the shim rather than the engine, and one the audit had
explicitly classified as "already net-neutral — no action needed". **When a
measurement keeps failing to attribute, stop inferring and label the data.**

**Still un-narrowed, and now cheap to find:** the remaining ten grabs are
noise. `pass 1` at 51.5 t200 is still the largest phase of a present, so the
next question is what the surviving announcements cost — the `l2d4e` and
`DrawChar` bands are the volume writers now, and the same counters can price
them by row count rather than by call.

### PRICING PASS 1 BY ROWS: the glyph box was 64 rows wide. −36% more.

With the cursor fixed, pass 1 was still the largest phase of a present, so the
next question was what it is now made of. Counted the rows actually diffed and
timed the gather separately:

| | per present |
|---|--:|
| rows SCANNED | **88.3** (of 200) |
| rows CHANGED | 10.4 |
| gather (folding the shim report into both pages) | 0.5 t200 — **1% of pass 1** |

Two things fall out. The gather is free, so the per-page set costs nothing to
maintain. And the scan was still touching 88 rows to find 10 — which, at ~0.24
t200 for a 320-byte long-compare, accounts for under half of pass 1. The rest
is `st_dt_ready_row` on the changed rows: its unguarded 320-iteration new-ink
scan, a stamp memcmp, and the row build.

**Where the 88 came from: `QD_GLYPH_MAX_ASCENT/DESCENT` were 32 and 32.** The
previous entry replaced DrawChar's clip with "the glyph box", but bounded that
box by a guess wide enough for any conceivable font — **64 rows announced per
glyph, for a font 8 to 12 pixels tall.** Now it takes the larger of the two
real metrics (`g_mac_font.ascent/height` and the built-in 8x8's 7/8), which is
still a superset whichever path draws and is an order of magnitude tighter.

| | before | after |
|---|--:|--:|
| rows scanned / present | 88.3 | **41.1** |
| pass 1 / present | 51.5 t200 | **32.8 t200** |
| in-present / present | 130.0 t200 | **111.3 t200** |
| rows changed / converted | 9984 / 776 | 9920 / 776 |

**pass 1 is now 32.8 t200 against the 91.4 it started at today — down 64%.**

**Cumulative on the present path, one session:**

| | in-present share of wall |
|---|--:|
| session start | **32.5%** |
| after the memcmp → long-compare row diff | 19.1% |
| after the cursor + glyph-box announcements | **13.6%** |

**Verified:** WALKTEST walk frame and the MAIN MENU both **pixel-identical, 0
of 489216** — the menu being the text-heavy screen a wrong glyph box would
corrupt. `FRUA_DIRTYCHECK` **zero** unannounced rows on the HEIRS drive. Four
targets build; 412 tests pass. `rows converted` is 776 in every run since the
narrowing began, which is the invariant worth watching: the scan keeps looking
at less and keeps reaching the same answer.

**Next, and now clearly the biggest single item:** `st_dt_ready_row` is over
half of what remains, and its 320-iteration new-ink scan runs on EVERY changed
row whether or not the row ends up converting — 9920 rows a drive. That is the
next thing to price, and unlike the scan it is not a search, it is real work
that may or may not be needed.

### THE NEW-INK SCAN: 3%, not "over half" — the ablation that said otherwise was confounded

Priced `st_dt_ready_row`'s 320-iteration new-ink scan by ablation
(`FRUA_NOINK`, guarded), and the first number looked enormous:

| | with scan | ablated |
|---|--:|--:|
| pass 1 | 31465 t200 | **21150** |
| band | 36439 | 29177 |
| rebands | 47 | **38** |
| rows changed | 9920 | **10799** |

**−33%, and confounded.** Removing the scan disables the re-quant trigger, so
the run took a different path: nine fewer re-bands, nearly nine hundred more
changed rows. The band column moving 20% is the tell — the scan does not live
there, so that difference is workload, not cost. Any timing read off this
comparison prices the scan AND the altered workload together.

**Two exact optimisations, neither changing what the trigger decides:**

1. **Threshold gate.** The only consumer is `s_dt_new_ink >= 4`, and the
   counter resets every present, so once four have been seen the rest of the
   present's scans cannot change the outcome — skip them.
2. **Local accumulator + pointer walk.** `s_dt_new_ink` is a file static and
   `s_used_idx` a static array, so the compiler had to assume the increment
   might alias the table and could keep neither in a register.

**Result: pass 1 31465 -> 30477 t200. Three percent.** And this time the
comparison is clean: **rebands 47 both ways, rows changed 9920, rows converted
776** — identical workload, one variable.

**So the previous entry's "st_dt_ready_row is over half of what remains" was
wrong**, and it was wrong because it was read off the confounded ablation. The
scan is a few percent. Retracted here rather than left standing.

**What this says about the remaining pass 1.** At 31.7 t200 per present it no
longer has a dominant component: ~41 scanned rows, ~10 changed rows through
`ready_row`, and 0.8 built rows do not add up to it under any per-row constant
I can justify — a factor of about two is unaccounted. Attributing that needs
per-phase timers INSIDE `ready_row`, which costs a Supexec pair per row (~20k
traps a drive, ~8% distortion). That is the honest next step if anyone wants
the rest; it is no longer a case of picking off an obvious hotspot.

**Verified:** WALKTEST walk frame **pixel-identical**; menu **pixel-identical
3 runs out of 3**. One earlier menu grab differed by exactly one cursor sprite
— the shield pointer versus the sword — which was a capture race against the
engine's own `SetCursor` on menu entry, not a rendering fault; three
back-to-back runs on the same binary then matched exactly. Four targets build;
412 tests pass.

### #63 PASS 1 IS FULLY NAMED — and the "~2x unaccounted" never existed

The previous entry left pass 1 at 31.7 t200 a present with, in its own words,
"no dominant component": ~41 scanned rows, ~10 changed and 0.8 built that "do
not add up to it under any per-row constant", a shortfall of roughly a factor
of two. The stated next step was per-phase Supexec timers inside
`st_dt_ready_row` — ~20k traps a drive and ~8% distortion.

That is not what this needed, and the hole was an artefact of how it was
estimated: **row counts multiplied by a hand-guessed cycles-per-row.** Count
the work exactly and price it once, and pass 1 closes to 6.8%.

**The method.** Two halves, neither of which perturbs the thing it measures:

1. **Exact work counters**, one add per call, not per unit —
   `sp_ph_cmpwords` accumulates `w + 1` at `st_row_differs`' early exit (so a
   row differing in its first word is charged one word, not eighty),
   `sp_ph_inkbytes` adds `ST_W` only when the new-ink gate actually passed,
   `sp_ph_built` counts `st_dt_build_row`.
2. **A one-shot calibration bench** (`st_prof_ph1cal`, ~6 s emulated) that runs
   each REAL primitive over the REAL buffers 1600 times and divides.

The bench is exact here for a reason specific to this target: **the 68000 has
no cache**, so there is no warm/cold distinction for a bench loop to get wrong.
This is the one measurement style that would not transfer to the 030 targets.

**Measured, HEIRS drive, 736 presents** (reproduced across two builds and three
dump windows; the calibration constants came back 679 / 1872 / 2379 / 3 both
runs):

| pass-1 component | t200 | share |
|---|--:|--:|
| `st_row_differs` — the shadow compare | **11628** | **50.1%** |
| the new-ink scan | **8217** | **35.4%** |
| `st_dt_build_row` — actual conversions | 1153 | 5.0% |
| dirty-row gather | 367 | 1.6% |
| loop floor (200 pend tests a present) | 276 | 1.2% |
| **RESIDUAL** | **1573** | **6.8%** |
| pass 1 total | 23214 | 100% |

Per-unit, all in this machine's own clock: the long-wise row compare is **53
cycles a byte**, the new-ink scan **146 cycles a byte** (2.75x — byte-at-a-time
with a table lookup against long-at-a-time compare), a row build **186 cycles a
pixel**. Pass 1 is 31.5 t200 a present, matching the 31.7 baseline, so this is
the same workload the earlier entries measured, not a different one.

**★ TWO TRAPS, both of which produced numbers that looked fine.**

**1. The bench measured nothing, and said so only if you knew the machine.**
Arm A first called `st_row_differs(row, row)` — same pointer, guaranteed equal,
full 80 words. It reported **6 t200 for 512 KB compared: 0.5 cycles a byte**,
which no 68000 can do. `st_row_differs` is static and small, so GCC inlines it,
proves `p[w] == q[w]` from the aliasing, and folds the loop away — leaving only
the counter increment, which it must keep, so the WORK counter still looked
completely plausible. Comparing against a second buffer holding a `memcpy` of
the first keeps the equality a runtime fact. That one line moved MODEL cmp from
778 to 11355 and turned a 67749 residual into 1520.

**2. Charging pass 1 for the force-full branch.** The raw counters are
since-boot across every phase, and the force-full path runs the same primitives
200 rows at a time OUTSIDE the pass-1 timer. It contributed **9000 of the first
run's 9776 `build_row` calls** — subtracting the raw figure would have charged
pass 1 with twelve times the conversions it actually performed. The counters
are now snapshotted across the timed region and the dump prints the pass-1
share and the since-boot total side by side; the gap between them is exactly
the force-full branch.

**And one pre-existing bug found on the way.** The older memcmp-vs-longcmp
calibration compared against `s_offpage`, which is `SCREEN_BYTES` (32000), while
indexing it to y=199 — i.e. 64000 bytes, running **32 KB past the end of an
Mxalloc block** on every sweep. No MMU, so it never faulted. Repointed at
`s_shadow_pg[]`, which is `ST_W * ST_H`. The published memcmp-vs-longcmp RATIO
is unaffected (both arms read the identical bytes, before and after), and that
ratio is the part the shipped change rests on.

**What this retires and what it opens.**

The "attribute the rest with per-row Supexec timers" plan is retired: there was
no unattributed bulk to find, and the instrument would have cost 8% distortion
to discover that.

It also settles the previous entry's retraction, in the direction of the
retraction being right about the METHOD and wrong about the size. That entry
withdrew "the new-ink scan is over half of pass 1" because the `FRUA_NOINK`
ablation moved rebands 47->38 and changed rows 9920->10799, so it priced the
scan and an altered workload together. **The scan is 35.4%** — the ablation's
33% was numerically close and arrived at unsoundly, which is why it was right
to withdraw it rather than keep a number that happened to land near the truth.

The 3% the last commit got out of it is also now explained: the threshold gate
(`s_dt_new_ink < 4`, reset every present) skips only **7.6%** of the scans —
7024 rows scanned of 7606 changed. Nearly every present pays full scans on its
first few changed rows before the counter saturates.

**The two levers that remain, in size order:**

1. **The shadow compare, 50%.** The dirty set already narrows 200 rows to ~35;
   what remains is verifying that an announced row really moved. Dropping the
   verification means trusting the announcements — for which there is real
   evidence (`FRUA_DIRTYCHECK` read ZERO unannounced rows over a full drive),
   and a bounded downside (an over-announced row costs a copy, and
   `ready_row`'s own ownership test still suppresses its conversion). It would
   also free the two 64 KB shadow buffers. This is a design change, not a
   tuning change, and wants its own de-risking pass.
2. **The new-ink scan, 35%.** Same shape as the win that already landed: the
   writers already read `lut[c]` for every pixel they stamp, so they could note
   `!s_used_idx[c]` for free at draw time instead of the present re-reading
   whole 320-byte rows to ask the same question. Strictly less work, because
   stamped pixels are a subset of scanned rows.

Together they are ~85% of pass 1, which is 30% of the in-present time, which is
13.6% of play. So the honest ceiling on both, done perfectly, is around 4% of
wall — worth stating before anyone starts, and a long way from the 32.5% the
present path cost when #63 opened.

### #63 THE BOOT IS THE REBANDS — 90 s of a 200 s boot is inside st_present

Everything #63 measured until now was the PLAY loop, because that is where the
instrument pointed. The user asked why an 8 MHz STE takes three minutes to the
main menu when a 7 MHz ECS A500 takes ~105 s, with a faster CPU and one fewer
bitplane. The answer was already in the profiler's FIRST window — its window is
16 presents and the boot is 16 presents — and nobody had read it.

**Boot, 16 presents, 11 rebands (STPROF, `--machine ste`):**

| phase | t200 | seconds | share of in-present |
|---|--:|--:|--:|
| **force-full rebuild** | 7700 | **38.5 s** | **43%** |
| **band (reband/repalette)** | 7120 | **35.6 s** | **40%** |
| pass 1 | 2368 | 11.8 s | 13% |
| run copies | 131 | 0.7 s | 0.7% |
| **in-present** | **17913** | **89.6 s** | — |

**~90 s of the 200 s boot is inside the display present**, which matches the
~95 s ST-vs-ECS gap almost exactly. This inverts the play-loop picture, where
pass 1 dominates and `band` is 20%: at boot pass 1 is 13% and the rebands are
everything.

The force-full is not a separate phenomenon — **a reband forces a whole-frame
rebuild**, so those two rows are one mechanism costing 74 s between them, about
6.7 s per reband. It had been invisible because the force-full branch `goto`s
past the pass-1 timer and landed in the unattributed remainder.

**Inside a reband (`b63rb`, 11 rebands):**

| step | t200 | s | per reband |
|---|--:|--:|--:|
| `quant_banded` (the median cut) | 1614 | 8.1 s | 0.73 s |
| **`s_used_idx` capture** | **2227** | **11.1 s** | **1.0 s** |
| stable-slot align | 118 | 0.6 s | 0.05 s |
| viewport overlay | 0 | 0 | — |
| band remainder (hw palette, slot reps, guards) | 3161 | 15.8 s | 1.4 s |

**★ THE PRESENCE SET IS COMPUTED TWICE.** `quant_banded` already builds
`used[b][i]` — which CLUT indices appear — and `st_reband` then runs its own
64000-iteration `s_used_idx[qsrc[n]] = 1` pass immediately afterwards. The
duplicate costs MORE than the quantiser it duplicates (11.1 s vs 8.1 s),
because `quant_banded` deliberately samples every OTHER row (32000 pixels) and
the copy samples all of them.

They cannot simply be merged: the half-row set would miss indices that live
only on odd rows, and `s_used_idx` feeds the new-ink detector, so a smaller set
means MORE spurious re-quants — the wrong direction. The fix is one full-frame
pass that fills both, with `quant_banded` taking presence as an input. That
changes which colours the median cut sees (all rows, not half), so it is a
palette-affecting change and needs frame verification, not a free refactor.

**Why the rebands fire at all — `b4audit` over 22 of them:**

| # | content rows | CLUT bytes moved | used idx | of which moved |
|---|--:|--:|--:|--:|
| 5 | 125 | **3** | 130 | **80** |
| 10, 14, 15, 18, 21 | 200/138/193/29/29 | **0** | ~23 | 7–26 |
| 17, 20 | **0** | 96 | 67 | 41 / 43 |
| others | 200 | 74–713 | 1–223 | most |

Three distinct wasteful classes, none of which #89 could see (it asked whether
rebands were *legitimate*, not what they *cost*):

1. **CLUT moved ZERO bytes** (5 of 22). These are the **new-ink trigger**:
   `s_dt_new_ink >= 4` sets `s_dirty = 1` AND clears `s_banded_valid`, forcing a
   full re-quant for a palette that did not change. The new-ink scan optimised
   two commits ago is the thing *causing* the most expensive operation in the
   boot — a 3% saving on the trigger, feeding a 6.7 s response.
2. **Content identical, only `s_vp_active` vetoed the cheap path** (#17, #20).
   Zero content rows and a 96-byte CLUT move, yet 41 of 67 used indices were
   reshuffled.
3. **A 3-byte CLUT delta reshuffling 80 of 130 in-use indices** (#5). One
   colour entry moved and the median cut landed somewhere completely different.

**★ CLASS 3 IS ALMOST CERTAINLY THE USER-REPORTED VISUAL BUG** — "flashes a
very true-to-colour image, then redraws to a greyed-out look and darkens the
door on HEIRS". A reband IS a visible palette reallocation, and #5 shows a
trivial CLUT edit reshuffling most of the on-screen colours. The B3.2
stable-slot alignment exists to prevent exactly this and is not achieving it
here (80/130 moved), which is the thing to look at first.

**One change landed, and it is small.** The `content_same` test used `memcmp`
over all 64000 bytes — 93 cycles/byte where the long-wise loop is ~53, the same
substitution that halved pass 1, never applied here. Swapped for
`st_buf_differs`. Measured `band` 7222 -> 7120 t200, **1.4%, which is within a
single run's noise** — because both primitives EARLY-EXIT and the content
genuinely differs on most rebands, so neither ever scans the full buffer. The
predicted ~8 s did not exist. Kept because it is semantically identical and
strictly cheaper in the worst case, but it is not a win worth claiming.

**Ranked, with the arithmetic:**

1. **Stop the force-full where the remap barely moved** — 38.5 s, the single
   biggest item in the boot, and shared with the visual bug.
2. **Fix class 1** (zero-CLUT-delta rebands from the new-ink trigger) — the
   right response to unseen ink is to map it to its nearest existing slot, a
   `remap[]` patch, not a re-partition. No reshuffle, no force-full.
3. **Single presence pass** — ~8 s, but palette-affecting.
4. **The 15.8 s band remainder** is still unattributed; `st_build_hw_palette`
   and `st_compute_slot_reps` are the candidates.

For scale: #63's remaining play-loop levers are worth ~4% of play wall. This is
~45% of the boot.

### #63 (2) THE NEW-INK TRIGGER NO LONGER RE-QUANTS — patch the remap instead

The detector exists for a real bug: an index the quantiser never saw rides
quant_banded's fallback, which is nearest in **LUMINANCE** — so a distinctly
coloured glyph lands on whatever background matches its brightness, and the
text goes invisible. Its RESPONSE was the problem: `s_dirty = 1;
s_banded_valid = 0`, i.e. a full re-quant, for ink that had not moved the CLUT
by a single byte (5 of 22 rebands measured that way).

The palette does not need re-partitioning to give one index a slot. It needs
that index mapped to the nearest slot it already has. `st_patch_new_ink()`
does exactly that — nearest in **RGB**, which cannot make the luminance
mistake — and marks it seen. Every already-mapped index keeps its slot, so
there is no epoch reset and, crucially, **no visible palette reshuffle**.

**Measured, same drive, matched flags, boot window:**

| | before | after |
|---|--:|--:|
| rebands | 11 | **10** |
| in-present | 17913 | **17283** (−3.5%) |
| band | 7120 | **6511** (−8.6%) |
| pass 1 | 2368 | 2523 (+6.5%) |
| force-full | 7700 | 7512 |

−630 t200 ≈ **−3.2 s of a 200 s boot**, one fewer palette reshuffle, and the
fallback for patched ink is now correct rather than luma-approximate. Modest at
boot because only ONE of the eleven boot rebands was the zero-CLUT kind; the
class is 5 of 22 across a longer run.

**★ THE GATE STAYS — REMOVING IT DOUBLED THE BOOT.** The first cut dropped the
`s_dt_new_ink < 4` threshold so the scan could collect every unseen index
rather than stopping at four pixels. In-present went **17913 -> 35653**, +88 s.
The gate is not the 3% it measured as in the play loop: there few rows change,
while at boot nearly all of them do, so without it every changed row of every
present pays a full 320-byte table-lookup scan. Recording identities INSIDE the
gate is free (one lookup per byte, as before), so that is what ships.

The consequence is that the recorded index set is partial, which decides the
rest of the design: a partial row set cannot be used for a targeted rebuild,
because the index is now marked seen and the rows we missed would never
register as new ink again — they would keep the stale mapping forever. So the
patch sets `s_force_full`. That is still half the old cost (the re-quant, the
epoch reset and the reshuffle are gone) and the remaining rebuild is item (1).

A `s_replane[][]` third signal ("planes stale, content unchanged") was built
for the targeted rebuild and then **removed** when the gate finding killed the
approach — dead mechanism is worse than no mechanism. The reasoning is kept
here because the signal is genuinely distinct from the other two and will be
needed if item (1) makes targeted rebuilds viable.

**★ AND A MEASUREMENT TRAP, THE THIRD OF THIS KIND THIS SESSION.** The "still
35513" reading that made the fix look inert was **my own diagnostic flag**: I
had added `-DFRUA_PLANAR_DIAG` to see the patch logging, and that flag runs
per-present `st_dt_probe` calls the baseline never had. Matching the flags
turned 35513 back into 17283. The rule already written up for the ablation and
the printf applies verbatim to build flags: **change one thing, and check that
the phases which should be unaffected really are.**

Also fixed on the way: `st_buf_differs` was defined next to `st_row_differs`,
which lives inside the `FRUA_PLANAR` block, while `st_present` calls it on
every build — the 020 Falcon and FPU targets failed to compile. It now sits
above the guard with the other shared helpers.

### #63 (1) THE FORCE-FULL WAS 92% ROW BUILDS — and half of a "row build" was a scan that cannot fire

The plan for item (1) was "skip the rows whose indices did not move after a
re-quant". **Measure the ceiling first** — and the ceiling closed the plan.

**Rows that could skip, per boot reband (every index in the row unmoved):**

| reband | used idx | moved | rows skippable |
|---|--:|--:|--:|
| 1, 3 | 1 | 0 | 200 / 200 (a one-colour screen) |
| 2, 4, 5, 7, 8, 9 | 22–130 | most | **0 / 200** |
| 6 | 223 | 214 | 24 / 200 |
| 10 | 22 | **2** | 131 / 200 |

**Six of ten rebands can skip exactly nothing**, and reband 10 — with 91% of
its indices UNMOVED — still only skips 65% of rows, because one moved index
anywhere in 320 pixels disqualifies the row. 555 of 2000 rows, ~27%, worth
~10 s. #89 reached the same verdict from index churn alone; the row-level
number is the one that actually decides it, and it is worse than the index
number suggests.

**So the force-full was split instead, and that found the real thing:**

| force-full component | t200 | s | share |
|---|--:|--:|--:|
| **row builds** | 6920 | **34.6 s** | **92%** |
| copies (192 KB x 10, BLiTTER) | 589 | 2.9 s | 8% |

The copies are fine — ~12 cycles/byte, proper blitter speed, and `ste: blitter
= 1` confirms it is in use. (An earlier inference that they ran at memcpy speed
was arithmetic, not measurement, and was wrong.) The cost is 2000 row builds at
**3.46 t200 each against a calibrated `st_dt_build_row` of 1.487** — so more
than half of a "row build" was not the conversion.

**★ IT WAS THE NEW-INK SCAN, LOOKING FOR SOMETHING THAT CANNOT BE THERE.**
`st_reband` captures `s_used_idx` from the very frame it is about to
re-convert, so during the force-full that follows, every one of the 200 scans
is guaranteed to come up empty — and *because* it comes up empty,
`s_dt_new_ink` stays 0, the `< 4` gate never closes, and all 200 rows pay the
full 320-byte scan. A one-line `s_ink_fresh` flag skips it.

**Measured, matched flags, clean run:**

| | after item (2) | after item (1) |
|---|--:|--:|
| in-present | 17283 | **14576** (−15.7%) |
| band | 6511 | 6511 (control, unchanged) |
| pass 1 | 2523 | 2525 (control, unchanged) |
| force-full | 7512 | **4801** (−36%) |
| ff row builds | 6920 | **4218** (−39%) |
| ff copies | 589 | 580 |

**−2702 t200 = −13.5 s**, entirely in the row builds, with both other phases
flat — the controls that make it believable. Menu frame **byte-identical**
(md5 match, 0 differing pixels): the flag only omits a detector in the one
place it provably cannot fire.

**Cumulative on the boot, this session:** in-present 17913 -> 14576 t200,
**−16.7 s of a ~200 s boot, ~8.4%**, across items (2) and (1). The force-full
is no longer the largest phase; `band` (6511) is.

**And the fourth confounder of the session, same family as the other three.**
The reading that made item (1) look like it had also inflated `band` (6511 ->
7631) was the ROWS-SKIPPABLE ceiling probe I had just added — a 64000-pixel
pass per reband, sitting INSIDE the band timer. It was removed once it had
answered its question, both because it had served its purpose and because
leaving instrumentation inside a phase timer poisons every later reading of
that phase. Running tally of things that faked a result this session: a printf,
an ablation that moved a control path, a diagnostic build flag, and now a
probe inside the timer it was next to.

**What is left on the boot**, in order: `band` at 6511 t200 (32.6 s) — of which
the duplicate `s_used_idx` pass is ~11 s and the un-split remainder
(`st_build_hw_palette`, `st_compute_slot_reps`) ~16 s — then the 4218 t200 of
genuine row conversion, which is inherent to a re-quant and only reducible by
rebanding less often.

### #63 B3.2 STABLE-SLOT ALIGNMENT REWRITTEN — and slot 0 is the BORDER

**First, a correction to the previous entry.** It said the alignment "exists to
prevent exactly this and is not achieving it", pointing at the user-reported
grey flash. That was wrong. The alignment permutes `s_band_pal` AND
`s_band_remap` **together**, so index v keeps its exact colour: it is a pure
renumber and the rendered pixels are invariant under it. It cannot cause a
visible palette change. The flash must come from the median cut choosing
different representative COLOURS, which is a separate problem.

What the alignment does govern is how many indices keep their slot NUMBER,
which is what decides whether a row can skip re-conversion after a re-band.

**Two defects in the original:**

1. **Wrong objective.** It minimised palette-position COLOUR DISTANCE — a
   proxy. The thing that matters, and the thing `used moved` measures, is how
   many used CLUT indices keep their slot. Where two old positions hold
   near-identical colours, distance cannot tell them apart but the indices
   sitting on them can.
2. **Order-dependent greedy.** Old position 0 got first pick of all sixteen and
   position 15 took the leftovers, so one arbitrary early choice could displace
   a later exact correspondence and cascade.

Replaced with a 16x16 table of how many used indices move from old slot p to
new slot n, matched strongest-correspondence-first (greedy max-first, not full
Hungarian — the table is dominated by a few large cells and this is a few
thousand cycles). Needs the live index->slot map from before the re-quant, so
`s_remap_prev` is snapshotted at the top of `st_reband` — taken there rather
than maintained at the bottom so it also captures anything `st_patch_new_ink`
changed since.

**★ SLOT 0 IS THE ST'S BORDER COLOUR REGISTER.** Letting the correspondence
greedy have position 0 changed the border from black to olive — **174976
differing pixels** in the menu grab, every one of them OUTSIDE the 320x200
image, where no pixel index is involved at all. The renumber is invariant for
pixels and NOT for the border. The original code's order dependence — walking
old positions 0..15, so position 0 always got first pick by colour — was, for
this one slot, load-bearing. Position 0 is now claimed by colour first,
deliberately and with a comment saying why, and the correspondence matching
gets the other fifteen.

**Measured (10 boot rebands):**

| | original | rewrite, slot 0 free | rewrite, slot 0 pinned (shipped) |
|---|--:|--:|--:|
| total `used moved` | 515 | 420 | **441 (−14%)** |
| rows skippable / 2000 | 555 (27.8%) | 721 (36.1%) | — |
| menu frame vs before | — | **174976 px differ** | **0 px — identical** |
| align cost | 106 t200 | — | 132 t200 (+0.13 s) |

**★ AND IT BUYS NO TIME TODAY. Stated plainly because the temptation is to
report the 14% and stop.** `ff rowbuilds` is 4217 t200 before and after —
nothing currently exploits a stable slot, because a re-band calls
`st_dt_epoch_reset()` and the force-full rebuilds every row regardless.

**The follow-on that would spend it is closed, on cost rather than ceiling.**
Detecting whether a row contains a moved index is a 320-byte table-lookup scan
— the same shape as the new-ink scan, measured at 146 cycles/byte, so **1.17
t200 per row against a calibrated `st_dt_build_row` of 1.487**. At the improved
36.1% skippable that is `1.17 + 0.639 x 1.487 = 2.12` per row versus 1.487
today: **43% WORSE**. The detection costs nearly as much as the rebuild it
avoids. A partial rebuild only becomes viable if row->index membership is
already known without scanning, i.e. maintained at draw time, which is more
state and more per-write cost.

So this change ships as a correctness-of-intent fix at negligible cost (0.065%
of the boot), better on its own metric, with a latent border hazard now
documented — not as a performance win. It pays off only if the epoch reset ever
becomes partial.

### #63 MEDIAN-CUT "INSTABILITY" — measured, and mostly NOT instability

Asked to fix the median cut, the first job was to measure the thing it was
accused of, because the previous attribution (slot churn) turned out to be
appearance-neutral and nobody had ever measured the COLOURS.

**The measurement**: for every index in use, the squared RGB distance between
the colour it displayed BEFORE a re-quant and the colour it displays after —
`st_coldist(s_band_pal_prev[s_remap_prev[v]], s_band_pal[s_band_remap[v]])`.
Max possible 195075. Plus mean palette CHROMA (max-min per slot) either side,
because "greyed out" is specifically a chroma drop.

| reband | rows | CLUT bytes | dmean | dmax | chroma |
|---|--:|--:|--:|--:|--:|
| 2 | 200 | 713 | 61184 | 157952 | 0 -> 100 |
| 3 | 200 | 126 | 256 | 256 | 100 -> 0 (only 1 index in use) |
| 4 | 200 | 455 | 43016 | 124416 | 0 -> 94 |
| **5** | 125 | **3** | **5244** | 29952 | 94 -> 84 |
| 6 | 200 | 657 | 34206 | 94976 | 84 -> 65 |
| 7 | 200 | 659 | 30136 | 80640 | 65 -> 31 |
| 8 | 200 | 667 | 8704 | 23808 | 31 -> 30 |
| 9 | 200 | 74 | 8389 | 46336 | 30 -> 32 |
| 10 | 7 | 512 | 104 | 1536 | 32 -> 32 |

**The big shifts are legitimate.** Rebands 2/4/6/7 move 30000-61000, but each
has 200 changed content rows AND 455-713 changed CLUT bytes: a genuinely new
screen with genuinely new colours, which is what a re-quant is for. The
progressive chroma decline 100 -> 94 -> 84 -> 65 -> 31 is not the quantiser
losing its grip either — it tracks the boot going from the colourful title
painting to the grey granite menu, and the captured frames confirm exactly
that.

**Only reband 5 looks like real instability**: THREE CLUT bytes changed, and
the used palette still moved by dmean 5244 (RMS ~72, ~42 per channel). Its
content moved 125 rows, so the used-colour POPULATION changed — the cut sees a
different input and lands elsewhere. Real, but a single mild case in ten.

**Captured the boot to check the reported symptom.** Frames every 3 s: the
title screen renders in full colour (golds, browns, the blue orb, lightning),
then the granite menu chrome, then the final menu with cyan headings. The
"colourful then grey" transition in the BOOT is faithful — the menu genuinely
is grey granite. The reported artefact is on the HEIRS door screen, which this
capture never reaches, so it is NOT yet reproduced and NOT yet diagnosed.

**★ NOTHING LANDED TODAY WOULD HAVE FIXED IT.** The three changes were the ink
patch (adds remap entries for previously-unmapped indices only), the
force-full ink-scan skip (no palette effect, menu byte-identical) and the
stable-slot alignment (appearance-neutral, verified 0 differing pixels). None
touches which colours the quantiser picks. If the artefact was there before, it
is there now — with one narrow exception: `st_patch_new_ink` changes what an
unseen index displays from nearest-LUMINANCE to nearest-RGB, so if the door's
colours were arriving as unseen ink, that specific case is improved.

**Both probes were removed after answering.** They cost ~880 t200 in `band` —
not the arithmetic but the FOUR EXTRA `dbg_log_num` CALLS per reband, log
writes inside a phase timer. Fifth confounder of the session, and the second
of exactly this shape after the ROWS-SKIPPABLE probe. The probe code is quoted
in this entry so it can be re-added deliberately rather than left resident.

**To actually settle the door**: drive HEIRS to the entry BIGPIC
(`FRUA_AUTOWALK_TREASURE`) and capture frames either side of the reband that
fires there, with the two probes temporarily back in. Until that exists, any
change to the median cut would be tuning against an unreproduced symptom.

### ★ #61 REPRODUCED — the HEIRS door screen, frame-accurate and headless

Drove HEIRS to the entry 3D view (`FRUA_AUTOWALK_TREASURE`) and captured at 2 s
intervals **at real speed** — fast-forward compresses a transient into fewer
host frames and would have hidden it.

**The evidence is three frames:**

| pair | differing pixels in the 3D viewport |
|---|--:|
| e64 -> e66 | 2804 |
| e66 -> e70 | 2804 |
| **e64 -> e70** | **0** |

The viewport changes and changes **back**. That is not a scene change, a
re-quant or a palette shift — it is a transient, and the first and last frames
are byte-identical.

**What changes**, from the difference mask: scattered single pixels in the sky
(stars) and **one solid band of the floor**. A floor pixel goes
`srgb(136,136,136)` -> `srgb(17,17,34)` -> back; a wall pixel three rows up is
unchanged throughout. **17,17,34 is the dark backdrop colour** — the value
sitting in the chunky surface *underneath* the composited viewport.

So the band is briefly showing STALE CHUNKY CONTENT through the viewport, which
is #61's suspected mechanism confirmed: the viewport composite is one-shot, and
an extra full present between commit and flip converts the frozen chunky rows
back over the composited planes. One present later the composite runs again and
the floor returns.

**★ THE FRAMING THIS SETTLES.** The extra present is not merely a cost on the
8 MHz machines — it is a CORRECTNESS bug that fires on every target, because
every target composites a viewport over a chunky surface. The Falcon pays the
same redundant full present; it is simply fast enough that the artefact flashes
by. So the redraw census is not an ST optimisation, it is the shared root of
both the performance ceiling and the visible glitches.

**A trap that nearly cost this hunt.** The first attempt used the `FRUA_STPROF`
build, which BLANKS the HEIRS viewport entirely (~10 extra Supexec per present
on a timing-sensitive path — the #91 sensitivity, already recorded). The
captured frames showed an empty BIGPIC area, which looks exactly like a render
bug and is not one. **Visual hunts must use a build WITHOUT the phase timers**;
correlating them with reband logs means two runs, not one.

**Next: a present-call-site census.** The cursor problem earlier in #63 was
cracked in one run by tagging `qd_screen_pixels` with `__LINE__` and counting
per site. The same instrument applies here: tag every path that reaches a FULL
present, count them per screen, and find the ones that fire more than once for
one logical change. That is measurable, platform-independent, and fixes cost
and correctness together.

### ★ THE PRESENT CENSUS — one site is 90% of all full presents

Built the analogue of the `__LINE__` grab census that cracked the cursor
problem: every `qd_present()` call site tagged with its line plus a one-char
file discriminator (`__FILE__[7]`), logged as an **ordered trace** rather than
per-site totals — because the thing being hunted is two presents for one
logical change, which a total cannot show and a sequence can. Each call also
records its OUTCOME, since a held or skipped present is not a redraw:
`H` held/suppressed, `S` skipped clean (#152), `P` actually presented.

**HEIRS drive, 40 keys, ~63 call sites in the codebase:**

| | count |
|---|--:|
| calls | 1537 |
| **presented** | **933** |
| held / deferred | 486 |
| skipped clean | 118 |

**Presented, by site:**

| site | presents |
|---|--:|
| **`src/engine/boot.c:24006`** | **838 (90%)** |
| `compat/quickdraw.c:413` (the deferred commit) | 28 |
| `boot.c:24144` | 20 |
| `boot.c:16600` / `:17153` | 12 / 11 |
| everything else | 1–7 each |

**And the redundancy signature — consecutive present pairs:**

    boot.c:24006 -> boot.c:24006     812   <-- SAME SITE, BACK TO BACK

**`boot.c:24006` is a TIMER-DRIVEN IDLE PRESENT**, not a change-driven one: a
port concession inside the modal pump so the engine's `l2c60` walk becomes
visible during `jt453`/`l2d3e`, rate-limited to ~5 Hz (12 ticks). Its own
comment already records one round of this — an unconditional present there made
every typed glyph pay a full-frame diff+blit and starved the engine to 3
characters a second. The rate limit fixed the starvation; it did not make the
present conditional on anything having changed.

**★ AND THE #152 CLEAN-SKIP IS NOT THE ANSWER — IT IS ALREADY WORKING.**
Instrumented `g_qd_touched` on entry: **ZERO of the 811 presents ran on an
untouched surface.** Every one had the surface genuinely marked dirty, so the
skip cannot eat them. That closes the obvious hypothesis and points at the
real chain:

1. Something grabs the screen pointer during the modal pump.
2. A grab sets `g_qd_touched` — unconditionally, and deliberately so: that is
   the fix from the `qd_screen_pixels_nomark` trap, where suppressing it made
   `qd_present()` skip a frame whose viewport composite was still pending.
3. The 5 Hz idle present sees "touched" and does a FULL present.
4. The full present converts ~0.8 rows. Nearly all of the work discovers that
   nothing changed — which is exactly the pass-1 cost measured earlier.

So `g_qd_touched` answers "did anyone take a pointer to the screen?" when the
idle present needs "did any pixels actually change?". Those are different
questions, and this is the THIRD time in #63 that conflating two signals has
been the bug (see the `g_qd_touched` vs dirty-set note, and `s_replane`).

**The fix this points to** is to gate the IDLE present — and only that one, not
the real frame commits — on the dirty-row set having something in it, rather
than on the touched boolean. The row set already knows, it is already
maintained, and a visibility concession with nothing to show has nothing to
present. That is a narrow change at one call site, and it is worth ~90% of the
full presents on EVERY target, not just the ST.

Not yet implemented: this entry is the census, and the next step is that gate
plus an A/B on both the drive wall clock and the door-screen artefact.

### #61 THE IDLE-PRESENT GATE — landed, and it changes NOTHING yet

Landed the gate the census pointed at: `qd_dirty_any()` (new, `planar_dirty_any`
behind a shim wrapper, keeping the engine→compat→platform layer rule) so the
5 Hz idle present at `boot.c:24028` only fires when a writer has actually
announced rows. The tick is stamped only when it presents, so the first change
after an idle stretch shows immediately rather than waiting out the window.

**The A/B, same drive, same seed, back to back:**

| | boot | presented | idle site | skipped |
|---|--:|--:|--:|--:|
| A — ungated | 13 s | 943 | **846** | 120 |
| B — gated | 13 s | 943 | **846** | 119 |

**Identical.** `qd_dirty_any()` is true at essentially every idle tick, so the
gate never gates. The predicate is right; the premise — that the idle present
often has nothing to show — is **wrong as measured**, or something announces
rows continuously.

**Three false steps worth recording, because two of them nearly published a
wrong result:**

1. **The first arm-B run reported `idle-site = 0` and looked like a total
   success.** It was a stale grep: landing the gate added comment lines, so the
   site moved from `boot.c:24006` to `:24028` and the packed value no longer
   matched. The correct decode says 846, unchanged. A census keyed on line
   numbers is invalidated by editing the file it measures — re-derive the key
   from the run, never carry it across an edit.
2. **Boot time read 113 s then 13 s for the same code**, which looked like a
   catastrophic regression and was host load. Wall clock on this box is not a
   usable A/B metric at that resolution; the present COUNT is deterministic and
   is the one to quote.
3. **`objdump -d | grep qd_dirty_any` returned 0** and briefly suggested the
   gate had been compiled out. Cross-file calls are relocations: `objdump -dr`
   or `nm` (`U _qd_dirty_any`) shows them. The gate was there all along.

**What is NOT yet known**: which writer announces rows between idle ticks. The
diagnostic that would name it — logging the all-flag and the dirty row count at
each idle tick — was run for only 45 s of a ~200 s drive and never reached the
modal pump, so it produced nothing. Re-run it over the full drive: an all-flag
means a blanket `qd_touch_all` from an un-migrated grab; a count of 16 would be
the cursor; a small odd count names a specific writer.

The gate is kept because it is the correct predicate for a visibility
concession, and it is inert rather than harmful. It is **not** a fix and the
code says so.

### ★ #61 THE ANNOUNCER NAMED — it is the TYPEWRITER, and the waste is FULL-vs-RECT

Re-ran the diagnostic over the whole drive (the 45 s version never reached the
modal pump and produced nothing). Logging the dirty REGION rather than a bare
count named it immediately:

| ticks | dirty rows | |
|--:|---|---|
| 187 | 135-143 (9) | |
| 165 | 143-151 (9) | text lines, |
| 105 | 151-159 (9) | marching DOWN the |
| 92 | 159-167 (9) | message area in |
| 48 | 167-175 (9) | 8-pixel steps |
| 37 | 175-183 (9) | |
| 165 | 0-199 (200) | genuine scene changes |
| 143 | ALL-flag | blanket `qd_touch_all` grabs |
| 97 | nothing | the gate would skip these |

Nine-row spans stepping 8 pixels at a time down rows 135-183 is **the
typewriter** — `l435a` pacing narrative text a line at a time, exactly the
mechanism the idle present was added to make visible.

**So the premise behind the gate was wrong in an instructive way.** These
presents are NOT idle and NOT spurious: something really did change and really
does need showing. Only 97 of ~918 ticks had nothing at all. The gate is
therefore correctly inert, and the earlier framing — "a timer presenting an
unchanged screen" — was wrong. What is wasteful is the SHAPE of the response:

**a 9-row change triggers a FULL 200-row present.**

That closes the loop on every number measured in #63. Pass 1 diffs 200 rows to
rediscover the 9 the dirty set has already named; the earlier phase split
measured "~10 rows changed, 0.8 converted" per present, which is exactly these
9-row text lines; and the door's floor band is a full present converting frozen
chunky rows it had no reason to touch.

**The fix this actually points to: make the idle present a RECT present bounded
by the announced rows.** The information needed is already there — the dirty
set names the 9 rows, `qd_present_rect` already exists, and the backend already
has a rect path (the viewport composite uses it). Roughly 600 of ~918 presents
are small spans that could take it; the 165 full-screen announcements and 143
blanket ALL-flag ticks legitimately stay full.

**One real complication, already documented at the reset site**: a rect present
updates only the SHOWN page, so on the double-buffered ST the rows must stay
dirty until a full present has given the other page its turn — which is why
`qd_present_rect` deliberately does not call `planar_dirty_reset()`. A
rect-ifed idle present therefore needs per-page bookkeeping, and the per-page
`s_pend[][]` sets built earlier in #63 are exactly that mechanism. This is not
a two-line change and should not be attempted as one.

**Status**: the gate stays (correct predicate, inert, comment says it is not a
fix). The next piece of work is the rect conversion, and it now has a measured
target: ~65% of full presents become 9-row updates.

### #61 THE RECT CONVERSION IS A REGRESSION — reverted, and it corrects my own framing

Built it: `qd_present_dirty()` collecting the announced runs and presenting them
through the existing rect hook, with the cursor composited first so its band
joins the set (otherwise a rect blit erases the cursor).

**It renders perfectly.** Door, roster, compass, position, and all four lines of
caravan text with the cyan highlight — captured and checked. And full presents
fell **943 -> 47, a 95% reduction**, exactly as designed.

**And the drive got much slower.** Two runs, both stopping at key 20 of 46 in
270 s where the baseline reached 36 in 240 s. Identical stopping points suggest
stalled rather than merely slowed; that was not run down further.

First hypothesis — that issuing one rect per run paid N viewport composites,
since `st_present_rect` ends every call with `st_vp_composite()` (~150 t200) —
was **wrong**: coalescing to a single bounding span changed nothing.

**★ THE FRAMING WAS WRONG, AND THE MEASUREMENT WAS ALREADY IN HAND.**

    pass 1 rows SCANNED = 25280 over 720 presents = 35.1 of 200

Pass 1 **already only scans the announced rows** — that is what the dirty-row
work earlier in #63 achieved. So "a 9-row change triggers a 200-row present"
was simply not true any more: it triggers a ~35-row scan that converts 0.8
rows. The remaining full-present cost is the band branch and the fixed
per-present overhead, not a 200-row sweep.

Meanwhile the rect path is the DUMB one. `st_blit_rows` converts every row in
the rect unconditionally — it has no shadow diff and no ownership skip, because
the walk's 88x88 viewport rect always genuinely changes. Replacing a smart
35-row scan that converts 0.8 rows with an unconditional convert of up to 100
rows is a straight loss, and that is what the drive measured.

**Reverted.** What survives is the census (diagnostic, instruction-identical
when off), the `qd_dirty_any` predicate and the inert gate — all already
committed and all still correct.

**What this rules out, which is worth as much as a win**: the full-present path
is not the crude thing the earlier entries assumed. It is already narrowed to
announced rows and already skips per-row. Making the idle present "smaller"
cannot help while the smaller path is less selective than the larger one. Any
future attempt must give the rect path the same per-row shadow/ownership skip
the full path has — at which point it is the full path restricted to a range,
and the honest question becomes whether the remaining band + fixed overhead is
worth the plumbing.

**The door artefact is therefore still open**, and still worth its own fix: it
is a correctness bug (stale chunky rows converted over the composite), not a
throughput one, and it does not depend on this optimisation.

### ★★ #61 FIXED — the viewport composite was one-shot, and there are TWO pages

**The bug, in one line:** `st_vp_composite()` cleared `s_vp_active` on its first
run — "one-shot per commit" — while the backend page-flips between NPAGES = 2
pages, so only ONE of them ever received the composited viewport.

The sequence that produces the artefact:

1. A full present targets the BACK page and rebuilds it from `s_chunky`. The
   viewport rows there are **frozen stale by design** — ADR-0016 B2.1 renders
   the 3D view into the planar scratch and never into chunky (confirmed
   earlier: `FRUA_R3DEXTENT` measured `render_3d_faithful` writing ZERO chunky
   rows).
2. `st_vp_composite()` then overlays the real viewport onto that page and
   clears the flag. Flip. Correct frame on screen.
3. The NEXT full present targets the OTHER page, rebuilds it from that same
   stale chunky — and finds the composite already spent. Flip.
4. **That page shows the previous 3D frame** until a new commit arrives.

Which is exactly what the capture showed: the floor band and the stars
reverting for one frame and coming back, everything else identical, because the
stale chunky under the viewport holds the PREVIOUS 3D frame and only those
pixels differ between the two.

**The fix** is the idiom already used next door for `s_force_full`, which its
own comment describes as "a COUNT of pages still owing the treatment (set to
NPAGES on init/re-band, decremented as each present consumes one)". The
viewport needs the same debt tracking — but **per page, not a count**, because
`st_present_rect` composites the SHOWN page repeatedly without flipping and a
plain counter would let it consume the other page's credit.

    s_vp_owe[NPAGES]   pages that still need this commit's composite
    s_vp_have          the scratch holds a valid rect

`st_vp_commit` sets both; `st_vp_composite` clears the entry for whichever page
`s_screen` currently names (the back page for a full present, the shown one for
a rect); and the **force-full re-owes both**, because it rewrites both pages
from `s_dt` — which does not carry the composite, since the composite writes
the PAGE, not the accumulation buffer. `s_vp_active` keeps its exact former
meaning for its other three consumers (the re-band quant overlay, the
`content_same` veto, the rect-inside-viewport skip) so nothing else moves.

**Verified:**

| check | before | after |
|---|--:|--:|
| door region, consecutive frames | 2804 -> 2804 -> 0 (change and revert) | **0 across 9 frames** |
| settled door frame vs the old build | — | **0 differing pixels** |
| boot in-present / band / pass1 / force-full | 14596 / 6522 / 2526 / 4801 | 14586 / 6523 / 2527 / 4801 |

The settled frame being byte-identical is the important one: the change removes
a transient and alters nothing else. Boot is untouched because no viewport is
active there. In play the cost is bounded by one extra composite per commit
(~150 t200) — and that extra composite is precisely the work that was MISSING,
which is why the second page was showing a stale frame.

**On the earlier framing.** This is what the whole "extra redraws" thread was
actually about, and it is a CORRECTNESS bug, not a throughput one — which is
why the throughput attack (the rect conversion) failed while this succeeds. It
also affects every double-buffered target, not just the STE.

### ★★★ ST/STe: IT WAS NEVER THE ENGINE — THE SOFTWARE SYNTH IS ~70% OF WALL

Asked to move from display work to "engine performance", the first job was to
find out where the engine's time actually goes. Nobody had ever profiled the
CPU on this target — every measurement in #63 was a display phase timer.

**Method.** Hatari 2.6.1 has a CPU profiler, but over the command FIFO every
debugger entry frees and re-allocates its buffers, so `profile stats`,
`profile counts` and `profile symbols` all report an empty window (the data IS
being collected — `r` shows a live per-instruction `3.44% (521487, ...)`
annotation — it just cannot be listed this way). Fell back to the recipe this
project already had: sample the PC repeatedly, map to symbols.

90 samples, HEIRS play loop, mapped through the load base **0x18872** (derived
twice from `$_exception_handler` and `.early_init`, both agreeing):

| function | hits | share |
|---|--:|--:|
| **`plat_sound_vbl`** | 50 | **55.6%** |
| `__mulsi3` (software 32-bit multiply) | 19 | **21.1%** |
| `qd_nearest_color` | 7 | 7.8% |
| `st_present` | 6 | 6.7% |
| `__udivsi3` | 2 | 2.2% |
| everything else | 6 | 6.6% |

Note what is NOT there: the whole display path — `st_present`,
`dc_plane_bridge_*` and `cursor_composite` together are **~10%**, consistent
with the 13.6% measured by the phase timers, and confirming that work is done.

**The control, because 90 samples is 90 samples.** `FRUA_NOSOUND`
(release-guarded) makes `plat_sound_init` return -1 so the machine runs silent.
Same drive, same seed, identical harness, back to back:

| arm | boot | 36 keys |
|---|--:|--:|
| sound ON | 13 s | **223 s** |
| sound OFF | 6 s | **67 s** |

**3.3x faster overall, 2.2x faster to boot.** (223 - 67) / 223 = **the software
synth is ~70% of ST/STe wall time.** The PC samples said 55.6% in the handler
itself; the rest is the `__mulsi3` it calls, which is why that entry is second.

**Why it costs so much.** `plat_sound_vbl` keeps a 2048-sample ring half full,
which at the STE's 25033 Hz means **~410 samples rendered per VBL frame**, each
one four-tone synthesis in software — on a CPU with no 32x32 multiply, so every
voice step goes through `__mulsi3`.

**This retires the framing of the last several sessions.** The ST/STe was never
display-bound and is not "engine-bound" in the sense of game logic either. Two
concrete levers, both untried:

1. **Halve the sample rate.** The STE DMA supports 12517 Hz (rate code 1) as
   well as the 25033 Hz currently selected — half the samples per frame, for a
   period-appropriate loss of fidelity on an 8 MHz machine.
2. **Get `__mulsi3` out of the inner loop.** 21% of the play loop is a libgcc
   software multiply the 68000 needs because the synth works in 32-bit; a
   16x16 `muls.w` formulation would cut most of it.

Neither is attempted here — this entry is the measurement, and the measurement
is that the target was in the wrong subsystem entirely.

## #63 postscript: the synth was rendering silence, 26.6 million samples of it

The PC-sampled profile put `plat_sound_vbl` at 55.6% of the ST/STe play loop
and `FRUA_NOSOUND` priced the whole subsystem at ~70% of wall (223 s -> 67 s
over the same drive). That measurement was right and the conclusion drawn from
it — "the four-tone synth is expensive, so either lower the sample rate or
rewrite the inner loop" — was wrong. Both proposed fixes traded something
(audible fidelity, or faithfulness of the mixer) against a cost that did not
need to exist at all.

`FRUA_SNDPROF` buckets every rendered sample by the state it was rendered in,
accounted per render CALL (~50/sec) rather than per sample, so the instrument
cannot distort what it measures. A boot + 36-key HEIRS walk:

```
b63snd: render calls  = 76831
b63snd: samples SILENT= 26633924      99.976%
b63snd: samples VOICED= 0            <- no voice was EVER armed
b63snd: samples tone  =    6334       0.024%  (one UI beep)
b63snd: samples sfx   =       0
```

Not "mostly idle" — **entirely** idle. The four-tone synth never had a voice
armed for the whole run, and the only audible samples in the entire drive were
a single beep. Every one of the other 26.6 million cost the per-sample loop
(three state tests, two clamps, a store) on a CPU with no cache.

The mechanism is that the DMA ring loops forever, so the vblank refill runs
whether or not anything is audible — ~410 samples every frame at 25033 Hz,
regardless. Silence is not free; it is synthesised at the same price as music.

**The fix is to notice that a ring full of zeroes can be looped indefinitely
and still play silence.** `synth_audible()` hoists the audibility test out of
the per-sample loop; `g_quiet_run` counts silence already committed, and once
a whole ring of it is down the refill stops until something makes noise. Free:
no rate change, no fidelity loss, no change to the mixer's faithfulness.

Three traps on the way in, all of which would have shipped a broken audio path:

1. **Returning early skips the sequencer hook.** The hook is what ARMS the
   voices, so an early `return` is a silence that can never end. The gate must
   skip the REFILL, not the function.
2. **The write pointer must keep its half-ring lead.** Leaving `g_ring_w` where
   it was makes `lead` wrap on the next audible frame, `todo` go negative, and
   the refill never run again. It is re-anchored to `play + RING_SAMPLES/2`.
3. **`g_quiet_run` has to reset on the way OUT of quiet, not only inside the
   loop.** If the loop body is skipped (todo <= 0) while audible, a stale
   quiet-run would gate the very next silent frame before a ring of zeroes had
   been written — looping the last fragment of sound forever.

Verified with `FRUA_SNDTEST` (four effects + a 12-second song) under the gate:

```
sndtest: song voices live = 3
b63snd: samples VOICED= 76510     b63snd: samples sfx = 79224
b63snd: samples SILENT= 12624     <- just the one ring-fill it takes to latch
```

Audio fully intact; silence rendering down ~1600x. Boot-to-menu on the STE
(same `start` path, fast-forward on in every arm):

| arm | boot |
|---|--:|
| before | 13 s |
| `FRUA_NOSOUND` (whole subsystem off) | 6 s |
| **gated** | **6 s** |

The gate recovers the entire cost the subsystem was imposing — it is now as
cheap as not having sound at all, while still having sound.

**The play-loop figure is NOT measurable by wall clock under fast-forward.**
The post-fix 36-key drive completed in 12 s, but the driver's own keystroke
pacing is ~0.3 s x 36 ~= 11 s of host sleep, so that drive is sleep-bound and
saturates the instrument. The honest claim is the boot number plus the
mechanism; a play-loop figure needs the emulated-time (t200) phase counters,
not `date`.

Two levers named in the previous commit are now RETIRED, not deferred: halving
the STE sample rate and reworking `__mulsi3` out of the synth inner loop were
both aimed at making it cheaper to render samples that should never have been
rendered. `__mulsi3` at 21% of the profile remains unattributed and is NOT the
synth — the silent path contains no multiply. That is a separate lead.

## #96 re-baseline: the display layer is 3.7% of the ST/STe play path

Every share figure in the #63 sections above was measured against a wall that
was mostly software synth. With the silence gate in, those denominators are all
wrong, so the whole play loop was re-measured in EMULATED time (t200) with the
gate as the only variable. `FRUA_SNDNOGATE` restores the old behaviour; the
arms' `display_ste.o` is byte-identical (`objdump | md5sum`), so nothing in the
display path differs between them.

Drive: HEIRS via `FRUA_AUTOPLAY + FRUA_AUTOWALK + FRUA_AUTOWALK_TREASURE`,
`FRUA_RNGSEED=12345`, three `b63play` windows of 8 rect presents each. The walk
script was extended from 6 steps to 18 for this: six steps yield exactly ONE
window, and that window's `wall` starts at the first rect present, so it spans
the whole modal intro and reports the walk's share against a wall that is
mostly not walking. Windows 2 and 3 are pure walk.

| window | arm | rect t200 | composite t200 | wall t200 | display per 1000 |
|---|---|--:|--:|--:|--:|
| 1 (spans intro) | gate OFF | 1081 | 7373 | 554726 | 1 |
| 1 | **gate ON** | 416 | 2854 | **223317** | 1 |
| 2 (pure walk) | gate OFF | 1229 | 5524 | 164881 | 7 |
| 2 | **gate ON** | 476 | 2196 | **65444** | 7 |
| 3 (pure walk) | gate OFF | 1237 | 4486 | 74037 | 16 |
| 3 | **gate ON** | 477 | 1669 | **29020** | 16 |

**1. The gate is worth ~2.5x on the PLAY loop, not just the boot.** 2.48x,
2.52x, 2.55x across three independent windows — the boot's 13 s -> 6 s was not
a boot-specific effect. Window 1 also reproduced to within 0.05% of a separate
earlier run (416 vs 418, 2854 vs 2852), which is the determinism control.

**2. ★ EVERY PHASE TIMER IN THIS FILE HAS BEEN MEASURING THE SYNTH.** The
display code did not change between arms — the object is byte-identical — yet
`rect t200` fell 1237 -> 477 and `composite t200` 4486 -> 1669, both ~2.6x, the
same factor as the wall. The timers bracket a region with two `_hz_200` reads,
and the synth's vblank fires inside that bracket and is charged to whatever was
running. So the historical figures ("in-present 32.5% of play", "pass 1 is 54%
of a present") were part display, part interrupt load, in an unknown mix. They
are not wrong about ORDERING, but their absolute values cannot be carried
forward. **`display per 1000` is identical in both arms (1, 7, 16)** precisely
because the interrupt was charged proportionally to everything.

**3. The whole display layer is 3.7% of the play path.** Summed over the gated
run's three windows: wall 317781 t200, full presents 3575 (48 of them), rect
presents 1369, composites 6719 — **11663 t200 total, 3.7%** (an upper bound:
any composite running inside a rect present is counted twice).

**So the two remaining pass-1 levers are RETIRED.** Dropping the shadow compare
and having writers note `!s_used_idx[c]` at stamp time both target pass 1,
which lives inside FULL presents — and full presents are 3575 t200, **1.1% of
the play path**. Perfect execution of both is worth well under one percent. The
same applies to any further c2p or banding tuning: there is nothing left to win
on the ST/STe display path.

**Where the other 96% goes is now the open question**, and it is engine work,
not display work. The `__mulsi3` 21% from the PC-sampled profile is part of it
and remains unattributed — it is not the synth (the silent path contains no
multiply) and it is not the display (3.7%).

**Method notes worth keeping:**
- **`st_prof_hot_dump` does not fire after the boot** (its window is 16 FULL
  presents), so any counter read from it post-menu is STALE. Two readings
  agreeing is not evidence of anything. `b63play` is the play-loop instrument;
  `plat_sound_prof_dump` is now called from BOTH so the walk reports too.
- **Scripted arrow keys through `driver.sh` do not walk HEIRS.** The entry
  chain is modal (caravan messages, then a treasure screen that ignores
  Return), so the keys are eaten and the drive silently samples menus. Use the
  autoplay array, which clears the chain first — that is what it exists for.
- **A per-window `wall` starts at the FIRST rect present**, so window 1 always
  spans whatever preceded the walk. Read windows 2+ for walk figures.

## #96 part 2: 85% of every software multiply came from ONE line

The PC-sampled profile put `__mulsi3` at 21% of the ST/STe play loop, but a
leaf sample names the callee. `__mulsi3` has **253 static call sites** (207 in
the lifted `boot.c`), so "which multiply" was unanswerable from sampling, and
libgcc's routine is already optimal for a 68000 — three `mulu.w`, eleven
instructions. There is nothing to win inside it. The only available win is to
stop CALLING it.

`platform/mulprof.c` (`FRUA_MULPROF`) is a return-address histogram over every
software multiply. Two implementation notes that cost a round each:

- **Defining `__mulsi3` ourselves does not link.** libgcc's `_mulsi3.o` is
  pulled in regardless and the link dies on `multiple definition`. Use
  `-Wl,--wrap=__mulsi3`: call sites go to `__wrap___mulsi3`, and
  `__real___mulsi3` still does the arithmetic — so the instrument provably
  cannot change a result.
- **★ `__builtin_return_address(0)` RETURNS 0 UNDER `-fomit-frame-pointer`,**
  which the whole build uses. The first run recorded a return address of zero
  for every call — and because zero was also the table's "empty" sentinel, the
  dump *skipped* the slot holding all the mass. The histogram looked plausible
  and simply did not add up: 2.9M total calls against a top-24 summing to 13k.
  Fixed with a per-object `-fno-omit-frame-pointer`, a separate `mp_used[]`
  occupancy flag, and a `ZERO-ret` counter that would have named the fault
  immediately. **A histogram that does not sum to its own total is telling you
  it is broken — check that before reading the ranking.**

HEIRS drive, 2,906,530 software multiplies, collisions 0.11%, top 24 covering
99.9%. Aggregated by real function (compiler-local `.LBB*` labels filtered out
of `nm` — leaving them in maps every site to a meaningless label, the same trap
as the PC-sample pass):

| function | calls | share |
|---|--:|--:|
| **`qd_nearest_color`** | **2,479,104** | **85.3%** |
| `render_3d_faithful` | 271,040 | 9.3% |
| `DrawChar` | 90,524 | 3.1% |
| `st_reband` | 28,326 | 1.0% |
| `qd_pixmap_fill` | 25,019 | 0.9% |
| everything else | ~11,000 | 0.4% |

`qd_nearest_color`'s three sites are 10 bytes apart — **one expression**:

```c
long d = dr * dr + dg * dg + db * db;   /* three __mulsi3 CALLS */
```

256 palette entries per lookup, three multiplies each: 768 software multiplies
per colour resolved, 9,684 lookups in the drive.

**Fix: a 511-entry table of squares, indexed by `delta + 255`.** The operands
cannot leave a byte's range, so the products cannot exceed 65025. A table
rather than `muls.w` for two reasons: a 16x16 multiply is still ~40-70 cycles on
a 68000 against ~14 for an indexed word read, and a table cannot be silently
undone by GCC failing to match the `mulhisi3` pattern. Plus an early exit on an
exact match — the original loop's strict `<` means no later index could have
displaced a zero-distance hit, so it returns the same index the full scan would.

**Equivalence proved before measuring**: a host harness runs both
implementations over 400 random palettes x 2000 queries, one query in four
forced onto an exact palette entry, plus all-black / all-white degenerate
palettes — **800,512 trials, 0 mismatches**. `qd_nearest_color` now contains
zero multiply instructions of any kind.

Same drive, same flags as the baseline above:

| window | before | after | |
|---|--:|--:|--:|
| 1 (spans intro) | 223,317 | 174,552 | **−21.8%** |
| 2 (pure walk) | 65,444 | 54,169 | **−17.2%** |
| 3 (pure walk) | 29,020 | 25,581 | **−11.9%** |
| *rect t200 (control)* | *416 / 477 / 477* | *415 / 477 / 477* | *unchanged* |
| *composite t200 (control)* | *2854 / 2196 / 1669* | *2844 / 2200 / 1674* | *unchanged* |

**−20% of the play path from one line**, and the two display phases that should
be unaffected are unaffected to within 0.4% — which is what makes the headline
number trustworthy rather than a re-measurement artefact.

Note this also explains `qd_nearest_color`'s 7.8% of the original PC samples:
it was both the top consumer of multiplies AND hot in its own right. The two
findings were the same function seen from two directions.

**Still live: `render_3d_faithful` at 9.3% and `DrawChar` at 3.1%** of the
multiplies. Smaller, and both are single sites rather than a 256-iteration
loop, so the ceiling is lower — but they are now the top of the list.

### The build stamp was blind to a flag SWAP (found the hard way)

The confirmatory `FRUA_MULPROF` re-run produced no `b96mul` output at all, and
the reason is worth more than the run: `BUILDSTAMP` summarised `EXTRA_CFLAGS`
as `$(words ...)$(firstword ...)` — a flag COUNT and the FIRST flag. Swapping
`-DFRUA_SNDPROF` for `-DFRUA_MULPROF` keeps both, so the stamp did not change,
nothing was purged, and — because no source file had changed either — make
rebuilt nothing. `make` reported success and the "new" binary was the old one.

**A flag-only change is invisible to make.** The stamp exists precisely to
catch that, and this hole let it through. It now checksums the whole
`EXTRA_CFLAGS` string.

Audit of every A/B pair in this session against the hole, since a silent
wrong-binary run would invalidate a result:

| pair | stamps | verdict |
|---|---|---|
| gated vs `SNDNOGATE` | 6/`-DFRUA_STPROF` vs 7/`-DFRUA_STPROF` | differ → purged ✓ |
| baseline vs nearest-colour fix | identical | but the change was a SOURCE edit, which make tracks normally ✓ |
| nearest-colour vs MULPROF re-run | identical, and no source edit | nothing rebuilt ✗ |

Only the confirmatory run was affected; the −20% measurement was driven by a
source edit and stands. **The tell was `mulprof.o` at 152 bytes** — an object
holding nothing but an `#ifdef` that did not fire. Check the artefact, not the
`make` exit status.

### Confirmation: the multiply count fell by exactly the attributed amount

Re-run with `FRUA_MULPROF` after the fix (and after the BUILDSTAMP repair, so
the binary is actually the new one — `mulprof.o` at 3022 bytes, not 152):

```
before      2,906,530
predicted   2,906,530 - 2,479,104 = 427,426
MEASURED                            427,341     (0.02% off)
```

−85.3%, and the residual matches the attribution to within noise. That is the
strongest form of confirmation available here: the histogram predicted the
post-fix total before the fix was measured, and it was right.

The remaining 427k, re-ranked:

| function | calls | share of what's left |
|---|--:|--:|
| `render_3d_faithful` | 271,040 | 63.4% |
| `DrawChar` | 90,524 | 21.2% |
| `st_reband` | 28,326 | 6.6% |
| `qd_pixmap_fill` | 25,019 | 5.9% |

Whether any of these is worth doing is now a smaller question than it looks:
the whole software-multiply population is 1/7 of what it was, so even
eliminating `render_3d_faithful`'s share entirely is worth roughly a seventh of
what `qd_nearest_color` was. Measure before building.

## #117 THE ATARI-WINS SWEEP AGAINST THE AMIGA BACKENDS

#116 found a **1.6x sitting unported for weeks** — Paula never got #96's
silence gate. That is a process failure, not a one-off: a fix lands on the
platform being profiled and nobody checks the other one. So every optimisation
from this campaign was walked against `platform/amiga/`.

| Atari win | Amiga status |
|---|---|
| **Silence gate (#96)** | **WAS MISSING → ported (#116), boot 203s → 125s, audio unchanged** |
| **`memcmp` → long-wise row compare (#63)** | **WAS MISSING → ported here (3 sites in `display_ecs.c`)** |
| `qd_nearest_color` squares table (#96 pt 2) | SHARED — `compat/quickdraw.c`, in the Amiga link line. Already had it |
| Dirty-row / draw-time planar (ADR-0016) | Already on ECS + AGA |
| #61 idle-present gate (`qd_dirty_any`) | SHARED — `boot.c` |
| `hw_palette` (#99) | **ECS is INELIGIBLE, AGA is a NO-OP — see below** |
| Timer-B raster split (#63) | ST hardware. The copper reloads per band for free; no analogue |
| Viewport composite (#63) | ST-specific mechanism (`s_vp_scratch` + `planar_blit_stlow`). Amiga has none — a design question, not an unported fix |
| AGA row-diffing vs a shadow | Deliberately not done, with reasons, in `display_aga.c` |

### `hw_palette` — eligible is not the same as useful

`display.h` says *"AGA looks eligible on the same argument but has NOT been
measured or verified here."* Walking it:

- **ECS is INELIGIBLE.** It quantises 256 colours to 32, so a plane value is a
  BAND SLOT, not the index. Changing the CLUT changes which slot a pixel should
  map to — the pixels genuinely do need re-rendering. `hw_palette = 1` there
  would be a correctness bug, not an optimisation. (It already has its own
  narrower answer: `ecs_repalette()` for the content-unchanged case.)
- **AGA is eligible and it would still do nothing.** 8 planes = 256 colours,
  remap is the identity, palette is in the copper — the TT's argument exactly.
  But `aga_present` never consults the dirty-row list: it walks all `AGA_H` rows
  unconditionally. The `qd_touch_all()` it would suppress costs AGA nothing, and
  the #152 clean-present skip is `pages == 1` only, which AGA is not.

**So the flag stays 0 on both, and now for a stated reason rather than for want
of measurement.** Anyone re-reading `display.h`'s "AGA looks eligible" should
stop here: the colour-model argument is sound, the work argument is not.

### The row compare: mechanism confirmed, win NOT measurable here

`memcmp` was the wrong primitive on the ST (93 cycles/byte against 30 for the
same compare written long-wise; swapping it HALVED the full present). ECS still
called it in three places — the present's row diff, the draw-time stamp check,
and the full-surface `content_same` test.

**Bebbo's `memcmp` is byte-wise too** — disassembled, it is the same
`moveb`/`moveb`/`cmpb`/`beq` loop as MiNTLib's — so the 3x mechanism is real on
this target and not an artefact of one libc.

**But boot-to-menu did not move: 125 s before, 125 s after.** That is expected
rather than disappointing, and this file already says why: the boot is the
REBANDS, not the row diffing, and the ST measured this fix as ~13% of *play*
time. There is no play-loop instrument on the Amiga (the phase counters are
`FRUA_STPROF`, Atari-only), and a scripted key drive is sleep-bound by the
driver's own pacing — the trap #96 recorded.

**Kept on the ST's own precedent for its `content_same` swap: semantically
identical, strictly cheaper in the worst case, not a win worth claiming.**
Verified pixel-identical (0 of 408960, two independent frames) so it is at
least provably free. A real figure needs an Amiga play-loop instrument, which
is the honest next step for anyone wanting to bank it.

★ **One build trap:** the helper first landed inside `#ifdef FRUA_PLANAR`,
where the ECS 68000 build (which implies it) linked fine and the **AGA 020
build did not** — two of the three call sites are outside that block. `static`
plus a missing definition reads as `undefined reference`, not a compile error.
Build every target, not the one you are measuring.

## #119 THE PROFILER WORKS — AND ITS FIRST RESULT CONTRADICTS THE 3.7%

**The blocker was never a broken Hatari build. `profile` over the command FIFO
silently collects NOTHING**, because `hatari-debug <cmd>` executes out-of-band
**without entering the debugger**, and Hatari commits its profile working set
only on a real debugger entry (`help profile`: *"Data is collected until
debugger is entered again"*). Every query that way returns `0 CPU addresses
listed` while still printing a plausible total time — which reads exactly like a
build compiled without profiling support. It is not: **profiling a bare TOS
boot through a BREAKPOINT shows activity in ROM (`0xe00034-0xe15a2a`) and none
in RAM**, which is the right answer, on the same binary. `b` over the FIFO does
not register either; breakpoints must come from `--parse` or another
breakpoint's `:file` script.

Recipe, now `tools/profile/st_profile.sh` + `st_aggregate.py`: two pre-armed
breakpoints bracket the window, each with an attached `:file` command script —
`profile on ; c` to open, `profile cycles N ; c` to close and dump. Aggregate
per-address rows to functions with `nm`, **filtering `.L`/`.LBB`/`.LBE` labels
and `*.o` markers** — left in, they swallow the ranking (`.LBB429 29.6%`), the
same trap as the #96 multiply histogram. Hatari's own `profile symbols` is not
a substitute: it lists only addresses sitting exactly ON a symbol, i.e. entry
points, and a 25-symbol request returned ONE row.

### The first real play-loop profile (STE, 30000-VBL window, 16 walk steps)

| function | share of ranked cycles |
|---|--:|
| `st_present` | **30.1%** |
| `qd_nearest_color` | **25.7%** |
| `dc_plane_bridge_span` | 11.0% |
| `__udivsi3` | 5.5% |
| `qd_planar_bridge_rect` | 4.8% |
| `qd_pixmap_fill` | 4.6% |
| `render_3d_faithful` | 2.5% |
| `jt200_layer` | 2.3% |
| `__divsi3` / `__mulsi3` | 1.8% / 1.7% |

**★ THIS DISAGREES WITH "#96: the display layer is 3.7% of the play path" BY AN
ORDER OF MAGNITUDE.** Summing the display work here — `st_present` +
`dc_plane_bridge_span` + `qd_planar_bridge_rect` + `st_c2p8` — gives **~47%**.
Both numbers cannot be right and the disagreement must be settled before either
is used to decide anything:

- the 3.7% came from `FRUA_STPROF` PHASE TIMERS, which bracket regions with two
  `_hz_200` reads. #96 itself recorded that those brackets charge whatever
  interrupt fires inside them to the bracketed region, and that the historical
  figures were "part display, part interrupt load, in an unknown mix";
- this figure is a per-address CYCLE COUNT with no bracketing and no
  attribution guesswork, but its denominator is one window on one drive, and
  the window mixes idle play-screen presents with 16 walk steps.

**Nobody should quote the 3.7% as settled again until this is reconciled** —
and the reconciliation is cheap now that the instrument works.

### The other surprise: `qd_nearest_color` is still 25.7%

#96 fixed its `dr*dr+dg*dg+db*db` (85% of ALL software multiplies, −20% of the
play path) with a squares table. That removed the MULTIPLIES; **the function is
still a 256-entry linear scan per lookup**, and it is now the second-largest
consumer in the play loop. The right next question is not "make the scan
faster" but "why is it called so often" — the same shape as the multiply find,
where the win came from removing the calls rather than optimising the callee.

### #120 RECONCILED — "3.7%" NEVER MEASURED THE DISPLAY LAYER

Both instruments, same build, same drive, two windows.

| | `b63play: display per 1000` | cycle profile: display + conversion |
|---|--:|--:|
| **walk-phase window** | **2–8** (0.2–0.8%) | **~71%** (`qd_pixmap_fill` 21.8 + `qd_planar_bridge_rect` 19.9 + `c2p4st_32` 16.1 + `st_c2p_span` 9.4 + `st_present` 4.4) |
| **idle-at-play window** | **44–45** (4.4%) | **~75%** (`st_present` 24.6 + `c2p4st_32` 23.4 + `st_c2p_span` 20.2 + `dc_plane_bridge_span` 7.0) |

**The counter swings 20x between the two windows while the cycle profile barely
moves. It is not measuring what its name says.** Its source is one line:

```c
dbg_log_num("b63play: display per 1000= ", (sp_rect_t * 1000L) / wall);
```

`sp_rect_t` accumulates time inside **`st_present_rect` ONLY**. It excludes
full presents (`st_present`), and it excludes `qd_pixmap_fill`,
`qd_planar_bridge_rect` and the c2p entry points, which are where the cycles
actually are. So the figure means "**the rect-present path's share of wall**",
not "the display layer's share of play" — and on the walk that path is a small,
highly variable slice.

**#96's 3.7% was a different, better sum** — done by hand across three windows
(`full presents 3575 + rect 1369 + composites 6719 of 317781 wall`) — so it did
include full presents. But it landed at 3.7% because that drive contained only
**48 full presents**, and it still omits the fill/bridge work. It was never
wrong about what it added up; it was wrong as a statement about the play loop.

**CONSEQUENCE: "the ST/STe display path is finished, there is nothing left to
win" is RETRACTED.** It rested on 3.7%. Display and conversion are the dominant
cost in both windows measured here. The `#96` claim that the two remaining
pass-1 levers are "worth well under one percent" was derived from the same
denominator and needs redoing.

**Caveats, stated so the next person can attack them:** each window is one run;
the ranked addresses are the top 400 by cycles, not the full set; and 12–19% of
ranked cycles are TOS ROM, now reported separately rather than silently folded
into the last program symbol (`__etext`) — which is what the first cut of
`st_aggregate.py` did, and it read as 12–20% of "our" time.

### #121 `qd_nearest_color` — the obvious caller is NOT the caller

The cycle profile puts `qd_nearest_color` at **26–34% of the ST play loop**,
second only to the present, and #96's squares table did not touch that: it
removed the MULTIPLIES from the distance term, leaving a 256-entry linear scan
per lookup.

**The obvious suspect was `qd_rebake_color_pointer`** — 16 × `qd_nearest_color`
= 4096 distance evaluations per call, invoked unconditionally from
`qd_set_palette`, which #99 measured firing **more than once per present** (521
touch_alls in 480 presents on the TT). `display_ecs.c` already carries an
"identical CLUT would reproduce identical bands" guard for the engine's
defensive re-installs, so hoisting that test into the shim looked free.

**Measured A/B, one flag apart, identical windows, no key injection:**

| arm | `qd_nearest_color` cycles |
|---|--:|
| gate OFF | 1,192,704,348 (33.8%) |
| gate ON | 1,192,912,620 (33.8%) |

**Identical to 0.02% — the guard does nothing, so it was reverted rather than
shipped with a comment claiming it targeted a third of the play loop.** Either
the engine's `qd_set_palette` traffic genuinely carries a changed CLUT every
time, or the rebake is simply not where the calls come from. The remaining live
callers are `RGBForeColor` and `RGBBackColor` (`compat/quickdraw.c` ~2502/2515),
one scan each, called per colour change during text and UI drawing — which fits
an idle play screen repainting its HUD. The PICT-decode sites are not on the
play path.

**Next step is an instrumented count, not another guess.** A return-address
histogram over `qd_nearest_color` (the `FRUA_MULPROF` pattern) would name the
caller in one run — and note that pattern's own trap: `__builtin_return_address`
returns 0 under `-fomit-frame-pointer`, which the whole build uses.

★ **Method note worth more than the negative result: the first before/after
here was invalid** and looked like a 28% REGRESSION. The two runs differed in
whether xdotool keys were injected, and under fast-forward host-paced keys land
at unpredictable emulated times. Any A/B on this instrument must hold key
injection constant — or inject none, as the arms above do.

### #122 THE HISTOGRAM NAMES IT: `qd_rebake_color_pointer`, 87.5% of calls

`FRUA_NCPROF` (in `compat/quickdraw.c`, modelled on `platform/mulprof.c`) is a
return-address histogram over `qd_nearest_color`. All three of mulprof's
self-checks pass, which is what makes the ranking readable: **histogram sum ==
total calls (60000), 0 collisions, 0 ZERO-ret**.

| caller | calls | share |
|---|--:|--:|
| `qd_rebake_color_pointer` | 52,502 | **87.5%** |
| `cursor_composite` (+0xfc) | 3,749 | 6.2% |
| `cursor_composite` (+0x114) | 3,749 | 6.2% |

**★ THIS DOES NOT CONTRADICT #121 — IT EXPLAINS IT.** #121 added a "skip the
rebake if the CLUT did not change" guard and measured NO difference, and
concluded the rebake was not the caller. It is the caller; the guard simply
never fired, because **the CLUT genuinely changes on essentially every
`qd_set_palette`**. Both measurements are true and the pair is more informative
than either: the traffic is real, not defensive re-installs.

So the cost is intrinsic to the current design: **16 lookups x a 256-entry scan
= 4096 distance evaluations per rebake, ~3,281 rebakes in the window**, i.e.
~13.4M scan iterations. Deferring the rebake to `cursor_composite` would NOT
help — 3,281 rebakes against 3,749 composites is already ~1:1.

**The lever that remains is incremental rebaking.** `qd_set_palette` takes
`(first, count)`, so a partial CLUT write only invalidates a cursor colour's
answer if a CHANGED entry is now nearer, or if that colour's previous best was
itself among the changed. That turns 16x256 into 16xcount plus a rare full
rescan. **Check `count`'s real distribution first** — if the engine always
writes all 256, the idea dies, and that is one counter, not a rewrite.

★ Two traps in building the histogram, both caught before it ran:
`__builtin_return_address(0)` read inside the helper yields an address inside
`qd_nearest_color` itself (every call lands on one slot, "it calls itself") —
take it in `qd_nearest_color` and pass it in. And the builtin returns 0 under
`-fomit-frame-pointer`, which the whole build carries, so the Makefile adds
`-fno-omit-frame-pointer` to that one object under `FRUA_NCPROF`.

### #123 `count` DISTRIBUTION — 98% of palette writes touch <= 16 entries

The one counter that decides whether incremental rebaking is worth building:

| `qd_set_palette` calls | 3,279 |
|---|--:|
| `count == 256` (full) | **15** (0.5%) |
| `count <= 16` | **3,214** (98.0%) |
| `count <= 32` | 36 |
| `count <= 64` | 3 |
| `count <= 255` | 11 |
| **mean count** | **8** |

**The rebake scans 256 entries x 16 cursor colours = 4096 distance evaluations
when, on average, EIGHT palette entries changed.** Nearly all of that work is
re-deciding answers that provably cannot have moved.

**The incremental form is exactly equivalent, not an approximation.** Keep
`best_idx[c]` and `best_d[c]` per cursor colour. On a write of `[first,
first+count)`:

- if `best_idx[c]` is OUTSIDE the range, that entry's colour did not change, so
  `best_d[c]` is still valid — only the changed entries can beat it, and
  comparing the 16 colours against `count` entries is the whole job;
- if `best_idx[c]` IS inside the range, its distance may have grown, so that
  one colour needs a full rescan.

At mean count 8, a colour's best falls in the changed window ~3% of the time,
so a typical call costs `16*8` plus about half a full rescan — **roughly 256
evaluations against 4096, a ~16x cut** on a function measured at **26-34% of
the ST play loop**. Worth building, and the equivalence is provable rather than
empirical, which is the right property for something in the cursor path.

NOT BUILT HERE — this entry is the go/no-go measurement only.

### #123b BUILT — the incremental rebake, 15.6x measured

`qd_rebake_range(first, count)` replaces the unconditional full rebake. It
keeps `s_bake_idx[16]` / `s_bake_d[16]` across palette writes and, when the
write is partial, compares the 16 cursor colours against only the CHANGED
entries — falling back to a full rescan for any colour whose own chosen index
was inside the written range.

**Equivalence proved before measuring** (`FRUA_REBAKEVERIFY` recomputes all 16
from scratch after every incremental update and compares): **24,000 checks, 0
mismatches**. The verifying build keeps the full answer, so it stays correct
even if the incremental path is wrong — the same discipline as #96's squares
table.

**★ THE TIE RULE WAS THE TRAP.** `qd_nearest_color` uses strict `<`, so it
keeps the LOWEST index among equal minima. Comparing a changed entry with
`d < best_d` alone keeps the OLD index on a tie even when the new one is lower,
which diverges from a full scan. Hence the explicit
`(d == best_d && i < best_idx)` arm.

**Measured A/B, one flag apart (`FRUA_NOINCREBAKE`), identical windows:**

| arm | rebake cycles | share |
|---|--:|--:|
| full rebake | 1,173,709,304 | **33.4%** |
| incremental | 75,113,050 | **2.2%** |

**15.6x, against a predicted ~16x.**

★ **Read the totals correctly.** The window is fixed in VBL, i.e. in TIME, so
total cycles are ~constant by construction (3.51G vs 3.37G) and the freed work
shows up as REDISTRIBUTION — `st_present`'s share rises to 58.7% because the
denominator shrank, not because it got slower. **A wall-clock claim needs a
work-boxed drive** (time to complete a fixed sequence), not this time-boxed
one. What is established here is that the rebake's own cost fell 15.6x and
~31 points of the play loop were freed for other work.

★ **And a harness trap that produced a wrong number first:** `ab_run.sh`
REBUILDS, so aggregating the OFF log against the binary left behind by the ON
build mis-attributes every address — it read 14.3% instead of 33.4%. Aggregate
each arm's log against ITS OWN binary; `st_aggregate.py` takes the binary as
its second argument for exactly this reason.

## #124 THE BOOT, PROFILED — flat, and division is the top item

86 s on an 8 MHz STE is the most user-facing number in the project and it had
never been profiled with a trustworthy instrument (the old boot analysis used
the phase timers #120 discredited). Window: program start to `menu: modal up`.

| function | share of program cycles |
|---|--:|
| `__udivsi3` | **10.6%** |
| `ui_glib_blit` | 10.0% |
| `jt1007` | 8.3% |
| `st_reband` | 7.8% |
| `plat_ticks` | **5.8%** |
| `l112c` | 5.5% |
| `qd_planar_bridge_rect` | 5.2% |
| `GetNextEvent` | 5.0% |
| `fill_common` | 4.4% |
| `unpackbits` | 3.9% |

594.8M program cycles (74.2 s) plus **24.5% outside the program** — TOS, i.e.
GEMDOS file I/O, which is expected for a load-heavy phase and mostly not ours.

**The boot is FLAT.** No single item dominates the way `qd_nearest_color` did
in the play loop; the cost is spread across art decode (`ui_glib_blit`,
`unpackbits`, `fill_common`), the quantiser (`st_reband`) and division. That
matters for expectations: there is no one change here worth 15x.

**Two items stand out as worth chasing anyway:**

1. **`__udivsi3` at 10.6%** — software DIVISION, and the exact shape of the
   `__mulsi3` find that became `qd_nearest_color` and −20% of the play path.
   The playbook already exists: `platform/mulprof.c` + `-Wl,--wrap`, which
   names the call site in one run. libgcc's routine is not the problem; the
   calls are. **This is the highest-confidence next step in the project.**
2. **`plat_ticks` at 5.8%** — reading the system tick should not cost a
   twentieth of the boot. That smells like a spin/poll loop rather than real
   work, and it is cheap to check.

`st_reband` at 7.8% is the quantiser and was already known to dominate parts of
the boot; it is now sized honestly rather than by the phase timers.

### #125 THE DIVISION HISTOGRAM — half the divides are a DELIBERATE WAIT

`FRUA_DIVPROF` (`platform/mulprof.c`, the `__mulsi3` playbook aimed at
division) names every source-level divide by return address. Self-checks pass:
**0 ZERO-ret, 117 collisions on 56,145 calls (0.2%), top-24 sum 54,280.**

★ **WRAP ALL FOUR ROUTINES, NOT JUST `__udivsi3`.** libgcc builds `__divsi3`,
`__umodsi3` and `__modsi3` on top of it (`__modsi3` -> `__divsi3` ->
`__udivsi3`), and those inner calls are undefined symbols in their own objects,
so `--wrap` catches them too. Wrapping only `__udivsi3` would have attributed
most of the traffic to an address inside libgcc — true, and useless. A
`dp_depth` counter suppresses the nested record so each source-level divide is
counted exactly once (136 nested calls suppressed).

Mix: `__udivsi3` 29,650 / `__divsi3` 19,612 / `__modsi3` 6,845 / `__umodsi3` 38.

| call site | divides | share |
|---|--:|--:|
| `plat_ticks` (x2 sites) | 27,833 | **49.5%** |
| `dc_plane_px` (x3 sites) | 18,330 | **32.6%** |
| `qd_planar_bridge_rect` | 3,106 | 5.5% |
| `st_dt_ready_row` | 2,776 | 4.9% |
| `qd_pixmap_fill` (x3) | 960 | 1.7% |
| `dc_plane_bridge_span` (x3) | 576 | 1.0% |
| `st_reband` (x3) | 363 | 0.6% |

**★ THIS RETRACTS #124's OPPORTUNITIES 1 AND 2.** "`__udivsi3` at 10.6%" and
"`plat_ticks` at 5.8%" are the SAME cost, and most of it is not recoverable. A
caller count cannot tell a hot path from a busy wait, so a second histogram one
level up asks who reads the clock:

| `TickCount` caller | calls | share |
|---|--:|--:|
| `port_show_intro` (the title dwell) | 13,182 | **44.3%** |
| `WaitNextEvent`'s deadline spin | 12,793 | **43.0%** |
| `jt1091` (the per-VBL sequencer) | 1,819 | 6.1% |

**86.7% of every clock read is inside a loop that is DELIBERATELY WAITING.**
`port_show_intro` holds each title screen with `deadline = TickCount() + 240`
— ~4 s x 5 screens ~= **20 s of the 74 s boot is the intro dwelling on
purpose**, exactly as the original does. `WaitNextEvent` is a tight spin on
`GetNextEvent` + `TickCount` until its sleep deadline, and the counters show
**197 calls, 197 timeouts — not one returned an event** (~5.6 s).

Making those divides cheaper recovers **zero wall clock**: the loop is bounded
by a deadline, so a faster iteration simply spins more times. This is the
time-boxed-vs-work-boxed trap in a new costume — the profiler measures cycles,
and cycles spent waiting look exactly like cycles spent working.

So roughly **a third of the boot is deliberate delay** and the ~74 s figure
should be read as ~25 s of intentional waiting plus ~50 s of real work.

**What IS recoverable — `dc_plane_px`, 32.6% of all divides.** ADR-0016's
draw-time plane store does **three 32-bit divisions per text pixel**:

```c
sy   = (short)(off / dt->chunky_pitch);
sx   = (short)(off % dt->chunky_pitch);
band = (short)((long)sy * dt->nbands / dt->h);
```

`DrawChar` already knows the clip-tested `(x, y)` it just wrote — it recomputes
them from a pointer difference. Passing them in kills the first two outright;
the band is a per-row value that can be hoisted or cached rather than recomputed
per pixel. At 6,110 text pixels in the boot that is ~3.5% of boot cycles, and it
matters MORE in the play loop, where text is drawn constantly. This is the
division work worth doing; the clock is not.

### #125b BUILT — the row memo, 4.7x fewer mapping divides

`dc_map()` replaces the address->(sx, sy, band) arithmetic that
`dc_plane_px`, `dc_plane_fill` and `dc_plane_bridge_span` each open-coded. It
**memoises the chunky ROW**: all the pixels of one glyph row land on the same
row, so the answer is the same for every one of them, and the modulo
disappears outright (`sx = off - lo`). Per-pixel cost becomes per-row cost, and
a miss now costs 2 divides instead of 3.

| | divides |
|---|--:|
| `dc_plane_px` + `dc_plane_fill` + `dc_plane_bridge_span` (before) | 19,866 |
| `dc_map` (after) | **4,264** |
| whole-boot total | **56,145 -> 38,763 (-31%)** |

~17,400 divides x ~1,100 cycles ~= **19M cycles, ~3.2% of the boot**, and
proportionally more in the play loop where text is drawn constantly.

**★ THE MEMO KEY MUST INCLUDE `nbands` AND `h`, NOT JUST THE BASE ADDRESS.**
`band` is `sy * nbands / h` and `st_reband` changes `nbands` at an epoch reset
while `dt->chunky` stays put. Keyed on the base alone, every row would keep
serving the PREVIOUS banding's slot until it happened to change rows — a wrong
colour on exactly the frames a reband was supposed to fix.

**Proved, not eyeballed.** `FRUA_DCMAPVERIFY` recomputes all three the original
way on every call and logs any disagreement (the FRUA_REBAKEVERIFY shape from
#123b): **6,622 checks to the menu, 0 mismatches**, and 0 across a full
`FRUA_AUTOPLAY` drive into the in-game event screen — the path that actually
rebands. Three separate boots produced a **byte-identical** menu PNG
(md5 99bbb623), and the play frame renders correctly.

★ Two traps hit while verifying:

- **`make test` RESETS THE BUILD STAMP.** The stamp check is a top-level
  `ifneq` with a `$(shell ...)` side effect, so it runs on ANY make invocation
  — `make test` (no `CPU68K`) rewrites `.machine` to `falcon-default-...` and
  purges the 68000 objects. A `CPU68K=68000` binary must be re-made after it.
  The baseline screenshot was nearly taken against the 020 build, which the
  emulator caught only because the program prints "requires a 68020 or higher".
- **`objdump -D -b binary` on the raw .prg is not an arch check** — it read
  2,705 020-ops on a 68000 binary. Scan the OBJECTS
  (`objdump -d compat/quickdraw.o | grep -cE 'muls\.l|bfextu|bfins'` = 0).

### #125c THE PLAY LOOP, IN WALL CLOCK — a step is ~2.1 s, a recompose ~16-26 s

Every play-loop figure so far has been a SHARE of a time-boxed cycle window.
This is the first measurement in the guest's own clock: `FRUA_STEPPROF` +
`FRUA_AUTOPLAY -DFRUA_AUTOWALK -DFRUA_AUTOWALK_TREASURE`, driven to a real
walk in HEIRS (party stepping 10,8 -> 11,7, command bar up), 32 step samples.

**A walk step, 8 MHz STE (60 Hz TickCount, 16.7 ms each):**

| phase | ticks | wall |
|---|--:|--:|
| setup (`dungeon_view_setup` + wall groups) | 0-1 | ~0 |
| **render (`render_3d_faithful`)** | **101-114** | **~1.8 s** |
| present (viewport rect) | 20-21 | ~0.34 s |
| **step total** | **~128** | **~2.1 s** |

**A FULL recompose (jt312's full path — events, entry), 5 samples:**

| phase | ticks | wall |
|---|--:|--:|
| **chrome (`port_draw_play_frame` + setup, 11 FSOpen)** | **611-629** | **~10.3 s** |
| render | 101-109 | ~1.8 s |
| hud | 200-366 | ~3.3-6.1 s |
| present | 34-133 | ~0.6-2.2 s |
| **total** | **~950-1230** | **~16-20 s** |

Plus the one-off `l63c0` compose entering the dungeon: **1,564 ticks = 26 s**.

**★ SO THE HEADLINE IS NOT THE STEP — IT IS THE RECOMPOSE.** A step is ~2.1 s
and 84% of it is the 3D render, with the present only 16%. But every event
message and every screen change pays a ~16-20 s full recompose, of which
**~10 s is the chrome phase alone** (and it issues 11 `FSOpen`s, so part of it
is disk, not drawing). Pressing Return through HEIRS' caravan chain is the
slowest thing in the game and nothing had ever pointed at it.

**★ AND THIS REFRAMES `st_present` 59% / `dc_plane_bridge_span` 21%.** Those
are shares of a 30,000-VBL window with 16 walk steps in it — at ~2.1 s a step
that is ~34 s of stepping in a ~500 s window, so **the window is ~93% IDLE**
(the harness paces keys 6-7 s apart). The 59% describes where cycles go on an
idle play screen far more than during a step; measured inside a step, the
present is 16%, not 59%. Neither number is wrong — they cover different
windows, and the cycle window is not the one that matters to a player.

★ `FRUA_MONOPROF` COULD NOT BE USED ON A COLOUR BUILD AT ALL. Its declarations
in `platform/display_sthigh.c` sit inside an outer `#ifdef FRUA_BWMODE`, so a
colour build with it fails to compile (`s_mono_wrote` undeclared) — which is
why the play loop's wall clock went unmeasured for as long as mono has been
shelved. `FRUA_STEPPROF` is the colour-safe subset; `FRUA_MONOPROF` implies it.

★ **THE THIRD NESTED-GUARD TRAP TODAY**: `FRUA_AUTOWALK_TREASURE` is nested
inside `#ifdef FRUA_AUTOWALK`, so passing it alone is SILENTLY INERT — the
drive ran 8 keys instead of 48 and never left the event screen. Same shape as
the Makefile's `FRUA_NCPROF` block and `FRUA_MONOPROF` above. **Pass
`-DFRUA_AUTOWALK -DFRUA_AUTOWALK_TREASURE` together, and always check the key
count.**

### #125e THE CHROME PHASE — a solid fill was mirrored PER PIXEL

#125c put ~10.3 s of a ~17 s full recompose in the chrome phase. Drilling
down, each level was ~97-99% of the one above it:

    chrome phase        611-629 ticks
      port_draw_play_frame  611-620   (99%; load/clut/memset are 0/2/2)
        l67ca               611-620
          jt76                  381   -> jt103 291, pieces 1-4 92
          piece 9 (viewport)    152
          piece 21 + dividers    63
        jt103 -> jt1161 -> PaintRect -> qd_pixmap_fill

`jt103` is a SOLID BOX FILL of the ~304x176 panel: 4.85 s, ~725 cycles per
pixel. A diagnostic build that skipped the plane mirror entirely put it at
**11 ticks** — so `dc_plane_fill`'s per-pixel `DC_PUT` was **280 of 291 ticks,
96%**, mirroring a chunky fill that itself cost 0.18 s.

**`planar_span_stlow()` writes a solid run a 16-pixel GROUP at a time.** For a
solid slot every plane word inside a fully covered group is a constant (0xFFFF
where the slot bit is set, 0x0000 where clear), so a full group costs `nplanes`
stores per 16 pixels instead of 16 read-modify-writes per plane; only the
ragged ends need a mask. `planar_fill_stlow` now routes through it too, so the
existing host test covers it.

| | ticks | wall |
|---|--:|--:|
| `jt103` | 291 -> **58** | 4.85 s -> **0.97 s** (5.0x) |
| chrome phase | 611-629 -> **420-439** | 10.2 s -> **7.0 s** |
| **FULL recompose** | 1014 -> **733** | **16.9 s -> 12.2 s (-28%)** |

The HUD phase dropped too (218 -> 169) — it fills solid plates as well.

**★ THE `unsigned short *` FORM IS AN ENDIANNESS BUG, AND THE HOST TEST CAUGHT
IT.** `*(unsigned short *)p |= mask` stores in the HOST's byte order: correct
on the big-endian m68k target, silently wrong on the little-endian host that
runs `tests/test_planar_fill.py`. It would have shipped working and failed only
in the test — the worst place to have that argument. Mask BYTE-WISE (hi/lo);
a fully covered group needs no read at all, so the fast path survives intact.

**★★ AND THE FIRST BEFORE/AFTER WAS INVALID — AGAIN, THE SAME WAY.** The play
frame differed by 2,252 pixels and the crops showed a DOOR present before and
absent after, which reads exactly like a render regression. It was not: the
baseline had been captured from a build WITHOUT the chrome/jt76/l67ca sub-stamps,
so the two arms differed in the instrumentation as well as the fix. Held one
flag apart (`FRUA_PERPIXELFILL`), span vs per-pixel is **AE=0**, and the whole
2,252 attributes to the stamps. **The dbg_log stamps are enough to change where
a timed drive lands — instrument BOTH arms identically.**

Menu frame byte-identical to the #125b baseline (AE=0); all five targets build;
427 tests pass.

**Next in the chrome phase**: with `jt103` down, the biggest items are the GLIB
art blits — piece 9 (the viewport frame) at 152 ticks and pieces 1-4 at 92.
Those are not solid fills, so they need the c2p treatment (a span re-encode)
rather than a constant-word fill; `dc_plane_bridge_span` is the same shape.

### #126 PIECE 9, MEASURED — 76% of it is the PLANAR BRIDGE, not the blit

Piece 9 (the 88x88 viewport frame) was 152-157 ticks of the chrome phase and
nobody had priced a pixel of it. Split with the same diagnostic that settled
`jt103` — a build that skips the plane mirror (`FRUA_DIAG_NOBRIDGE`) — plus a
per-leaf log of each landed rect's encoding and size.

| l67ca item | bridge ON | bridge OFF | the bridge |
|---|--:|--:|--:|
| `jt76` (incl. jt103 58) | 184 | 113 | 71 |
| **piece 9** | **157** | **38** | **119 (76%)** |
| piece 21 | 31 | 11 | 20 |
| dividers x2 | 46 | 17 | 29 |
| **l67ca total** | **418** | **179** | **239 (57%)** |

**`qd_planar_bridge_rect` is now the single biggest item in the chrome phase**
— 239 ticks, **~4.0 s** of the 7.0 s that remains after #125e. The blit itself
(`l2d4e`, which also DECODES: modes 2 and 5 are compressed) is 179 ticks.

It is the SAME per-pixel `DC_PUT` shape `dc_plane_fill` had before #125e: read
the chunky byte, remap it through the band LUT, then read-modify-write every
plane for that one pixel, plus per-pixel coverage bookkeeping. Measured at
**~450 cycles/pixel**, against ~337 for the decode+blit it mirrors.

**Geometry (per `l67ca`, 23 invocations sampled): 77 leaves, 70,892 pixels —
1.1x the whole 320x200 screen, redrawn from scratch every recompose.** The leaf
census explains the shape: **1,425 of 1,767 leaves are 8x11** (the composite
chrome tiles), so the bridge is called with a tiny rect ~62 times per l67ca and
pays its `dsp_planar_draw_target` + per-row band divide on 88 pixels at a time.

**★ THIS IS THE "ONE PRIMITIVE" CASE, AND IT IS REAL** — but it is
`qd_planar_bridge_rect`, NOT `dc_plane_bridge_span` as first guessed. (The
first reading of `ui_glib_blit` found it writing chunky directly and concluded
the art path had no draw-time plane store at all; it does, one level up in
`l309c`.) `qd_planar_bridge_rect` and `dc_plane_bridge_span` are literally the
same inner loop, so one span encoder serves both.

Unlike the solid fill, every pixel here has a DIFFERENT index, so the
constant-word trick does not apply — this needs a real chunky->planar span
conversion, which is exactly what the vendored `third_party/c2p-68k` subtree
does. Note the subtree is a CONSUMER relationship (CLAUDE.md): edit upstream
and `git subtree push`, do not re-add local copies.

NOT FIXED HERE — this entry is the measurement only.

### #126b BUILT — the span encoder, and the chrome phase is 4.5 s (was 10.2)

`planar_c2p_span_stlow()` converts a chunky run to ST-Low planes a 16-pixel
GROUP at a time: the group's plane words are accumulated in REGISTERS and
stored once, so a fully covered group is `nplanes` word stores for 16 pixels
with no read at all (the aligned 4-plane case shift-accumulates, no variable
shift). Unlike #125e's solid fill every pixel has its own index, so the words
must really be gathered — a real c2p, just a small one. `dc_cover_span()` does
the other half: the per-pixel `if`-plus-two-stores becomes one counting pass, a
`memset` of the flags and a `memcpy` of the indices (`dt->idx` holds exactly
the bytes being read, so the copy IS the same assignment).

Both bridge call sites now route through it — `qd_planar_bridge_rect` (the art
path, via `l309c`) and `dc_plane_bridge_span` (`qd_pixmap_fill`'s patterned and
bitwise arms, `CopyBits`, the cursor).

| | per-pixel | span | |
|---|--:|--:|--:|
| `piece 9` | 152 | **73** | 2.1x |
| `jt76` | 163 | **129** | |
| **chrome phase** | **420** | **267** | 7.0 s -> **4.45 s** |
| **FULL recompose** | **732** | **581** | 12.2 s -> **9.7 s** |

Cumulative over #125e + #126b: **the chrome phase is 611 -> 267 ticks (10.2 s
-> 4.45 s, 2.3x)** and a full recompose **1014 -> 581 (16.9 s -> 9.7 s, -43%)**.

**★★ THE CROSS-ARM SCREENSHOT IS NOT A CORRECTNESS TEST HERE, AND IT LIED IN
BOTH DIRECTIONS.** With the leaf trace compiled in, span vs per-pixel gave
AE=0; with it gone, AE=3216 — same code. The crops showed **a different party
member in the roster (LADY ILLIS vs MALTIER)** with the corridor art
pixel-identical: the arms differ in SPEED, so the faster drive lands on a
different game state (a different character highlighted when the Return lands).
**An A/B that changes performance cannot be validated by a timed drive's final
frame.** What settles it instead:

- **Host test** (`tests/test_planar_fill.py`): 3,000 random spans — unaligned,
  sub-group, multi-group — against a per-pixel `planar_put_stlow` reference,
  compared with `memcmp` over the WHOLE buffer on a randomised background, so
  untouched bytes are asserted too. **Mutation-tested**: flipping one plane's
  source bit makes it fail.
- **`FRUA_BRIDGEVERIFY`** for the coverage half, which no host test can reach:
  **3,298 checks, 0 mismatches**, including the INDEPENDENT invariant
  `rowcov[y] == popcount(cov[row])` — a property of the whole coverage system,
  not a restatement of the new code.
- **Menu frame** (a deterministic point, unlike the walk): byte-identical to
  the long-standing baseline, md5 99bbb623.

★ And the #126 leaf trace had to be split out of `FRUA_STEPPROF` into
`FRUA_LEAFTRACE`: 77 leaves x 2 log lines per `l67ca` cost ~50 ticks and
inflated the chrome AND hud phases while folded in (chrome read 469 instead of
420, hud 409 instead of 169). The A/B stayed valid because both arms carried
it, but every absolute number was wrong. **An instrument that logs per item
becomes the thing you are measuring.**

Amiga keeps the per-pixel loop (no span primitive for separate planes yet), so
ECS/AGA are correct and simply unimproved. That is the obvious next port.

### #127 THE SPAN ENCODER, PORTED TO AMIGA — and a VACUOUS TEST caught

`planar_span_amiga()` / `planar_c2p_span_amiga()` are the separate-plane
siblings of #125e/#126b. The unit is a BYTE (8 pixels, MSB-first, plane p at
`dst + p*plane_bytes`) rather than a 16-pixel word group, but the shape is the
same: a solid run makes each fully covered plane byte a constant, and a chunky
run's plane bytes are accumulated in a register and stored once instead of
`nplanes` read-modify-writes per pixel. No endianness hazard here — byte
addressed throughout, unlike the ST word form.

Both Amiga macro arms (`DC_SPAN`, `DC_C2P`) now route through them, so ECS and
AGA stop paying the ~450 cycles/pixel the ST stopped paying.

**★★ THE FIRST AGA "PASS" WAS VACUOUS — `make MACHINE=amiga` DOES NOT DEFINE
`FRUA_PLANAR`.** Only the release targets pass it (`release-amiga`,
`release-amiga-ecs`); the plain AGA build is the chunky+c2p path, so NEITHER
arm compiled the bridge and the byte-identical menu proved nothing. It looked
like a clean A/B. What exposed it was an unrelated link error: the
`FRUA_BRIDGEVERIFY` counters live beside `dc_cover_span` inside the planar
block, so guarding the boot.c dump on `FRUA_BRIDGEVERIFY` alone failed to link
**exactly on the config the test was meant to cover**. Guard is now
`#if defined(FRUA_BRIDGEVERIFY) && defined(FRUA_PLANAR)`.

**And the screenshot could not have caught it either**: AGA-planar vs
AGA-chunky is **AE=0** — as ADR-0016 requires, the two paths are byte-identical
by construction. A frame comparison cannot tell you which path RAN. Only a
positive signal can.

| target | config | result |
|---|---|--:|
| ST/STe | `CPU68K=68000` | 3,298 checks, **0 mismatches** |
| Amiga AGA | `MACHINE=amiga -DFRUA_PLANAR` | 3,298 checks, **0 mismatches** |
| Amiga ECS | `MACHINE=amiga CPU68K=68000 -DFRUA_FORCE_ECS` (5 planes) | 3,298 checks, **0 mismatches** |

The identical check count across all three is itself a cross-check: same engine
work, same spans, three different plane layouts. ECS logged its native path
(`ecs: 320x200x5 32-colour, per-band copper palette up`), so the 5-plane arm
really ran.

Host tests extended to both new primitives — 3,000 random spans each (solid and
c2p, alternating) against a per-pixel `planar_put_amiga` reference, `memcmp`
over the whole buffer on a randomised background. **Mutation-tested twice** (a
wrong plane-bit source, and an off-by-one in the span clamp); both fail the
suite.

★ **The Amiga driver never passed `DISPLAY` into the flatpak** — it sets
`--env=SDL_VIDEODRIVER=x11` but relies on the ambient `DISPLAY`, so the
`env -u DISPLAY` habit (correct for Hatari, which would otherwise open on the
user's desktop) makes amiberry die with "SDL could not initialize! x11 not
available". Run it as `DISPLAY=:99 FRUA_AMIGA_DISPLAY=:99`, which still keeps
it off the real desktop.

NOT MEASURED: no before/after wall-clock on Amiga. The Atari numbers do not
transfer — different plane count (5 and 8 vs 4), different memory bandwidth,
and ECS runs a 7 MHz 68000. Correctness is established on all three; the size
of the Amiga win is an open question.

### #128 THE SPAN ENCODER MEASURED — and the first cross-machine table

`FRUA_STEPPROF` is in `boot.c`, so it works on every target. First like-for-like
numbers across all five machines, one instrument, one drive (HEIRS to a real
walk). All figures are **60 Hz Mac ticks** — the Amiga shim scales its 50 Hz PAL
VBL by 6/5, so a tick means the same thing everywhere.

**A/B, span encoder vs per-pixel (one flag pair apart, same config per machine):**

| | `jt76` | piece 9 | FULL recompose |
|---|--:|--:|--:|
| Atari STE (4 planes) | 163 -> **129** | 152 -> **73** | 732 -> **581** (-21%) |
| Amiga AGA (8 planes) | 115 -> **46** | 42 -> **36** | 290 -> **197** (-32%) |
| Amiga ECS (5 planes)* | 344 -> **135** | 205 -> **93** | chrome 820 -> **444** |

*ECS A/B ran at `cpu_speed=max` — ratios only. Its row in the cross-machine
table below was re-measured at `cpu_speed=real`.

**★ THE WIN'S SHAPE DIFFERS BY PLANE COUNT, AND AGA's c2p BARELY MOVED.** On
AGA `jt76` (which is mostly `jt103`, a SOLID fill) improves 2.5x, but piece 9
(the c2p bridge) only 1.2x. The reason is in the code: the ST c2p has a
4-plane shift-accumulate fast path, the Amiga one only has the general
`for (p = 0; p < nplanes; p++)` inner loop — and AGA runs **8** planes, so that
loop still dominates. ECS with 5 planes gets 2.2x from the same code.
**An 8-plane fast path for `planar_c2p_span_amiga` is the obvious next win**,
and it is AGA-specific rather than a general Amiga problem.

**Cross-machine, current code, honest emulator speeds:**

| machine | CPU | display | walk step | full recompose |
|---|---|---|--:|--:|
| Atari TT030 | 68030 32 MHz | chunky 8bpp | 6 tk (**0.10 s**) | 52 tk (**0.87 s**) |
| Atari Falcon030 | 68030 16 MHz | chunky 8bpp | 12 tk (**0.20 s**) | 76 tk (**1.27 s**) |
| Amiga AGA (A1200) | 68020 14 MHz + fast | 8 planes | 15 tk (**0.25 s**) | 197 tk (**3.28 s**) |
| Amiga ECS (A600) | 68000 7 MHz + fast | 5 planes | 94 tk (**1.57 s**) | 666 tk (**11.10 s**) |
| Atari STE | 68000 8 MHz | 4 planes | 129 tk (**2.15 s**) | 581 tk (**9.68 s**) |

Two things worth reading off it:

- **The 030 machines are in a different league** — a Falcon recompose is 1.3 s
  against the STE's 9.7 s, 7.6x, on a CPU only 2x the clock. The rest is
  32-bit 020+ codegen and no plane conversion at all.
- **AGA is 2.6x slower than a Falcon on a recompose despite a comparable CPU**
  (3.28 s vs 1.27 s) — that gap IS the planar bridge, which the chunky machines
  never pay. Its step, where little converts, is nearly Falcon-class (0.25 s vs
  0.20 s). So the AGA's remaining cost is concentrated exactly where the
  8-plane c2p fast path above would land.
- **The ECS out-steps the STE (1.57 s vs 2.15 s) despite a SLOWER 68000** —
  7 MHz against 8. The likely reason is FAST RAM: the A600 config has 4 MB of
  it, so code and data run without the chip-bus contention every ST access
  pays. Offered as the plausible explanation, not a measured one. It does not
  hold for the recompose (11.1 s vs 9.68 s), where the ECS's fifth plane costs
  more than the fast RAM saves.

**★★ THE SHIPPED ECS CONFIG IS NOT AT REAL SPEED — `openua-ecs.uae` has
`cpu_speed=max`.** The emulated 68000 runs unthrottled against a 50 Hz VBL, so
it does far more work per tick than an A600: measured "ECS" steps came out
FASTER than the STE's, which is impossible for a 7 MHz 68000 against an 8 MHz
one. Caught by that impossibility, not by the config. **A/B ratios survive
(both arms share the config); absolute and cross-machine figures do not.** The
skill doc specifies `cpu_speed=real` for this config, so it has drifted.

**★ Hatari's `--fast-forward` is NOT the same hazard.** It runs the emulation
faster in wall-clock while keeping the machine's internal CPU:VBL relationship
correct, so emulated-tick measurements stay valid — which is why the STE
figures here match the independently measured 86 s real-speed boot. amiberry's
`cpu_speed=max` changes the RATIO. Fast-forward is safe; unthrottled CPU is not.

★ `g_fsopen_calls` had to move to `boot.c`: it was defined in `compat/files.c`,
which is `#ifndef FRUA_AMIGA`, so any Amiga build with `FRUA_STEPPROF` failed
to link. Same nested-guard family as the rest of this session.

### #129 THE AGA FAST PATH — piece 9 halves again, and the win is TWO changes

#128 found the Amiga c2p barely improving on AGA (piece 9 only 1.2x) because
`planar_c2p_span_amiga` had only the general per-plane inner loop and AGA runs
**8** planes. Two changes, and it is worth separating them because they help
different traffic:

1. **A 32-pixel aligned block goes through the subtree's `c2p_transpose32`**
   (`third_party/c2p-68k`, ~4 ops/pixel) instead of the per-pixel/per-plane bit
   test. Only when the block is 32-aligned AND entirely inside the span, so
   every byte is full — no mask, no read-back. **Narrow spans deliberately do
   NOT come here**: padding an 8-pixel leaf out to a 32-pixel transpose costs
   more than it saves, and 1,425 of 1,767 chrome leaves are 8 wide (#126).
   This is what the WIDE leaves (320x8, 320x16, 88x88) get.
2. **The narrow path walks the slot bits with a RUNNING shift** (`s & 1;
   s >>= 1`) instead of `(s >> p) & 1`. A variable shift is 6+2n cycles on a
   68000 and this ran `nplanes` times per PIXEL — so it is worth most exactly
   where the plane count is highest. This is what the SMALL leaves get, and it
   is why piece 9 (all 8x11 leaves) improved at all.

| | per-pixel (#126) | span (#127) | +fast path (#129) |
|---|--:|--:|--:|
| AGA `jt76` | 115 | 46 | **37** |
| AGA piece 9 | 42 | 36 | **15** |
| AGA FULL recompose | 290 | 197 | **160** (4.83 s -> **2.67 s**, -45%) |
| ECS `jt76`* | 344 | 135 | **118** |
| ECS piece 9* | 205 | 93 | **57** |
| ECS FULL recompose | — | 666 | **583** (11.10 s -> **9.72 s**) |

*ECS `l67ca` figures at `cpu_speed=max` (ratios); the FULL row is `real`.

**Cross-machine, updated:**

| machine | walk step | full recompose |
|---|--:|--:|
| Atari TT030 | 0.10 s | 0.87 s |
| Atari Falcon030 | 0.20 s | 1.27 s |
| Amiga AGA | 0.25 s | **2.67 s** (was 3.28) |
| Amiga ECS | 1.58 s | **9.72 s** (was 11.10) |
| Atari STE | 2.15 s | 9.68 s |

AGA is now **2.1x** a Falcon on a recompose, down from 2.6x. ECS and the STE
have converged to within 1% of each other (9.72 s vs 9.68 s) from opposite
directions — the ECS has fast RAM and a slower CPU, the STE a faster CPU and
contended RAM.

**★ THE HOST TEST WAS TESTING FOUR PLANES ONLY.** `tests/test_planar_fill.py`
is built around `NP 4`, so neither Amiga plane count the port actually ships
(ECS 5, AGA 8) had ever been exercised — and the fast path stores per plane, so
an 8-plane bug would have been invisible. Added a 4/5/8-plane sweep;
mutation-tested by making the fast path write only 4 planes, which the new
block catches and the old one did not.

**★ `-Werror` IN `tests/test_planar.py` CAUGHT THE INCLUDE.** Pulling `c2p32.h`
into `planar.h` left `c2p_load32` unused in every consumer, and that test builds
its harness with `-Werror`. The fix was the right code anyway: use `c2p_load32`
for the gather instead of open-coding the big-endian packing, which had been a
second place to get the lane order wrong.

Verified: host suite 427 pass (2 fast-path mutations + 1 plane-count mutation
all caught); `FRUA_BRIDGEVERIFY` 3,298 checks / 0 mismatches on **both** AGA
(8 planes) and ECS (5); ST menu byte-identical to the long-standing baseline
(the ST path is untouched — only `planar_c2p_span_amiga` changed); six build
configs green.

### #130 WHAT AN EVENT MODAL ACTUALLY DAMAGES — 0 to 81 pixels of 64,000

The event path sets `g_view_force_full = 1` after a modal, on the rationale
(in the code) that *"The Mac rebuilds the play dialog from scratch on every
cycle; this is the port's equivalent, **and it is cheap**"*. That claim had
never been measured. It costs a FULL recompose — ~16.9 s originally, 9.7 s
after #125e/#126b — and it fires after every event message.

`FRUA_MODALDAMAGE` snapshots the chunky screen immediately BEFORE
`port_draw_play_frame` and diffs it against the screen after the whole compose
(chrome + 3D render + HUD + present). **Pixels the rebuild does not change are
work it did not need to do.** One HEIRS walk drive, all five full rebuilds:

| # | trigger | changed px | rows | cols |
|---|---|--:|---|---|
| 1 | entry (`s_view_first`) | 7,075 | 0-197 | 0-319 |
| 2 | event modal | **0** | — | — |
| 3 | event modal | **15** | 104-110 | 218-222 |
| 4 | event modal | **0** | — | — |
| 5 | event modal | **81** | 104-111 | 210-238 |

**Two of the five rebuilds changed NOTHING AT ALL**, and the other three
changed at most 81 pixels of 64,000 — an 8-row strip at rows 104-111, x
210-238, which is the CLOCK text (`12:04 AM`). A ~9.7-second full recompose to
repaint a clock digit.

Even the entry rebuild is narrower than it looks: its dirty rows are only
**0-15 and 184-199** — the top and bottom strips. The 168 rows in between,
including the whole 88x88 viewport and the roster panel, were redrawn
identically.

**So the answer to "does the modal damage the frame?" is: on this path, no.**
Not the border, not the viewport surround, not the compass ring, not the
roster. The force-full is repainting a static screen to update a clock.

★ SCOPE, stated honestly: this is HEIRS' entry/caravan event chain on the walk
path — the events an autoplay drive reaches. The force-full was added for real
symptoms (the comments record a BLANK ROSTER, a BLANK CLOCK, and jt221's bare
FRAME pieces surfacing as three stray plates), so other paths — CAST, camp, a
true modal dialog over the viewport — may damage more. **What is measured is
that the walk-path event chain damages nothing; what is NOT measured is every
other route to `g_view_force_full`.** A fix should repaint what is dirty rather
than assume nothing ever is.

The obvious shape: after a modal, repaint the HUD panels (roster, clock,
command bar) — the things the comments say came back blank — WITHOUT re-laying
`port_draw_play_frame`'s chrome or re-running the 3D view. On the numbers above
that is ~9.7 s -> the cost of a clock repaint, on every event message.

★ `FRUA_MODALDAMAGE` needs `FRUA_STEPPROF` (its report lives in the FULL-path
block `FRUA_STEPTIME` guards, and so does its buffer). Alone it would compile
the snapshot and never the comparison — silently inert, the same shape as
`FRUA_AUTOWALK_TREASURE` (#125c) and the vacuous AGA A/B (#127). It now
`#error`s from a spot that cannot itself be compiled out, which is the part the
first attempt got wrong: the guard was placed INSIDE the block it was warning
about, so it vanished with it.

### #131 THE HUD-ONLY REPAINT — event messages ~halve, and CAST nearly caught me out

#130 measured that a full rebuild after an event modal changes 0-81 pixels of
64,000. So `jt312` now has a LIGHT path: `g_view_hud_only` runs the HUD repaint
(roster, clock, command bar) and the full present, and skips
`port_draw_play_frame` entirely. `g_view_force_full` is unchanged and still
does both.

| compose | force-full | HUD-only |
|---|--:|--:|
| entry (chrome) | 386 | 386 |
| event message | **580** | **301** |
| backdrop reload (chrome) | 687 | 688 |
| event message | **708** | **430** |
| event message | **555** | **277** |

**~278 ticks — 4.6 s — off every event message on an 8 MHz STE**, i.e. 9.7 s to
about 5.0 s. Chrome-bearing composes are untouched, as they must be.

**★★ ONLY ONE OF THE TWO force-full SITES COULD TAKE IT, AND THE FIRST ATTEMPT
SHIPPED THE REGRESSION #130 EXPLICITLY WARNED ABOUT.** The `l63c0` site is not
the event path at all — its own comment says *"Force-full on EVERY deep entry,
not just after an event"*, because `l63c0` is re-entered after every command
through `jt948 -> jt240` and `jt221`'s prelude lays bare FRAME pieces. Driving
**CAST / VIEW / INV** with that site on the light path reproduced the exact
documented symptom: **three stray plates across the top of the play screen.**
Reverted; the light path is used only at the per-STEP modal site, which is
where the pool was really clobbered. With that split, the command drive is
identical to the control again.

This is what #130's scope caveat was for — "what is NOT measured is every other
route to `g_view_force_full`" — and it took a deliberately hostile drive to
find, not the event chain the autowalk already covered.

**★ EQUIVALENCE BY SCREEN HASH, NOT BY SCREENSHOT.** `FRUA_SCREENSUM` logs an
FNV hash of the whole composed surface after EVERY `jt312` compose, numbered.
The two arms drive the same engine-tick-paced key sequence, so their hash
SEQUENCES must match element for element — and this does not care that one arm
is seconds faster, which a final-frame comparison demonstrably does (#126b saw
AE=0 and AE=3216 from the same code). Results: **33/33 identical** on the walk
drive, **54/54 identical** with CAST/VIEW/INV driven. The faster arm simply
reaches more composes (36 and 48 against 33 and 54) — itself corroboration.

New test-only flags, both release-guarded: `FRUA_MODALFORCEFULL` (the A/B arm)
and `FRUA_AUTOWALK_CMDS` (appends CAST/VIEW/INV to the headless script — worth
keeping, since no event chain exercises that exit).

### #132 THE 3D RENDER — it was never the WALLS, it was a DIVIDE PER PIXEL

The walk step (~2.1 s, 84% of it `render_3d_faithful`) was the last unmeasured
block. Phase stamps (`FRUA_R3DPROF`) put it:

| phase | ticks | share |
|---|--:|--:|
| setup / clip | 0 | — |
| trapezoid region fills | 23-30 | ~25% |
| **backdrop image blit** | **53** | **~50%** |
| `l6148` | 1-2 | — |
| `jt199` (the frustum walk = THE WALLS) | 22-23 | ~21% |
| viewport commit | 0 | — |

**The walls are a fifth of it.** Half is the backdrop image — and the reason is
one line:

```c
short bx = (short)(((long)xx * g_back_w) / bw);   /* per PIXEL */
```

A 32-bit divide for the horizontal scale, per pixel, and a 68000 has no 32-bit
divide: ~1,100 cycles into libgcc, **7,744 times** for an 88x88 viewport.
Measured at ~910 cycles/pixel, which is the divide plus `map_px`.

The source column is a pure function of `(xx, bw, g_back_w)` — **the same `bw`
values on every one of the `bh` rows**. Build the column map once and reuse it;
the row map tables for the same reason. Cached across frames, since the
viewport and backdrop dimensions rarely change.

| | divide | tabled |
|---|--:|--:|
| backdrop blit | 53 tk | **14 tk** |
| step render | 101-112 | **63-76** |
| **whole walk step** | **~128 tk (2.13 s)** | **~89 tk (1.48 s), -30%** |

★ Note this divide never appeared in #125's histogram: that profiled the BOOT,
and the dungeon backdrop does not run before the menu. **A histogram only knows
the window you gave it** — the play loop had never had one.

★ A mislabel worth recording: the first split blamed `l57f2`, which is a
one-line wrapper around `l58c4` — and `l58c4` returns immediately in colour
(`g_dungeon_bigpic_overlay` is 0, permanently). The bracket was really
measuring the INLINE region fills and backdrop blit that sit between
`cw_view_clip` and the `l57f2()` call. Reading the callee before believing the
label is what turned "l57f2 is 75%" into the actual finding.

Equivalence: `FRUA_SCREENSUM` hash sequences **identical over 36 composes**
(#131's method — immune to one arm running faster); ST menu byte-identical;
five configs build; 427 tests pass. A/B arm `FRUA_BACKDROPDIV`, release-guarded.

**Next in the render**: the trapezoid fills are now the largest item (23-30 tk)
and they are `map_px` per pixel over three solid regions — the same shape
`dc_plane_fill` had before #125e, and the same fix (span fills) should apply.

### #133 THE TRAPEZOID FILLS — a memset per row; the walk step is now HALVED

After #132 the largest remaining render item was the three perspective region
fills (23-30 of a step's ~89 ticks). They are SOLID regions painted with a
bounds-checked store per pixel:

```c
for (yy = ct; yy < cb; yy++)
    for (xx = cl; xx < cr; xx++)
        map_px(vtgt, vpitch, sw, sh, xx, yy, fill);
```

`map_px` clips to `[0,sw) x [0,sh)`, so folding that clamp into the region
bounds ONCE lets each row become a `memset` with no per-pixel test at all.

| | per-pixel | memset |
|---|--:|--:|
| trapezoid fills | 24-28 tk | **0-3 tk** |

**The walk step, end to end on an 8 MHz STE:**

| | render | present | step |
|---|--:|--:|--:|
| before #132 | 101-112 | 20-21 | **~128 tk = 2.13 s** |
| after #132 (backdrop table) | 63-76 | 20 | ~89 tk = 1.48 s |
| after #133 (region memset) | **39-51** | 20 | **~65 tk = 1.08 s** |

**A walk step is HALVED — 2.13 s to 1.08 s.** The render is now `jt199` (the
walls, 22-23 tk) plus the backdrop (14) plus ~nothing for the regions, and for
the first time the largest thing in a step is the actual wall rendering rather
than a fill.

Equivalence: `FRUA_SCREENSUM` hash sequences **identical over 44 composes**; ST
menu byte-identical; five configs build; 427 tests pass. A/B arm `FRUA_TRAPPX`,
release-guarded.

**Where the ST/STe play loop now stands** (all measured, same instrument):

| | before this run | now |
|---|--:|--:|
| walk step | 2.13 s | **1.08 s** |
| event message | 16.9 s | **5.0 s** |
| dungeon entry / full rebuild | 16.9 s | ~9.7 s |

**Next**: `jt199` at 22-23 tk is the largest render item and has never been
split — it is the frustum walk that blits the wall tiles through
`l309c_tile -> l2d4e`. The other open item is the entry rebuild, still ~9.7 s,
whose chrome phase #126 left at 4.45 s with the GLIB art blits (`l2d4e`,
decode + per-pixel copy) as the remainder.

### #134 THE WALL TILES — three hoists, and the hash method shows its LIMIT

After #133 the largest render item was `jt199` (22 tk), the frustum walk. It
blits **17 tiles / 13,968 examined pixels** a step through `l309c_tile`, at
~220 cycles per examined pixel. Three hoists, all behaviour-preserving:

1. **The x clip is a property of the ROW, not the pixel.** The original tested
   `dx` against both edges inside the loop; the surviving `c` range is just
   `[clip_l - x0, clip_r - x0)` intersected with `[0, w)`, computed once.
2. **Source and destination ROW POINTERS**, so `r * w` and `dy * pitch` leave
   the pixel loop.
3. **One 256-entry table for the colour decision.** The global 255 key, the
   band range test, the per-set magenta key and the band rebase are all pure
   functions of the source byte: fold them into `lut[256]` (0 = drop, else
   `0x100 | byte`) and the inner loop is load / test / store. Rebuilt per tile
   so it cannot go stale against a wall reload — 256 iterations against 13,968
   pixels pays for itself many times over. **`static`, not a local**: 512 bytes
   of stack five calls deep on a machine whose stack the engine does not own.

| | before | after |
|---|--:|--:|
| `jt199` | 22 tk | **14 tk** |
| step render | 39-51 | **31-44** |
| **walk step** | ~65 tk (1.08 s) | **~55 tk (0.92 s)** |

**★★ AND THE SCREEN-HASH METHOD FAILED HERE — WORTH KNOWING WHY.** #131
introduced hash sequences as an equivalence test "immune to one arm running
faster". Against this change the sequences **differed from compose 2**, which
reads as a rendering regression. It was not:

- **`FRUA_TILEVERIFY`** recomputes the ORIGINAL decision — colour AND clip —
  for every pixel and compares: **734,848 pixels, 0 mismatches.**
- The final walk frame is **byte-identical (AE=0)** to the #133 build.
- A host check of the LUT construction agrees for all 256 values across every
  band base.

So the hash method's guarantee is narrower than #131 claimed: it holds while
the COMPOSE SEQUENCE is unchanged, and a large enough speed change can perturb
which composes happen relative to the engine-tick-paced keys. **A hash sequence
compares screens ONLY if the two runs' composes correspond.** The per-pixel
verifier has no such dependency, and is what should settle a render change.

Five configs build; 427 tests pass; ST menu byte-identical. A/B arm
`FRUA_TILEPX`, release-guarded.

**The ST/STe walk step across this run: 2.13 s -> 0.92 s, a 2.3x cut**, with
the render now 31-44 tk against a fixed ~20 tk present — the present is
becoming the floor.

### #135 PORT HEALTH SURVEY — all five re-measured on current code

#128's table is stale: #131-#134 were all ENGINE-level, so every target moved.
Re-measured with one instrument (`FRUA_STEPPROF`), one drive, 60 Hz ticks.

| machine | CPU | display | walk step | event message | full rebuild |
|---|---|---|--:|--:|--:|
| Atari TT030 | 68030 32 MHz | chunky | **0.07 s** | 0.52-2.65 s | **0.30 s** |
| Atari Falcon030 | 68030 16 MHz | chunky | **0.15 s** | 0.72-2.85 s | 0.62 s |
| Amiga AGA | 68020 14 MHz | 8 planes | **0.20 s** | 1.32-2.57 s | 1.40 s |
| Atari STE | 68000 8 MHz | 4 planes | **0.92 s** | 3.45-5.98 s | 5.3-10.3 s |
| Amiga ECS | 68000 7 MHz | 5 planes | **0.97 s** | 3.92 s | 9.10 s |

Against the start of this run: STE step **2.15 -> 0.92 s**, ECS **1.57 -> 0.97**,
AGA **0.25 -> 0.20**, Falcon **0.20 -> 0.15**, TT **0.10 -> 0.07**. Full rebuild
on the 020 machines fell hardest — AGA **3.28 -> 1.40 s**, TT **0.87 -> 0.30**.

**Health, plainly:** all five colour targets build, boot and play. The three
020 machines are comfortably interactive. The two 68000 machines are at ~0.95 s
a step — playable, not brisk. **The mono ST-High build still compiles and still
does not boot** (shelved; hangs on the disk-swap prompt, see the mono notes).

**★ THE BIGGEST REMAINING ITEM IS NO LONGER THE RENDER — IT IS THE ROSTER, AND
IT GROWS.** Splitting the HUD phase (bar / clock / roster) across a drive:

| | compose 1 | 2 | 3 | 4 |
|---|--:|--:|--:|--:|
| TT roster | 20 | 20 | **88** | **149** |
| STE roster | 60 | 65 | **191** | 41 |
| STE bar | 82 | 87 | 82 | 81 |
| STE clock | 18 | 20 | 18 | 18 |

The bar and clock are flat. **`jt937` — the party roster grid — climbs 7x
within a single drive on the TT and spikes to 191 ticks (3.2 s) on the STE**,
for a party of ONE character. That is not a rendering cost that should vary at
all, and it is now the largest single item in an event compose on every machine.

This lines up with something already documented: `jt25` (the roster NAME paint)
carries a **wild-pointer guard** whose comment says *"the AREA command's play
re-render can hand the roster-name paint a corrupted node (a stray/wrapped
next-link)"*. The guard skips the bad node — but the walk still iterates. **A
roster paint whose cost grows over a drive is what a lengthening or looping
list looks like.** So this is a correctness lead first and a perf item second.

**RANKED, for whoever picks this up:**

1. **`jt937` roster paint** — grows 7x within a drive, biggest item in an event
   compose on all five machines, and there is an existing comment describing a
   corrupted next-link on the same structure. Measure the list LENGTH per paint
   before optimising anything.
2. **`l2c60` command bar** — flat but expensive on the 68000 machines: ~82 tk
   (1.4 s) on STE, ~104 (1.7 s) on ECS, against 3-5 tk on the 020s. A 20x
   machine ratio on a fixed workload points at per-pixel plate drawing, the same
   shape as #133/#134.
3. **Entry chrome** — STE 5.3-10.3 s, ECS 9.1 s, still the largest one-off wait.
   #126 left the GLIB art blits (`l2d4e` decode + per-pixel copy) as the
   remainder.
4. **The present floor** — a fixed ~21 tk (0.35 s) of every STE/ECS step, now
   ~38% of a step. Render work below ~30 tk stops being worth much until this
   moves.

### #136 THE ROSTER IS INNOCENT — #135's CONCLUSION WAS WRONG

#135 said the roster paint grows 7x within a drive and pointed at a corrupted
`.next` chain. **Measured, the list is exactly ONE node on every paint** —
`FRUA_ROSTERPROF` over a whole drive: `nodes = 1`, `broke = 0`, `lastrow = 5`,
every time. No growth, no corruption, no early break. The existing wild-pointer
guard in `jt25` is doing nothing here.

Splitting `l02dc` confirms it is flat: headers 10-11 tk, row bar 0-2, name 7-8,
AC+HP 4-5 — **~25 ticks, dead flat, every compose.**

**The variance was never in the roster. The bracket contained a SECOND call.**
`g_mpf_h2..f2` spans `jt937(...)` *and* `play_sticky_text_replay()`, and the
label said "HUD roster ticks". Separated:

| | roster | **sticky text** | bar |
|---|--:|--:|--:|
| compose 1 | 23 | 37 | 82 |
| compose 2 | 26 | 38 | 87 |
| compose 3 | 23 | **168** | 82 |
| compose 4 | 23 | 18 | 81 |

`play_sticky_text_replay` is the whole of it — **168 ticks (2.8 s) on one
compose**. It redraws up to five lines of the sticky event text through `jt96`,
the word-wrap renderer, **on every recompose, identical text**. That is the
same "repainting something that did not change" shape as the chrome in #131,
and the same fix should apply: replay only when the text or its box changed.

**★★ THIS IS THE THIRD MISLABELLED BRACKET THIS SESSION**, all the same
mistake — a phase named after its FIRST call while spanning more:

- `l57f2` "is 75% of the render" (#132) — it is a no-op wrapper; the bracket
  held the inline region fills before it.
- `FULL chrome ticks` on a HUD-only compose (#131) — stale stamps, read as
  garbage.
- `HUD roster ticks` (here) — roster plus the sticky-text replay.

**A phase label is a claim about what a bracket contains, and it needs checking
like any other claim.** Two of the three sent an investigation at the wrong
function first.

**RANKED, corrected:**

1. **`play_sticky_text_replay`** — up to 2.8 s, redrawing identical text every
   recompose. Skip-if-unchanged first; the text renderer itself second.
2. **`l2c60` command bar** — flat ~82 tk (1.4 s) STE, ~104 (1.7 s) ECS, against
   3-5 tk on the 020s. A 20x machine ratio on fixed work still points at
   per-pixel plate drawing.
3. **Entry chrome** — STE 5.3-10.3 s, the largest one-off.
4. **The present floor** — ~21 tk of every STE/ECS step.

`FRUA_ROSTERPROF` stays: it is the instrument that settled this, and it is the
one that should run before anyone optimises a list walk.

### #137 STICKY TEXT, SKIP-IF-UNCHANGED — event messages 5.0 s -> 3.2 s

#136 put the whole HUD variance in `play_sticky_text_replay`. Before changing
anything, `FRUA_STICKYDAMAGE` asked what the replay actually CHANGES — snapshot
the text-box rows, replay, diff:

| compose | ran chrome? | replay changed | cost |
|---|---|--:|--:|
| 2 | no | 0 px | 0 |
| 3 | **yes** | **479 px** (rows 136-142) | 53 |
| 4 | no | 0 px | 55 |
| 5 | no | **0 px** | **184** |

**The only replay that changed a pixel was on a compose that ran
`port_draw_play_frame`** — which wipes the screen, box included. Every HUD-only
replay redrew text that was already correct, one of them for 184 ticks (3.1 s).
The reason the box is already right is that the event-fire path replays
immediately when the text changes; by the time a HUD-only compose runs, there
is nothing to do.

So: `g_sticky_dirty`, set by the chrome wipe and cleared by the event-fire
replay. The HUD block replays only when dirty.

| | before | after |
|---|--:|--:|
| sticky replay, whole drive | 0/53/55/184 tk | 0/0/38/0 |
| **event compose** | 231/359/207 tk | **193/191/189** |
| **event message** | ~5.0 s | **~3.2 s** |

Note the after column is FLAT — the spikes are gone, which is the real symptom
being fixed: an event message no longer costs a different amount depending on
what came before it.

**Proved, not assumed.** `FRUA_STICKYVERIFY` takes every skip, snapshots the
box, runs the replay ANYWAY and reports any pixel it changes — the verifying
build always replays, so it renders correctly whatever the answer, and the log
is the evidence. **3 skips, 0 unsafe.** The command drive
(`FRUA_AUTOWALK_CMDS`) produces **0 skips**, because every command exit
force-fulls and so legitimately needs the replay — no skips, no risk. Walk
frame byte-identical to the #134 build; ST menu byte-identical; five configs
build; 427 tests pass. A/B arm `FRUA_STICKYALWAYS`, release-guarded.

**ST/STe play loop, end of this run:**

| | start | now |
|---|--:|--:|
| walk step | 2.13 s | **0.92 s** |
| event message | 16.9 s | **3.2 s** |
| dungeon entry | 16.9 s | ~5.3-10.3 s |

**Next**: `l2c60` (the command bar) is now the largest flat item at ~82 tk
(1.4 s) on STE against 3-5 tk on the 020s — a 20x machine ratio on fixed work,
which is the per-pixel signature #133/#134 both had.

### #138 THE COMMAND BAR — it is `jt137`, not `jt382`, and the fix is a level up

`l2c60` at ~82 tk (1.4 s) on STE against 3-5 on the 020s was the largest flat
item after #137. `FRUA_BARPROF` times each DLItem: **13 items, 7 of which cost
8-16 ticks each** — the seven command words — and cost tracks word width, so
it is per-pixel work.

**★ THE COMMENTS NAME THE WRONG FUNCTION.** Both `l2c60`'s comment and the HUD
block say *"jt382 draws it per label when g_hud_paint"*. A stamp inside jt382's
text arm **never fired**. Logging the DLItem method pointer and resolving it
against `nm` gives **`jt137`** for all seven. That is the FOURTH mislabel this
session, and the second to send an investigation at the wrong function.

`jt137` msg 1 rebuilds each item's plate from FRAME.CTL glyphs via `jt448` —
left cap, `rec[24]` middle pieces every 4 units, right cap — then the label.
So the bar is ~7 x (2 + N) GLIB piece blits per repaint, on the same
`l309c -> l2d4e` path #126 measured, for a bar whose contents never change.

**Two things were tried and BOTH REVERTED for measuring nothing:**

- **A `mac_font_pixel` (c, row) memo.** It does a 32-bit multiply
  (`row * rowBytes`, `__mulsi3`, ~140 cycles) per PIXEL, invariant across a
  glyph row — textbook. Measured: **bar 115-122 tk before, 114-121 after.**
  No effect, so it was reverted rather than shipped as complexity with no
  benefit. The per-pixel multiply is real; it is simply not the bottleneck.
- **`l2c60(g_bar_dirty)` instead of `l2c60(1)`**, letting the engine's own
  "painted" bit (0x80) skip unchanged items — the #137 shape. Measured:
  **81-88 tk either way.** The reason is one level up: `play_screen_relayout`
  REBUILDS THE DLITEM POOL on every event modal, so the painted bits are clear
  and everything repaints legitimately. Reverted.

**So the fix is not in `l2c60` or `jt137` — it is that the pool is rebuilt per
event modal.** The bar's seven words are identical before and after; the
rebuild discards paint state that was still valid. That is the same shape as
#131 (chrome force-full) and #137 (sticky text) one layer further in, and it is
where the 1.4 s goes.

★ Worth stating plainly: two measured non-results in a row. Both hypotheses
were reasonable — a per-pixel 32-bit multiply, and a redundant force-repaint —
and both were wrong about THIS cost. Reverting them is the result, not a
failure to get one; shipping either would have added surface for nothing.

### #139 WHY THE RELAYOUT IS UNCONDITIONAL — and three failed attempts to skip the repaint

**It is not gratuitous.** The call site says so: an event's "Press [Return]"
modal (`l1806`) **RESETS THE SHARED DLItem POOL** — the command bar and the
walk input sources are replaced by the modal's RETURN button. So after a modal
the play items genuinely do not exist and must be rebuilt for INPUT to work.

**But rebuilding the data is not the same as needing to redraw.**
`FRUA_BARDAMAGE` snapshots the bar rows, repaints, and diffs:

| compose | ran chrome? | bar repaint changed |
|---|---|--:|
| post-modal | no | **0 px** |
| chrome wipe | yes | 3,310 px (rows 187-197) |
| post-modal | no | **0 px** |
| post-modal | no | **0 px** |

**The modal takes the POOL but not the PIXELS.** The bar is still on screen,
correct, when `l2c60(1)` repaints all thirteen items for ~82-100 ticks
(1.4-1.7 s) an event message.

**THREE ATTEMPTS TO SKIP IT, ALL MEASURING NOTHING:**

1. `l2c60(g_bar_dirty)` instead of `l2c60(1)` (#138) — 81-88 tk either way.
   The rebuilt items come back with the painted bit (0x80) CLEAR, so force=0
   repaints them anyway.
2. Mark every rebuilt item painted after the relayout — still 13/13 painted,
   93-100 tk. `l2c60`'s head runs `l30ba(0, count-1, 0)` when `g_a5_-9247` is
   0 (which the rebuild re-arms), and that pass sends cmd 0 to every item;
   `jt137`'s cmd-0 arm clears the bit again.
3. Mark painted AND set `g_a5_-9247 = 1` to suppress that pass — **still
   92-100 tk.** Something else clears the painted state between the relayout
   and the HUD block.

All three reverted. **What is established: the repaint is provably redundant
(0 px, three times), it costs 1.4-1.7 s an event message, and the painted bit
is being cleared by something between `play_screen_relayout` and `l2c60` that
attempt 3 did not account for.** Finding that clearer is the next step — and
it wants a trace of `rec[28]` across the interval, not another guess.

★ Three attempts, three non-results, all reverted. Each hypothesis was
plausible and each was refuted by the same instrument in one run. The cost of
being wrong here is one drive; the cost of shipping any of them unverified
would have been a change that does nothing, in a path that already has four
mislabelled comments.

`FRUA_BARDAMAGE` is kept — it is what proved the repaint redundant, and it is
what will confirm a real fix.

### #140 THE TRACE — the painted bit is FINE; the SCREEN IS WIPED 1-7x PER COMPOSE

Tracing `rec[28]` across `play_screen_relayout` -> `l2c60`, with the pool
identity alongside (the thing #139's three attempts never checked):

| point | pool ptr | count | `g_a5_-9247` | `item6[28]` |
|---|--:|--:|--:|--:|
| after relayout | 1374124 | 13 | 1 | **0xB0** |
| l2c60 entry | 1374124 | 13 | 1 | **0xB0** |
| after l30ba | 1374124 | 13 | 1 | **0xB0** |

**Every assumption behind #139's attempts was wrong.** The pool is NOT
reallocated. `g_a5_-9247` is already 1, so `l30ba`'s "mark all dirty" pass
never runs. And `item6[28] = 0xB0` — **bit 7 is SET**: the items are already
marked painted, by `jt137` itself, without any help. Nothing clears the painted
state at all. The only thing forcing the repaint is the literal `1` in
`l2c60((short)1)`.

So why did passing `g_bar_dirty` change nothing? **Because `g_bar_dirty` is
correctly 1 every time** — and the reason is the real finding:

    compose   port_draw_play_frame calls since the previous compose
      1                 7
      2                 1
      3                 1
      4                 1

**`port_draw_play_frame` — the full-screen wipe plus the whole FRAME.CTL chrome
re-lay — runs one to SEVEN times between composes**, including on HUD-only
composes where `jt312`'s chrome branch is skipped entirely. It has three call
sites; `jt312`'s is only one of them, and `render_3d_faithful` carries another
behind `s_chrome_drawn`.

**This reframes the chrome accounting.** #125e/#126 measured "the chrome phase"
through `jt312`'s bracket and got 611 -> 267 ticks. That bracket only ever saw
ONE of the wipes. The others are outside every timer built so far — which also
explains #126's otherwise odd count of 20 `l67ca` runs against 5 full composes,
recorded at the time and not followed up.

So the bar repaint is not the target after all: **it is downstream of a screen
that is genuinely being wiped several times per compose.** Fixing the bar would
paint a bar that the next wipe destroys.

★ FOUR attempts across #138-#140 to make the bar cheaper, all reverted, all
refuted by measurement. The trace shows why every one of them had to fail: they
targeted a paint-state mechanism that was already working correctly. **The
question "why is the relayout unconditional?" had a good answer (the modal
resets the pool) that was simply not the reason for the cost.**

**NEXT, and it is bigger than the bar**: find the other `port_draw_play_frame`
callers and ask the #130 question of each — does this wipe change anything? At
1-7 wipes per compose, with one wipe measured at 611 ticks before #125e, this
is likely the largest remaining item in the whole play loop, and it has been
hiding behind a bracket that only counted one of them.

### #141 THE OTHER WIPERS — it is `render_3d_faithful`, re-armed by a stale CLUT flag

`port_draw_play_frame` has FOUR call sites. Counting each per compose:

| compose | play re-entry | **`render_3d` !s_chrome_drawn** | AREA map | `jt312` chrome |
|---|--:|--:|--:|--:|
| 2 | 1 | **5** | 0 | 1 |
| 3 | 0 | 0 | 0 | 1 |
| 4 | 0 | **1** | 0 | 0 |
| 5 | 0 | **1** | 0 | 0 |

**The extra wipes are `render_3d_faithful`'s own `if (!s_chrome_drawn)`** —
which nothing outside that function ever timed. `s_chrome_drawn` is cleared by
one thing: the wall-group reload block. And that block is firing for the wrong
reason:

    wallreload: clob = 1   ds456 = 50801   grp012 = 50801

**The wall ids MATCH — 50801 both sides. The reload is triggered purely by
`g_clut_clobbered`, every single frame.** So each frame reloads three wall
groups and the backdrop FROM DISK, clears `s_chrome_drawn`, and re-lays the
entire FRAME.CTL chrome, none of which the wall ids asked for.

`g_clut_clobbered` is set in `jt993` (a GLIB palette commit) whenever
`start < 145` — i.e. any picture install that lands below the backdrop band.

**Bisected so far — where it is NOT set:**

- **not the render**: `clob` is **0** at the end of `render_3d_faithful`, every
  frame (the reload block's own clear holds);
- **not the command bar**: 0 both before and after `l2c60`, so the plate blits
  are innocent — which also retires the #138-#140 theory that the bar was
  implicated;
- **not `dungeon_view_setup`**: 0 across it.

So the setter lies in a NARROW interval: **between `dungeon_view_setup()`
returning and `render_3d_faithful`'s wall-reload check** — i.e. `jt312`'s own
backdrop pick (`cell_backdrop_id` / `load_backdrop`) and the wall-group block
that sits between them. That is a handful of calls, and the same
before/after `clob` probe finishes it.

**Why this matters more than anything else outstanding**: a single
`port_draw_play_frame` measured **611 ticks** before #125e and **267** after.
At one to five of them per compose, plus three wall-group loads and a backdrop
load from DISK each time, this is very likely the largest single item left in
the play loop — and it has been invisible because every chrome timer built so
far brackets `jt312`'s call site, which is not the one firing.

★ It also explains, at last, #126's *"20 `l67ca` runs against 5 full composes"*
— recorded, flagged as odd, and left alone for fifteen entries.

## #160 THE TT WRITER HALF — DONE, AND IT WAS MOSTLY A BUILD FLAG (2026-08-01)

The deferral above gave two reasons. Both turned out softer than they read.

**"It is not a display-file change."** It very nearly is. The whole writer layer
in `compat/quickdraw.c` dispatches through `DC_PUT` / `DC_C2P` / `DC_SPAN`,
whose non-Amiga arm is `planar_*_stlow` — and those are already **generic in
`nplanes`** (`slot >> p` for `p < nplanes`, with `slot` a byte, which covers 8
planes exactly). TT-Low is ST-Low's word-interleave with 8 planes instead of 4.
So not one line of the writer layer changed. What was needed:

- `display_tt.c`: a draw-time plane buffer + cov/idx/rowcov, an identity remap,
  `tt_dt_target()`, and `planar_draw_target_register()` at init;
- `tt_blit_rows`: ask `tt_dt_ready_row(y)` first and copy the row out of the
  plane buffer instead of converting it;
- the Makefile flag.

**The TT is the SIMPLE case, like AGA, not like the ST.** 8 planes = 256
colours and the palette is hardware (`hw_palette`, #99), so the remap is the
IDENTITY: no bands, no re-band, no epoch reset, no new-ink trigger. A stamp can
never be invalidated, so there is nothing to reset. `tt_dt_ready_row` is
`aga_dt_ready_row` with a different span converter, self-healing ownership
included — a row an engine-direct blitter drew converts ONCE, is then claimed
(`cov=1`, `idx=chunky`, `rowcov=W`) and skipped thereafter.

**The line doubling never reaches the writers.** Engine row `y` lands on screen
rows `TOP_BORDER + 2y` and `+1`. The stamp buffer is the ENGINE frame
(320x200 interleaved) and the doubling stays at present time, where it always
was — which is why the ST's writer contract needed no row-mapping extension.

**"The headroom left is small."** Measured, same scripted drive, 176 presents:

| build | presents | rows presented | rows CONVERTED |
|---|---|---|---|
| chunky (#99 dirty-row present) | 176 | 23 748 | 23 748 |
| draw-time planar | 176 | 23 894 | **3 024** |

20 870 of 23 894 rows were already writer-stamped: an **87% cut in conversion
work** on top of what #99 removed. The "~6%" in the deferral was the share of
the ORIGINAL 95 092-row figure still being converted; as a fraction of the work
the present still does each frame, it was 100%.

**Byte-identical on both machines**, chunky vs draw-time, walk view + the
post-AREA-toggle frame: TT 2/2 and **Falcon 2/2**. The Falcon check is the
load-bearing one — Falcon and TT share ONE 020 binary, and this switch enables
the whole writer layer in it. VIDEL never registers a target, so
`dsp_planar_draw_target()` returns 0 and every writer takes its chunky store.

**`FRUA_PLANAR` is now the default on EVERY target**, not just `CPU68K=68000`.
Gating on the CPU would have kept the TT's new path out of every dev build and
every emulator soak — the exact "the binary under test is not the binary that
ships" gap that making it the 68000 default was meant to close. It also closes
the same gap for AGA, whose zip has carried the flag since 0.5.1 while
`make MACHINE=amiga` compiled the chunky path. `PLANAR=0` still opts out.

★ **Trap, and it cost a wrong conclusion for one run.** The first TT A/B came
back "DIFFERS" with the planar PNG 6 KB larger — which reads exactly like
plane corruption. It was the HARNESS: the drive's sleeps were tuned on the
Falcon, the planar build shifts the per-present timing, and the entry chain
desynced so the run parked on the TREASURE screen while the baseline was in the
walk view. Two different game states, not two renderings. Re-paced for the TT
(~2x) both runs reached 10,8 facing north and the frames were byte-identical.
**Confirm both arms are in the same state before believing a pixel diff.**

**NOT measured: wall-clock.** Same caveat as #99 — under Hatari the host sets
the frame pace, so only conversion WORK is meaningful here. This wants real
hardware, and the TT now has a reason to be measured on it.


## B5 — the viewport stops converting (#144, 2026-08-18)

The dungeon viewport is now stamped in planes at draw time and composited by a
copy. See the ADR-0016 addendum in `docs/decisions.md` for the interface and the
numbers; this section is the traps, which cost most of the session.

**The verifier has a structural blind spot, and it is not a small one.**
`FRUA_VPPLANAR` compares the planes against the chunky pixels — so it only ever
runs in a build where the chunky path is ALIVE. That is its reference. Every
counter it reports (`uncovered`, `MISSED`, `MISMATCH`) can therefore read zero
while the planes-only build is broken, because the thing that breaks is the
REMOVAL of the reference. Two regressions proved it: the planar backdrop was
building its strips out of `vtgt`, so deleting the chunky pass took its own
source with it and the sky and floor went black; and the palette derivation
above. A screenshot caught the first, the backend's own comment named the second.
**Green there means "the two paths agree while both run", never "the planar path
stands alone."**

**A composite that never engages is pixel-identical to one that does.** If the
copy path silently falls back to the c2p — engine hands back NULL planes, the
commit takes the chunky branch, anything — the screen is exactly right. That is
the one failure this change cannot be caught by looking at, so each path logs a
one-shot marker and DBG.LOG says `COPY (B5)` or `c2p (fast)` outright.

**Measure the A/B on ONE binary.** `vpplanar=off` in `video.cfg` (same reader as
`display_nova.c`'s `novalut`) selects the arms without a rebuild. Two builds
differ in layout and codegen, which is how a performance claim quietly becomes a
claim about the compiler. The chunky arm reproduces an older build's screenshot
at AE = 0, which is also what establishes that the fixture is deterministic
enough for a pixel diff to mean anything.

**Shares are a trap in a fixed-time window.** The profile window is 35,000 VBLs
of EMULATED time, so total cycles in it are conserved (2,976M vs 2,968M across
the arms, 0.29% apart). When rendering gets cheaper the freed cycles are spent on
more game-loop iterations, so every other function grows and DILUTES the share of
the thing that shrank — `l309c_tile` went 23.4% -> 18.2% while actually dropping
157.7M cycles. Read absolute cycles, and check where the saving went (here: 20%
to TOS idle, the rest to more frames).

**"No output" and "a reading of zero" are the same text.** Four consecutive runs
produced no lazy-pass counters at all; each time the log threshold sat above the
render count (8 renders against a threshold of 512, then 64, then 8). The fix is
to fire on the FIRST call while investigating. The same shape bit three other
checks in this work, each needing a second measurement whose only job was to
prove the first could fail: `STAMPED` beside `SURVIVORS` (a sentinel that might
never have been written), `rebands seen` beside `reband-on-stale` (a check that
might never have run), and a whole-row `ROW DIFF` beside the restricted
`band DIFF` (a comparison that might have had nothing to find — it did not, and
that is how the single replicated palette was discovered).

**Build every target, not the one you are iterating on.** The plane fetch went in
outside its `#ifdef` and broke AGA, ECS, the Atari 020 and the shipping ST while
the ST diagnostic build stayed green throughout.

**Still open:** the chunky pass survives as a rare palette-sampling pass (~1 frame
in 4 measured), and the Amiga is not on B5 — its backends do not register
`dsp_viewport_planes`, so ECS/AGA keep the chunky scratch and their own
composite.

## #141 — the speckle is the BUDGET, not a stale remap (2026-08-19)

The task title guessed "stale remap suspected". It is not. Two findings, and the
second one invalidates a test gate rather than any shipped code.

**It is not intermittent — the HARNESS was.** Eight boots of the slot-B walk
fixture gave AE = 0 six times and AE = 61,848 twice, and the two bad frames are
byte-identical to EACH OTHER. Diffing the palette traces (FRUA_PALTRACE) shows
the runs agree for 50 events exactly and then the "bad" ones simply continue:
three small palette writes, a re-band, then ~80 `setpal first=97 count=6`
(palette cycling). They did not diverge, they PROGRESSED — to the complete walk
screen WITH the command bar, while the clean runs were photographed before the
bar was drawn. A 70 s settle after `b` reproduces the speckle every time.

**So the AE reference frame used throughout #144/#142 is an INCOMPLETE state.**
`st-dungeon-skipfill.png` has an empty command-bar box. Every AE = 0 in that work
compared one pre-command-bar frame against another. It exercised the 3D viewport,
which is what those changes touched, so their conclusions stand — but it never
covered the HUD/command-bar palette path at all, which is exactly where this bug
lives. **A walk AE gate must wait for the command bar** (or it silently tests
less than it looks like it does).

**The number that settles the cause:** distinct CLUT indices per re-band over a
fixture boot — 7, 84, 59, 59, 129, 129, 182, 182, 32, 32, 16, 20, **64, 66**. The
complete walk frame asks for 64-66 colours against ST_NCOL = 16 slots, and the
re-band that produces the speckle runs with `vp_active=1 chunky_ok=1`, i.e. with
the viewport folded into the quant source CORRECTLY. Nothing is stale. The bar's
inks join the competition for QUANT_KEEP's six exact slots, wall colours lose,
and the median cut puts greys on red/cyan.

That is a hue error rather than an approximation, so it is one of: the 16 chosen
colours carry no neutral, or the nearest-match metric mis-picks. Both belong with
#138/#139 (offline pre-quantise), not with a remap repair.

## The viewport's own palette — B1's split, put back on the ST (#139)

ADR-0016 B1 collapsed the ST's per-band palettes into ONE cut replicated to
every band, because per-band palettes produced the #40 seams: a flat panel
spanning a band boundary rendered in stripes. That reasoning holds for
boundaries chosen by arithmetic. It does not hold for a boundary placed exactly
where the content changes, and the dungeon screen has two such lines — the top
and bottom edges of the first-person viewport.

It is ON BY DEFAULT since 2026-08-20. `vpbands=off` in `video.cfg` is the escape
hatch. Still a RUNTIME knob rather than a compile-time one, and that stays true
now the default has flipped: one binary has to hold both arms or an A/B is
comparing two builds.

Both directions are verified on the fixture walk with one binary — no
`video.cfg` gives 21 distinct colours in the game area and **AE=0 against the
opt-in `vpbands=on` render**; `vpbands=off` gives 15 and **AE=0 against the
pre-split render**.

### What it buys

Measured with `tools/quant/qvp` on the captured walk frames, against the true
CLUT colour of every pixel (mean squared RGB):

|                       | viewport | whole screen |
|-----------------------|---------:|-------------:|
| one group             |    392.5 |        155.0 |
| three groups          |    232.5 |        107.2 |
| change                |     -41% |         -31% |

The second walk capture agrees (395.7 → 245.3, 161.0 → 106.9). **Both** halves
improve — the chrome is not paying for the split, because it was the chrome's
own colours crowding the viewport out in the first place. On screen this shows
up as the night sky, which the single cut rendered near-black and the split
renders blue, and the frame carries **24 distinct colours instead of 16**.

### What it costs

Matched present counts, same binary, only the knob differs:

| presents | wall ticks off | wall ticks on |
|---------:|---------------:|--------------:|
|       16 |         13,814 |        13,814 |
|       32 |         19,980 |        19,949 |
|       48 |         23,580 |        24,216 |

Identical through the boot — there is no viewport yet, so both run one group —
and +2.7% by 48 presents. The MARGINAL cost over the dungeon phase alone
(presents 32→48) is **+18.5%**, and that is the number to quote: the split only
exists on dungeon screens.

### Where that 18.5% went

Almost all of it was Timer B. The raster resolution had to go from 20 rows to 8
(the viewport is at y=24, 88 tall, and 24/112/200 are all multiples of 8; 20
cannot express those boundaries), so the handler fired 25 times a frame where
only TWO of those boundaries are a real palette change.

Two fixes were tried, in order, and only the second worked:

1. **A fast exit for unchanged bands** — a flag array tested before the d0-d7
   save, so a band inside a group cost a test, a pointer bump and an `rte`.
   Measured **807 cycles/fire against 771 for the full handler**: no change.
   Entry and exit are the whole bill at this cadence. Reverted.
2. **Variable TBDR** — reprogram the timer per fire so it fires only at group
   boundaries, twice a frame instead of twenty-five times. This is the one that
   worked:

| presents | off | 25 fires | 2 fires |
|---------:|----:|---------:|--------:|
|       16 | 13,814 | 13,814 | 13,649 |
|       32 | 19,980 | 19,949 | 19,851 |
|       48 | 23,580 | 24,216 | 23,535 |

The marginal dungeon-phase cost (presents 32→48) goes from **+18.5% to +2.3%**,
and the rendered frame is **AE=0 against the 25-fire version** — the split lands
on exactly the same scanlines.

#### How the reprogramming works

MFP68901 event-count mode: a write to TBDR while the timer RUNS sets the data
register only — the counter keeps the value it reloaded at the last underflow —
so a written interval always takes effect one fire later. Each fire descriptor
(`struct st_tb_fire`) therefore carries both halves:

- `expect` — the counter's reload while THIS fire runs. The spin waits for the
  counter to drop below it, which is how the boundary line's display ending is
  detected. It used to be the constant `ST_RPB` and cannot be now.
- `next` — what to program into TBDR during this fire, i.e. the interval that
  will run after the NEXT one.

The last live fire programs 255, which cannot underflow inside 200 display
lines, so the frame ends with no further interrupts and the VBL re-phases from
scratch. Trailing sentinels make a late-serviced request (the #91 case)
harmless instead of a walk off the table.

The handler needed a scratch register for the now-variable compare, so the
palette loads into **d0-d6 and a1** rather than d0-d7 — still eight registers
and 32 bytes, and `moveml`'s register order is fixed, so the load and the store
agree.

### The one thing an emulator cannot settle

The one-line-early fire plus the spin is timing-sensitive, and Hatari's MFP is
not a real MFP. Everything else about the split has been measured here, but that
part has not been seen on hardware. If a real ST ever disagrees — a palette
landing a line early or late, a torn band edge — `vpbands=off` is the first
thing to reach for, and it restores a byte-identical pre-split frame.

**`st_prof_tbcost` no longer prices this.** It armed Timer B by force, which
worked when the timer fired at a fixed cadence regardless of content; forcing it
on a uniform screen now arms a timer with nothing scheduled. It refuses and says
so rather than reporting the cost of an idle timer as the cost of the split.
The wall-clock fixture A/B above replaces it — one binary, `vpbands` on vs off.

### Screens checked with it on

| screen | groups | result |
|---|---|---|
| boot, title, main menu | 1 | unchanged (15-16 colours) |
| dungeon walk | 3 | 24-27 colours, correct |
| town / BIGPIC event (innkeeper, tavern, barbarian portrait) | 3 | 19-20 colours, correct, roster text keeps its red/cyan |
| treasure / XP | 3 | 19-20 colours, correct |
| combat | 1 | **16 colours exactly** — one palette, correct |

Three groups **only** on screens that commit a viewport. Everything else stays
at one.

Combat is measured, not inferred: with `vpbands=on`, the walk's game area
carries **21** distinct colours and the combat screen carries **exactly 16**,
with a uniform border in both. That is the direct observable for "the split is
not engaged" — `st_group_layout` requires `s_vp_active`, which only
`st_vp_commit` sets, and combat does not render the first-person viewport.

Reaching it headlessly needed #147 fixed first: the `FRUA_CBTAUTO` auto-fire
gated on `g_a5_27990 == 4`, which is already true during the boot, so it fired a
combat with no party seated — wedging the engine, or bus-erroring at `$7f80a`
once `FRUA_CBTPLAY` walked the party that was not there. The gate is now
`g_cbt_walk_live`, set by l63c0's poll, so it can only come true after a real
walk loop has run.

### ★ THE BORDER BANDS, AND CROPPING TO THE IMAGE HIDES IT

Colour register 0 is what the ST shows in the **border**, and with a raster split
every group gets a turn at it. Three independent cuts put three different
colours there, and the border grows a horizontal band exactly as tall as the
viewport — measured on the left border strip, `#555544` above and below against
`#555555` inside.

It survived the first round of checks because every comparison cropped to the
320x200 image, where there is nothing wrong. It was found by looking at a full
screenshot.

`st_unify_border()` fixes it: in each later group, find the slot nearest group
0's slot 0, permute it into position 0 (palette and remap together, so no pixel
changes meaning), then set its RGB to group 0's exactly. `st_repalette` calls it
with `may_permute = 0` — that path's whole premise is that slot numbering does
not move, so the planes on screen stay valid and no force-full is needed;
permuting there would renumber slots behind those planes, which is the "brown
chrome" failure. `st_reband` passes 1, having just re-quantised.

### The same bug was already shipping on ECS, with 25 bands

`display_ecs.c` has run 25 real per-band cuts over 32 colours since the copper
palette went in — the copper reloads all 32 registers at every band boundary for
free, so it never had the ST's interrupt-cost problem and never needed the
group machinery. But COLOR00 is the Amiga's border too, DIW is exactly 320x200,
and the copper rewrites COLOR00 along with the other 31. Twenty-five
independent cuts, twenty-five different border colours.

Measured on a boot frame, down a single column of the left overscan:

| | colours | runs |
|---|---:|---:|
| before | 21 | 28 |
| before, display rows only | 16 | 21 |
| after `ecs_unify_border` | 3 | 4 |
| after, display rows only | **1** | **1** |

Changing every 8 scanlines, one of the stripes pure white. Same fix as the ST's
`st_unify_border` — nearest slot permuted into position 0, RGB forced to band
0's — with the same `may_permute = 0` on the repalette path, which there forces
the copper WORD alone.

AGA is unaffected: 256 colours, one palette, no band split.

### Traps found on the way here

- **The band count is the RASTER RESOLUTION, not the palette count.** Groups
  (`s_ngrp`, `s_grp_b0`) are runs of bands sharing one cut. Every band inside a
  group holds an identical copy, so a tile straddling a band boundary within a
  group still bakes the right slots — only a group boundary is a real edge, and
  those sit on the viewport's own edges. This is what let `ST_NBANDS` go to 25
  without touching `band = y * nbands / h` in the c2p or in any of the engine's
  draw-time writers.
- **`s_ngrp == 1` has to be byte-identical to the old path**, and it is:
  the same scripted walk gives AE=0 against the pre-change build, despite
  `ST_NBANDS` going 10 → 25.
- A layout change between re-bands has no usable predecessor — the middle
  group's previous cut covered different rows — so it is treated as the first
  re-band rather than aligned against nonsense.
- The no-force-full guard tested `s_have_prev_pal`, which `st_reband` sets to 1
  a few lines above it, so its no-predecessor arm was dead and the first
  re-band compared against an all-zero previous remap. It forced a rebuild
  anyway (index 0 is almost never slot 0 in a fresh cut), but by luck. It tests
  `first` now.
