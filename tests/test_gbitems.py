"""src/engine/gbitems.c against tools/gbgeo.py: a Gold Box game's item data.

The Python functions are the reference (ADR-0026): item_table() for
ITEM.DAT, find_words() and find_shops() for the tables in DQK.EXE, which
they find by shape. The C loader reads the same tables at the Steam
build's positions, with sums, and must give the same results:

  * on a SYNTHETIC executable built with the tables at those positions,
    shaped so that the reference finds them there too;
  * on the REAL DQK.EXE and ITEM.DAT when the machine has them.

The game data is never committed; the real-file checks skip without it.
"""
import glob
import os
import struct
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import gbgeo                                                   # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "src", "engine", "gbitems.c"),
       os.path.join(ROOT, "tests", "gbitems_host.c")]

DS, SEG = 0x63a20, 0x322e
WORDS, SHOPS = 0x3f55, 0x75c6          # the Steam DQK.EXE's positions


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("gbitems") / "gbitems_host")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-o", exe] + SRC, check=True)
    return exe


def run(cbin, *args):
    out = subprocess.run([cbin] + [str(a) for a in args], check=True,
                         capture_output=True, text=True).stdout
    return out.splitlines()


def c_tables(cbin, path, verify=False):
    lines = run(cbin, "exe", path, *(["verify"] if verify else []))
    if lines[0] != "OK":
        return lines[0]
    words = [l[5:] for l in lines[1:] if l.startswith("word ")]
    shops = [list(bytes.fromhex(l.split(" ", 2)[2] if l.count(" ") == 2 else ""))
             for l in lines[1:] if l.startswith("shop ")]
    return words, shops


OWN = ["Hoopak", "Dragonlance", "Solamnic Plate", "Striking"]
STOCK = [[1, 2, 3], [10, 11], [200], [7, 8, 9, 12], [254], [33, 34],
         [50], [60, 61, 62], [100, 101]]


def synthetic(own=OWN, stock=STOCK, end=True):
    exe = bytearray(DS + 0x7800)
    sp = 0x5000

    def string(s):
        nonlocal sp
        b = s.encode("latin-1") + b"\0"
        exe[DS + sp:DS + sp + len(b)] = b
        sp += len(b)
        return sp - len(b)

    first = [string(s) for s in ("Battle Axe", "Hand Axe", "Club", "Dagger",
                                 "Dart", "Hammer", "Javelin")]
    filler = string("Word")
    table = [filler] + first + [filler] * (126 - 8) + [string(s) for s in own]
    if end:
        table.append(string("who is looking very old and frail"))
    table += [filler] * (256 - len(table))
    for k, off in enumerate(table):
        struct.pack_into("<HH", exe, DS + WORDS + 4 * k, off, SEG)

    at, k = DS + SHOPS, 1
    items = []
    for s, lst in enumerate(stock):
        struct.pack_into("<H", exe, at + 2 * s, k)
        struct.pack_into("<H", exe, at + 18 + 2 * s, k + len(lst) - 1)
        k += len(lst)
        items += lst
    struct.pack_into("<%dH" % len(items), exe, at + 36, *items)
    return bytes(exe)


def test_synthetic_matches_reference(cbin, tmp_path):
    exe = synthetic()
    p = tmp_path / "DQK.EXE"
    p.write_bytes(exe)
    assert gbgeo.find_words(exe) == OWN
    assert gbgeo.find_shops(exe) == STOCK
    assert c_tables(cbin, p) == (OWN, STOCK)


def test_refuses_another_build(cbin, tmp_path):
    p = tmp_path / "DQK.EXE"
    p.write_bytes(synthetic())
    assert c_tables(cbin, p, verify=True).startswith("ERR -2 item words")


def test_bad_shop_shape(cbin, tmp_path):
    exe = bytearray(synthetic())
    struct.pack_into("<H", exe, DS + SHOPS + 2, 9)     # shop 2 does not follow
    p = tmp_path / "DQK.EXE"
    p.write_bytes(exe)
    assert c_tables(cbin, p) == "ERR -3 shops"


def test_item_table(cbin, tmp_path):
    table = bytes((k * 7 + 3) & 0xff for k in range(17 * 254))
    p = tmp_path / "ITEM.DAT"
    for data in (table, table[:17 * 100]):
        p.write_bytes(data)
        assert bytes.fromhex(run(cbin, "items", p)[0]) == gbgeo.item_table(data)
    p.write_bytes(table[:20])
    assert run(cbin, "items", p)[0] == "ERR -1"


def _real(name):
    steam = os.path.expanduser("~/.local/share/Steam/steamapps/common")
    for pat in (os.path.join(ROOT, "data", "**", "DQK", "**", name),
                os.path.join(steam, "Dungeons & Dragons Krynn Series", "**",
                             "The Dark Queen of Krynn", "**", name)):
        for p in sorted(glob.glob(pat, recursive=True)):
            return p
    pytest.skip("no Dark Queen %s on this machine" % name)


def test_real_exe_matches_reference(cbin):
    path = _real("DQK.EXE")
    with open(path, "rb") as fh:
        exe = fh.read()
    words, shops = c_tables(cbin, path, verify=True)
    assert words == gbgeo.find_words(exe)
    assert shops == gbgeo.find_shops(exe)
    assert words[:2] == ["Hoopak", "Dragonlance"] and len(words) == 29
    assert [len(s) for s in shops] == [len(s) for s in gbgeo.find_shops(exe)]


def test_real_item_table(cbin):
    path = _real("ITEM.DAT")
    with open(path, "rb") as fh:
        data = fh.read()
    assert bytes.fromhex(run(cbin, "items", path)[0]) == gbgeo.item_table(data)
