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

# Target CPU (see CLAUDE.md "Toolchain flags").
#
# Falcon030 and TT030 are both 68030, so COMPILE for 020/030/040/060
# (-m68020-60 -msoft-float): it gets 32-bit muls/divs and bitfield ops that
# bare -m68000 can't emit, with the soft-float ABI (no FPU instructions).
#
# But m68k-atari-mint ships no soft-float 020 *multilib* — `-m68020-60`
# selects the hard-float libgcc/crt0, whose startup aborts with "requires a
# 68881" on the FPU-less Falcon030. So we LINK against the default (m68000)
# soft-float runtime instead (LDCPU = -m68000): 020 object code + the
# FPU-less soft-float crt0/libgcc/libc. The soft-float helper routines are
# CPU-agnostic, so 020 objects call them fine and the binary boots without
# an FPU. `make FPU=1` uses the hard-float 020-60 multilib throughout (TT030
# / an FPU'd Falcon), where compile and link CPU match.
#
# Verify the compile flag took with:
#   m68k-atari-mint-objdump -d <obj>.o | grep -E 'muls\.l|bfextu|bfins'
CPU   := -m68020-60 -msoft-float
LDCPU := -m68000
ifeq ($(FPU),1)
  CPU   := -m68020-60 -m68881
  LDCPU := -m68020-60 -m68881
endif

# -Wno-multichar: Mac code uses 4-character type codes ('WIND', 'DLOG', ...);
# they are well-defined on the port's single big-endian 68k target.
WARN := -Wall -Wextra -Wno-unused-parameter -Wno-multichar
OPT  ?= -O2 -g -fomit-frame-pointer
STD  := -std=gnu99

CFLAGS  := $(CPU) $(STD) $(WARN) $(OPT)
ASFLAGS := $(CPU)
LDFLAGS := $(LDCPU)
