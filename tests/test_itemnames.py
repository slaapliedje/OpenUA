"""Tests for tools/itemnames.py (the built-in item name-word list). All
synthetic — the real name words are copyrighted engine data, so these inject a
fake word list and exercise the composition rule, never a resource fork."""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from itemnames import (ItemNamer, ItemNameError, NAME_WORDS_A5_OFFSET,
                       NAME_WORD_STRIDE)
from items import Item


# A synthetic name-word table (index 0 = empty word, as in the real table).
WORDS = ["", "Long Sword", "Mail", "Banded", "Cloak", "of", "Displacement",
         "+1"]


def _item(b1, b2, b3, item_type=0):
    it = Item()
    it.raw[0] = item_type & 0xff
    it.raw[1] = b1
    it.raw[2] = b2
    it.raw[3] = b3
    return it


def test_structure_constants():
    assert NAME_WORDS_A5_OFFSET == -13628
    assert NAME_WORD_STRIDE == 4


def test_single_word_name():
    n = ItemNamer(WORDS)
    assert n.base_name(_item(0, 0, 1)) == "Long Sword"


def test_parts_emit_high_to_low():
    # [1,2,3] = [0, "Mail"(2), "Banded"(3)] -> "Banded Mail" (part 3 then 2)
    n = ItemNamer(WORDS)
    assert n.base_name(_item(0, 2, 3)) == "Banded Mail"


def test_three_part_name():
    # The PRIMARY noun sits in template[3]; jt28 emits [3], then [2], then [1].
    # "Cloak of Displacement" is stored [1]=Displacement, [2]=of, [3]=Cloak
    # (matches the real base game's [58,90,44] = Cloak/of/Displacement).
    n = ItemNamer(WORDS)
    assert n.base_name(_item(6, 5, 4)) == "Cloak of Displacement"


def test_empty_parts_are_skipped():
    n = ItemNamer(WORDS)
    assert n.base_name(_item(0, 0, 0)) == ""
    assert n.base_name(_item(7, 0, 1)) == "Long Sword +1"   # [1]=+1 emitted last


def test_word_out_of_range_is_empty():
    n = ItemNamer(WORDS)
    # index 99 is beyond the table -> contributes nothing
    assert n.base_name(_item(0, 0, 99)) == ""
    assert n.word(99) == ""
    assert n.word(0) == ""
    assert n.word(1) == "Long Sword"


def test_accepts_raw_bytes():
    n = ItemNamer(WORDS)
    raw = bytearray(18)
    raw[3] = 1
    assert n.base_name(raw) == "Long Sword"


def test_short_record_rejected():
    n = ItemNamer(WORDS)
    with pytest.raises(ItemNameError):
        n.base_name(b"\x00\x00")
