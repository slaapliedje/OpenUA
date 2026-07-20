# Native planar rendering plan (ADR-0016)

Branch: `planar-native`. Goal: the bitplane machines (ST/STe, Amiga ECS/OCS)
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
