"""tools/diskimg.py — the pure-Python FAT12 and Amiga FFS image writers.

The read-back tests use mtools (mtype, fsck.fat) and amitools (xdftool and
its Validator) as independent readers, and skip when those are absent.
"""
import os
import random
import shutil
import struct
import subprocess
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import diskimg  # noqa: E402


def sample_files():
    rnd = random.Random(7)
    files = {}

    def f(path, size):
        files[path] = bytes(rnd.getrandbits(8) for _ in range(size))

    f('EMPTY.DAT', 0)
    f('ONE.DAT', 512)
    f('ODD.DAT', 513)
    f('BIG.TLB', 120000)            # > 72 blocks: FFS extension blocks
    f('frua.rsc', 30000)
    for i in range(30):              # enough names to chain in the hash table
        f('F%02d.DAT' % i, rnd.randint(1, 5000))
    f('HEIRS.DSN/GEO001.DAT', 4000)
    f('HEIRS.DSN/SAVE/SAVGAMA.CSV', 12000)
    f('HEIRS.DSN/SAVE/VAULTA.DAT', 866)
    # a data block that LOOKS like a header block (type 2, key 0): a writer
    # that picked blocks to checksum by their contents corrupted these
    files['TRAP.DAT'] = b'\x00\x00\x00\x02' + bytes(508)
    return files


# --- FAT12 ------------------------------------------------------------------

# The boot-sector values mformat writes for the same geometry, measured
# 2026-11-26 (mformat -f 1440 / -f 720 / -t 255 -h 2 -n 36).
MFORMAT_BPB = {
    'atari':    (512, 1, 1, 2, 224, 2880, 0xF0, 9, 18, 2),
    'atari720': (512, 2, 1, 2, 112, 1440, 0xF9, 3, 9, 2),
    'gotek':    (512, 8, 1, 2, 512, 18360, 0xF0, 7, 36, 2),
}


@pytest.mark.parametrize('geo', sorted(MFORMAT_BPB))
def test_fat_boot_sector_matches_mformat(geo):
    img = diskimg.FatImage(geo).build('OPENUA')
    bpb = struct.unpack('<HBHBHHBHHH', img[11:28])
    assert bpb == MFORMAT_BPB[geo]
    assert img[510:512] == b'\x55\xaa'
    # TOS would EXECUTE a boot sector whose big-endian words sum to 0x1234
    assert sum(struct.unpack('>256H', img[:512])) & 0xFFFF != 0x1234
    assert len(img) == bpb[5] * 512


@pytest.mark.parametrize('geo', sorted(MFORMAT_BPB))
def test_fat_reads_back_with_mtools(geo, tmp_path):
    if not shutil.which('mtype'):
        pytest.skip('mtools not installed')
    files = sample_files()
    im = diskimg.FatImage(geo)
    for p, d in files.items():
        im.add_file(p, d)
    out = str(tmp_path / 'd.st')
    im.save(out, 'OpenUA-D1')
    for p, d in files.items():
        r = subprocess.run(['mtype', '-i', out, '::' + p], capture_output=True)
        assert r.returncode == 0 and r.stdout == d, p
    if shutil.which('fsck.fat'):
        r = subprocess.run(['fsck.fat', '-n', out], capture_output=True, text=True)
        assert r.returncode == 0, r.stdout + r.stderr


def test_fat_rejects_long_names():
    im = diskimg.FatImage('atari')
    with pytest.raises(ValueError):
        im.add_file('TOOLONGNAME.DAT', b'x')
    with pytest.raises(ValueError):
        im.add_file('A.LONG', b'x')


def test_fat_full_disk_raises():
    im = diskimg.FatImage('atari720')
    for i in range(8):
        im.add_file('F%d.DAT' % i, bytes(100000))
    with pytest.raises(diskimg.DiskFull):
        im.build('X')


def test_fat_root_entry_limit_raises():
    im = diskimg.FatImage('atari720')            # 112 root entries
    for i in range(112):                         # + the volume label = 113
        im.add_file('F%03d.DAT' % i, b'x')
    with pytest.raises(diskimg.DiskFull):
        im.build('X')


# --- Amiga FFS --------------------------------------------------------------

def _adf(files):
    im = diskimg.AdfImage()
    for p, d in files.items():
        im.add_file(p, d)
    return im


def test_adf_layout_basics():
    img = _adf({'A.DAT': b'hello'}).build('OpenUA-Data-1')
    assert len(img) == 1760 * 512
    assert img[:4] == b'DOS\x01'
    root = img[880 * 512:881 * 512]
    assert sum(struct.unpack('>128I', root)) & 0xFFFFFFFF == 0
    assert root[432] == len('OpenUA-Data-1')
    assert root[433:433 + 13] == b'OpenUA-Data-1'


def test_adf_reads_back_with_amitools(tmp_path):
    xdf = os.path.join(HERE, '..', 'tools', '.venv', 'bin', 'xdftool')
    xdf = xdf if os.path.exists(xdf) else shutil.which('xdftool')
    if not xdf:
        pytest.skip('amitools not installed')
    files = sample_files()
    out = str(tmp_path / 'd.adf')
    _adf(files).save(out, 'OpenUA-Data-1')
    for p, d in files.items():
        dst = tmp_path / 'rb'
        if dst.exists():
            dst.unlink()
        r = subprocess.run([xdf, out, 'read', p, str(dst)], capture_output=True)
        assert r.returncode == 0 and dst.read_bytes() == d, p


def test_adf_validates_clean():
    pytest.importorskip('amitools')
    from amitools.fs.blkdev.BlkDevFactory import BlkDevFactory
    from amitools.fs.validate.Validator import Validator
    import tempfile
    with tempfile.NamedTemporaryFile(suffix='.adf', delete=False) as fh:
        path = fh.name
    try:
        _adf(sample_files()).save(path, 'OpenUA-Data-1')
        dev = BlkDevFactory().open(path, read_only=True)
        v = Validator(dev, min_level=0)
        v.scan_boot()
        v.scan_root()
        v.scan_dir_tree()
        v.scan_files()
        v.scan_bitmap()
        assert v.get_summary() == (0, 0)
        dev.close()
    finally:
        os.unlink(path)


def test_adf_full_disk_raises():
    im = diskimg.AdfImage()
    for i in range(10):
        im.add_file('F%d.DAT' % i, bytes(100000))
    with pytest.raises(diskimg.DiskFull):
        im.build('X')


def test_adf_rejects_bad_names():
    im = diskimg.AdfImage()
    with pytest.raises(ValueError):
        im.add_file('A' * 31, b'x')
    with pytest.raises(ValueError):
        im.add_file('A:B', b'x')


def test_images_are_deterministic():
    files = sample_files()
    a1 = _adf(files).build('L')
    a2 = _adf(files).build('L')
    assert a1 == a2
    f = diskimg.FatImage('atari')
    g = diskimg.FatImage('atari')
    for p, d in files.items():
        f.add_file(p, d)
        g.add_file(p, d)
    assert f.build('L') == g.build('L')
