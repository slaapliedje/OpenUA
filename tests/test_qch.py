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


# ---- writing a .QCH back (ADR-0026 decision 4) ----------------------------

def run_w(cbin, tmp_path, c, base=None):
    pc = tmp_path / "T.CCH"
    pc.write_bytes(bytes(c))
    args = [cbin, "-w", str(pc)]
    if base is not None:
        pb = tmp_path / "B.QCH"
        pb.write_bytes(bytes(base))
        args.append(str(pb))
    return subprocess.run(args, check=True, capture_output=True,
                          text=True).stdout.strip()


def shaped(q):
    """`q` kept to what every real hero holds and a write derives: 0xc9
    mirrors the item count, the affect head (4..7) and each node's next
    pointer are non-zero but on the last node, which is 0."""
    q = bytearray(q)
    _, items, nodes = gbchar.parse_qch(bytes(q))
    q[0xc9] = len(items)
    q[4:8] = b"\x12\x00\x71\x58" if nodes else bytes(4)
    end = gbchar.QCH_FIXED + gbchar.ITEM * len(items)
    for k in range(len(nodes)):
        p = end + gbchar.NODE * k + 5
        q[p:p + 4] = bytes(4) if k == len(nodes) - 1 else bytes([9 * k + 9, 0, 0x71, 0x58])
    return bytes(q)


@pytest.mark.parametrize("case", [c for c in CASES if c[4] is None])
def test_round_trip_gives_the_qch_back(cbin, tmp_path, case):
    """.QCH -> .CCH -> .QCH with the original as the base is the original,
    in the reference and in the C (no container: one cannot come back)."""
    q = shaped(synthetic(*case))
    c, _ = gbchar.convert(q, native=True)
    assert gbchar.to_qch(c, q) == q
    assert run_w(cbin, tmp_path, c, q) == q.hex()


@pytest.mark.parametrize("case", CASES)
def test_writer_matches_gbchar(cbin, tmp_path, case):
    """A changed character, a container among its items, and no base."""
    q = bytes(synthetic(*case))
    c, _ = gbchar.convert(q, native=True)
    c = bytearray(c)
    c[0x60 + 3] ^= 0x5a                       # the name, 4th letter
    c[0x5b] = 5 | 2 << 3 | 3 << 5              # god, robe, order
    c[0x17e] = 99                              # current hit points
    if case[2]:
        c[8:12] = b"\x00\x2b\xd3\x5c"           # a live pointer, as written
    if case[3]:
        c[4:8] = b"\x00\x2b\xd3\x70"
    for base in (q, None):
        want = gbchar.to_qch(bytes(c), base)
        assert run_w(cbin, tmp_path, c, base) == want.hex()
        assert want[0x6b] == q[0x6b] ^ 0x5a and want[0x18b] == 99
        assert (want[0x19b] & 7, want[0x19d] & 3, want[0x19a] & 3) == (5, 2, 3)


def test_container_and_contents_are_left_out(cbin, tmp_path):
    q = bytes(synthetic(2, 5, 3, 2, None))
    c, _ = gbchar.convert(q, native=True)
    rec, items, nodes = bytearray(c[:398]), c[398:398 + 54], c[398 + 54:]
    box = bytearray(18)
    box[0], box[13] = 73, 2
    stream = rec + items[:18] + box + items[18:54] + items[:18] + nodes
    stream[193] = 3                             # three top-level items
    want = gbchar.to_qch(bytes(stream), q)
    assert want[8] == 2 and want[0xc9] == 2
    assert run_w(cbin, tmp_path, stream, q) == want.hex()


@pytest.mark.skipif(not glob.glob(os.path.join(HEROES, "*.QCH")),
                    reason="the Dark Queen's heroes are not on this machine")
def test_real_heroes_round_trip(cbin, tmp_path):
    for path in sorted(glob.glob(os.path.join(HEROES, "*.QCH"))):
        with open(path, "rb") as fh:
            q = fh.read()
        c, _ = gbchar.convert(q, native=True)
        assert gbchar.to_qch(c, q) == q, path
        assert run_w(cbin, tmp_path, c, q) == q.hex(), path
