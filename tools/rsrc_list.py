#!/usr/bin/env python3
"""List the contents of a classic Mac resource fork.

Parses the resource map and reports each resource type with its count and
total data size. Pass a 4-character type to list that type's resources
individually (id, size, name) -- e.g. CODE for the 68k program segments.

Usage:
    rsrc_list.py <resource-fork>            summary by type
    rsrc_list.py <resource-fork> <TYPE>     individual resources of TYPE
"""
import sys
import struct


def load(path):
    with open(path, "rb") as f:
        return f.read()


def parse(blob):
    """Yield (type, id, name, size) for every resource in the fork."""
    data_off, map_off, _data_len, _map_len = struct.unpack_from(">IIII", blob, 0)
    type_list_off = struct.unpack_from(">H", blob, map_off + 24)[0]
    name_list_off = struct.unpack_from(">H", blob, map_off + 26)[0]
    type_list = map_off + type_list_off
    name_list = map_off + name_list_off

    n_types = struct.unpack_from(">H", blob, type_list)[0] + 1
    for t in range(n_types):
        base = type_list + 2 + t * 8
        rtype = blob[base:base + 4]
        count = struct.unpack_from(">H", blob, base + 4)[0] + 1
        ref_list = type_list + struct.unpack_from(">H", blob, base + 6)[0]
        for r in range(count):
            ref = ref_list + r * 12
            rid = struct.unpack_from(">h", blob, ref)[0]
            name_off = struct.unpack_from(">H", blob, ref + 2)[0]
            data_ptr = struct.unpack_from(">I", blob, ref + 4)[0] & 0x00FFFFFF
            size = struct.unpack_from(">I", blob, data_off + data_ptr)[0]
            name = ""
            if name_off != 0xFFFF:
                ln = blob[name_list + name_off]
                name = blob[name_list + name_off + 1:
                            name_list + name_off + 1 + ln].decode(
                                "mac-roman", "replace")
            yield rtype.decode("mac-roman", "replace"), rid, name, size


def main(argv):
    if not argv:
        print(__doc__)
        sys.exit(2)
    blob = load(argv[0])
    res = list(parse(blob))

    if len(argv) >= 2:
        want = argv[1]
        rows = [r for r in res if r[0] == want]
        if not rows:
            sys.exit(f"no resources of type {want!r}")
        total = 0
        for rtype, rid, name, size in sorted(rows, key=lambda r: r[1]):
            print(f"  {rtype} {rid:>6}  {size:>9}  {name}")
            total += size
        print(f"  -- {len(rows)} resources, {total} bytes")
        return

    by_type = {}
    for rtype, rid, name, size in res:
        c, s = by_type.get(rtype, (0, 0))
        by_type[rtype] = (c + 1, s + size)
    print(f"  {'type':<6} {'count':>6} {'bytes':>11}")
    grand = 0
    for rtype in sorted(by_type):
        c, s = by_type[rtype]
        print(f"  {rtype:<6} {c:>6} {s:>11}")
        grand += s
    print(f"  -- {len(res)} resources in {len(by_type)} types, {grand} bytes")


if __name__ == "__main__":
    main(sys.argv[1:])
