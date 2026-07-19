"""Tests for tools/itemsel.py (the -12645 item-selector grid). Synthetic — the
real grid is extracted from the user's own resource fork, never committed."""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from itemsel import (SelectorGrid, SelectorError, make_cell, cell_kind,
                     cell_slots, SELECTOR_ROWS, SELECTOR_COLS,
                     SELECTOR_A5_OFFSET, SELECTOR_SIZE)


def _synthetic_grid():
    # row r, col c -> a distinct item id (r*20 + c + 1), like the real seed
    return [[r * SELECTOR_COLS + c + 1 for c in range(SELECTOR_COLS)]
            for r in range(SELECTOR_ROWS)]


def test_constants():
    assert SELECTOR_A5_OFFSET == -12645
    assert SELECTOR_ROWS == 16 and SELECTOR_COLS == 20
    assert SELECTOR_SIZE == 320


def test_cell_kind_and_slots_match_jt188():
    # jt188: kind = cell[0]>>4; bit i at cell[2 - i//8] & (1 << (i%8))
    cell = make_cell(5, [0, 8, 16, 19])
    assert cell_kind(cell) == 5
    assert cell_slots(cell) == [0, 8, 16, 19]
    # slot 0..7 live in cell[2], 8..15 in cell[1], 16..19 in low nibble of cell[0]
    assert cell[2] & 0x01          # slot 0
    assert cell[1] & 0x01          # slot 8
    assert cell[0] & 0x01          # slot 16
    assert cell[0] & 0x08          # slot 19


def test_cell_roundtrips():
    for kind in (0, 7, 15):
        for slots in ([], [0], [19], [0, 5, 19], list(range(20))):
            c = make_cell(kind, slots)
            assert cell_kind(c) == kind
            assert cell_slots(c) == sorted(slots)


def test_make_cell_rejects_bad_args():
    with pytest.raises(SelectorError):
        make_cell(16, [0])
    with pytest.raises(SelectorError):
        make_cell(0, [20])


def test_items_for_cell():
    g = SelectorGrid(_synthetic_grid())
    cell = make_cell(2, [0, 1, 3])         # row 2 -> ids 41,42,44
    assert g.items_for_cell(cell) == [41, 42, 44]


def test_items_for_cell_skips_empty_slots():
    rows = _synthetic_grid()
    rows[1][2] = 0                         # blank a slot
    g = SelectorGrid(rows)
    assert g.items_for_cell(make_cell(1, [1, 2, 3])) == [22, 24]


def test_locate_and_cell_for_item():
    g = SelectorGrid(_synthetic_grid())
    assert g.locate(41) == (2, 0)
    assert g.locate(999) is None
    cell = g.cell_for_item(41)
    assert g.items_for_cell(cell) == [41]
    assert g.cell_for_item(999) is None


def test_bad_grid_shape_rejected():
    with pytest.raises(SelectorError):
        SelectorGrid([[0] * 20] * 15)      # only 15 rows
    with pytest.raises(SelectorError):
        SelectorGrid([[0] * 19] * 16)      # 19 cols
