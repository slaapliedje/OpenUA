# Soft-float m68020-60 toolchain

The Falcon030 has **no FPU**, but bare `-m68000` codegen can't emit the 32-bit
`muls.l`/`divs.l`/bitfield ops the engine wants (CLAUDE.md "Toolchain flags").
We therefore build `-m68020-60` code with the **soft-float** ABI.

The stock m68k-atari-mint distribution can't link that: its multilib set is
only `.` (m68000 soft-float) and a **hard-float** `m68020-60` (it pulls in the
68881). `-m68020-60 -msoft-float` against the stock toolchain silently selects
the hard-float `m68020-60` runtime, whose `crt0` aborts at startup with *"This
program requires a 68881 or higher arithmetic coprocessor"* on the FPU-less
Falcon030.

The fix is a **private cross toolchain with an added soft-float `m68020-60`
multilib**. `toolchain/m68k-atari-mint.mk` points `CROSS` at it (default
`$(HOME)/opt/cross-mint`). This document records how to rebuild it; the
toolchain itself is not committed (it's ~GB of build tree).

## What gets built

A self-contained prefix at `$(TOOLROOT)` (default `~/opt/cross-mint`):

- **gcc 15.2.0** (th-otto m68k-atari-mint fork) with a `m68020-60/msoft-float`
  multilib added — so `-print-multi-lib` lists it and `-m68020-60 -msoft-float`
  selects an FPU-less 020 `libgcc` (soft-float helpers compiled as 020 code,
  **zero** FPU instructions).
- **mintlib** soft-float-020 `crt0.o`/`libc.a` installed into the matching
  sysroot dir `…/sys-root/usr/lib/m68020-60/msoft-float/`.
- The **system** binutils 2.45 (`/usr/bin/m68k-atari-mint-*`) are reused, via
  symlinks in the private prefix's `bin/` and tooldir `m68k-atari-mint/bin/`.

`make FPU=1` still selects the stock hard-float `m68020-60` multilib for the
TT030 / an FPU-equipped Falcon.

## Build steps

Prerequisites: native gcc, `gmp`/`mpfr`/`mpc`/`isl` dev libs, ~2 GB disk.

### 1. gcc

```sh
git clone --depth 1 -b mint/gcc-15 https://github.com/th-otto/m68k-atari-mint-gcc.git gcc-15
# add the soft-float 020 multilib:
( cd gcc-15 && patch -p1 < $REPO/toolchain/patches/gcc15-t-mint-softfloat-020.patch )

# private sysroot = copy of the system one (so we can add libs to it):
PFX=$HOME/opt/cross-mint
mkdir -p $PFX/m68k-atari-mint/sys-root
cp -a /usr/m68k-atari-mint/sys-root/. $PFX/m68k-atari-mint/sys-root/

mkdir build-gcc && cd build-gcc
../gcc-15/configure --target=m68k-atari-mint --prefix=$PFX \
  --with-sysroot=$PFX/m68k-atari-mint/sys-root --with-gcc-major-version-only \
  --with-gnu-as --with-gnu-ld --enable-languages=c --disable-nls \
  --disable-werror --disable-libssp --without-newlib \
  --with-pkgversion='falcon-port softfloat-020' \
  CFLAGS_FOR_TARGET='-O2 -fomit-frame-pointer'

# host gcc 16 defaults to a C++ dialect where u8"" is char8_t[] (breaks
# libcody) — pin the host dialect to gnu++17:
make -j$(nproc) all-gcc          CXXFLAGS='-g -O2 -std=gnu++17' CXXFLAGS_FOR_BUILD='-g -O2 -std=gnu++17'
make -j$(nproc) all-target-libgcc CXXFLAGS='-g -O2 -std=gnu++17'
make install-gcc install-target-libgcc

# reuse system binutils: symlink into the private prefix + gcc tooldir
for t in as ld ar ranlib nm strip objcopy objdump; do
  ln -sf /usr/bin/m68k-atari-mint-$t $PFX/bin/m68k-atari-mint-$t
  ln -sf /usr/bin/m68k-atari-mint-$t $PFX/m68k-atari-mint/bin/$t
done
```

### 2. mintlib (soft-float-020 crt0 + libc)

```sh
git clone --depth 1 https://github.com/th-otto/mintlib.git
cd mintlib
# add the lib020sf variant (Makefile SUBDIRS + lib020sf/ control files):
patch -p1 < $REPO/toolchain/patches/mintlib-softfloat-020.patch
cp lib020/Makefile lib020sf/Makefile     # then edit per the patch note:
#   subdir=lib020sf  instdir=m68020-60/msoft-float  cflags='-m68020-60 -msoft-float'
cp lib020/{BINFILES,SRCFILES,EXTRAFILES,MISCFILES,README} lib020sf/

export PATH=$PFX/bin:$PATH
# gcc 15 needs the era-appropriate dialect + tentative-def behaviour:
MFLAGS="CROSS_TOOL=m68k-atari-mint CFLAGS='-O2 -fomit-frame-pointer -fgnu89-inline -std=gnu99 -fcommon'"
make -C include CROSS_TOOL=m68k-atari-mint           # generate features.h / syscall-list.h
eval make -C lib020sf $MFLAGS -j$(nproc)             # -> lib020sf/libc.a (020 soft-float)

# soft-float-020 crt0/gcrt0 (startup/ builds only the default; compile directly):
CFL='-Wall -O2 -fomit-frame-pointer -fgnu89-inline -std=gnu99 -fcommon -m68020-60 -msoft-float -nostdinc -Iinclude -Imintlib'
m68k-atari-mint-gcc $CFL          -c startup/crt0.S -o crt0.o
m68k-atari-mint-gcc $CFL -DGCRT0  -c startup/crt0.S -o gcrt0.o

# install into the new multilib dir:
SF=$PFX/m68k-atari-mint/sys-root/usr/lib/m68020-60/msoft-float
mkdir -p $SF
cp lib020sf/libc.a lib020sf/libiio.a crt0.o gcrt0.o $SF/
```

### 3. Verify

```sh
G=$PFX/bin/m68k-atari-mint-gcc
$G -m68020-60 -msoft-float -print-multi-directory      # => m68020-60/msoft-float
$G -m68020-60 -msoft-float -print-file-name=crt0.o     # => …/msoft-float/crt0.o
# soft-float (no FPU insns) + 020 codegen present:
m68k-atari-mint-objdump -d $($G -m68020-60 -msoft-float -print-libgcc-file-name) \
  | grep -cE 'fmove|fadd|fmul'        # 0
m68k-atari-mint-objdump -d build/src/engine/boot.o \
  | grep -cE 'muls\.l|bfextu|bfins'   # > 0
```

Then `make` in the repo links cleanly with compile and link both
`-m68020-60 -msoft-float`, and `make run` boots on a stock (FPU-less) Falcon.
