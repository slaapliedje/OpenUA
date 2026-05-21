# m68k-atari-mint cross toolchain configuration.
#
# Requires Vincent Rivière's m68k-atari-mint GCC binaries, or a toolchain
# built from cross-mint. Override CROSS= if your prefix differs.

CROSS  ?= m68k-atari-mint-
CC     := $(CROSS)gcc
LD     := $(CROSS)gcc
AR     := $(CROSS)ar
STRIP  := $(CROSS)strip
HATARI ?= hatari

# Target CPU.
#
# m68k-atari-mint ships only two relevant library sets: the default (68000,
# soft-float) and m68020-60 (hard-float — needs a 68881/68882 FPU). There is
# no soft-float 020-60 variant. The Falcon030 has no FPU, so the default
# build is plain 68000: soft-float, and 68000 code runs unchanged on the
# Falcon's (and the TT's) 68030. `make FPU=1` builds the hard-float 020-60
# variant for an FPU-equipped machine — the TT030, or an FPU'd Falcon.
CPU := -m68000
ifeq ($(FPU),1)
  CPU := -m68020-60 -m68881
endif

# -Wno-multichar: Mac code uses 4-character type codes ('WIND', 'DLOG', ...);
# they are well-defined on the port's single big-endian 68k target.
WARN := -Wall -Wextra -Wno-unused-parameter -Wno-multichar
OPT  ?= -O2 -g
STD  := -std=gnu99

CFLAGS  := $(CPU) $(STD) $(WARN) $(OPT)
ASFLAGS := $(CPU)
LDFLAGS := $(CPU)
