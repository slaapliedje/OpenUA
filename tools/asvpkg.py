#!/usr/bin/env python3
"""asvpkg.py - write an SVR4 package datastream (what `pkgtrans` makes and
`pkgadd -d FILE` installs) for Atari System V, from files on the PC.

    asvpkg.py OUT.pkg PKGINFO_FILE  DEST=SRC[:MODE] ...

PKGINFO_FILE holds the KEY=value lines of the package's pkginfo (PKG,
NAME, ARCH, VERSION, CATEGORY, ...; CLASSES defaults to "none"). Each
DEST=SRC installs the file SRC at the absolute path DEST, mode 0755 for
an executable SRC and 0644 otherwise unless MODE is given. A DEST ending
in '/' with no SRC (`/opt/openua/doc/=`) is just a directory. Directories
leading to every file are made 0755 root sys, except the system's own
(SYSDIRS), which are left out; files belong to root bin.

The layout is the one ASV's own pkgtrans writes (read off a package it
made in Hatari, which ASV's pkgadd installs):

  - a text header, "# PaCkAgE DaTaStReAm", one "PKG PARTS BLOCKS" line and
    "# end of header", NUL-padded to 512 bytes;
  - a newc cpio archive (magic 070701; what ASV's `cpio -c` means) of
    PKG/pkginfo and PKG/pkgmap, padded to 512;
  - a newc cpio archive of pkginfo, pkgmap and root/<every path>, padded
    to 512.

pkgadd unpacks with `cpio -icdumD -C 512`. Its cpio refuses a TRAILER!!!
entry whose link count is 0, as GNU cpio writes it, with "Bad magic
number/header"; ASV's pkgtrans gives it 1.

pkgmap lines are "1 f none PATH MODE OWNER GROUP SIZE SUM MTIME" (d for a
directory, i for pkginfo), headed ": 1 BLOCKS". SUM is the byte sum folded
to 16 bits, as pkgmk computes it (and `pkgchk` checks). Everything is
written in sorted order with one fixed time, so the same inputs give the
same bytes.
"""
import os
import sys

MTIME = 1767225600          # 2026-01-01: fixed, for byte-identical output

# The system's own directories are left out of the package. Listed, pkgadd
# compares their mode and owner with the disk's and stops for an answer
# when they differ ("attribute change only"), as they do on a TT that has
# been lived in. (Found by the asv-games packages, which share this tool.)
SYSDIRS = {'/opt', '/usr', '/usr/bin', '/usr/lib', '/usr/share', '/var',
           '/var/opt', '/etc', '/etc/opt', '/usr/local', '/usr/local/bin'}


def cksum(data):
    """pkgmk's checksum: the sum of the bytes, folded to 16 bits"""
    s = sum(data)
    while s >> 16:
        s = (s & 0xffff) + (s >> 16)
    return s


def newc(entries):
    """a newc cpio archive of (name, mode, data) entries plus its trailer.
    The trailer's link count is 1, as ASV's pkgtrans writes it: ASV's cpio
    calls a trailer with 0 a "Bad magic number/header"."""
    out = bytearray()
    ino = 1
    for name, mode, data in list(entries) + [('TRAILER!!!', 0, b'')]:
        nm = name.encode() + b'\0'
        if name == 'TRAILER!!!':
            fields = (0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, len(nm), 0)
        else:
            fields = (ino, mode, 0, 0, 2 if mode & 0o040000 else 1, MTIME,
                      len(data), 0, 0, 0, 0, len(nm), 0)
        out += b'070701' + b''.join(b'%08X' % f for f in fields) + nm
        out += b'\0' * (-len(out) % 4)
        out += data
        out += b'\0' * (-len(out) % 4)
        ino += 1
    return bytes(out)


def pad512(b):
    return b + b'\0' * (-len(b) % 512)


def parse_pkginfo(text):
    info = {}
    for line in text.splitlines():
        line = line.strip()
        if line and not line.startswith('#'):
            k, _, v = line.partition('=')
            info[k.strip()] = v.strip()
    for k in ('PKG', 'NAME', 'ARCH', 'VERSION'):
        if k not in info:
            raise SystemExit('pkginfo: no %s' % k)
    info.setdefault('CLASSES', 'none')
    return info


def build(pkginfo_text, items):
    """the datastream's bytes. items: (dest, data or None, mode)"""
    info = parse_pkginfo(pkginfo_text)
    pkg = info['PKG']
    order = ('PKG', 'NAME', 'ARCH', 'VERSION', 'CATEGORY', 'CLASSES')
    keys = [k for k in order if k in info] + sorted(k for k in info if k not in order)
    pkginfo = ''.join('%s=%s\n' % (k, info[k]) for k in keys).encode()

    files, dirs = {}, set()
    for dest, data, mode in items:
        if not dest.startswith('/'):
            raise SystemExit('%s: not an absolute path' % dest)
        path = dest.rstrip('/')
        if data is None:
            dirs.add(path)
        else:
            files[path] = (data, mode)
        d = os.path.dirname(path)
        while d not in ('/', ''):
            dirs.add(d)
            d = os.path.dirname(d)
    dirs -= SYSDIRS
    clash = dirs & set(files)
    if clash:
        raise SystemExit('both a file and a directory: %s' % ', '.join(sorted(clash)))

    paths = sorted(dirs | set(files))
    body = sum(len(files[p][0]) for p in files) + len(pkginfo)
    blocks = (body + 511) // 512 + len(paths) + 8

    lines = [': 1 %d' % blocks]
    for p in paths:
        if p in dirs:
            lines.append('1 d none %s 0755 root sys' % p)
        else:
            data, mode = files[p]
            lines.append('1 f none %s %04o root bin %d %d %d'
                         % (p, mode, len(data), cksum(data), MTIME))
    lines.append('1 i pkginfo %d %d %d' % (len(pkginfo), cksum(pkginfo), MTIME))
    pkgmap = ('\n'.join(lines) + '\n').encode()

    head = ('# PaCkAgE DaTaStReAm\n%s 1 %d\n# end of header\n' % (pkg, blocks)).encode()
    out = pad512(head)
    out += pad512(newc([(pkg + '/pkginfo', 0o100644, pkginfo),
                        (pkg + '/pkgmap', 0o100644, pkgmap)]))
    payload = [('pkginfo', 0o100644, pkginfo), ('pkgmap', 0o100644, pkgmap),
               ('root', 0o040755, b'')]
    for p in paths:
        if p in dirs:
            payload.append(('root' + p, 0o040755, b''))
        else:
            data, mode = files[p]
            payload.append(('root' + p, 0o100000 | mode, data))
    out += pad512(newc(payload))
    return out


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__.split('\n\n')[1])
    out, info_file, specs = argv[0], argv[1], argv[2:]
    items = []
    for spec in specs:
        dest, _, src = spec.partition('=')
        mode = None
        if ':' in src:
            src, _, m = src.rpartition(':')
            mode = int(m, 8)
        if not src:
            items.append((dest, None, None))
            continue
        data = open(src, 'rb').read()
        if mode is None:
            mode = 0o755 if os.access(src, os.X_OK) else 0o644
        items.append((dest, data, mode))
    with open(info_file) as f:
        stream = build(f.read(), items)
    with open(out, 'wb') as f:
        f.write(stream)
    print('%s: %d bytes' % (out, len(stream)))


if __name__ == '__main__':
    main(sys.argv[1:])
