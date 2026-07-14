#!/usr/bin/env python3
"""Diff a Hatari screenshot against ground-truth art rendered offline.

The port draws FRUA's art through its own decode -> palette -> CLUT -> blit
chain.  To know whether that chain is faithful you need a reference the port
had no hand in: decode the same asset straight from the .CTL/.TLB and compare.

Doing that naively FAILS, and the failure looks exactly like a palette bug:

    RMSE 671/65535 (1.0%), 142056 of 145920 pixels "differ"

...because **Hatari's SDL surface is RGB565**.  Every colour comes back out of
a screenshot with the low 3/2/3 bits of R/G/B replaced by a bit-replicated
copy of the high bits.  8-bit 0x83 lands as 0x82; 0x1F lands as 0x18.  That is
an EMULATOR artifact, not the port.  Quantize the reference the same way and a
faithful render diffs to *exactly zero*.

This bit me for real: chasing a "big-picture colour cast" that did not exist.

Usage:
    screen_diff.py SHOT.png REF.ppm [--scale 2] [--search 16]

Prints the best alignment and the residual.  Exit status 0 iff the region is a
bit-exact match once RGB565 is accounted for.
"""
import argparse
import subprocess
import sys


def q565(r, g, b):
	"""Quantize an 8:8:8 colour the way Hatari's RGB565 framebuffer does.

	5 bits R, 6 bits G, 5 bits B, then bit-replicated back up to 8.  This is
	SDL's standard 565->888 expansion, not a truncation: 0x1F -> 0x18, not 0x00.
	"""
	r5, g6, b5 = r >> 3, g >> 2, b >> 3
	return ((r5 << 3) | (r5 >> 2),
	        (g6 << 2) | (g6 >> 4),
	        (b5 << 3) | (b5 >> 2))


def read_ppm(path):
	"""Minimal binary-PPM (P6) reader -> (w, h, bytes)."""
	d = open(path, "rb").read()
	if not d.startswith(b"P6"):
		raise ValueError("%s: not a binary PPM" % path)
	fields, off = [], 2
	while len(fields) < 3:
		while off < len(d) and d[off : off + 1].isspace():
			off += 1
		if d[off : off + 1] == b"#":
			while d[off : off + 1] not in (b"\n", b""):
				off += 1
			continue
		start = off
		while off < len(d) and not d[off : off + 1].isspace():
			off += 1
		fields.append(int(d[start:off]))
	return fields[0], fields[1], d[off + 1 :]


def write_ppm(path, w, h, pix):
	with open(path, "wb") as f:
		f.write(b"P6\n%d %d\n255\n" % (w, h))
		f.write(bytes(pix))


def quantize_ppm(src, dst):
	"""Write `src` back out as it would look through Hatari's framebuffer."""
	w, h, pix = read_ppm(src)
	out = bytearray()
	for i in range(0, w * h * 3, 3):
		out += bytes(q565(pix[i], pix[i + 1], pix[i + 2]))
	write_ppm(dst, w, h, out)
	return w, h


def _metric(shot, ref, geom, metric):
	"""One `compare` call; returns the metric as a float (inf on failure)."""
	out = subprocess.run(
		["compare", "-metric", metric, "-extract", geom, shot, ref, "null:"],
		capture_output=True, text=True).stderr
	try:
		return float(out.split()[0])
	except (ValueError, IndexError):
		return float("inf")


def align(shot, ref, w, h, x0, y0, radius):
	"""Slide `ref` over `shot` around (x0,y0); return (rmse, x, y) of the best."""
	best = (float("inf"), x0, y0)
	for y in range(max(0, y0 - radius), y0 + radius + 1):
		for x in range(max(0, x0 - radius), x0 + radius + 1):
			v = _metric(shot, ref, "%dx%d+%d+%d" % (w, h, x, y), "RMSE")
			if v < best[0]:
				best = (v, x, y)
	return best


def main(argv=None):
	ap = argparse.ArgumentParser(description=__doc__,
	                             formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("shot", help="Hatari screenshot (PNG)")
	ap.add_argument("ref", help="ground-truth render (binary PPM)")
	ap.add_argument("--scale", type=int, default=2,
	                help="the port draws art at this zoom (default 2)")
	ap.add_argument("--at", default="36,70", metavar="X,Y",
	                help="expected top-left of the art in the shot")
	ap.add_argument("--search", type=int, default=12,
	                help="alignment search radius in pixels")
	ap.add_argument("--raw", action="store_true",
	                help="skip RGB565 quantization (shows the artifact)")
	a = ap.parse_args(argv)

	tmp = a.ref + (".raw.ppm" if a.raw else ".565.ppm")
	if a.raw:
		w, h, pix = read_ppm(a.ref)
		write_ppm(tmp, w, h, pix)
	else:
		w, h = quantize_ppm(a.ref, tmp)

	scaled = tmp + ".png"
	subprocess.run(["convert", tmp, "-filter", "point",
	                "-resize", "%d%%" % (a.scale * 100), scaled], check=True)
	sw, sh = w * a.scale, h * a.scale

	x0, y0 = (int(v) for v in a.at.split(","))
	rmse, x, y = align(a.shot, scaled, sw, sh, x0, y0, a.search)
	ae = _metric(a.shot, scaled, "%dx%d+%d+%d" % (sw, sh, x, y), "AE")

	print("art %dx%d drawn at %dx zoom, best fit at +%d+%d" % (w, h, a.scale, x, y))
	print("RMSE %.3f   pixels differing: %d / %d" % (rmse, ae, sw * sh))
	if ae == 0:
		print("BIT-EXACT: the port's render matches the offline decode exactly.")
		return 0
	print("MISMATCH: %.2f%% of pixels differ."
	      % (100.0 * ae / (sw * sh)))
	if not a.raw:
		print("(RGB565 already accounted for -- this is a real difference.)")
	return 1


if __name__ == "__main__":
	sys.exit(main())
