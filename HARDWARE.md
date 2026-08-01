# Installing OpenUA on real hardware

Everything here is **untested on real machines** — that is the whole point of
writing it down. If you get further than this document expects, or less far,
that is worth reporting either way.

`tools/mkhwdist.sh <version>` builds the disk images described below from the
release zips, into `dist/hw/`.

## Read this first: the data does not fit on floppies

A minimum playable install is about **4 MB** — the base art libraries and data
files, plus one design — on top of the ~1 MB engine binary. That is three
1.44 MB Atari disks, seven 720 KB ones, or five Amiga disks.

(It used to be 7.4 MB. A staged directory holds every art library **twice** —
the DOS original `.TLB` and the Mac `.ctl` twin the converter derives from it,
23 pairs. The engine reads the `.ctl`, so `tools/mkdatadisks.sh` drops the DOS
originals by default and the set halves. `ART=both` keeps them, which you want
only if you intend to revive the monochrome build.)

So **every one of these machines needs a hard disk, CF or SD card.** The disk
images below get the *engine* across; they cannot get the *game* across. If
your target machine has only floppy drives, it cannot run this yet.

The practical route is to prepare the data on your PC and write it to the mass
storage directly (a CF card in a reader, an SD in an ACSI/IDE adapter), then
use the disk image only for the binary — or skip the image entirely and copy
the binary the same way.

No game data is included with OpenUA. You supply it from a legally-obtained
copy of Unlimited Adventures; see [`GAMEDATA.md`](GAMEDATA.md) for how to
build `frua.rsc` and stage the design folders.

## The images

| Machine | Image | Notes |
|---|---|---|
| Falcon030 / TT030 | `openua-falcon-<v>.st` | 1.44 MB. Binary raw — runs off the disk. |
| ST / STE / Mega ST | `openua-atari-st-<v>.st` | 720 KB. Binary zipped; it does not fit raw. |
| Amiga AGA (A1200/A4000) | `openua-amiga-aga-<v>.adf` | 880 KB. Binary raw. |
| Amiga ECS/OCS (A500+/A600/A2000) | `openua-amiga-ecs-<v>-disk1.adf`, `-disk2.adf` | 880 KB each. Binary split in half. |

All of them are plain images: write them to real floppies, or serve them from a
Gotek / HxC.

### Why the Mega ST disk is compressed

The 68000 build is 1,063,312 bytes. A 720 KB disk holds 737,280, and the
extended ST formats a Gotek can serve top out around 923,648 (82 tracks × 11
sectors × 2 sides) — still short. The Mega ST's WD1772 cannot read HD media, so
the 1.44 MB route is not available to it either.

The disk therefore carries `FRUA.ZIP`. Unpack it PC-side and copy `FRUA.PRG` to
your mass storage along with the game data.

### Why the ECS set is two disks and not an archive

The ECS binary is 993,296 bytes against ~878 KB usable on an 880 KB disk. The
obvious answer is LhA, but that makes the install depend on a tool you may not
have. **AmigaDOS has shipped `Join` in `C:` since 1.2**, so the binary is simply
split in half and rejoined with stock OS commands:

```
Copy DF0:frua.00 TO DH0:            ; disk 1
Copy DF0:frua.01 TO DH0:            ; disk 2
Join DH0:frua.00 DH0:frua.01 AS DH0:frua
Protect DH0:frua +e
Delete DH0:frua.00 DH0:frua.01
```

`Protect +e` matters — `Join` does not set the executable bit.

## Per-machine requirements

**Atari Falcon030 / TT030** — `openua-falcon-*.zip`, 4 MB, TOS 4.04 (Falcon) or
3.0x (TT). One binary serves both; the display and sound paths are chosen at
runtime.

**Atari ST / STE / Mega ST** — `openua-atari-st-*.zip`, 2 MB, **colour
monitor**. ST-low, 16 colours, native bitplanes. TOS 2.06 or EmuTOS is what has
been tested; there is no version check in the code, so earlier TOS may well
work — nobody has tried. A mono (SM124) setup will *not* work yet: the
monochrome build currently hangs at boot.

This 68000 build also runs on the TT and Falcon, which pick their own
higher-colour backend, so it is the run-on-anything Atari binary.

**Amiga AGA** — `openua-amiga-*.zip`, **Kickstart 3.0+**, about 4 MB. Chooses
AGA or RTG at runtime.

**Amiga ECS/OCS** — `openua-amiga-ecs-*.zip`, **Kickstart 2.0+**, 2 MB. Native
32-colour bitplanes. **Kickstart 1.3 will not work** — it dies in the C startup
before the program begins, so you get no error from OpenUA itself, just a
failure to launch.

## Installing

1. Stage your game data on the PC (see `GAMEDATA.md`). You want `frua.rsc` and
   at least one `*.DSN` design folder.
2. Copy the data to the machine's hard disk / CF / SD.
3. Copy the engine binary into the **same directory as the data** — it looks
   for `frua.rsc` relative to where it runs.
4. Run it: `FRUA.PRG` on Atari, `frua` on Amiga.

`UAINST` (`UAINST.TTP` / `uainst`) is optional and installs DOS fan modules
from their ZIP, converting the art in place.

## What is worth reporting

Anything at all, but especially:

- Does it boot, and how long does it take to reach the main menu?
- Does the intro sequence render correctly?
- Real-machine timings against the emulator figures — the emulated STE walks a
  step in 0.92 s and the ECS in 0.97 s.
- Sound: does anything play, and does it sound right?
- On the ST: any TOS version older than 2.06 that works, or fails.

Emulator baselines for comparison, menu to the tavern in the sample module:
TT 2:50, Falcon 2:35, AGA 4:00, STE 6:23, ECS 9:52.
