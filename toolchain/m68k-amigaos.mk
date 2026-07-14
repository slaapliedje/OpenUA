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
STRIP    := $(CROSS)strip

# amiberry is a flatpak here; the run: target invokes it via flatpak. Override
# AMIBERRY if you have a native binary on PATH.
AMIBERRY ?= flatpak run com.blitterstudio.amiberry

# Target CPU. AGA's baseline machine is the A1200 (68EC020), so -m68020 covers
# the whole AGA range (020/030/040/060). Soft-float: no FPU is assumed, exactly
# as on the FPU-less Falcon030 build — one binary serves a bare A1200 and an
# FPU-equipped 030/040. Bebbo's default multilib is soft-float, so -msoft-float
# is the natural (and only universally-safe) choice for shared code.
#
# Verify 020 codegen took with:
#   m68k-amigaos-objdump -d <obj>.o | grep -E 'muls\.l|bfextu|bfins'
CPU   := -m68020 -msoft-float
LDCPU := -m68020 -msoft-float

# FRUA_AMIGA gates the machine-specific paths (compat/files.c, platform/amiga).
# -noixemul: link against libnix (a standalone C runtime), NOT ixemul.library —
# the game owns the machine and must not depend on the ixemul unix-emulation
# layer being installed on the target.
WARN := -Wall -Wextra -Wno-unused-parameter -Wno-multichar
OPT  ?= -O2 -g -fomit-frame-pointer
STD  := -std=gnu99
DEFS := -DFRUA_AMIGA

CFLAGS  := $(CPU) $(STD) $(WARN) $(OPT) $(DEFS)
ASFLAGS := $(CPU) $(DEFS)
LDFLAGS := $(LDCPU) -noixemul
