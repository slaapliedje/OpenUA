#!/usr/bin/env python3
"""wall_extract.py — decode the FRUA dungeon wall libraries (8X8DB/8X8DC.CTL).

These are a GLIB-of-GLIBs: the top-level GLIB's N entries are each themselves a
GLIB — one wall SET (48 pieces + an item-0 palette). A piece is a raw-8bpp image
with an 8-byte header:

    0  2  height      (u16 BE)
    2  2  y-bearing   (s16 BE)
    4  2  x-bearing   (s16 BE)
    6  1  bpp_w       width = bpp_w * 8
    7  1  flags       (0xc5 => low-nibble 5 = raw 8bpp; 255 = transparent)

then width*height index bytes. The indices are DIRECT indices into the level's
shared dungeon palette (NOT each set's 37-entry item-0): e.g. in 8X8DB the stone
sets (1, 5) use ~41..60, the wood set (8) ~0..53. So a per-set palette renders
stone as out-of-range — use --gray to inspect SHAPES, or supply the real shared
CLUT to check colour.

This is reference/ingest tooling (ADR-0001/0007); it embeds no FRUA data.

Usage:
    python3 tools/wall_extract.py 8X8DB.CTL                 # set/piece summary
    python3 tools/wall_extract.py 8X8DB.CTL --sets 1,5,8 --idx 1,6,7,8,42 \
        --gray --png out.png                                # shape montage
"""
import argparse
import struct
import sys


def glib_entries(buf):
    if buf[:4] != b"GLIB":
        raise ValueError("not a GLIB (magic %r)" % buf[:4])
    count, _flags = struct.unpack_from(">HH", buf, 8)
    offs = list(struct.unpack_from(">%dI" % (count + 1), buf, 16))
    return [(offs[i], offs[i + 1]) for i in range(count)]


def piece(setbuf, idx):
    ents = glib_entries(setbuf)
    if idx >= len(ents):
        return None
    o, e = ents[idx]
    b = setbuf[o:e]
    if len(b) < 8:
        return None
    h = struct.unpack_from(">H", b, 0)[0]
    yb = struct.unpack_from(">h", b, 2)[0]
    xb = struct.unpack_from(">h", b, 4)[0]
    w = b[6] * 8
    return dict(h=h, w=w, yb=yb, xb=xb, flags=b[7], px=b[8:], nitems=len(ents))


def render(p, pal, gray):
    from PIL import Image
    w, h = p["w"], p["h"]
    img = Image.new("RGB", (max(w, 1), max(h, 1)), (255, 0, 255))
    if w == 0 or h == 0:
        return img
    ld = img.load()
    px = p["px"]
    for y in range(h):
        for x in range(w):
            o = y * w + x
            v = px[o] if o < len(px) else 255
            if v == 255:
                continue
            if gray:
                g = min(255, v * 4)
                ld[x, y] = (g, g, g)
            elif pal and v < len(pal):
                ld[x, y] = pal[v]
            else:
                ld[x, y] = (255, 0, 255)
    return img


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file")
    ap.add_argument("--sets", default="", help="comma list of set indices (default: all)")
    ap.add_argument("--idx", default="1,2,6,7,8,42", help="comma list of piece indices")
    ap.add_argument("--gray", action="store_true", help="grayscale by index (see shapes)")
    ap.add_argument("--png", help="write a montage (rows=sets, cols=idx)")
    ap.add_argument("--scale", type=int, default=3)
    args = ap.parse_args(argv)

    data = open(args.file, "rb").read()
    top = glib_entries(data)
    print("%s: %d wall sets" % (args.file, len(top)))

    sets = ([int(x) for x in args.sets.split(",")] if args.sets
            else list(range(len(top))))
    idxs = [int(x) for x in args.idx.split(",")]

    for s in sets:
        o, e = top[s]
        sb = data[o:e]
        rng = []
        for idx in idxs:
            p = piece(sb, idx)
            if not p:
                rng.append("#%d:none" % idx)
                continue
            vals = [v for v in p["px"][:p["w"] * p["h"]] if v != 255]
            rng.append("#%d:%dx%d[%s-%s]" % (idx, p["w"], p["h"],
                       min(vals) if vals else "-", max(vals) if vals else "-"))
        print("  set %d (%d items): %s" % (s, piece(sb, 0)["nitems"] if piece(sb, 0) else 0,
                                           "  ".join(rng)))

    if args.png:
        from PIL import Image, ImageDraw
        S = args.scale
        cw, ch = 64 * S, 64 * S
        mont = Image.new("RGB", (len(idxs) * cw, len(sets) * ch), (30, 30, 30))
        dr = ImageDraw.Draw(mont)
        for si, s in enumerate(sets):
            o, e = top[s]
            sb = data[o:e]
            for ii, idx in enumerate(idxs):
                p = piece(sb, idx)
                if not p:
                    continue
                img = render(p, None, args.gray).resize(
                    (p["w"] * S, p["h"] * S), Image.NEAREST)
                mont.paste(img, (ii * cw, si * ch + 12))
                dr.text((ii * cw + 2, si * ch + 1), "s%d#%d" % (s, idx),
                        fill=(230, 230, 230))
        mont.save(args.png)
        print("wrote", args.png)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
