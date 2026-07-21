#!/usr/bin/env python3
"""glb2glib — convert the DOS release's root HLIB data banks to GLIB.

The five root .GLB banks (GAME, GEO, MONST, SCRIPT, STRG) are the
'DATA'-signature default-content libraries: base monster definitions,
default GEO areas, default event scripts and strings. The DOS release
stores them as HLIB (little-endian container); the Mac engine reads
GLIB (big-endian) natively — jt1011 walks the header/offsets and
jt1013 scans the item-0 [id,index] directory with native shorts. An
unconverted bank opens fine and then dies in jt1011's magic check:
"Invalid Library File" -> the "Disk read error" box (this was the
(3,17) rider-fight crash — combat loads the root MONST.glb for the
base monster records).

The conversion law, PROVEN against the Mac release's own banks
(2026-07-21): swap the container (u32 size, u16 count, u32 offsets;
the u16 version field's bytes are already order-identical), u16-swap
item 0 IF it is the [id,index] directory, keep every other entry's
bytes RAW (design-data words are little-endian by design on BOTH
releases — the engine reads them through the jt1180 swap, the same
double-flip convention as GEO files), and pad to even length. Item 0
is a directory iff its first word n satisfies 2 + 4*n == its length
(GAME/GEO/MONST/STRG do; SCRIPT's item 0 is a Pascal-string label
and stays raw — the Mac file pins both cases). Under this law the
DOS GAME.GLB and GEO.GLB convert BYTE-IDENTICALLY to the Mac
release's files; MONST differs in 2 bytes and SCRIPT in 24 —
v1.2 content edits (the DOS release is the newer data) — while
STRG's strings were re-edited (sizes differ).

SOUNDS.GLB is NOT this format (signature 'DIG4', sample bank — see
tools/voc2glb.py); any non-'DATA' signature is skipped.
"""
import os
import struct
import sys


def convert(dos):
    """One HLIB 'DATA' bank -> GLIB bytes (None = not convertible)."""
    if dos[:4] != b"HLIB" or dos[12:16] != b"DATA":
        return None
    size, = struct.unpack("<I", dos[4:8])
    count, = struct.unpack("<H", dos[8:10])
    n = count + 1
    offs = struct.unpack("<%dI" % n, dos[16:16 + 4 * n])
    out = bytearray()
    out += b"GLIB"
    out += struct.pack(">IH", size + (size & 1), count)
    out += dos[10:12]                       # version: bytes already match
    out += dos[12:16]                       # 'DATA'
    out += struct.pack(">%dI" % n, *offs)
    out += dos[16 + 4 * n:]
    if len(out) & 1:
        out += b"\x00"
    # u16-swap item 0 only when it IS the [id,index] directory: first
    # word n with 2 + 4*n == item length. (SCRIPT's item 0 is a label.)
    if count >= 1 and offs[1] - offs[0] >= 2:
        dcount, = struct.unpack("<H", dos[offs[0]:offs[0] + 2])
        if 2 + 4 * dcount == offs[1] - offs[0]:
            for i in range(offs[0], offs[1] - 1, 2):
                out[i], out[i + 1] = out[i + 1], out[i]
    return bytes(out)


def main(argv):
    if not argv:
        print(__doc__)
        print("usage: glb2glib.py <bank.GLB> [...]   (converts in place)")
        return 2
    rc = 0
    for path in argv:
        data = open(path, "rb").read()
        if data[:4] == b"GLIB":
            print("glb2glib: %s already GLIB" % path)
            continue
        conv = convert(data)
        if conv is None:
            print("glb2glib: %s skipped (%s/%s)"
                  % (path, data[:4].decode("latin1"),
                     data[12:16].decode("latin1")))
            continue
        with open(path, "wb") as f:
            f.write(conv)
        print("glb2glib: %s -> GLIB (%d entries, %d bytes)"
              % (path, struct.unpack(">H", conv[8:10])[0], len(conv)))
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
