"""tools/mkdatadisks.py — the player's data-disk tool, on a synthetic game
folder (no SSI data): what goes onto the disks, the packing, disk 1's
extras, and that every image reads back.
"""
import os
import random
import shutil
import subprocess
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import mkdatadisks  # noqa: E402

XDF = os.path.join(HERE, '..', 'tools', '.venv', 'bin', 'xdftool')


@pytest.fixture
def game(tmp_path):
    """A fake staged game folder: root files of assorted sizes, two designs,
    saves to keep and to drop, things that must never be packed."""
    rnd = random.Random(3)
    g = tmp_path / 'game'
    g.mkdir()

    def put(rel, size):
        p = g.joinpath(*rel.split('/'))
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(bytes(rnd.getrandbits(8) for _ in range(size)))

    put('frua.rsc', 29000)
    for i in range(20):
        put('ART%02d.TLB' % i, rnd.randint(40000, 290000))
        put('ART%02d.ctl' % i, 1000)
    for i in range(30):
        put('DATA%02d.DAT' % i, rnd.randint(100, 20000))
    put('CKIT.EXE', 5000)            # never packed
    put('DBG.LOG', 10)
    put('frua.prg', 7000)
    put('CHAR0001.CHR', 100)
    put('HEIRS.DSN/GEO001.DAT', 9000)
    put('HEIRS.DSN/SAVE/SAVGAMA.CSV', 12000)   # Save A ships
    put('HEIRS.DSN/SAVE/VAULTA.DAT', 866)
    put('HEIRS.DSN/SAVE/SAVGAMB.CSV', 12000)   # the player's: dropped
    put('HEIRS.DSN/SAVGAMA.CSV', 12000)        # stray at the design root
    put('HEIRS.DSN/BARBARUS.CCH', 400)
    put('OTHER.DSN/GEO001.DAT', 5000)          # not asked for
    return str(g)


def test_file_list_rules(game):
    files = mkdatadisks.file_list(game, ['HEIRS.DSN'], 'tlb')
    assert 'frua.rsc' in files
    assert not [f for f in files if f.lower().endswith('.ctl')]
    for never in ('CKIT.EXE', 'DBG.LOG', 'frua.prg', 'CHAR0001.CHR'):
        assert never not in files
    assert 'HEIRS.DSN/SAVE/SAVGAMA.CSV' in files
    assert 'HEIRS.DSN/SAVE/VAULTA.DAT' in files
    assert 'HEIRS.DSN/SAVE/SAVGAMB.CSV' not in files
    assert 'HEIRS.DSN/SAVGAMA.CSV' not in files
    assert 'HEIRS.DSN/BARBARUS.CCH' not in files
    assert not [f for f in files if f.startswith('OTHER.DSN')]
    ctl = mkdatadisks.file_list(game, ['HEIRS.DSN'], 'ctl')
    assert not [f for f in ctl if f.lower().endswith('.tlb')]


@pytest.mark.parametrize('family', mkdatadisks.FAMILIES)
def test_pack_places_every_file_once(game, family):
    files = mkdatadisks.file_list(game, ['HEIRS.DSN'], 'tlb')
    disks = mkdatadisks.pack(game, files, family, 200000)
    flat = [f for d in disks for f in d]
    assert sorted(flat) == sorted(files)
    if family == 'gotek':
        assert len(disks) == 1


@pytest.mark.parametrize('family', mkdatadisks.FAMILIES)
def test_make_family_images(game, tmp_path, family):
    inst = tmp_path / 'inst'
    inst.mkdir()
    for n in ('INSTDISK.TTP', 'INSTDISK.PRG', 'instdisk'):
        (inst / n).write_bytes(b'installer ' + n.encode())
    out = str(tmp_path / 'out')
    files = mkdatadisks.file_list(game, ['HEIRS.DSN'], 'tlb')
    work = str(tmp_path / 'work')
    os.makedirs(out)
    # a stale disk from an earlier, longer set must go
    ext = 'adf' if family == 'amiga' else 'st'
    stale = os.path.join(out, 'openua-data-%s-disk9.%s' % (family, ext))
    open(stale, 'wb').write(b'old')
    paths = mkdatadisks.make_family(family, game, files, out, 'tlb', 'off',
                                    '', str(inst), work)
    assert not os.path.exists(stale)
    assert os.path.exists(os.path.join(out, 'README-DATA-%s.txt' % family))
    n = len(paths)
    got = {}
    for i, p in enumerate(paths, 1):
        names = _read_all(p, family)
        lst = names.pop('DISK.LST').decode().splitlines()
        assert lst[0] == '%d %d OpenUA game data' % (i, n)
        listed = lst[1:]
        if i == 1:
            assert names.pop('NDISKS') == str(n).encode()
            want = (['instdisk'] if family == 'amiga'
                    else ['INSTDISK.TTP', 'INSTDISK.PRG'])
            for w in want:
                assert names.pop(w.upper()) == b'installer ' + w.encode()
        assert sorted(k for k in names) == sorted(f.upper() for f in listed)
        got.update(names)
    for f in files:
        src = open(os.path.join(game, *f.split('/')), 'rb').read()
        assert got[f.upper()] == src, f


def _read_all(img, family):
    """{UPPER relative path: bytes} via mtools / amitools (skip if absent)."""
    import tempfile
    td = tempfile.mkdtemp()
    try:
        if family == 'amiga':
            xdf = XDF if os.path.exists(XDF) else shutil.which('xdftool')
            if not xdf:
                pytest.skip('amitools not installed')
            subprocess.run([xdf, img, 'unpack', td], check=True,
                           capture_output=True)
            root = os.path.join(td, [d for d in os.listdir(td)
                                     if os.path.isdir(os.path.join(td, d))][0])
        else:
            if not shutil.which('mcopy'):
                pytest.skip('mtools not installed')
            subprocess.run(['mcopy', '-s', '-n', '-i', img, '::/*', td],
                           check=True, capture_output=True)
            root = td
        out = {}
        for r, _, fs in os.walk(root):
            for f in fs:
                if f.endswith('.uaem') or f.endswith('.xdfmeta'):
                    continue
                rel = os.path.relpath(os.path.join(r, f), root)
                out[rel.replace(os.sep, '/').upper()] = \
                    open(os.path.join(r, f), 'rb').read()
        return out
    finally:
        shutil.rmtree(td)


def test_missing_installers_still_make_disks(game, tmp_path, monkeypatch):
    monkeypatch.setattr(mkdatadisks, 'HERE', str(tmp_path / 'nowhere'))
    out = str(tmp_path / 'out')
    files = mkdatadisks.file_list(game, ['HEIRS.DSN'], 'tlb')
    paths = mkdatadisks.make_family('atari', game, files, out, 'tlb', 'off',
                                    '', '', str(tmp_path / 'w'))
    assert paths


def test_module_set_on_amiga(game, tmp_path):
    out = str(tmp_path / 'out')
    files = mkdatadisks.file_list(game, ['HEIRS.DSN'], 'tlb')
    paths = mkdatadisks.make_family('amiga', game, files, out, 'tlb', 'off',
                                    'Curse', '', str(tmp_path / 'w'))
    names = _read_all(paths[0], 'amiga')
    assert names['DISK.LST'].decode().splitlines()[0].endswith('Curse module')
    script = names['INSTALL.SCRIPT'].decode()
    assert 'of %d' % len(paths) in script
    assert 'Curse-Data-' in script
    assert 'INSTALL.INFO' in names and 'INSTALL' in names


def test_source_must_be_a_game_or_a_dos_install(tmp_path):
    with pytest.raises(SystemExit):
        mkdatadisks.resolve_source(str(tmp_path), str(tmp_path / 'w'))
