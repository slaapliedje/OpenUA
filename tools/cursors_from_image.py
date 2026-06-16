#!/usr/bin/env python3
"""Build a flat 'FCUR' colour-cursor pack from a grid image of cursors.

The engine (src/main.c load_frua_cursors / qd_install_color_pointer) installs
cursor 0 as a 16x16 colour mouse pointer from a `frua.cur` FCUR pack. Normally
that pack is generated from the DOS ALWAYS.TLB (copyrighted). This tool builds
the same pack from a free, redrawn cursor sheet instead — a 6-column grid of
cursors on a magenta (transparent) background with dark gutters, in ALWAYS order
(#0 sword pointer, shield, disc, directional arrows, turns, crosshair, square,
hourglass...).

    python3 tools/cursors_from_image.py data/mouse_cursors.jpeg -o frua.cur

FCUR format (big-endian, matches tools/hlib_extract.py emit_cursor_pack):
    0  4   'FCUR'
    4  2   version (1)
    6  2   cursor count
    8  48  palette, 16 RGB triples
    per cursor: u16 width, height, xhot, yhot, then width*height palette
                indices (0xFF = transparent).
"""
import argparse
import struct
import sys

from PIL import Image

# 16-colour cursor palette (index 0..15). Covers the sword (greys + gold),
# shield (blue), the red selection dot, and the white/grey/black arrows.
PALETTE = [
    (0, 0, 0),        # 0  black outline
    (255, 255, 255),  # 1  white
    (200, 200, 200),  # 2  light grey
    (140, 140, 140),  # 3  mid grey
    (80, 80, 80),     # 4  dark grey
    (210, 170, 40),   # 5  gold
    (150, 110, 20),   # 6  dark gold
    (50, 70, 190),    # 7  blue
    (110, 130, 230),  # 8  light blue
    (200, 30, 30),    # 9  red
    (60, 40, 20),     # 10 brown shadow
    (170, 140, 90),   # 11 tan
    (120, 90, 50),    # 12 mid brown
    (230, 210, 160),  # 13 pale gold highlight
    (40, 40, 40),     # 14 near-black
    (160, 160, 160),  # 15 grey
]

CURSOR_SIZE = 16

# Per-cursor hotspots (xhot, yhot); only cursor 0 (the pointer) really matters.
# The sword's active point is the blade tip, upper-left.
HOTSPOTS = {0: (2, 2)}


def is_magenta(c):
    r, g, b = c
    return r > 115 and b > 115 and g < 115 and abs(r - b) < 95


def is_dark(c, thr=70):
    r, g, b = c
    return r < thr and g < thr and b < thr


def nearest(c):
    r, g, b = c
    best, bd = 0, 1 << 30
    for i, (pr, pg, pb) in enumerate(PALETTE):
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if d < bd:
            bd, best = d, i
    return best


def find_gutters(values, n_cells):
    """Given a per-line 'is this line all-gutter' boolean list, return the
    content spans (start, end) for n_cells cells."""
    runs = []
    s = None
    for i, v in enumerate(values + [True]):
        if v and s is None:
            s = i
        elif not v and s is not None:
            runs.append((s, i))
            s = None
    return runs


def cell_spans(im, axis, n_cells):
    """Detect content spans along axis 0 (cols, scanning x) or 1 (rows)."""
    W, H = im.size
    px = im.load()
    if axis == 0:
        # gutter columns = all-dark down a set of sample rows
        ys = [int(H * f) for f in (0.08, 0.25, 0.42, 0.6)]
        content = [not all(is_dark(px[x, y]) for y in ys) for x in range(W)]
        spans = find_gutters(content, n_cells)
        length = W
    else:
        xs = [int(W * f) for f in (0.06, 0.22, 0.40, 0.58, 0.76, 0.93)]
        content = [not all(is_dark(px[x, y]) for x in xs) for y in range(H)]
        spans = find_gutters(content, n_cells)
        length = H
    # keep the widest n_cells spans, sorted by position
    spans = [s for s in spans if s[1] - s[0] > length // (n_cells * 4)]
    return spans


def extract(im, cols, rows):
    """Yield (index, 16x16 list-of-rows of palette indices/0xFF)."""
    cspans = cell_spans(im, 0, cols)
    rspans = cell_spans(im, 1, rows)
    if len(cspans) < cols or len(rspans) < rows:
        # fall back to even geometric grid
        W, H = im.size
        cspans = [(int(W * c / cols) + 6, int(W * (c + 1) / cols) - 6)
                  for c in range(cols)]
        rspans = [(int(H * r / rows) + 6, int(H * (r + 1) / rows) - 6)
                  for r in range(rows)]
    idx = 0
    for (y0, y1) in rspans[:rows]:
        for (x0, x1) in cspans[:cols]:
            # trim a small inner margin to drop gutter bleed
            mx = max(2, (x1 - x0) // 24)
            my = max(2, (y1 - y0) // 24)
            cell = im.crop((x0 + mx, y0 + my, x1 - mx, y1 - my))
            small = cell.resize((CURSOR_SIZE, CURSOR_SIZE), Image.LANCZOS)
            sp = small.load()
            grid = []
            opaque = 0
            for yy in range(CURSOR_SIZE):
                row = []
                for xx in range(CURSOR_SIZE):
                    c = sp[xx, yy]
                    if is_magenta(c):
                        row.append(0xFF)
                    else:
                        row.append(nearest(c))
                        opaque += 1
                grid.append(row)
            yield idx, grid, opaque
            idx += 1


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("-o", "--out", default="frua.cur")
    ap.add_argument("--cols", type=int, default=6)
    ap.add_argument("--rows", type=int, default=5)
    ap.add_argument("--sheet", help="write a PNG contact sheet for verification")
    args = ap.parse_args(argv[1:])

    im = Image.open(args.image).convert("RGB")
    cursors = [(i, g) for (i, g, op) in extract(im, args.cols, args.rows)
               if op > 4]   # drop empty (all-transparent) trailing cells

    out = bytearray(b"FCUR")
    out += struct.pack(">HH", 1, len(cursors))
    for (r, g, b) in PALETTE:
        out += bytes((r, g, b))
    for (i, grid) in cursors:
        hx, hy = HOTSPOTS.get(i, (0, 0))
        out += struct.pack(">HHHH", CURSOR_SIZE, CURSOR_SIZE, hx, hy)
        for row in grid:
            out += bytes(row)
    with open(args.out, "wb") as f:
        f.write(out)
    print("wrote %s: %d cursors, %d bytes" % (args.out, len(cursors), len(out)))

    if args.sheet:
        cell = 20
        cols = 8
        rowsn = (len(cursors) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * cell, rowsn * cell), (64, 64, 64))
        spx = sheet.load()
        for n, (i, grid) in enumerate(cursors):
            cx = (n % cols) * cell
            cy = (n // cols) * cell
            for yy in range(CURSOR_SIZE):
                for xx in range(CURSOR_SIZE):
                    v = grid[yy][xx]
                    spx[cx + xx, cy + yy] = (255, 0, 255) if v == 0xFF else PALETTE[v]
        sheet.resize((cols * cell * 3, rowsn * cell * 3), Image.NEAREST).save(args.sheet)
        print("wrote sheet", args.sheet)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
