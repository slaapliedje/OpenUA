---
name: run-falcon-port
description: Build, run, screenshot, and drive the FRUA Atari Falcon030/TT030 port (frua.prg) in the Hatari emulator. Use when asked to run, build, boot, screenshot, or interact with the game/engine, or run its test suite.
---

This repo cross-compiles `frua.prg` (a Forgotten Realms: Unlimited Adventures
port) for the Atari Falcon030/TT030 and runs it inside the **Hatari** emulator.
There is no native Linux binary — you drive it through Hatari under a virtual X
server. The handle is **`.claude/skills/run-falcon-port/driver.sh`**: it builds,
boots Hatari under Xvfb, screenshots the Falcon display, and sends keystrokes.
It is a thin headless wrapper over the project's real harness,
`tools/hatari_ui.sh`.

All paths below are relative to the repo root.

## Prerequisites

This box is **Linux Mint 22.2 (Ubuntu 24.04 base) → `apt`**. (An earlier revision
of this skill said Arch/`pacman`; that was wrong for this machine.)

```bash
sudo apt install hatari xvfb imagemagick x11-utils xdotool python3
```

- **Falcon TOS ROM** ships with the `hatari` package at
  `/usr/share/hatari/TOSv4.04.img` (the Makefile/harness default).
- **Cross toolchain** — the hard prerequisite. `make` needs the private
  `m68k-atari-mint` GCC with an **added soft-float `m68020-60` multilib**
  (the FPU-less Falcon030 target). It is NOT a stock package; build it per
  `docs/toolchain-softfloat-020.md` into `~/opt/cross-mint` (the Makefile's
  `TOOLROOT` default). Verify it resolves: `m68k-atari-mint-gcc --version`.
- **Game data** is copyrighted and git-ignored (`data/`). It is staged at
  `data/work/gamedata` here. `run`/`screenshot` need it; without it the engine
  boots "with no resources" (a near-empty screen). Staging is documented in
  `docs/mac-release.md` (`make gamedata DSN=HEIRS.DSN`).

## Newer Hatari (2.6.1) — already built at `~/opt/hatari`

Ubuntu/Mint ship **2.4.1**. A 2.6.1 build lives at `~/opt/hatari/bin/hatari` and
`driver.sh` picks it up automatically (override with `HATARI_BIN`). Nothing to do
unless it's missing.

If you need to rebuild it: Debian's own 2.6.1 `.deb` will NOT run here (it wants
glibc 2.42; Mint has 2.39), so build from source — and `sudo` needs a password on
this box, so get the dev headers WITHOUT root by unpacking the debs into a
sysroot:

```bash
apt-get download libsdl2-dev zlib1g-dev libpng-dev   # works as a normal user
for d in *.deb; do dpkg-deb -x "$d" sysroot; done
# the dev .so symlinks point at runtime libs that live in the SYSTEM, not the
# sysroot — repoint the dangling ones or the link fails:
ln -sf /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0.3000.0 sysroot/usr/lib/x86_64-linux-gnu/libSDL2.so
ln -sf /usr/lib/x86_64-linux-gnu/libz.so.1.3            sysroot/usr/lib/x86_64-linux-gnu/libz.so

curl -LO https://github.com/hatari/hatari/archive/refs/tags/v2.6.1.tar.gz
# cmake is not installed either — Kitware's binary tarball needs no root.
cmake -S hatari-2.6.1 -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=$HOME/opt/hatari \
  -DCMAKE_LIBRARY_PATH=$PWD/sysroot/usr/lib/x86_64-linux-gnu \
  -DCMAKE_C_FLAGS="-I$PWD/sysroot/usr/include/x86_64-linux-gnu -I$PWD/sysroot/usr/include"
cmake --build build -j"$(nproc)" && cmake --install build
```

The `-I .../x86_64-linux-gnu` is load-bearing: Ubuntu's `SDL_config.h` is a shim
that includes `SDL2/_real_SDL_config.h` from the arch-specific include dir, which
GCC only searches by default under `/usr`.

## Build

```bash
make            # soft-float frua.prg + frua.rsc (runs on Falcon030 AND TT030)
# make FPU=1    # FPU-required TT030 variant (hard-float 68881)
```

Output: `frua.prg` (an `Atari ST M68K` executable). Confirm 020 codegen took:

```bash
m68k-atari-mint-objdump -d src/engine/boot.o | grep -cE 'muls\.l|bfextu|bfins'
# non-zero (e.g. ~1794) means -m68020-60 is in effect
```

## Run / screenshot (agent path) — USE THIS

`driver.sh` auto-starts a persistent Xvfb on `:99` when no usable `DISPLAY`
exists, then delegates to `tools/hatari_ui.sh`. One-shot build→boot→screenshot:

```bash
.claude/skills/run-falcon-port/driver.sh smoke /tmp/frua.png
# -> builds, boots, waits for "menu: modal up", saves a STABLE frame, stops
```

Or step by step (each call is a separate invocation; Hatari persists between
them via state in `/tmp/frua-ui`):

```bash
D=.claude/skills/run-falcon-port/driver.sh
"$D" start                 # boot; returns when "menu: modal up" (~12s here)
"$D" dump /tmp/frua.png    # PREFERRED screenshot: Hatari's OWN screendump (clean,
                           #   cropped, no X grab; robust in fullscreen). See below.
"$D" shots /tmp/frua.png   # X-grab screenshot (waits for the frame to settle)
"$D" key e                 # send keystrokes — MENU HAS LETTER ACCELERATORS (e=Edit
                           #   Modules, p=Play); inside dialogs n=NEXT, p=PREV
"$D" wait 'regex' [n]      # block until the conout log has >= n matches
"$D" log                   # dump the conout log (engine printf / dbg_log)
"$D" dbg '<cmd>'           # run a Hatari-debugger command (see below)
"$D" quit                  # GRACEFUL shutdown (finalizes an open AVI recording)
"$D" stop                  # kill Hatari (leaves Xvfb up for reuse)
```

## Sound (agent path) — how to check audio without ears

A screenshot can't tell you whether the DMA path is silent, so `driver.sh sound`
records the emulator's audio and reports it:

```bash
"$D" sound /tmp/frua-sound.wav     # ~60s: build+boot+record+quit+analyze
```

It builds the **`FRUA_SNDTEST`** harness (`src/engine/boot.c` — fires four sfx
through `jt52` right after boot, and logs the two gates `-806` / `-17444` so a
silent run says WHICH gate closed), boots with `--avirecord`, quits Hatari
gracefully, and runs `tools/avi_audio.py` to print every burst of sound:

```
4 burst(s) of sound:
  #1  start  33.33s  dur  0.98s  peak  7872  ~ 386 Hz dominant
  ...
```

Four bursts = the sfx path is live end-to-end. `NONE — the DMA path produced
silence` (exit 1) = it is not. Afterwards run plain `make` to get the stock
binary back (the harness build is left in place).

Sound gotchas, all of which cost real time to find:

- **Quit Hatari, don't kill it.** An `--avirecord` AVI is finalized on close;
  `stop` SIGKILLs, leaving RIFF sizes at 0 and the file unparseable. Use `quit`
  (needs `--confirm-quit off`, which `sound` passes).
- **Record at real speed** (`--fast-forward no`), or the samples are dropped.
- **Hatari writes UNPADDED AVI chunks**, against the spec — a spec-conformant
  parser desyncs a byte on the first odd-sized chunk and finds no audio at all.
  `tools/avi_audio.py` resyncs; don't hand this AVI to a strict reader.
- `"main: sound chip locked"` in the log is **success** — it's the engine
  reporting `Locksnd()` worked, not an error.

**Debugger (`dbg`).** Stock Hatari 2.4.1+ speaks the `hatari-debug` protocol
over the cmd-fifo — no fork needed. Boot with `HATARI_BIN=hrdb "$D" start` to
auto-load frua.prg's symbol table, then reference engine names directly:

```bash
HATARI_BIN=hrdb "$D" start          # boot + `symbols prg` (engine names resolve)
"$D" dbg 'd _l28d4'                  # disassemble a function by name
"$D" dbg 'm _jt272'                  # dump memory at a symbol
"$D" dbg 'e _g_a5_below+12288'       # evaluate expr -> address (A5 globals);
                                     #   then `m $<hex>` to dump it
"$D" dbg 'r'                         # CPU registers + next instruction
"$D" dbg 'b pc=_jt272'               # set a conditional breakpoint (halts on hit)
"$D" dbg 'b'                         # list breakpoints
```

Syntax notes (stock Hatari): `m`/`d` take a bare symbol or hex — NOT `sym+off`
arithmetic; use `e sym+off` to get the address, then `m $hex`. `b` alone lists
breakpoints; `b pc=_sym` sets one. Output is captured from the stdout log. To
catch a crash PC, boot with `HATARI_ARGS="--debug"` so a CPU exception drops
into the debugger, then `dbg 'r'`. (Avoid `--trace cpu_exception` — it floods
on every TOS TRAP and makes boot impractically slow.)

Then view the PNG with the Read tool. A correct boot looks like the main menu:
title "UNLIMITED ADVENTURES", "CURRENT GAME DESIGN: HEIRS.DSN", and a 2-column
button grid (PLAY THE GAME … QUIT FROM GAME), over a Hatari status bar reading
"14MB Falcon, TOS 4.04" (the dev default; `FRUA_MEM=4` or `1` selects the
memory-fit configurations — the shipping floor is 4MB today, 1MB the goal).

- **Prefer `dump` over `shots`/`shot`.** `dump` triggers Hatari's built-in
  screendump over the control FIFO (`hatari-shortcut screenshot`; the launch sets
  `--screenshot-dir $STATE/shots --crop yes`). It needs no X window grab or
  imagemagick, gives a clean statusbar-cropped 688x490 PNG, and works in
  fullscreen (where the X-grab breaks). `shots` (the X-grab) still works and is
  the fallback. Both wait for a fresh frame.
- Env: `GEMDOS_DIR` (C: mount, default `data/work/gamedata`), `FALCON_TOS`,
  `FRUA_XVFB_DISPLAY` (default `:99`), or set `DISPLAY` to reuse a real X server.

## Run the TT030 variant (same binary, TT-low letterbox)

The same `frua.prg` runs on an emulated TT (the `_VDO` cookie picks the
TT-shifter backend: 320x480 TT low, the 320x200 frame line-doubled into a
40-line letterbox). Boot it through this same driver with overrides —
`HATARI_ARGS` is applied last, so it overrides `--machine`:

```bash
export FALCON_TOS=~/opt/etos512us.img          # EmuTOS 512k (TT-capable)
export HATARI_ARGS="--machine tt --dsp none"
"$D" start                                     # "menu: modal up" as usual
```

TT-specific input gotcha: Hatari's absolute click targeting desyncs from the
engine (engine clamps its pointer to 320x200; Hatari's model spans 320x480).
Re-sync BEFORE every targeted click with two dead-zone clicks, then click at
**(2*engine_x, engine_y)** window coordinates (x doubled, y RAW engine, not
the on-screen 2*y+40):

```bash
"$D" click 638 478 && sleep 1     # far corner: both models clamp
"$D" click 1 1     && sleep 1     # origin: both models at (0,0)
"$D" click 460 179                # = engine (230,179), BEGIN ADVENTURING
```

Keys need no translation. Sound is intentionally silent on TT (the Falcon
CODEC backend gates itself off; the STE-DMA ring backend is the open item).

## Run (human path)

On a machine with a real display, the Makefile boots Hatari in a window:

```bash
make run                       # boot frua.prg, repo root mounted as C:
make run-game DSN=HEIRS.DSN    # stage game data + boot (HEIRS sample module)
```

Useless headless (a window opens and waits) — use the driver instead.

## Test

```bash
make test        # host-side pytest over tools/ (fast; 129 passed, 1 skipped here)
make test-slow   # boots frua.prg in Hatari + asserts the frame renders (~12s; needs DISPLAY)
```

`make test-slow` needs `DISPLAY` — under headless, run it after the driver has
brought up Xvfb, e.g. `DISPLAY=:99 make test-slow`.

## Gotchas

- **NEVER `pkill -f hatari`.** `-f` matches the full command line, and your
  launching shell contains the word "hatari" — you kill your own shell and every
  command silently exits 1. Use `pkill -9 -x hatari` (exact name). The driver's
  `stop` does this for you.
- **A SIGKILL'd driver orphans Hatari.** `start` blocks until the menu is up; if
  your *outer* command times out mid-`start`, the disowned Hatari keeps running
  and holds the C: mount, stalling the next boot. Give `start` ≥150s of headroom,
  and `pkill -9 -x hatari` before retrying if a boot hangs.
- **Drive the main menu with KEYBOARD ACCELERATORS — `driver.sh key <letter>`,
  no mouse needed.** Each menu button has a letter accelerator (first letter of
  the item): `key e` = EDIT MODULES, `key p` = PLAY THE GAME, etc. Inside
  dialogs the nav buttons take accels too (e.g. `key n` = NEXT page, `key p` =
  PREV). This is the RELIABLE way to reach the design editor and other "mouse-
  only"-looking screens. Verified: `key e` opens the record editor, `key n`
  pages it. Prefer this over the mouse.
- **Synthetic MOUSE clicks are UNRELIABLE here (use the keyboard accels above).**
  Tried both windowed and fullscreen (`HATARI_ARGS="-f"`). Windowed: teleport
  clicks don't move the ST cursor; a gradual step-in makes motion register but it
  drifts (relative-mode). Fullscreen + `hatari-shortcut mousegrab` (over the FIFO)
  DOES move the FRUA cursor via `xdotool mousemove_relative`, but button clicks
  still don't land on FRUA UI — Hatari's ST-mouse position (a fixed centre
  crosshair in grab mode) desyncs from FRUA's drawn cursor, so the click hits a
  dead zone. Net: motion is drivable, precise clicking is not. Use keyboard
  accels; they cover the menu + editor. (Only two FIFO shortcuts exist:
  `mousegrab`, `screenshot`.)
- **`shots` prints a harmless `[[: arithmetic syntax error` line** under Xvfb
  (the `magick compare` AE metric trips `set -o pipefail` when frames differ).
  It self-recovers and still saves a correct settled frame — ignore the stderr.
- **★★ `shots` CANNOT see a transient message — it skips them by construction.**
  FRUA prints messages ("BORIS CANNOT DO MAGIC", "EMPTY!", "PIOUS BUYS 20
  ARROWS") on row 24, dwells ~1s (`l4bac` -> `jt476`), then repaints. `shots`
  waits for the frame to **settle**, so it grabs the screen *after* the message
  is gone — and the command bar it finds there looks like nothing ever happened.
  This cost months: "CAST/INV/shop messages are invisible" was carried as a
  known P1 bug that **never existed**. For anything transient use **`shot`**
  (immediate) ~1s after the keypress, and build `-DFRUA_MSGTRACE` to log the
  exact string and the dwell. `shots` remains correct for STATIC screens.
- **`SDL_VIDEODRIVER=x11`** is load-bearing for screenshots; the harness sets it.
- Boot speed varies with host load (~12s here at `--fast-forward`); the
  "Your system is too slow … sound samples" warning is cosmetic.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `make` fails: command not found / wrong multilib | Cross toolchain missing or stock. Build the soft-float `m68020-60` toolchain per `docs/toolchain-softfloat-020.md`; check `m68k-atari-mint-gcc --version`. |
| boot hangs, never logs "menu: modal up" | Stale Hatari holding C:. `pkill -9 -x hatari`, then retry `start`. |
| `no Hatari window found` / empty (~358 B) PNG | Xvfb not up or grab too early. Driver auto-starts Xvfb on `:99`; re-run `shots` (it retries). |
| screenshot is a half-drawn/black play screen | Use `shots`, not `shot`; it waits for the frame to settle. |
| a **transient message** never appears ("CAST does nothing") | **You used `shots`.** It waits for the frame to SETTLE, so it skips transient text BY CONSTRUCTION. Use **`shot`** (immediate) within ~1s of the keypress. See the Gotcha below. |
| `make run-game` says gamedata not found | Copyrighted `data/frua-mac/joined` isn't unpacked. See `docs/mac-release.md`. |
