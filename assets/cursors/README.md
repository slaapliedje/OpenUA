# Colour mouse cursors (free / redrawn)

Individual 16×16 transparent PNGs, one per cursor. `make` (the `gamedata`
target) packs them into a `frua.cur` FCUR pack via
`tools/cursors_from_image.py`, and the engine installs **cursor `00`** as the
colour mouse pointer (`src/main.c` → `qd_install_color_pointer`).

These replace the copyrighted FRUA cursor art (the DOS `ALWAYS.TLB`) with a
free set, so the colour pointer ships with the repo. Setting `DOS_ALWAYS=` on
the make line still takes priority if you'd rather load the original art from
your own DOS data at build time.

## Editing / replacing

- Files are packed in order of the **leading number** in the filename, so
  `00-*.png` is the pointer, `01-*.png` the next cursor, etc. The label after
  the number is cosmetic — rename freely.
- Each PNG is RGBA with real transparency (alpha < 128 = transparent). Any size
  works; the tool downscales to 16×16, so a larger crisp source is fine.
- Colours are quantised to a 16-entry cursor palette (greys, gold, blue, red);
  see `PALETTE` in `tools/cursors_from_image.py`.
- The pointer's hotspot (the active pixel) is set in the tool's `HOTSPOTS`
  (cursor 0 = the sword's blade tip).

To rebuild after a change: `make gamedata` (or just `make run-game`).

Current set: AI-generated, extracted from a 6-column cursor sheet.
