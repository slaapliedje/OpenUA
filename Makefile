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
CFLAGS += $(EXTRA_CFLAGS)

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

# Faithful frustum-walker coordinate trace: drive jt199 once and log every
# slot's screen coord (via l5b42) so the 8000-space->screen transform can
# be re-derived against the clip viewport. Opt in with FRUA_COORD_TRACE=1.
ifeq ($(FRUA_COORD_TRACE),1)
CFLAGS += -DFRUA_COORD_TRACE
endif

# 3D-view frame-time benchmark: time 60 render frames (render-only and
# render+present) and log the tick counts, to gauge dungeon-view perf on
# the target. Opt in with FRUA_PERF_TEST=1.
ifeq ($(FRUA_PERF_TEST),1)
CFLAGS += -DFRUA_PERF_TEST
endif

# Toolbox shim bring-up demo: after ua_main returns, run the legacy
# WIND/menu-bar/Control-Manager exercise loop in main.c that proved the
# shim lands pixels during early bring-up. Default off — the real engine
# menu now owns the UI, and its Quit returns through ua_main straight to a
# clean teardown / desktop exit. Opt in with `make FRUA_SHIM_DEMO=1 run`.
ifeq ($(FRUA_SHIM_DEMO),1)
CFLAGS += -DFRUA_SHIM_DEMO
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
# console to this terminal; --dsp emu emulates the Falcon DSP. The --auto
# arg is an Atari path, not a host path — the GEMDOS-mounted build dir
# becomes C: inside the emulator, so frua.prg sits at C:\frua.prg.
FALCON_TOS ?= /usr/share/hatari/TOSv4.04.img
# Patch TOS to skip the memory test / slow boot path — shaves ~25s off every
# `make run`. Override (HATARI_FASTBOOT=) to boot a stock TOS.
HATARI_FASTBOOT ?= --fast-boot on
ifeq ($(FPU),1)
HATARI_FPU := --fpu 68881
endif
run: $(TARGET)
	$(HATARI) --machine falcon $(HATARI_FPU) $(HATARI_FASTBOOT) --dsp emu --tos $(FALCON_TOS) \
	          --conout 2 -d . --auto 'C:\$(TARGET)'

# Staged game-data folder for module-load testing. The engine opens
# .DAT/.GLB/.CTL files by bare filename from the GEMDOS mount, but the
# unpacked Mac release nests them under data/frua-mac/joined/. This
# target flattens the shared engine libraries (Disk1-4) plus the
# TUTORIAL.DSN design into one folder, with frua.prg / frua.rsc
# symlinked so they track the latest build. Mount it with:
#   make gamedata && GEMDOS_DIR=data/work/gamedata make probe
# or for the visible run, point hatari -d at it.
GAMEDATA_DIR := data/work/gamedata
MAC_JOINED   := data/frua-mac/joined
# Extra hand-dropped designs to stage alongside the bundled ones (for .DSN
# compatibility testing); each <name>.DSN folder there becomes a pickable
# design. Override or leave empty to skip.
DESIGNS_DIR  ?= data/designs
# Which design directory to flatten in alongside the shared libs.
# Override to test other modules, e.g. `make gamedata DSN=HEIRS.DSN`.
DSN ?= TUTORIAL.DSN
# Colour mouse pointer. The engine reads a flat 'frua.cur' FCUR pack and shows
# its cursor 0 in colour. Two sources, in priority order:
#   1. DOS_ALWAYS — path to the DOS release's ALWAYS.TLB (its 8bpp cursors are
#      the original art; loaded from your own data, not committed):
#        make run-game DOS_ALWAYS="/path/to/DOS/DATA/DISK1/ALWAYS.TLB"
#   2. assets/cursors/ — the bundled free (redrawn) cursor PNGs, packed by
#      tools/cursors_from_image.py. Used when DOS_ALWAYS is unset.
# Neither available -> the engine keeps the mono cursor.
DOS_ALWAYS ?=
CURSORS_DIR ?= assets/cursors
gamedata: $(TARGET) frua.rsc
	@if [ ! -d "$(MAC_JOINED)" ]; then \
		echo "  gamedata: $(MAC_JOINED) not found — unpack the Mac release first (docs/mac-release.md)"; \
		exit 1; \
	fi
	@# Re-stage the design assets but PRESERVE runtime-created characters:
	@# CHAR*.CHR is the saved-character roster the Training Hall writes
	@# (save_roster). A plain `rm -rf` here would delete every character you
	@# created last session, so a created character never survived a re-run.
	@mkdir -p "$(GAMEDATA_DIR)"
	@find "$(GAMEDATA_DIR)" -mindepth 1 -maxdepth 1 ! -name 'CHAR*.CHR' \
		-exec rm -rf {} +
	@for d in Disk1 Disk2 Disk3 Disk4; do \
		for f in "$(MAC_JOINED)/$$d"/*; do \
			[ -f "$$f" ] && cp "$$f" "$(GAMEDATA_DIR)/"; \
		done; \
	done
	@# Each design is staged as its own .DSN subdirectory, so the picker
	@# (jt315 -> L494e) can enumerate them and the file shim resolves
	@# "<name>.DSN:<file>" into the matching folder. All bundled designs
	@# are staged; $(DSN) is the one the boot seed makes current.
	@# Extra hand-dropped designs (data/designs/) are staged too, for
	@# .DSN compatibility testing; .dsn or .DSN, case-insensitive.
	@for dsn in "$(MAC_JOINED)"/*.DSN "$(DESIGNS_DIR)"/*.DSN "$(DESIGNS_DIR)"/*.dsn; do \
		[ -d "$$dsn" ] || continue; \
		base=$$(basename "$$dsn"); \
		mkdir -p "$(GAMEDATA_DIR)/$$base"; \
		for f in "$$dsn"/*; do \
			[ -f "$$f" ] && cp "$$f" "$(GAMEDATA_DIR)/$$base/"; \
		done; \
	done
	@# Stage the chosen design's SAVE folder (the shipped 1993 saves —
	@# SAVGAMA.CSV + VAULTA.DAT) at the gamedata root: mac_path_to_c
	@# strips non-.DSN folder prefixes, so "<dsn>:SAVE:SAVGAMA.CSV"
	@# resolves to the bare filename there.
	@if [ -d "$(MAC_JOINED)/$(DSN)/SAVE" ]; then \
		cp "$(MAC_JOINED)/$(DSN)/SAVE"/* "$(GAMEDATA_DIR)/" \
			&& echo "  gamedata: staged $(DSN)/SAVE saves"; \
	fi
	@# The faithful boot design marker "start.dat" (l0444 reads it at
	@# boot; jt128 rewrites it when a design is picked in-game): a
	@# 34-byte NUL-padded design name + the 1-byte resume flag.
	@printf '%s' "$(DSN)" | head -c 34 > "$(GAMEDATA_DIR)/start.dat"
	@truncate -s 34 "$(GAMEDATA_DIR)/start.dat"
	@printf '\001' >> "$(GAMEDATA_DIR)/start.dat"
	@rm -f "$(GAMEDATA_DIR)/CURRENT.TXT"
	@ln -sf "$(abspath $(TARGET))"  "$(GAMEDATA_DIR)/$(TARGET)"
	@ln -sf "$(abspath frua.rsc)"   "$(GAMEDATA_DIR)/frua.rsc"
	@if [ -n "$(DOS_ALWAYS)" ] && [ -f "$(DOS_ALWAYS)" ]; then \
		python3 tools/hlib_extract.py "$(DOS_ALWAYS)" --emit "$(GAMEDATA_DIR)/frua.cur" >/dev/null \
			&& echo "  gamedata: staged colour cursors from $(DOS_ALWAYS)"; \
	elif ls "$(CURSORS_DIR)"/*.png >/dev/null 2>&1; then \
		python3 tools/cursors_from_image.py "$(CURSORS_DIR)" -o "$(GAMEDATA_DIR)/frua.cur" >/dev/null \
			&& echo "  gamedata: staged free colour cursors from $(CURSORS_DIR)"; \
	fi
	@echo "  gamedata: staged $$(ls "$(GAMEDATA_DIR)" | grep -ivc frua) files + $(DSN) into $(GAMEDATA_DIR)"

run-game: gamedata
	$(HATARI) --machine falcon $(HATARI_FPU) $(HATARI_FASTBOOT) --dsp emu --tos $(FALCON_TOS) \
	          --conout 2 -d "$(GAMEDATA_DIR)" --auto 'C:\$(TARGET)'

# Bring-up probe: boot a probe-instrumented build in Hatari, fast-
# forward 15 seconds, capture the dbg_log output, and force-kill
# Hatari cleanly (avoids the "Really quit?" dialog that catches
# SIGTERM). `make probe` rebuilds with ENGINE_PROBE=1 and writes
# the trace to /tmp/probe.log; override duration / log path with
# PROBE_DURATION / PROBE_LOG.
PROBE_DURATION ?= 15
PROBE_LOG      ?= /tmp/probe.log
probe:
	$(MAKE) clean
	$(MAKE) ENGINE_PROBE=1
	tools/run_probe.sh $(PROBE_DURATION) $(PROBE_LOG)

# FC group-cache correctness audit: a probe build runs fc_cache_audit()
# in boot_a5_seed_defaults (register/resolve/append/size/MRU/purge/
# compaction/drop/flush over an isolated scratch pool), logging
# "FC AUDIT pass=N fail=M". This target boots it, extracts those lines,
# and fails if any check failed (or the audit never ran). Repeatable
# regression guard for the Resource Manager / FC layer.
FC_AUDIT_LOG ?= /tmp/fcaudit.log
fc-audit:
	$(MAKE) clean
	$(MAKE) ENGINE_PROBE=1
	@mkdir -p $(GAMEDATA_DIR)
	GEMDOS_DIR=$(GAMEDATA_DIR) tools/run_probe.sh 25 $(FC_AUDIT_LOG) >/dev/null
	@echo "--- FC cache audit ---"
	@grep -E "FC AUDIT|FC\.t" $(FC_AUDIT_LOG) || (echo "FC AUDIT: did not run" && exit 1)
	@grep -q "FC AUDIT fail = 0" $(FC_AUDIT_LOG) \
		&& echo "FC AUDIT: PASS" \
		|| (echo "FC AUDIT: FAIL (see $(FC_AUDIT_LOG))" && exit 1)

# Character-record / char-gen audit: a probe build runs the boot self-tests
# for the record-layout unification — the .cch save/load round-trip
# (jt578<->jt577 + L0ce0 byte-swap) and the faithful ability roll
# (l24d2/l1672 per race, scores read through the @112 faithful slot). This
# boots it, prints the results, and fails unless both PASS. Repeatable guard
# for CODE 17 char-gen + the save format.
CG_AUDIT_LOG ?= /tmp/cgaudit.log
# The self-tests run late in boot (after menu pumping), so allow ~45s and
# make sure no OTHER Hatari (e.g. `make run-game`) is mounting the same C:
# drive at the same time — concurrent instances stall the boot at the title.
cg-audit:
	$(MAKE) clean
	$(MAKE) ENGINE_PROBE=1
	tools/run_probe.sh 45 $(CG_AUDIT_LOG) >/dev/null
	@echo "--- character-record audit ---"
	@grep -E "cch round-trip self-test|cg roll self-test|^(Elf|HalfElf|Dwarf|Gnome|Halfling|Human)$$|stat = " $(CG_AUDIT_LOG) || (echo "CG AUDIT: did not run" && exit 1)
	@grep -q "cch round-trip self-test: PASS" $(CG_AUDIT_LOG) \
		&& grep -q "cg roll self-test: PASS" $(CG_AUDIT_LOG) \
		&& echo "CG AUDIT: PASS" \
		|| (echo "CG AUDIT: FAIL (see $(CG_AUDIT_LOG))" && exit 1)

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

.PHONY: all run run-game gamedata probe fc-audit cg-audit test test-slow clean data-pool-regen
