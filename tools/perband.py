#!/usr/bin/env python3
"""perband.py — offline art reduction to the PER-BAND colour budget
(ADR-0020 v2).

WHY THIS AND NOT prequant.py's GLOBAL PALETTE. The bitplane backends do not
paint through one palette per frame: Amiga ECS runs 25 copper bands of 32
colours each (8 scanlines per band) and the ST runs a Timer-B viewport
split. Reducing the art to a single 32-colour palette therefore throws that
capability away — measured on the ECS titles, it banded a red gradient into
hard stripes and looked visibly worse than what the runtime quantizer
already produces (ADR-0020 addendum).

The right target is not "N colours per picture". It is:

    no 8-SCANLINE STRIP of the picture may need more than `budget`
    distinct colours

because that is exactly the constraint the hardware imposes. A picture that
satisfies it can carry FAR more than 32 colours overall — different strips
spend their 32 on different colours, which is how Amiga "rainbow" art and
SSI's own hand-authored ST art work — and every band then quantizes
EXACTLY: nothing is merged at runtime, so there are no per-band seams and no
nearest-luma stray pixels. The runtime quantizer is left with nothing to do.

HOW, WITHOUT TOUCHING A SINGLE PIXEL. A strip's cost is the number of
distinct RGB VALUES its indices resolve to, not the number of indices. So
the budget can be met by MERGING PALETTE ENTRIES alone: give two indices the
same RGB and they cost one colour wherever they co-occur. Pixels, offsets,
codecs and structure are untouched — the same by-construction-valid rewrite
prequant.py v1 proved, and the unmodified engine plays the result.

The picture lands in the viewport hole at screen y=24, which is a multiple
of the 8-row band height, so a picture row r sits in band (24+r)/8 and the
picture's own 8-row strips align with the hardware's bands exactly.

BUDGET. Pass less than the hardware's 32: a screen band also carries frame
chrome, the roster and the text box. The default 24 leaves 8 for those.

USAGE
    python3 tools/perband.py GAMEDATA --report                  # measure only
    python3 tools/perband.py GAMEDATA --out DST --budget 24     # convert
    python3 tools/perband.py GAMEDATA --out DST --budget 12 --rows 8   # ST

Only decodable colour pieces are counted (mode 2 with flags 0x40 = PackBits
8bpp rows, mode 7 = transparency RLE, mode 9 = composite of those). Pieces
in codecs this tool cannot read are REPORTED and the picture is left alone
rather than converted on partial information.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prequant  # noqa: E402  (container + type-8 palette parsing)
import art_convert  # noqa: E402  (HLIB <-> GLIB, for DOS-format art)

GLIB = b"GLIB"


# --- pixel decoding ---------------------------------------------------------

def unpackbits(data, pos, want):
    """Mac _UnpackBits: -> (bytes, newpos). Emits at least `want` bytes."""
    out = bytearray()
    n = len(data)
    while len(out) < want and pos < n:
        b = data[pos]
        pos += 1
        if b < 128:                       # b+1 literals
            k = b + 1
            out += data[pos:pos + k]
            pos += k
        elif b > 128:                     # 257-b copies of the next byte
            if pos >= n:
                break
            out += bytes([data[pos]]) * (257 - b)
            pos += 1
        # b == 128 is a no-op
    return bytes(out), pos


def decode_mode2(body, pix_w, height):
    """PackBits 8bpp rows; 255 = transparent. -> {row: set(indices)}."""
    rows, pos = {}, 0
    for r in range(height):
        row, pos = unpackbits(body, pos, pix_w)
        s = set(row[:pix_w])
        s.discard(255)
        if s:
            rows[r] = s
    return rows


def decode_raw8(body, pix_w, height):
    """Raw 8bpp rows, stride = pix_w, 255 = transparent. This is l2d4e's
    fall-through arm (`else if (flags & 0x40)`), which is what modes 0 and 5
    take — mode 5 being the 8x8 dungeon wall tiles."""
    rows = {}
    for r in range(height):
        row = body[r * pix_w:(r + 1) * pix_w]
        if not row:
            break
        s = set(row)
        s.discard(255)
        if s:
            rows[r] = s
    return rows


def decode_mode7(body, pix_w, height):
    """Transparency RLE (engine decode_glib_t7): 0 = end of row, 1..127 =
    that many literals, >=128 = skip 257-b transparent. 0 = transparent."""
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
                        rows.setdefault(y, set()).add(v)
                x += 1
        else:
            x += 257 - b
    return rows


class Undecodable(Exception):
    pass


def piece_rows(sub_glib, idx, y_off, depth=0):
    """Decode entry `idx` of a picture's sub-GLIB into {abs_row: set(idx)},
    following composites. Raises Undecodable on a codec we cannot read."""
    if depth > 4:
        raise Undecodable("composite nesting")
    top = prequant.parse_glib(sub_glib)
    if top is None:
        raise Undecodable("not a GLIB")
    cnt, offs = top
    if idx >= cnt:
        raise Undecodable("index out of range")
    ent = sub_glib[offs[idx]:offs[idx + 1]]
    if len(ent) < 8:
        raise Undecodable("truncated entry")
    height = struct.unpack(">H", ent[0:2])[0]
    ybear = struct.unpack(">h", ent[2:4])[0]
    flags = ent[7]
    mode = flags & 15
    pix_w = ent[6] * 8
    body = ent[8:]
    y0 = y_off - ybear

    if mode == 9:                          # composite: 6-byte sub-records
        out, count, i, p = {}, 1, 0, 0
        while i < count and p + 6 <= len(body):
            sub = body[p:p + 6]
            p += 6
            dy = struct.unpack(">h", sub[2:4])[0]
            for r, s in piece_rows(sub_glib, sub[0], y0 + dy, depth + 1).items():
                out.setdefault(r, set()).update(s)
            count = sub[1]
            i += 1
        return out
    if mode == 8:                          # palette block: no pixels
        return {}
    if mode == 2 and (flags & 0x40):
        raw = decode_mode2(body, pix_w, height)
    elif mode == 7:
        raw = decode_mode7(body, pix_w, height)
    elif flags & 0x40:
        # l2d4e's fall-through: raw 8bpp rows. Modes 0 and 5 land here.
        raw = decode_raw8(body, pix_w, height)
    else:
        # No 0x40 = a 1bpp MONO piece, expanded through the CURRENT PEN —
        # one colour, wherever it lands. It cannot push a band over its
        # budget on its own, so it contributes nothing to the constraint
        # and must NOT disqualify the picture (doing so skipped every
        # library that mixes mono chrome with colour art).
        return {}
    return {y0 + r: s for r, s in raw.items()}


# --- the per-band constraint ------------------------------------------------

def strips_of(rowsets, rows_per_band, screen_y):
    """Group per-row index sets into band-aligned strips. screen_y is where
    row 0 lands on screen, so strips line up with the hardware's bands."""
    out = {}
    for r, s in rowsets.items():
        out.setdefault((screen_y + r) // rows_per_band, set()).update(s)
    return list(out.values())


def coldist(a, b):
    dr, dg, db = a[0] - b[0], a[1] - b[1], a[2] - b[2]
    return dr * dr + dg * dg * 2 + db * db      # green-weighted, like the engine


def merge_to_budget(colours, strips, budget):
    """colours: {index: (r,g,b)}. strips: [set(index)]. Merge palette entries
    (globally) until no strip needs more than `budget` distinct colours.

    Each merge picks the closest pair of colours WITHIN some over-budget
    strip, so the fidelity is spent exactly where the constraint binds;
    everywhere else the palette is untouched. -> {index: (r,g,b)}."""
    # union-find over indices that share a colour
    rep = {i: i for i in colours}

    def find(i):
        while rep[i] != i:
            rep[i] = rep[rep[i]]
            i = rep[i]
        return i

    cur = dict(colours)
    weight = {i: 1 for i in colours}
    merges = 0
    while True:
        over = None
        for st in strips:
            # ★ find(i) ALWAYS resolves to a live root, so count every
            # index in the strip. Gating this on `i in cur` (as the first
            # cut did) drops indices that were merged INTO another group,
            # which makes an over-budget strip look compliant and exits
            # the loop early — the tool then reported bands still over
            # budget after "converting" them.
            groups = {find(i) for i in st}
            if len(groups) > budget:
                over = (st, len(groups))
                break
        if over is None:
            break
        st, _n = over
        groups = sorted({find(i) for i in st})
        best, bd = None, None
        for a in range(len(groups)):
            for b in range(a + 1, len(groups)):
                d = coldist(cur[groups[a]], cur[groups[b]])
                if bd is None or d < bd:
                    bd, best = d, (groups[a], groups[b])
        if best is None:
            break
        ga, gb = best
        wa, wb = weight[ga], weight[gb]
        ca, cb = cur[ga], cur[gb]
        mixed = tuple((ca[k] * wa + cb[k] * wb) // (wa + wb) for k in range(3))
        rep[gb] = ga
        cur[ga] = mixed
        weight[ga] = wa + wb
        del cur[gb]
        merges += 1
        if merges > 4096:                  # safety valve; never hit in practice
            break
    return {i: cur[find(i)] for i in colours}, merges


# --- per-picture driver -----------------------------------------------------

VIEWPORT_Y = 24        # where a picture lands on screen (a multiple of 8)


def convert_file(data, budget, rows_per_band, screen_y, stats):
    """-> new bytes. Rewrites each picture's palette so no band exceeds
    `budget` colours. Pictures with an unreadable piece are left alone."""
    buf = bytearray(data)
    top = prequant.parse_glib(data)
    if top is None:
        return bytes(buf)
    cnt, offs = top
    for i in range(cnt):
        item = data[offs[i]:offs[i + 1]]
        if item[:4] != GLIB:
            continue
        sub = prequant.parse_glib(item)
        if sub is None:
            continue
        scnt, soffs = sub
        pal_at = None
        for j in range(scnt):
            ent = item[soffs[j]:soffs[j + 1]]
            w = prequant.pal_window(ent)
            if w is not None:
                pal_at = (j, w)
                break
        if pal_at is None:
            continue
        j, (start, count, _ncopy) = pal_at

        rowsets, bad = {}, None
        for k in range(scnt):
            if k == j:
                continue
            try:
                for r, s in piece_rows(item, k, screen_y).items():
                    rowsets.setdefault(r, set()).update(s)
            except Undecodable as e:
                bad = str(e)
                break
        if bad is not None:
            stats["skipped"] += 1
            stats["reasons"].setdefault(bad, 0)
            stats["reasons"][bad] += 1
            continue
        if not rowsets:
            continue

        pbase = offs[i] + soffs[j] + 8
        colours = {}
        for n in range(count):
            colours[start + n] = tuple(buf[pbase + n * 3:pbase + n * 3 + 3])
        # only indices this picture actually uses, and that the palette covers
        strips = [set(x for x in st if x in colours)
                  for st in strips_of(rowsets, rows_per_band, screen_y)]
        strips = [s for s in strips if s]
        if not strips:
            continue

        before = max(len({colours[x] for x in st}) for st in strips)
        newcol, merges = merge_to_budget(colours, strips, budget)
        after = max(len({newcol[x] for x in st}) for st in strips)

        err = 0.0
        for n in range(count):
            idx = start + n
            a, b = colours[idx], newcol[idx]
            err += sum((a[k] - b[k]) ** 2 for k in range(3)) / 3.0
            buf[pbase + n * 3:pbase + n * 3 + 3] = bytes(b)
        stats["pics"] += 1
        stats["merges"] += merges
        stats["err"] += err
        stats["slots"] += count
        stats["before"] = max(stats["before"], before)
        stats["after"] = max(stats["after"], after)
        stats["overfull_before"] += sum(
            1 for st in strips if len({colours[x] for x in st}) > budget)
        stats["overfull_after"] += sum(
            1 for st in strips if len({newcol[x] for x in st}) > budget)
    return bytes(buf)


def convert_any(data, budget, rows_per_band, screen_y, stats):
    """Convert either container format.

    The shipping data disks carry SSI's DOS `.TLB` (HLIB) art and the engine
    derives the Mac `.ctl` (GLIB) twin on first touch (ADR-0014/0019), so the
    converter has to reach the DOS files or the per-band palettes never get
    to the machine.

    An HLIB palette entry holds the SAME RGB payload at the same +8 offset as
    its GLIB twin — only the container endianness and the header word order
    differ, and `_convert_entry` passes colour tables through verbatim. So:
    convert to GLIB IN MEMORY, do the whole analysis with the decoders that
    are already proven there, and then write the merged triples back into the
    ORIGINAL bytes. Entry order and count are preserved by the conversion, so
    a palette found at (item i, sub j) in the GLIB view is at (item i, sub j)
    in the HLIB source, and the RGB bytes land at the same offset within it.
    """
    if data[:4] == prequant.GLIB:
        return convert_file(data, budget, rows_per_band, screen_y, stats)
    if data[:4] != b"HLIB":
        return data
    try:
        glib = art_convert.convert(data, to=prequant.GLIB)
    except Exception as e:                     # unreadable/odd container
        stats["skipped"] += 1
        stats["reasons"].setdefault("hlib: %s" % e, 0)
        stats["reasons"]["hlib: %s" % e] += 1
        return data
    newglib = convert_file(glib, budget, rows_per_band, screen_y, stats)
    if newglib == glib:
        return data                            # nothing changed
    # Map every rewritten palette back onto the HLIB source by position.
    out = bytearray(data)
    gtop, htop = prequant.parse_glib(glib), art_convert.parse(data)
    if gtop is None:
        return data
    gcnt, goffs = gtop
    hoffs = htop["offsets"]
    if htop["count"] != gcnt:
        return data
    for i in range(gcnt):
        gitem = glib[goffs[i]:goffs[i + 1]]
        nitem = newglib[goffs[i]:goffs[i + 1]]
        if gitem == nitem or gitem[:4] != prequant.GLIB:
            continue
        gsub = prequant.parse_glib(gitem)
        hsub = art_convert.parse(data[hoffs[i]:hoffs[i + 1]])
        if gsub is None or hsub["count"] != gsub[0]:
            continue
        scnt, soffs = gsub
        hsoffs = hsub["offsets"]
        for j in range(scnt):
            ge = gitem[soffs[j]:soffs[j + 1]]
            ne = nitem[soffs[j]:soffs[j + 1]]
            if ge == ne:
                continue
            w = prequant.pal_window(ge)
            if w is None:                      # only palettes may differ
                continue
            _start, count, _n = w
            src = hoffs[i] + hsoffs[j] + 8
            out[src:src + count * 3] = ne[8:8 + count * 3]
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("src", help="gamedata directory or a single .ctl")
    ap.add_argument("--out", help="write the converted tree here")
    ap.add_argument("--budget", type=int, default=24,
                    help="colours allowed per band (< the hardware's 32: "
                         "chrome and text share the band)")
    ap.add_argument("--rows", type=int, default=8,
                    help="scanlines per band (ECS: 200/25 = 8)")
    ap.add_argument("--screen-y", type=int, default=VIEWPORT_Y,
                    help="screen row a picture's row 0 lands on")
    ap.add_argument("--report", action="store_true", help="measure, write nothing")
    args = ap.parse_args()

    files = ([args.src] if os.path.isfile(args.src)
             else list(prequant.art_files(args.src)))
    stats = {"pics": 0, "merges": 0, "err": 0.0, "slots": 0, "before": 0,
             "after": 0, "skipped": 0, "reasons": {},
             "overfull_before": 0, "overfull_after": 0}
    for path in files:
        data = open(path, "rb").read()
        new = convert_any(data, args.budget, args.rows, args.screen_y, stats)
        if args.out and not args.report:
            rel = (os.path.relpath(path, args.src) if os.path.isdir(args.src)
                   else os.path.basename(path))
            dst = os.path.join(args.out, rel)
            os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
            with open(dst, "wb") as f:
                f.write(new)

    print("pictures converted     : %d" % stats["pics"])
    print("pictures skipped       : %d %s"
          % (stats["skipped"],
             ("(" + ", ".join("%s x%d" % (k, v)
                              for k, v in sorted(stats["reasons"].items())) + ")")
             if stats["reasons"] else ""))
    print("worst band, colours    : %d -> %d   (budget %d)"
          % (stats["before"], stats["after"], args.budget))
    print("bands over budget      : %d -> %d"
          % (stats["overfull_before"], stats["overfull_after"]))
    print("palette entries merged : %d" % stats["merges"])
    if stats["slots"]:
        print("palette rms error      : %.1f"
              % ((stats["err"] / stats["slots"]) ** 0.5))
    return 0


if __name__ == "__main__":
    sys.exit(main())
