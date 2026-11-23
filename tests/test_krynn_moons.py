"""Krynn's moons (tools/gbsave.py moons_day; boot.c krynn_moons_day).

The rule is DQK.EXE's: 0x5e58 counts each moon on with its period, 0x60c8
calls it when the hours carry into a day, and a new game starts every phase
at 2 (0x37f0b). The test reads those out of the game's own DQK.EXE when it is
here, holds boot.c's table to the reference, and plays the reference over
real saves: H was saved in the game four days after D.
"""
import os
import re
import sys

import pytest

ROOT = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gbsave                                                  # noqa: E402

DQK_EXE = os.path.expanduser(
    "~/.local/share/Steam/steamapps/common/Dungeons & Dragons Krynn Series/"
    "games/The Dark Queen of Krynn/GAME/DQK/DQK.EXE")
SAVES = os.path.join(ROOT, "data", "work", "dqk-saves")


def boot_c():
    return open(os.path.join(ROOT, "src", "engine", "boot.c")).read()


def test_boot_c_is_the_reference():
    src = boot_c()
    m = re.search(r"krynn_moons_day\(void\)\n\{.*?period\[3\] = \{ (\d+), "
                  r"(\d+), (\d+) \}", src, re.S)
    assert m and tuple(int(x) for x in m.groups()) == gbsave.MOON_PERIOD
    assert int(re.search(r"#define GBS_MOON_PHASE\s+(0x[0-9a-f]+)",
                         src).group(1), 16) == gbsave.MOON_PHASE
    assert int(re.search(r"#define GBS_MOON_COUNT\s+(0x[0-9a-f]+)",
                         src).group(1), 16) == gbsave.MOON_COUNT


def test_rule():
    cells = bytearray(1024)
    for m in range(3):
        cells[gbsave.MOON_PHASE - 1 + m] = 2
    seen = []
    for _ in range(72):
        gbsave.moons_day(cells)
        seen.append(tuple(cells[gbsave.MOON_PHASE - 1:gbsave.MOON_PHASE + 2]))
    # each phase lasts period + 1 days (36-, 8- and 28-day cycles), in the
    # order high, waning, low, waxing
    for m, period in enumerate(gbsave.MOON_PERIOD):
        col = [s[m] for s in seen]
        runs = [[col[0], 1]]
        for v in col[1:]:
            if v == runs[-1][0]:
                runs[-1][1] += 1
            else:
                runs.append([v, 1])
        assert all(n == period + 1 for _, n in runs[1:-1])
        assert runs[0][1] == period          # day 0, not seen, began it
        assert all((b[0] - a[0]) % 4 == 1 for a, b in zip(runs, runs[1:]))
    assert [gbsave.moon_bonus(p) for p in range(4)] == [0, 1, 2, 0]


@pytest.mark.skipif(not os.path.exists(os.path.join(SAVES, "H")),
                    reason="no saves made in the game here")
def test_four_days_after_d_is_h():
    d = gbsave.GbSave(open(os.path.join(SAVES, "D", "SAVGAMD.QSV"),
                           "rb").read())
    h = gbsave.GbSave(open(os.path.join(SAVES, "H", "SAVGAMH.QSV"),
                           "rb").read())
    assert (h.clock["days"] - d.clock["days"]) == 4
    cells = bytearray(d.cells)
    for _ in range(4):
        gbsave.moons_day(cells)
    lo, hi = gbsave.MOON_PHASE - 1, gbsave.MOON_COUNT + 2
    assert cells[lo:hi] == h.cells[lo:hi]
    # one day more: Lunitari waxing, the game's "BONUS : 1" for Cinder
    gbsave.moons_day(cells)
    assert gbsave.moon_bonus(cells[gbsave.MOON_PHASE]) == 1


@pytest.mark.skipif(not os.path.exists(DQK_EXE), reason="DQK.EXE not here")
def test_rule_is_dqk_exes():
    d = open(DQK_EXE, "rb").read()
    # 0x5e58: each moon's period and count cell, pushed for 0x5d6e
    calls = re.findall(rb"\xb0(.)\x50\xb8(..)\x50\x0e\xe8",
                       d[0x5e58:0x5eaa], re.S)
    assert [(p[0], int.from_bytes(a, "little")) for p, a in calls] == [
        (gbsave.MOON_PERIOD[m], gbsave.MOON_COUNT - 1 + m) for m in range(3)]
    # 0x5d6e: count at the cell + 1, phase at the cell - 2, phase wraps at 3
    f = d[0x5d6e:0x5e58]
    assert b"\x8b\x46\x06\x40" in f and b"\x05\xfe\xff" in f
    assert b"\x26\x80\x3f\x03" in f
    # 0x60c8: the moons move when field 3, the hours, carries
    assert d[0x6128:0x6132] == bytes.fromhex("837efa0375 3e0ee826fd"
                                             .replace(" ", ""))
    # 0x37f0b: a new game starts every phase at high sanction
    assert d[0x37f0b:0x37f1d] == bytes.fromhex(
        "26c687fa010226c687fb010226c687fc0102")
