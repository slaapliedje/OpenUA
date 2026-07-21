"""voc2glb — the DOS-VOC -> Mac SOUNDS.GLB sfx converter.

Holds the synthesized bank to the engine's contracts (jt975's GLIB
walk, jt964's piece headers, l7ee0's rate/count layout) and — when the
Mac corpus is staged — to the measured content relation: the VOC's 13
Creative-ADPCM blocks are the SAME RECORDINGS as the Mac bank's 13
DIG8 pieces (decode correlation >= 0.95 per piece, counts within 1).
Skips when the copyrighted DOS corpus is not staged under data/.
"""
import os
import struct
import sys

import pytest

sys.path.insert(0, "tools")
import voc2glb  # noqa: E402

DOS_ROOT = "data/dos-frua"
MAC_SOUNDS = "data/frua-mac/joined/Disk1/SOUNDS.GLB"

pytestmark = pytest.mark.skipif(
    not os.path.isdir(DOS_ROOT),
    reason="DOS corpus not staged under data/",
)


@pytest.fixture(scope="module")
def bank():
    voc = voc2glb.find_voc(DOS_ROOT)
    assert voc is not None, "no SFXDQ.VOC in the DOS corpus"
    return voc2glb.build_glb(open(voc, "rb").read())


def parse_glb(d):
    assert d[:4] == b"GLIB"
    size, count, zero = struct.unpack(">IHH", d[4:12])
    assert size == len(d)
    assert zero == 0
    assert d[12:16] == b"DIG8"
    offs = struct.unpack(">%dI" % (count + 1), d[16:16 + (count + 1) * 4])
    assert offs[0] == 16 + (count + 1) * 4
    assert offs[count] == len(d)
    return [d[offs[i]:offs[i + 1]] for i in range(count)]


def test_bank_shape(bank):
    pieces = parse_glb(bank)
    assert len(pieces) == 13
    for p in pieces:
        rate, count = struct.unpack(">HI", p[:6])
        assert rate in (7407, 11111, 5556)      # the three VOC rates
        assert count > 0
        assert len(p) == 6 + count + (count & 1)  # even-padded
        assert len(p) % 2 == 0


def test_samples_are_signed_full_scale(bank):
    """The x4 gain boosts every piece into the usable range (measured
    peaks 104..128 across the 13; the Mac's own copies clip at 128).
    A quiet bank = the gain step was lost."""
    pieces = parse_glb(bank)
    for i, p in enumerate(pieces):
        count = struct.unpack(">I", p[2:6])[0]
        s = [b - 256 if b > 127 else b for b in p[6:6 + count]]
        assert max(abs(v) for v in s) >= 100, i


@pytest.mark.skipif(not os.path.isfile(MAC_SOUNDS),
                    reason="Mac corpus not staged")
def test_content_matches_the_mac_recordings(bank):
    mac = open(MAC_SOUNDS, "rb").read()
    mo = struct.unpack(">14I", mac[16:16 + 56])
    pieces = parse_glb(bank)
    for i, p in enumerate(pieces):
        rate, count = struct.unpack(">HI", p[:6])
        mrate, mcount = struct.unpack(">HI", mac[mo[i]:mo[i] + 6])
        assert abs(count - mcount) <= 1, i
        assert abs(rate - mrate) <= mrate * 0.005, i      # rounding only
        a = [b - 256 if b > 127 else b for b in p[6:6 + count]]
        b = [v - 256 if v > 127 else v for v in mac[mo[i] + 6:mo[i] + 6 + mcount]]
        n = min(len(a), len(b))
        ma, mb = sum(a[:n]) / n, sum(b[:n]) / n
        num = sum((x - ma) * (y - mb) for x, y in zip(a, b))
        da = sum((x - ma) ** 2 for x in a[:n]) ** 0.5
        db = sum((y - mb) ** 2 for y in b[:n]) ** 0.5
        corr = num / (da * db) if da * db else 0
        assert corr >= 0.95, (i, round(corr, 4))


@pytest.mark.skipif(not os.path.isfile(MAC_SOUNDS),
                    reason="Mac corpus not staged")
def test_total_size_matches_the_mac_bank(bank):
    """Same recordings, same container, same padding -> the synthesized
    bank reproduces the Mac file's exact byte size."""
    assert len(bank) == os.path.getsize(MAC_SOUNDS)
