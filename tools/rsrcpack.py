#!/usr/bin/env python3
"""Pack a Mac resource fork into the flat FRSC archive (ADR-0007).

Atari filesystems have no resource fork, so the Mac resource fork is repacked
host-side into one indexed (type, id) archive — the FRSC format, specified in
tools/README.md. The Resource Manager shim (compat/resources.c) reads it at
runtime.

Usage:
    rsrcpack.py <resource-fork> [-o <output>]

<resource-fork> is a raw resource fork; appledouble.py unwraps one from an
AppleDouble file first. The default output is frua.rsc.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from macrsrc import ResourceFork

FRSC_MAGIC = b"FRSC"
FRSC_VERSION = 1
HEADER_SIZE = 16
ENTRY_SIZE = 16


def build_archive(resources):
    """Pack `resources` into a FRSC archive, returning the bytes.

    `resources` is an iterable of objects with .type (str), .id (int),
    .attrs (int) and .data (bytes) — for example macrsrc.Resource.
    """
    # Normalise, then sort by (4-byte type, id) so the shim can binary-search.
    items = [(r.type.encode("mac-roman").ljust(4)[:4], r.id,
              r.attrs & 0xFFFF, r.data) for r in resources]
    items.sort(key=lambda it: (it[0], it[1]))

    data_off = HEADER_SIZE + ENTRY_SIZE * len(items)
    entries = bytearray()
    body = bytearray()
    for rtype, rid, attrs, data in items:
        entries += rtype
        entries += struct.pack(">hHII", rid, attrs,
                               data_off + len(body), len(data))
        body += data

    header = struct.pack(">4sHHII", FRSC_MAGIC, FRSC_VERSION, len(items),
                         HEADER_SIZE, 0)
    return header + bytes(entries) + bytes(body)


def main(argv):
    if not argv:
        print(__doc__)
        sys.exit(2)

    fork_path = None
    out_path = "frua.rsc"
    exclude = set()
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == "-o":
            i += 1
            if i >= len(argv):
                sys.exit("rsrcpack: -o needs an argument")
            out_path = argv[i]
        elif arg in ("-x", "--exclude"):
            i += 1
            if i >= len(argv):
                sys.exit("rsrcpack: --exclude needs a 4-char type")
            exclude.add(argv[i].encode("mac-roman").ljust(4)[:4]
                        .decode("mac-roman"))
        elif arg.startswith("-"):
            sys.exit(f"rsrcpack: unknown option {arg!r}")
        elif fork_path is None:
            fork_path = arg
        else:
            sys.exit(f"rsrcpack: unexpected argument {arg!r}")
        i += 1
    if fork_path is None:
        sys.exit("rsrcpack: no resource fork given")

    rf = ResourceFork.from_file(fork_path)
    resources = rf.resources
    if exclude:
        # Drop whole resource types the runtime never reads. Chiefly 'CODE':
        # the port executes its OWN lifted C, so the original 68k CODE segments
        # (~90% of the fork) are dead weight in the runtime archive AND make
        # frua.rsc a full copy of the Mac program. Excluding them leaves only
        # the data resources the engine actually loads (DATA/DREL, clut, FONT,
        # STRS, dialog/menu templates) — a much smaller, non-executable blob.
        # The disassembly workflow reads the .rfork directly, not frua.rsc, so
        # nothing offline depends on CODE being here either.
        dropped = [r for r in resources if r.type in exclude]
        resources = [r for r in resources if r.type not in exclude]
        drop_bytes = sum(len(r.data) for r in dropped)
        print(f"  excluded {len(dropped)} resource(s) of type "
              f"{sorted(exclude)} ({drop_bytes} bytes)")
    archive = build_archive(resources)
    with open(out_path, "wb") as f:
        f.write(archive)
    print(f"  {len(resources)} resources -> {out_path} "
          f"({len(archive)} bytes)")


if __name__ == "__main__":
    main(sys.argv[1:])
