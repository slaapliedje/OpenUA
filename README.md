# FRUA — Atari Falcon030 / TT030 port

A port of SSI's *Forgotten Realms: Unlimited Adventures* (1993) to the Atari
Falcon030 and TT030.

The port is based on the **Macintosh 68k release**. Because the Mac version is
already 68k machine code and the Falcon/TT are 68030 machines, the CPU code
carries over directly — the work is retargeting the Mac Toolbox to Atari
TOS/GEM/XBIOS and rebuilding the display and sound paths.

## Status

Early but interactive. `make` builds `frua.prg` and (if the source
resource fork is present) packs `frua.rsc` from it. `make run` boots
the build in Hatari: the engine entry runs end-to-end on top of the
shim, then a small post-engine probe opens the three real `WIND`
resources from the resource fork, stacks them on the desktop in the
real CLUT-129 palette, and hands the mouse over — click-to-front,
title-bar drag with an XOR outline, keypress to exit.

What's wired:

- **Display HAL** (`platform/`) — Falcon VIDEL 256-colour back buffer
  with mode switch, palette load, chunky-to-planar present; an `input`
  HAL with a Supexec-installed IKBD-packet mouse driver and a 60 Hz
  tick counter scaled from `_hz_200`.
- **Toolbox shim** (`compat/`) — complete enough to drive the demo above:
  - **Memory Manager** — `Ptr` and relocatable `Handle` blocks
    (`HLock` / `HUnlock` are bookkeeping; the heap doesn't relocate).
  - **QuickDraw** — geometry, the `GrafPort` / `CGrafPort` types, regions,
    every rect / line / oval / blit / clip primitive, pen size / mode /
    pattern, RGB foreground/background with nearest-CLUT lookup, and a
    text family (TextFont/Size/Face/Mode + DrawChar/DrawString/CharWidth/
    StringWidth) over an embedded 8x8 fallback font.
  - **Window Manager** — full lifecycle, frames painted into the screen
    port with active-vs-inactive title-bar styling, FindWindow / DragWindow
    with an animated XOR outline, TrackGoAway.
  - **Event Manager** — `EventRecord`, posted FIFO, `TickCount`, BIOS
    keyboard pump, IKBD-driven mouse-edge synthesis for mouseDown /
    mouseUp, updateEvt synthesis from non-empty `updateRgn`s,
    `WaitNextEvent` with a sleep tick.
  - **Resource Manager** — reads the flat `(type, id)` FRSC archive,
    plus `OpenResFile` / `UseResFile` / `CurResFile` / `CloseResFile` /
    `HomeResFile` / `CreateResFile` over a refnum table.
  - **File Manager** — `FSOpen` / `FSRead` / `FSWrite` / `FSClose` /
    `GetEOF` / `SetEOF` / `GetFPos` / `SetFPos` / `Create` / `FSDelete` /
    `GetVol` / `SetVol` / `FlushVol` / `GetFInfo` / `SetFInfo` over
    GEMDOS file calls.
  - **Toolbox startup** — `toolbox_init` runs the seven manager inits in
    Mac order; the engine's `master_init` calls it.
- **Engine** (`src/engine/`) — lifted from the Mac CODE segments:
  `ua_main` runs to completion, `core_init`, `master_init` / `_shutdown`,
  the `fc` file-cache subsystem, allocator / string / RNG / error
  helpers, and an early frontier on CODE 6: `L07dc` (the play-loop
  body), `L5124` (its first-time init), `jt942` / `jt943` (the loop
  predicate flag pair), `JT[399]` (the engine's memset), `jt174`, `jt76`,
  `jt480` (the string-table setter), plus a `jt918` entry-only skeleton.
  Unlifted callees are PROBE-instrumented stubs — `make ENGINE_PROBE=1`
  emits the per-frame call sequence.
- **Tooling** (`tools/`) — `dis68k.py`, the resource-fork extractors,
  `rsrcpack` (Mac fork → FRSC archive), and a host test suite
  (`make test`).

Still ahead: the THINK C `DATA + DREL` replay (real `ua_get_string`
mapping for every index), the Dialog Manager (`ALRT` / `DLOG`), the
Sound Manager, real NFNT font loading, and the engine's gameplay
segments. `docs/` has the subsystem maps, the bring-up probe log,
and the lifting workflow.

## Approach

| Aspect       | Decision                                                      |
|--------------|---------------------------------------------------------------|
| Source base  | Decompile the Macintosh 68k release                           |
| Decomp form  | Hybrid — recompilable C, with 68k asm where it resists lifting |
| Mac Toolbox  | Compatibility shim first, native Atari APIs later             |
| Toolchain    | m68k-atari-mint GCC cross-compiler                            |
| Display      | Hardware abstraction layer: VIDEL / TT-shifter / VDI          |

See [`docs/architecture.md`](docs/architecture.md) and
[`docs/decisions.md`](docs/decisions.md) for the full rationale.

## Building

Requires the [m68k-atari-mint](https://github.com/vincentariel/m68k-atari-mint)
GCC cross toolchain on `PATH` (override the prefix with `make CROSS=`).

```sh
make            # build frua.prg (soft-float; runs on Falcon030 and TT030)
make FPU=1      # FPU-required variant tuned for the TT030
make run        # boot the build in the Hatari emulator (Falcon mode)
make test       # host-side test suite (pytest over tools/)
make clean      # remove build output
```

## Layout

```
src/        port bootstrap + decompiled engine (src/engine/)
compat/     Mac Toolbox compatibility shim
platform/   Atari hardware abstraction layer
toolchain/  cross-toolchain configuration
tools/      host-side extractors / converters
tests/      host-side test suite
docs/       architecture, decisions, Toolbox mapping
data/       original FRUA assets (git-ignored)
```

## Legal

This repository contains only port code. It ships no original FRUA assets or
binaries — those are copyrighted by their rightsholders and must be supplied
separately (see [`data/README.md`](data/README.md)).
