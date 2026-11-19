# retroplat -- the portable platform seam, and one backend per vintage
# machine. Extracted from RetroWP (~/dev/retrowp) per its
# docs/platform-extraction.md.
#
# There is nothing to link here. A platform layer has no main(), so the
# only thing worth asserting about it standalone is that it COMPILES
# against nothing but its own headers -- which is exactly the property
# that makes it shareable. An application dependency creeping back in is
# invisible inside a consuming app's build, where that app's headers are
# already on the include path; it is a hard error here.
#
#   make check          seam + host backend, native
#   make check-atari    m68k-atari-mint-gcc
#   make check-gem4xe   Calypsi cc65816 + gem4xe's kit (Atari 8-bit)
#   make check-amiga    m68k-amigaos-gcc
#   make check-mac      Retro68's m68k-apple-macos-gcc
#   make check-dos      Open Watcom wcc
#   make check-iigs     ORCA/C via Golden Gate (portable half only, so far)
#   make check-all      every toolchain that is installed
#
# Always recompiles: this is a gate, and a gate that can be stale is not
# a gate. It is a couple of seconds over ~6,600 lines.

BUILD = build

CC      ?= cc
ATARICC ?= m68k-atari-mint-gcc

AMIGA_TOOLROOT ?= $(HOME)/opt/amiga
AMIGACC        ?= $(AMIGA_TOOLROOT)/bin/m68k-amigaos-gcc
AMIGA_CPU      ?= -m68020

MAC_TOOLROOT ?= $(HOME)/opt/Retro68-build/toolchain
MACCC        ?= $(MAC_TOOLROOT)/bin/m68k-apple-macos-gcc

DOS_TOOLROOT ?= $(HOME)/opt/watcom
DOSCC        ?= $(DOS_TOOLROOT)/binl64/wcc
DOS_ENV       = WATCOM=$(DOS_TOOLROOT) PATH=$(DOS_TOOLROOT)/binl64:$$PATH

# Relaxed rather than strict, and deliberately so: the native GEM, MUI,
# Toolbox and BIOS headers these backends include are not -pedantic-clean,
# and that is not this gate's business. The gate is the include path.
CFLAGS_HOST  = -Wall -Wextra -O2
CFLAGS_ATARI = -Wall -Wextra -O2
CFLAGS_AMIGA = -Wall -Wextra $(AMIGA_CPU)
CFLAGS_MAC   = -Wall -Wextra -O2
CFLAGS_DOS   = -0 -ml -bt=dos -wx

# The portable half: no platform calls, no application, nothing to stub.
PORTABLE_SRC = src/endian.c src/utf8.c src/fontpick.c src/monobits.c src/polyfill.c src/papermetrics.c src/papermetrics_tables.c

# Atari 8-bit, through gem4xe -- GEM for a 65816-accelerated XL/XE. The
# ST's VDI-facing files (dialog, draw, metrics) compile unchanged against
# gem4xe's GEM; the rest of the ST backend speaks GEMDOS/BIOS directly, so
# those five have *_gem4xe.c siblings.
CALYPSI     ?= $(HOME)/dev/toolchains/calypsi-65816
G4XCC       ?= $(CALYPSI)/bin/cc65816
GEM4XE_SDK  ?= $(HOME)/dev/gem4xe/build/gem4xe-sdk
CFLAGS_G4X   = --code-model=large --data-model=large -O2
GEM4XE_ONLY  = $(wildcard backends/atari/*_gem4xe.c)
GEM4XE_SRC   = $(PORTABLE_SRC) backends/atari/dialog_atari.c \
               backends/atari/draw_atari.c backends/atari/metrics_atari.c \
               $(GEM4XE_ONLY)

# Apple IIGS. ORCA/C through Golden Gate's iix; on Linux, its Windows
# build under Wine, wrapped by a script with an iix-clean beside it.
IIX ?= $(HOME)/dev/gem4xe/ref/iix

.PHONY: check check-atari check-gem4xe check-amiga check-mac check-dos check-iigs check-qt check-all clean

check:
	@mkdir -p $(BUILD)/host
	@for f in $(PORTABLE_SRC) backends/host/*.c; do \
		$(CC) $(CFLAGS_HOST) -Iinclude -Ibackends/host \
			-c $$f -o $(BUILD)/host/`basename $$f .c`.o || exit 1; \
	done
	@echo "check: seam + host backend"
	@$(CC) $(CFLAGS_HOST) -std=c89 -pedantic -Iinclude -Ibackends/host -Itests \
		-c tests/conformance.c -o $(BUILD)/host/conformance.o
	@$(CC) $(CFLAGS_HOST) -Iinclude -Ibackends/host -Itests \
		-c tests/host_conformance.c -o $(BUILD)/host/host_conformance.o
	@$(CC) $(BUILD)/host/*.o -o $(BUILD)/host/conformance
	@cd $(BUILD)/host && ./conformance

check-atari:
	@mkdir -p $(BUILD)/atari
	@for f in $(PORTABLE_SRC) backends/atari/*.c; do \
		$(ATARICC) $(CFLAGS_ATARI) -Iinclude -Ibackends/atari \
			-c $$f -o $(BUILD)/atari/`basename $$f .c`.o || exit 1; \
	done
	@echo "check-atari: GEM / VDI / TOS backend"

# Every *_gem4xe.c is wrapped in #ifdef GEM4XE_APP_GEM_H, which only
# gem4xe's gem.h defines -- so check-atari above compiles each one to an
# EMPTY object, and passing says nothing about them. This is their gate.
# The same guard is also how this one could pass while checking nothing:
# a kit that stopped defining the macro would empty every file without an
# error. So each *_gem4xe object must define at least one global function.
check-gem4xe:
	@mkdir -p $(BUILD)/gem4xe
	@for f in $(GEM4XE_SRC); do \
		$(G4XCC) $(CFLAGS_G4X) -Iinclude -Ibackends/atari -I$(GEM4XE_SDK)/include \
			$$f -o $(BUILD)/gem4xe/`basename $$f .c`.o || exit 1; \
	done
	@for f in $(GEM4XE_ONLY); do \
		o=$(BUILD)/gem4xe/`basename $$f .c`.o; \
		nm --defined-only $$o | grep -q ' T ' || { \
			echo "check-gem4xe: FAIL -- $$o defines nothing: is GEM4XE_APP_GEM_H still defined by gem.h?"; \
			exit 1; }; \
	done
	@echo "check-gem4xe: Atari 8-bit (gem4xe) backend, `echo $(GEM4XE_ONLY) | wc -w` gem4xe-only files define code"

check-amiga:
	@mkdir -p $(BUILD)/amiga
	@for f in $(PORTABLE_SRC) backends/amiga/*.c; do \
		$(AMIGACC) $(CFLAGS_AMIGA) -Iinclude -Ibackends/amiga \
			-c $$f -o $(BUILD)/amiga/`basename $$f .c`.o || exit 1; \
	done
	@echo "check-amiga: AmigaOS backend"

# The Qt backend: Cairo's job done by QPainter, Pango's by QFontMetrics.
# The first backend in this library for a machine that is still made, the
# only one in C++, and the only one that is a first-class target on Linux,
# Windows and macOS alike -- which is the whole reason it replaced a GTK3
# backend that was none of those things on two of the three.
#
# Its twips conversion is exact for the same reason the Mac's is: Qt sizes
# fonts in POINTS and a twip is 1/20 point. Both are built on the printer's
# point rather than on a screen pixel.
QTPKG ?= Qt5Widgets
check-qt:
	@mkdir -p $(BUILD)/qt
	@for f in backends/qt/*.cpp; do \
		$(CXX) -std=c++11 -Wall -Wextra -fPIC `pkg-config --cflags $(QTPKG)` \
			-Iinclude -Ibackends/qt -Ibackends/host \
			-c $$f -o $(BUILD)/qt/`basename $$f .cpp`.o || exit 1; \
	done
	@echo "check-qt: Qt5 + QPainter backend"

check-mac:
	@mkdir -p $(BUILD)/mac
	@for f in $(PORTABLE_SRC) backends/mac/*.c; do \
		$(MACCC) $(CFLAGS_MAC) -Iinclude -Ibackends/mac \
			-c $$f -o $(BUILD)/mac/`basename $$f .c`.o || exit 1; \
	done
	@echo "check-mac: Mac Classic Toolbox backend"

check-dos:
	@mkdir -p $(BUILD)/dos
	@for f in $(PORTABLE_SRC) backends/dos/*.c; do \
		env $(DOS_ENV) $(DOSCC) $(CFLAGS_DOS) -i=include -i=backends/dos \
			-i=$(DOS_TOOLROOT)/h \
			-fo=$(BUILD)/dos/`basename $$f .c`.o $$f || exit 1; \
	done
	@echo "check-dos: DOS text-mode backend"

# No IIGS backend yet: this gates the portable half under ORCA/C (16-bit
# int, 32-bit pointers) so backends/iigs/ is gated from its first file.
# tools/iigs/orca_check.sh says why it stages flat and runs a control.
check-iigs:
	@tools/iigs/orca_check.sh $(IIX) $(PORTABLE_SRC) $(wildcard backends/iigs/*.c)

# An absent cross-toolchain is not a failed gate.
check-all: check
	@command -v $(ATARICC) >/dev/null 2>&1 && $(MAKE) check-atari || echo "-- no Atari toolchain, skipped"
	@test -x $(G4XCC) && test -f $(GEM4XE_SDK)/include/gem.h && $(MAKE) check-gem4xe || echo "-- no Calypsi or gem4xe kit, skipped"
	@test -x $(AMIGACC) && $(MAKE) check-amiga || echo "-- no Amiga toolchain, skipped"
	@pkg-config --exists $(QTPKG) && $(MAKE) check-qt || echo "-- no Qt5, skipped"
	@test -x $(MACCC) && $(MAKE) check-mac || echo "-- no Mac toolchain, skipped"
	@test -x $(DOSCC) && $(MAKE) check-dos || echo "-- no DOS toolchain, skipped"
	@test -x $(IIX) && $(MAKE) check-iigs || echo "-- no ORCA/C (Golden Gate iix), skipped"

clean:
	rm -rf $(BUILD)
