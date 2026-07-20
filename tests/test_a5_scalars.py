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
