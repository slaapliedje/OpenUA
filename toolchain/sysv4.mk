# AMIX (Amiga UNIX) and Atari System V: SVR4.0 on the 68020/030. One static
# AMIX binary serves both (ASV runs it through atari-sysv-sp1's amx module).
# The mint GCC compiles against AMIX's headers and the SVR4 binutils of
# gcc-cross-amix assemble and link: see toolchain/sysv4-cc and sysv4-ld.
#   AMIX_SYSROOT  AMIX's headers and libraries, X11 included
#                 (atari-sysv-sp1: amix/mksysroot.sh)
ifeq ($(AMIX_SYSROOT),)
$(error MACHINE=amix needs AMIX_SYSROOT (atari-sysv-sp1 amix/mksysroot.sh))
endif
export AMIX_SYSROOT
CC     := toolchain/sysv4-cc
LD     := toolchain/sysv4-ld
AR     := $(HOME)/opt/asv-cross/bin/m68k-cbm-sysv4-ar
STRIP  := $(HOME)/opt/asv-cross/bin/m68k-cbm-sysv4-strip

# -mno-align-int: the engine's Mac structures need GCC's 2-byte alignment of
# int, not SVR4's 4 (see sysv4-cc)
CPU   := -m68020-60 -msoft-float -mno-align-int
WARN  := -Wall -Wextra -Wno-unused-parameter -Wno-multichar
OPT   ?= -O2 -fomit-frame-pointer
STD   := -std=gnu99
DEFS  := -DFRUA_UNIX
CFLAGS  := $(CPU) $(STD) $(WARN) $(OPT) $(DEFS)
ASFLAGS := $(CPU) $(DEFS)
LDFLAGS :=
LDLIBS  := -lX11 -lsocket
