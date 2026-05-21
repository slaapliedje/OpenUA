# FRUA — Atari Falcon030 / TT030 port

A port of SSI's *Forgotten Realms: Unlimited Adventures* (1993) to the Atari
Falcon030 and TT030.

The port is based on the **Macintosh 68k release**. Because the Mac version is
already 68k machine code and the Falcon/TT are 68030 machines, the CPU code
carries over directly — the work is retargeting the Mac Toolbox to Atari
TOS/GEM/XBIOS and rebuilding the display and sound paths.

## Status

Early development. `make` builds `frua.prg`; it is still a VIDEL
display-backend demo — `make run` boots it in Hatari — as the decompiled
engine is not yet wired as the entry point. Built so far:

- **Display** (`platform/`) — the Falcon VIDEL 256-colour backend: mode
  switch, chunky-to-planar conversion, palette; verified in Hatari.
- **Engine** (`src/engine/`) — lifted from the Mac `CODE` segments: the
  application bootstrap (`ua_main`), core init, the master init/shutdown,
  the file-cache subsystem, and the allocator / string / RNG / error
  helpers. Not-yet-lifted callees are no-op stubs that mark the frontier.
- **Toolbox shim** (`compat/`) — the Memory Manager (`Ptr` and relocatable
  `Handle`) is complete; QuickDraw has the geometry core, the `GrafPort` /
  `CGrafPort` types and `NewPixMap`; the Window Manager creates b&w, colour
  and resource-loaded windows; the Resource Manager reads the flat FRSC
  archive; the Toolbox-startup traps are stubbed.
- **Tooling** (`tools/`) — the `dis68k.py` decompiler, the resource-fork
  extractors, and `rsrcpack` (Mac fork → FRSC archive), with a host test
  suite (`make test`).

Drawing (QuickDraw primitives onto the HAL surface), sound, events and the
File Manager are the main pieces still ahead; `docs/` has the subsystem
maps and the lifting workflow.

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
