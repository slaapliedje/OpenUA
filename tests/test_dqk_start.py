"""A new Dark Queen game starts with the script DQK.EXE starts it with
(boot.c GBS_DQK_NEW_GAME_SCRIPT): 0x32e21 tests the running-script cell
0x16 and, with none running, loads ECL 4 (Palanthas), or ECL 7 in its demo.
Script 1 is SSI's debug jumper ("DO YOU WANT TO USE THE JUMPER?"), which
the game never runs; DOS starts at Palanthas, and so does the port
(Hatari, 2026-11-23).
"""
import os
import re

import pytest

ROOT = os.path.join(os.path.dirname(__file__), "..")
EXE = os.path.expanduser(
    "~/.local/share/Steam/steamapps/common/Dungeons & Dragons Krynn Series/"
    "games/The Dark Queen of Krynn/GAME/DQK/DQK.EXE")


@pytest.mark.skipif(not os.path.exists(EXE), reason="DQK.EXE not here")
def test_new_game_script_is_dqk_exes():
    d = open(EXE, "rb").read()
    assert d[0x32e21:0x32e2c] == bytes.fromhex("c41e0fb2" "26807f1600" "7524")
    assert d[0x32e33:0x32e38] == bytes.fromhex("c60663ae07")     # demo: 7
    assert d[0x32e3a:0x32e3f] == bytes.fromhex("c60663ae04")     # else: 4
    src = open(os.path.join(ROOT, "src", "engine", "boot.c")).read()
    want = d[0x32e3e]
    assert re.search(r"#define GBS_DQK_NEW_GAME_SCRIPT %du\b" % want, src)
    assert "? GBS_DQK_NEW_GAME_SCRIPT : 0u" in src
