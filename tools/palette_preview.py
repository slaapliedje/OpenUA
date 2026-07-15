#!/usr/bin/env python3
"""palette_preview.py — preview FRUA frames reduced to the ECS/ST colour budgets.

The engine renders into a 256-colour chunky buffer. The ECS Amiga and the
Atari ST/STE can't show 256; a native backend for them needs a QUANTIZER that
reduces the live 256-entry palette to the machine's budget and remaps every
pixel to the nearest survivor. Before that lands in C, this tool answers the
question that gates the whole effort — "how bad does it look?" — by applying
the same reduction to a captured 256-colour frame (a screenshot from the
working Falcon/AGA build) and emitting a side-by-side strip.

Targets modelled:
  ST     16 colours, 3 bits/gun (512-colour palette)   <- the quality cliff
  STE    16 colours, 4 bits/gun (4096-colour palette)
  ECS    32 colours, 4 bits/gun
  EHB    64 colours (ECS Extra-Half-Brite: 32 base + their half-bright twins)

This operates on the RGB screenshot, not the real CLUT+chunky path, so it is a
VIABILITY preview, not a bit-exact model of the C quantizer — but the visual
verdict (legibility, where the palette budget goes) transfers directly.

Usage:
    python3 tools/palette_preview.py <frame.png> [<frame.png> ...]
    # writes <frame>.quant.png next to each input

Findings so far (docs/ecs-st-quantizer-plan.md): STE >> plain ST (the 4-bit
vs 3-bit palette is the cliff); the mottled chrome border is a palette hog
that argues for a per-region split; ECS-32 is the sweet spot.
"""
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("palette_preview needs Pillow:  pip install pillow")


def snap(img, bits):
    """Snap each channel to `bits` per gun — the hardware palette's grid."""
    step = 256 >> bits
    return img.point(lambda v: min(255, (v // step) * step + step // 2))


def reduce_colours(img, n, bits):
    q = img.quantize(colors=n, method=Image.MEDIANCUT).convert("RGB")
    return snap(q, bits)


def ehb(img):
    """ECS Extra-Half-Brite upper bound: 64-colour median cut, 4-bit snap.

    A faithful EHB quantizer must constrain colours 32..63 to be exactly half
    of 0..31; this preview relaxes that (it just shows what ~64 colours buys),
    so treat it as the optimistic bound the constrained version approaches.
    """
    return reduce_colours(img, 64, 4)


def banded(img, n, bits, nbands):
    """Per-horizontal-band palette: split into `nbands` strips, quantise each
    to its own N-colour palette. This models the achievable hardware — a
    palette reload at horizontal blank (free on the Amiga copper, an HBL
    handler on the ST), where each SCANLINE can carry its own colours but a
    single scanline is one palette (so side-by-side regions on the same rows
    still share). nbands == image height is the per-scanline ceiling; nbands
    == 1 is the global baseline.
    """
    w, h = img.size
    out = Image.new("RGB", (w, h))
    y = 0
    for i in range(nbands):
        y1 = h * (i + 1) // nbands
        if y1 <= y:
            continue
        strip = img.crop((0, y, w, y1))
        out.paste(reduce_colours(strip, n, bits), (0, y))
        y = y1
    return out


VARIANTS = [
    ("orig 256", lambda im: im),
    ("ST 16 / 3-bit", lambda im: reduce_colours(im, 16, 3)),
    ("STE 16 / 4-bit", lambda im: reduce_colours(im, 16, 4)),
    ("ECS 32 / 4-bit", lambda im: reduce_colours(im, 32, 4)),
    ("ECS-EHB 64", ehb),
]


def strip_for(src):
    w, h = src.size
    pad = 6
    tiles = [fn(src) for _, fn in VARIANTS]
    out = Image.new("RGB", (w * len(tiles) + pad * (len(tiles) - 1), h), (0, 0, 0))
    for i, t in enumerate(tiles):
        out.paste(t, (i * (w + pad), 0))
    return out


BAND_STEPS = [1, 4, 12, None]   # None = per-line (band count == height)


def band_strip_for(src, n, bits):
    """orig | global | 4-band | 12-band | per-line, at (n colours, bits/gun)."""
    w, h = src.size
    pad = 6
    tiles = [("orig", src)]
    for nb in BAND_STEPS:
        nb = h if nb is None else nb
        tiles.append((f"{nb}-band", banded(src, n, bits, nb)))
    out = Image.new("RGB", (w * len(tiles) + pad * (len(tiles) - 1), h), (0, 0, 0))
    for i, (_, t) in enumerate(tiles):
        out.paste(t, (i * (w + pad), 0))
    return out


def main(argv=None):
    argv = argv if argv is not None else sys.argv[1:]
    band_mode = "--banded" in argv
    argv = [a for a in argv if a != "--banded"]
    if not argv:
        sys.exit(__doc__)
    for path in argv:
        src = Image.open(path).convert("RGB")
        bbox = src.getbbox()            # trim letterbox so bands map to content
        if bbox:
            src = src.crop(bbox)
        stem = os.path.splitext(path)[0]
        if band_mode:
            # global vs banded, at ST-16 (3-bit) and ECS-32 (4-bit)
            band_strip_for(src, 16, 3).save(stem + ".band-st16.png")
            band_strip_for(src, 32, 4).save(stem + ".band-ecs32.png")
            print(f"{path} -> {stem}.band-st16.png, {stem}.band-ecs32.png "
                  f"[orig | {' | '.join(str(nb or 'per-line') for nb in BAND_STEPS)}]")
        else:
            strip_for(src).save(stem + ".quant.png")
            print(f"{path} -> {stem}.quant.png  "
                  f"[{', '.join(l for l, _ in VARIANTS)}]")


if __name__ == "__main__":
    main()
