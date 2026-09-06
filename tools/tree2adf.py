#!/usr/bin/env python3
"""tree2adf.py — spread ANY directory tree over 880 KB Amiga FFS floppy images.

    tools/tree2adf.py <srcdir> <outdir> <label-base> [--readme FILE]

For a Gotek: a bare file on the stick is invisible to the Amiga, which only
sees whatever IMAGE the Gotek is serving as DF0:. So anything meant for the
machine (a driver archive, a commercial package the user owns, a module) has
to become .adf images first. mkdatadisks.sh does this for OpenUA data with
manifests; this does it for an arbitrary tree with no installer semantics.

First use (2026-09-06): Roadshow 1.15, 4.7 MB / 263 files -> 6 disks, every
file read back out of the images and diffed against the source before the
set went on the stick. Directory blocks are charged ONCE per disk — charging
them per file over-packed the same tree into 7 disks with the last 91% empty.

Every disk keeps the tree's directory structure, so copying each disk's
contents into ONE drawer on the Amiga merges back into the original tree.
Files are packed whole, largest first (first-fit decreasing); nothing is
split. Disk 1 also carries the README if given. Volumes are labelled
<label-base>-1, -2, ...  Writes are checked; a failed write aborts.
"""
import os
import subprocess
import sys

XDFTOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".venv", "bin", "xdftool")
CAP_BLOCKS = 1740          # usable 512-byte blocks on an 880 KB FFS floppy, after root+bitmap+slack
UNIT = 512


def cost(nbytes):
    """Blocks a file costs on FFS: data + file header + an extension block per 72 data blocks."""
    data = (nbytes + UNIT - 1) // UNIT
    return data + 1 + data // 72


def main():
    args = sys.argv[1:]
    readme = None
    if "--readme" in args:
        i = args.index("--readme")
        readme = args[i + 1]
        del args[i:i + 2]
    src, out, base = args
    files = []
    for r, _d, fs in os.walk(src):
        for f in fs:
            p = os.path.join(r, f)
            files.append((os.path.relpath(p, src), os.path.getsize(p)))
    files.sort(key=lambda x: -x[1])
    disks = []                                    # list of [used_blocks, [(rel, size)], dirs_on_disk]
    reserve = cost(os.path.getsize(readme)) + 8 if readme else 0

    def dir_levels(rel):
        parts = rel.split("/")[:-1]
        return {"/".join(parts[:k]) for k in range(1, len(parts) + 1)}

    for rel, sz in files:
        need = dir_levels(rel)
        if cost(sz) + len(need) > CAP_BLOCKS - reserve:
            sys.exit("file too big for one disk: %s (%d bytes)" % (rel, sz))
        for d in disks:
            first = d is disks[0]
            # a directory block is paid ONCE per disk, not once per file
            c = cost(sz) + len(need - d[2])
            if d[0] + c <= CAP_BLOCKS - (reserve if first else 0):
                d[0] += c
                d[1].append((rel, sz))
                d[2] |= need
                break
        else:
            disks.append([cost(sz) + len(need), [(rel, sz)], set(need)])
    os.makedirs(out, exist_ok=True)
    for n, (used, members, _dirs) in enumerate(disks, 1):
        img = os.path.join(out, "%s-Disk%d.adf" % (base, n))
        if os.path.exists(img):
            os.remove(img)
        subprocess.run([XDFTOOL, img, "create", "+", "format", "%s-%d" % (base, n), "ffs"],
                       check=True, capture_output=True)
        dirs = sorted({os.path.dirname(rel) for rel, _ in members if os.path.dirname(rel)},
                      key=lambda d: (d.count("/"), d))
        made = set()
        for d in dirs:
            parts = d.split("/")
            for k in range(1, len(parts) + 1):
                sub = "/".join(parts[:k])
                if sub in made:
                    continue
                subprocess.run([XDFTOOL, img, "makedir", sub], check=True, capture_output=True)
                made.add(sub)
        for rel, _ in sorted(members):
            d = os.path.dirname(rel)
            cmd = [XDFTOOL, img, "write", os.path.join(src, rel)] + ([d] if d else [])
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                sys.exit("WRITE FAILED on disk %d: %s\n%s" % (n, rel, r.stderr))
        if n == 1 and readme:
            subprocess.run([XDFTOOL, img, "write", readme, "README.txt"], check=True, capture_output=True)
        info = subprocess.run([XDFTOOL, img, "info"], capture_output=True, text=True).stdout
        free = [l for l in info.splitlines() if l.startswith("free:")]
        print("%s  %3d files  %s" % (os.path.basename(img), len(members), free[0] if free else ""))
    print("%d disks" % len(disks))


if __name__ == "__main__":
    main()
