# Driving a retro emulator headlessly

Notes from building a test harness that boots a 68k game binary in Hatari
(Atari ST/STE/TT/Falcon) and amiberry (Amiga), drives it with no display
attached, and compares frames — so that rendering work can be regression-tested
the way normal code is.

The emulators are not designed for this. None of them has a "run my program,
send these keys, give me a PNG" mode. But all the pieces exist, and once
assembled the payoff is large: **you can prove a rendering change is a no-op by
diffing screenshots**, which is otherwise nearly impossible to establish for a
graphics rewrite.

Written up because I could not find it written down anywhere.

---

## The shape

One shell script — call it `driver.sh` — with verbs, and persistent state
between invocations so an agent or a CI job can drive it step by step:

```
driver.sh build              build + stage the binary where the emulator sees it
driver.sh start              boot; BLOCK until the program says it is ready
driver.sh shot   out.png     screenshot
driver.sh shots  out.png     screenshot, waiting for the frame to stop changing
driver.sh key    Down Return send keystrokes
driver.sh click  x y         click at emulated-screen coordinates
driver.sh drag   x1 y1 x2 y2 press, move, release (for pulldown menus)
driver.sh wait   'regex' [n] block until the program's log matches n times
driver.sh log                dump that log
driver.sh stop               kill the emulator
```

Four pieces make it work:

**1. A virtual display.** `Xvfb` on a spare display number. Start it once and
reuse; the emulator opens a real X window inside it, which is what makes
screenshots possible at all.

**2. A filesystem the guest and host share.** Both emulators can mount a host
directory as a guest drive (Hatari GEMDOS emulation; amiberry `filesystem2`).
This is the whole communication channel: the guest writes a log file, the host
tails it. No serial port, no debugger protocol.

**3. A readiness marker.** Do not sleep and hope. Have the program print
something specific when it is genuinely up (`menu: modal up`), and have `start`
block until that appears in the shared-directory log. Boot times vary by 3× with
host load; a fixed sleep is a flake generator.

**4. Screenshot by grabbing the emulator's window.** `xwininfo -root -tree` to
find it, ImageMagick to capture. Note `xdotool search --name` frequently does
*not* find these windows — search the tree instead.

## The technique that makes it worth building

**Frame diffing as a regression test.** This is the point of the whole exercise.

When you rewrite a rendering path and believe it changes nothing visible, you
can now prove it:

1. Capture reference frames on the old build (menu, in-game, whatever exercises
   the path).
2. Make the change.
3. Capture the same frames via the identical key sequence.
4. `md5sum` them.

Byte-identical output is a far stronger claim than "it looked fine". We used
this to land a bitplane-copy rewrite and a blitter path with confidence, and it
caught nothing — which was the useful result, because the alternative was
shipping on a hunch.

For paths that *do* change (animation, a scrolling view), compare *consecutive*
frames pairwise with the changing region masked out, and assert the surrounding
chrome is stable. That catches "my viewport change also corrupted the HUD".

**Verify by state, not by input.** The single most valuable rule here. See below.

## Traps

Every one of these cost real time.

### Silence is not success

The recurring failure mode, in three flavours:

- **A build flag that never compiled in.** Our diagnostic flag was missing from
  the Makefile's dependency stamp, so `make FLAG=1` after a normal build said
  "nothing to be done" and produced a binary with no instrumentation. The empty
  log read exactly like "that code path didn't execute". *Check the flag is in
  the binary* — `strings`, or count the symbol in the objdump — before drawing
  conclusions from what a diagnostic doesn't say.
- **A dump gated on a window that never elapses.** One attribution report fired
  every 64 frames; scripted runs produce under 20. It had never once emitted
  output, and nobody noticed, because absence looked normal.
- **A check that passed because the file was missing.** An integrity script
  looked for a binary at the wrong path inside an archive; `objdump` on a
  nonexistent file reports zero of everything, which is indistinguishable from
  a clean result. Assert the path exists before believing a zero.

### Verify by state, not by key count

A scripted walk logged 24/24 keys delivered while the character never moved: the
keys were going into a modal dialog, which discards them. The log was perfectly
truthful and completely misleading.

Verify with something the *program* changed as a result — an in-game clock that
advances one minute per step, a position readout, a counter. Two full test runs
were wasted before this rule was learned. If your program has no observable
state for the thing you are testing, add one.

### Input quirks

- **Instant synthetic clicks are invisible.** The guest samples the mouse button
  at 50 Hz; `xdotool click` presses for microseconds. Hold for 100–300 ms.
- **One key per invocation.** Two keysyms in one `xdotool key` call reliably
  lose one. Pace them ~0.4 s apart.
- **The first key after a screen transition is often eaten.** Many programs
  deliberately drain the keyboard buffer while loading. Send a throwaway.
- **Emulated mice may be delta-only.** amiberry translates host motion into
  joystick-port deltas after a capture click, so there is no absolute
  positioning: you track the pointer yourself and it goes stale the moment the
  guest warps its own cursor. Hatari needs mouse-warp *disabled* for absolute
  clicks to land.
- **Pointer acceleration is non-linear** under some guest OSes (observed
  0.2–1.1 screen pixels per host pixel, direction-dependent, in the same
  session). Slam to a corner, then step in small increments.

### Menus are their own problem

Classic Mac-style pulldowns commit on **mouse release inside the item** — a
click on an already-open menu does nothing but move the pointer. You need a
single press-move-release gesture.

Which collides with: **you cannot screenshot while a button is held** (it
deadlocked our virtual display). So you cannot look at the open menu to find the
item you want.

The way out: deliberately release on a *separator*. Nothing is selected, the
menu stays open, and now you can screenshot it and read off the coordinates for
next time. Write them down — that table is harness infrastructure.

Also: `Escape` in a nested UI may close far more than you intended. Ours exited
the entire editor rather than the open menu.

### Getting to the interesting screen

Real content is often unreachable by accident of its own design. Three of the
four taverns in our test module had no map square pointing at them; the only
reachable one sat behind a mountain range on a terrain-gated overland map.

**Author the situation instead.** We generate small purpose-built data files —
a bare room with exactly one event on the square the party starts on, so the
handler runs before a single key is pressed. This removes navigation, timing
and modals from the test in one move. If your program consumes data files, a
generator for minimal ones is the highest-leverage harness component after the
driver itself.

Corollary: make the generated situation *event-free* except for the thing under
test. Ours originally had events on the entry square, which swallowed the walk
keys — see "verify by state" above.

### Screenshot timing

Grab a frame while the guest is mid-redraw and you catch a half-drawn screen.
Take two shots a short interval apart and compare; only return when they match.
Anything doing a slow full-screen repaint needs this or your test is flaky in a
way that looks like a real bug.

### Housekeeping

- **Never `pkill -f <emulator>`.** `-f` matches the full command line, and your
  own launching shell contains the emulator's name — you kill your own shell and
  every subsequent command silently exits non-zero. Use `pkill -x`.
- **A killed driver orphans the emulator**, which keeps holding the shared
  directory and stalls the next boot. Give the blocking `start` generous
  headroom, and kill leftovers before retrying.
- **Unset any inherited `DISPLAY`.** Ours defaulted to the developer's real
  desktop, so a test run opened a window on their screen and typed into it.
- **Emulator timing models are not hardware.** A blitter benchmark came out
  ~45% above what the real chip can sustain. Trust *ratios* between shapes;
  treat absolute throughput as an upper bound and say so.

## What it buys

With this in place a rendering change becomes an ordinary, testable change:
build both versions, drive the same sequence, diff the frames. Beyond that, the
same harness runs a profiler build and returns numbers, drives the program's own
editors and dialogs, and reaches screens that had never been exercised in the
project's history — several of which turned out to work fine and had simply
never been looked at.

The build is maybe a day. It repaid that within a week and has repaid it many
times since.
