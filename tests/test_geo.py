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
                 VERSION_MAX, EVENT_TYPES, EVENT_CONDITIONS)


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


def test_event_info_empty_slot():
    g = Geo.blank(4, 4)
    assert g.event_info(0) is None            # all-zero slot -> empty


def test_event_header_encode_decode():
    g = Geo.blank(4, 4)
    # Combat (type 1), fires on a 50% roll, once-only, chains to event 7.
    g.set_event_header(0, type=1, cond_type=5, cond_param=50,
                       chain=7, once_only=True)
    g = Geo.parse(g.build())
    e = g.event_info(0)
    assert e["type"] == 1 and e["name"] == "Combat"
    assert e["cond_type"] == 5 and "percent" in e["cond_name"]
    assert e["cond_param"] == 50
    assert e["chain"] == 7
    assert e["once_only"] is True


def test_event_header_condition_and_once_pack():
    g = Geo.blank(4, 4)
    g.set_event_header(1, type=2, cond_type=8, cond_param=0b0101)  # facing N|S
    raw = g.event(1)
    assert raw[0] == 2
    assert (raw[1] >> 3) == 8 and (raw[1] & 1) == 0    # not once-only
    assert raw[2] == 0b0101


def test_combat_build_and_decode():
    g = Geo.blank(8, 8)
    g.set_combat(0, [(66, 4), (25, 1)], text_id=0x1234, picture=200)
    g = Geo.parse(g.build())
    c = g.combat(0)
    assert c["type"] == 1 and c["random"] is False
    assert c["groups"] == [(66, 4), (25, 1)]
    assert c["text_id"] == 0x1234
    assert c["picture"] == 200
    assert g.event_info(0)["name"] == "Combat"


def test_combat_random_flag_is_type_33():
    g = Geo.blank(8, 8)
    g.set_combat(0, [(10, 3)], random=True)
    assert g.event(0)[0] == 33
    assert g.combat(0)["random"] is True


def test_combat_slot_encoding():
    # count in low 5 bits of the even byte, monster id in the odd byte
    g = Geo.blank(8, 8)
    g.set_combat(0, [(66, 4)])
    raw = g.event(0)
    assert (raw[8] & 0x1f) == 4      # count
    assert raw[9] == 66              # monster id


def test_combat_rejects_out_of_range():
    g = Geo.blank(8, 8)
    with pytest.raises(GeoError):
        g.set_combat(0, [(66, 0)])          # count 0
    with pytest.raises(GeoError):
        g.set_combat(0, [(66, 32)])         # count > 31
    with pytest.raises(GeoError):
        g.set_combat(0, [(0, 4)])           # monster id 0
    with pytest.raises(GeoError):
        g.set_combat(0, [(1, 1)] * 7)       # > 6 groups


def test_combat_on_non_combat_event_raises():
    g = Geo.blank(8, 8)
    g.set_event_header(0, type=2)           # Message
    with pytest.raises(GeoError):
        g.combat(0)


def test_passage_build_decode():
    g = Geo.blank(8, 8)
    g.set_passage(0, dest_area=6, x=3, y=4, facing=2)
    g = Geo.parse(g.build())
    p = g.passage(0)
    assert p["type"] == 11
    assert p["dest_area"] == 6
    assert p["x"] == 3 and p["y"] == 4
    assert p["facing"] == 2
    assert p["confirm"] is False
    assert g.event_info(0)["name"] == "Passage / level change"


def test_passage_slot_encoding():
    g = Geo.blank(8, 8)
    g.set_passage(0, dest_area=9, x=5, y=2, facing=4)
    raw = g.event(0)
    assert raw[0] == 11
    assert raw[14] == 9              # ev[14] = target area
    assert raw[9] == 5               # ev[9] = landing row (x)
    assert raw[8] == 2               # ev[8] = landing col (y)
    assert (raw[7] & 0x0c) >> 1 == 4 # facing packed in ev[7] bits 2-3
    assert (raw[12] & 1) == 0        # direct landing (no marker)


def test_passage_rejects_bad_args():
    g = Geo.blank(8, 8)
    with pytest.raises(GeoError):
        g.set_passage(0, dest_area=0, x=1, y=1)      # area 0
    with pytest.raises(GeoError):
        g.set_passage(0, dest_area=6, x=1, y=1, facing=1)  # facing not 0/2/4/6


def test_passage_on_non_passage_raises():
    g = Geo.blank(8, 8)
    g.set_combat(0, [(1, 1)])
    with pytest.raises(GeoError):
        g.passage(0)


def test_strg_roundtrip_uppercase():
    g = Geo.blank(4, 4)
    g.strg_write(["", "You enter a dark cavern.", "A cold wind blows.", "ATTACK!"])
    g = Geo.parse(g.build())
    s = g.strg_read()
    assert s[0] == ""
    assert s[1] == "YOU ENTER A DARK CAVERN."     # folds to uppercase
    assert s[2] == "A COLD WIND BLOWS."
    assert s[3] == "ATTACK!"


def test_strg_is_fixed_point():
    g = Geo.blank(4, 4)
    src = ["HELLO WORLD", "", "1234567890", "A", "AB", "ABC", "ABCD", "ABCDE"]
    g.strg_write(src)
    once = g.strg_read()[:len(src)]
    g.strg_write(once)
    assert g.strg_read()[:len(src)] == once      # re-encode is stable


def test_strg_header_and_index():
    g = Geo.blank(4, 4)
    g.strg_write(["", "HI", "WORLD"])
    # header body-capacity word (LE on disk)
    assert struct.unpack_from("<H", g.strg, 0)[0] == STRG_SIZE - 406
    assert struct.unpack_from("<H", g.strg, 2)[0] == 0xffff
    # index: slot 0 empty, slots 1/2 have packed lengths
    assert g.strg[6 + 0] == 0
    assert g.strg[6 + 1] > 0 and g.strg[6 + 2] > 0


def test_strg_body_overflow_raises():
    g = Geo.blank(4, 4)
    with pytest.raises(GeoError):
        g.strg_write(["X" * 9000])         # exceeds the 6762-byte body


def test_message_build_decode_and_resolve():
    g = Geo.blank(4, 4)
    g.strg_write(["", "You enter a dark cavern.", "A cold wind blows."])
    g.set_message(0, text_ids=[2, 3], picture=5, sound=9)
    g = Geo.parse(g.build())
    m = g.message(0)
    assert m["lines"] == [2, 3]
    assert m["picture"] == 5 and m["sound"] == 9
    assert g.event_info(0)["name"] == "Message / Text"
    tbl = g.strg_read()
    # event text ids are 1-based -> strg_read()[id-1]
    assert tbl[m["lines"][0] - 1] == "YOU ENTER A DARK CAVERN."


def test_message_slots_big_endian():
    g = Geo.blank(4, 4)
    g.set_message(0, text_ids=[0x0102, 0x0304])
    raw = g.event(0)
    assert raw[8] == 0x01 and raw[9] == 0x02     # big-endian
    assert raw[10] == 0x03 and raw[11] == 0x04


def test_message_rejects_too_many_lines():
    g = Geo.blank(4, 4)
    with pytest.raises(GeoError):
        g.set_message(0, text_ids=[1, 2, 3, 4, 5, 6])


def test_event_type_and_condition_tables():
    assert EVENT_TYPES[1] == "Combat"
    assert EVENT_TYPES[2].startswith("Message")
    assert EVENT_TYPES[33].startswith("Combat")
    assert EVENT_CONDITIONS[0] == "always"
    assert "percent" in EVENT_CONDITIONS[5]
    # unmapped type falls back gracefully
    g = Geo.blank(4, 4)
    g.set_event_header(0, type=99)
    assert "unmapped" in g.event_info(0)["name"]


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
