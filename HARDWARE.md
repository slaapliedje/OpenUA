# Installing OpenUA on real hardware

**A real Falcon030 has now done this**, from a hard-disk install, on a VGA
monitor, and played — so the Falcon route below is walked, not theoretical. The
ST/STE build has run on a Mega STe and the AGA build has had a brief test on
an A1200 (both 2026-08); the TT, Amiga ECS and Amiga RTG builds are still
untested on real machines, and the floppy/Gotek media have been verified as
filesystems rather than by booting a physical drive. If you get further than this document expects, or less far,
that is worth reporting either way.

What the first real machine changed, so you know what to look for: the AREA map
was drawing every wall transposed, quitting did not restore the desktop's video
mode, and a VGA monitor has **no 320×200 mode at all** — which is why
`video.cfg` exists (see "Choosing the video mode" below). None of the three
could have been found in an emulator.

`tools/mkhwdist.sh <version>` builds the disk images described below from the
release zips, into `dist/hw/`.

## Read this first: the data does not fit on floppies

A minimum playable install is about **4 MB** — the base art libraries and data
files, plus one design — on top of the ~1 MB engine binary. That is three
1.44 MB Atari disks, seven 720 KB ones, or five Amiga disks.

(It used to be 7.4 MB. A staged directory holds every art library **twice** —
the DOS original `.TLB` and the Mac `.ctl` twin the converter derives from it,
23 pairs. `tools/mkdatadisks.sh` ships **SSI's DOS files** by default and the
engine converts each library on first touch, so the set halves and what you
install is what SSI shipped. Budget ~7.4 MB at the destination even so: the
converted twin is written back beside the original. `ART=ctl` ships the
converted art instead — one disk fewer, no first-touch pause. `ART=both` keeps
both, which you want only to revive the monochrome build.)

So **every one of these machines needs a hard disk, CF or SD card** — unless
you have a **Gotek running FlashFloppy**, which changes the arithmetic
completely and is covered in its own section below: the whole data set fits on
one image there. With real floppy drives, the disk images below get the
*engine* across but cannot get the *game* across.

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
| ST / STE / Mega ST **on a Gotek** | `openua-st-gotek-<v>.st` | 1.44 MB at 150 rpm. Binary RAW — no unzip step. Needs FlashFloppy + `IMG.CFG`. |
| Amiga AGA (A1200/A4000) | `openua-amiga-aga-<v>.adf` | 880 KB. Binary raw. |
| Amiga ECS/OCS (A500+/A600/A2000) | `openua-amiga-ecs-<v>-disk1.adf`, `-disk2.adf` | 880 KB each. Binary split in half. |

All but the Gotek one are plain images: write them to real floppies, or serve
them from a Gotek / HxC.

## Gotek / FlashFloppy: bigger images than the hardware should allow

If you have a Gotek running **FlashFloppy**, the floppy capacity limits below
mostly stop applying, and the install gets much shorter — the whole game data
set arrives on **one image instead of six**.

The trick is not a faster disk. A stock ST's WD1772 is locked to 250 kbit/s and
nothing changes that. FlashFloppy instead **slows the emulated rotation**, so
more sectors pass the head per revolution at the same bit rate, and it will
serve **up to 255 cylinders** instead of 80. Slower rotation costs random-access
speed and nothing else, which is irrelevant for copying files off once.

| Image | Geometry | Speed | Holds |
|---|---|---|---|
| `openua-st-gotek-<v>.st` | 80 × 2 × 18, 150 rpm | ½ | the ST engine RAW, 1.44 MB |
| `openua-data-gotek-disk1.st` | 255 × 2 × 36, 75 rpm | ¼ | the ENTIRE data set, 9.4 MB |

**Copy `IMG.CFG` to the root of the Gotek's USB stick.** It ships next to the
images and declares those geometries; without it FlashFloppy only recognises
standard floppy sizes and the images will not mount. Geometry and rpm figures
come from [phjanderson/flashfloppy-atari-disks](https://github.com/phjanderson/flashfloppy-atari-disks),
which is worth a look if you want blank images in other sizes.

Two caveats worth knowing. Large images occasionally throw a read error,
typically right after a disk swap — retry it. And the 255-cylinder images
cannot be tested in Hatari (it decodes standard floppy geometries only), so
unlike everything else here they have been verified as filesystems rather than
by booting them. The 1.44 MB Gotek ST image *was* booted, on an emulated stock
ST.

### Why the Mega ST disk is compressed

This is about a REAL floppy drive; on a Gotek, use `openua-st-gotek-<v>.st`
above and skip it.

The 68000 build is 1,063,312 bytes. A 720 KB disk holds 737,280, and the
extended formats a WD1772 can be talked into top out around 923,648 (82 tracks
× 11 sectors × 2 sides) — still short. It cannot read HD media at all, so the
1.44 MB route is not available to a real drive either.

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

### Choosing the video mode (Falcon)

By default the engine picks from the monitor type: 320x200 on RGB/TV, and on
VGA 320x240 with the engine's 200 lines centred and a blanked 20-line band
above and below. That choice is a guess — you know what your monitor syncs.
Put one token in a file called **`video.cfg`** beside the binary to override it:

| token | mode word | result |
|---|---|---|
| `auto` (or no file) | from the monitor type | the default above |
| `rgb200` | RGB/TV timing, no VGA bit | **320x200, no letterbox** — try this first on VGA |
| `vga240` | VGA + double-line | 320x240, 20-line bands |
| `vga480` | VGA, no double-line | 320x480, 140-line bands |
| `0x<hex>` | raw `VsetMode` word | for a monitor none of the presets suit |

`rgb200` is the interesting one: it asks for the RGB timing on whatever monitor
you have, which `auto` will never do on VGA, and it is the only way to get the
frame with no blanked bands. If the monitor will not sync it you will see it
immediately — and the engine still checks the mode gives a 320-wide frame with
at least 200 lines, falling back to the automatic choice if not, so a bad line
here cannot strand you.

The tokens name a MODE, not a guaranteed geometry: `VERTFLAG` halves the
vertical resolution on VGA (480→240) and doubles it on RGB/TV (200→400), so
there is no monitor-independent "320x240". There is no 320x200 VGA mode at all
— that is why a VGA Falcon letterboxes. `DBG.LOG` records the width, height and
letterbox actually obtained, so a `0x<hex>` experiment documents itself.

### If quitting leaves the desktop in the wrong colours

This was an open bug through 0.9.4 and is **fixed as of 0.9.5** — a real Falcon
now returns to a correct desktop. It is worth knowing the escape hatch exists
anyway, because restoring the video mode reinitialises the VDI but *not* the
AES (Atari Compendium p.290), and a machine with a different desktop mode or a
VDI replacement may want a different order. Add a second token to `video.cfg`,
on the same line or its own:

| token | on exit |
|---|---|
| `exit=full` | (default) restore the mode, then the palette |
| `exit=vdi` | restore the mode only, and let the VDI's own reinitialisation set the colours |
| `exit=palfirst` | restore the palette first, so the VDI reinitialisation has the last word |
| `exit=mode` | the older `VsetMode` route, then the palette |

A file containing only `exit=vdi` leaves the video mode on its automatic
choice. `DBG.LOG` records which strategy ran and the mode word being restored
(`videl_shutdown: exit style` / `restoring mode`), so a report of "this one
worked" is self-documenting.

**Step 3 is not optional, and running the engine straight off the disk looks
broken.** Launched from the floppy, it initialises fully — display, sound,
`frua.rsc` — and then hits a black screen, because the game data it needs is
not on that disk and cannot fit (the minimum install is ~7.4 MB). There is no
message: booted from a floppy it also cannot write its own `DBG.LOG`, so there
is no trail either. Verified in Hatari 2026-08-02, and it is the same black
screen you get running from a hard disk with the data missing — the engine is
fine, it simply has nothing to load. Copy the binary next to your data first.

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

## Monochrome (SM124 / ST-High) needs the MAC release

The colour builds play from **either** the DOS or the Mac release (ADR-0017).
**Mono is the one exception: it requires the Mac release.**

In mono the engine selects the `.tlb` art set, and a Mac B&W `.TLB` is a
different FORMAT from a DOS HLIB `.TLB` despite sharing the extension. The
install-time mono synthesiser (`tools/art_convert.py`) can derive 17 of the 23
libraries from colour art, but not the six chrome ones — **ALWAYS, FRAME, GEN,
MENU, TITLE, TOPVIEW** — and ALWAYS is the first thing the boot loads. With a
DOS-only install the mono build stops with `LBLoad: Bad Lib: 'always.TLB'`
rather than starting.

Mono is also deprioritised: it is not part of the shipped zips and is built
explicitly (`make CPU68K=68000 EXTRA_CFLAGS=-DFRUA_BWMODE`).
