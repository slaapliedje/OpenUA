---
name: run-amiga-port
description: Build, run, screenshot, and drive the Amiga AGA port (frua) in the amiberry emulator — make MACHINE=amiga, boot an emulated A1200, send keys and mouse clicks, capture the screen, verify sound. Use for any "run/test/screenshot the Amiga port" request.
---

This repo cross-compiles `frua` (an AmigaOS hunk executable) for Amiga AGA
(A1200) and runs it in the **amiberry** emulator (flatpak). The handle is
**`.claude/skills/run-amiga-port/driver.sh`** — it builds, boots, waits for
the engine's own boot log, screenshots, and injects input with the timing
quirks amiberry needs. Paths below are relative to the repo root.

## Prerequisites (all verified present here)

- **Bebbo m68k-amigaos-gcc** at `~/opt/amiga` — NOT a stock package; build per
  `docs/toolchain-amiga.md`. Verify: `~/opt/amiga/bin/m68k-amigaos-gcc --version`.
- **amiberry** flatpak: `flatpak run com.blitterstudio.amiberry` resolves.
- **Kickstart 3.2** at `~/Amiberry/ROMs/kicka1200.rom` and the machine config
  `~/Amiberry/Configurations/openua.uae` (A1200, AGA, 2MB chip + 8MB fast,
  `filesystem2` mounting `data/work/amiga-mount` as DH0, `sound_output=exact`).
- **Game data staged** in `data/work/amiga-mount/` (copyrighted, git-ignored):
  the FRUA gamedata + `S/startup-sequence` (`stack 65536` / `cd DH0:` / `frua`).
- Host tools: `xdotool`, `xwininfo`, ImageMagick `import`, `parec` (pulse).
- **A desktop display on `:0`** — the amiberry window opens there, NOT on an
  Xvfb (override with `FRUA_AMIGA_DISPLAY` if your desktop differs).

## Run / drive (agent path)

```bash
D=.claude/skills/run-amiga-port/driver.sh
"$D" smoke /tmp/amiga-menu.png   # build -> boot -> menu screenshot -> stop
```

Or step by step (state persists in `/tmp/frua-amiga` between invocations):

```bash
"$D" build                 # make MACHINE=amiga + stage into the mount
"$D" start                 # boot; returns at "menu: modal up" (~40-60s);
                           #   captures the mouse, pointer tracked at (160,100)
"$D" shot /tmp/a.png       # screenshot the amiberry window
"$D" click 75 133          # move the EMULATED pointer to lores (x,y) + click
"$D" move -10 5            # relative pointer move (updates the tracked pos)
"$D" key Down              # one keysym per key; repeats fine: key Down Down
"$D" wait 'regex' [n]      # block until DBG.LOG has >= n matches
"$D" log                   # dump the engine's DBG.LOG boot/debug trail
"$D" sound /tmp/a.wav      # SNDTEST build + boot + 55s host-audio capture;
                           #   exits 1 if (near-)silent; restores normal build
"$D" stop                  # kill amiberry
```

A correct boot shows the UNLIMITED ADVENTURES main menu ("CURRENT GAME
DESIGN: HEIRS.DSN", 2-column button grid) with the blue shield pointer at
centre. Menu lores coords: left button column x≈75 — PLAY THE GAME y≈120,
SELECT A DESIGN y≈133, CREATE NEW DESIGN y≈146; right column x≈230 — QUIT
FROM GAME y≈185.

## Test

`make test` (host pytest) covers the Amiga c2p transpose
(`tests/test_c2p_amiga.py`). `make MACHINE=amiga CPU68K=68000` must also link
(the ECS/68000 policy check).

## Gotchas (all hit live — do not rediscover them)

- **The emulated mouse is delta-only.** amiberry translates host motion into
  JOY0DAT deltas after a capture click (`start` does it). `click x y` works
  from a TRACKED position — accurate right after `start`; if the game warps
  its own pointer or you alt-tab, the tracking is stale. Re-anchor by
  restarting, or navigate by `move` + screenshots.
- **Instant synthetic clicks are invisible.** The game samples the button
  from CIA PRA at 50Hz; `xdotool click` presses for microseconds. The
  driver's `click` holds the button 0.3s. Never bypass it.
- **One key per xdotool invocation**, window activated first. Two keysyms in
  one `xdotool key` call lose one. The driver paces them 0.4s apart.
- **`xdotool search --name amiberry` finds nothing** — the driver uses
  `xwininfo -root -tree` instead.
- **A silent sound capture may be the EMULATOR config**: `sound_output=none`
  in openua.uae disables Paula output entirely. The driver's `sound` refuses
  to run in that case.
- **Never `pkill -f amiberry`** (`-f` can match your own shell); the driver
  uses `pkill -x amiberry`.
- **The picker's design list takes several seconds to build** (it loads each
  design's GAME header); keys typed during the build are deliberately
  drained by the engine. Wait for the list to render before sending arrows.
- Machine-switch staleness is handled: the Makefile's `.machine` stamp purges
  objects when MACHINE changes — no manual `make clean`.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `start` times out, no "menu: modal up" | Stale amiberry holding the mount: `pkill -x amiberry`, retry. Check `tail /tmp/frua-amiga/amiberry.log` and `data/work/amiga-mount/DBG.LOG` for where boot stopped. |
| "Insufficient FAR Memory!" in DBG.LOG | The Bebbo shift-miscompiler workaround was dropped — see `toolchain/m68k-amigaos.mk` (`-fbbb`) and `docs/toolchain-amiga.md`. |
| no amiberry window on :0 | The desktop session isn't on `:0` — set `FRUA_AMIGA_DISPLAY`. The flatpak needs X11 (`SDL_VIDEODRIVER=x11` is set by the driver). |
| clicks land on the wrong control | Tracked position drifted. `stop` + `start` re-anchors at (160,100). |
| `sound` exits 1 with "0s loud" | Config `sound_output` (driver checks `none`, but also verify `exact`), or pulse default sink changed — the driver records the CURRENT default sink's monitor. |
