"""What a Krynn god grants (rules.h KRYNN_GOD_SPELLS / KRYNN_TOHIT, from
DQK.EXE 0x3ddff and 0x625d4).

The god spells are bits of the known-spell byte DQK 0x15e (rec[346] here),
spell 57 + bit, the class-3 "Special" spells. The table in rules.h is held
to the shipped heroes, whose .QCH files the game wrote: each carries exactly
its god's bits among the class-3 ones (their other bits there are ordinary
spells, Cure Serious Wounds at bit 1). And to DQK.EXE itself, when it is
here: each god's arm of 0x3ddff ORs its bits from the 1 << n table at
DS:0x68bc.
"""
import glob
import os
import re
import struct

import pytest

ROOT = os.path.join(os.path.dirname(__file__), "..")
GAME = os.path.expanduser(
    "~/.local/share/Steam/steamapps/common/Dungeons & Dragons Krynn Series/"
    "games/The Dark Queen of Krynn/GAME/DQK")
DQK_DS = 0x63a20
GODS = ("none", "Paladine", "Majere", "Kiri-Jolith", "Mishakal", "Sirrion",
        "Reorx", "Shinare")


def god_spells():
    src = open(os.path.join(ROOT, "src", "engine", "rules.h")).read()
    m = re.search(r'#define KRYNN_GOD_SPELLS\(god\).*?"((?:\\[0-7]{3})+)"',
                  src, re.S)
    assert m, "KRYNN_GOD_SPELLS not found"
    return [int(o, 8) for o in re.findall(r"\\([0-7]{3})", m.group(1))]


def test_table_shape():
    t = god_spells()
    assert len(t) == 8 and t[0] == 0 and t[6] == 0     # none, Reorx
    assert t[3] == 0x08 and t[5] == 0x80               # Kiri-Jolith, Sirrion
    # every god bit is a class-3 spell, never bit 1 (Cure Serious Wounds)
    assert all(not v & 0x02 for v in t)


@pytest.mark.skipif(not glob.glob(os.path.join(GAME, "SAVE", "*.QCH")),
                    reason="no Dark Queen heroes on this machine")
def test_shipped_heroes_carry_their_gods_bits():
    t = god_spells()
    mask = 0
    for v in t:
        mask |= v
    seen = 0
    for f in glob.glob(os.path.join(GAME, "SAVE", "*.QCH")):
        q = open(f, "rb").read()
        god = q[0x19b]
        if god == 0:
            continue
        assert q[0x15e] & mask == t[god], (f, GODS[god], hex(q[0x15e]))
        seen += 1
    assert seen >= 3


@pytest.mark.skipif(not os.path.exists(os.path.join(GAME, "DQK.EXE")),
                    reason="DQK.EXE not here")
def test_table_is_dqk_exes():
    d = open(os.path.join(GAME, "DQK.EXE"), "rb").read()
    fn = 0x3ddff
    assert d[fn:fn + 4] == bytes.fromhex("c41e57c7")   # les bx,[char]
    base = 0x3de1a - 0x395a                            # the overlay's CS
    targets = struct.unpack_from("<7H", d, 0x3de1a)
    bits = [1 << n for n in range(8)]
    got = [0]
    for t in targets:
        i, v = base + t, 0
        for _ in range(64):                            # walk the arm
            if d[i:i + 2] == b"\x0a\x06":              # or al,[DS:68bc+n]
                v |= d[DQK_DS + struct.unpack_from("<H", d, i + 2)[0]]
                i += 4
            elif d[i] in (0xe9,):                      # jmp near
                i = i + 3 + struct.unpack_from("<h", d, i + 1)[0]
            elif d[i] == 0xeb:                         # jmp short
                i = i + 2 + struct.unpack_from("<b", d, i + 1)[0]
            elif d[i] == 0xcb:                         # retf
                break
            elif d[i] == 0x74 or d[i] == 0x75:         # Reorx's dwarf test
                break
            else:
                i += 1
        got.append(v)
    assert all(b in bits or b == 0 for g in got for b in [g & -g])
    assert got == god_spells()
