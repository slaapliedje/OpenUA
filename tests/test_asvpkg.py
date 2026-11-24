"""tools/asvpkg.py writes the SVR4 package datastream ASV's pkgadd reads.

The layout is ASV's own pkgtrans's, read off a package it made: a 512-byte
text header, then two newc cpio archives each padded to 512 bytes, the
first PKG/pkginfo + PKG/pkgmap, the second pkginfo, pkgmap and root/<path>. These tests parse what the writer makes with a
reader written from that description, not from the writer.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools'))
import asvpkg  # noqa: E402

INFO = """PKG=TESTPKG
NAME=a test
ARCH=m68k
VERSION=1.0
"""


def read_newc(d, pos):
    """[(name, mode, data)] of the newc archive at pos, and where it ends"""
    out = []
    while True:
        assert d[pos:pos + 6] == b'070701', 'no newc header at %d' % pos
        h = d[pos:pos + 110]
        mode, nlink = int(h[14:22], 16), int(h[38:46], 16)
        size, namesize = int(h[54:62], 16), int(h[94:102], 16)
        name = d[pos + 110:pos + 110 + namesize - 1].decode()
        assert d[pos + 110 + namesize - 1] == 0
        p = pos + 110 + namesize
        p = (p + 3) & ~3
        data = d[p:p + size]
        p = (p + size + 3) & ~3
        pos = p
        if name == 'TRAILER!!!':
            # ASV's cpio takes a trailer with no links for a bad header
            assert nlink == 1
            return out, pos
        out.append((name, mode, data))


def parse(d):
    head = d[:512]
    text = head.rstrip(b'\0').decode()
    lines = text.splitlines()
    assert lines[0] == '# PaCkAgE DaTaStReAm'
    assert lines[-1] == '# end of header'
    assert all(b == 0 for b in head[len(text):])
    a1, end1 = read_newc(d, 512)
    start2 = (end1 + 511) // 512 * 512
    assert all(b == 0 for b in d[end1:start2])
    a2, end2 = read_newc(d, start2)
    assert len(d) == (end2 + 511) // 512 * 512
    return lines[1].split(), a1, a2


def sysv_sum(data):
    s = sum(data)
    while s >> 16:
        s = (s & 0xffff) + (s >> 16)
    return s


def make(items):
    return asvpkg.build(INFO, items)


def test_layout_and_pkgmap():
    exe = bytes(range(256)) * 300            # a byte sum that needs folding
    d = make([('/opt/t/bin/prog', exe, 0o755), ('/opt/t/doc/README', b'hello\n', 0o644)])
    hdr, a1, a2 = parse(d)
    assert hdr[0] == 'TESTPKG' and hdr[1] == '1'
    assert [n for n, _, _ in a1] == ['TESTPKG/pkginfo', 'TESTPKG/pkgmap']
    names = [n for n, _, _ in a2]
    assert names == ['pkginfo', 'pkgmap', 'root', 'root/opt', 'root/opt/t', 'root/opt/t/bin',
                     'root/opt/t/bin/prog', 'root/opt/t/doc', 'root/opt/t/doc/README']
    files = {n: (m, data) for n, m, data in a2}
    assert files['root/opt/t/bin/prog'] == (0o100755, exe)
    assert files['root/opt/t/doc/README'][0] == 0o100644
    assert files['root/opt/t'][0] == 0o040755
    # the two copies of pkginfo and pkgmap agree
    assert a1[0][2] == files['pkginfo'][1] and a1[1][2] == files['pkgmap'][1]

    pkgmap = files['pkgmap'][1].decode().splitlines()
    assert pkgmap[0] == ': 1 %s' % hdr[2]
    by_path = {}
    for line in pkgmap[1:]:
        f = line.split()
        assert f[0] == '1'
        by_path[f[2] if f[1] == 'i' else f[3]] = f
    assert by_path['/opt/t/bin'][1:] == ['d', 'none', '/opt/t/bin', '0755', 'root', 'sys']
    prog = by_path['/opt/t/bin/prog']
    assert prog[1:8] == ['f', 'none', '/opt/t/bin/prog', '0755', 'root', 'bin', str(len(exe))]
    assert int(prog[8]) == sysv_sum(exe)
    info = by_path['pkginfo']
    assert info[1] == 'i' and int(info[3]) == len(files['pkginfo'][1])
    assert int(info[4]) == sysv_sum(files['pkginfo'][1])


def test_pkginfo_keeps_fields_and_defaults_classes():
    d = make([('/opt/t/f', b'x', 0o644)])
    _, a1, _ = parse(d)
    info = dict(l.split('=', 1) for l in a1[0][2].decode().splitlines())
    assert info['PKG'] == 'TESTPKG' and info['VERSION'] == '1.0' and info['CLASSES'] == 'none'


def test_same_input_same_bytes():
    items = [('/opt/t/b', b'b', 0o644), ('/opt/t/a', b'a', 0o755)]
    assert make(items) == make(list(reversed(items)))


def test_checksum_is_pkgmks():
    # the fold pkgmk uses (checked against less-704-2.pkg's pkgmap, whose
    # sums pkgchk accepts on the TT), for sums that need one fold or two
    assert asvpkg.cksum(b'') == 0
    assert asvpkg.cksum(b'\xff' * 70000) == sysv_sum(b'\xff' * 70000)
    for n in (1, 255, 65535, 65536, 1 << 20):
        data = bytes([0xff]) * n
        assert asvpkg.cksum(data) == sysv_sum(data)
    # a sum of 0xffffffff folds to 0x1fffe, which folds again
    assert asvpkg.cksum(b'\xff' * 16843009) == 0xffff


def test_a_file_where_a_directory_is_refused():
    try:
        make([('/opt/t', b'x', 0o644), ('/opt/t/f', b'y', 0o644)])
    except SystemExit:
        return
    raise AssertionError('accepted /opt/t as both a file and a directory')
