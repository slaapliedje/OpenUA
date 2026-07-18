---
name: run-amiga-port
description: Build, run, screenshot, and drive the Amiga AGA port (frua) — and the uainst installer / Workbench — in the amiberry emulator. make MACHINE=amiga, boot an emulated A1200, send keys and mouse clicks, capture the screen, verify sound. Use for any "run/test/screenshot the Amiga port" or "run uainst on Amiga" request.
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
  the FRUA gamedata + `S/User-Startup` ending in `frua` (run from `S/Startup-
  sequence` before `LoadWB`). No `stack` command — frua gets its stack from a
  `__stack` global (small enough to survive the ~4 KB default; uainst needs
  more and self-manages via StackSwap).
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
"$D" start                 # boot frua; returns at "menu: modal up" (~40-60s);
                           #   captures the mouse, pointer tracked at (160,100)
"$D" boot [secs]           # RAW boot (no frua wait) for uainst / Workbench;
                           #   waits `secs` (default 48) then finds the window
"$D" grab                  # capture the emulated mouse (centre click -> 160,100)
"$D" shot /tmp/a.png       # screenshot the amiberry window
"$D" click 75 133          # move the EMULATED pointer to lores (x,y) + click
"$D" move -10 5            # relative pointer move (updates the tracked pos)
"$D" dclick                # best-effort double-click at the pointer (unreliable)
"$D" key Down              # one keysym per key; repeats fine: key Down Down
"$D" wait 'regex' [n]      # block until DBG.LOG has >= n matches
"$D" log                   # dump the engine's DBG.LOG boot/debug trail
"$D" sound /tmp/a.wav      # SNDTEST build + boot + 55s host-audio capture;
                           #   exits 1 if (near-)silent; restores normal build
"$D" stop                  # kill amiberry
```

`start` waits for frua's own `menu: modal up` marker, so it ONLY works when
the mount boots frua. To run anything else — the `uainst` installer, or a
plain Workbench — use `boot` (which just waits a fixed time) and `grab` the
mouse yourself.

A correct boot shows the UNLIMITED ADVENTURES main menu ("CURRENT GAME
DESIGN: HEIRS.DSN", 2-column button grid) with the blue shield pointer at
centre. Menu lores coords: left button column x≈75 — PLAY THE GAME y≈120,
SELECT A DESIGN y≈133, CREATE NEW DESIGN y≈146; right column x≈230 — QUIT
FROM GAME y≈185.

## Running something other than frua (uainst / Workbench)

The mount's `S/User-Startup` launches `frua`. To boot the **uainst** installer
or a plain **Workbench** instead, swap the startup, `boot` (raw), then restore.
Always back up and restore — the mount is a real WB3.2 system dir.

```bash
cd data/work/amiga-mount
cp S/User-Startup S/User-Startup.bak
# uainst with the ASL requesters (no args) — needs Workbench for a pubscreen,
# so run it AFTER LoadWB, not from User-Startup (which runs before LoadWB):
python3 - <<'PY'
p='S/Startup-sequence'; s=open(p).read()
open(p,'w').write(s.replace('LoadWB\nEndCLI >NIL:',
                            'LoadWB\nWait 3\nCD DH0:\nuainst\nEndCLI >NIL:'))
PY
printf 'DEVS:Monitors/uaegfx >SYS:MON.LOG\n' > S/User-Startup   # drop the frua line
cp ../../../uainst_amiga ./uainst                                # stage the binary
cp ../../../data/work/fanmods/curse.zip ./curse.zip             # a test module
# ...boot, drive, screenshot... then restore:
mv -f S/User-Startup.bak S/User-Startup
git checkout -- S/Startup-sequence 2>/dev/null || true          # if tracked
```

Prefer the **CLI-arg** path for verifying the extract/convert core — it is
deterministic and needs no mouse: set the startup to `uainst DH0:curse.zip
DH0:Dest` (dest omitted = install into the CWD). Use the **GUI** (no-arg) path
only to exercise the ASL requesters themselves. ASL-requester driving:

- Click a list row (or the File field) to **activate** the requester window,
  then type — otherwise keystrokes go to the boot console behind it. Typing an
  absolute path (`DH0:curse.zip`) into the File gadget and pressing **Return**
  resolves it.
- **Return confirms** a requester when a string gadget is active (the ZIP
  requester's File field). It does **not** confirm a DrawersOnly requester (the
  "install into which drawer?" one) — you must click its **Install here** button.
- uainst self-manages its stack (StackSwap, 256 KB), so it runs fine from a
  Shell or a double-clicked icon despite the ~4 KB default stack. It also ships
  a Workbench icon (`uainst.info`, fixed position 12,8) — `make installer-amiga`
  builds both.

Workbench navigation (opening a disk / drawer): single-click an icon's **label**
to select it (the label is the reliable hitbox — the small glyph is easy to
miss), confirm the highlight in a screenshot, then `dclick`.

## Testing the ECS build (native 32-colour, 68000)

The default config is AGA/68020. To boot the **ECS** release binary
(`release-amiga-ecs` — native ECS bitplanes on a plain 68000) you need a
different binary AND an ECS machine config:

```bash
make MACHINE=amiga CPU68K=68000 EXTRA_CFLAGS='-DFRUA_FORCE_ECS'   # the ECS binary
cp frua data/work/amiga-mount/frua                                # stage it
AMIBERRY_CONF=~/Amiberry/Configurations/openua-ecs.uae \
  .claude/skills/run-amiga-port/driver.sh boot 120                # boot (68000 is SLOW)
```

`openua-ecs.uae` is NOT in the repo (amiberry configs live under `~/Amiberry/`);
recreate it as an A600-class ECS/68000 machine on a **KS3.2** ROM (matches the
WB3.2 mount) with the same `filesystem2` DH0 line as `openua.uae`:

```
kickstart_rom_file=/home/jfergus/Amiberry/ROMs/CDTVA500A600A2000.47.115.rom
cpu_type=68000
cpu_model=68000
chipset=ecs
chipset_compatible=A600
chipmem_size=4          # 2 MB chip (ECS max)
fastmem_size=4          # 4 MB fast
cpu_speed=real
filesystem2=rw,DH0:OpenUA:/home/jfergus/dev/OpenUA/data/work/amiga-mount,1
```

- Verified boots to the main menu (2026-07-18). `DBG.LOG` shows the native
  path: `ecs: 320x200x5 32-colour, per-band copper palette up`.
- **Budget ~105 s to `menu: modal up`** (vs ~40 s for AGA/020) — the 7 MHz
  68000 spends most of it in frua.rsc + data-pool replay + STRS load. Use
  `boot 120` (not the frua-only `start`), then poll DBG.LOG for `menu: modal up`.
- Restore an AGA build (`make MACHINE=amiga && cp frua data/work/amiga-mount/`)
  when done, so the default `openua.uae` config works next time.

## Test

`make test` (host pytest) covers the Amiga c2p transpose
(`tests/test_c2p_amiga.py`). `make MACHINE=amiga CPU68K=68000` must also link
(the ECS/68000 policy check).

## Gotchas (all hit live — do not rediscover them)

- **The emulated mouse is delta-only.** amiberry translates host motion into
  JOY0DAT deltas after a capture click (`start`/`grab` do it). `click x y` works
  from a TRACKED position — accurate right after capture; if the game warps
  its own pointer or you alt-tab, the tracking is stale. Re-anchor by
  restarting, or navigate by `move` + screenshots.
- **The delta mapping is NON-LINEAR under Workbench/Intuition.** Inside the
  frua engine the 1:1 lores tracking holds, but on the WB screen amiberry's
  pointer acceleration makes big moves overshoot wildly (observed 0.2–1.1
  screen-px per host-px on the same session, direction-dependent). Do NOT trust
  a single large `move` to land on a gadget. Technique that works: slam to a
  corner first (`move -900 -900`), then step toward the target in SMALL moves
  (≤ ~60 host px), screenshotting between steps. Budget many iterations for a
  precise WB target (a button, a scroll arrow).
- **Double-clicks are the hardest gesture** and often just don't fire. amiberry
  needs a long button-hold to register a click at all (below), but WB's
  double-click wants two *quick* clicks — the two requirements fight. Most
  reliable: single-click the icon's LABEL to select it, verify the highlight in
  a screenshot, then `dclick`. Even then, retry. If you only need to prove an
  icon/tool is valid, a Shell/CLI launch is far cheaper than a WB double-click.
- **Instant synthetic clicks are invisible.** The button is sampled from CIA
  PRA at 50Hz; `xdotool click` presses for microseconds. `click`/`dclick` hold
  ~0.1–0.3s. Never bypass them with a bare `xdotool click`.
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
