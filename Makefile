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

# Mount the build directory as a GEMDOS drive and autostart the binary.
# Adjust paths/flags to taste once a Falcon TOS image is configured in Hatari.
run: $(TARGET)
	$(HATARI) --machine falcon -d . --auto $(TARGET)

# Host-side test suite — pytest over tools/. Not a cross-build.
PYTEST ?= tools/.venv/bin/pytest
test:
	$(PYTEST) tests -q

clean:
	$(RM) $(OBJ) $(DEP) $(TARGET)

-include $(DEP)

.PHONY: all run test clean
