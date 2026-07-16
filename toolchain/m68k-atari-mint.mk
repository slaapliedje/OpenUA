# m68k-atari-mint cross toolchain configuration.
#
# Uses a private cross toolchain (th-otto m68k-atari-mint gcc 15.2.0) built
# with an added soft-float m68020-60 multilib — see
# docs/toolchain-softfloat-020.md. The stock distribution ships only an
# m68000 soft-float runtime and a *hard*-float m68020-60 one, so this repo
# needs the extra multilib to link FPU-less 020 code (the layer rule and the
# Falcon030's missing FPU both demand it).
#
# Override TOOLROOT if the toolchain lives elsewhere, or CROSS to use a
# different prefix entirely.
TOOLROOT ?= $(HOME)/opt/cross-mint
CROSS  ?= $(TOOLROOT)/bin/m68k-atari-mint-
CC     := $(CROSS)gcc
LD     := $(CROSS)gcc
AR     := $(CROSS)ar
STRIP  := $(CROSS)strip
HATARI ?= hatari

# Target CPU (see CLAUDE.md "Toolchain flags").
#
# Falcon030 and TT030 are both 68030, so build for 020/030/040/060
# (-m68020-60): it gets 32-bit muls/divs and bitfield ops that bare -m68000
# can't emit. -msoft-float selects the private soft-float m68020-60 multilib
# (FPU-less crt0/libgcc/libc), correct on the FPU-less Falcon030. Compile and
# link CPU now match — no m68000 link shim is needed.
#
# `make FPU=1` switches to the hard-float 68881 m68020-60 multilib for the
# TT030 / an FPU-equipped Falcon.
#
# Verify 020 codegen took with:
#   m68k-atari-mint-objdump -d <obj>.o | grep -E 'muls\.l|bfextu|bfins'
CPU   := -m68020-60 -msoft-float
LDCPU := -m68020-60 -msoft-float
ifeq ($(FPU),1)
  CPU   := -m68020-60 -m68881
  LDCPU := -m68020-60 -m68881
endif
# CPU68K=68000: a bare-68000 soft-float build (the private m68000 multilib). The
# ST/STE has a 68000, so its binary must be 68000-clean; the engine already is
# (the Mac source emits no 020-only instructions). A 68000 build also RUNS on
# the 030 machines (TT/Falcon), just without their 32-bit muls/bitfields — so
# this is the "runs everywhere" Atari binary, at some speed cost on the 030s.
ifeq ($(CPU68K),68000)
  CPU   := -m68000 -msoft-float
  LDCPU := -m68000 -msoft-float
endif

# -Wno-multichar: Mac code uses 4-character type codes ('WIND', 'DLOG', ...);
# they are well-defined on the port's single big-endian 68k target.
WARN := -Wall -Wextra -Wno-unused-parameter -Wno-multichar
OPT  ?= -O2 -g -fomit-frame-pointer
STD  := -std=gnu99

CFLAGS  := $(CPU) $(STD) $(WARN) $(OPT)
ASFLAGS := $(CPU)
LDFLAGS := $(LDCPU)
