# The Macintosh release

How the Mac release of FRUA is packaged, and how to unpack it down to the
decompilation inputs. Everything here lands under `data/`, which is
git-ignored — none of it is committed.

## What was supplied

`Unlimited_Adventures_disks.sit` — a StuffIt 5 archive (~3.8 MB) holding the
three install floppies of the Macintosh FRUA release as Apple DiskCopy 4.2
images (1.44 MB HFS volumes, 84-byte DiskCopy header).

## Unpacking pipeline

```
Unlimited_Adventures_disks.sit
 └─ unar ───────────────────► 3x DiskCopy 4.2 images
      └─ strip 84-byte header ──► raw HFS volumes
           └─ hfs_extract.py (machfs)
                └─ DiskDoubler split archive: 3x SPLT segments + "DD Expand"
                     └─ dd_unsplit.py ──► one DDAR archive
                          └─ unar ──────► the "Unlimited Adventures ƒ" folder
```

The disks do not hold the game directly — they hold a DiskDoubler (Salient
Software) archive split across all three floppies. Steps, from the repo root:

```sh
# 1. StuffIt -> three DiskCopy 4.2 floppy images
unar -o data data/Unlimited_Adventures_disks.sit

# 2. each HFS volume holds one DiskDoubler split segment; extract all three
for n in 1 2 3; do
  tools/.venv/bin/python3 tools/hfs_extract.py extract \
    "data/SSI Unlimited Adventures/Unlimited Adventures $n.image" data/dd-archive
done

# 3. reassemble the DiskDoubler split archive (SPLT segments -> one DDAR)
python3 tools/dd_unsplit.py data/dd-archive/*.dd.1 \
    data/dd-archive/*.dd.2 data/dd-archive/*.dd.3 -o data/dd-archive/joined.ddar

# 4. expand the DiskDoubler archive
unar -o data/frua-mac data/dd-archive/joined.ddar
```

## What's inside

The `Unlimited Adventures ƒ` folder:

| Item                   | What it is                                            |
|------------------------|-------------------------------------------------------|
| `Unlimited Adventures` | The application. unar writes it AppleDouble-wrapped.  |
| `Disk1` … `Disk4`      | Game data: `.TLB .GLB .SLB .CTL .DAT` library files.  |
| `HEIRS.DSN`            | "Heirs to Skull Crag" — the built-in sample adventure.|
| `TUTORIAL.DSN`         | The tutorial design (with its own `SAVE/` folder).    |
| `Art`                  | MacPaint / PICT source art.                           |

## The 1.2 oracle (added 2026-07-24)

`data/` holds **two** StuffIt archives and they are different releases:

| archive | contents | version |
|---|---|---|
| `Unlimited_Adventures_disks.sit` | three DiskCopy 4.2 floppy images (the pipeline above) | **1.0** |
| `unlimited_adventures.sit` | an *installed* "SSI Unlimited Adventures Folder" | **1.2** |

The second one is the Mac 1.2 build ADR-0017 decision 7 wants as a per-function
oracle. It needs no DiskCopy/DiskDoubler pass — the folder is already unpacked
inside it:

```sh
unar -o data/work/mac12 data/unlimited_adventures.sit
python3 tools/appledouble.py \
  "data/work/mac12/SSI Unlimited Adventures Folder/Unlimited Adventuresƒ/Unlimited Adventures.rsrc" \
  --fork resource -o data/work/UnlimitedAdventures-1.2.rfork
python3 tools/dis68k.py data/work/UnlimitedAdventures-1.2.rfork --out data/work/disasm-1.2
```

`Version 1.2    February 28,1994`, 633,145-byte fork, app dated 1994-03-02,
`sha256 c9673b14cb426aa10e5ab79a9a72f20ffa8510b5cbc606384c8679763edc6ad1` on the
AppleDouble `.rsrc`. It is a **build-time input only** — never a player
requirement (ADR-0017 decision 1), and like everything under `data/` it is not
committed.

Diffing it against 1.0 is what produced the 32-hunk delta in
`docs/function-audit-2026-07-24.md` §5. Note that a raw byte diff is useless
here: 1.2 removes one jump-table entry, which shifts every `jsr %a5@(…)` operand
above it and makes 22 of 23 segments "differ" without a single instruction
changing. Compare mnemonic streams with operands dropped.

## The decompilation target

The application's **resource fork** is the 68k program. Pull the raw fork out
of unar's AppleDouble wrapper:

```sh
python3 tools/appledouble.py \
    "data/frua-mac/joined/Unlimited Adventures.rsrc" \
    --fork resource -o data/work/UnlimitedAdventures.rfork
```

`tools/rsrc_list.py` summarises it — 87 resources in 23 types, ~631 KB:

- **`CODE` ×23, ~565 KB** — the 68k program segments. `CODE 0` is the jump
  table; `CODE 1` the main segment; `CODE 2`–`22` the game and editor code.
  Every segment is under the 32 KB classic-Mac segment limit.
- `CREL` / `DREL` — code / data relocation tables. Their presence marks this
  as a **THINK C** (Symantec) build: A5-relative globals, segment relocation
  applied at load. The decompilation should assume THINK C conventions.
- `DATA` — the global data segment; `STRS` — the string table.
- `DITL` ×5, `DLOG` ×4, `ALRT` ×1, `MENU` ×3, `WIND` ×3 — strikingly few
  Toolbox UI templates. FRUA draws most of its interface itself, which
  reinforces ADR-0006: a GEM-AES mapping would have bought little.
- `FONT`/`FOND`, `clut`/`pltt`, `ICN#`/`icl4`/`icl8`/`ics#` — bundled bitmap
  fonts, 256-colour palettes, and Finder icons.
