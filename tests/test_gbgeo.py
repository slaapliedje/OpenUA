"""tools/gbgeo.py — Gold Box four-plane maps, and their re-expression as FRUA areas.

Every map here is built in the test; no game data is read. The real-data check
(Curse of the Azure Bonds' maps walked on the Falcon build) is recorded in the
module and in ADR-0023 — what these tests pin is the part of that result a
future edit could silently undo: the plane packing, the door flags, and above
all the AXIS AND EDGE ORDER of the FRUA cells, which was wrong twice before a
walk through an asymmetric map settled it.
"""
import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import gbgeo                                              # noqa: E402
from gbgeo import GbMap, GbGeoError, N, E, S, W           # noqa: E402
from geo import Geo                                       # noqa: E402


def planes(width, height, cells):
    """Pack {(x, y): (slots NESW, doors NESW)} into four planes."""
    n = width * height
    p = [bytearray(n) for _ in range(4)]
    for (x, y), (sl, dr) in cells.items():
        i = y * width + x
        p[0][i] = (sl[N] << 4) | sl[E]
        p[1][i] = (sl[S] << 4) | sl[W]
        p[3][i] = dr[N] | (dr[E] << 2) | (dr[S] << 4) | (dr[W] << 6)
    return b"".join(bytes(x) for x in p)


def one_cell(slots, doors=(0, 0, 0, 0), at=(3, 5)):
    return GbMap.from_planes(planes(16, 16, {at: (slots, doors)}), 16, 16)


def test_planes_are_nibble_packed_per_edge():
    m = one_cell((1, 2, 3, 4), (0, 1, 2, 3))
    assert m.slots[5][3] == (1, 2, 3, 4)          # N, E, S, W
    assert m.doors[5][3] == (0, 1, 2, 3)
    assert m.slots[0][0] == (0, 0, 0, 0)


def test_gen1_block_accepts_the_dos_length_word():
    raw = planes(16, 16, {(1, 1): ((7, 0, 0, 0), (0, 0, 0, 0))})
    assert GbMap.from_gen1_block(raw).slots[1][1][N] == 7
    assert GbMap.from_gen1_block(struct.pack("<H", 1024) + raw).slots[1][1][N] == 7
    with pytest.raises(GbGeoError):
        GbMap.from_gen1_block(b"\x00\x00" + raw)          # wrong length word
    with pytest.raises(GbGeoError):
        GbMap.from_gen1_block(raw[:-1])


def test_edge_byte_mapping():
    m = one_cell((0, 5, 5, 5), (0, 0, 1, 2))
    assert m.frua_edge(3, 5, N) == 0x00           # no slot: open, nothing drawn
    assert m.frua_edge(3, 5, E) == 0xE5           # slot, no door flag: solid
    assert m.frua_edge(3, 5, S) == 0x05           # door: passable, still textured
    assert m.frua_edge(3, 5, W) == 0x05           # any non-zero flag passes
    assert m.passable(3, 5, N) and not m.passable(3, 5, E)
    assert m.passable(3, 5, S) and m.passable(3, 5, W)


def test_frua_cell_axes_and_edge_order():
    """THE ONE THAT MATTERS. An asymmetric cell — a different slot on every
    edge, off the diagonal — must land on set_cell(col=y, row=x) with its edge
    bytes in N, E, S, W order. Both alternatives that were tried first pass a
    square-room test and fail this."""
    m = one_cell((1, 2, 3, 4), at=(3, 5))
    g = m.to_frua()
    cell = g.cell(col=5, row=3)
    assert [b & 15 for b in cell[:4]] == [1, 2, 3, 4]
    assert all(b >> 4 == 14 for b in cell[:4])
    assert g.cell(col=3, row=5)[:4] == bytes(4)   # the transposed cell is empty
    assert cell[4] == 0                           # no event: ECL is not imported


def test_non_square_map_keeps_its_shape():
    m = GbMap.from_planes(planes(20, 10, {(19, 9): ((0, 9, 9, 0), (0, 0, 0, 0))}),
                          width=20, height=10)
    g = m.to_frua()
    # geo.py's `width` bounds the north-south axis (its `col`), `height` the
    # east-west one — see the #104 note in tools/geo.py.
    assert (g.width, g.height) == (10, 20)
    assert [b & 15 for b in g.cell(col=9, row=19)[:4]] == [0, 9, 9, 0]


def test_entry_is_a_cell_with_an_exit_facing_it():
    # everything sealed except one cell that opens SOUTH
    sealed = ((1, 1, 1, 1), (0, 0, 0, 0))
    cells = {(x, y): sealed for x in range(16) for y in range(16)}
    cells[(6, 2)] = ((1, 1, 0, 1), (0, 0, 0, 0))
    m = GbMap.from_planes(planes(16, 16, cells), 16, 16)
    assert m.find_entry() == (6, 2, S)
    g = m.to_frua()
    assert g.entry_point(0) == (6, 2, 4)          # (row = x, col = y, facing S)


def test_edge_symmetry_scores_a_consistent_map_and_not_a_misread_one():
    # a wall between (4,4) and (5,4), stored on both sides
    good = GbMap.from_planes(planes(16, 16, {
        (4, 4): ((0, 3, 0, 0), (0, 0, 0, 0)),
        (5, 4): ((0, 0, 0, 3), (0, 0, 0, 0))}), 16, 16)
    agree, total = good.edge_symmetry()
    assert total == 2 * 16 * 15 and agree == total
    one_sided = one_cell((0, 3, 0, 0), at=(4, 4))
    assert one_sided.edge_symmetry()[0] == total - 1


def test_a_map_too_big_for_a_frua_area_is_refused_by_name():
    # 28x28 = 784 cells: the Dark Queen case. It must fail loudly here, not
    # produce a truncated area.
    m = GbMap.from_planes(bytes(4 * 28 * 28), 28, 28)
    with pytest.raises(GbGeoError, match="576"):
        m.to_frua()


def library(magic, members):
    end = ">" if magic == b"GLIB" else "<"
    head = 16 + 4 * (len(members) + 1)
    offs, pos = [], head
    for m in members:
        offs.append(pos)
        pos += len(m)
    offs.append(pos)
    return (magic + struct.pack(end + "IH", pos, len(members)) + b"\x00\x01DATA"
            + struct.pack(end + "%dI" % len(offs), *offs) + b"".join(members))


@pytest.mark.parametrize("magic,end", [(b"GLIB", ">"), (b"HLIB", "<")])
def test_library_with_id_table_in_either_endianness(magic, end):
    a = planes(16, 16, {(0, 0): ((2, 0, 0, 0), (0, 0, 0, 0))})
    b = planes(16, 16, {(0, 0): ((9, 0, 0, 0), (0, 0, 0, 0))})
    table = struct.pack(end + "H", 2) + struct.pack(end + "HHHH", 0x10, 2, 0x21, 1)
    maps = gbgeo.load_maps(library(magic, [table, a, b]))
    assert [m.area_id for m in maps] == [0x10, 0x21]
    assert [m.slots[0][0][N] for m in maps] == [9, 2]     # the table's order


def test_library_member_with_a_height_width_header():
    # Dark Queen's form: 8-byte header, height then width, then the planes.
    body = planes(12, 10, {(11, 9): ((0, 0, 0, 6), (0, 0, 0, 0))})
    member = bytes([10, 12, 0, 0, 0, 0, 0, 0]) + body
    table = struct.pack("<HHH", 1, 34, 1)
    (m,) = gbgeo.load_maps(library(b"HLIB", [table, member]))
    assert (m.width, m.height, m.area_id) == (12, 10, 34)
    assert m.slots[9][11][W] == 6


def test_bad_containers_are_rejected():
    with pytest.raises(GbGeoError):
        gbgeo.read_library(b"GLIB" + bytes(20))
    lib = library(b"GLIB", [struct.pack(">HHH", 1, 1, 7), bytes(1024)])
    with pytest.raises(GbGeoError, match="member 7"):
        gbgeo.load_maps(lib)


def test_design_round_trips_through_the_frua_reader(tmp_path):
    maps = [one_cell((1, 2, 3, 4), at=(3, 5)), one_cell((5, 6, 7, 8), at=(1, 2))]
    d = gbgeo.build_design(maps, name="GBTEST")
    folder = d.write(str(tmp_path))
    names = sorted(os.listdir(folder))
    assert "GEO005.DAT" in names and "GEO006.DAT" in names
    with open(os.path.join(folder, "GEO006.DAT"), "rb") as f:
        g = Geo.parse(f.read())
    assert [b & 15 for b in g.cell(col=2, row=1)[:4]] == [5, 6, 7, 8]
