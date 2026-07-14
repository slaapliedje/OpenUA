# Architecture

## Goal

**OpenUA** — an open reimplementation of SSI's *Unlimited Adventures* engine
(1993), decompiled from its Macintosh 68k release and retargeted to Motorola 68k
retro machines: the Atari Falcon030 / TT030 today, and the Amiga AGA next
(ADR-0012). One engine, several machines, behind a hardware-abstraction layer.

## The layer cake

```
  +-----------------------------------------------------------+
  |  FRUA engine + editor   (decompiled, hybrid C + 68k asm)   |  src/engine/
  |  combat, party, modules, rendering logic, design tools     |
  +-----------------------------------------------------------+
  |  Mac Toolbox compatibility shim                            |  compat/
  |  QuickDraw, Resource/Memory/Sound/File/Event Managers      |
  +-----------------------------------------------------------+
  |  Platform HAL    display | input | audio                  |  platform/
  |  backends: VIDEL (Falcon) | TT-shifter (TT) | VDI (later)  |
  +-----------------------------------------------------------+
  |  Atari TOS — GEMDOS / BIOS / XBIOS / line-A / AES          |
  +-----------------------------------------------------------+
```

The engine calls the Toolbox shim as if it were still on a Mac. The shim is
implemented on top of the platform HAL, which is the only layer that knows
whether it is talking to a Falcon or a TT. See `docs/decisions.md` for why.

## Repository layout

| Path                  | Contents                                              |
|-----------------------|-------------------------------------------------------|
| `src/`                | Port bootstrap (`main.c`) and engine sources.         |
| `src/engine/`         | Decompiled FRUA engine + editor, grouped by subsystem.|
| `compat/`             | Mac Toolbox compatibility shim.                       |
| `compat/include/`     | Toolbox-facing headers the engine includes.           |
| `platform/`           | Atari hardware abstraction layer.                     |
| `platform/include/`   | HAL interfaces (`display.h`, input, audio).           |
| `toolchain/`          | Cross-toolchain configuration.                        |
| `tools/`              | Host-side extractors / converters / decomp helpers.   |
| `docs/`               | Architecture, decisions, Toolbox mapping.             |
| `data/`               | Original FRUA assets — git-ignored, never committed.  |

## Porting workflow

1. **Disassemble** the Mac 68k binary; identify subsystems and entry points.
2. **Lift** each subsystem to C under `src/engine/`; keep stubborn routines as
   `.S` 68k assembly alongside their C siblings (hybrid — ADR-0002).
3. **Shim** the Mac Toolbox calls each subsystem makes (ADR-0003). Track
   coverage in `docs/toolbox-mapping.md`.
4. **Back** the shim with the platform HAL; bring up the VIDEL display backend
   first.
5. **Verify** against the original behaviour, then migrate hot paths from the
   shim to native Atari APIs.

## Hardware notes

- **CPU:** both targets are 68030. m68k-atari-mint has no soft-float 020-60
  library, so the default build is plain 68000 — soft-float, and 68000 code
  runs unchanged on the 68030. It runs on the FPU-less Falcon030 and the
  TT030. `make FPU=1` builds a hard-float `68020-60` variant for an
  FPU-equipped machine (the TT030, or an FPU'd Falcon).
- **Video:** Falcon030 = VIDEL (programmable, RGB/VGA modes, 256-colour and
  true-colour). TT030 = TT-shifter (TT-medium/TT-low, 256 colours from a
  4096-colour palette). Different enough to demand the HAL split (ADR-0005).
- **Audio:** Falcon030 = 8-bit stereo DMA sound + DSP56001. TT030 = YM2149 PSG
  only. The audio HAL must degrade gracefully on the TT.
