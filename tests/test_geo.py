"""Round-trip + field tests for tools/geo.py (the GEO area format).

All synthetic — no copyrighted FRUA data. Builds areas from scratch, mutates
fields, and asserts parse(build(x)) == build(x) and the documented invariants.
"""
import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from geo import (Geo, GeoError, GEO_SIZE, HDR_SIZE, MAP_SIZE, ENCR_SIZE,
                 STRG_SIZE, MAX_CELLS, MAX_EVENTS, CELL_SIZE, EVENT_SIZE,
                 VERSION_MAX)


def test_blank_is_fixed_size():
    assert len(Geo.blank(8, 8).build()) == GEO_SIZE


def test_blank_roundtrips():
    g = Geo.blank(11, 24)
    assert Geo.parse(g.build()).build() == g.build()


def test_container_framing():
    data = Geo.blank(4, 4).build()
    assert data[0:4] == b"FORM"
    assert struct.unpack_from(">I", data, 4)[0] == GEO_SIZE - 8
    assert data[8:12] == b"AMOD"
    assert struct.unpack_from(">I", data, 12)[0] == GEO_SIZE - 16
    assert data[16:20] == b"HDR "
    assert struct.unpack_from(">I", data, 20)[0] == HDR_SIZE


def test_chunk_offsets_and_sizes():
    data = Geo.blank(4, 4).build()
    # HDR MAP ENCR STRG appear in order at their fixed sizes.
    off = 16
    for tag, size in ((b"HDR ", HDR_SIZE), (b"MAP ", MAP_SIZE),
                      (b"ENCR", ENCR_SIZE), (b"STRG", STRG_SIZE)):
        assert data[off:off + 4] == tag
        assert struct.unpack_from(">I", data, off + 4)[0] == size
        off += 8 + size
    assert off == GEO_SIZE


def test_version_and_dims_fields():
    g = Geo.blank(11, 24)
    assert g.version == VERSION_MAX
    assert (g.width, g.height) == (11, 24)
    g2 = Geo.parse(g.build())
    assert g2.version == VERSION_MAX and (g2.width, g2.height) == (11, 24)


def test_entry_point_roundtrip():
    g = Geo.blank(16, 16)
    g.set_entry_point(0, x=5, y=7, facing=3)
    g.set_entry_point(2, x=1, y=2, facing=6)
    g = Geo.parse(g.build())
    assert g.entry_point(0) == (5, 7, 3)
    assert g.entry_point(2) == (1, 2, 6)
    # facing is masked to 3 bits and doesn't disturb neighbours
    g.set_entry_point(0, x=5, y=7, facing=0xFF)
    assert g.entry_point(0)[2] == 7


def test_cell_walls_special_zone():
    g = Geo.blank(10, 10)
    g.set_cell(3, 4, walls=(0x1A, 0x20, 0x00, 0xF3), special=5, zone=6)
    g = Geo.parse(g.build())
    raw = g.cell(3, 4)
    assert raw[0:4] == bytes((0x1A, 0x20, 0x00, 0xF3))
    assert g.wall(3, 4, 0) == 0x1        # high nibble of 0x1A
    assert g.wall(3, 4, 3) == 0xF        # high nibble of 0xF3
    assert g.cell_special(3, 4) == 5
    assert g.cell_zone(3, 4) == 6


def test_cell_column_major_layout():
    # cell_index = height*col + row -> the byte offset the engine (jt201) uses.
    g = Geo.blank(5, 7)          # width 5, height 7
    g.set_cell(2, 3, special=42)
    idx = g.height * 2 + 3
    assert g.map[idx * CELL_SIZE + 4] == 42


def test_cell_bounds_checked():
    g = Geo.blank(4, 4)
    with pytest.raises(IndexError):
        g.set_cell(4, 0, special=1)
    with pytest.raises(IndexError):
        g.cell(0, 4)


def test_zone_rule_norest_bit():
    g = Geo.blank(8, 8)
    g.hdr[48 + 3 * 4] = 12          # zone 3 interrupt event
    g.hdr[49 + 3 * 4] = 0x80        # zone 3 no-rest
    ev, norest = g.zone_rule(3)
    assert ev == 12 and norest is True
    assert g.zone_rule(0) == (0, False)


def test_events():
    g = Geo.blank(8, 8)
    rec = bytes([1]) + bytes(range(1, EVENT_SIZE))   # type 1 (combat)
    g.set_event(0, rec)
    g = Geo.parse(g.build())
    assert g.event_type(0) == 1
    assert g.event(0) == rec
    assert g.event(1) == bytes(EVENT_SIZE)           # empty


def test_capacity_constants():
    assert MAX_CELLS == 576 and MAP_SIZE == MAX_CELLS * CELL_SIZE
    assert MAX_EVENTS == 100 and ENCR_SIZE == MAX_EVENTS * EVENT_SIZE


def test_parse_rejects_bad_dims():
    g = Geo.blank(4, 4)
    g.width = 30
    g.height = 30            # 900 > 576
    with pytest.raises(GeoError):
        Geo.parse(g.build())


def test_parse_rejects_bad_version():
    g = Geo.blank(4, 4)
    g.version = 99
    with pytest.raises(GeoError):
        Geo.parse(g.build())
    g.version = 200
    with pytest.raises(GeoError):
        Geo.parse(g.build())


def test_parse_rejects_bad_size():
    with pytest.raises(GeoError):
        Geo.parse(b"FORM" + bytes(100))


def test_parse_rejects_non_iff():
    data = bytearray(Geo.blank(4, 4).build())
    data[0:4] = b"XXXX"
    with pytest.raises(GeoError):
        Geo.parse(bytes(data))


def test_odd_size_tolerated():
    # the engine pads odd file sizes; parse should accept GEO_SIZE+1.
    data = Geo.blank(4, 4).build() + b"\x00"
    assert Geo.parse(data).width == 4
