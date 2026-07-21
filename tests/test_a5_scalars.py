"""The port-authored A5 scalars must match the Mac DATA image byte-for-byte.

src/engine/a5_scalars.c states a slice of the A5 world as the port's own code
(functional constants — masks, fills, ladders, flag bytes) instead of copying
the Mac DATA resource, which is what lets a build carrying it be
redistributable (ADR-0017, task #71).

That only holds if the stated values are actually right. These tests re-derive
the pattern tables from their formulas and parse the literal tables straight
out of the C, then check both against the real DATA image. A future edit that
drifts from the original — or a mis-transcribed constant — fails here rather
than as a subtle in-game behaviour change.

The image comes from the copyrighted resource fork under the git-ignored
data/, so everything SKIPS when it is not staged.
"""
import os
import re

import pytest

RFORK = "data/work/UnlimitedAdventures.rfork"
SRC = "src/engine/a5_scalars.c"

pytestmark = pytest.mark.skipif(
    not (os.path.exists(RFORK) and os.path.exists(SRC)),
    reason="Mac resource fork not staged under data/",
)


@pytest.fixture(scope="module")
def image():
    """The expanded A5-below DATA image; index with a5_at()."""
    import sys
    sys.path.insert(0, "tools")
    from macrsrc import ResourceFork
    import datapool
    rf = ResourceFork.from_file(RFORK)
    res = {(r.type, r.id): r.data for r in rf.resources}
    return datapool.expand_data(res[("DATA", 0)], res[("ZERO", 0)])


def a5_at(img, slot, n):
    """The n bytes the Mac image holds at A5-relative `slot` (negative)."""
    base = len(img) + slot
    assert 0 <= base and base + n <= len(img), f"slot {slot} out of range"
    return img[base:base + n]


@pytest.fixture(scope="module")
def csrc():
    with open(SRC) as fh:
        return fh.read()


# --- the pattern tables, re-derived from the formulas in the C -------------

def test_right_shift_mask_ladder(image):
    """A5-4650: four (1<<(n+1))-1 bytes, then eight 0xffff>>n words."""
    want = [(1 << (i + 1)) - 1 for i in range(4)]
    for i in range(8):
        m = 0xFFFF >> i
        want += [m >> 8, m & 0xFF]
    assert bytes(want) == a5_at(image, -4650, len(want))


def test_left_shift_mask_ladder(image):
    """A5-4615: a lead 0x01, then eight (0xffff<<n) words."""
    want = [0x01]
    for i in range(8):
        m = (0xFFFF << i) & 0xFFFF
        want += [m >> 8, m & 0xFF]
    assert bytes(want) == a5_at(image, -4615, len(want))


def test_dither_pattern(image):
    """A5-3040: nine 0x55, then 0xff/0x55 alternating."""
    want = [0x55] * 9 + [0x55 if (i & 1) else 0xFF for i in range(7)]
    assert bytes(want) == a5_at(image, -3040, len(want))


def test_ff_fd_pair(image):
    want = [0xFD if (i & 1) else 0xFF for i in range(4)]
    assert bytes(want) == a5_at(image, -7224, len(want))


# --- the literal tables, parsed back out of the C -------------------------

def test_memset_fills_match_image(image, csrc):
    """Every `memset(&g_a5_byte(slot), val, n)` states what the Mac had."""
    fills = re.findall(
        r"memset\(&g_a5_byte\((-?\d+)\),\s*0x([0-9a-fA-F]+),\s*(\d+)\)", csrc)
    assert fills, "no fills parsed — has a5_scalars.c changed shape?"
    for slot, val, n in fills:
        slot, val, n = int(slot), int(val, 16), int(n)
        assert a5_at(image, slot, n) == bytes([val] * n), \
            f"fill at A5{slot} disagrees with the Mac image"


def test_single_bytes_match_image(image, csrc):
    """Every `{ slot, 0xNN }` flag/count/default byte states what the Mac had."""
    singles = re.findall(r"\{\s*(-\d+),\s*0x([0-9a-fA-F]{2})\s*\}", csrc)
    assert len(singles) > 40, f"expected the full single-byte table, got {len(singles)}"
    for slot, val in singles:
        slot, val = int(slot), int(val, 16)
        assert a5_at(image, slot, 1)[0] == val, \
            f"byte at A5{slot} disagrees with the Mac image"


def test_index_ladders_match_image(image, csrc):
    """Every `first + i * step` ladder reproduces its run."""
    ladders = re.findall(
        r"for \(i = 0; i < (\d+); i\+\+\)\s*\n\s*"
        r"g_a5_byte\((-\d+) \+ i\) = \(unsigned char\)\((\d+) \+ i \* (-?\d+)\)",
        csrc)
    assert ladders, "no index ladders parsed"
    for n, slot, first, step in ladders:
        n, slot, first, step = int(n), int(slot), int(first), int(step)
        want = bytes([(first + i * step) & 0xFF for i in range(n)])
        assert a5_at(image, slot, n) == want, \
            f"ladder at A5{slot} disagrees with the Mac image"


def test_deferred_tables_are_not_authored(csrc):
    """A5-8673 and A5-804 must NOT be stated here.

    Both look formulaic but no derivation reproduces them byte-exactly (the
    pitch table mixes rounding modes; the curve fits a quadratic only to +/-3),
    so re-expressing them would change behaviour. They stay on the data path
    until that is settled — see task #71.
    """
    code = re.sub(r"/\*.*?\*/", "", csrc, flags=re.S)   # comments explain them
    assert "-8673" not in code, "the A5-8673 curve is not exactly derivable yet"
    assert "-804" not in code, "the A5-804 pitch table is not exactly derivable yet"


def test_compass_direction_table(image, csrc):
    """g_a5_27980: the 8x3 N/NE/E/SE/S/SW/W/NW compass rose labels.

    Indexed off the base pointer, so the --refs-from scalar filter dropped all
    but its first 4 bytes; port-authored in a5_scalars.c to restore the
    replay-off compass needle (#73). Must match the Mac image exactly.
    """
    assert 'dirs' in csrc, "compass direction table not authored"
    want = bytes([ord('N'),0,0, ord('N'),ord('E'),0, ord('E'),0,0,
                  ord('S'),ord('E'),0, ord('S'),0,0, ord('S'),ord('W'),0,
                  ord('W'),0,0, ord('N'),ord('W'),0])
    assert a5_at(image, -27980, 24) == want


def test_threshold_word_ladder(image, csrc):
    """g_a5_-17516: nine ascending big-endian value words, 100..5000."""
    import struct
    m = re.search(r"thr\[9\] = \{(.*?)\};", csrc, re.S)
    assert m, "threshold ladder not authored"
    vals = [int(x) for x in re.findall(r"\d+", m.group(1))]
    assert vals == [100, 250, 500, 1000, 1500, 2000, 3000, 4000, 5000]
    blob = b"".join(struct.pack(">h", v) for v in vals)
    assert blob == a5_at(image, -17516, 18)


def test_index_permutation(image, csrc):
    """g_a5_-3072: the 15-entry index remap — a bijection over 1..15."""
    m = re.search(r"perm\[15\] = \{(.*?)\};", csrc, re.S)
    assert m, "index permutation not authored"
    vals = [int(x) for x in re.findall(r"\d+", m.group(1))]
    assert sorted(vals) == list(range(1, 16)), "not a permutation of 1..15"
    assert bytes(vals) == a5_at(image, -3072, 15)


def test_diagnostic_strings(image, csrc):
    """The three Mac diagnostic strings, byte-exact incl. the length byte."""
    s = b"Color art requires 256 colors -- using Black & White art."
    assert s.decode() in csrc
    assert a5_at(image, -72, 58) == bytes([57]) + s[:57]
    assert "Visible screen %d" in csrc
    assert a5_at(image, -2610, 18) == b"Visible screen %d\x00"
    assert a5_at(image, -122, 8) == b"Moebius\x00"


def test_class_alignment_rules_matrix(image, csrc):
    """g_a5_-30450: the 17x12 class/alignment legality matrix (#68 target 1).

    Parsed back out of the C literal and compared byte-for-byte against the
    Mac image, so a mis-stated rule fails here instead of as a wrong chargen
    default. Row semantics: count, then legal alignment indices (0=LG..8=CE).
    """
    m = re.search(r"aln\[17\]\[12\] = \{(.*?)\};", csrc, re.S)
    assert m, "class-alignment matrix not authored"
    rows = re.findall(r"\{([^}]*)\}", m.group(1))
    assert len(rows) == 17
    blob = b""
    for r in rows:
        blob += bytes(int(x) for x in r.replace(" ", "").split(",") if x)
    assert blob == a5_at(image, -30450, 204)


def test_race_class_rules_matrix(image, csrc):
    """g_a5_-30864: the 6x14 race/class legality matrix (chargen class default)."""
    m = re.search(r"rc\[6\]\[14\] = \{(.*?)\};", csrc, re.S)
    assert m, "race/class matrix not authored"
    rows = re.findall(r"\{([^}]*)\}", m.group(1))
    assert len(rows) == 6
    blob = b""
    for r in rows:
        blob += bytes(int(x) for x in r.replace(" ", "").split(",") if x)
    assert blob == a5_at(image, -30864, 84)
