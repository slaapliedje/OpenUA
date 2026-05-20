#!/usr/bin/env python3
"""Reassemble a DiskDoubler split archive (SPLT segments) into one DDAR file.

DiskDoubler spread a large archive across several disks as SPLT segments.
Each segment is a 94-byte header followed by its slice of the real archive.

SPLT segment header (big-endian):
    0x00  char[4]  'SPLT'
    0x04  uint16   total segment count
    0x08  uint32   total size of the reassembled archive
    0x10  char[4]  contained archive type     ('DDAR')
    0x14  char[4]  contained archive creator  ('DDAP')
    0x2A  uint16   this segment's index (0-based)
    0x2C  uint32   this segment's payload length
    0x5E           payload begins here

Usage:
    dd_unsplit.py <seg0> <seg1> ... -o <output.ddar>
"""
import sys
import struct

DATA_OFFSET = 0x5E  # 94-byte SPLT header


def read_segment(path):
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:4] != b"SPLT":
        sys.exit(f"{path}: not a SPLT segment (magic {blob[:4]!r})")
    count = struct.unpack_from(">H", blob, 0x04)[0]
    total = struct.unpack_from(">I", blob, 0x08)[0]
    index = struct.unpack_from(">H", blob, 0x2A)[0]
    plen = struct.unpack_from(">I", blob, 0x2C)[0]
    payload = blob[DATA_OFFSET:DATA_OFFSET + plen]
    if len(payload) != plen:
        sys.exit(f"{path}: short payload ({len(payload)} of {plen})")
    return count, total, index, payload


def main(argv):
    if "-o" not in argv:
        print(__doc__)
        sys.exit(2)
    oi = argv.index("-o")
    segs, out = argv[:oi], argv[oi + 1]

    parts = [read_segment(p) for p in segs]
    count, total = parts[0][0], parts[0][1]
    if len(parts) != count:
        sys.exit(f"expected {count} segments, got {len(parts)}")

    parts.sort(key=lambda p: p[2])  # order by segment index
    data = b"".join(p[3] for p in parts)
    if len(data) != total:
        sys.exit(f"reassembled {len(data)} bytes, header expected {total}")

    with open(out, "wb") as f:
        f.write(data)
    print(f"{out}: {len(data)} bytes from {count} segments")


if __name__ == "__main__":
    main(sys.argv[1:])
