"""src/engine/qch.c against tools/gbchar.py: a Dark Queen .QCH as the .CCH
stream FRUA's loader reads (ADR-0026 step (d)).

The Python converter is the reference; the C reader is built on the host and
must produce the same bytes, with and without the game's own rules, on
synthetic files (every byte its own value, a container among the items, each
race) and on the six heroes the Steam release ships when the machine has
them. The game data is never committed; those checks skip without it.
"""
import glob
import os
import random
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import gbchar                                                  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "src", "engine", "qch.c"),
       os.path.join(ROOT, "tests", "qch_host.c")]
HEROES = os.path.expanduser(
    "~/.local/share/Steam/steamapps/common/Dungeons & Dragons Krynn Series/"
    "games/The Dark Queen of Krynn/Default files/Saves")


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("qch") / "qch_host")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-o", exe] + SRC, check=True)
    return exe


def run(cbin, tmp_path, q, native):
    p = tmp_path / "T.QCH"
    p.write_bytes(bytes(q))
    out = subprocess.run([cbin, str(p)] + (["native"] if native else []),
                         check=True, capture_output=True, text=True).stdout.strip()
    return out


def synthetic(seed, race, items, nodes, container_at=None):
    rnd = random.Random(seed)
    q = bytearray(rnd.randrange(256) for _ in range(gbchar.QCH_FIXED))
    q[8] = items
    q[0x58] = race
    q[0x68:0x78] = b"SYNTH HERO %d\0\0\0\0" % seed
    q[0x68:0x78] = q[0x68:0x78][:16]
    for k in range(items):
        it = bytearray(rnd.randrange(256) for _ in range(gbchar.ITEM))
        it[0] = 73 if k == container_at else (it[0] if it[0] != 73 else 5)
        q += it
    for _ in range(nodes):
        q += bytes(rnd.randrange(256) for _ in range(gbchar.NODE))
    return q


CASES = [(1, 0, 0, 0, None), (2, 5, 3, 2, None), (3, 6, 4, 1, 2),
         (4, 7, 1, 0, 0), (5, 3, 0, 5, None), (6, 200, 2, 3, None)]


@pytest.mark.parametrize("case", CASES)
@pytest.mark.parametrize("native", [False, True])
def test_synthetic_matches_gbchar(cbin, tmp_path, case, native):
    q = synthetic(*case)
    want, _ = gbchar.convert(bytes(q), native=native)
    assert run(cbin, tmp_path, q, native) == want.hex()


def test_refuses_what_gbchar_refuses(cbin, tmp_path):
    assert run(cbin, tmp_path, bytes(100), False) == "ERR -1"
    q = synthetic(7, 6, 2, 1) + b"\0"          # a stray byte past the nodes
    with pytest.raises(gbchar.GbCharError):
        gbchar.parse_qch(bytes(q))
    assert run(cbin, tmp_path, q, False) == "ERR -2"


@pytest.mark.skipif(not glob.glob(os.path.join(HEROES, "*.QCH")),
                    reason="the Dark Queen's heroes are not on this machine")
@pytest.mark.parametrize("native", [False, True])
def test_real_heroes_match_gbchar(cbin, tmp_path, native):
    files = sorted(glob.glob(os.path.join(HEROES, "*.QCH")))
    assert len(files) == 6
    for path in files:
        with open(path, "rb") as fh:
            q = fh.read()
        want, _ = gbchar.convert(q, native=native)
        assert run(cbin, tmp_path, q, native) == want.hex(), path


# What DQK.EXE 0xbb19 reads, as the six heroes hold it: Roland a Knight of
# the Crown (order 1); Cinder a cleric of Sirrion (5, neutral) in the red
# robe (2); Timasalin of Kiri-Jolith (3) in the white (1); Balthir and
# Walker Keys of Mishakal (4); Roark neither cleric, mage nor knight.
KRYNN = {"ROLANDOF": 1 << 5, "'CINDER'": 5 | 2 << 3, "TIMASALI": 3 | 1 << 3,
         "BALTHIRN": 4, "WALKERKE": 4, "ROARK'CR": 0}


@pytest.mark.skipif(not glob.glob(os.path.join(HEROES, "*.QCH")),
                    reason="the Dark Queen's heroes are not on this machine")
def test_real_heroes_god_robe_order(cbin, tmp_path):
    for stem, want in KRYNN.items():
        with open(os.path.join(HEROES, stem + ".QCH"), "rb") as fh:
            q = fh.read()
        native = bytes.fromhex(run(cbin, tmp_path, q, True))
        frua = bytes.fromhex(run(cbin, tmp_path, q, False))
        assert native[91] == want, stem
        assert frua[91] == 0, stem
