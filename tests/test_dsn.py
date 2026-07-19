"""Tests for tools/dsn.py (the playable-design builder). All synthetic — no
copyrighted data. Verifies the GAME001.DAT layout the engine reads (start area
at byte 48, entry at 49) and the folder assembly."""
import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from dsn import Design, demo_design, GAME_SIZE
from geo import Geo, GeoError


def test_game001_size_and_fields():
    d = Design("MYMOD", title="My Module")
    d.xp = 12345
    d.platinum = 250
    d.gems = 7
    d.jewelry = 3
    d.start_area = 4
    d.start_entry = 2
    d.add_area(4, Geo.blank(6, 6))
    g = d.game001()
    assert len(g) == GAME_SIZE
    assert g[:32].split(b"\x00")[0] == b"My Module"
    assert struct.unpack_from("<I", g, 32)[0] == 12345
    assert struct.unpack_from("<I", g, 36)[0] == 250
    assert struct.unpack_from("<I", g, 40)[0] == 7
    assert struct.unpack_from("<I", g, 44)[0] == 3
    assert g[48] == 4          # start area
    assert g[49] == 2          # start entry


def test_game001_requires_start_area_geo():
    d = Design("X")
    d.add_area(2, Geo.blank(4, 4))
    d.start_area = 1           # no GEO for area 1
    with pytest.raises(GeoError):
        d.game001()


def test_name_gets_dsn_suffix():
    assert Design("FOO").name == "FOO.DSN"
    assert Design("BAR.DSN").name == "BAR.DSN"


def test_write_folder_layout(tmp_path):
    d = Design("GEN", title="Gen")
    d.start_area = 5
    d.add_area(5, Geo.blank(8, 8))
    d.add_area(6, Geo.blank(4, 4))
    folder = d.write(str(tmp_path))
    assert os.path.basename(folder) == "GEN.DSN"
    files = sorted(os.listdir(folder))
    assert files == ["GAME001.DAT", "GEO005.DAT", "GEO006.DAT"]
    # the GEO files parse back
    geo5 = Geo.parse(open(os.path.join(folder, "GEO005.DAT"), "rb").read())
    assert (geo5.width, geo5.height) == (8, 8)


def test_write_make_current(tmp_path):
    d = Design("CUR")
    d.start_area = 1
    d.add_area(1, Geo.blank(4, 4))
    d.write(str(tmp_path), make_current=True)
    start = open(os.path.join(str(tmp_path), "start.dat"), "rb").read()
    assert len(start) == 35                          # 34-byte name field + 1 flag
    assert start[:8] == b"CUR.DSN\x00"               # null-terminated name


def test_write_rejects_no_areas(tmp_path):
    with pytest.raises(GeoError):
        Design("EMPTY").write(str(tmp_path))


def test_demo_design_is_valid_and_playable_shape():
    d = demo_design("GENAREA")
    # start area has a GEO and the header is buildable
    assert d.start_area in d.areas
    g = d.game001()
    assert g[48] == d.start_area
    # the demo area round-trips and carries the wired events
    geo = d.areas[d.start_area]
    geo = Geo.parse(geo.build())
    assert geo.event_info(0)["name"] == "Message / Text"
    assert geo.combat(1)["groups"] == [(1, 3)]
    # the entry cell points at the message event (special = index+1)
    ex, ey, _ = geo.entry_point(0)
    assert geo.cell_special(ex, ey) == 1
