#!/usr/bin/env python3
"""ecsdump.py — read the ECS backend's own state dump and locate #19 exactly.

Every earlier attempt to answer "which pixels are wrong?" went through an
emulator screenshot, which puts TWO unknowns in one measurement: the artefact,
and where the picture sits in the captured window. Three alignment methods
disagreed (dx 21/28/22, dy 10/23/12), so the resulting percentages were not
worth recording. `FRUA_ECSDUMP` writes the backend's own state instead, and
this reads it — both sides in GAME coordinates, nothing to align.

The dump carries both ends of the quantiser, which is the point:

    clut + chunky          the INPUT  — the 256-colour frame. This is exactly
                           what AGA displays, and AGA is clean.
    band_pal + band_remap  the OUTPUT — the per-band 32-colour cut.
    planes (v2)            the SCREEN — the bitplanes display DMA is fetching.
                           A correct cut can still reach a wrong screen, and
                           that gap is what --planes measures.

    shown(x, y) = band_pal[band][ band_remap[band][ chunky[y][x] ] ]
    want (x, y) = clut[ chunky[y][x] ]
    band        = y * nbands // h

A pixel whose shown colour is nowhere near its wanted colour IS the artefact,
and the mapping that produced it is named rather than guessed at.

Usage:
    tools/ecsdump.py DUMP.BIN --info
    tools/ecsdump.py DUMP.BIN --report [--thresh N] [--top N]
    tools/ecsdump.py DUMP.BIN --png OUT_PREFIX      (needs Pillow)
"""
import argparse
import struct
import sys

HDR = 32
MAGIC = b"ECSD"


class Dump(object):
    """One ECS backend state dump, sliced but not interpreted."""

    def __init__(self, blob):
        if len(blob) < HDR or blob[:4] != MAGIC:
            raise ValueError("not an ECSD dump (bad magic)")
        (ver, self.seq, self.w, self.h,
         self.nbands, self.ncol) = struct.unpack(">6H", blob[4:16])
        if ver not in (1, 2):
            raise ValueError("unsupported dump version %d" % ver)
        self.version = ver
        self.depth = blob[16] if ver >= 2 else 0
        self.front = blob[17] if ver >= 2 else 0
        self.pitch = struct.unpack(">H", blob[18:20])[0] if ver >= 2 else 0

        n_clut = 256 * 3
        n_pal = self.nbands * self.ncol * 3
        n_rem = self.nbands * 256
        n_chk = self.w * self.h
        n_pln = self.pitch * self.h * self.depth if ver >= 2 else 0
        want = HDR + n_clut + n_pal + n_rem + n_chk + n_pln
        if len(blob) != want:
            raise ValueError("dump is %d bytes, expected %d" % (len(blob), want))

        o = HDR
        self.clut = blob[o:o + n_clut];              o += n_clut
        self.band_pal = blob[o:o + n_pal];           o += n_pal
        self.band_remap = blob[o:o + n_rem];         o += n_rem
        self.chunky = blob[o:o + n_chk];             o += n_chk
        self.planes = blob[o:o + n_pln] if n_pln else None

    # -- the three lookups the backend itself performs -------------------
    def band_of(self, y):
        return y * self.nbands // self.h

    def idx_at(self, x, y):
        return self.chunky[y * self.w + x]

    def slot_of(self, band, idx):
        return self.band_remap[band * 256 + idx]

    def slot_rgb(self, band, slot):
        o = (band * self.ncol + slot) * 3
        return (self.band_pal[o], self.band_pal[o + 1], self.band_pal[o + 2])

    def plane_idx_at(self, x, y):
        """Decode one pixel back out of the bitplanes.

        Plane p contributes bit p. This is the inverse of the c2p the backend
        ran, so comparing it with chunky[] asks the one question nothing else
        does: did the pixel that reached the SCREEN keep the index the cut
        assigned it?"""
        if self.planes is None:
            raise ValueError("dump has no planes (version 1)")
        byte = y * self.pitch + (x >> 3)
        bit = 7 - (x & 7)
        v = 0
        for p in range(self.depth):
            plane = self.planes[p * self.pitch * self.h + byte]
            v |= ((plane >> bit) & 1) << p
        return v

    def want_rgb(self, idx):
        return (self.clut[idx * 3], self.clut[idx * 3 + 1], self.clut[idx * 3 + 2])

    def shown_rgb(self, x, y):
        band = self.band_of(y)
        return self.slot_rgb(band, self.slot_of(band, self.idx_at(x, y)))


def load(path):
    with open(path, "rb") as f:
        return Dump(f.read())


def dist(a, b):
    """Sum of absolute channel differences. Cheap, and monotonic in the thing
    a human means by 'nowhere near' — a red->beige swap scores hundreds while
    the grey->lighter-grey the earlier probe found scores ~100."""
    return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])


def pair_errors(d):
    """Error of every (band, index) mapping the frame ACTUALLY USES.

    Per (band, idx) rather than per pixel: the cut decides colour once per
    pair, so this is the full set of decisions, with pixel counts attached.
    """
    counts = {}
    for y in range(d.h):
        band = d.band_of(y)
        row = y * d.w
        for x in range(d.w):
            key = (band, d.chunky[row + x])
            counts[key] = counts.get(key, 0) + 1

    out = []
    for (band, idx), n in counts.items():
        slot = d.slot_of(band, idx)
        want = d.want_rgb(idx)
        shown = d.slot_rgb(band, slot)
        out.append({"band": band, "idx": idx, "slot": slot, "pixels": n,
                    "want": want, "shown": shown, "err": dist(want, shown)})
    out.sort(key=lambda r: -r["err"])
    return out


def runs(d, thresh):
    """Bad pixels grouped into HORIZONTAL runs.

    #19 is reported as horizontal runs of 3-60 px, and that shape is the
    evidence: in a smooth vertical gradient a horizontal span shares one index,
    so one wrong (band, index) mapping paints exactly such a run. Grouping this
    way lets the output be compared with the report directly.
    """
    out = []
    for y in range(d.h):
        band = d.band_of(y)
        x = 0
        while x < d.w:
            idx = d.idx_at(x, y)
            e = dist(d.want_rgb(idx), d.slot_rgb(band, d.slot_of(band, idx)))
            if e < thresh:
                x += 1
                continue
            x0 = x
            while x < d.w and dist(d.want_rgb(d.idx_at(x, y)),
                                   d.slot_rgb(band, d.slot_of(band, d.idx_at(x, y)))) >= thresh:
                x += 1
            out.append({"y": y, "x0": x0, "x1": x - 1, "len": x - x0,
                        "band": band, "idx": idx, "err": e,
                        "want": d.want_rgb(idx),
                        "shown": d.slot_rgb(band, d.slot_of(band, idx))})
    return out


def plane_mismatches(d):
    """Every pixel whose PLANE slot differs from the slot the cut assigned it.

    ★ THE PLANES HOLD SLOTS, NOT CLUT INDICES. Five planes carry 0..31, while
    chunky carries 0..255, so the comparison is against remap[band][idx] — the
    slot — and NOT against the chunky index. Comparing with the raw index
    reports ~99.9% of pixels wrong on every frame, which is a broken measure,
    not a spectacular bug.

    The cut can be perfect and the screen still wrong: the planes are written
    by a separate path (draw-time stamping plus a bridging c2p) and nothing
    until now compared them with the state they are supposed to represent.
    """
    out = []
    for y in range(d.h):
        band = d.band_of(y)
        for x in range(d.w):
            idx = d.idx_at(x, y)
            want = d.slot_of(band, idx)
            got = d.plane_idx_at(x, y)
            if got != want:
                out.append({"x": x, "y": y, "chunky": idx, "slot": want,
                            "plane": got, "band": band})
    return out


def plane_runs(mm, d):
    """Group plane mismatches into horizontal runs, as the report describes."""
    by = {}
    for m in mm:
        by.setdefault(m["y"], []).append(m)
    out = []
    for y in sorted(by):
        row = sorted(by[y], key=lambda m: m["x"])
        x0 = prev = row[0]["x"]
        for m in row[1:]:
            if m["x"] == prev + 1:
                prev = m["x"]
                continue
            out.append({"y": y, "x0": x0, "x1": prev, "len": prev - x0 + 1,
                        "band": d.band_of(y)})
            x0 = prev = m["x"]
        out.append({"y": y, "x0": x0, "x1": prev, "len": prev - x0 + 1,
                    "band": d.band_of(y)})
    return out


def cmd_planes(d, top):
    if d.planes is None:
        print("dump is version 1 — no planes. Rebuild with the v2 FRUA_ECSDUMP.")
        return
    mm = plane_mismatches(d)
    tot = d.w * d.h
    print("planes     depth %d, pitch %d, front buffer %d"
          % (d.depth, d.pitch, d.front))
    print("mismatch   %d of %d pixels (%.3f%%) differ from the slot the cut chose"
          % (len(mm), tot, 100.0 * len(mm) / tot))
    if not mm:
        print("           the screen agrees with the cut.")
        return
    rr = plane_runs(mm, d)
    lens = sorted(r["len"] for r in rr)
    print("runs       %d horizontal; length min/median/max = %d / %d / %d"
          % (len(rr), lens[0], lens[len(lens) // 2], lens[-1]))
    print("           x %d..%d, y %d..%d"
          % (min(r["x0"] for r in rr), max(r["x1"] for r in rr),
             min(r["y"] for r in rr), max(r["y"] for r in rr)))
    rows = sorted(set(r["y"] for r in rr))
    print("           rows mod 8: %s"
          % sorted(set(y % 8 for y in rows)))
    print("LONGEST RUNS")
    for r in sorted(rr, key=lambda r: -r["len"])[:top]:
        print("  y=%3d x=%3d..%3d len=%3d band=%2d" % (r["y"], r["x0"], r["x1"],
                                                       r["len"], r["band"]))


def slot_usage(d, band):
    """Which of the band's slots are actually reachable, and by how many
    indices. A slot no index maps to is FREE (that is what ecs_ink_adopt_scan
    hands out); a slot many distant indices share is a cut under pressure."""
    used = {}
    for idx in range(256):
        used.setdefault(d.slot_of(band, idx), []).append(idx)
    return used


def hex_rgb(c):
    return "#%02x%02x%02x" % c


def cmd_info(d):
    print("seq        %d" % d.seq)
    print("geometry   %dx%d, %d bands (%d rows each), %d slots"
          % (d.w, d.h, d.nbands, d.h // d.nbands, d.ncol))
    idxs = set(d.chunky)
    print("indices    %d distinct in the frame" % len(idxs))
    pe = pair_errors(d)
    print("mappings   %d (band,index) pairs in use" % len(pe))
    if pe:
        w = pe[0]
        print("worst      band %d idx %3d -> slot %2d  %s -> %s  err %d  (%d px)"
              % (w["band"], w["idx"], w["slot"], hex_rgb(w["want"]),
                 hex_rgb(w["shown"]), w["err"], w["pixels"]))


def cmd_report(d, thresh, top):
    pe = pair_errors(d)
    bad = [r for r in pe if r["err"] >= thresh]
    tot = d.w * d.h
    npx = sum(r["pixels"] for r in bad)
    print("threshold  %d (sum |dR|+|dG|+|dB|)" % thresh)
    print("mappings   %d of %d exceed it" % (len(bad), len(pe)))
    print("pixels     %d of %d (%.2f%%)" % (npx, tot, 100.0 * npx / tot))
    print()
    print("WORST MAPPINGS")
    for r in bad[:top]:
        print("  band %2d  idx %3d -> slot %2d   %s -> %s   err %4d   %5d px"
              % (r["band"], r["idx"], r["slot"], hex_rgb(r["want"]),
                 hex_rgb(r["shown"]), r["err"], r["pixels"]))
    rr = runs(d, thresh)
    print()
    print("HORIZONTAL RUNS  %d total" % len(rr))
    if rr:
        lens = sorted(r["len"] for r in rr)
        print("  length min/median/max = %d / %d / %d"
              % (lens[0], lens[len(lens) // 2], lens[-1]))
        print("  leftmost x0 = %d, rightmost x1 = %d"
              % (min(r["x0"] for r in rr), max(r["x1"] for r in rr)))
        print("  longest:")
        for r in sorted(rr, key=lambda r: -r["len"])[:top]:
            print("    y=%3d x=%3d..%3d len=%2d band=%2d idx=%3d  %s -> %s"
                  % (r["y"], r["x0"], r["x1"], r["len"], r["band"], r["idx"],
                     hex_rgb(r["want"]), hex_rgb(r["shown"])))


def cmd_png(d, prefix):
    from PIL import Image
    shown = Image.new("RGB", (d.w, d.h))
    want = Image.new("RGB", (d.w, d.h))
    diff = Image.new("RGB", (d.w, d.h))
    ps, pw, pd = shown.load(), want.load(), diff.load()
    for y in range(d.h):
        band = d.band_of(y)
        for x in range(d.w):
            idx = d.idx_at(x, y)
            wc = d.want_rgb(idx)
            sc = d.slot_rgb(band, d.slot_of(band, idx))
            pw[x, y] = wc
            ps[x, y] = sc
            e = min(255, dist(wc, sc))
            pd[x, y] = (e, 0, 0) if e else (0, 0, 0)
    shown.save(prefix + "-shown.png")
    want.save(prefix + "-want.png")
    diff.save(prefix + "-diff.png")
    print("wrote %s-{shown,want,diff}.png" % prefix)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump")
    ap.add_argument("--info", action="store_true")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--png", metavar="PREFIX")
    ap.add_argument("--planes", action="store_true",
                    help="compare the displayed bitplanes against chunky (v2)")
    ap.add_argument("--thresh", type=int, default=150)
    ap.add_argument("--top", type=int, default=12)
    a = ap.parse_args(argv)

    d = load(a.dump)
    if a.info or not (a.report or a.png or a.planes):
        cmd_info(d)
    if a.planes:
        cmd_planes(d, a.top)
    if a.report:
        cmd_report(d, a.thresh, a.top)
    if a.png:
        cmd_png(d, a.png)
    return 0


if __name__ == "__main__":
    sys.exit(main())
