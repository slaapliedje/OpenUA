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

# Target CPU: 68030 (Falcon030 / TT030).
#
# The Falcon030 ships with NO FPU; the TT030 has a 68882. The default build
# is soft-float so a single binary runs on both machines. Build with
# `make FPU=1` to produce an FPU-required variant tuned for the TT030.
CPU := -m68030
ifeq ($(FPU),1)
  CPU += -m68881
else
  CPU += -msoft-float
endif

WARN := -Wall -Wextra -Wno-unused-parameter
OPT  ?= -O2 -g
STD  := -std=gnu99

CFLAGS  := $(CPU) $(STD) $(WARN) $(OPT)
ASFLAGS := $(CPU)
LDFLAGS := $(CPU)
