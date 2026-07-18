# m68k-amigaos cross toolchain configuration (Amiga AGA target — ADR-0012).
#
# Uses Bebbo's m68k-amigaos GCC (github.com/bebbo/amiga-gcc): modern GCC that
# emits AmigaOS hunk executables and bundles the NDK (exec/dos/graphics/
# intuition). The codebase is already GCC -std=gnu99 -m68020 C, so it builds
# with this toolchain unchanged apart from the platform/amiga backend.
#
# This toolchain is NOT a stock package — build it from source on a networked
# host (see docs/toolchain-amiga.md), then point TOOLROOT here (Bebbo's default
# install prefix is /opt/amiga; this repo defaults to ~/opt/amiga to avoid sudo).
#
# Override TOOLROOT if the toolchain lives elsewhere, or CROSS for a different
# prefix entirely (e.g. a system /usr install).
TOOLROOT ?= $(HOME)/opt/amiga
CROSS    ?= $(TOOLROOT)/bin/m68k-amigaos-
CC       := $(CROSS)gcc
LD       := $(CROSS)gcc
AR       := $(CROSS)ar
# m68k-amigaos-strip CORRUPTS hunk executables: the stripped binary
# wild-jumps out of libnix's constructor-list walker at startup
# (verified under vamos 2026-07-18 — the unstripped and link-time '-s'
# binaries run, the post-stripped one crashes at pc=RAM-top before
# main). Strip at LINK time with '-s' instead (release targets pass
# EXTRA_LDFLAGS=-s); strip-target is a safe no-op on this machine.
STRIP    := true --skipped-amiga-strip-corrupts-hunks

# amiberry is a flatpak here; the run: target invokes it via flatpak. Override
# AMIBERRY if you have a native binary on PATH.
AMIBERRY ?= flatpak run com.blitterstudio.amiberry

# Target CPU — a per-machine KNOB, not a codebase assumption. The engine is
# 68000-clean by construction (the original Mac FRUA binary contains zero
# 68020-only instructions across all 22 CODE segments — the compiler flags
# alone decide the codegen tier), which is deliberate: an ECS Amiga (A500,
# 68000) target is on the roadmap, alongside Atari ST/STe. AGA's baseline is
# the A1200 (68EC020), so 68020 is this machine's default; override for a
# 68000 test build with:
#
#   make MACHINE=amiga CPU68K=68000
#
# Soft-float everywhere: no FPU is assumed, exactly as on the FPU-less
# Falcon030 build. Bebbo's multilibs cover both tiers (the "." base multilib
# is 68000; libm020 is selected automatically by -m68020).
#
# Verify 020 codegen with (NB: this objdump DECODES as 68000 by default —
# without -m the 020 ops print as `.short` garbage and grep counts zero):
#   m68k-amigaos-objdump -d -m m68k:68020 <obj> | grep -cE 'mulsl|bfextu|bfins'
CPU68K ?= 68020
CPU   := -m$(CPU68K) -msoft-float
LDCPU := -m$(CPU68K) -msoft-float

# FRUA_AMIGA gates the machine-specific paths (compat/files.c, platform/amiga).
# -noixemul: link against libnix (a standalone C runtime), NOT ixemul.library —
# the game owns the machine and must not depend on the ixemul unix-emulation
# layer being installed on the target.
WARN := -Wall -Wextra -Wno-unused-parameter -Wno-multichar
OPT  ?= -O2 -g -fomit-frame-pointer
STD  := -std=gnu99
DEFS := -DFRUA_AMIGA

# ★ MISCOMPILER WORKAROUNDS — do not re-enable without re-verifying BOTH
# reproducers (docs/toolchain-amiga.md). TWO of Bebbo GCC 6.5.0b's own -fbbb
# passes miscompile this engine; this value is the default set MINUS them:
#
#  "h" (optimize shift instructions): narrows a 32-bit shift of a 16-bit-
#      typed value to WORD shifts when it can sink the shift past calls —
#      jt463's `(long)maxkb * 1024L` became `lslw #8; lslw #2` = a 2KB FAR
#      pool ("Insufficient FAR Memory!" at boot, heisen-masked by any nearby
#      instrumentation).
#
#  "r" (basic-block register rename): clobbers a live pointer under some
#      rename shape — jt1015/l3b1e's library pointer arrived as garbage
#      ("LBISize: Invalid Library File" on LOAD SAVED GAME, deterministic).
#      Convicted by bisection: every -fbbb set containing r fails, every
#      set without it passes (2026-07-15; the load-save-A repro).
BBB  := -fbbb=abcefilmnpsz0

CFLAGS  := $(CPU) $(STD) $(WARN) $(OPT) $(DEFS) $(BBB)
ASFLAGS := $(CPU) $(DEFS)
LDFLAGS := $(LDCPU) -noixemul
