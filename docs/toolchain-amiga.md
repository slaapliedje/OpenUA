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
- **newlib build fails on `malloc.cpp`** — its C++ compile rule omits the
  `-I…/newlib/libc/include` that the C rules have, so `<string.h>` isn't found.
  OPEN: add that include to the amigaos `malloc.o` rule (or set
  `CXXFLAGS_FOR_TARGET`). Until then libc.a isn't built; the newlib *headers*
  exist in the source tree (`projects/newlib-cygwin/newlib/libc/include`) and can
  be `-I`'d to compile (not link) the engine.

## Status (2026-07-14)

- ✅ **Toolchain**: working `m68k-amigaos-gcc 6.5.0b` + binutils, NDK headers
  integrated and auto-found. Built entirely from non-GitHub sources.
- 🔶 **libc**: newlib lib build blocked on the `malloc.cpp` include snag above
  (headers usable, lib not yet built).
- 🔶 **Engine compile**: attempting `make MACHINE=amiga` surfaced the real
  machine-coupling to port — these files still `#include <mint/osbind.h>`
  (GEMDOS) and need their Atari bits guarded + routed through the HAL, the same
  way `compat/files.c` already is: **`src/main.c`, `src/engine/error.c`,
  `compat/events.c`, `compat/macmemory.c`** (`compat/files.c` = done). That is
  the next concrete porting step.
- The `platform/amiga/` backends + `compat/files_amiga.c` remain structural
  skeletons (hardware bodies `TODO(hw)`). The Falcon/TT build is unaffected.
