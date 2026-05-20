"""Tests for the AppleSingle/AppleDouble fork extractor (tools/appledouble.py)."""
import appledouble

from fixtures import build_appledouble


def test_extract_resource_fork(tmp_path):
    rfork = b"RESOURCE-FORK-BYTES"
    src = tmp_path / "file.ad"
    src.write_bytes(build_appledouble({9: b"\0" * 32, 2: rfork}))
    out = tmp_path / "out.rsrc"
    appledouble.main([str(src), "--fork", "resource", "-o", str(out)])
    assert out.read_bytes() == rfork


def test_extract_data_fork(tmp_path):
    src = tmp_path / "file.ad"
    src.write_bytes(build_appledouble({1: b"DATA", 2: b"RSRC"}))
    out = tmp_path / "out.bin"
    appledouble.main([str(src), "--fork", "data", "-o", str(out)])
    assert out.read_bytes() == b"DATA"
