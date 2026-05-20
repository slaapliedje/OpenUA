#!/usr/bin/env python3
"""List the contents of a classic Mac resource fork.

Reports each resource type with its count and total data size. Pass a
4-character type to list that type's resources individually (id, size,
name) -- e.g. CODE for the 68k program segments.

Usage:
    rsrc_list.py <resource-fork>            summary by type
    rsrc_list.py <resource-fork> <TYPE>     individual resources of TYPE
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from macrsrc import ResourceFork


def main(argv):
    if not argv:
        print(__doc__)
        sys.exit(2)
    rf = ResourceFork.from_file(argv[0])

    if len(argv) >= 2:
        want = argv[1]
        rows = rf.of_type(want)
        if not rows:
            sys.exit(f"no resources of type {want!r}")
        total = 0
        for r in rows:
            print(f"  {r.type} {r.id:>6}  {len(r.data):>9}  {r.name}")
            total += len(r.data)
        print(f"  -- {len(rows)} resources, {total} bytes")
        return

    by_type = {}
    for r in rf.resources:
        c, s = by_type.get(r.type, (0, 0))
        by_type[r.type] = (c + 1, s + len(r.data))
    print(f"  {'type':<6} {'count':>6} {'bytes':>11}")
    grand = 0
    for rtype in sorted(by_type):
        c, s = by_type[rtype]
        print(f"  {rtype:<6} {c:>6} {s:>11}")
        grand += s
    n = sum(c for c, _ in by_type.values())
    print(f"  -- {n} resources in {len(by_type)} types, {grand} bytes")


if __name__ == "__main__":
    main(sys.argv[1:])
