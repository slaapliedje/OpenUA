# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

A port of SSI's *Forgotten Realms: Unlimited Adventures* (FRUA, 1993) to the
Atari Falcon030 and TT030, based on a decompilation of the **Macintosh 68k**
release. Mac 68k → Atari 68030: the CPU code carries over; the work is
retargeting the Mac Toolbox and the display/sound paths.

## Ratified decisions (see docs/decisions.md before changing course)

- Port from the **Macintosh** release, not MS-DOS (ADR-0001).
- **Hybrid** decompilation: recompilable C, with 68k asm where lifting is hard
  (ADR-0002).
- **Mac Toolbox compatibility shim** first; migrate to native Atari APIs later
  (ADR-0003).
- **m68k-atari-mint GCC** cross toolchain (ADR-0004).
- **Display HAL** with VIDEL / TT-shifter / VDI backends (ADR-0005).
- **Editor UI** reimplemented inside the Toolbox shim, not mapped to AES
  (ADR-0006).
- **Resource fork** delivered as a flat `(type, id)` archive built by
  `tools/rsrcpack` (ADR-0007).

## Layer rule

`src/engine/` (engine) → `compat/` (Mac Toolbox shim) → `platform/` (HAL) →
TOS. Only `platform/` may know which machine it runs on. Engine code must not
call XBIOS/TOS directly — go through the shim or the HAL.

## Build / test

```sh
make            # build frua.prg (soft-float — runs on Falcon030 and TT030)
make FPU=1      # FPU-required TT030 variant
make run        # boot in Hatari (Falcon mode)
```

Default build is **soft-float** so one binary serves the FPU-less Falcon030
and the 68882-equipped TT030. Do not assume an FPU in shared code.

## Conventions

- C is `gnu99`; tabs for indentation (matches existing files).
- Keep ratified decisions in `docs/decisions.md` (append-only ADRs).
- Update `docs/toolbox-mapping.md` whenever a Toolbox manager's status changes.
- Never commit anything under `data/` — original FRUA assets are copyrighted.
