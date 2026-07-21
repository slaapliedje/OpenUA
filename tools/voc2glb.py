#!/usr/bin/env python3
"""voc2glb — build a Mac-format SOUNDS.GLB (DIG8) from the DOS SFXDQ.VOC.

The DOS release carries FRUA's 13 sampled sound effects twice:

    SOUNDS.GLB   HLIB container, 'DIG4' — 4-bit streams for the DOS
                 drivers (little-endian directory, big-endian piece
                 headers; several pieces truncated to save disk)
    SFXDQ.VOC    Creative Voice File, codec 1 (4-bit Creative ADPCM)
                 — the Sound Blaster driver's copy

The Mac release's SOUNDS.GLB is a GLIB 'DIG8' bank of 8-bit samples.
Measured 2026-07-21: the VOC's 13 sound blocks match the Mac bank's 13
pieces IN ORDER, with every decoded sample count equal to the Mac's
count to +/-1 (e.g. 4368-byte block -> 8736 Mac samples; the 161-byte
block -> 321 with its reference byte) and the same rates (VOC sr bytes
121/166/76 = ~7407/11111/5555 Hz vs the Mac's stored 7418/11127/5564).
The Mac bank IS these recordings: Creative-ADPCM-decoded, boosted ~4x
(the Mac copies clip at full scale), stored signed. Decode correlation
against the Mac samples runs 0.98..0.9991 per piece.

Byte-parity is NOT achievable here: the exact decoder table + gain
chain SSI's Mac port used differs from the canonical Creative tables
in its saturation behaviour (best fit: x4 post-decode, rms ~13 against
heavily-clipped Mac copies). Like the Mac-redrawn ALWAYS cursors, the
remaining delta is an intrinsic platform-master difference; this tool
ships the same recordings through the documented codec instead.

## Formats

VOC:   26-byte header; blocks [type u8][len u24le]. Type 1 = sound:
       [sr u8][codec u8][data]; rate = 1e6/(256-sr); codec 1 = 4-bit
       Creative ADPCM: first data byte is the 8-bit reference sample,
       then 2 nibbles/byte HIGH FIRST, each stepping the sample via
       the scale/adjust tables (DOSBox's tables, verified by the Mac
       correlation above).

GLIB bank (from the Mac SOUNDS.GLB + jt975/jt964/l7ee0):
       'GLIB' [size u32be] [count u16be] [0 u16] 'DIG8'
       (count+1) x u32be piece offsets (from file start; first = 72)
       pieces: [rate u16be] [count u32be] [signed 8-bit samples],
       padded to even length. jt964 sign-flips the samples to
       excess-128 at load; l7ee0 builds the play-time FFSynthRec in
       place over bytes 8..13.
"""
import os
import struct
import sys

GAIN = 4        # the Mac bank's measured boost (best fit, clip at full scale)

SCALE_MAP = (
    0, 1, 2, 3, 4, 5, 6, 7, 0, -1, -2, -3, -4, -5, -6, -7,
    1, 3, 5, 7, 9, 11, 13, 15, -1, -3, -5, -7, -9, -11, -13, -15,
    2, 6, 10, 14, 18, 22, 26, 30, -2, -6, -10, -14, -18, -22, -26, -30,
    4, 12, 20, 28, 36, 44, 52, 60, -4, -12, -20, -28, -36, -44, -52, -60,
)
ADJUST_MAP = (
    0, 0, 0, 0, 0, 16, 16, 16, 0, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16, 240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16, 240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 0, 0, 0, 240, 0, 0, 0, 0, 0, 0, 0,
)


def ct_adpcm4_decode(data):
    """Creative 4-bit ADPCM -> excess-128 samples. data[0] = reference."""
    sample = data[0]
    out = [sample]
    scale = 0
    for b in data[1:]:
        for nib in (b >> 4, b & 15):
            v = scale + nib
            sample = max(0, min(255, sample + SCALE_MAP[v]))
            scale = (scale + ADJUST_MAP[v]) & 0xFF
            out.append(sample)
    return out


def parse_voc(data):
    """-> [(rate_hz, adpcm_bytes)] for every type-1 sound block."""
    if not data.startswith(b"Creative Voice File\x1a"):
        raise ValueError("not a VOC file")
    pos = struct.unpack("<H", data[20:22])[0]
    blocks = []
    while pos < len(data) and data[pos] != 0:
        btype = data[pos]
        blen = data[pos + 1] | (data[pos + 2] << 8) | (data[pos + 3] << 16)
        if btype == 1:
            sr, codec = data[pos + 4], data[pos + 5]
            if codec != 1:
                raise ValueError("VOC block codec %d (want 1 = 4-bit ADPCM)"
                                 % codec)
            rate = int(round(1000000.0 / (256 - sr)))
            blocks.append((rate, data[pos + 6:pos + 4 + blen]))
        pos += 4 + blen
    return blocks


def build_glb(voc_data):
    """SFXDQ.VOC bytes -> a Mac-format DIG8 GLIB bank."""
    pieces = []
    for rate, adpcm in parse_voc(voc_data):
        dec = ct_adpcm4_decode(adpcm)
        samples = bytearray()
        for v in dec:
            s = max(-128, min(127, (v - 128) * GAIN))
            samples.append(s & 0xFF)
        body = struct.pack(">HI", rate, len(samples)) + bytes(samples)
        if len(body) & 1:
            body += b"\x00"
        pieces.append(body)

    count = len(pieces)
    hdr_len = 16 + (count + 1) * 4
    offs = []
    off = hdr_len
    for p in pieces:
        offs.append(off)
        off += len(p)
    offs.append(off)
    out = b"GLIB" + struct.pack(">IHH", off, count, 0) + b"DIG8"
    out += struct.pack(">%dI" % (count + 1), *offs)
    out += b"".join(pieces)
    return out


def find_voc(dos_dir):
    for r, _d, files in os.walk(dos_dir):
        for f in files:
            if f.upper() == "SFXDQ.VOC":
                return os.path.join(r, f)
    return None


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        print("usage: voc2glb.py <dos_dir_or_voc> <out.glb>")
        return 2
    src = argv[0]
    if os.path.isdir(src):
        src = find_voc(src)
        if src is None:
            print("voc2glb: no SFXDQ.VOC under %s" % argv[0])
            return 1
    bank = build_glb(open(src, "rb").read())
    with open(argv[1], "wb") as f:
        f.write(bank)
    n = struct.unpack(">H", bank[8:10])[0]
    print("voc2glb: %s (%d bytes, %d sfx) from %s"
          % (argv[1], len(bank), n, os.path.basename(src)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
