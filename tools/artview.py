#!/usr/bin/env python3
"""Render FRUA art (GLIB `.ctl` / HLIB `.TLB`) to PNG, offline and at full
256 colours.

WHY THIS EXISTS. When a screen looks wrong on a 16/32-colour machine, there
are two very different explanations — the port's colour reduction mangled it,
or the pixels were always like that in SSI's data — and an emulator screenshot
cannot tell them apart, because everything it shows has already been through
the quantiser. This renders the art the way a 256-colour machine would, from
the file, with no engine and no emulator in the loop. If a mark is here it is
in the data; if it is not, the port put it there.

It reuses `perband.py`'s decoders rather than reimplementing the codecs — the
same PackBits / transparency-RLE / raw-8bpp arms, extended to keep the pixel
POSITIONS that the colour-counting versions throw away.

    python3 tools/artview.py ART.TLB --info
    python3 tools/artview.py ART.TLB --item 3 -o out.png
    python3 tools/artview.py ART.TLB --all --out-dir shots/

`--info` prints the container tree (entries, codecs, sizes) so you can find
the picture you want; `--item N` renders one; `--all` renders every entry that
decodes.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import art_convert                                            # noqa: E402
import perband                                                # noqa: E402
import prequant                                               # noqa: E402

TRANSPARENT = -1


def as_glib(data):
    """Accept either container; HLIB is converted in memory (see perband)."""
    if data[:4] == prequant.GLIB:
        return data
    if data[:4] == b"HLIB":
        return art_convert.convert(data, to=prequant.GLIB)
    raise SystemExit("not a GLIB/HLIB container")


# --- pixel-preserving twins of perband's row decoders ------------------------
#
# perband returns {row: set(indices)} because it only needs to know how many
# distinct colours a band uses. Rendering needs WHERE each index landed, so
# these return {row: {x: index}}. The control flow is deliberately identical
# to its counterpart, so a fix to one is easy to mirror in the other.

def px_mode2(body, pix_w, height):
    """PackBits 8bpp rows; 255 = transparent."""
    rows, pos = {}, 0
    for r in range(height):
        row, pos = perband.unpackbits(body, pos, pix_w)
        d = {x: v for x, v in enumerate(row[:pix_w]) if v != 255}
        if d:
            rows[r] = d
    return rows


def px_raw8(body, pix_w, height):
    """Raw 8bpp rows, stride = pix_w, 255 = transparent (modes 0 and 5)."""
    rows = {}
    for r in range(height):
        row = body[r * pix_w:(r + 1) * pix_w]
        if not row:
            break
        d = {x: v for x, v in enumerate(row) if v != 255}
        if d:
            rows[r] = d
    return rows


def px_mode7(body, pix_w, height):
    """Transparency RLE: 0 ends the row, 1..127 literals, >=128 skips 257-b.
    Index 0 is transparent."""
    rows, pos, x, y = {}, 0, 0, 0
    n = len(body)
    while y < height and pos < n:
        b = body[pos]
        pos += 1
        if b == 0:
            y += 1
            x = 0
        elif b < 128:
            for _ in range(b):
                if pos < n:
                    v = body[pos]
                    pos += 1
                    if v and x < pix_w:
                        rows.setdefault(y, {})[x] = v
                x += 1
        else:
            x += 257 - b
    return rows


def piece_pixels(sub, idx, y_off=0, x_off=0, depth=0, out=None):
    """Paint entry `idx` of a picture's sub-GLIB into {(x, y): index},
    following mode-9 composites. Mirrors perband.piece_rows."""
    if out is None:
        out = {}
    if depth > 4:
        raise perband.Undecodable("composite nesting")
    top = prequant.parse_glib(sub)
    if top is None:
        raise perband.Undecodable("not a GLIB")
    cnt, offs = top
    if idx >= cnt:
        raise perband.Undecodable("index out of range")
    ent = sub[offs[idx]:offs[idx + 1]]
    if len(ent) < 8:
        raise perband.Undecodable("truncated entry")

    height = struct.unpack(">H", ent[0:2])[0]
    ybear = struct.unpack(">h", ent[2:4])[0]
    xbear = struct.unpack(">h", ent[4:6])[0]
    flags = ent[7]
    mode = flags & 15
    pix_w = ent[6] * 8
    body = ent[8:]
    y0 = y_off - ybear
    x0 = x_off - xbear

    if mode == 9:
        # 6-byte sub-records: [0] child, [1] total, [2:4] dy, [4:6] dx.
        # The FIRST record carries the total, so `count` is re-read each
        # pass exactly as the engine does.
        count, i, p = 1, 0, 0
        while i < count and p + 6 <= len(body):
            sub_rec = body[p:p + 6]
            p += 6
            dy = struct.unpack(">h", sub_rec[2:4])[0]
            dx = struct.unpack(">h", sub_rec[4:6])[0]
            piece_pixels(sub, sub_rec[0], y0 + dy, x0 + dx, depth + 1, out)
            count = sub_rec[1]
            i += 1
        return out
    if mode == 8:                                  # palette block, no pixels
        return out
    if mode == 2 and (flags & 0x40):
        raw = px_mode2(body, pix_w, height)
    elif mode == 7:
        raw = px_mode7(body, pix_w, height)
    elif flags & 0x40:
        raw = px_raw8(body, pix_w, height)
    else:
        # 1bpp mono, drawn in the engine's CURRENT PEN — there is no pen
        # here, so it is skipped rather than guessed at.
        return out
    for r, d in raw.items():
        for x, v in d.items():
            out[(x0 + x, y0 + r)] = v
    return out


def clut_of(entry_bytes, base_clut=None):
    """Build a 256-entry CLUT from every type-8 palette block in this entry.

    jt993's law (see prequant.pal_window): hdr[1] bit0 = an explicit window
    with start/count at +2/+4, and the RGB payload follows at +8. Blocks are
    applied in order, so a later one overrides an earlier one exactly as the
    engine's successive installs do."""
    clut = list(base_clut) if base_clut else [(0, 0, 0)] * 256
    found = []
    prequant.walk_palettes(entry_bytes, 0, found)
    for off, start, count, _ncopy in found:
        rgb = entry_bytes[off + 8:off + 8 + count * 3]
        for i in range(count):
            if start + i < 256 and (i * 3 + 2) < len(rgb):
                clut[start + i] = (rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2])
    return clut, len(found)


def render_compose(glib, idxs, transparent_rgb=(0, 0, 0), canvas=None):
    """Composite several top-level entries into ONE image, in order.

    A title screen on screen is not one entry: l19d4 loads "<name>1" and blits
    it, then RELOADS the same library with the caller's mode and blits that
    over it — a base plus an overlay. Comparing a screenshot against the base
    alone therefore shows the whole overlay as a difference, which looks like
    corruption and is not. Pieces carry their own bearings, so painting each
    entry at its own absolute coordinates reproduces the composed screen.

    The palette ACCUMULATES across entries: each entry's type-8 blocks are
    applied in turn over the running CLUT, so a later window overrides an
    earlier one exactly as the engine's successive installs do. Rebuilding it
    per entry instead would lose the base's colours the moment an overlay
    installs its own window."""
    from PIL import Image

    cnt, offs = prequant.parse_glib(glib)
    clut = [(0, 0, 0)] * 256
    pix = {}
    for i in idxs:
        ent = glib[offs[i]:offs[i + 1]]
        if prequant.parse_glib(ent) is None:
            continue
        clut, _ = clut_of(ent, clut)
        sub_cnt, _ = prequant.parse_glib(ent)
        for j in range(sub_cnt):
            try:
                piece_pixels(ent, j, 0, 0, 0, pix)
            except perband.Undecodable:
                continue
    if not pix:
        return None, "no decodable pixels"
    if canvas:
        w, h = canvas
        x0 = y0 = 0
    else:
        xs = [p[0] for p in pix]
        ys = [p[1] for p in pix]
        x0, y0 = min(xs), min(ys)
        w, h = max(xs) - x0 + 1, max(ys) - y0 + 1
    im = Image.new("RGB", (w, h), transparent_rgb)
    px = im.load()
    for (x, y), v in pix.items():
        if 0 <= x - x0 < w and 0 <= y - y0 < h:
            px[x - x0, y - y0] = clut[v]
    return im, "%dx%d  entries=%s  colours=%d" % (w, h, list(idxs),
                                                  len({v for v in pix.values()}))


def render_entry(glib, i, transparent_rgb=(0, 0, 0)):
    """-> (PIL.Image, info string) for top-level entry i, or (None, why)."""
    from PIL import Image

    cnt, offs = prequant.parse_glib(glib)
    ent = glib[offs[i]:offs[i + 1]]
    if prequant.parse_glib(ent) is None:
        return None, "not a picture (no sub-GLIB)"

    clut, npal = clut_of(ent)
    sub_cnt, _ = prequant.parse_glib(ent)

    pix = {}
    drawn = 0
    for j in range(sub_cnt):
        try:
            piece_pixels(ent, j, 0, 0, 0, pix)
            drawn += 1
        except perband.Undecodable:
            continue
    if not pix:
        return None, "no decodable pixels"

    xs = [p[0] for p in pix]
    ys = [p[1] for p in pix]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    w, h = x1 - x0 + 1, y1 - y0 + 1
    im = Image.new("RGB", (w, h), transparent_rgb)
    px = im.load()
    for (x, y), v in pix.items():
        px[x - x0, y - y0] = clut[v]
    return im, ("%dx%d  pieces=%d/%d  palette-blocks=%d  colours=%d"
                % (w, h, drawn, sub_cnt, npal, len({v for v in pix.values()})))


def info(glib):
    cnt, offs = prequant.parse_glib(glib)
    print("top-level entries: %d" % cnt)
    for i in range(cnt):
        ent = glib[offs[i]:offs[i + 1]]
        size = offs[i + 1] - offs[i]
        sub = prequant.parse_glib(ent)
        if sub is None:
            if len(ent) >= 8:
                flags = ent[7]
                print("  %3d: leaf %7d B  mode=%-2d flags=0x%02x %dx%d"
                      % (i, size, flags & 15, flags,
                         ent[6] * 8, struct.unpack(">H", ent[0:2])[0]))
            else:
                print("  %3d: leaf %7d B  (short)" % (i, size))
            continue
        scnt, soffs = sub
        modes = []
        for j in range(scnt):
            se = ent[soffs[j]:soffs[j + 1]]
            modes.append(se[7] & 15 if len(se) >= 8 else -1)
        pals = []
        prequant.walk_palettes(ent, 0, pals)
        print("  %3d: PICTURE %7d B  pieces=%d modes=%s palettes=%d"
              % (i, size, scnt, sorted(set(modes)), len(pals)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("art", help="a .ctl (GLIB) or .TLB (HLIB) library")
    ap.add_argument("--item", type=int, help="render this top-level entry")
    ap.add_argument("--all", action="store_true", help="render every entry")
    ap.add_argument("-o", "--out", help="output PNG (with --item)")
    ap.add_argument("--out-dir", default=".", help="output dir (with --all)")
    ap.add_argument("--info", action="store_true", help="print the tree")
    ap.add_argument("--compose", help="composite entries, e.g. 1,4 (base,overlay)")
    ap.add_argument("--canvas", help="force a canvas size, e.g. 320x200")
    ap.add_argument("--scale", type=int, default=1, help="nearest-neighbour zoom")
    args = ap.parse_args()

    glib = as_glib(open(args.art, "rb").read())
    if args.compose:
        from PIL import Image
        idxs = [int(v) for v in args.compose.split(",")]
        canvas = None
        if args.canvas:
            canvas = tuple(int(v) for v in args.canvas.lower().split("x"))
        im, note = render_compose(glib, idxs, canvas=canvas)
        if im is None:
            print(note)
            return 1
        if args.scale > 1:
            im = im.resize((im.width * args.scale, im.height * args.scale),
                           Image.NEAREST)
        im.save(args.out or "compose.png")
        print("composed -> %s   %s" % (args.out or "compose.png", note))
        return 0
    if args.info or (args.item is None and not args.all):
        info(glib)
        return 0

    from PIL import Image
    cnt, _ = prequant.parse_glib(glib)
    todo = range(cnt) if args.all else [args.item]
    for i in todo:
        im, note = render_entry(glib, i)
        if im is None:
            if not args.all:
                print("entry %d: %s" % (i, note))
            continue
        if args.scale > 1:
            im = im.resize((im.width * args.scale, im.height * args.scale),
                           Image.NEAREST)
        out = (args.out if (args.out and not args.all)
               else os.path.join(args.out_dir,
                                 "%s_%03d.png"
                                 % (os.path.splitext(os.path.basename(args.art))[0], i)))
        im.save(out)
        print("entry %3d -> %s   %s" % (i, out, note))
    return 0


if __name__ == "__main__":
    sys.exit(main())
