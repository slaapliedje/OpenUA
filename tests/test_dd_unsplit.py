"""Tests for DiskDoubler split-archive reassembly (tools/dd_unsplit.py)."""
import dd_unsplit

from fixtures import build_splt_segment


def test_read_segment_parses_header(tmp_path):
    payload = b"abcdefgh"
    p = tmp_path / "s.dd"
    p.write_bytes(build_splt_segment(0, 1, len(payload), payload))
    count, total, index, got = dd_unsplit.read_segment(str(p))
    assert (count, total, index, got) == (1, len(payload), 0, payload)


def test_reassembly_orders_segments_by_index(tmp_path):
    parts = [b"AAAA", b"BBBBBB", b"CC"]
    total = sum(len(p) for p in parts)
    files = []
    for idx in (2, 0, 1):                       # fed out of order on purpose
        f = tmp_path / f"part{idx}.dd"
        f.write_bytes(build_splt_segment(idx, 3, total, parts[idx]))
        files.append(str(f))
    out = tmp_path / "joined.bin"
    dd_unsplit.main(files + ["-o", str(out)])
    assert out.read_bytes() == b"".join(parts)
