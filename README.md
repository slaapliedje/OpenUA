# OpenUA

An open reimplementation of SSI's *Unlimited Adventures* engine — the 1993 Gold
Box adventure **construction set** — for Motorola 68k retro machines. One engine,
four machines: the Atari **Falcon030** and **TT030**, and the **Amiga AGA**
(A1200/A4000), plus an **RTG** path so an ECS Amiga with a graphics card runs the
full 256-colour game.

*OpenUA* = **Open** + **U**nlimited **A**dventures.

The engine is decompiled from the **Macintosh 68k** release of *Forgotten
Realms: Unlimited Adventures*. Because the Mac version is already 68k machine
code and the target machines are 68020/030, the CPU code carries over directly —
the work is retargeting the Mac Toolbox and rebuilding the display, sound, and
input paths behind a hardware-abstraction layer, so one engine serves several
machines.

> OpenUA is an unofficial, fan-made project, not affiliated with or endorsed by
> the rightsholders of *Forgotten Realms*, *Unlimited Adventures*, or the Gold
> Box games. It contains only original port code — no game data (see Legal).

> **No game data is included.** FRUA and its modules are copyrighted. You supply
> your own original Mac (or DOS — see below) FRUA data. See
> [`data/README.md`](data/README.md).

## Status — playable beta

The runtime plays a real adventure end to end on **all four** machine targets.
Everything below is **emulator-validated only** — Hatari for the Atari builds,
amiberry for the Amiga — and has **never been run on real hardware**. Treat it
accordingly.

| Target | Backend | Status |
|---|---|---|
| **Atari Falcon030** | VIDEL 16bpp | Playable beta — the original target; full play-through verified in Hatari. |
| **Atari TT030** | TT-low 8bpl, 320×200 line-doubled into a 320×400 letterbox | Verified in Hatari + EmuTOS: menu → load → caravan event → 3D town walk; STE-DMA sound (music + SFX). |
| **Amiga AGA** (A1200/A4000) | Direct copper list, AGA bank palette, hardware-sprite pointer | Playable — verified in amiberry through **combat**: save-load, the caravan event, the town walk, the animated fireplace, and a full fight. Keyboard, mouse, and Paula audio all live. |
| **Amiga RTG** (ECS + graphics card) | Picasso96/CyberGraphX chunky screen | Backend written; the AGA-vs-RTG auto-detect is verified, the Picasso96 runtime is not yet stood up. |

The same binary serves each family (one `frua.prg` for Falcon **and** TT; one
`frua` for AGA **and** RTG) — the machine is detected at runtime and the matching
display/sound path is chosen. On the paletted targets (TT, AGA, RTG) the palette
lives in hardware, so colour-cycle animation like the tavern fireplace is free.

What works (on every target unless noted):

- **Exploration** — first-person 3D dungeon view and the top-down area automap;
  movement by arrow keys or mouse; all eight command-bar verbs
  (MOVE · AREA · CAST · VIEW · ENCAMP · SEARCH · LOOK · INV).
- **Events** — text, combat, treasure, stairs, transfers, shops, temples,
  encounters, chains, and the approach-direction and quest-flag gating that
  designs use.
- **Combat** — turn-based, playable through to a party wipe, driven by the
  arrow keys.
- **Towns & services** — shops (buy / sell / identify, pooled and personal
  funds), temples (healing services), taverns.
- **Magic** — the full loop: memorize from the grimoire, rest to commit, cast;
  spell effects apply and are consumed.
- **Characters** — character generation for every class, the Training Hall
  roster (add / remove / modify / view / train), equipping weapons and armour
  (AC and damage update and persist), and save / load of the party and game.
- **Editors** — the in-engine GEO map editor, event editor, record/game-settings
  editor, art gallery, and monster editor, plus GDOS printing from the editor.
- **Sound** — digitized SFX and the Mac four-tone-synth music, through the
  Falcon CODEC, the TT's STE-DMA sound, or Amiga Paula.

It plays the bundled sample design **HEIRS TO SKULL CRAG** and real commercial
modules (e.g. *Pool of Radiance*) on their own art. The current gaps are
fidelity/polish, not missing features — see
[`docs/enhancements.md`](docs/enhancements.md).

## Getting the game data

**OpenUA ships no game data** — you supply your own from a legally-obtained copy
of FRUA. The easiest source today is a digital re-release, both of which bundle
FRUA in **Forgotten Realms: The Archives – Collection Two**:

- **GOG** — [Forgotten Realms: The Archives – Collection Two](https://www.gog.com/game/forgotten_realms_the_archives_collection_two)
- **Steam** — [Forgotten Realms: The Archives – Collection Two](https://store.steampowered.com/app/1882280/Forgotten_Realms_The_Archives__Collection_Two/)

Those are the **DOS** release. The engine here is Mac-derived, but design/data
files are byte-identical between the DOS and Mac versions, and the included art
converter (`tools/art_convert.py`, below) makes the DOS art readable — so
Collection Two's FRUA works. If you have the original **Macintosh** release
instead, see [`docs/mac-release.md`](docs/mac-release.md) for unpacking it.

## Building

`MACHINE` selects the target family (default `falcon`, which also covers the
TT). Each family needs its own cross toolchain.

### Atari (Falcon030 / TT030)

Requires the **m68k-atari-mint GCC** cross toolchain with a soft-float
`m68020-60` multilib on `PATH` (override the prefix with `make CROSS=…` or
`TOOLROOT=…`). Building that toolchain is documented in
[`docs/toolchain-softfloat-020.md`](docs/toolchain-softfloat-020.md).

```sh
make                 # build frua.prg — soft-float; runs on Falcon030 AND TT030
make FPU=1           # FPU-required variant tuned for the TT030 (68881 hard-float)
make run             # boot the build in Hatari (Falcon mode)
make test            # host-side test suite (pytest over tools/)
make release         # packaged, flag-guarded shipping build -> dist/
make clean
```

The default build is soft-float so **one binary serves the FPU-less Falcon030
and the 68882-equipped TT030**; the display and sound path (VIDEL vs. TT
shifter) is chosen at runtime from the `_VDO` cookie. The toolchain flags are
non-negotiable (`-m68020-60 -msoft-float`); see [`CLAUDE.md`](CLAUDE.md).

### Amiga (AGA / RTG)

Requires the **Bebbo `m68k-amigaos` GCC** toolchain (build it once per
[`docs/toolchain-amiga.md`](docs/toolchain-amiga.md); default prefix
`~/opt/amiga`).

```sh
make MACHINE=amiga               # build frua (an AmigaOS hunk executable)
make MACHINE=amiga CPU68K=68000  # 68000-clean build (for the eventual ECS target)
```

One `frua` serves AGA and RTG: an AA-chipset machine gets the direct-copper AGA
backend; a non-AA machine gets the RTG (Picasso96/CyberGraphX) backend. The
engine is deliberately 68000-clean by construction (the original Mac binary uses
zero 68020-only instructions), so the CPU tier is purely a compiler flag —
groundwork for later ECS/ST machines.

### Running a game module

You provide your own FRUA data. Unpack the Mac release under `data/` (see
[`docs/mac-release.md`](docs/mac-release.md)), then stage a design and boot:

```sh
make run-game DSN=HEIRS.DSN     # stage the shared libraries + a design, boot Hatari
```

`make gamedata DSN=<name>.DSN` flattens the shared engine libraries and the
chosen `.DSN` into `data/work/gamedata/` (the GEMDOS `C:` mount) without
disturbing any characters you created.

## Using DOS FRUA data and fan modules

FRUA's whole point is that players build their own adventures, and a large
**community** has — hundreds of fan-made modules, none of them official SSI
releases. They are archived at **[frua.rosedragon.org](http://frua.rosedragon.org)**
(the long-running community site). Most are authored on the PC, and their art is
doubly unreadable by this Mac-derived engine — wrong byte order *and* a different
pixel layout. `tools/art_convert.py` converts the art container both ways
(DOS **`HLIB`** ↔ Mac **`GLIB`**), so those community modules become playable:

```sh
python3 tools/art_convert.py <module-art-files>   # HLIB <-> GLIB
```

The transform was **derived by diffing the same module shipped in both
formats** (one asset set authored once, in both a PC and a Mac release), so it
reproduces the real Mac bytes exactly — `tests/test_art_convert.py` checks that
against the matched pair. Design/data files (`.DAT`/`.GEO`/…) are byte-identical
between releases and need no conversion; only the art containers do. See
[`docs/fan-module-hacks.md`](docs/fan-module-hacks.md) and
[`docs/dos-inventory.md`](docs/dos-inventory.md).

`tools/` also holds the resource-fork extractors (`macrsrc.py`, `hfs_extract.py`,
`appledouble.py`), the `HLIB`/`GLIB` extractors (`hlib_extract.py`,
`wall_extract.py`), `rsrcpack.py` (Mac resource fork → the flat FRSC archive the
engine loads), and the disassembler `dis68k.py`.

## Architecture

The port is layered, and only the innermost platform layer knows which machine
it runs on:

```
src/engine/  ->  compat/  ->  platform/  ->  TOS / AmigaOS
 (lifted        (Mac         (hardware
  engine)        Toolbox      abstraction:
                 shim)        VIDEL / TT-shifter / AGA-copper / RTG)
```

- **`src/engine/`** — the decompiled engine, lifted function-by-function from the
  Mac CODE segments. Recompilable C, with 68k asm where lifting resists.
- **`compat/`** — the Mac Toolbox compatibility shim (Memory, QuickDraw, Window,
  Event, Resource, File, Dialog, Menu, Control, Sound, TextEdit managers). The
  engine keeps the Mac spellings; the shim routes them to GEMDOS/GEM/VDI.
- **`platform/`** — the display / input / sound / debug HAL. The engine renders
  into one 8-bit paletted chunky buffer; each machine backend puts it on screen
  (Falcon `platform/display_videl.c`, TT `platform/display_tt.c`, Amiga
  `platform/amiga/`). Chunky→planar conversion, where a machine needs it, is a
  shared masked-swap transpose; graphics-card targets take the buffer directly.

Design rationale lives in [`docs/architecture.md`](docs/architecture.md) and the
append-only decision log [`docs/decisions.md`](docs/decisions.md). `docs/` also
holds the per-subsystem maps ("`*-wall.md`") and the decompilation workflow
([`docs/decompilation.md`](docs/decompilation.md)).

## Repository layout

```
src/         port bootstrap (src/main.c) + decompiled engine (src/engine/)
compat/      Mac Toolbox compatibility shim
platform/    hardware abstraction layer (Atari VIDEL/TT here; platform/amiga/ = AGA + RTG)
toolchain/   per-machine cross-toolchain configuration (.mk)
tools/       host-side extractors / converters / disassembler
tests/       host-side pytest suite
docs/        architecture, decisions, subsystem maps, formats
data/        original FRUA assets — you supply these (git-ignored)
```

## License

The port's own source code, tooling, and documentation are licensed under the
**GNU General Public License v2** — see [`LICENSE`](LICENSE).

    Copyright (C) 2026 Slaapliedje

This covers only the port (the decompiled/reimplemented engine, the Toolbox
shim, the HAL, and the tools). It grants **no rights** to *Forgotten Realms:
Unlimited Adventures* itself, which remains copyrighted by its rightsholders and
is never included here (see Legal, below). The AI-generated free cursor set under
`assets/cursors/` is part of the port and covered by the same license.

## Legal

This repository contains **only port source code and tooling**. It ships no
original FRUA assets, binaries, resource forks, or module data — those are
copyrighted by their rightsholders and must be supplied separately from your own
legally-obtained copy. The build regenerates the copyrighted replay tables from
your resource fork locally; they are never committed. See
[`data/README.md`](data/README.md).

A **compiled** `frua.prg` built with real data embeds ~12 KB of copyrighted FRUA
`DATA`, so binaries are **not** redistributable — build your own from your own
data. This is a source-only project.
