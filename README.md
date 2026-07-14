# FRUA — Atari Falcon030 / TT030 port

A port of SSI's *Forgotten Realms: Unlimited Adventures* (FRUA, 1993) to the
Atari **Falcon030** and **TT030**, decompiled from the **Macintosh 68k** release.

Because the Mac version is already 68k machine code and the Falcon/TT are 68030
machines, the CPU code carries over directly. The work is retargeting the Mac
Toolbox to Atari TOS/GEM/XBIOS and rebuilding the display, sound, and input
paths behind a hardware-abstraction layer.

> **No game data is included.** FRUA and its modules are copyrighted. You supply
> your own original Mac (or DOS — see below) FRUA data. See
> [`data/README.md`](data/README.md).

## Status — playable beta (v0.1.0-beta)

The runtime plays a real adventure end to end. Verified in the **Hatari**
emulator on its own art; it has **never been run on real Falcon030/TT030
hardware** — treat it as emulator-validated only.

What works:

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
- **Sound** — digitized SFX and the Mac four-tone-synth music.

It plays the bundled sample design **HEIRS TO SKULL CRAG** and real commercial
modules (e.g. *Pool of Radiance*) on their own art. The current gaps are
fidelity/polish, not missing features — see
[`docs/enhancements.md`](docs/enhancements.md).

## Building

Requires the **m68k-atari-mint GCC** cross toolchain with a soft-float
`m68020-60` multilib on `PATH` (override the prefix with `make CROSS=…` or
`TOOLROOT=…`). Building that toolchain is documented in
[`docs/toolchain-softfloat-020.md`](docs/toolchain-softfloat-020.md).

```sh
make                 # build frua.prg (soft-float; runs on Falcon030 AND TT030)
make FPU=1           # FPU-required variant tuned for the TT030 (68881 hard-float)
make run             # boot the build in Hatari (Falcon mode)
make test            # host-side test suite (pytest over tools/)
make release         # packaged, flag-guarded shipping build -> dist/
make clean
```

The default build is soft-float so **one binary serves the FPU-less Falcon030
and the 68882-equipped TT030**. The toolchain flags are non-negotiable
(`-m68020-60 -msoft-float`); see [`CLAUDE.md`](CLAUDE.md).

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

FRUA has a large fan-module scene (hundreds of PC modules). Their art is authored
for one release and is doubly unreadable by this Mac-derived engine — wrong byte
order *and* a different pixel layout. `tools/art_convert.py` converts the art
container both ways (DOS **`HLIB`** ↔ Mac **`GLIB`**), so PC modules become
playable:

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

## Amiga AGA (in progress)

A second target — **Amiga AGA** (A1200/A4000) — is scaffolded but not yet
compiled. It reuses the whole engine and shim; only the toolchain and the
`platform/` backend differ. `make MACHINE=amiga` selects it once the Bebbo
`m68k-amigaos` toolchain is built. See
[`docs/toolchain-amiga.md`](docs/toolchain-amiga.md) and ADR-0012 in
[`docs/decisions.md`](docs/decisions.md).

## Architecture

The port is layered, and only the innermost platform layer knows which machine
it runs on:

```
src/engine/  ->  compat/  ->  platform/  ->  TOS / AmigaOS
 (lifted        (Mac         (hardware
  engine)        Toolbox      abstraction:
                 shim)        VIDEL / TT / AGA)
```

- **`src/engine/`** — the decompiled engine, lifted function-by-function from the
  Mac CODE segments. Recompilable C, with 68k asm where lifting resists.
- **`compat/`** — the Mac Toolbox compatibility shim (Memory, QuickDraw, Window,
  Event, Resource, File, Dialog, Menu, Control, Sound, TextEdit managers). The
  engine keeps the Mac spellings; the shim routes them to GEMDOS/GEM/VDI.
- **`platform/`** — the display / input / sound / debug HAL. The engine renders
  into one 8-bit paletted chunky buffer; each machine backend puts it on screen.

Design rationale lives in [`docs/architecture.md`](docs/architecture.md) and the
append-only decision log [`docs/decisions.md`](docs/decisions.md). `docs/` also
holds the per-subsystem maps ("`*-wall.md`") and the decompilation workflow
([`docs/decompilation.md`](docs/decompilation.md)).

## Repository layout

```
src/         port bootstrap (src/main.c) + decompiled engine (src/engine/)
compat/      Mac Toolbox compatibility shim
platform/    hardware abstraction layer (platform/amiga/ = AGA backend)
toolchain/   per-machine cross-toolchain configuration (.mk)
tools/       host-side extractors / converters / disassembler
tests/       host-side pytest suite
docs/        architecture, decisions, subsystem maps, formats
data/        original FRUA assets — you supply these (git-ignored)
```

## Legal

This repository contains **only port source code and tooling**. It ships no
original FRUA assets, binaries, resource forks, or module data — those are
copyrighted by their rightsholders and must be supplied separately from your own
legally-obtained copy. The build regenerates the copyrighted replay tables from
your resource fork locally; they are never committed. See
[`data/README.md`](data/README.md).
