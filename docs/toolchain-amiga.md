# Amiga AGA toolchain (Bebbo m68k-amigaos GCC)

The Amiga AGA target (ADR-0012) builds with **Bebbo's `m68k-amigaos-gcc`** — a
modern GCC that emits AmigaOS hunk executables and bundles the NDK. It is **not
a stock package**; you build it from source once, then point
`toolchain/m68k-amigaos.mk` at it.

## Build it (on a networked host, not a filtered sandbox)

Bebbo's build clones ~15 component repos from GitHub and pulls the NDK from
aminet, so run it in a shell with normal internet (a CI sandbox behind a
GitHub-blocking proxy will fail at the first `git clone`).

Host prerequisites (Arch/Garuda shown; Debian/Ubuntu names in parentheses):

```sh
sudo pacman -S --needed make gcc bison flex gperf texinfo autoconf gettext \
                        python3 wget rsync lha
# Debian: build-essential bison flex gperf texinfo autoconf gettext-base \
#         python3 wget rsync lhasa
```

Clone and build (installs into `~/opt/amiga` — the default `TOOLROOT` this repo
expects; Bebbo's own default is `/opt/amiga`, which needs `sudo`):

```sh
git clone https://github.com/bebbo/amiga-gcc ~/src/amiga-gcc
cd ~/src/amiga-gcc
make all PREFIX=$HOME/opt/amiga -j"$(nproc)"      # ~30-60 min, several GB
```

`make all` builds the full kit (vbcc/vasm/vlink/SDL/ixemul too). For just what
this port needs you can build the smaller set: `make gcc` builds binutils + gcc,
then `make libnix newlib ndk` gives the C runtime and headers.

The repo is mirrored at `https://codeberg.org/bebbo/amiga-gcc` if GitHub is
unreachable, but the **component** clones still resolve to GitHub — Codeberg
alone does not unblock the build.

Verify:

```sh
~/opt/amiga/bin/m68k-amigaos-gcc --version
```

## Build the port for Amiga

```sh
make MACHINE=amiga                 # -> frua (an AmigaOS hunk executable)
```

Override `TOOLROOT` if the toolchain lives elsewhere:

```sh
make MACHINE=amiga TOOLROOT=/opt/amiga
```

Confirm 020 codegen took (same check as the Falcon build):

```sh
m68k-amigaos-objdump -d src/engine/boot.o | grep -cE 'muls\.l|bfextu|bfins'
```

## Run it (amiberry)

The run harness is **amiberry** (installed here as the
`com.blitterstudio.amiberry` flatpak). You need a Kickstart 3.1/3.2 ROM and an
AGA machine config (A1200). Point amiberry at a hard-drive directory containing
`frua` plus the game data, and boot. A dedicated run skill/driver
(`.claude/skills/run-amiga-port/`) is TODO — see the Falcon
`run-falcon-port` skill for the pattern.

## RTG (Picasso96) on a classic Amiga — verified recipe (2026-07-15)

The full 256-colour game renders on a live Picasso96 320×200×8 RTG screen —
a classic non-AGA Amiga with an accelerator and a graphics card. `dsp_detect`
routes non-AA machines to `platform/amiga/display_rtg.c` (BestModeID +
OpenScreenTags 8-bit + WriteChunkyPixels + LoadRGB32). What it took to stand
up the P96 runtime under amiberry, in order (each step fixed one failure):

1. **`gfxcard_type=uaegfx`** (not `A2410` — that's the Commodore TIGA board).
   amiberry's "UAE RTG" board provides the card interface itself.
2. **68020+** (`cpu_type=68020`, `cpu_24bit_addressing=false`): modern P96
   refuses 68000. Authentic anyway — RTG cards lived in accelerated Amigas.
3. **A real 3.2 ROM** (exec 47 — e.g. `CDTVA500A600A2000.47.115.rom`). The
   Workbench 3.2 install's Startup-Sequence needs exec 47; a 3.1.4 ROM (exec
   46) trips its `LoadModule ROMUPDATE` path and drops to a `1>` CLI.
4. **A real Workbench 3.2 on the boot drive** (not a bare mount): P96's
   `rtg.library` needs `ENV:`, the assigns, `icon.library`, `iffparse.library`
   — all of which a full WB provides. Copy the WB dir into the mount, keep the
   game data, install P96 into it (LIBS:Picasso96API.library, LIBS:Picasso96/
   {rtg.library, uaegfx.card, emulation.library}, DEVS:Picasso96Settings.*),
   and launch `frua` from `S/User-Startup`.
5. **`fastmem_size=4`** — the engine's ~4 MB working set needs fast RAM (1 MB
   chip alone → "not enough memory available"). ★`fastmem_size=8` SIGSEGVs
   amiberry on this config; 4 MB works and fits the game.

★The P96 **monitor is not needed** on amiberry — the UAE RTG board
self-registers modes once `rtg.library` is loadable, so the monitor's "could
not create graphics board context" is harmless. ★Use the older P96 (with
`uaegfx.card`); the 2023 P96 3.x dropped it (targets real cards only). Build
`frua` for this machine with the CPU it emulates (`make MACHINE=amiga` for
68020; `CPU68K=68000` links too but P96 needs 020).

## Building it WITHOUT GitHub (what actually worked here, 2026-07-14)

This sandbox's egress blocks GitHub's repo/API/codeload endpoints but allows
everything else. Bebbo's build turned out to need **no GitHub** for the compiler
+ libc + NDK: its core repos live on **franke.ms** (Bebbo's own git) and the NDK
on aminet, with gmp/mpfr/mpc from ftp.gnu.org. Only the peripheral tools (vbcc,
vasm, vlink, SDL, and `fd2pragma`) are GitHub-only, and a minimal build skips
them (`sfdc` on franke generates the GCC inline headers, so `fd2pragma` isn't
needed).

- `make gcc PREFIX=~/opt/amiga -j8` — binutils + GCC 6.5.0b, franke.ms + gnu.org
  only. **Done and verified** (compiles a test object; `m68k-amigaos-gcc
  --version` works).
- The NDK: dropped in from a local **NDK3.2R4** (Hyperion) or downloaded from
  aminet; `sfdc` (franke) generates the GCC `proto/`/`inline/` headers into
  `~/opt/amiga/m68k-amigaos/ndk-include`, which is **auto-added** to the
  compiler's include path. `<exec/types.h>` etc. then resolve with no `-I`.

Snags hit and fixes:
- **fd2sfd patch conflict** — franke's source is already patched, so Bebbo's
  `patch -N` errors. Fix: `rm patches/fd2sfd/fd2inline.c.diff` (and the `.rej`);
  the pre-patched configure script is fine, so the rule won't re-run.
- **newlib build failed on `malloc.cpp`** — FIXED (2026-07-14). Root cause: the
  parent make augments `CC` with the multilib `-I…/targ-include -I…/libc/include`,
  but the amigaos Makefile's `.cpp.o` suffix rule uses plain `$(CXX)`, which
  isn't augmented — so `malloc.cpp` (the one C++ file in newlib) compiled with no
  libc include path and couldn't find `<string.h>`/`<stabs.h>`. Fix: append both
  `-I` to the `INCLUDES` variable (consumed by BOTH `COMPILE` and `CXXCOMPILE`)
  in `newlib/libc/sys/amigaos/Makefile.{am,in}`:
  `-I$(abs_builddir)/../../../targ-include -I$(newlib_basedir)/libc/include`.
  Then `make newlib PREFIX=~/opt/amiga` completes and installs
  `m68k-amigaos/lib/libc.a` (+ the newlib headers into the sysroot).
  ⚠️ The multilib build spawns many jobs — do NOT launch a second `make newlib`
  over a running one; concurrent `ar`/`ranlib` on the same `lib.a` races and
  fails with `ranlib: unable to copy file 'lib.a'`. Let one finish.

## Status (2026-07-14 late) — ★★ THE PORT LINKS

`make MACHINE=amiga` produces **`frua` — an AmigaOS loadseg()able hunk
executable** (~660 KB) containing the complete engine + Toolbox shim + the
platform/amiga backends. 68020 codegen verified (561 `mulsl`/`bfextu`/`bfins`
in the linked binary).

- ✅ **Toolchain**: `m68k-amigaos-gcc 6.5.0b` + binutils + NDK, all non-GitHub.
- ✅ **libc**: newlib `libc.a` (after the `malloc.cpp` INCLUDES fix above).
- ✅ **Runtime libs**: `make libgcc libnix PREFIX=~/opt/amiga` — the earlier
  `make gcc` builds the compiler but NOT libgcc; the first link fails with
  `cannot find -lgcc` until this runs. libnix provides crt0 + the -noixemul
  C runtime.
- ✅ **plat_sys HAL**: the four GEMDOS-coupled files route through
  `platform/include/plat_sys.h` (Falcon `sys_falcon.c` / Amiga `sys_amiga.c`).
  - NB: compile with the **mk's `CFLAGS`** — `-noixemul` is a *link* flag only;
    passing it to a compile flips newlib's reent header path and breaks
    `<stdio.h>` (`unknown type name '__FILE'`).
- ✅ **Point clash solved**: `platform/include/amiga_ndk.h` — a dos.library-only
  NDK wrapper that renames the NDK's graphics `Point` for shim-side TUs and
  deliberately omits `proto/exec.h` (its `FreeMem(ptr,size)` declaration
  collides irreconcilably with the Mac Memory Manager's `FreeMem(void)`).
- ✅ **Link-stage cleanups**: c2p.S's TOS vblqueue trampolines are now
  `#ifndef FRUA_AMIGA` (they name Falcon handlers); `c2p_amiga.c` is a
  portable-C 8-plane chunky→planar for the AGA scatter; `vdi_stub.c` reports
  no-GDOS so the printing shim fails exactly as the Mac with no printer;
  sound_paula implements `plat_sound_set_vbl_hook` (now in plat_sound.h);
  files_amiga.c has the real `mac_path_to_c` ('/' separators, and the
  engine's literal '\' separators translate too).

Gotchas:
- **Machine switch needs `make clean`** — both machines share object paths;
  `make` right after `make MACHINE=amiga` links Amiga objects with the MiNT
  linker (and vice versa) and fails confusingly.
- **`m68k-amigaos-objdump` decodes as 68000 by default** — 020 instructions
  print as `.short` garbage and the codegen grep counts ZERO. Pass
  `-m m68k:68020`.
- GCC 6.5 rejects `static x = <const object>;` initializers newer GCCs fold
  (quickdraw.c's cursor uses a macro initializer now).

## ★★ IT BOOTS (2026-07-15): the main menu renders on an emulated A1200

`frua` + the staged gamedata in a directory harddrive (S/startup-sequence =
`stack 65536` + `cd DH0:` + `frua`), Kickstart 3.2 (kicka1200.rom from the
licensed AmigaOS 3.2 lha), amiberry A1200/AGA config with 2MB chip + 8MB fast:
the port boots to the UNLIMITED ADVENTURES main menu — correct chrome, text,
palette, cursor — through the direct copper list, the AGA bank palette, the C
c2p, the dos.library file shim and the VERTB tick server. `PROGDIR:DBG.LOG`
carries the boot breadcrumbs (the same trail as the Falcon's C:\DBG.LOG).

Launch (headless-ish; the flatpak grabs the desktop display):
```sh
flatpak run --env=SDL_VIDEODRIVER=x11 com.blitterstudio.amiberry \
    --log --config ~/Amiberry/Configurations/openua.uae -G
```

### ★★ The Bebbo GCC 6.5 SHIFT MISCOMPILER (worked around in the mk)
The first boots died with "Insufficient FAR Memory!": jt463's
`maxbytes = (long)maxkb * 1024L` (maxkb=450) compiled to **WORD shifts**
(`lslw #8; lslw #2`) — 0x70800 truncated to 0x0800 = a 2KB pool. The culprit
is Bebbo's own `h` ("optimize shift instructions") pass in the default
`-fbbb=+`: when it can sink such a shift past intervening calls it narrows it
to HImode and silently drops the carry-out. Any nearby instrumentation
changed the expression flow and masked it (a textbook heisenbug — chased
through three false theories: caller-frame smash, stack overflow, memory
race). Reproducer: two `(long)short * 1024L` locals with calls between
compute and use, -O2. Workaround: `-fbbb=abcefilmnprsz0` (default minus `h`)
in toolchain/m68k-amigaos.mk. If the toolchain is ever rebuilt/upgraded,
re-run the reproducer before dropping the flag.

### Input + speed (2026-07-15, 1b9578c + f81c15b)
The mouse pointer is hardware sprites 0+1 (attached), repositioned in the
VERTB server — video-rate tracking, no c2p cost. The keyboard is an
**input.device handler** at priority 100, NOT the ciaa.resource ICR vector
first planned: keyboard.device owns the CIA-A SP interrupt bit on any booted
system (and this port keeps the OS alive for dos.library), so AddICRVector
can never claim it. The handler consumes rawkey + rawmouse, which also stops
input reaching the Workbench behind the game. Rawkeys map to Atari IKBD
scancodes; the polls apply TOS keytable semantics, so the Event Manager shim
is machine-blind.

The naive per-pixel c2p cost ~1 second per 320x200 frame at 14MHz — every
modal-loop pass ends in a full present (jt1134), so menus ran at seconds per
pass and input LOOKED broken while the input layer was perfect. The c2p is
now a masked-swap bit-matrix transpose (32 pixels/step, 68000-clean C,
bit-identical to the naive scatter — tests/test_c2p_amiga.py). Build with
`-DFRUA_KBTRACE` to get the modal-pass-rate counters that diagnosed this
(polls / l2d3e / jt1134 / qd_present per 16 polls, logged to DBG.LOG).

Emulator-harness gotchas: an instantaneous `xdotool click` is INVISIBLE to
the game (the button is sampled from CIA PRA at 50Hz) — use
`mousedown; sleep 0.3; mouseup`. Send ONE key per xdotool invocation.
amiberry captures the mouse on the first in-window click; after that only
RELATIVE host moves reach JOY0DAT.

### Paula audio (2026-07-15, 355d33e)
The Falcon sound architecture one-for-one: a single looping DMA ring
(Paula reloads AUDxLC/LEN natively) that the VERTB mixer renders into —
four-tone synth voices, the swMode tone, and the current effect on top.
Paula's audio pointers are write-only, so the play cursor is MODELED:
period 156 consumes exactly 454 samples per PAL frame (156 x 454 = one
frame in colour clocks), zero drift. The LED filter is switched off (a
3.2kHz low-pass would muffle 11-22kHz content). Verified via the
machine-neutral `EXTRA_CFLAGS=-DFRUA_SNDTEST` boot harness with host
audio capture (`parec -d <sink>.monitor`): four sfx bursts + song 0
sustained, spectral peak at middle C. NB: amiberry's config needs
`sound_output=exact` — the bring-up config had `none`, which makes any
capture silently pass-fail at the emulator, not the port.

Next: the `run-amiga-port` driver skill (amiberry harness, patterned on
run-falcon-port), then a PLAY THE GAME deep-run for Falcon parity.

## ✅ AGA verified (2026-07-18) — first boot of the AGA path

`make MACHINE=amiga` + the `openua.uae` config (A1200, 68EC020, AGA,
2 MB chip + 8 MB fast, `kicka1200.rom`, DH0: = `data/work/amiga-mount`)
boots to the fully rendered interactive main menu: granite skin, cyan
text, 256-entry CLUT, colour cursor, VBL sound task — `DBG.LOG` shows
the `amiga-aga` backend (the 2026-07-15 runs exercised `amiga-ecs` and
RTG). Launch: `flatpak run com.blitterstudio.amiberry -f
~/Amiberry/Configurations/openua.uae -G` (`-G` skips the launcher GUI).

Harness gotchas (for the future run-amiga skill):
- **`pkill -f amiberry` kills your own shell** — same trap as the
  Hatari one; use `pkill -9 -x amiberry`.
- The flatpak ignores `--env=DISPLAY=:99`: it renders on the session
  display (grant `flatpak override --user --socket=x11` first on a
  Wayland session). Screenshot with `import -window <id>` on `:0`.
- The engine is launched by the mount's `S/User-Startup` (`frua`).
