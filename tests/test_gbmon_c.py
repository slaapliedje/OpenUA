"""src/engine/gbmon.c against tools/gbmon.py: a Gold Box monster library.

The Python functions are the reference (ADR-0026): load_library() and
unpack() for the container, convert() for the record. The C reader must give
the same bytes, on synthetic libraries (both byte orders, records with
effects and items, a record too short) and on the real MONCHA.GLB when the
machine has it. The game data is never committed.
"""
import glob
import os
import struct
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))

import gbmon                                                   # noqa: E402
from test_gbmon import library, record                         # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "src", "engine", "gbmon.c"),
       os.path.join(ROOT, "src", "engine", "gblib.c"),
       os.path.join(ROOT, "tests", "gbmon_host.c")]


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("gbmon") / "gbmon_host")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-o", exe] + SRC, check=True)
    return exe


def c_member(cbin, lib, ident, items=None, native=False):
    args = [cbin, str(lib), str(ident), str(items) if items else "-"]
    if native:
        args.append("native")
    out = subprocess.run(args, check=True, capture_output=True, text=True).stdout
    lines = out.splitlines()
    if lines[0].startswith("ERR"):
        return lines[0], None
    return bytes.fromhex(lines[0]), (bytes.fromhex(lines[1])
                                     if lines[1] != "SHORT" else "SHORT")


def table17(n=8):
    t = bytearray(17 * n)
    for k in range(n):
        t[k * 17] = 0x10 + k
        t[k * 17 + 3] = 0x40 + k
    return bytes(t)


@pytest.mark.parametrize("magic", [b"HLIB", b"GLIB"])
def test_synthetic_library_matches_reference(cbin, tmp_path, magic):
    items = table17()
    worn = bytearray(items[2 * 17:3 * 17])
    worn[10] = 1
    q = bytearray(record(effects=(3, 9, 200), items=(bytes(worn), bytes([0x99] * 17),
                                                      items[7 * 17:8 * 17])))
    q[0x19a], q[0x19b], q[0x19d] = 2, 5, 1
    members = {4: record(), 77: bytes(q), 300: record(effects=range(1, 13))}
    lib = tmp_path / "MONCHA.GLB"
    lib.write_bytes(library(members, magic))
    it = tmp_path / "ITEM.DAT"
    it.write_bytes(items)
    ref = gbmon.load_library(lib.read_bytes())
    index = gbmon.item_index(items)
    for ident in members:
        for native in (False, True):
            raw, rec = c_member(cbin, lib, ident, it, native)
            assert raw == ref[ident]
            assert rec == gbmon.convert(ref[ident], index, native)[0], (ident, native)
    raw, rec = c_member(cbin, lib, 77)                  # no ITEM.DAT: items left off
    assert rec == gbmon.convert(ref[77])[0]


def test_absent_member_and_not_a_library(cbin, tmp_path):
    lib = tmp_path / "MONCHA.GLB"
    lib.write_bytes(library({4: record()}))
    assert c_member(cbin, lib, 5)[0] == "ERR -2"
    lib.write_bytes(b"NOPE" + bytes(60))
    assert c_member(cbin, lib, 4)[0] == "ERR -1"


def test_short_record(cbin, tmp_path):
    lib = tmp_path / "MONCHA.GLB"
    lib.write_bytes(library({4: bytes(0x100)}))
    assert c_member(cbin, lib, 4)[1] == "SHORT"


def test_runs_unpack(cbin, tmp_path):
    """PackBits runs, which the synthetic packer never writes."""
    rec = record()
    rec = bytes([0xaa] * 128) + rec[128:]
    packed = bytearray(struct.pack(">H", len(rec)) + bytes([0x81, 0xaa]))  # 128 x 0xaa
    for i in range(128, len(rec), 100):
        packed += bytes([len(rec[i:i + 100]) - 1]) + rec[i:i + 100]
    packed = bytes(packed)
    lib = bytearray(library({4: rec}))
    # replace member 1 with the run-packed bytes
    n = struct.unpack_from("<H", lib, 8)[0]
    offs = list(struct.unpack_from("<%dL" % (n + 1), lib, 16))
    lib = lib[:offs[1]] + packed
    offs[2] = len(lib)
    struct.pack_into("<%dL" % (n + 1), lib, 16, *offs)
    p = tmp_path / "MONCHA.GLB"
    p.write_bytes(bytes(lib))
    assert c_member(cbin, p, 4)[0] == gbmon.unpack(packed) == rec


def _real(name, disk):
    for p in sorted(glob.glob(os.path.expanduser(
            "~/.local/share/Steam/steamapps/common/*Krynn*/games/"
            "The Dark Queen of Krynn/GAME/DQK/%s/%s" % (disk, name)))):
        return p
    pytest.skip("no Dark Queen %s on this machine" % name)


def test_real_library_matches_reference(cbin):
    lib, items = _real("MONCHA.GLB", "DISK2"), _real("ITEM.DAT", "DISK1")
    with open(lib, "rb") as fh:
        ref = gbmon.load_library(fh.read())
    with open(items, "rb") as fh:
        index = gbmon.item_index(fh.read())
    assert len(ref) > 50
    for ident in sorted(ref):
        raw, rec = c_member(cbin, lib, ident, items, native=ident % 2 == 0)
        assert raw == ref[ident], ident
        assert rec == gbmon.convert(ref[ident], index, ident % 2 == 0)[0], ident
