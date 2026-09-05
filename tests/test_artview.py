"""artview.py — the offline art renderer.

Its whole value is answering "is this mark in SSI's data, or did the port put
it there?", so what has to be right is that a decoded pixel lands at the right
COORDINATE with the right COLOUR. perband's decoders are already pinned for
the colour SETS they return; these tests pin the positions its pixel-preserving
twins add, plus the palette-window law and composite placement.

PIL is only imported inside artview.render_entry, so none of this needs it.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import artview  # noqa: E402
import prequant  # noqa: E402

from test_prequant import _glib, _pal_block  # noqa: E402
from test_perband import _raw8_piece  # noqa: E402


def _mode7_piece(rows, width_bytes, body):
    """A transparency-RLE piece: flags 0x47."""
    hdr = struct.pack(">Hhh", rows, 0, 0) + bytes([width_bytes, 0x47])
    return hdr + bytes(body)


def test_raw8_pixels_keep_their_x():
    # one 8px row: indices 5,255,7 then transparent -> only x=0 and x=2 set
    body = bytes([5, 255, 7, 255, 255, 255, 255, 255])
    rows = artview.px_raw8(body, 8, 1)
    assert rows == {0: {0: 5, 2: 7}}


def test_mode7_skip_advances_x_by_257_minus_b():
    # 2 literals at x0,x1; skip 3 (b=254); 1 literal at x5; 0 ends the row
    rows = artview.px_mode7(bytes([2, 40, 41, 254, 1, 42, 0]), 16, 1)
    assert rows == {0: {0: 40, 1: 41, 5: 42}}


def test_mode7_index_zero_is_transparent():
    rows = artview.px_mode7(bytes([3, 9, 0, 11, 0]), 16, 1)
    assert rows == {0: {0: 9, 2: 11}}          # the 0 in the middle is a hole


def test_mode2_packbits_pixels():
    # 2 literals then a run of 4 -> 6 px, none transparent
    body = bytes([1, 0xAA, 0xBB, 253, 0x0C])
    rows = artview.px_mode2(body, 6, 1)
    assert rows == {0: {0: 0xAA, 1: 0xBB, 2: 0x0C, 3: 0x0C, 4: 0x0C, 5: 0x0C}}


def test_bearings_offset_the_piece():
    # ybear/xbear are SUBTRACTED from the caller's origin (perband does the
    # same for y); a piece with bearings (-3,-5) lands at (+5,+3).
    hdr = struct.pack(">Hhh", 1, -3, -5) + bytes([1, 0xC0])
    ent = hdr + bytes([7] + [255] * 7)
    g = _glib([ent])
    pix = artview.piece_pixels(g, 0)
    assert pix == {(5, 3): 7}


def test_composite_places_children_by_dx_dy():
    # entry 0: the composite. entries 1 and 2: one pixel each.
    one = struct.pack(">Hhh", 1, 0, 0) + bytes([1, 0xC0]) + bytes([3] + [255] * 7)
    two = struct.pack(">Hhh", 1, 0, 0) + bytes([1, 0xC0]) + bytes([4] + [255] * 7)
    # 6-byte records: [child, total, dy(BE16), dx(BE16)]
    rec = (bytes([1, 2]) + struct.pack(">hh", 10, 20)
           + bytes([2, 2]) + struct.pack(">hh", 30, 40))
    comp = struct.pack(">Hhh", 1, 0, 0) + bytes([1, 0x09]) + rec
    g = _glib([comp, one, two])
    pix = artview.piece_pixels(g, 0)
    assert pix == {(20, 10): 3, (40, 30): 4}


def test_clut_applies_the_palette_window():
    cols = [(10, 20, 30), (40, 50, 60), (70, 80, 90)]
    pal = _pal_block(32, cols)
    ent = _glib([pal])
    clut, n = artview.clut_of(ent)
    assert n == 1
    assert clut[32] == (10, 20, 30)
    assert clut[33] == (40, 50, 60)
    assert clut[34] == (70, 80, 90)
    assert clut[31] == (0, 0, 0)               # outside the window, untouched


def test_mono_piece_contributes_no_pixels():
    # no 0x40 = 1bpp mono, drawn in the engine's pen; there is no pen offline,
    # so it must be skipped rather than guessed at (same rule as perband).
    ent = struct.pack(">Hhh", 4, 0, 0) + bytes([1, 0x92]) + bytes(16)
    g = _glib([ent])
    assert artview.piece_pixels(g, 0) == {}
