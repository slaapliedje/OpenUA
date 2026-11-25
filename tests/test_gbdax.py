"""src/engine/gbdax.c against tools/ecl.py dax_blocks: a Gold Box DOS .DAX
archive, the container Champions and Death Knights of Krynn keep their maps,
scripts and pictures in.

The Python is the reference. The C must unpack the same blocks from
synthetic archives (literal runs, repeat runs, both at their limits, an
empty block) and refuse the same broken ones, and unpack every archive of
the two games when the machine has them. The game data is never committed.
"""
import glob
import os
import random
import struct
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import ecl                                                     # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "src", "engine", "gbdax.c"),
       os.path.join(ROOT, "tests", "gbdax_host.c")]
KRYNN = os.path.expanduser(
    "~/.local/share/Steam/steamapps/common/Dungeons & Dragons Krynn Series/games")
REAL = (sorted(glob.glob(os.path.join(KRYNN, "Champions of Krynn", "GAME", "COK", "*.DAX")))
        + sorted(glob.glob(os.path.join(KRYNN, "Death Knights of Krynn", "GAME", "DKK", "*.DAX"))))


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("gbdax") / "gbdax_host")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-o", exe] + SRC, check=True)
    return exe


def c_read(cbin, path):
    out = subprocess.run([cbin, str(path)], check=True, capture_output=True,
                         text=True).stdout.split("\n")
    if out[0].startswith("ERR"):
        return int(out[0].split()[1])
    blocks = {}
    for line in out:
        if not line:
            continue
        bid, _, rest = line.partition(" ")
        blocks[int(bid)] = rest if rest.startswith("ERR") else bytes.fromhex(rest)
    return blocks


def pack(data, rnd):
    """A packing of `data` with both kinds of run, as the games mix them."""
    out, p = bytearray(), 0
    while p < len(data):
        run = 1
        while p + run < len(data) and data[p + run] == data[p] and run < 128:
            run += 1
        if run >= 3 or (run == 2 and rnd.randrange(2)):
            out += bytes([256 - run, data[p]])
            p += run
        else:
            n = min(rnd.randrange(1, 129), len(data) - p)
            out += bytes([n - 1]) + data[p:p + n]
            p += n
    return bytes(out)


def archive(blocks, rnd):
    toc, body = bytearray(), bytearray()
    for bid, data in blocks:
        packed = pack(data, rnd)
        toc += struct.pack("<BIHH", bid, len(body), len(data), len(packed))
        body += packed
    return struct.pack("<H", len(toc)) + bytes(toc) + bytes(body)


def test_synthetic_archives_match_reference(cbin, tmp_path):
    rnd = random.Random(1990)
    for case in range(10):
        blocks = []
        for k in range(rnd.randrange(1, 8)):
            n = rnd.choice((0, 1, 2, 127, 128, 129, 1026, 5000))
            data = bytes(rnd.choice((0, 0, 0, rnd.randrange(256)))
                         for _ in range(n))
            blocks.append((rnd.randrange(256) if k else 255, data))
        ids = [b for b, _ in blocks]
        if len(set(ids)) != len(ids):
            continue
        d = archive(blocks, rnd)
        p = tmp_path / ("a%d.dax" % case)
        p.write_bytes(d)
        ref = ecl.dax_blocks(d)
        assert ref == dict(blocks)
        assert c_read(cbin, p) == ref


def test_refuses_what_the_reference_refuses(cbin, tmp_path):
    rnd = random.Random(3)
    good = archive([(7, bytes(range(200)))], rnd)
    cases = {
        "toc": (b"\x05\x00" + good[2:], "ERR"),
        "past": (good[:-5], "block"),
        "size": (good[:2 + 5] + struct.pack("<H", 201) + good[2 + 7:], "block"),
    }
    for name, (d, kind) in cases.items():
        p = tmp_path / (name + ".dax")
        p.write_bytes(d)
        out = c_read(cbin, p)
        if kind == "ERR":
            assert out == -1, name
        else:
            assert isinstance(out[7], str) and out[7].startswith("ERR"), name
        with pytest.raises(ecl.EclError):
            ecl.dax_blocks(d)


@pytest.mark.skipif(not REAL, reason="Champions / Death Knights not on this machine")
def test_real_archives_match_reference(cbin):
    for path in REAL:
        d = open(path, "rb").read()
        try:
            ref = ecl.dax_blocks(d)
        except ecl.EclError:
            continue                    # not every .DAX unpacks this way
        assert c_read(cbin, path) == ref, path


def test_an_id_listed_twice_gives_its_first_block(cbin, tmp_path):
    """Champions' PIC1, SPRIT2 and SPRIT3 list an id twice."""
    d = archive([(14, b"first" * 10), (2, b"x"), (14, b"second" * 10)],
                random.Random(8))
    p = tmp_path / "dup.dax"
    p.write_bytes(d)
    assert ecl.dax_blocks(d)[14] == b"first" * 10
    assert c_read(cbin, p) == ecl.dax_blocks(d)
