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

### TT030 — NOT YET NATIVE PLANAR (added to the plan 2026-07-26)

`platform/display_tt.c` converts **every row of every frame**, unconditionally:
TT-Low is 320x480 in 8 word-interleaved bitplanes, the engine's 320x200 chunky
frame is line-doubled into it, and `tt_c2p_span` runs over the lot. None of
ADR-0016's draw-time machinery (dt buffer, ownership, row skip) reaches this
backend.

It is a planar machine, so the ADR applies to it in principle. It was left out
deliberately, not by oversight: **a 32 MHz 68030 has enough raw power to sort of
play as-is**, which is exactly what the ST/STe and Amiga ECS lack — so the
scarce effort went where the machines could not cope. That reasoning still
holds; this entry exists so the gap is recorded rather than rediscovered.

Notes for whoever picks it up:
- The AGA port (#86) is the closest template — 8 planes, and if the TT palette
  is used as an identity map the remap collapses the same way AGA's did, which
  is what made AGA ~90 lines instead of ST's several hundred.
- The line-doubling is the wrinkle AGA did not have: a draw-time writer stamps
  one source row that must land in two planar lines.
- Do the reband work above FIRST if it lands — on the evidence, ownership is
  worth little while the epoch is being reset every other present.

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
