"""prequant.py structural guarantees (ADR-0020 palette-snap).

The whole safety argument of v1 is "only the RGB triples inside type-8
palette blocks change; every other byte survives". These tests pin that on
a synthetic GLIB (no data/ dependency, runs in CI), plus the jt993 window
law the parser implements.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import prequant  # noqa: E402


def _glib(entries, tag=b"GLIB"):
    """Assemble a big-endian GLIB container from raw entry byte strings."""
    count = len(entries)
    off0 = 16 + 4 * (count + 1)
    offsets, pos = [], off0
    for e in entries:
        offsets.append(pos)
        pos += len(e)
    offsets.append(pos)
    head = b"GLIB" + struct.pack(">I", pos) + struct.pack(">HH", count, 0) + tag
    return head + struct.pack(">%dI" % (count + 1), *offsets) + b"".join(entries)


def _pal_block(start, colours, cycles=()):
    """Type-8 block, explicit window, optional cycle records."""
    flags = 1 | (2 if cycles else 0)
    hdr = bytes([0, flags]) + struct.pack(">hh", start, len(colours)) \
        + bytes([len(cycles), 0xC8])
    body = b"".join(bytes(c) for c in colours) \
        + b"".join(bytes(r) for r in cycles)
    return hdr + body


def _fixture():
    pal = _pal_block(32, [(255, 0, 0), (0, 255, 0), (10, 20, 30), (200, 200, 200)],
                     cycles=[(0, 12, 33, 2)])
    piece = bytes([0, 59, 0, 0, 0, 0, 38, 0xC7]) + b"\x03\x01\x02\x03\x00"
    sub = _glib([pal, piece])
    index = bytes([0, 8, 0, 240, 0, 1, 0, 241])
    return _glib([index, sub]), pal, piece


def test_walk_finds_nested_palette():
    data, pal, _ = _fixture()
    found = []
    prequant.walk_palettes(data, 0, found)
    assert len(found) == 1
    off, start, count, ncopy = found[0]
    assert (start, count, ncopy) == (32, 4, 1)
    assert data[off:off + len(pal)] == pal


def test_snap_changes_only_rgb_triples():
    data, _, _ = _fixture()
    found = []
    prequant.walk_palettes(data, 0, found)
    target = [(255, 0, 0), (0, 0, 0)]
    new, slots, _mse, _coll = prequant.snap_file(data, found, target)
    assert len(new) == len(data)
    assert slots == 4
    off = found[0][0]
    lo, hi = off + 8, off + 8 + 4 * 3
    # everything outside the triple span is byte-identical
    assert new[:lo] == data[:lo]
    assert new[hi:] == data[hi:]
    # every rewritten triple is one of the target colours
    for i in range(4):
        assert tuple(new[lo + i * 3:lo + i * 3 + 3]) in target


def test_implicit_window_count_from_size():
    # hdr[1] bit0 clear -> count derived from the block size (jt993's
    # mode-dependent arm, pinned against TITLE.ctl's 256-colour block)
    hdr = bytes([0, 0, 0, 0, 0, 0, 0, 0xC8])
    block = hdr + bytes(256 * 3)
    assert prequant.pal_window(block) == (0, 256, 0)


def test_non_glib_passthrough():
    found = []
    prequant.walk_palettes(b"HLIB" + bytes(60), 0, found)
    assert found == []


def test_median_cut_snaps_to_channel_depth():
    pal = prequant.median_cut({(255, 128, 3): 5, (0, 40, 250): 5}, 2, 3)
    assert 1 <= len(pal) <= 2
    step = 255 / 7
    for c in pal:
        for ch in c:
            assert abs(ch - round(ch / step) * step) < 1
