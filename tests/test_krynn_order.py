"""Knight new Order: the Dark Queen's hall item 4 (boot.c krynn_knight_order,
from DQK.EXE 0x487f2 and 0x48710).

The bars are held to DQK.EXE when the machine has it: the XP cells the game
compares against (DS:0x98e, DS:0x9de) must be its own XP table's Sword level 3
and Rose level 4, which is how the port reads them (jt26 at the next order),
and the base ability rows (DS:0x7d98, DS:0x7da4) and the texts must be the
port's. The promotion rule is checked against what DOS did to the shipped
Roland of Dust (Crown 11, 1,000,001 XP, 134 HP): Sword 9 at 130 HP, then
Rose 9 at 130 HP, THAC0 8 (seen in DOSBox and in Hatari, 2026-11-23).
"""
import os
import re
import struct
import sys

import pytest

ROOT = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gbrules                                                 # noqa: E402

GAME = os.path.expanduser(
    "~/.local/share/Steam/steamapps/common/Dungeons & Dragons Krynn Series/"
    "games/The Dark Queen of Krynn/GAME/DQK")
EXE = os.path.join(GAME, "DQK.EXE")
DS = gbrules.DS
here = pytest.mark.skipif(not os.path.exists(EXE), reason="DQK.EXE not here")


def boot_src():
    return open(os.path.join(ROOT, "src", "engine", "boot.c")).read()


def port_tables():
    src = boot_src()
    lv = re.search(r"k_knight_bar_level\[2\] = \{ (\d+), (\d+) \}", src)
    ab = re.search(r"k_knight_bar_ability\[2\]\[6\] = \{(.*?)\n\};", src, re.S)
    rows = [[int(v) for v in r.split(",")]
            for r in re.findall(r"\{\s*([\d,\s]+?)\s*\}", ab.group(1))]
    return [int(lv.group(1)), int(lv.group(2))], rows


def body():
    return re.search(r"static void krynn_knight_order\(unsigned char \*rec\)"
                     r"\n\{(.*?)\n\}", boot_src(), re.S).group(1)


def xp_row(exe, prog):
    xp = gbrules.raw(exe, "xp")
    n = gbrules.XP_LEVELS
    return struct.unpack_from("<%dl" % n, xp, 4 * n * prog)


@here
def test_bars_are_dqk_exes():
    d = open(EXE, "rb").read()
    levels, rows = port_tables()
    assert rows == [list(struct.unpack_from("<6H", d, DS + 0x7d98)),
                    list(struct.unpack_from("<6H", d, DS + 0x7da4))]
    # 0x487f2: cmpb $3 / $4, es:0xa6 (the knight level) ...
    assert d[0x4895b:0x48961] == bytes.fromhex("2680bfa600") + bytes([levels[0]])
    assert d[0x48a8a:0x48a90] == bytes.fromhex("2680bfa600") + bytes([levels[1]])
    # ... and the XP longs at DS:0x98e / 0x9de (cmp hi word at +2)
    assert d[0x4896e:0x48972] == bytes.fromhex("3b169009")
    assert d[0x48a9a:0x48a9e] == bytes.fromhex("3b16e009")
    xp0 = DS + dict((n, o) for n, o, _, _ in gbrules.TABLES)["xp"]
    n = gbrules.XP_LEVELS
    sword = gbrules.PROGRESSIONS.index("knight of the sword")
    rose = gbrules.PROGRESSIONS.index("knight of the rose")
    assert DS + 0x98e == xp0 + 4 * (n * sword + levels[0])
    assert DS + 0x9de == xp0 + 4 * (n * rose + levels[1])
    assert xp_row(d, sword)[levels[0]] == 12000
    assert xp_row(d, rose)[levels[1]] == 27000


@here
def test_texts_are_dqk_exes():
    d = open(EXE, "rb").read()
    want = []
    for p in list(range(0x3d4a, 0x3d66, 4)) + [0x3ece]:
        off, seg = struct.unpack_from("<HH", d, DS + p)
        assert seg == gbrules.DS_SEG
        end = d.index(b"\0", DS + off)
        want.append(d[DS + off:end].decode())
    b = body()
    for s in want[:-1]:
        assert '"%s"' % s in b, s
    assert '"Change Exit"' in b and want[-1] == "Change Exit"
    end = d.index(b"\0", DS + 0x7e11)
    assert d[DS + 0x7e11:end] == b"Knight new Order"
    assert '"Knight new Order"' in boot_src()


def test_promotion_loop_is_dqks():
    b = body()
    assert re.search(r"while \(jt26\(\(long\)\(uintptr_t\)rec, rec\[158\], 1\)"
                     r" > \*\(long \*\)\(rec \+ 68\)\)", b)
    assert re.search(r"if \(old > 9\) \{\s+loss = 2;\s+con = 0;", b)
    assert "rec[184] / (old + 1)" in b and "jt881((long)(uintptr_t)rec, 1)" in b
    assert re.search(r"jt910\(.*?\);\s+rec\[197\] = \(unsigned char\)jt33\(rec\);",
                     b, re.S)


def promote(level, hp, dice, xp, row):
    """0x48710 on (knight level, max HP, knight dice HP), CON bonus 0."""
    while row[level] > xp:
        old, level = level, level - 1
        loss = 2 if old > 9 else dice // (old + 1)
        dice -= loss
        hp -= loss
    return level, hp


@here
def test_roland_as_dos_promoted_him():
    d = open(EXE, "rb").read()
    q = open(os.path.join(GAME, "SAVE", "ROLANDOF.QCH"), "rb").read()
    xp = struct.unpack_from("<l", q, 0x44)[0]
    level, hp, order = q[0xa6], q[0x89], q[0x19a]
    assert (level, hp, order, xp) == (11, 134, 1, 1000001)
    sword = xp_row(d, gbrules.PROGRESSIONS.index("knight of the sword"))
    rose = xp_row(d, gbrules.PROGRESSIONS.index("knight of the rose"))
    level, hp = promote(level, hp, q[0xc0], xp, sword)
    assert (level, hp) == (9, 130)
    level, hp = promote(level, hp, q[0xc0], xp, rose)
    assert (level, hp) == (9, 130)
