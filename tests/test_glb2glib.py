"""glb2glib — the DOS HLIB -> Mac GLIB root-data-bank converter.

The strongest possible check exists here: under the conversion law
(container swap + item-0 directory u16 swap + raw entries + even pad)
the DOS GAME.GLB and GEO.GLB must reproduce the Mac release's files
BYTE-IDENTICALLY, and MONST.GLB to within its 2 known v1.2 data-edit
bytes. Skips what isn't staged under data/.
"""
import glob
import os
import struct
import sys

import pytest

sys.path.insert(0, "tools")
import glb2glib  # noqa: E402

DOS_ROOT = "data/dos-frua"
MAC_ROOT = "data/frua-mac/joined"

pytestmark = pytest.mark.skipif(
    not os.path.isdir(DOS_ROOT),
    reason="DOS corpus not staged under data/",
)


def _dos(name):
    hits = glob.glob("%s/DISK*/%s.GLB" % (DOS_ROOT, name))
    return hits[0] if hits else None


def _mac(name):
    for d in ("Disk1", "Disk2", "Disk3", "Disk4"):
        p = "%s/%s/%s.GLB" % (MAC_ROOT, d, name)
        if os.path.isfile(p):
            return p
    return None


def test_converted_banks_walk_as_glib():
    for name in ("GAME", "GEO", "MONST", "SCRIPT", "STRG"):
        p = _dos(name)
        assert p is not None, name
        conv = glb2glib.convert(open(p, "rb").read())
        assert conv is not None, name
        assert conv[:4] == b"GLIB" and conv[12:16] == b"DATA", name
        size, count = struct.unpack(">IH", conv[4:10])
        assert size == len(conv) and len(conv) % 2 == 0, name
        offs = struct.unpack(">%dI" % (count + 1), conv[16:16 + 4 * (count + 1)])
        assert offs[0] == 16 + 4 * (count + 1), name
        assert all(offs[i] <= offs[i + 1] for i in range(count)), name
        assert offs[count] <= len(conv), name
        # item 0: a [count + (id,index) pairs] directory on the banks
        # that have one (SCRIPT's item 0 is a Pascal-string label)
        dcount = struct.unpack(">H", conv[offs[0]:offs[0] + 2])[0]
        if 2 + dcount * 4 == offs[1] - offs[0]:
            assert name != "SCRIPT"
            for k in range(dcount):
                _id, idx = struct.unpack(
                    ">HH", conv[offs[0] + 2 + k * 4:offs[0] + 6 + k * 4])
                assert 0 <= idx <= count, (name, k)
        else:
            assert name == "SCRIPT", name


def test_sounds_bank_is_skipped():
    hits = glob.glob("%s/DISK*/SOUNDS.GLB" % DOS_ROOT)
    assert hits
    assert glb2glib.convert(open(hits[0], "rb").read()) is None


@pytest.mark.skipif(not os.path.isdir(MAC_ROOT), reason="Mac corpus not staged")
def test_law_reproduces_the_mac_banks():
    for name, budget in (("GAME", 0), ("GEO", 0), ("MONST", 2), ("SCRIPT", 24)):
        dp, mp = _dos(name), _mac(name)
        assert dp and mp, name
        conv = glb2glib.convert(open(dp, "rb").read())
        mac = open(mp, "rb").read()
        assert len(conv) == len(mac), name
        diffs = sum(1 for a, b in zip(conv, mac) if a != b)
        assert diffs <= budget, (name, diffs)
