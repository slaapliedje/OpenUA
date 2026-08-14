#!/usr/bin/env python3
"""quantsim.py — try ST/STE and Amiga ECS palette-reduction strategies OFFLINE.

WHY: the reduction lives on an 8MHz 68000 behind a 170-second emulator boot, so
iterating on it in C is painfully slow, and "does this look better?" is not a
question a unit test answers. This runs the same *strategies* over a captured
frame on the host, reports mean-squared RGB error, and writes PNGs to look at.

INPUT is a screenshot of the FALCON (or Amiga AGA) build — those are chunky
8bpp, so they are the 256-colour ground truth the banded backends are trying to
approximate. Grab one with:

    .claude/skills/run-falcon-port/driver.sh shots /tmp/frame.png

STRATEGIES
    global   N colours for the whole frame     — what st_reband does TODAY
                                                 (quant_banded(..., nbands=1)),
                                                 and what ecs_reband does
    band     N per horizontal strip            — the machinery already present
                                                 in display_ste.c, disabled in
                                                 ADR-0016 B1 over seam artefacts
    +dither  error-weighted ordered dither     — pairs each colour with its
                                                 second-nearest palette entry
                                                 and mixes them spatially

The dither is SELECTIVE for free: a colour whose nearest palette entry is exact
never dithers, so chrome and glyph text stay crisp while gradient art gets
blended. That is the answer to the objection recorded in
docs/ecs-st-quantizer-plan.md fork #3 ("muddies crisp pixel art").

Expect +dither to INCREASE the reported MSE. That is not a defect — ordered
dithering trades pointwise error for perceptual accuracy. Judge it by eye, and
by whether it hides the per-band seams; the numbers are only there to keep the
band-vs-global comparison honest.

    python3 tools/quantsim.py /tmp/frame.png --ncol 16 --bands 10 --bits 4
    python3 tools/quantsim.py /tmp/frame.png --ncol 32 --bands 25 --bits 8   # ECS

Measured on real HEIRS frames (2026-08-14), mean-squared RGB error:

    frame            global-16   band-16   band-16+dither
    3D walk view         448       162          187
    BigPic event         585       190          221
    UA title             403       278          319

This is host analysis tooling — it embeds no FRUA data (ADR-0001/0007).
"""
import argparse
import sys

try:
    from PIL import Image
except ImportError:                                  # pragma: no cover
    sys.exit("quantsim.py needs Pillow: pip install pillow")

BAYER4 = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))


def median_cut(pixels, n):
    """Reduce `pixels` (a population list of RGB triples) to n representatives.

    Mirrors quantize.h's strategy — split the box with the largest weighted
    spread — closely enough for a relative comparison between strategies.
    """
    boxes = [pixels]
    while len(boxes) < n:
        best, bi, baxis = -1, -1, 0
        for i, box in enumerate(boxes):
            if len(box) < 2:
                continue
            for ax in range(3):
                vals = [p[ax] for p in box]
                spread = (max(vals) - min(vals)) * len(box)
                if spread > best:
                    best, bi, baxis = spread, i, ax
        if bi < 0:
            break
        box = sorted(boxes[bi], key=lambda p: p[baxis])
        mid = len(box) // 2
        boxes[bi:bi + 1] = [box[:mid], box[mid:]]
    reps = []
    for box in boxes:
        if not box:
            reps.append((0, 0, 0))
        else:
            reps.append(tuple(sum(p[a] for p in box) // len(box)
                              for a in range(3)))
    while len(reps) < n:
        reps.append(reps[-1])
    return reps[:n]


def dist2(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def two_nearest(c, pal):
    """(best, best_d2, second, second_d2) over the palette."""
    i0 = i1 = -1
    d0 = d1 = 1 << 30
    for i, p in enumerate(pal):
        d = dist2(c, p)
        if d < d0:
            i1, d1, i0, d0 = i0, d0, i, d
        elif d < d1:
            i1, d1 = i, d
    return i0, d0, i1, d1


def render(img, ncol, nbands, dither, bits):
    """dither: '' | 'row' | 'check' | 'bayer4'. Returns (image, mean sq err)."""
    w, h = img.size
    px = img.load()
    out = Image.new('RGB', (w, h))
    op = out.load()
    err = 0
    rows_per = (h + nbands - 1) // nbands
    step = 256 >> bits
    for b in range(nbands):
        y0, y1 = b * rows_per, min(h, (b + 1) * rows_per)
        if y0 >= y1:
            continue
        pop = [px[x, y] for y in range(y0, y1) for x in range(w)]
        pal = [tuple(min(255, (v // step) * step + step // 2) for v in c)
               for c in median_cut(pop, ncol)]
        cache = {}
        for y in range(y0, y1):
            for x in range(w):
                c = px[x, y]
                if c not in cache:
                    cache[c] = two_nearest(c, pal)
                i0, d0, i1, _ = cache[c]
                if not dither or d0 == 0:
                    pick = i0
                else:
                    a, bb = pal[i0], pal[i1]
                    num = sum((c[k] - a[k]) * (bb[k] - a[k]) for k in range(3))
                    den = sum((bb[k] - a[k]) ** 2 for k in range(3))
                    t = 0.0 if den == 0 else max(0.0, min(1.0, num / den))
                    if dither == 'row':
                        thr = (y & 1) * 0.5 + 0.25
                    elif dither == 'check':
                        thr = ((x ^ y) & 1) * 0.5 + 0.25
                    else:
                        thr = (BAYER4[y & 3][x & 3] + 0.5) / 16.0
                    pick = i1 if thr < t else i0
                op[x, y] = pal[pick]
                err += dist2(c, pal[pick])
    return out, err / float(w * h)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('frame', help='Falcon/AGA screenshot (chunky 8bpp truth)')
    ap.add_argument('--ncol', type=int, default=16, help='colours per band')
    ap.add_argument('--bands', type=int, default=10, help='horizontal bands')
    ap.add_argument('--bits', type=int, default=4, help='bits/gun (STE 4, Amiga 8)')
    ap.add_argument('--crop', default=None,
                    help='LEFT,TOP,RIGHT,BOTTOM to trim emulator chrome')
    ap.add_argument('--out', default='/tmp/quantsim',
                    help='output PNG prefix (default /tmp/quantsim)')
    args = ap.parse_args()

    im = Image.open(args.frame).convert('RGB')
    if args.crop:
        im = im.crop(tuple(int(v) for v in args.crop.split(',')))
    im = im.resize((320, 200), Image.NEAREST)
    im.save(f'{args.out}_ref.png')
    print(f'{args.frame}: {args.ncol} colours, {args.bands} bands, '
          f'{args.bits} bits/gun  (distinct in frame: '
          f'{len(set(im.getdata()))})')
    for label, nb, dith in (('global      ', 1, ''),
                            ('band        ', args.bands, ''),
                            ('band+dither ', args.bands, 'check')):
        out, err = render(im, args.ncol, nb, dith, args.bits)
        name = label.strip().replace('+', '_')
        out.save(f'{args.out}_{name}.png')
        print(f'  {label}  mean sq RGB err = {err:8.1f}   -> '
              f'{args.out}_{name}.png')


if __name__ == '__main__':
    main()
