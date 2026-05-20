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
