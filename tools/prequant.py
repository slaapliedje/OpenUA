#!/usr/bin/env python3
"""prequant.py — OFFLINE art pre-quantization to a platform-native palette
(ADR-0020).

WHY: the bitplane machines (ST/STE 16 colours, Amiga ECS 32) reduce the
256-colour art at RUNTIME — a banded quantizer plus re-band heuristics on a
7–16 MHz 68000. SSI's own Curse of the Azure Bonds ST port did the opposite:
a couple of hand-authored 16-colour palettes and authoring-time dithering,
no runtime quantization at all. This tool moves the colour work offline the
same way, over the USER'S OWN installed art (copyright-clean, like the
existing on-load .tlb->.ctl conversion).

WHAT IT DOES (v1 = palette-snap): every colour GLIB library (.ctl) holds,
per picture set, a type-8 palette block (metric[7]&15 == 8). The tool
gathers every palette colour across the corpus, computes ONE target palette
of --ncol colours quantized to --bits bits per channel (median cut, usage
weighted), and rewrites ONLY the RGB triples inside the type-8 blocks to
their nearest target colour. Pixels, offsets, metrics, cycle records — every
other byte — are untouched, so the output is valid art BY CONSTRUCTION and
byte-identical in size. The unmodified engine then plays it and its runtime
quantizer finds art that is already inside the hardware budget: it settles
into one stable palette with nothing to re-band.

The type-8 layout is the engine's own (jt993 "TNPalette", boot.c):

    hdr[8]:  [1] bit0 = explicit window, bit1 = cycle records valid
             [2:4] start (BE), [4:6] count (BE)   when bit0
             [6]   ncopy = cycle-record count      when bit1
             [7]   low nibble = 8
    payload: count*3 RGB bytes, then ncopy*4 cycle records
             (flags, period, base, count — base/count in CLUT slots)

When bit0 is clear the count is mode-dependent in the engine (16 or 256);
here it is derived from the block size instead, which reproduces the real
corpus exactly (TITLE.ctl's full-256 block included).

USAGE
    python3 tools/prequant.py SRcDIR --out DSTDIR --ncol 16 --bits 3   # ST
    python3 tools/prequant.py SRCDIR --out DSTDIR --ncol 32 --bits 4   # ECS
    ... --scope file       # one palette per library instead of global
    ... --report           # stats only, write nothing

Only files whose magic is big-endian 'GLIB' are touched (the mono .TLB
twins and DOS HLIBs pass through untouched when --out copies them).
`.DSN` subdirectories are walked too (ADR-0011: module art converts the
same way and never overwrites the base game).

CAVEAT (why this is v1): snapping cannot dither — two slots that collapse
to the same target colour lose their contrast, and a COLOUR-CYCLING range
whose slots collapse animates less visibly (the report counts these).
Pixel-level dithering that trades slots spatially is the planned v2; it
requires decoding/re-encoding the piece codecs (types 2/5/7).
"""
import argparse
import os
import struct
import sys

GLIB = b"GLIB"
HDR = 16


def parse_glib(data):
    """-> (count, offsets) or None if not a big-endian GLIB container."""
    if len(data) < HDR or data[:4] != GLIB:
        return None
    count = struct.unpack(">H", data[8:10])[0]
    need = HDR + 4 * (count + 1)
    if len(data) < need:
        return None
    offsets = struct.unpack(">%dI" % (count + 1), data[HDR:need])
    if any(o > len(data) for o in offsets):
        return None
    return count, offsets


def pal_window(entry):
    """Type-8 block -> (start, count, ncopy) per the jt993 law, or None."""
    if len(entry) < 8 or (entry[7] & 15) != 8:
        return None
    flags = entry[1]
    ncopy = entry[6] if (flags & 2) else 0
    if flags & 1:
        start = struct.unpack(">h", entry[2:4])[0]
        count = struct.unpack(">h", entry[4:6])[0]
    else:
        start = 0
        count = (len(entry) - 8 - ncopy * 4) // 3
    if count < 0 or count > 256 or 8 + count * 3 + ncopy * 4 > len(entry) + 3:
        return None                      # +3: real blocks carry pad bytes
    return start, count, ncopy


def walk_palettes(data, base, out):
    """Collect (abs_offset, start, count, ncopy) for every type-8 block,
    recursing into nested GLIB entries."""
    top = parse_glib(data)
    if top is None:
        return
    count, offs = top
    for i in range(count):
        ent = data[offs[i]:offs[i + 1]]
        if ent[:4] == GLIB:
            walk_palettes(ent, base + offs[i], out)
        else:
            w = pal_window(ent)
            if w is not None:
                out.append((base + offs[i],) + w)


def snap_bits(v, bits):
    """Quantize one 0..255 channel to `bits` significant bits, rescaled so
    the extremes stay 0 and 255 (what the ST/ECS colour registers show)."""
    levels = (1 << bits) - 1
    return round(round(v * levels / 255) * 255 / levels)


def median_cut(weighted, ncol, bits):
    """weighted: {(r,g,b): weight} -> list of ncol (r,g,b) target colours,
    each snapped to the channel depth."""
    boxes = [list(weighted.items())]
    while len(boxes) < ncol:
        # split the box with the largest weighted spread
        best, best_score, best_axis = None, -1, 0
        for bi, box in enumerate(boxes):
            if len(box) < 2:
                continue
            for axis in range(3):
                vals = [c[axis] for c, _ in box]
                score = (max(vals) - min(vals)) * sum(w for _, w in box)
                if score > best_score:
                    best, best_score, best_axis = bi, score, axis
        if best is None:
            break
        box = boxes.pop(best)
        box.sort(key=lambda cw: cw[0][best_axis])
        half = sum(w for _, w in box) / 2.0
        acc, cut = 0.0, 0
        for k, (_, w) in enumerate(box):
            acc += w
            if acc >= half:
                cut = max(1, min(k + 1, len(box) - 1))
                break
        boxes.append(box[:cut])
        boxes.append(box[cut:])
    pal = []
    for box in boxes:
        tw = sum(w for _, w in box)
        if tw == 0:
            continue
        rgb = tuple(snap_bits(sum(c[a] * w for c, w in box) / tw, bits)
                    for a in range(3))
        pal.append(rgb)
    # dedupe (snapping can merge close centroids), keep order
    seen, out = set(), []
    for c in pal:
        if c not in seen:
            seen.add(c)
            out.append(c)
    return out


def nearest(pal, rgb):
    return min(pal, key=lambda p: (p[0] - rgb[0]) ** 2
               + (p[1] - rgb[1]) ** 2 * 2      # green-weighted, like the engine
               + (p[2] - rgb[2]) ** 2)


def gather(path):
    """-> (data, [(off, start, count, ncopy)]) or (data, []) if not GLIB."""
    data = open(path, "rb").read()
    pals = []
    walk_palettes(data, 0, pals)
    return data, pals


def art_files(root):
    for dirpath, dirnames, filenames in sorted(os.walk(root)):
        dirnames.sort()
        for fn in sorted(filenames):
            if fn.lower().endswith((".ctl", ".tlb")):
                yield os.path.join(dirpath, fn)


def colours_of(data, pals):
    """{(r,g,b): weight} across the file's palette blocks."""
    w = {}
    for off, _s, count, _n in pals:
        p = data[off + 8:off + 8 + count * 3]
        for i in range(count):
            c = tuple(p[i * 3:i * 3 + 3])
            w[c] = w.get(c, 0) + 1
    return w


def snap_file(data, pals, pal):
    """Rewrite every palette triple to its nearest target colour.
    -> (new_bytes, n_slots, mse_sum, cycle_collapsed)"""
    buf = bytearray(data)
    slots, mse, collapsed = 0, 0.0, 0
    for off, _s, count, ncopy in pals:
        base = off + 8
        snapped = []
        for i in range(count):
            c = tuple(buf[base + i * 3:base + i * 3 + 3])
            t = nearest(pal, c)
            buf[base + i * 3:base + i * 3 + 3] = bytes(t)
            snapped.append(t)
            mse += sum((a - b) ** 2 for a, b in zip(c, t)) / 3.0
            slots += 1
        # cycle ranges that lost contrast (adjacent slots now identical)
        cyc = base + count * 3
        pstart = pal_start = _s
        for r in range(ncopy):
            rec = buf[cyc + r * 4:cyc + r * 4 + 4]
            cb, cc = rec[2], rec[3]
            lo, hi = cb - pal_start, cb - pal_start + cc
            if 0 <= lo < hi <= count:
                rng = snapped[lo:hi]
                collapsed += sum(1 for a, b in zip(rng, rng[1:]) if a == b)
    return bytes(buf), slots, mse, collapsed


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("src", help="gamedata directory (or one .ctl file)")
    ap.add_argument("--out", help="write converted tree here (default: report only)")
    ap.add_argument("--ncol", type=int, default=16, help="target colours (16 ST, 32 ECS)")
    ap.add_argument("--bits", type=int, default=3, help="bits per channel (3 STF, 4 STE/ECS)")
    ap.add_argument("--scope", choices=("global", "file"), default="global",
                    help="one palette for the corpus, or one per library")
    ap.add_argument("--report", action="store_true", help="stats only, write nothing")
    args = ap.parse_args()

    files = ([args.src] if os.path.isfile(args.src)
             else list(art_files(args.src)))
    corpus = []                                  # (path, data, pals)
    for path in files:
        data, pals = gather(path)
        corpus.append((path, data, pals))

    glib_files = [(p, d, s) for p, d, s in corpus if s]
    print("libraries with palettes: %d of %d files"
          % (len(glib_files), len(corpus)))

    gpal = None
    if args.scope == "global":
        w = {}
        for _p, d, s in glib_files:
            for c, n in colours_of(d, s).items():
                w[c] = w.get(c, 0) + n
        gpal = median_cut(w, args.ncol, args.bits)
        print("corpus colours: %d unique -> target palette %d (%d-bit channels)"
              % (len(w), len(gpal), args.bits))

    tot_slots = tot_mse = tot_coll = 0
    for path, data, pals in corpus:
        rel = os.path.relpath(path, args.src) if os.path.isdir(args.src) \
            else os.path.basename(path)
        if not pals:
            new = data                            # pass through (mono/HLIB)
        else:
            pal = gpal if gpal is not None else \
                median_cut(colours_of(data, pals), args.ncol, args.bits)
            if not pal:                           # palette blocks all empty
                new = data
                print("  %-24s %3d palettes (empty) — passed through"
                      % (rel, len(pals)))
                if args.out and not args.report:
                    dst = os.path.join(args.out, rel)
                    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
                    with open(dst, "wb") as f:
                        f.write(new)
                continue
            new, slots, mse, coll = snap_file(data, pals, pal)
            tot_slots += slots
            tot_mse += mse
            tot_coll += coll
            print("  %-24s %3d palettes %5d slots  rms %6.1f%s"
                  % (rel, len(pals), slots, (mse / slots) ** 0.5 if slots else 0,
                     ("  CYCLE-COLLAPSED %d" % coll) if coll else ""))
        if args.out and not args.report:
            dst = os.path.join(args.out, rel)
            os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
            with open(dst, "wb") as f:
                f.write(new)
    if tot_slots:
        print("TOTAL: %d slots, rms error %.1f, cycle slots collapsed %d"
              % (tot_slots, (tot_mse / tot_slots) ** 0.5, tot_coll))
    return 0


if __name__ == "__main__":
    sys.exit(main())
