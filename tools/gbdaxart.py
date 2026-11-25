"""gbdaxart.py — Champions' and Death Knights' pictures (.DAX) as the picture
libraries FRUA's engine draws (ADR-0026: the games' own files, read as they
are kept).

THE BLOCKS. Every block of PIC*.DAX (the 88x88 event pictures) and SPRIT*.DAX
(the approaching monsters) is

    u8      frames
    then per frame:
      u32   delay           the animation's timing for the frame (0: still)
      u16   height          the standard 17-byte frame header
      u16   width / 8         (docs/dax-format.md 4a; that document read
      u16   x, y              these blocks 5 bytes early and called them an
      u8    items             unsolved "complex" variant)
      u8[8] (not used here)
      pixels                height * width / 2 bytes, 4 bits a pixel, the
                            high nibble first, indices into the EGA palette

which accounts for every PIC and SPRIT block of both games to the byte. A
sprite has three frames, the monster far, middle and near, x and y placing
each in 8-pixel units.

THE LIBRARY written is a Mac GLIB in the shape the Dark Queen's converted
PICA.CTL has (tools/art_convert.py): an id table, then per picture a nested
"TILE" library whose entry 0 is the palette (type 8: the 16 EGA colours at
CLUT 32..47) and whose further entries are the frames, run-coded a row at a
time (method 18, type 2, art_convert.packbits_row) with pixel = 32 + index.

    python3 tools/gbdaxart.py PIC1.DAX [PIC2.DAX ...] -o PICA.CTL
    python3 tools/gbdaxart.py BIGPIC?.DAX --shift 128 -o BIGPIC.CTL

The game data is the user's own; nothing here ships any.
"""
import struct
import sys

import art_convert
import ecl

EGA = ((0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0),
       (170, 0, 170), (170, 85, 0), (170, 170, 170), (85, 85, 85),
       (85, 85, 255), (85, 255, 85), (85, 255, 255), (255, 85, 85),
       (255, 85, 255), (255, 255, 85), (255, 255, 255))
BASE = 32                   # the CLUT entry EGA colour 0 goes to


class DaxArtError(ValueError):
    pass


def frames(block):
    """[(delay, height, width, x, y, [index per pixel, row-major])]. A block
    that is one plain frame (a big picture: the 17-byte header, no frame
    count) is read as such."""
    if not block:
        raise DaxArtError("an empty block")
    if len(block) >= 17:
        h, w8, x, y = struct.unpack_from("<HHHH", block, 0)
        if block[8] == 1 and h and w8 and len(block) == 17 + h * w8 * 4:
            px = []
            for b in block[17:]:
                px += (b >> 4, b & 15)
            return [(0, h, 8 * w8, x, y, px)]
    out, p = [], 1
    for _ in range(block[0]):
        if p + 21 > len(block):
            raise DaxArtError("a frame header runs past the block")
        delay, = struct.unpack_from("<I", block, p)
        h, w8, x, y = struct.unpack_from("<HHHH", block, p + 4)
        items = block[p + 12]
        p += 21
        w = 8 * w8
        n = h * w // 2 * max(items, 1)
        if items != 1 or not (h and w) or p + n > len(block):
            raise DaxArtError("not a frame this reads (%dx%d, %d items)" % (w, h, items))
        px = []
        for b in block[p:p + n]:
            px += (b >> 4, b & 15)
        out.append((delay, h, w, x, y, px))
        p += n
    if p != len(block):
        raise DaxArtError("%d bytes after the frames" % (len(block) - p))
    return out


def _glib(entries, tag, id_table=0):
    """A Mac GLIB of `entries` (bytes each)."""
    off, offs = 16 + 4 * (len(entries) + 1), []
    for e in entries:
        offs.append(off)
        off += len(e)
    offs.append(off)
    return (b"GLIB" + struct.pack(">IH", off, len(entries))
            + bytes([0, id_table]) + tag
            + struct.pack(">%dI" % len(offs), *offs) + b"".join(entries))


def palette_entry():
    """Type 8: flags 5 (start and count given, no cycle ranges), CLUT 32..47."""
    return (struct.pack(">HHH", 5, BASE, len(EGA)) + bytes([0, 0xc8])
            + b"".join(bytes(c) for c in EGA))


def frame_entry(h, w, px):
    """Method 18 (type 2), rows packed one at a time, even-padded."""
    body = b"".join(art_convert.packbits_row(bytes(BASE + v for v in px[y * w:(y + 1) * w]))
                    for y in range(h))
    if len(body) % 2:
        body += b"\x00"
    return struct.pack(">Hhh", h, 0, 0) + bytes([w // 8, 0xc2]) + body


def picture(block):
    """One picture: its nested TILE library."""
    return _glib([palette_entry()] + [frame_entry(h, w, px)
                                      for _, h, w, _, _, px in frames(block)], b"TILE")


def library(blocks, shift=0):
    """[(id, block)] -> the picture library, in that order, each id `shift`
    higher (the big pictures, 112.., go to FRUA's 240..)."""
    table = struct.pack(">H", len(blocks)) + b"".join(
        struct.pack(">HH", i + shift, k + 1) for k, (i, _) in enumerate(blocks))
    return _glib([table] + [picture(b) for _, b in blocks], b"GLIB", 1)


def archive_blocks(paths):
    """The blocks of the archives in their order, an id seen before left out
    (the engine's order: each archive read once, front to back)."""
    out, seen = [], set()
    for path in paths:
        d = open(path, "rb").read()
        toc = struct.unpack_from("<H", d, 0)[0]
        blocks = ecl.dax_blocks(d)
        for k in range(toc // 9):
            bid = d[2 + 9 * k]
            if bid not in seen:
                seen.add(bid)
                out.append((bid, blocks[bid]))
    return out


def main(argv):
    if "-o" not in argv:
        print(__doc__)
        return 2
    out = argv[argv.index("-o") + 1]
    shift = int(argv[argv.index("--shift") + 1]) if "--shift" in argv else 0
    blocks = archive_blocks([a for a in argv[:argv.index("-o")]
                             if a.upper().endswith(".DAX")])
    with open(out, "wb") as fh:
        fh.write(library(blocks, shift))
    print("%s: %d pictures" % (out, len(blocks)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
