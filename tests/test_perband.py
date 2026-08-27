"""perband.py — the per-band budget constraint (ADR-0020 v2).

The tool's whole claim is: after conversion, no band-aligned strip needs more
than `budget` distinct colours, and nothing but palette RGB bytes changed.
Both are pinned here on synthetic art (no data/ dependency, runs in CI).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import perband  # noqa: E402
import prequant  # noqa: E402

from test_prequant import _glib, _pal_block  # noqa: E402


def _raw8_piece(rows, width_bytes, pixels):
    """A raw 8bpp piece (l2d4e's fall-through arm): flags 0xC0."""
    hdr = struct.pack(">Hhh", rows, 0, 0) + bytes([width_bytes, 0xC0])
    return hdr + bytes(pixels)


def _picture(colours, pixel_rows, start=32):
    """One picture: a type-8 palette plus a raw 8bpp piece."""
    pal = _pal_block(start, colours)
    w = len(pixel_rows[0])
    flat = []
    for r in pixel_rows:
        flat += list(r)
    piece = _raw8_piece(len(pixel_rows), w // 8, flat)
    return _glib([pal, piece])


def test_unpackbits_literal_and_run():
    # 2 literals (n=1 -> n+1), then a run of 4 (n = 257-4 = 253)
    data = bytes([1, 0xAA, 0xBB, 253, 0xCC])
    out, pos = perband.unpackbits(data, 0, 6)
    assert out == bytes([0xAA, 0xBB, 0xCC, 0xCC, 0xCC, 0xCC])
    assert pos == len(data)


def test_mode7_skip_is_257_minus_b():
    # 2 literals, skip 3 (b = 254), 1 literal, end of row
    body = bytes([2, 40, 41, 254, 1, 42, 0])
    rows = perband.decode_mode7(body, 16, 1)
    assert rows[0] == {40, 41, 42}


def test_raw8_drops_transparent_255():
    rows = perband.decode_raw8(bytes([5, 255, 6, 255, 255, 255, 255, 255]), 8, 1)
    assert rows[0] == {5, 6}


def test_mono_piece_does_not_disqualify():
    # flags without 0x40 = a 1bpp mono piece: contributes no colours, and
    # must not raise (that used to skip whole libraries).
    ent = struct.pack(">Hhh", 4, 0, 0) + bytes([1, 0x92]) + bytes(16)
    g = _glib([ent])
    assert perband.piece_rows(g, 0, 0) == {}


def test_budget_is_met_and_only_palette_bytes_change():
    # 4 rows of 8 px, each row a distinct pair of indices -> one 8-row strip
    # holding 8 distinct colours; budget 3 forces merging.
    cols = [(i * 25, 255 - i * 25, (i * 60) % 256) for i in range(8)]
    rows = [[32 + (2 * r) % 8] * 4 + [32 + (2 * r + 1) % 8] * 4 for r in range(4)]
    pic = _picture(cols, rows)
    data = _glib([bytes([0, 8, 0, 240, 0, 1, 0, 241]), pic])

    stats = {"pics": 0, "merges": 0, "err": 0.0, "slots": 0, "before": 0,
             "after": 0, "skipped": 0, "reasons": {},
             "overfull_before": 0, "overfull_after": 0}
    new = perband.convert_file(data, budget=3, rows_per_band=8, screen_y=24,
                               stats=stats)

    assert stats["pics"] == 1
    assert stats["skipped"] == 0
    assert stats["after"] <= 3, "budget not met"
    assert stats["overfull_after"] == 0
    assert len(new) == len(data)

    # every changed byte must lie inside a type-8 palette's RGB block
    spans = []
    found = []
    prequant.walk_palettes(data, 0, found)
    for off, _s, count, _n in found:
        spans.append((off + 8, off + 8 + count * 3))
    for i, (a, b) in enumerate(zip(data, new)):
        if a != b:
            assert any(lo <= i < hi for lo, hi in spans), \
                "byte %d changed outside a palette RGB block" % i


def test_no_op_when_already_within_budget():
    cols = [(10, 20, 30), (40, 50, 60)]
    rows = [[32] * 4 + [33] * 4 for _ in range(4)]
    pic = _picture(cols, rows)
    data = _glib([bytes([0, 8, 0, 240, 0, 1, 0, 241]), pic])
    stats = {"pics": 0, "merges": 0, "err": 0.0, "slots": 0, "before": 0,
             "after": 0, "skipped": 0, "reasons": {},
             "overfull_before": 0, "overfull_after": 0}
    new = perband.convert_file(data, budget=8, rows_per_band=8, screen_y=24,
                               stats=stats)
    assert stats["merges"] == 0
    assert new == data          # nothing to do -> byte-identical


def test_strips_align_to_the_band_grid():
    # screen_y 24 is a multiple of 8, so picture rows 0..7 are one band and
    # row 8 starts the next. This alignment is the tool's core assumption.
    rowsets = {r: {r} for r in range(16)}
    strips = perband.strips_of(rowsets, 8, 24)
    assert len(strips) == 2
    assert strips[0] == set(range(0, 8))
    assert strips[1] == set(range(8, 16))
