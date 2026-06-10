#!/usr/bin/env python3
"""hlib_extract.py — decode FRUA tile/art libraries (*.TLB, *.GLB, *.CTL).

Both Unlimited Adventures releases store UI/art tiles in the same container,
under two endian-flipped labels:

  - DOS  "HLIB"  — little-endian; tiles are 8bpp colour (VGA Mode-X).
  - Mac  "GLIB"  — big-endian;    tiles are 1bpp data+mask (UI/cursor libs)
                                   or piece-coded art (walls/sprites, TODO).

The shared header (offsets are u32 in the file's endianness):

    0   4   magic              'HLIB' (DOS) or 'GLIB' (Mac)
    4   4   total file size
    8   2   entry count (N)
    10  2   flags / version    (0 in the libraries seen)
    12  4   content tag        'TILE' (tile lib) or 'DATA' (.GLB data lib)
    16  4*(N+1)  offset table; entry i spans [off[i], off[i+1]), off[N]=EOF.

    entry 0 : 16-colour palette — 8-byte header, then 16 RGB triples.
    entry k : a tile — 8-byte header (u16 width, u16 xhot, u16 yhot,
              u16 flags), then the pixels:
                HLIB: VGA Mode-X (4 unchained planes, plane p = columns
                      x&3==p, stride=ceil(width/4)); each byte an 8-bit
                      palette index, 0xFF = transparent.
                GLIB: a 1bpp data plane then a 1bpp mask plane, each
                      ceil(width/16) words per row; data bit = black/white,
                      mask bit = opaque (mask 0 = transparent).

This is reference/ingest tooling (the port ships from the Mac fork per
ADR-0001/0007); it embeds no FRUA data. Designs (.DSN/.DAT) are identical
across both releases and need no conversion.

Usage:
    python3 tools/hlib_extract.py LIB.TLB                 # summary
    python3 tools/hlib_extract.py LIB.TLB --sheet out.png # contact sheet
    python3 tools/hlib_extract.py LIB.TLB --dump outdir   # per-tile PNGs
    python3 tools/hlib_extract.py LIB.TLB --tile 7 --png t7.png
"""

import argparse
import struct
import sys


MAGIC_DOS = b"HLIB"
MAGIC_MAC = b"GLIB"
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
    """A FRUA tile/art library — DOS 'HLIB' (LE) or Mac 'GLIB' (BE)."""

    def __init__(self, data):
        magic = data[:4]
        if magic == MAGIC_DOS:
            self.kind = "HLIB"
            self.endian = "<"
        elif magic == MAGIC_MAC:
            self.kind = "GLIB"
            self.endian = ">"
        else:
            raise ValueError("not an HLIB/GLIB file (bad magic %r)" % magic)
        e = self.endian
        (self.size,) = struct.unpack_from(e + "I", data, 4)
        self.count, self.flags = struct.unpack_from(e + "HH", data, 8)
        self.tag = data[12:16]
        self.offsets = list(
            struct.unpack_from(e + "%dI" % (self.count + 1), data, 16)
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
        width, xhot, yhot, flags = struct.unpack_from(self.endian + "HHHH", e, 0)
        body = e[8:]
        if width == 0:
            return Tile(i, 0, 0, (xhot, yhot), flags, b"")
        if self.kind == "HLIB":
            return self._tile_modex(i, width, xhot, yhot, flags, body)
        return self._tile_mono(i, width, xhot, yhot, flags, body)

    def _tile_modex(self, i, width, xhot, yhot, flags, body):
        """DOS: VGA Mode-X (4 unchained planes) -> row-major 8bpp indices."""
        stride = (width + 3) // 4            # bytes per plane-row
        per_plane = len(body) // 4
        height = per_plane // stride if stride else 0
        out = bytearray(width * height)
        for y in range(height):
            for x in range(width):
                src = (x & 3) * per_plane + y * stride + (x >> 2)
                out[y * width + x] = body[src] if src < len(body) else TRANSPARENT
        return Tile(i, width, height, (xhot, yhot), flags, bytes(out))

    def _tile_mono(self, i, width, xhot, yhot, flags, body):
        """Mac: 1bpp data plane + 1bpp mask plane -> row-major indices,
        mapping data bit to black(0)/white(15) and a clear mask bit to
        transparent (0xFF). words_per_row = ceil(width/16)."""
        wpr = (width + 15) // 16
        rowbytes = wpr * 2
        height = len(body) // (2 * rowbytes) if rowbytes else 0
        data = body[:rowbytes * height]
        mask = body[rowbytes * height:2 * rowbytes * height]
        out = bytearray([TRANSPARENT]) * (width * height)
        for y in range(height):
            for x in range(width):
                word = x >> 4
                bit = 0x8000 >> (x & 15)
                off = y * rowbytes + word * 2
                d = struct.unpack_from(">H", data, off)[0]
                m = struct.unpack_from(">H", mask, off)[0]
                if m & bit:
                    out[y * width + x] = 0 if (d & bit) else 15
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

    print("%s %s  tag=%r  entries=%d  size=%d" %
          (lib.kind, args.file, lib.tag.decode("latin1"), lib.count, lib.size))
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
