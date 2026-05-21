# Forgotten Realms: Unlimited Adventures
# Atari Falcon030 / TT030 port — top-level cross-build.
#
#   make            build frua.prg
#   make FPU=1      build an FPU-required TT030 variant
#   make run        boot the build in the Hatari emulator (Falcon mode)
#   make test       run the host-side pytest suite over tools/
#   make clean      remove build output

include toolchain/m68k-atari-mint.mk

TARGET  := frua.prg

SRCDIRS := src src/engine compat platform
INCLUDE := -Isrc -Icompat/include -Iplatform/include

CSRC := $(foreach d,$(SRCDIRS),$(wildcard $(d)/*.c))
ASRC := $(foreach d,$(SRCDIRS),$(wildcard $(d)/*.S))
OBJ  := $(CSRC:.c=.o) $(ASRC:.S=.o)
DEP  := $(OBJ:.o=.d)

CFLAGS += $(INCLUDE)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

%.o: %.S
	$(CC) $(ASFLAGS) -MMD -MP -c -o $@ $<

# Boot the build in Hatari (Falcon mode), GEMDOS-mounting the build dir.
# FALCON_TOS must point at a Falcon TOS 4.0x ROM — override if yours differs.
# `make run` emulates a stock (FPU-less) Falcon; `make run FPU=1` adds a
# 68881 so the hard-float FPU build can be tested. --conout 2 mirrors the
# console to this terminal; --dsp emu emulates the Falcon DSP.
FALCON_TOS ?= /usr/share/hatari/TOSv4.04.img
ifeq ($(FPU),1)
HATARI_FPU := --fpu 68881
endif
run: $(TARGET)
	$(HATARI) --machine falcon $(HATARI_FPU) --dsp emu --tos $(FALCON_TOS) \
	          --conout 2 -d . --auto $(TARGET)

# Host-side test suite — pytest over tools/. Not a cross-build.
PYTEST ?= tools/.venv/bin/pytest
test:
	$(PYTEST) tests -q

clean:
	$(RM) $(OBJ) $(DEP) $(TARGET)

-include $(DEP)

.PHONY: all run test clean
