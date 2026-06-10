#!/usr/bin/env python3
"""hlib_extract.py — decode FRUA (DOS) HLIB tile libraries (*.TLB).

The MS-DOS release of Unlimited Adventures stores its UI/art tiles in an
"HLIB" container.  This is NOT the shipping asset path for the port (we
build from the Mac resource fork, ADR-0001/0007) — it is a one-off utility
for *recovering reference art*, e.g. the mouse-cursor shapes that the Mac
build loads at runtime and never seeds into a resource we can lift.

Format (all little-endian):

    offset  size  field
    0       4     'HLIB' magic
    4       4     total file size
    8       4     entry count (N)
    12      4     content tag ('TILE')
    16      4*(N+1)  offset table; entry i spans [off[i], off[i+1]),
                     off[N] == file size.

    entry 0 : 16-colour palette.  8-byte header, then 16 RGB triples
              (6-bit-ish VGA values 0x00/0x57/0xAB/0xFF...).
    entry k : a tile.  8-byte header —
                u16 width
                u16 xhot     (cursor hot-spot x / placement origin)
                u16 yhot     (cursor hot-spot y / placement origin)
                u16 flags    (low byte ~0x15)
              then the pixels in VGA *Mode-X* (unchained planar) order:
              four planes of (stride*height) bytes, plane p holding the
              columns x where x & 3 == p, stride = ceil(width/4) bytes per
              plane-row.  Each byte is an 8-bit palette index; 0xFF means
              transparent.  height = (len-8) / (stride*4).

Usage:
    python3 tools/hlib_extract.py LIB.TLB                 # summary
    python3 tools/hlib_extract.py LIB.TLB --sheet out.png # contact sheet
    python3 tools/hlib_extract.py LIB.TLB --dump outdir   # per-tile PNGs
    python3 tools/hlib_extract.py LIB.TLB --tile 7 --png t7.png
"""

import argparse
import struct
import sys


MAGIC = b"HLIB"
TRANSPARENT = 0xFF


class Tile:
    __slots__ = ("index", "width", "height", "hotspot", "flags", "pixels")

    def __init__(self, index, width, height, hotspot, flags, pixels):
        self.index = index
        self.width = width
        self.height = height
        self.hotspot = hotspot      # (xhot, yhot) from the header
        self.flags = flags
        self.pixels = pixels        # bytes, row-major width*height, 0xFF = transparent


class HLib:
    def __init__(self, data):
        if data[:4] != MAGIC:
            raise ValueError("not an HLIB file (bad magic %r)" % data[:4])
        self.size, self.count = struct.unpack_from("<II", data, 4)
        self.tag = data[12:16]
        table_off = 16
        self.offsets = list(
            struct.unpack_from("<%dI" % (self.count + 1), data, table_off)
        )
        self.data = data
        self.palette = self._read_palette()      # list[(r,g,b)] len 16
        self.tiles = [self._read_tile(i) for i in range(1, self.count)]

    def _entry(self, i):
        return self.data[self.offsets[i]:self.offsets[i + 1]]

    def _read_palette(self):
        e = self._entry(0)
        # 8-byte header, then 16 RGB triples.
        pal = []
        body = e[8:8 + 48]
        for i in range(0, len(body), 3):
            pal.append((body[i], body[i + 1], body[i + 2]))
        while len(pal) < 16:
            pal.append((0, 0, 0))
        return pal[:16]

    def _read_tile(self, i):
        e = self._entry(i)
        width, xhot, yhot, flags = struct.unpack_from("<HHHH", e, 0)
        body = e[8:]
        if width == 0:
            return Tile(i, 0, 0, (xhot, yhot), flags, b"")
        stride = (width + 3) // 4            # bytes per plane-row
        per_plane = len(body) // 4
        height = per_plane // stride if stride else 0
        # De-planarize Mode-X into a row-major buffer.
        out = bytearray(width * height)
        for y in range(height):
            for x in range(width):
                plane = x & 3
                col = x >> 2
                src = plane * per_plane + y * stride + col
                out[y * width + x] = body[src] if src < len(body) else TRANSPARENT
        return Tile(i, width, height, (xhot, yhot), flags, bytes(out))


def _palette_rgb(pal, idx):
    # VGA-style 0x00/0x57/0xAB/0xFF entries are already full-range here.
    return pal[idx & 0x0F]


def tile_to_image(tile, pal, bg=None):
    from PIL import Image
    if tile.width == 0 or tile.height == 0:
        return Image.new("RGBA", (1, 1), (0, 0, 0, 0))
    img = Image.new("RGBA", (tile.width, tile.height), (0, 0, 0, 0))
    px = img.load()
    for y in range(tile.height):
        for x in range(tile.width):
            v = tile.pixels[y * tile.width + x]
            if v == TRANSPARENT:
                if bg is not None:
                    px[x, y] = bg
                continue
            r, g, b = _palette_rgb(pal, v)
            px[x, y] = (r, g, b, 255)
    return img


def contact_sheet(lib, path, scale=4, cols=8, bg=(255, 0, 255, 255)):
    from PIL import Image, ImageDraw
    pad = 6
    label_h = 10
    cell_w = 16 * scale + pad * 2
    cell_h = 16 * scale + pad * 2 + label_h
    n = len(lib.tiles)
    rows = (n + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * cell_w, rows * cell_h), (40, 40, 40, 255))
    draw = ImageDraw.Draw(sheet)
    for k, tile in enumerate(lib.tiles):
        r, c = divmod(k, cols)
        ox = c * cell_w + pad
        oy = r * cell_h + pad + label_h
        img = tile_to_image(tile, lib.palette, bg=bg)
        img = img.resize((tile.width * scale, tile.height * scale), Image.NEAREST)
        sheet.alpha_composite(img, (ox, oy))
        draw.text((c * cell_w + 2, r * cell_h + 1),
                  "#%d %dx%d" % (tile.index, tile.width, tile.height),
                  fill=(230, 230, 230, 255))
    sheet.save(path)
    return sheet


def main(argv):
    ap = argparse.ArgumentParser(description="Decode FRUA DOS HLIB (*.TLB) tile libraries.")
    ap.add_argument("file")
    ap.add_argument("--sheet", metavar="PNG", help="write a contact sheet of all tiles")
    ap.add_argument("--dump", metavar="DIR", help="write each tile as DIR/NNN.png")
    ap.add_argument("--tile", type=int, help="select a single tile index")
    ap.add_argument("--png", metavar="PNG", help="with --tile: write that tile")
    ap.add_argument("--scale", type=int, default=4, help="zoom for --sheet/--png")
    args = ap.parse_args(argv)

    with open(args.file, "rb") as f:
        lib = HLib(f.read())

    print("HLIB %s  tag=%r  entries=%d  size=%d" %
          (args.file, lib.tag.decode("latin1"), lib.count, lib.size))
    print("palette:", " ".join("%02x%02x%02x" % c for c in lib.palette))
    for t in lib.tiles:
        print("  #%3d  %2dx%-2d  hotspot=%s flags=0x%04x" %
              (t.index, t.width, t.height, t.hotspot, t.flags))

    if args.sheet:
        contact_sheet(lib, args.sheet, scale=args.scale)
        print("wrote contact sheet ->", args.sheet)
    if args.dump:
        import os
        os.makedirs(args.dump, exist_ok=True)
        for t in lib.tiles:
            img = tile_to_image(t, lib.palette)
            img.save(os.path.join(args.dump, "%03d.png" % t.index))
        print("dumped %d tiles -> %s" % (len(lib.tiles), args.dump))
    if args.tile is not None and args.png:
        t = next((t for t in lib.tiles if t.index == args.tile), None)
        if t is None:
            print("no tile #%d" % args.tile, file=sys.stderr)
            return 1
        img = tile_to_image(t, lib.palette)
        img = img.resize((t.width * args.scale, t.height * args.scale))
        img.save(args.png)
        print("wrote tile #%d -> %s" % (args.tile, args.png))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
