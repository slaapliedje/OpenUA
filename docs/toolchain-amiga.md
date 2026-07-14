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

## Status

Scaffolded 2026-07-14 (ADR-0012). The build seam, the `platform/amiga/`
backends, and `compat/files_amiga.c` are structural skeletons: the machine-
neutral logic (backend wiring, palette conversion, the descriptor table) is
real; the hardware bring-up bodies are marked `TODO(hw)` and are **not yet
compiled or run** — the toolchain above is the prerequisite. The Falcon/TT build
is unaffected (`make` with no `MACHINE` still builds `frua.prg` identically).
