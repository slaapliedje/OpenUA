---
name: run-aranym-nova
description: Boot OpenUA's NOVA graphics-card display path in ARAnyM (fVDI 8bpp 640x400, no hardware) — build, start, wait on dbg.log markers, screenshot, stop. Use to test/regress the Nova backend (detection, palettes, VDI plumbing, boot) without the real ATW800/2.
---

Boots the FALCON/020 `frua.prg` on ARAnyM with an fVDI 8bpp 640x400 screen,
where `video.cfg nova=force` makes dsp_detect bind the NOVA backend — the
whole card code path (AES/VDI probe, depth guard, LUT bind, chunky present,
palettes) runs with no hardware. Handle:
**`.claude/skills/run-aranym-nova/driver.sh`** (paths relative to repo root).

```bash
D=.claude/skills/run-aranym-nova/driver.sh
"$D" build       # stage repo frua.prg + video.cfg(nova=force) into the drive
"$D" start       # boot; returns at 'menu: modal up' (~60s, JIT)
"$D" log         # dump the game's dbg.log (THE observable — see limits)
"$D" wait 're' n # block until dbg.log matches
"$D" shot f.png  # root-window screenshot of the :96 display
"$D" stop        # pkill -x aranym
```

A correct boot's dbg.log opens with `nova: hardware LUT bound`, `nova: up
8bpp chunky, card w = 640`, and ends at `menu: modal up`.

## What this harness CAN and CANNOT verify

- **CAN**: the Nova detect path (vq_extnd planes==8, the >=640 TT-Low
  guard), nova_init/LUT bind, palette writes, the present/row-diff CODE
  paths, engine boot on the backend, anything that logs. This is the
  regression net for display_nova changes short of the real card.
- **CANNOT (fundamental)**: SEE the game's pixels. The ARAnyM fVDI driver
  renders host-side via NatFeats; the backend's `Logbase()` writes land in
  undisplayed RAM. The window shows the frozen GEM desktop while the game
  runs "behind" it. The LUT "bind" also succeeds against plain RAM — its
  read-back is not a hardware proof.
- **OPEN**: xdotool keys did not reach the game on first bring-up
  (activate/focus/click-first all tried). For deep testing use an
  FRUA_AUTOPLAY build (platform/autoplay_script.h) — it drives play with
  no input and dbg markers tell the story.
- Visible 640x400x256 output needs the real card — or the Hatari
  ET4000-emulation fork idea (donor implementations exist in DOSBox-X /
  86Box / PCem / MAME; see the `nova-graphics-card` memory).

## Setup / rebuild (state lives in /tmp/frua-aranym — REBOOT-MORTAL)

Durable pieces: `data/work/aranym/boot.st` (the boot floppy: BetaDOS +
hostfs.dos + fVDI.prg in AUTO, BDCONFIG.SYS `*DOS, \AUTO\HOSTFS.DOS, C:C`,
FVDI.SYS with `01r aranym.sys mode 640x400x8@72 assumenf irq accelerate`,
EMUDESK.INF `#Z 01 C:\FRUA.PRG@` autorun) and `aranym.cfg.template`
(EmuTOS ROM from ~/.aranym/emutos-aranym.img, JIT, [HOSTFS] C).
The fVDI/BetaDOS binaries came from AFROS 8.12 (sourceforge aranym/afros).

```bash
ST=/tmp/frua-aranym; mkdir -p $ST/drive_c
rsync -r --exclude DBG.LOG --exclude frua.prg data/work/gamedata/ $ST/drive_c/
mkdir -p $ST/drive_c/gemsys   # aranym.sys + ATFF09/10.FNT from AFROS drive_c/gemsys
cp data/work/aranym/boot.st $ST/boot.st
sed 's#^C = .*#C = /tmp/frua-aranym/drive_c#; s#^Floppy = .*#Floppy = /tmp/frua-aranym/boot.st#' \
    data/work/aranym/aranym.cfg.template > $ST/aranym.cfg
make && .claude/skills/run-aranym-nova/driver.sh build
```
(gemsys note: keep a copy of aranym.sys + the two fonts beside boot.st in
data/work/aranym/gemsys/ — copied below.)

## Gotchas (hit live)

- EmuTOS alone does NOT mount hostfs — that is what the boot floppy's
  BetaDOS + hostfs.dos are for. No floppy = green desktop, no C:.
- Hatari's VDI mode caps at 4 planes; it cannot host this test.
- ARAnyM writes the log as lowercase `dbg.log` (hostfs case handling).
- `pkill -x aranym`, never `-f`.
