"""The DOS-built frua.rsc must reconstruct the Mac STRS pool byte-exactly.

tools/rsrc_from_dos.py builds the shipping frua.rsc for a DOS-sourced
install: 2108 entries extracted from the user's CKIT.EXE by position
(installer/strs_map_dos12.json) plus 37 port-authored Mac-platform strings.
The A4 relocations and the lifted code's fixed-offset lookups depend on the
pool matching the original exactly — these tests hold the reconstruction to
byte-identity and pin the verify-refuse behaviour (ADR-0017 decision 5).

Skips when the copyrighted inputs are not staged under data/.
"""
import os
import sys

import pytest

RFORK = "data/work/UnlimitedAdventures.rfork"
DOSEXE = "data/dos-frua/CKIT.EXE"
MAP = "installer/strs_map_dos12.json"

pytestmark = pytest.mark.skipif(
    not (os.path.exists(RFORK) and os.path.exists(DOSEXE)),
    reason="Mac resource fork / DOS CKIT.EXE not staged under data/",
)


@pytest.fixture(scope="module")
def parts():
    import json
    sys.path.insert(0, "tools")
    from macrsrc import ResourceFork
    import rsrc_from_dos
    rf = ResourceFork.from_file(RFORK)
    mac = {(r.type, r.id): r.data for r in rf.resources}[("STRS", 0)]
    doc = json.load(open(MAP))
    exe = open(DOSEXE, "rb").read()
    return rsrc_from_dos, mac, doc, exe


def test_pool_is_byte_identical(parts):
    mod, mac, doc, exe = parts
    assert mod.build_strs(exe, doc) == mac


def test_verify_refuses_a_different_build(parts):
    mod, _, doc, exe = parts
    ok, _ = mod.verify_exe(exe, doc)
    assert ok
    ok, msg = mod.verify_exe(exe + b"x", doc)
    assert not ok and "different DOS build" in msg
    ok, msg = mod.verify_exe(exe[:-1] + bytes([exe[-1] ^ 1]), doc)
    assert not ok


def test_archive_holds_strs_only(parts):
    import struct
    mod, mac, doc, exe = parts
    blob = mod.build_rsc(exe, doc)
    magic, ver, n, hsz, _ = struct.unpack_from(">4sHHII", blob, 0)
    assert magic == b"FRSC" and n == 1
    t, rid, attrs, off, ln = struct.unpack_from(">4shHII", blob, 16)
    assert t == b"STRS" and rid == 0 and ln == len(mac)
    assert blob[off:off + ln] == mac
