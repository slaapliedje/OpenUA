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


def _study_levels():
    """boot.c spell_study_level: {id: level} for the god spells, and the
    default."""
    src = open(os.path.join(ROOT, "src", "engine", "boot.c")).read()
    body = re.search(r"spell_study_level\(unsigned char id\)\n\{(.*?)\n\}",
                     src, re.S).group(1)
    cases = {int(a): int(b) for a, b in
             re.findall(r"case (\d+): return (\d+);", body)}
    default = int(re.search(r"default: return (\d+);", body).group(1))
    return cases, default


@pytest.mark.skipif(not os.path.exists(os.path.join(GAME, "DQK.EXE")),
                    reason="DQK.EXE not here")
def test_special_study_levels_are_dqk_exes():
    """DQK.EXE 0x6cc9: the level a level-0 (god) spell is studied at."""
    d = open(os.path.join(GAME, "DQK.EXE"), "rb").read()
    f = d[0x6cc9:0x6cf4]
    assert f[:3] == b"\x55\x8b\xec"
    ids = [struct.unpack_from("<H", f, m.start() + 1)[0]
           for m in re.finditer(rb"\x3d..\x74", f, re.S)]
    vals = [struct.unpack_from("<H", f, m.start() + 1)[0]
            for m in re.finditer(rb"\xb8..\xeb|\xb8..\x5d", f, re.S)]
    # arms in source order: 0x39 -> first, 0x3d -> second, 0x3b -> third
    jumps = [m.start() + 5 + struct.unpack_from("<b", f, m.start() + 4)[0]
             for m in re.finditer(rb"\x3d..\x74", f, re.S)]
    arms = sorted(m.start() for m in re.finditer(rb"\xb8..", f, re.S))
    got = {i: vals[arms.index(j)] for i, j in zip(ids, jumps)}
    cases, default = _study_levels()
    assert got == cases and vals[-1] == default == 1
    assert set(cases) <= {57, 59, 60, 61, 62, 63, 64}


@pytest.mark.skipif(not os.path.exists(os.path.join(GAME, "DQK.EXE")),
                    reason="DQK.EXE not here")
def test_special_is_cast_at_the_clerical_level():
    """DQK.EXE's caster level (FRUA's jt17) sends spell class 3 down its
    class-0 arm (the switch at 0xb56d), and the port does the same under
    Krynn's rules."""
    d = open(os.path.join(GAME, "DQK.EXE"), "rb").read()
    assert d[0xb561:0xb572] == bytes.fromhex("3d04007603e908018bd8d1e32effa79219")
    t = struct.unpack_from("<5H", d, 0xb572)
    assert t[3] == t[0] and len(set(t)) == 4
    src = open(os.path.join(ROOT, "src", "engine", "boot.c")).read()
    body = re.search(r"static short jt17\(short a, short b\)\n\{(.*?)\n\}",
                     src, re.S).group(1)
    assert re.search(r"case 3:.*?if \(!g_rules->knights\)\s+break;\s+"
                     r"/\* fall through \*/\s+case 0:", body, re.S)


@pytest.mark.skipif(not os.path.exists(os.path.join(GAME, "DQK.EXE")),
                    reason="DQK.EXE not here")
def test_robe_is_dqk_exes():
    """DQK.EXE 0x3c3e5 (overlay 5, code base 0x3a4c0): a mage's robe by
    alignment, through a jump table at CS:0x1f40; boot.c krynn_robe, which
    creation and (under Krynn's rules) Change Class to mage use."""
    d = open(os.path.join(GAME, "DQK.EXE"), "rb").read()
    base, fn = 0x3a4c0, 0x3c3e5
    assert d[fn + 6:fn + 0x1b] == bytes.fromhex(
        "c45e06268b4762" "3d0700" "773d" "8bd8d1e3" "2effa7401f")
    arms = struct.unpack_from("<8H", d, base + 0x1f40)

    def robe(al):
        if al > 7:
            return 0
        i = base + arms[al]
        if i == fn + 0x4f:                           # straight to the end
            return 0
        assert d[i:i + 3] == bytes.fromhex("c45e06")
        assert d[i + 3:i + 9] == bytes.fromhex("2680bfaa0000")   # mage level
        assert d[i + 11:i + 16] == bytes.fromhex("26c6879d01")   # rec 0x19d
        return d[i + 16]
    dqk = [robe(al) for al in range(9)]
    src = open(os.path.join(ROOT, "src", "engine", "boot.c")).read()
    body = re.search(r"static unsigned char krynn_robe\(const unsigned char "
                     r"\*rec\)\n\{(.*?)\n\}", src, re.S).group(1)
    assert "rec[93] % 3" in body and "al == 2 ? 0 : al == 0 ? ROBE_WHITE : ROBE_RED" in body
    assert dqk == [[1, 2, 0][al % 3] for al in range(9)]
    assert re.search(r"g_rules->knights && cls == 5 && KRYNN_ROBE\(rec\) == 0\)"
                     r"\s+rec\[REC_KRYNN\] = KRYNN_PACK\(KRYNN_GOD\(rec\), "
                     r"krynn_robe\(rec\)", src)
