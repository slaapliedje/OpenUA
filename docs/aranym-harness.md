# ARAnyM as a Nova test harness — feasibility, and what is still blocked

**Status: PARTIAL. The `nova=force` key is done and shipped; the ARAnyM boot
reaches an EmuTOS desktop but the host drive is not mounted, so nothing has
actually been run under it yet. No Nova code path has been verified here.**

Why bother: `display_nova.c` is the only backend with no emulator. Hatari cannot
host it (`dsp_detect` returns VIDEL on `_VDO == 3` before the probe, and Hatari's
`--vdi-planes` tops out at 4, so the 8-plane screen the probe requires cannot
exist). Every Nova change therefore needs a round trip through the user's real
ATW800/2. The workstation-leak fix in `bbbd5167` is unverified for exactly this
reason.

## What the force key does (DONE)

`video.cfg` may contain `nova=force`, which makes `dsp_detect()` run the Nova
probe even when `_VDO` says Falcon. Read by a small independent helper in
`display_videl.c` rather than through `videl_cfg_mode()`, because that runs
inside `videl_init` — i.e. after a backend has already been chosen.

A config key rather than a `-D` deliberately: ONE binary then serves the card
and the emulator. This session lost time to a field log produced by a build
older than the fix it was meant to be testing; fewer variants is fewer chances
to compare the wrong two things.

**What ARAnyM could validate:** the probe itself, the AES/VDI workstation open,
the depth query, palette writes, and regressions like the detect leak (count
`nova: card width` in `DBG.LOG` — it must appear exactly ONCE, not twelve
times).

**What it cannot:** `nova_hw_inverse`, the VDI pen -> hardware-slot table
measured from the ATW800/2 PALTEST corners. ARAnyM is the same *class* of
target (chunky 8bpp through `graf_handle` + `vq_extnd`), not the same hardware.
Anything depending on that mapping still needs the card.

## Verified facts about ARAnyM 1.1.0 here

- Boots **headless on Xvfb** (`DISPLAY=:99`, `SDL_VIDEODRIVER=x11`). Window
  title `ARAnyM 1.1.0 (Press the [Pause] key for SETUP)`, 640x480.
- `-r 8` selects 8-bit depth; config `[VIDEO] BootColorDepth = 8`.
- `Cookie_MCH = 50000` -> machine type 5 (ARAnyM).
- **TOS 4.04 boots** from Hatari's `/usr/share/hatari/tos404.img` via
  `--option GLOBAL:TOS:<path>`, and reaches the GEM desktop — **but shows only
  floppies A/B. Plain TOS has no HostFS**; ARAnyM's host-folder mapping is a
  NatFeat the ROM knows nothing about.
- **ARAnyM REJECTS Hatari's EmuTOS**: `etos512us.img` gives "Wrong TOS version.
  You need the original TOS 4.04." It wants its own build.
- `emutos-aranym-1.3` (fetched from the EmuTOS SourceForge release, installed at
  `~/.aranym/emutos-aranym.img`) **boots fine** — `EmuTOS 2024/03/17 loading …
  [OK]` — and `-e` selects it.
- With EmuTOS the desktop still shows only `DISK A` even with
  `[HOSTFS] C = /tmp/aranym-mount/` set. The drive is configured; the default
  desktop simply has no icon for it. Needs Options -> Install Devices, or a
  desktop `.INF` that declares it.
- `[GLOBAL] Bootstrap = mintara.prg` (the shipped default) makes EmuTOS try to
  bootstrap FreeMiNT and fail — harmless, but clear it for a clean boot.

## The gotcha that cost the most

**`import -window <stale-id>` hangs AND wedges the whole X server.** ARAnyM's
window id changes between runs (0x200008 vs 0x200026 here). Capturing a dead id
hangs `import`, and afterwards `xwininfo` and `xwd` hang too — the display is
unusable until `Xvfb` is killed and restarted. This looked exactly like "ARAnyM
cannot be screenshotted" for a while; it is not. **Re-query the live window id
immediately before every capture** and it works first time:

```sh
wid=$(xwininfo -root -tree | grep -i aranym | head -1 | awk '{print $1}')
import -window "$wid" /tmp/shot.png
```

Same family as the Hatari note about never grabbing while a button is held.

## Remaining work

1. **Get the host drive visible.** Either drive Options -> Install Devices once
   and save the desktop, or ship a prepared `.INF`. Until this is done nothing
   can be launched.
2. **Autorun `FRUA.PRG`.** A `#Z 01 C:\FRUA.PRG@` line in a desktop `.INF`
   is the intended mechanism (it runs *after* AES is up, which the Nova path
   needs — `nova_open_ws` calls `appl_init`, so the AUTO folder is NOT usable).
   A bare `NEWDESK.INF` containing only the `#Z` line did not autorun under
   TOS 4.04; it likely needs a fuller INF.
3. **`tools/aranym_ui.sh`** mirroring `tools/hatari_ui.sh`: `start` (boot + wait
   for a `DBG.LOG` marker), `shot`, `log`, `stop`, `key`, `click`. The waiting
   trick that works well here is polling for `DBG.LOG` in the mount — the engine
   writing it is proof it ran, and needs no screenshot at all.
4. Then the actual payoff: boot the `-DFRUA_NOVA` build with `nova=force` and
   confirm `nova: card width` appears **once**.

## Reproducing where this got to

```sh
M=/tmp/aranym-mount
mkdir -p $M && cp <gamedata>/* $M/
make EXTRA_CFLAGS=-DFRUA_NOVA && cp frua.prg $M/FRUA.PRG
printf 'nova=force\n' > $M/video.cfg
export DISPLAY=:99 SDL_VIDEODRIVER=x11
aranym -e -r 8 -N --option "HOSTFS:C:$M/" &
```
