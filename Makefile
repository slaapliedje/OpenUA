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

# data_pool.c is generated at build time (see the data-pool rule
# below). It isn't on disk during the initial wildcard, so add the
# object explicitly.
ifeq ($(filter src/engine/data_pool.o,$(OBJ)),)
OBJ  += src/engine/data_pool.o
endif
DEP  := $(OBJ:.o=.d)

CFLAGS += $(INCLUDE)

# Engine bring-up probe: instrument the engine's unlifted stubs (boot.c,
# master.c) so each logs its name when called. Default off so production
# builds carry no logging spam. See docs/engine-bring-up.md.
ifeq ($(ENGINE_PROBE),1)
CFLAGS += -DFRUA_ENGINE_PROBE
endif

# Toolbox shim ALRT 200 splash demo: opens the real fork's "A disk error
# occurred!" alert during bring-up to prove the Dialog/Alert path lands
# pixels correctly. Default off so `make run` boots straight to the menu
# bar without a modal in the way. Opt in with `make FRUA_SPLASH_ALERT=1 run`.
ifeq ($(FRUA_SPLASH_ALERT),1)
CFLAGS += -DFRUA_SPLASH_ALERT
endif

all: $(TARGET) frua.rsc

$(TARGET): $(OBJ)
	$(LD) $(LDFLAGS) -o $@ $^

# Resource fork → flat FRSC archive (ADR-0007). The engine FSOpens
# frua.rsc at startup and hands the bytes to the Resource Manager
# shim. The rfork itself is copyrighted FRUA data, so the source path
# lives under data/work/ (gitignored); the generated archive is
# gitignored too.
RFORK ?= data/work/UnlimitedAdventures.rfork
frua.rsc: $(RFORK) tools/rsrcpack.py
	@if [ -f "$<" ]; then \
		python3 tools/rsrcpack.py $< -o $@; \
	else \
		echo "  frua.rsc: $< not found; skipping (engine runs with no resources)"; \
	fi

# DATA + DREL replay tables. These files are NEVER tracked in git
# (they would contain copyrighted FRUA application bytes when
# regenerated). Both .h and .c are produced fresh on every build:
# from frua.rsc when the archive is present, or as zero-entry
# stubs otherwise. See tools/dataemit.py for the encoder and
# docs/engine-bring-up.md for how the replay is applied at engine
# startup.
DATAPOOL_H := src/engine/data_pool.h
DATAPOOL_C := src/engine/data_pool.c
DATAPOOL_FILES := $(DATAPOOL_H) $(DATAPOOL_C)

$(DATAPOOL_FILES) &: frua.rsc tools/dataemit.py tools/datapool.py
	@if [ -f frua.rsc ]; then \
		python3 tools/dataemit.py frua.rsc \
			--out-h $(DATAPOOL_H) --out-c $(DATAPOOL_C); \
		echo "  data_pool: regenerated from frua.rsc"; \
	else \
		python3 tools/dataemit.py --stub \
			--out-h $(DATAPOOL_H) --out-c $(DATAPOOL_C); \
		echo "  data_pool: stubbed (no frua.rsc)"; \
	fi

# Make every .o depend on the data pool existing — but only as an
# order-only prereq so changing data_pool.{c,h} doesn't trigger an
# unrelated rebuild storm.
$(OBJ): | $(DATAPOOL_FILES)

# Keep the `data-pool-regen` alias for the explicit, no-arg invocation
# the docs reference.
data-pool-regen: $(DATAPOOL_FILES)

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

# Host-side test suite — pytest over tools/. Not a cross-build. The
# `slow` test boots frua.prg in Hatari and snaps a screenshot — skip
# by default; opt in with `make test-slow`.
PYTEST ?= tools/.venv/bin/pytest
test:
	$(PYTEST) tests -q

test-slow:
	$(PYTEST) tests -q -m slow

clean:
	$(RM) $(OBJ) $(DEP) $(TARGET) $(DATAPOOL_FILES)

-include $(DEP)

.PHONY: all run test test-slow clean data-pool-regen
