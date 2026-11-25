"""src/engine/gbdaxart.c against tools/gbdaxart.py: Champions' and Death
Knights' pictures (.DAX) as the GLIB picture libraries the engine draws.

The Python is the reference. The C, assembled as the engine assembles a
library (the head, then each picture), must write the same bytes on
synthetic blocks (still and animated pictures, sprites' three frames, a big
picture's plain frame, rows that test every packing rule) and on every PIC,
SPRIT and BIGPIC archive of the two games when the machine has them. The
game data is never committed.
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
import gbdaxart                                                # noqa: E402
import art_convert                                             # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "src", "engine", "gbdaxart.c"),
       os.path.join(ROOT, "src", "engine", "gbdax.c"),
       os.path.join(ROOT, "tests", "gbdaxart_host.c")]
KRYNN = os.path.expanduser(
    "~/.local/share/Steam/steamapps/common/Dungeons & Dragons Krynn Series/games")
GAMES = [os.path.join(KRYNN, "Champions of Krynn", "GAME", "COK"),
         os.path.join(KRYNN, "Death Knights of Krynn", "GAME", "DKK")]


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("gbdaxart") / "gbdaxart_host")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-o", exe] + SRC, check=True)
    return exe


def c_build(cbin, out, shift, files):
    r = subprocess.run([cbin, str(out), str(shift)] + [str(f) for f in files],
                       check=True, capture_output=True, text=True).stdout.strip()
    assert not r.startswith("ERR"), r
    return out.read_bytes()


def dax(blocks):
    toc, body = bytearray(), bytearray()
    for bid, data in blocks:
        packed = b"".join(bytes([len(data[i:i + 128]) - 1]) + data[i:i + 128]
                          for i in range(0, len(data), 128))
        toc += struct.pack("<BIHH", bid, len(body), len(data), len(packed))
        body += packed
    return struct.pack("<H", len(toc)) + bytes(toc) + bytes(body)


def block(rnd, sizes, plain=False):
    """Frames of (h, w); pixel rows mixing long runs, pairs and noise."""
    def pixels(h, w):
        out = bytearray()
        for _ in range(h):
            row = []
            while len(row) < w:
                kind = rnd.randrange(4)
                v = rnd.randrange(16)
                row += [v] * (rnd.randrange(3, 200) if kind == 0 else
                              2 if kind == 1 else 1)
            row = row[:w]
            out += bytes(row[i] << 4 | row[i + 1] for i in range(0, w, 2))
        return bytes(out)
    if plain:
        h, w = sizes[0]
        return struct.pack("<HHHHB", h, w // 8, 0, 0, 1) + bytes(8) + pixels(h, w)
    b = bytearray([len(sizes)])
    for h, w in sizes:
        b += struct.pack("<IHHHHB", rnd.randrange(30), h, w // 8, 1, 2, 1) + bytes(8)
        b += pixels(h, w)
    return bytes(b)


def test_frames_read_back():
    rnd = random.Random(1)
    b = block(rnd, [(88, 88), (65, 40)])
    fr = gbdaxart.frames(b)
    assert [(h, w) for _, h, w, _, _, _ in fr] == [(88, 88), (65, 40)]
    big = block(rnd, [(120, 304)], plain=True)
    assert [(h, w) for _, h, w, _, _, _ in gbdaxart.frames(big)] == [(120, 304)]
    with pytest.raises(gbdaxart.DaxArtError):
        gbdaxart.frames(b + b"\0")


def test_synthetic_libraries_match_reference(cbin, tmp_path):
    rnd = random.Random(1991)
    blocks = [(6, block(rnd, [(88, 88)])),
              (24, block(rnd, [(88, 88), (88, 88), (88, 88)])),
              (33, block(rnd, [(88, 56), (73, 32), (57, 24)])),
              (113, block(rnd, [(120, 304)], plain=True))]
    f = tmp_path / "T.DAX"
    f.write_bytes(dax(blocks))
    for shift in (0, 128):
        want = gbdaxart.library(blocks, shift)
        assert c_build(cbin, tmp_path / "OUT.CTL", shift, [f]) == want
    # and the rows decode back to the pixels, 32 + index
    lib = art_convert.parse(gbdaxart.library(blocks))
    pic = art_convert.parse(lib["entries"][2])
    ent = pic["entries"][1]
    h, w = struct.unpack(">H", ent[:2])[0], ent[6] * 8
    rows, p, px = [], 8, gbdaxart.frames(dict(blocks)[24])[0][5]
    lin = art_convert.rle_decode(ent[8:], w * h)
    assert list(lin) == [32 + v for v in px]


@pytest.mark.skipif(not os.path.isdir(GAMES[1]), reason="the games are not on this machine")
@pytest.mark.parametrize("kind,shift", [("PIC", 0), ("BIGPIC", 128), ("SPRIT", 0)])
def test_real_libraries_match_reference(cbin, tmp_path, kind, shift):
    for game in GAMES:
        files = sorted(glob.glob(os.path.join(game, kind + "?.DAX")))
        if not files:
            continue
        want = gbdaxart.library(gbdaxart.archive_blocks(files), shift)
        assert c_build(cbin, tmp_path / "OUT.CTL", shift, files) == want, (game, kind)
