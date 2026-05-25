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

# Engine bring-up probe: instrument the engine's unlifted stubs (boot.c,
# master.c) so each logs its name when called. Default off so production
# builds carry no logging spam. See docs/engine-bring-up.md.
ifeq ($(ENGINE_PROBE),1)
CFLAGS += -DFRUA_ENGINE_PROBE
endif

all: $(TARGET) frua.rsrc

$(TARGET): $(OBJ)
	$(LD) $(LDFLAGS) -o $@ $^

# Resource fork → flat FRSC archive (ADR-0007). The engine FSOpens
# frua.rsrc at startup and hands the bytes to the Resource Manager
# shim. The rfork itself is copyrighted FRUA data, so the source path
# lives under data/work/ (gitignored); the generated archive is
# gitignored too.
RFORK ?= data/work/UnlimitedAdventures.rfork
frua.rsrc: $(RFORK) tools/rsrcpack.py
	@if [ -f "$<" ]; then \
		python3 tools/rsrcpack.py $< -o $@; \
	else \
		echo "  frua.rsrc: $< not found; skipping (engine runs with no resources)"; \
	fi

# DATA + DREL replay tables — regenerated from frua.rsrc when the
# archive is present. The committed stubs (G_A5_*_LEN/COUNT = 0)
# keep the build happy on systems without the FRUA fork. See
# tools/dataemit.py for the encoder and docs/engine-bring-up.md for
# how the replay is applied at engine startup.
DATAPOOL_H := src/engine/data_pool.h
DATAPOOL_C := src/engine/data_pool.c
data-pool-regen: frua.rsrc tools/dataemit.py tools/datapool.py
	@if [ -f frua.rsrc ]; then \
		python3 tools/dataemit.py frua.rsrc \
			--out-h $(DATAPOOL_H) --out-c $(DATAPOOL_C) --summary; \
	else \
		python3 tools/dataemit.py --stub \
			--out-h $(DATAPOOL_H) --out-c $(DATAPOOL_C); \
		echo "  data_pool: stubbed (no frua.rsrc)"; \
	fi

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

.PHONY: all run test clean data-pool-regen
