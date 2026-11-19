# retroplat

The platform seam for vintage-targeting C programs, and one backend per
machine. Extracted from RetroWP, a portable word processor for vintage
machines, so that more than one
program can target an Atari ST, an Amiga, a Mac Classic and DOS without
writing the same five backends again.

    include/platform.h   the seam: memory, file I/O, clipboard, timer, font
                         metrics, text and rect drawing, images, windows,
                         dialogs, a byte transport, native<->UTF-8 conversion
    include/types.h      fixed-width types, status enum
    include/endian.h     explicit big-endian read/write helpers
    include/utf8.h       encode/decode
    src/                 the portable half: endian.c, utf8.c, fontpick.c, monobits.c,
                         polyfill.c (polygons by spans, wide lines as polygons)
    backends/host/       stub + POSIX, for building and testing off-target
    backends/atari/      GEM / VDI / TOS -- and, through gem4xe, the Atari
                         8-bit: the *_gem4xe.c files (see below)
    backends/amiga/      AmigaOS
    backends/mac/        Mac Classic Toolbox
    backends/dos/        DOS text mode (Open Watcom)
    backends/qt/         Qt5 + QPainter (Linux, Windows, macOS)
    backends/win32/      Win32 GDI

~6,600 lines. C89, big-endian-canonical on disk and wire, no dynamic
dependencies beyond an allocator wrapper the backend itself provides.

## What belongs here, and what does not

The line is: **"how do I draw a rectangle on this machine" belongs here;
"what rectangles does my program want" does not.**

RetroWP's `platform/atari/app_shell.c` is 4,678 lines of word processor
and stayed behind, along with its action registry, keymap and help
viewer. This directory has no application in it at all, which is what
makes it shareable.

## Checking it

    make check          seam + host backend, native
    make check-atari    m68k-atari-mint-gcc
    make check-gem4xe   Calypsi cc65816 + gem4xe's kit (Atari 8-bit)
    make check-amiga    m68k-amigaos-gcc
    make check-mac      Retro68's m68k-apple-macos-gcc
    make check-dos      Open Watcom wcc
    make check-qt       Qt5 via pkg-config
    make check-iigs     ORCA/C via Golden Gate (portable half, so far)
    make check-all      every toolchain that is installed

There is nothing to link — a platform layer has no `main()`. The only
thing worth asserting standalone is that it **compiles against nothing
but its own headers**, which is precisely the property that makes it
shareable, and precisely the one a consuming application's build cannot
test: inside that build, the application's headers are already on the
include path.

This is not hypothetical. Four `metrics_*.c` files included RetroWP's
`docmodel.h` to read four text-style bits, invisibly, for as long as the
code existed. Those bits are now `PLAT_STYLE_*` in `platform.h`, next to
the `plat_font_open()` argument that consumes them, and `make check-all`
is what keeps the next one from lasting as long.

An absent cross-toolchain skips rather than fails.

`backends/win32/` has no gate yet.

## The byte transport and TCP

`plat_transport_open` takes an endpoint by prefix. `serial:` works on the
ST, the Amiga, the Mac and DOS. `tcp:<host>:<port>` is a TCP client:

- **host**: POSIX sockets, always.
- **Amiga**: `bsdsocket.library` (AmiTCP, Miami, Roadshow, an emulator's
  bsdsocket emulation), always compiled in -- the calls go through the
  library, so it costs about 2 KB. The library is opened with the
  connection; without a TCP stack, the open answers `WP_UNSUPPORTED`.
- **Atari**: MiNTnet, and **opt-in: build with `-DRP_ATARI_TCP`**.
  mintlib's resolver, socket and select code costs a GEM program about
  68 KB even if it never connects, which a 1 MB plain-TOS machine cannot
  spare. With the define, the MiNT cookie gates the socket calls at run
  time. **STinG** (plain TOS or EmuTOS) is always compiled in, since it
  costs about 1 KB and nothing from mintlib (`-DRP_ATARI_NO_STING` drops
  it). `tcp:` tries MiNTnet first and STinG second, each gated on its
  cookie; with neither, it answers `WP_UNSUPPORTED`. A STinG port has to be
  switched on already (STNGPORT.CPX or STING.INF); the backend does not
  choose one. As with MiNTnet, a dotted address skips the resolver, and a
  name goes to STinG's resolve(), which has no timeout of its own.

A dotted address skips the resolver; a real name is looked up with
`gethostbyname`, which has no timeout and can block when the DNS server is
unreachable. Everything else waits at most the caller's timeout.

## The Atari 8-bit, through gem4xe

[gem4xe](https://github.com/slaapliedje/gem4xe) is GEM for a 65816-accelerated
Atari XL/XE. The ST backend's VDI-facing files -- `dialog_atari.c`,
`draw_atari.c`, `metrics_atari.c` -- compile **unchanged** against its GEM.
The rest of the ST backend talks GEMDOS/BIOS/XBIOS directly, so those five
pieces have siblings: `file_`, `mem_`, `clipboard_`, `transport_` and
`timer_gem4xe.c`. A consumer building for the 8-bit compiles the three shared
files plus those five; one building for the ST leaves the five out.

Each `*_gem4xe.c` is wrapped in `#ifdef GEM4XE_APP_GEM_H`, which only gem4xe's
`gem.h` defines. That is what lets `check-atari` take `backends/atari/*.c`
whole -- and it is also why `check-atari` passing **says nothing** about the
gem4xe files: under m68k gcc each one compiles to an empty object.
`check-gem4xe` is their gate. It compiles the 8-bit set with Calypsi against
gem4xe's kit, then requires every `*_gem4xe` object to define a function, so a
kit that stopped defining the guard macro cannot empty them silently.

Point it elsewhere with `CALYPSI=` (the toolchain root) and `GEM4XE_SDK=` (an
unpacked `gem4xe-sdk`, or `build/gem4xe-sdk` in a gem4xe tree after
`make sdk`).

## Consuming it

Point the application's build at `include/` and one `backends/<target>/`,
and compile the backend's `.c` files alongside your own. RetroWP does it
with a `RETROPLAT ?= ../retroplat` knob:

    make RETROPLAT=/path/to/retroplat

Header *filenames* are the same ones RetroWP used before the move
(`types.h`, not `retroplat_types.h`), so an existing consumer's
`#include "types.h"` keeps resolving — to this copy, once the old file is
gone and this directory is on the path.

Two things a consumer should know:

- **`plat_gc` is opaque, and a windowed application needs more.** Each
  backend ships a `metrics_<target>_internal.h` exposing its own graphics
  context (on Atari, `atari_gc { VdiHdl vdi; short window_handle; }`)
  because the application owns its window. That is a documented
  per-backend escape hatch, not a leak.
- **`types.h` may collide.** If your project already defines its own
  `u8`/`u16`/`u32`, a strict C89 compiler is entitled to reject the
  duplicate typedefs when one file includes both. Have yours include this
  one rather than restate it.

## Not here yet

**Apple IIGS** (65816 / GS/OS System 6.x). No backend is written, but the
toolchain is chosen and gated:

- **ORCA/C 2.2** through Golden Gate's `iix`. ORCA/C ships the GS/OS toolbox
  headers and writes OMF, the format GS/OS loads. `int` is 16 bits and pointers
  are 32, as on DOS. `make check-iigs` compiles the portable half under ORCA's
  full lint today, and picks up `backends/iigs/*.c` from the first file. Set
  `IIX=` to your `iix`.
- ORCA/C takes no `-I`, and a quoted `#include` searches the **current
  directory**, then ORCA's own `Libraries:ORCACDefs:`, which has its own
  `types.h`, `memory.h`, `font.h`, `event.h` and `window.h`. Run from anywhere
  else, `types.h` here is not the one found. `tools/iigs/orca_check.sh` stages
  flat and compiles from inside the stage for that reason; a consumer's build
  will need the same care.
- The 65816 is the first **little-endian** target since DOS, so file and wire
  I/O need `backends/dos/`'s byte-swap treatment.
- The Calypsi route (`tools/raw2omf.py`) is kept but was not chosen: Calypsi
  has no IIGS runtime or toolbox headers, and needs that converter for OMF.

**Commodore 900** (Zilog Z8001 / Coherent). Planned, with no toolchain
confirmed. There is no Z8000 cross-compiler in maintained form; the route is
Coherent's own `cc`, inside Michal Pleban's headless
[commodore-900-emulator](https://github.com/MichalPleban/commodore-900-emulator),
whose bundled Coherent disk carries Mark Williams' tools. Whether that `cc`
accepts C89 -- prototypes, `const` -- or only K&R C is the open question,
and the first thing to answer. Its backend would be this library's first
terminal (tty) backend rather than a GUI one. The CPU is big-endian.

## Naming

The symbols still read `wp_status` / `WP_OK`, from RetroWP. Renaming them
to `plat_`/`PLAT_` is mechanical and touches every file in every
consumer, so it is deliberately deferred rather than mixed into the move
— new additions to the seam use `PLAT_` from the start, as
`PLAT_STYLE_*` already does.

## Licence

**LGPL-2.1-or-later.** `COPYING.LIB` carries the text, and every source file
carries `SPDX-License-Identifier: LGPL-2.1-or-later`.

The library half of the choice is the point: link retroplat from anything you
like, including a program under terms of your own — what the licence asks is
that changes **to retroplat itself** stay available, and that a program you hand
someone leaves them able to relink it against a modified copy (section 6).

It is the same licence gem4xe's application kit and cflib use, so a program built
from all three — as the Atari 8-bit build of GACS is — answers to one set of
library terms rather than three.
