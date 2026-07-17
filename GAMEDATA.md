# Getting and installing the game data

OpenUA is only the engine. It plays the data files of *Forgotten Realms:
Unlimited Adventures* (FRUA, SSI 1993), which are copyrighted — **no OpenUA
release contains them**. You supply them from your own copy of the game.

## Where to get FRUA

- **Your own original Macintosh release** — the primary source. Everything
  OpenUA needs comes from it. It shipped as three 1.44 MB floppies (also found
  as a StuffIt archive of DiskCopy images).
- **GOG** — [Forgotten Realms: The Archives – Collection Two](https://www.gog.com/game/forgotten_realms_the_archives_collection_two)
- **Steam** — [Forgotten Realms: The Archives – Collection Two](https://store.steampowered.com/app/1882280/Forgotten_Realms_The_Archives__Collection_Two/)

The GOG/Steam re-releases bundle the **DOS** version of FRUA. Its designs and
data files are byte-identical to the Mac ones and its art converts (see below) —
but one piece, the engine resource archive `frua.rsc`, can today only be built
from the **Macintosh application** itself. So the Mac release gives you a
complete install; the DOS release covers module data and art once you have that
base.

## The game folder

Everything lives in **one folder**, next to the engine binary — the same layout
on every platform. The engine opens its files by bare name from the directory
it starts in.

```
OPENUA/
├── frua.prg              the engine (Atari; named `frua` on Amiga) — from the release zip
├── frua.rsc              engine resources, packed from the Mac application (below)
├── 8X8DB.TLB 8X8DB.CTL   ┐
├── ALWAYS.TLB ALWAYS.CTL │  the shared libraries — every file from the Mac
├── PICA…PICF, BIGPIC,    │  release's Disk1…Disk4 folders, copied flat
├── GAME.GLB GEO.GLB …    │  (.TLB .CTL .GLB .SLB .DAT)
├── MUSIC.SLB ITEM.DAT …  ┘
├── HEIRS.DSN/            each adventure ("design") is a folder named *.DSN
├── TUTORIAL.DSN/
├── start.dat             35-byte "current design" marker (below)
└── frua.cur              optional colour mouse pointer (engine falls back to mono)
```

The engine writes into this folder at runtime — roster characters
(`CHAR*.CHR`) next to the binary, save games (`SavGam*`) inside the current
`.DSN` — so it must live on a **writable** drive.

## From the Macintosh release

First get at the files. If you have the floppies/StuffIt archive, the full
unpacking pipeline (StuffIt → HFS → DiskDoubler) is documented in
[`docs/mac-release.md`](docs/mac-release.md); if you have a working Mac or Mac
emulator with FRUA installed, you already have the `Unlimited Adventures ƒ`
folder. Then:

1. **Libraries** — copy every file inside `Disk1`, `Disk2`, `Disk3`, `Disk4`
   flat into the game folder.
2. **Designs** — copy each `*.DSN` folder whole. If a design has a `SAVE/`
   subfolder, move its contents up into the `.DSN` folder itself.
3. **`frua.rsc`** — pack it from the application's resource fork (needs a
   clone of this repo and Python 3). From the unpacked release:

   ```sh
   python3 tools/appledouble.py "…/Unlimited Adventures.rsrc" \
       --fork resource -o UnlimitedAdventures.rfork
   python3 tools/rsrcpack.py UnlimitedAdventures.rfork -o frua.rsc
   ```

   (If you copied the application out of an emulator as a raw resource fork,
   skip the first step and feed the fork straight to `rsrcpack.py`.)
4. **`start.dat`** — the boot marker naming the current design:

   ```sh
   python3 -c "open('start.dat','wb').write(b'HEIRS.DSN'.ljust(34,b'\x00')+b'\x01')"
   ```

If you build OpenUA from source, `make gamedata DSN=HEIRS.DSN` performs all of
this into `data/work/gamedata/` for you.

## From GOG / Steam (the DOS release)

Design and data files (`GAME001.DAT`, `GEO*.DAT`, `MONST*`, …) are
byte-identical between the DOS and Mac releases — copy them as-is. The **art**
is not: DOS art containers are `HLIB` (VGA planar, little-endian), the engine
reads Mac `GLIB`. One conversion pass fixes a module's art in place:

```sh
python3 tools/art_convert.py SOMEMODULE.DSN/*.TLB    # HLIB -> GLIB .ctl
```

This is the door to the [fan-module archive](http://frua.rosedragon.org) —
hundreds of community adventures, mostly authored on the PC. Verified
end-to-end with *Pool of Radiance* running on its own converted art. Known
limit: RLE-compressed big-picture entries (piece type 2) are not decoded yet
and are refused loudly rather than mangled.

Remember the caveat above: the DOS release cannot produce `frua.rsc`, so it
complements a Mac base install rather than replacing it.

## Setting it up per platform

| Release zip | Machine | Needs |
|---|---|---|
| `openua-falcon-*` | Atari Falcon030 / TT030 | 4 MB RAM; TOS 4.04 (Falcon) or 3.0x (TT) |
| `openua-atari-st-*` | Atari ST / STE (runs on any Atari) | 2 MB RAM; TOS 2.06 or EmuTOS |
| `openua-amiga-*` | Amiga AGA (A1200/A4000) or RTG card | KS 3.0+, ~4 MB |
| `openua-amiga-ecs-*` | Amiga ECS/OCS (A500+/A600/A2000/A3000) | KS 2.0+, 2 MB |

**Atari** — put the folder anywhere on your hard drive (e.g. `C:\OPENUA`) and
run `FRUA.PRG` from inside it; TOS makes the program's folder the current
directory, which is where the engine looks. In **Hatari**, mount the folder as
the GEMDOS drive: `hatari -d OPENUA --auto 'C:\frua.prg'` (add `--machine ste`
+ a TOS 2.06 image for the ST build).

**Amiga** — put everything in one drawer. From a Shell: `cd` into the drawer
and run `frua` (no icon is shipped yet). In **amiberry/WinUAE**, add the folder
as a directory hard drive and do the same from the Shell.

> ⚠️ OpenUA is emulator-validated only and has never been run on real
> hardware. If you try it on the real thing, please report what happens.

## Legal

You must own FRUA. Never redistribute the game folder you build — the
libraries, the designs, **and `frua.rsc`** are all copyrighted FRUA data. The
OpenUA binaries themselves contain none of it, which is exactly why they can be
shared and your data cannot.
