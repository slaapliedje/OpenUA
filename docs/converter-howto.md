# Playing PC (DOS) fan modules

OpenUA plays the community's FRUA modules. The design data
(`GAME001/GEO*/MONST*/STRG*.DAT`) is byte-identical between the DOS and
Mac releases and needs no conversion — but a PC module's **custom art**
ships in the DOS `HLIB` format under DOS 8.3 names. Three ways to play
one, from easiest to most manual:

## 1. Just drop it in (colour play)

Unpack the module into a `<NAME>.DSN` folder in your OpenUA directory
and play. The engine detects DOS art and **converts it in place on
first touch** — the first load of each art file pauses briefly (a
fraction of a second typically; a few seconds once for a big title
screen on an 8 MHz ST), writes the converted `.ctl` next to the
original, and never pays that cost again.

The one thing this does NOT give you is 1-bit art for the mono
(ST High) build — synthesizing it is far too slow to do during play,
so the mono build shows the base game's art until you run the
installer or the offline converter below.

## 2. UAINST.TTP — install straight from the ZIP (Atari)

Drag the module's ZIP onto `UAINST.TTP` from the desktop (or run it
and type the ZIP name). It extracts the module into a `.DSN` folder
and converts everything up front — colour **and** mono. A big module
takes a few minutes on an ST, seconds-to-a-minute on a Falcon. Then
pick the design with SELECT A DESIGN.

## 2a. uainst — install straight from the ZIP (Amiga)

The Amiga build of the same installer. Run it with no argument (from a
Shell, or by double-clicking it) and it pops the standard **asl.library**
file requesters: pick the module ZIP, then pick the drawer to install
into. It then extracts and converts exactly as the Atari version does —
colour `.ctl` twins plus the 1-bit mono synthesis the ECS build needs.
You can also drive it from a Shell with arguments:
`uainst <module.zip> [destination-drawer]` (with no destination it
installs into the current drawer). Then pick the design with SELECT A
DESIGN.

The installer runs its whole job on a 256 KB stack it allocates itself
(StackSwap), so it works regardless of the small default stack a Shell
or Workbench launch grants — no `Stack` command needed.

## 3. art_convert.py — convert on your PC/Mac

## What you need

- Python 3 on your PC/Mac (no extra packages).
- The module, unpacked into its `.DSN` folder.

## How

Convert **a copy** staged where the engine will read it — the module's
`.DSN` folder inside your OpenUA game-data directory:

```sh
python3 art_convert.py MYMODULE.DSN/*.TLB MYMODULE.DSN/*.tlb
```

For each DOS art file this writes:

- `<name>.ctl` — the colour art, converted to the engine's GLIB format,
  named in DOS 8.3 (the engine probes that spelling as a fallback, and
  it fits real Atari volumes);
- `<name>.TLB` — **overwritten in place** with a synthesized 1-bit
  version for the mono (ST High) build, derived from the colour art via
  the engine's own ink model with ordered dithering.

Because the original HLIB file is consumed, always convert a staged
copy and keep the module archive pristine.

## Options

- `--no-mono` — skip the 1-bit synthesis (leave the original `.TLB`).
- `--mac-names` — emit expanded Mac-convention names
  (`bigpic0245.ctl`) instead of 8.3. Only for use with real Mac FRUA on
  an HFS volume: on a GEMDOS/FAT volume these names are too long and
  **collide** after 8.3 clipping.

## Coverage

Every format that appears in the fan-module corpus converts — plain,
RLE (method 18), compressed-transparent (method 23, both layouts),
animation tables (method 25), AND/OR mask pairs, wall master libraries,
and the CBODY/COMSPR composite tables. Several conversions are proven
**byte-identical** against SSI's own Mac files. Whole-library UI art
(`FRAME`, `TITLE`) converts in colour only.

Verified end-to-end with BEOWOLF, AGAINST THE GIANTS, and the Pool of
Radiance remake (Game39: 191/191 files) on both the colour and ST-mono
builds.
