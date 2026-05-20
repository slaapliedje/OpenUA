# FRUA — Atari Falcon030 / TT030 port

A port of SSI's *Forgotten Realms: Unlimited Adventures* (1993) to the Atari
Falcon030 and TT030.

The port is based on the **Macintosh 68k release**. Because the Mac version is
already 68k machine code and the Falcon/TT are 68030 machines, the CPU code
carries over directly — the work is retargeting the Mac Toolbox to Atari
TOS/GEM/XBIOS and rebuilding the display and sound paths.

## Status

Project scaffolding. `make` builds a toolchain smoke-test `.prg`; the engine,
Toolbox shim, and platform backends are not yet implemented.

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
make clean      # remove build output
```

## Layout

```
src/        port bootstrap + decompiled engine (src/engine/)
compat/     Mac Toolbox compatibility shim
platform/   Atari hardware abstraction layer
toolchain/  cross-toolchain configuration
tools/      host-side extractors / converters
docs/       architecture, decisions, Toolbox mapping
data/       original FRUA assets (git-ignored)
```

## Legal

This repository contains only port code. It ships no original FRUA assets or
binaries — those are copyrighted by their rightsholders and must be supplied
separately (see [`data/README.md`](data/README.md)).
