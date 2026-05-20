#!/usr/bin/env python3
"""Extract a fork from an AppleSingle / AppleDouble file.

The Unarchiver writes Mac files with forks as AppleDouble. This pulls a named
fork (the resource fork by default) back out as a raw byte stream.

Entry IDs: 1 = data fork, 2 = resource fork, 3 = real name, 9 = finder info.

Usage:
    appledouble.py <file> [--fork resource|data] -o <output>
    appledouble.py <file> --info
"""
import sys
import struct

MAGIC = {0x00051600: "AppleSingle", 0x00051607: "AppleDouble"}
FORK_ID = {"data": 1, "resource": 2}
ENTRY_NAME = {1: "data fork", 2: "resource fork", 3: "real name",
              8: "date info", 9: "finder info"}


def parse(path):
    with open(path, "rb") as f:
        blob = f.read()
    magic, version = struct.unpack_from(">II", blob, 0)
    if magic not in MAGIC:
        sys.exit(f"{path}: not AppleSingle/AppleDouble (magic {magic:#010x})")
    n = struct.unpack_from(">H", blob, 24)[0]
    entries = {}
    for i in range(n):
        eid, off, length = struct.unpack_from(">III", blob, 26 + i * 12)
        entries[eid] = (off, length)
    return blob, MAGIC[magic], entries


def main(argv):
    if not argv:
        print(__doc__)
        sys.exit(2)
    path = argv[0]
    blob, kind, entries = parse(path)

    if "--info" in argv:
        print(f"{path}: {kind}, {len(entries)} entries")
        for eid, (off, length) in sorted(entries.items()):
            name = ENTRY_NAME.get(eid, f"id {eid}")
            print(f"  {name:<14} offset={off:<10} length={length}")
        return

    fork = "resource"
    if "--fork" in argv:
        fork = argv[argv.index("--fork") + 1]
    if "-o" not in argv:
        print(__doc__)
        sys.exit(2)
    out = argv[argv.index("-o") + 1]

    eid = FORK_ID[fork]
    if eid not in entries:
        sys.exit(f"{path}: no {fork} fork present")
    off, length = entries[eid]
    with open(out, "wb") as f:
        f.write(blob[off:off + length])
    print(f"{out}: {length} bytes ({fork} fork)")


if __name__ == "__main__":
    main(sys.argv[1:])
