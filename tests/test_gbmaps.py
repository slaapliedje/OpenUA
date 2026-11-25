"""src/engine/gbmaps.c and gblib.c against tools/gbgeo.py: a Gold Box
game's maps and scripts read from its own libraries.

The Python functions are the reference (ADR-0026): load_maps() reads the
maps, GbMap.to_frua().build() writes the FRUA area, map_descriptor() the
descriptor plane, and ecl.load_blocks() the scripts. The C must give the
same bytes, on synthetic libraries (both byte orders, Gen1 16x16 members and
the Dark Queen's sized ones, a wide map, a sealed map) and on the real
GEO.GLB and ECL.GLB when the machine has them. The game data is never
committed.
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
import gbgeo                                                   # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "src", "engine", "gbmaps.c"),
       os.path.join(ROOT, "src", "engine", "gblib.c"),
       os.path.join(ROOT, "tests", "gbmaps_host.c")]


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("gbmaps") / "gbmaps_host")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-o", exe] + SRC, check=True)
    return exe


def run(cbin, *args):
    return subprocess.run([cbin] + [str(a) for a in args], check=True,
                          capture_output=True, text=True).stdout.strip()


def library(entries, magic=b"HLIB"):
    """entries: [(id, member bytes)] in id-table order."""
    e = "<" if magic == b"HLIB" else ">"
    table = struct.pack(e + "H", len(entries))
    for k, (ident, _) in enumerate(entries, 1):
        table += struct.pack(e + "HH", ident, k)
    body = [table] + [m for _, m in entries]
    off = 16 + 4 * (len(body) + 1)
    offs = []
    for b in body:
        offs.append(off)
        off += len(b)
    offs.append(off)
    head = magic + struct.pack(e + "IHH", off, len(body), 1) + b"DATA"
    return head + struct.pack(e + "%dI" % len(offs), *offs) + b"".join(body)


def planes(w, h, rnd, sealed=False):
    n = w * h
    if sealed:      # every edge a plain wall: no way out anywhere
        return bytes([0x11] * n) + bytes([0x11] * n) + bytes(rnd.randrange(256)
                                                            for _ in range(n)) + bytes(n)
    return bytes(rnd.choice((0, 0, 0x10, 0x01, 0x23, 0xf0, 0x0f)) for _ in range(2 * n)) \
        + bytes(rnd.randrange(256) for _ in range(n)) \
        + bytes(rnd.choice((0, 0, 0, 1, 4, 0x10, 0x40, 0xff)) for _ in range(n))


def sized(w, h, p):
    return bytes([h, w, 0, 0, 0, 0, 0, 0]) + p


@pytest.mark.parametrize("magic", [b"HLIB", b"GLIB"])
def test_synthetic_maps_match_reference(cbin, tmp_path, magic):
    rnd = random.Random(7)
    entries = [(3, planes(16, 16, rnd)),                       # Gen1 block
               (9, sized(10, 7, planes(10, 7, rnd))),          # sized, asymmetric
               (2, sized(28, 28, planes(28, 28, rnd))),        # wide: 784 cells
               (40, sized(5, 4, planes(5, 4, rnd, sealed=True)))]
    lib = tmp_path / "GEO.GLB"
    data = library(entries, magic)
    lib.write_bytes(data)
    maps = gbgeo.load_maps(data)
    assert run(cbin, lib, "ids").split() == [str(m.area_id) for m in maps]
    for i, m in enumerate(maps):
        assert bytes.fromhex(run(cbin, lib, "geo", i)) == m.to_frua().build(), i
        for boot in (0, 1):
            assert bytes.fromhex(run(cbin, lib, "desc", i, boot)) \
                == gbgeo.map_descriptor(m, boot), i


def test_not_a_map(cbin, tmp_path):
    lib = tmp_path / "GEO.GLB"
    lib.write_bytes(library([(3, bytes(100))]))
    assert run(cbin, lib, "geo", 0) == "ERR -4"
    assert run(cbin, lib, "geo", 1) == "ERR -2"
    lib.write_bytes(b"NOPE" + bytes(40))
    assert run(cbin, lib, "ids") == "ERR -1"


def test_member_by_id_is_the_last_valid_entry(cbin, tmp_path):
    lib = tmp_path / "ECL.GLB"
    data = bytearray(library([(5, b"first"), (5, b"second"), (6, b"other")]))
    lib.write_bytes(bytes(data))
    assert bytes.fromhex(run(cbin, lib, "member", 5)) == b"second"
    assert run(cbin, lib, "member", 7) == "ERR -2"


def _real(name):
    for p in sorted(glob.glob(os.path.expanduser(
            "~/.local/share/Steam/steamapps/common/*Krynn*/games/"
            "The Dark Queen of Krynn/GAME/DQK/DISK3/%s" % name))):
        return p
    pytest.skip("no Dark Queen %s on this machine" % name)


def test_real_maps_match_reference(cbin):
    path = _real("GEO.GLB")
    with open(path, "rb") as fh:
        maps = gbgeo.load_maps(fh.read())
    assert len(maps) == 20
    assert run(cbin, path, "ids").split() == [str(m.area_id) for m in maps]
    for i, m in enumerate(maps):
        assert bytes.fromhex(run(cbin, path, "geo", i)) == m.to_frua().build(), i
        assert bytes.fromhex(run(cbin, path, "desc", i, 1 if i == 0 else 0)) \
            == gbgeo.map_descriptor(m, 1 if i == 0 else 0), i


def test_real_scripts_match_reference(cbin):
    path = _real("ECL.GLB")
    with open(path, "rb") as fh:
        blocks = ecl.load_blocks(fh.read())
    assert len(blocks) == 47
    for b in blocks:
        assert bytes.fromhex(run(cbin, path, "member", b.block_id)) == b.raw, b.block_id


def test_a_dax_block_is_a_map(cbin, tmp_path):
    """Champions' and Death Knights' maps: each a .DAX block of 1026 bytes,
    the 1024 planes behind a little-endian length word (gbgeo
    from_gen1_block); the bare 1024 too."""
    rnd = random.Random(11)
    p = planes(16, 16, rnd)
    for raw in (struct.pack("<H", 1024) + p, p):
        f = tmp_path / "B.DAT"
        f.write_bytes(raw)
        m = gbgeo.GbMap.from_gen1_block(raw, 36)
        assert bytes.fromhex(run(cbin, f, "block", 36, "geo")) == m.to_frua().build()
        assert bytes.fromhex(run(cbin, f, "block", 36, "desc", 16)) \
            == gbgeo.map_descriptor(m, 16)
    # Death Knights' header word is not 0x0400; the planes are read all the same
    f.write_bytes(struct.pack("<H", 0x830f) + p)
    m = gbgeo.GbMap.from_gen1_block(struct.pack("<H", 0x830f) + p, 32)
    assert bytes.fromhex(run(cbin, f, "block", 32, "geo")) == m.to_frua().build()
    f.write_bytes(p[:1000])
    assert run(cbin, f, "block", 36, "geo") == "ERR -4"
