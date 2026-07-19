# OpenUA — an open reimplementation of SSI's Unlimited Adventures engine
# for 68k retro machines. Top-level cross-build.
#
# MACHINE=falcon (default): Atari Falcon030 / TT030.  MACHINE=amiga: Amiga AGA.
#
#   make            build frua.prg
#   make FPU=1      build an FPU-required TT030 variant
#   make run        boot the build in the Hatari emulator (Falcon mode)
#   make test       run the host-side pytest suite over tools/
#   make clean      remove build output

# Target machine (ADR-0012). `falcon` (default) builds the Atari Falcon030/TT030
# .prg; `amiga` builds the Amiga AGA hunk executable. Only the toolchain and the
# platform/ backend set differ — engine + compat + the shared c2p are identical.
#   make                  Falcon030/TT030 (default)
#   make MACHINE=amiga     Amiga AGA (needs the Bebbo toolchain; docs/toolchain-amiga.md)
MACHINE ?= falcon

# Shared platform sources (machine-neutral): the chunky->planar converter is
# pure 68k asm used by any bitplane display backend (AGA; a future STe).
PLATFORM_SHARED := platform/c2p.S platform/planar.c

ifeq ($(MACHINE),amiga)
include toolchain/m68k-amigaos.mk
TARGET       := frua
PLATFORM_SRC := $(PLATFORM_SHARED) \
                platform/amiga/display_aga.c \
                platform/amiga/display_rtg.c \
                platform/amiga/display_ecs.c \
                platform/amiga/c2p_amiga.c \
                platform/amiga/sound_paula.c \
                platform/amiga/input_amiga.c \
                platform/amiga/dbglog_amiga.c \
                platform/amiga/sys_amiga.c \
                platform/amiga/vdi_stub.c
else ifeq ($(MACHINE),falcon)
include toolchain/m68k-atari-mint.mk
TARGET       := frua.prg
PLATFORM_SRC := $(PLATFORM_SHARED) \
                platform/display_videl.c \
                platform/display_tt.c \
                platform/display_ste.c \
                platform/display_sthigh.c \
                platform/sound_falcon.c \
                platform/input.c \
                platform/vdi.c \
                platform/dbglog.c \
                platform/sys_falcon.c
else
$(error unknown MACHINE '$(MACHINE)' — use 'falcon' or 'amiga')
endif

SRCDIRS := src src/engine compat
INCLUDE := -Isrc -Icompat/include -Iplatform/include

# Machine stamp: the two machines share object paths, so a MACHINE switch
# without a clean silently links stale other-machine objects (bitten three
# times — an empty-guard files.o from the falcon build once linked into the
# amiga binary). When the stamp disagrees with $(MACHINE), purge all objects
# at parse time; the stamp file makes same-machine rebuilds incremental.
# The stamp includes the CPU tier and FPU flag, not just the machine: a
# `make CPU68K=68000` after a plain 020 `make` used to rebuild only changed
# files as 68000 and silently link the rest as 68020 — an illegal-instruction
# death on a real 68000 that looks like an engine bug (bitten 2026-07-15).
BUILDSTAMP := $(MACHINE)-$(or $(CPU68K),default)-$(or $(FPU),nofpu)-$(words $(EXTRA_CFLAGS))$(firstword $(EXTRA_CFLAGS))
ifneq ($(shell cat .machine 2>/dev/null),$(BUILDSTAMP))
$(shell find src compat platform -name '*.o' -delete 2>/dev/null; \
        find src compat platform -name '*.d' -delete 2>/dev/null; \
        echo $(BUILDSTAMP) > .machine)
endif

CSRC := $(foreach d,$(SRCDIRS),$(wildcard $(d)/*.c)) $(filter %.c,$(PLATFORM_SRC))
ASRC := $(filter %.S,$(PLATFORM_SRC))
OBJ  := $(CSRC:.c=.o) $(ASRC:.S=.o)

# data_pool.c is generated at build time (see the data-pool rule
# below). It isn't on disk during the initial wildcard, so add the
# object explicitly.
ifeq ($(filter src/engine/data_pool.o,$(OBJ)),)
OBJ  += src/engine/data_pool.o
endif

# The DOS-art converter core (ADR-0014) lives outside SRCDIRS on purpose:
# src/convert/ also holds the host-CLI and benchmark mains, which must
# never link into the engine. Add just the core object.
OBJ  += src/convert/artconv.o
DEP  := $(OBJ:.o=.d)

CFLAGS += $(INCLUDE)
CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS += $(EXTRA_LDFLAGS)

# Engine bring-up probe: instrument the engine's stubs so each logs its name
# when called. Default off so production builds carry no logging spam.
# See docs/engine-bring-up.md.
#
#   make ENGINE_PROBE=1        coverage set -> DBG.LOG. THE ONE TO USE.
#   make ENGINE_PROBE=flood    every call -> VT-52 console. See the warning.
#
# ENGINE_PROBE=1 selects the DEDUPED FILE probe (each name logged once, in
# first-hit order, to DBG.LOG). It boots to the menu and stays interactive.
#
# It used to select the per-call console probe, and that build CRASHED a couple
# of seconds into boot (a GEMDOS trap from the sound VBL — see platform/dbglog.c)
# after logging ~1900 lines. A probe that dies mid-run reports "0 calls" for
# everything afterwards, which reads as "this code never runs": it produced
# THREE false negatives in a single session. The crash is fixed, but the flood
# mode is still a poor default — 70k+ Cconws calls are so slow the engine never
# reaches the menu, and VT-52 text scribbles over the framebuffer. Keep it
# opt-in, and read a "0" from it with suspicion.
ifeq ($(ENGINE_PROBE),1)
CFLAGS += -DFRUA_ENGINE_PROBE_ONCE
endif
ifeq ($(ENGINE_PROBE),flood)
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
#
# NOEMBED=1 forces the stub even when frua.rsc IS present: the binary
# then carries ZERO copyrighted bytes and data_pool_replay() rebuilds
# the A5 world from the user's frua.rsc at runtime (the identical
# DATA/ZERO/DREL decode, ported to C in data_pool_decode.h). This is
# how a REDISTRIBUTABLE binary is produced — pair it with a stripped
# link (see `make release`). Requires the runtime frua.rsc to supply
# the data the binary no longer embeds.
DATAPOOL_H := src/engine/data_pool.h
DATAPOOL_C := src/engine/data_pool.c
DATAPOOL_FILES := $(DATAPOOL_H) $(DATAPOOL_C)

$(DATAPOOL_FILES) &: frua.rsc tools/dataemit.py tools/datapool.py
	@if [ -z "$(NOEMBED)" ] && [ -f frua.rsc ]; then \
		python3 tools/dataemit.py frua.rsc \
			--out-h $(DATAPOOL_H) --out-c $(DATAPOOL_C); \
		echo "  data_pool: regenerated from frua.rsc (embedded — not redistributable)"; \
	else \
		python3 tools/dataemit.py --stub \
			--out-h $(DATAPOOL_H) --out-c $(DATAPOOL_C); \
		if [ -n "$(NOEMBED)" ]; then \
			echo "  data_pool: STUBBED (NOEMBED) — runtime replay from frua.rsc, redistributable"; \
		else \
			echo "  data_pool: stubbed (no frua.rsc)"; \
		fi; \
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
# TOS 4.04 fallback chain: the system copy, then the user-local download, then
# EmuTOS 512K. Only NON-EMPTY files count — a botched ROM copy left 0-byte TOS
# files once (2026-07-15), and hatari's "FATAL: Can not load TOS" for an empty
# file looks like a harness bug.
FALCON_TOS ?= $(shell for f in /usr/share/hatari/TOSv4.04.img \
	$(HOME)/Downloads/Atari/tos404.img; do \
	[ -s $$f ] && { echo $$f; exit; }; done; \
	echo /usr/share/hatari/etos512us.img)
# Emulated ST-RAM in MB. Falcon-valid sizes: 1, 4, 14. Development default
# is 14 so memory-pressure flakes don't mask logic bugs; the shipping
# targets are 4 (the pragmatic floor today) and eventually 1 (the Mac's
# footprint) — test those with `make run HATARI_MEM=4` / `HATARI_MEM=1`.
HATARI_MEM ?= 14
# Patch TOS to skip the memory test / slow boot path — shaves ~25s off every
# `make run`. Override (HATARI_FASTBOOT=) to boot a stock TOS.
HATARI_FASTBOOT ?= --fast-boot on
ifeq ($(FPU),1)
HATARI_FPU := --fpu 68881
endif
run: $(TARGET)
	$(HATARI) --machine falcon --memsize $(HATARI_MEM) $(HATARI_FPU) $(HATARI_FASTBOOT) --dsp emu --tos $(FALCON_TOS) \
	          --conout 2 -d . --auto 'C:\$(TARGET)'

# Boot the current build on an emulated STE. Needs an EmuTOS image (TOS 4.04
# is Falcon-only) — override EMUTOS with your path. Mounts the staged
# gamedata dir as C: (stage it once with `make gamedata DSN=HEIRS.DSN`).
# Builds its own 68000 configuration (same pattern as run-mono: depending
# on $(TARGET) let a bare `make run-ste` silently rebuild the default
# 68020 binary, which dies on the STE's 68000).
EMUTOS ?= $(HOME)/Downloads/Atari/etos256us.img
run-ste:
	$(MAKE) CPU68K=68000 EXTRA_CFLAGS="$(EXTRA_CFLAGS)"
	$(HATARI) --machine ste --memsize 4 --tos $(EMUTOS) --zoom 2 --sound 44100 \
	          --conout 2 -d $(GAMEDATA_DIR) --auto 'C:\$(TARGET)'

# Boot the B&W (Mac monochrome) build on an emulated ST with a mono monitor
# (ST High, 640x400). Needs a real ST TOS ROM (2.06 recommended — EmuTOS
# also boots ST High). Just:
#   make run-mono
# The target BUILDS the right configuration itself (recursive make with
# CPU68K=68000 + FRUA_BWMODE — the build stamp repurges objects as needed).
# Depending on $(TARGET) directly was a trap: a bare `make run-mono` after
# a flagged build re-evaluated with DEFAULT flags, silently rebuilt the
# 68020 colour binary, and MiNTLib's crt0 then refused it on the ST with
# "requires a 68020". The engine auto-detects the mono monitor
# (Getrez()==2) and runs the Mac's own 1-bit mode: 480x300 game window,
# .TLB art, the L4e12 text plates.
ST_TOS ?= $(shell for f in /usr/share/hatari/tos206us.img \
                           $(HOME)/Downloads/Atari/tos206us.img \
                           /usr/share/hatari/etos512us.img; do \
                      [ -f $$f ] && { echo $$f; break; }; done)
run-mono:
	$(MAKE) CPU68K=68000 EXTRA_CFLAGS="-DFRUA_BWMODE $(EXTRA_CFLAGS)"
	$(HATARI) --machine st --memsize 4 --monitor mono --tos $(ST_TOS) \
	          --zoom 2 --conout 2 -d $(GAMEDATA_DIR) --auto 'C:\$(TARGET)'

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
	@# Each design's own files go in its .DSN folder, AND its SAVE/ slots go
	@# there too (SAVGAMA.CSV + VAULTA.DAT). The engine's savgam_path scopes
	@# save slots to "<current-design>.DSN\SavGam*.csv", so switching designs
	@# in-game (Select a Design) switches which slots Load/Save sees — without
	@# this per-design staging, Load always found the one flat-staged design's
	@# saves regardless of the picked design.
	@for dsn in "$(MAC_JOINED)"/*.DSN "$(DESIGNS_DIR)"/*.DSN "$(DESIGNS_DIR)"/*.dsn; do \
		[ -d "$$dsn" ] || continue; \
		base=$$(basename "$$dsn"); \
		mkdir -p "$(GAMEDATA_DIR)/$$base"; \
		for f in "$$dsn"/*; do \
			[ -f "$$f" ] && cp "$$f" "$(GAMEDATA_DIR)/$$base/"; \
		done; \
		if [ -d "$$dsn/SAVE" ]; then \
			cp "$$dsn/SAVE"/* "$(GAMEDATA_DIR)/$$base/" 2>/dev/null || true; \
		fi; \
	done
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
	$(HATARI) --machine falcon --memsize $(HATARI_MEM) $(HATARI_FPU) $(HATARI_FASTBOOT) --dsp emu --tos $(FALCON_TOS) \
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

# Clean BOTH machines' objects, not just $(OBJ) (the current MACHINE's list):
# the machines share object paths, so a stale other-machine .o silently links
# into the next build (seen live: a 68020 sys_amiga.o survived a falcon-mode
# clean and linked into a -m68000 build).
clean:
	$(RM) $(OBJ) $(DEP) $(TARGET) $(DATAPOOL_FILES)
	find src compat platform -name '*.o' -delete 2>/dev/null || true
	find src compat platform -name '*.d' -delete 2>/dev/null || true
	$(RM) frua frua.prg uainst.ttp uainst_amiga uainst.info

# clean does NOT remove dist/ — release-all cleans objects between platforms and
# must keep the earlier binaries' packaged output. `distclean` wipes dist too.
distclean: clean
	$(RM) -r dist

# --- installer ---------------------------------------------------------------
#
# uainst.ttp — the native fan-module installer (task #23 / ADR-0014):
# ZIP -> extract into <dest>/<MODULE>.DSN -> convert all DOS art with the
# same byte-exact core the engine links (colour .ctl twins + the mono
# .tlb synthesis the engine deliberately never does on the fly).
# A single 68000 build runs on every Atari. Drag the module ZIP onto it
# from the desktop (TTP args), or run it bare and type the ZIP name.
# ZIP reading is vendored public-domain miniz v1.14 (installer/miniz.c).
installer: uainst.ttp
uainst.ttp: installer/main.c installer/miniz.c src/convert/artconv.c src/convert/artconv.h
	$(CC) -m68000 -msoft-float -std=gnu99 -O2 -fomit-frame-pointer \
	    -o $@ installer/main.c installer/miniz.c src/convert/artconv.c
	$(STRIP) $@

# The Amiga build of the same installer. Adds installer/asl_amiga.c: with
# no ZIP argument it pops the standard asl.library file/drawer requesters
# (task #24) instead of prompting on the console, then runs the identical
# extract+convert core. Uses the Bebbo toolchain directly — installer
# builds are standalone, not part of the MACHINE= engine object tree.
AMIGA_CROSS ?= $(HOME)/opt/amiga/bin/m68k-amigaos-
installer-amiga: uainst_amiga uainst.info
uainst_amiga: installer/main.c installer/asl_amiga.c installer/miniz.c src/convert/artconv.c src/convert/artconv.h
	$(AMIGA_CROSS)gcc -m68000 -msoft-float -noixemul -std=gnu99 -O2 \
	    -fomit-frame-pointer -s \
	    -o $@ installer/main.c installer/asl_amiga.c installer/miniz.c \
	    src/convert/artconv.c
	# no post-link strip: m68k-amigaos-strip corrupts hunk executables
	# (see toolchain/m68k-amigaos.mk) — '-s' above strips at link time

# Workbench icon for uainst. A tool with no .info cannot be launched from
# Workbench at all; this one also carries a STACK=200000 tooltype and a
# matching do_StackSize so a double-click gets a generous stack (belt-and-
# braces with the installer's own StackSwap).
uainst.info: tools/make_amiga_icon.py
	python3 tools/make_amiga_icon.py --type tool --stack 200000 \
	    --tooltype 'STACK=200000' --x 12 --y 8 -o $@

# --- release ----------------------------------------------------------------
#
# `make release` builds the shipping binary with -DFRUA_RELEASE, which hard-
# errors if any BEHAVIOUR-ALTERING debug flag is enabled (FRUA_AUTOWIN silently
# kills the monster side; FRUA_SKIP_ENTRY_EVENTS skips the event chain;
# FRUA_CORRIDOR / FRUA_RAYCAST swap the renderer). Those must never ship, and
# "we'll remember" is not a mechanism — see src/engine/release_guard.h.
#
# Emulator-validated only: nothing here has been run on real Falcon030/TT030
# hardware. Say so in the release notes.
VERSION ?= 0.3.1-beta

# Every release binary is REDISTRIBUTABLE: NOEMBED=1 stubs the copyrighted DATA
# pool (rebuilt at runtime from the user's frua.rsc), the link is stripped, and
# frua.rsc is deliberately NOT bundled (it is the user's copyrighted game data).
# The shipped binary contains only the port's own code — docs/redistributable-
# binary.md. -DFRUA_RELEASE hard-errors on any behaviour-altering debug flag
# (src/engine/release_guard.h).
#
#   make release            Atari Falcon030 / TT030  (one .prg for both)
#   make release-amiga      Amiga AGA + RTG          (one hunk, runtime-detected)
#   make release-amiga-ecs  Amiga bare ECS/OCS       (32-colour native bitplanes)
#   make release-all        all three
#
# PKG_DIST $(1)=distname $(2)=binary $(3)=platform label $(4)=needs line
define PKG_DIST
	@mkdir -p dist/$(1)
	@cp $(2) dist/$(1)/
	@cp README.md GAMEDATA.md docs/enhancements.md dist/$(1)/ 2>/dev/null || true
	@cp tools/art_convert.py dist/$(1)/
	@cp docs/converter-howto.md dist/$(1)/CONVERTER.md
	@case "$(1)" in \
	*falcon*|*atari*) \
		[ -f uainst.ttp ] && cp uainst.ttp dist/$(1)/UAINST.TTP || true;; \
	*amiga*) \
		[ -f uainst_amiga ] && cp uainst_amiga dist/$(1)/uainst || true; \
		[ -f uainst.info ] && cp uainst.info dist/$(1)/uainst.info || true;; \
	esac
	@case "$(1)" in \
	*amiga*) \
		printf 'OpenUA (%s) %s\n\nAn open reimplementation of SSI'"'"'s Unlimited Adventures engine.\n\nEMULATOR-VALIDATED ONLY: never run on real hardware. Please report\nwhat happens if you do.\n\nThis binary contains NO copyrighted game data. You supply your own\nfrua.rsc (built from your legally-obtained Unlimited Adventures copy;\nsee README) plus the design/data files; the engine reconstructs its\ninternal tables from frua.rsc at launch.\n\n%s\n\nAll 8 exploration commands work (MOVE AREA CAST VIEW ENCAMP SEARCH\nLOOK INV). Shops, temples, combat, save/load and equipping work.\nSee enhancements.md for the known gaps.\n\nNEW: PC (DOS) fan modules play with their own custom art — two ways.\nInstall straight from the module ZIP with the bundled uainst (it extracts\nAND converts every frame, including the 1-bit art the ECS build needs).\nOr convert on your PC with art_convert.py (Python 3); see CONVERTER.md.\nNOTE: unlike the Atari build, the Amiga engine does NOT convert DOS art\non the fly — always run a DOS module through uainst or art_convert.py\nbefore playing it. Every art format in the fan corpus is supported,\nseveral proven byte-identical against SSI'"'"'s own Mac files.\n' '$(3)' '$(VERSION)' '$(4)' > dist/$(1)/RELEASE.TXT;; \
	*) \
		printf 'OpenUA (%s) %s\n\nAn open reimplementation of SSI'"'"'s Unlimited Adventures engine.\n\nEMULATOR-VALIDATED ONLY: never run on real hardware. Please report\nwhat happens if you do.\n\nThis binary contains NO copyrighted game data. You supply your own\nfrua.rsc (built from your legally-obtained Unlimited Adventures copy;\nsee README) plus the design/data files; the engine reconstructs its\ninternal tables from frua.rsc at launch.\n\n%s\n\nAll 8 exploration commands work (MOVE AREA CAST VIEW ENCAMP SEARCH\nLOOK INV). Shops, temples, combat, save/load and equipping work.\nSee enhancements.md for the known gaps.\n\nNEW: PC (DOS) fan modules play with their own custom art — three ways.\nEasiest: just drop the module'"'"'s files into a .DSN folder; the engine\nconverts DOS art in place on first touch. Or install straight from the\nZIP with UAINST.TTP (extracts AND converts, including the 1-bit art the\nmono ST build needs). Or convert on your PC with the bundled\nart_convert.py (Python 3). See CONVERTER.md. Every art format in the\nfan corpus is supported, several proven byte-identical against SSI'"'"'s\nown Mac files.\n' '$(3)' '$(VERSION)' '$(4)' > dist/$(1)/RELEASE.TXT;; \
	esac
	@cd dist && zip -qr $(1).zip $(1)
	@echo "release -> dist/$(1).zip  (redistributable: no game data embedded)"
endef

# Strip the current MACHINE's binary (invoked as a sub-make so $(STRIP)/$(TARGET)
# resolve for that machine's toolchain).
strip-target:
	$(STRIP) $(TARGET)

# NOTE on ordering: `make test` MUST run BEFORE the release build, not after.
# tests/test_build.py runs a bare `subprocess.run(["make"])` — a DEFAULT build
# (68020, embedded data pool, unstripped). Run after the release build it
# clobbers frua.prg right before PKG_DIST: the ST zip shipped a 68020 binary
# (illegal instructions on a real 68000), everything shipped unstripped, and
# the "redistributable" NOEMBED binary was replaced by an embedded one. Test
# first, then clean wipes its artifacts, then the real build is the last thing
# to touch the binary. (Verified 2026-07-18: bfextu 0->2302, syms 0->93892
# when `make test` followed a stripped 68000 build.)
release:
	$(MAKE) test
	$(MAKE) clean
	$(MAKE) installer
	$(MAKE) NOEMBED=1 EXTRA_CFLAGS='-DFRUA_RELEASE -DFRUA_VERSION=\"$(VERSION)\"'
	$(MAKE) strip-target
	$(call PKG_DIST,openua-falcon-$(VERSION),frua.prg,Atari Falcon030/TT030,Needs: 4MB RAM and TOS 4.04 (Falcon) or 3.0x (TT). One binary serves both — the display/sound path is chosen at runtime.)

release-amiga:
	$(MAKE) test
	$(MAKE) clean
	$(MAKE) installer-amiga
	$(MAKE) MACHINE=amiga NOEMBED=1 EXTRA_LDFLAGS=-s EXTRA_CFLAGS='-DFRUA_RELEASE -DFRUA_VERSION=\"$(VERSION)\"'
	$(MAKE) MACHINE=amiga strip-target
	$(call PKG_DIST,openua-amiga-$(VERSION),frua,Amiga AGA / RTG,Needs: an AA machine (A1200/A4000) or an accelerated ECS Amiga with a graphics card such as Picasso96 or CyberGraphX. KS3.0+ and about 4MB. AGA vs RTG is chosen at runtime.)

release-amiga-ecs:
	$(MAKE) test
	$(MAKE) clean
	$(MAKE) installer-amiga
	$(MAKE) MACHINE=amiga CPU68K=68000 NOEMBED=1 EXTRA_LDFLAGS=-s EXTRA_CFLAGS='-DFRUA_RELEASE -DFRUA_FORCE_ECS -DFRUA_VERSION=\"$(VERSION)\"'
	$(MAKE) MACHINE=amiga strip-target
	$(call PKG_DIST,openua-amiga-ecs-$(VERSION),frua,Amiga ECS/OCS 32-colour,Needs: an ECS or OCS Amiga (A500+/A600/A2000/A3000) with KS2.0+ and 2MB. Native 32-colour bitplanes for machines with no AGA and no graphics card.)

# Atari ST/STE: a bare-68000 build (CPU68K=68000). The 68000 codegen runs on
# EVERY Atari (ST/STE via ST-low 16-colour, TT via TT-low, Falcon via VIDEL),
# detection picks the backend — so this .prg is the "runs on any Atari" binary.
release-ste:
	$(MAKE) test
	$(MAKE) clean
	$(MAKE) installer
	$(MAKE) CPU68K=68000 NOEMBED=1 EXTRA_CFLAGS='-DFRUA_RELEASE -DFRUA_VERSION=\"$(VERSION)\"'
	$(MAKE) CPU68K=68000 strip-target
	$(call PKG_DIST,openua-atari-st-$(VERSION),frua.prg,Atari ST/STE 16-colour,Needs: an ST or STE (or Mega ST/STE) with 2MB and TOS 2.06 or EmuTOS. ST-low 16-colour native bitplanes. This 68000 build also runs on the TT and Falcon (they pick their own higher-colour backend) so it is the run-on-anything Atari binary.)

release-all:
	$(RM) -r dist
	$(MAKE) release
	$(MAKE) release-ste
	$(MAKE) release-amiga
	$(MAKE) release-amiga-ecs
	@echo "all releases staged under dist/:" && ls dist/*.zip

-include $(DEP)

.PHONY: installer installer-amiga all run run-ste run-mono run-game gamedata probe fc-audit cg-audit test test-slow clean distclean data-pool-regen release release-ste release-amiga release-amiga-ecs release-all strip-target
