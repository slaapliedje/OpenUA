"""Tests for the classic Mac resource-fork reader (tools/macrsrc.py)."""
import pytest

from fixtures import build_resource_fork
from macrsrc import ResourceFork


def test_round_trip_types_and_count():
    res = [("CODE", 0, "", b"\x00" * 8),
           ("CODE", 1, "Main", b"ABCD"),
           ("STR ", 100, "", b"hello")]
    rf = ResourceFork(build_resource_fork(res))
    assert len(rf.resources) == 3
    assert {r.type for r in rf.resources} == {"CODE", "STR "}


def test_of_type_is_sorted_by_id():
    rf = ResourceFork(build_resource_fork(
        [("CODE", 5, "", b"x"), ("CODE", 1, "", b"yy")]))
    assert [r.id for r in rf.of_type("CODE")] == [1, 5]


def test_get_returns_data_and_name():
    rf = ResourceFork(build_resource_fork([("CODE", 1, "Main", b"ABCD")]))
    r = rf.get("CODE", 1)
    assert r.data == b"ABCD"
    assert r.name == "Main"


def test_get_missing_raises_keyerror():
    rf = ResourceFork(build_resource_fork([("CODE", 1, "", b"z")]))
    with pytest.raises(KeyError):
        rf.get("CODE", 999)


def test_from_file(tmp_path):
    p = tmp_path / "fork.bin"
    p.write_bytes(build_resource_fork([("DATA", 0, "", b"\x01\x02")]))
    rf = ResourceFork.from_file(str(p))
    assert rf.get("DATA", 0).data == b"\x01\x02"
