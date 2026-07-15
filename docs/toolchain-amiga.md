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

Next, in order: the CIA keyboard (ciaa.resource ICR
rawkey ring — the menu is mouse-only until then), Paula audio, the hardware
sprite cursor — then the `run-amiga-port` driver skill (amiberry harness,
patterned on run-falcon-port).
