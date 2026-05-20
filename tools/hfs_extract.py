#!/usr/bin/env python3
"""Read a classic HFS volume (or DiskCopy 4.2 image) and list or extract it.

Resource forks are written as <name>.rsrc next to the data fork, ready to be
fed to rsrcpack (ADR-0007).

Usage:
    hfs_extract.py list    <image>
    hfs_extract.py extract <image> <dest-dir>
"""
import sys
import os
from machfs import Volume, Folder


def load_volume(path):
    """Load an HFS volume, transparently stripping a DiskCopy 4.2 header."""
    with open(path, "rb") as f:
        blob = f.read()
    # DiskCopy 4.2 images carry an 84-byte header; the HFS volume signature
    # 'BD' sits 1024 bytes into the volume itself.
    if blob[1108:1110] == b"BD" and blob[1024:1026] != b"BD":
        blob = blob[84:]
    v = Volume()
    v.read(blob)
    return v


def walk(folder, prefix=""):
    for name in sorted(folder):
        item = folder[name]
        path = prefix + name
        if isinstance(item, Folder):
            yield path + "/", None
            yield from walk(item, path + "/")
        else:
            yield path, item


def cmd_list(image):
    v = load_volume(image)
    for path, item in walk(v):
        if item is None:
            print(f"  [dir]                      {path}")
        else:
            d, r = len(item.data), len(item.rsrc)
            kind = f"{item.type.decode('mac-roman'):>4}/{item.creator.decode('mac-roman'):<4}"
            print(f"  {kind}  data={d:>8}  rsrc={r:>8}  {path}")


def cmd_extract(image, dest):
    v = load_volume(image)
    for path, item in walk(v):
        out = os.path.join(dest, path)
        if item is None:
            os.makedirs(out, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as f:
            f.write(item.data)
        if item.rsrc:
            with open(out + ".rsrc", "wb") as f:
                f.write(item.rsrc)
        print(f"  {path}  ({len(item.data)} data, {len(item.rsrc)} rsrc)")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    mode = sys.argv[1]
    if mode == "list":
        cmd_list(sys.argv[2])
    elif mode == "extract" and len(sys.argv) == 4:
        cmd_extract(sys.argv[2], sys.argv[3])
    else:
        print(__doc__)
        sys.exit(2)
