"""Tests for tools/items.py (the ITEM.DAT item table). All synthetic — no
copyrighted data."""
import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from items import (Item, ItemTable, ItemError, ITEM_SIZE, ITEM_COUNT,
                   ITEMDAT_SIZE)


def test_table_size_constants():
    assert ITEM_SIZE == 18 and ITEM_COUNT == 254
    assert ITEMDAT_SIZE == ITEM_COUNT * ITEM_SIZE == 4572


def test_blank_table_roundtrips():
    t = ItemTable()
    assert len(t.build()) == ITEMDAT_SIZE
    assert ItemTable.parse(t.build()).build() == t.build()


def test_item_fields_encode_decode():
    it = Item()
    it.type = 5
    it.value = 1500
    it.value2 = 300
    it.range_code = 7
    it.class_mask = 0xAB
    it = Item(it.raw)
    assert it.type == 5
    assert it.raw[0] == 5 and it.raw[3] == 5     # type mirrors into [3]
    assert it.value == 1500
    assert struct.unpack_from("<H", it.raw, 4)[0] == 1500   # little-endian
    assert it.value2 == 300
    assert it.range_code == 7
    assert it.class_mask == 0xAB


def test_item_ids_are_1_based():
    t = ItemTable()
    t[1].type = 11
    t[254].type = 22
    t = ItemTable.parse(t.build())
    assert t[1].type == 11
    assert t[254].type == 22
    # id 1 is record 0 in the raw file
    assert t.build()[0] == 11
    assert t.build()[253 * ITEM_SIZE] == 22


def test_item_id_bounds():
    t = ItemTable()
    with pytest.raises(IndexError):
        _ = t[0]
    with pytest.raises(IndexError):
        _ = t[255]


def test_empty_and_used_ids():
    t = ItemTable()
    assert t.used_ids() == []          # all-zero -> nothing used
    t[5].type = 30
    t[200].type = 4
    assert t.used_ids() == [5, 200]
    # type 0 and 255 both read as empty
    t[5].type = 255
    assert t[5].empty
    assert t.used_ids() == [200]


def test_parse_rejects_bad_size():
    with pytest.raises(ItemError):
        ItemTable.parse(b"\x00" * 100)
    with pytest.raises(ItemError):
        Item(b"\x00" * 10)
