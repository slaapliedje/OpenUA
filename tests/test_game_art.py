"""A Dark Queen design takes its pictures from the game's own DISK folders
(boot.c gbs_game_art, called by l17e2 on a miss in the design folder).

Checked in Hatari on a design that is a plain copy of the game's install
folder (2026-11-23): Palanthas's picture came from DISK2\\PICB.TLB, and the
PICB.ctl the engine wrote into the design folder is byte-identical to
tools/art_convert.py's conversion of it.
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gbgeo                                                   # noqa: E402

SRC = open(os.path.join(ROOT, "src", "engine", "boot.c")).read()
BODY = re.search(r"static int gbs_game_art\(const char \*name, "
                 r"const char \*design_path\)\n\{(.*?)\n\}", SRC, re.S).group(1)


def test_the_libraries_are_gbgeos_picture_libraries():
    libs = re.search(r"lib\[\] = \{(.*?)\};", BODY, re.S).group(1)
    assert tuple(re.findall(r'"(\w+)"', libs)) == gbgeo.PICTURE_LIBS


def test_never_the_screens_own_pieces():
    # FRUA's screen is built from its own FRAME, wall and combat libraries
    for lib in ("FRAME", "8X8D", "DUNGCOM", "WILDCOM", "COMSPR", "BACK",
                "TITLE", "ALWAYS", "GEN", "CBODY", "TOPVIEW"):
        assert '"%s' % lib not in BODY


def test_only_a_design_holding_the_game():
    assert '"DQK.EXE"' in BODY
    assert r'sub[] = { "DISK1\\", "DISK2\\", "DISK3\\" }' in BODY


def test_the_conversion_lands_in_the_design_folder():
    # written under the very spelling l17e2 probes, not beside the .TLB
    assert "ua_dos_art_convert_file(in, design_path)" in BODY
    l17e2 = re.search(r"static short l17e2\(short kind, const char \*name, "
                      r"short mode, void \*cbp\)\n\{(.*?)\n\}", SRC, re.S).group(1)
    assert re.search(r"refnum = jt398\(dspec, [^;]*;\s*"
                     r"if \(refnum < 0 && gbs_game_art\(name, dspec\)\)\s*"
                     r"refnum = jt398\(dspec,", l17e2)


def test_colour_only():
    # the mono build asks for .tlb names, which stay a miss here
    assert 'if (!ua_ext_is(name, "ctl"))' in BODY
