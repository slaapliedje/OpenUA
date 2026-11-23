"""The character sheet's class line under Krynn's rules (boot.c
sheet_krynn_words, from DQK.EXE 0x5cd06..0x5ce8a).

The words come from the ruleset (gbrules.sheet_words: the sheet's own kind
names, gods, robes, orders). The rule is held to what DOS showed for the
shipped heroes (DOSBox, 2026-11-23; the port in Hatari showed the same), and
its tests to DQK.EXE's bytes when the machine has it.
"""
import glob
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
here = pytest.mark.skipif(not os.path.exists(EXE), reason="DQK.EXE not here")

DOS = {
    "ROLAND OF DUST": "KNIGHT OF THE CROWN",
    "TIMASALIN": "KIRI-JOLITH CLERIC/FIGHTER/WHITE MAGE",
    "'CINDER'": "SIRRION CLERIC/FIGHTER/RED MAGE",
    "BALTHIR NOMOON": "MISHAKAL CLERIC/RANGER",
    "ROARK 'CROAKER'": "FIGHTER/THIEF",
    "WALKER KEYS": "MISHAKAL CLERIC/THIEF",
}


def line(q, w):
    """DQK 0x5cd06 on a .QCH record `q` with the sheet words `w`."""
    buf = w["sheet_kind_names"][q[0x5a]]
    if q[0xa5]:                                  # cleric levels
        buf = w["god_names"][q[0x19b]] + " " + buf
    if q[0xaa]:                                  # mage levels
        z, g = buf + "\0", 1                   # the C string's NUL
        while not (z[g - 1] == "M" and g + 1 < len(buf) and z[g + 1] == "g") \
                and g <= len(buf):
            g += 1
        buf = buf[:g - 1] + w["robe_names"][q[0x19d]] + " " + buf[g - 1:]
    if q[0xa6]:                                  # knight levels
        buf += w["order_names"][q[0x19a]]
    return buf


@here
def test_shipped_heroes_read_as_dos_showed():
    w = gbrules.sheet_words(open(EXE, "rb").read())
    seen = {}
    for f in glob.glob(os.path.join(GAME, "SAVE", "*.QCH")):
        q = open(f, "rb").read()
        seen[q[0x68:0x78].split(b"\0")[0].decode()] = line(q, w).upper()
    assert seen == DOS


@here
def test_its_tests_are_dqk_exes():
    d = open(EXE, "rb").read()
    assert d[0x5cd1f:0x5cd25] == bytes.fromhex("2680bfa50000")   # cleric
    assert d[0x5cd62:0x5cd68] == bytes.fromhex("2680bfaa0000")   # mage
    assert d[0x5cdf1:0x5cdf7] == bytes.fromhex("2680bfa60000")   # knight
    assert d[0x5cd81:0x5cd84] == b"\x36\x80\x3f" and d[0x5cd84] == ord("M")
    assert d[0x5cd92:0x5cd95] == b"\x36\x80\x3f" and d[0x5cd95] == ord("g")
    # the kind name is the sheet's table, indexed by the class word
    assert d[0x5ccfe:0x5cd0a] == bytes.fromhex("268b5f5ad1e3d1e3ffb7f83a")


def test_the_port_composes_it_so():
    src = open(os.path.join(ROOT, "src", "engine", "boot.c")).read()
    body = re.search(r"static void sheet_krynn_words\(.*?\n\{(.*?)\n\}",
                     src, re.S).group(1)
    tests = re.findall(r"if \(rec\[(\d+)\] > 0\)", body)
    assert tests == ["157", "162", "158"]          # DQK 0xa5, 0xaa, 0xa6
    assert "buf[g - 1] == 'M'" in body and "buf[g + 1] == 'g'" in body
    assert "god_name[KRYNN_GOD(rec)]" in body
    assert "robe_name[KRYNN_ROBE(rec)]" in body
    assert "order_name[KRYNN_ORDER(rec)]" in body
    # FRUA's rules have no sheet words: the line stays FRUA's
    assert re.search(r"if \(g_rules->god_name != NULL\)\s+sheet_krynn_words", src)


def test_robe_goes_before_mage_or_at_the_end():
    w = {"sheet_kind_names": ["Mage", "Thief"], "god_names": ["None"],
         "robe_names": ["", "White"], "order_names": [""]}
    q = bytearray(0x1a0)
    q[0xaa], q[0x19d] = 1, 1
    assert line(q, w) == "White Mage"
    q[0x5a] = 1
    assert line(q, w) == "ThiefWhite "             # no "M?g": appended
    struct.pack_into("<H", q, 0x5a, 0)
    q[0x19d] = 0
    assert line(q, w) == " Mage"                   # a mage without a robe
