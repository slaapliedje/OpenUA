"""Tests for the FRSC archive packer (tools/rsrcpack.py)."""
import struct

from fixtures import build_resource_fork
from macrsrc import Resource, ResourceFork
from rsrcpack import build_archive


def _parse_frsc(blob):
    """Parse a FRSC archive into (version, reserved, entries), where each
    entry is (type, id, attrs, data) in entry-table order."""
    magic, version, count, tbl_off, reserved = struct.unpack_from(
        ">4sHHII", blob, 0)
    assert magic == b"FRSC"
    entries = []
    for i in range(count):
        e = tbl_off + i * 16
        rtype = blob[e:e + 4]
        rid, attrs, off, length = struct.unpack_from(">hHII", blob, e + 4)
        entries.append((rtype, rid, attrs, blob[off:off + length]))
    return version, reserved, entries


def test_header_fields():
    blob = build_archive([Resource("CODE", 1, "", 0, b"abcd")])
    magic, version, count, tbl_off, reserved = struct.unpack_from(
        ">4sHHII", blob, 0)
    assert magic == b"FRSC"
    assert version == 1
    assert count == 1
    assert tbl_off == 16
    assert reserved == 0


def test_entries_sorted_by_type_then_id():
    res = [Resource("MENU", 5, "", 0, b"m5"),
           Resource("CODE", 9, "", 0, b"c9"),
           Resource("CODE", 2, "", 0, b"c2"),
           Resource("MENU", 1, "", 0, b"m1")]
    _v, _r, entries = _parse_frsc(build_archive(res))
    assert [(t, i) for (t, i, _a, _d) in entries] == [
        (b"CODE", 2), (b"CODE", 9), (b"MENU", 1), (b"MENU", 5)]


def test_data_round_trips():
    res = [Resource("STR ", 100, "", 0, b"hello"),
           Resource("STR ", 7, "", 0, b"x" * 300)]
    _v, _r, entries = _parse_frsc(build_archive(res))
    got = {(t, i): d for (t, i, _a, d) in entries}
    assert got[(b"STR ", 7)] == b"x" * 300
    assert got[(b"STR ", 100)] == b"hello"


def test_negative_id_and_attrs_preserved():
    res = [Resource("snd ", -16000, "", 0x40, b"\x01\x02\x03")]
    _v, _r, entries = _parse_frsc(build_archive(res))
    assert entries[0] == (b"snd ", -16000, 0x40, b"\x01\x02\x03")


def test_data_section_follows_entry_table():
    res = [Resource("CODE", 1, "", 0, b"aa"),
           Resource("CODE", 2, "", 0, b"bbb")]
    blob = build_archive(res)
    first_data_off = struct.unpack_from(">I", blob, 16 + 8)[0]
    assert first_data_off == 16 + 16 * 2          # header + two entries


def test_empty_archive():
    version, _r, entries = _parse_frsc(build_archive([]))
    assert version == 1
    assert entries == []


def test_packs_a_resource_fork():
    fork = build_resource_fork([("CODE", 1, "Main", b"ABCD"),
                                ("DATA", 0, "", b"\x01\x02")])
    _v, _r, entries = _parse_frsc(build_archive(ResourceFork(fork).resources))
    got = {(t, i): d for (t, i, _a, d) in entries}
    assert got == {(b"CODE", 1): b"ABCD", (b"DATA", 0): b"\x01\x02"}
