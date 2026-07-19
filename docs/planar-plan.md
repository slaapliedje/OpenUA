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

### Staging constraint (found during Phase 2 scoping)

The 8bpp surface is **shared**: ~30 distinct engine sites (plus the QuickDraw
shim primitives) grab `qd_screen_pixels()` and write chunky indices directly —
the dungeon leaf (`l309c_tile`), text (`DrawChar`), fills, chrome, cursor, event
pictures, the automap, etc. As long as **any** one writes chunky, the surface is
chunky and the backend must c2p it. So "drop chunky + c2p" cannot be one commit;
it is the *last* step, after every writer is planar. The end state is unchanged
(full native planar, c2p gone); the phasing converts writers in value order and
keeps the frame coherent during transition with a **composite**: the backend
still c2p's the chunky surface (row-diffed → nearly free for the mostly-static
UI) and then blits the native-planar regions over it. c2p disappears only when
the last chunky writer is converted and the surface itself becomes planar.

**Phase 2 — native-planar dungeon viewport (composited).**
- The dungeon viewport is the hot redraw path (it repaints every step; the UI is
  mostly static). Render it natively planar into a **separate planar viewport
  buffer** instead of the chunky hole: `FRUA_PLANAR` fork in `l309c_tile`
  (`boot.c:12709`) + `cw_blit_piece` writes plane bits via `planar_blit`; wall
  pieces convert to planar once at `cw_load_slot`/`cw_finalize`
  (`boot.c:11628`/`11764`) into a store beside `g_cwf_body[]`.
- Backend: after the (row-diffed) c2p of the chunky surface, **blit the planar
  viewport buffer into the viewport hole** (the 88×88 colour / 176×176 mono
  region). The per-step cost drops from "c2p the viewport" to "plane-blit the
  viewport" — the win, incrementally, on the path that redraws every step.
- Palette: the viewport band's hardware palette carries the wall-set colours
  (base 32/69/106), so pieces convert with a near-identity band remap; the UI
  bands keep the UI palette (the per-band copper split the ECS backend already
  runs). Grab the walk-step baseline first so the win is a real number.
- Verify: the 3D dungeon renders identically to the chunky build
  (screenshot-diff) in amiberry (ECS) + Hatari (ST/STe).

**Phase 3 — convert the remaining chunky writers; drop the composite.**
- Convert the rest of the ~30 surface writers to planar in value order: chrome
  (static → convert once), `DrawChar` text (`compat/quickdraw.c:2212`), rect
  fills (`jt1161`/panels), GLIB art blits (bigpics/backdrop), cursor, automap.
- When the last writer is planar, the surface itself becomes planar: **remove the
  composite and the c2p** — backend `present` is a flip. Chunky is gone on
  `FRUA_PLANAR` builds.

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
