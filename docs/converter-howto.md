# Playing PC (DOS) fan modules — the art converter

OpenUA plays the community's FRUA modules. The design data
(`GAME001/GEO*/MONST*/STRG*.DAT`) is byte-identical between the DOS and
Mac releases and needs no conversion — but a PC module's **custom art**
ships in the DOS `HLIB` format under DOS 8.3 names, which the engine
(a Mac-release port) cannot read. `art_convert.py` converts it.

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
