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


def main(argv=None):
    argv = argv if argv is not None else sys.argv[1:]
    if not argv:
        sys.exit(__doc__)
    for path in argv:
        src = Image.open(path).convert("RGB")
        out_path = os.path.splitext(path)[0] + ".quant.png"
        strip_for(src).save(out_path)
        print(f"{path} -> {out_path}  [{', '.join(l for l, _ in VARIANTS)}]")


if __name__ == "__main__":
    main()
