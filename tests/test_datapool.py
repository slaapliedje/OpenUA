"""Tests for the THINK C DATA + DREL parser (tools/datapool.py)."""
import struct

import pytest

from datapool import (
    DataPool,
    RelocEntry,
    RelocTable,
    RELOC_BASE_A4,
    RELOC_BASE_A5,
    data_offset_for,
    parse_data,
    parse_drel,
    read_initial_long,
)


def test_parse_data_records_byte_count():
    pool = parse_data(b"\x00" * 1024)
    assert len(pool.bytes) == 1024
    assert pool.a5_below_size == 1024


def test_parse_data_grows_to_match_drel_reach():
    # DREL touches a slot at A5-1500, but DATA only initialises 1024
    # bytes — the A5-below region must be sized to cover both.
    pool = parse_data(b"\x00" * 1024, drel_min_a5_offset=-1500)
    assert pool.a5_below_size == 1500


def test_parse_drel_decodes_signed_shorts():
    # Words with bit 0 = 0 are A5-base entries; the offsets are
    # already even, so the encoded word IS the offset.
    blob = struct.pack(">3H", 0xFFE0, 0xFFF0, 0xFFF8)
    table = parse_drel(blob)
    assert len(table.entries) == 3
    assert [e.a5_offset for e in table.entries] == [-32, -16, -8]
    assert all(e.base == RELOC_BASE_A5 for e in table.entries)


def test_parse_drel_base_bit_splits_a4_from_a5():
    # 0x0010 → offset +16, A5 base.
    # 0x0011 → offset +16 (LSB masked), A4 base (string pool).
    blob = struct.pack(">2H", 0x0010, 0x0011)
    table = parse_drel(blob)
    assert table.entries[0] == RelocEntry(a5_offset=16, base=RELOC_BASE_A5)
    assert table.entries[1] == RelocEntry(a5_offset=16, base=RELOC_BASE_A4)


def test_a5_min_excludes_a4_entries():
    blob = struct.pack(">3H", 0xFFE0, 0xFF00, 0x8001)  # -32 A5, -256 A5, -32768 A4
    table = parse_drel(blob)
    # Only the A5-base entries count toward the buffer-sizing minimum.
    assert table.a5_min() == -256


def test_parse_drel_rejects_odd_length():
    with pytest.raises(ValueError):
        parse_drel(b"\x00\x00\x00")


def test_data_offset_for_maps_negative_to_index():
    # Pool of 12 bytes. A5 = index 12. Offset -4 -> index 12 + (-4) = 8.
    pool = parse_data(b"\x00" * 12)
    assert data_offset_for(-4, pool) == 8
    assert data_offset_for(-12, pool) == 0


def test_data_offset_for_out_of_range_returns_negative():
    pool = parse_data(b"\x00" * 12)
    # Past A5 (offset > 0): not in below-A5 data.
    assert data_offset_for(1, pool) == -1
    # Read straddles end of data (need 4 bytes starting at index 10
    # → would touch indexes 10..13, but data is 12 bytes).
    assert data_offset_for(-2, pool) == -1
    # Below the data start (offset more negative than data size).
    assert data_offset_for(-20, pool) == -1


def test_read_initial_long_returns_value_at_offset():
    blob = bytearray(16)
    struct.pack_into(">I", blob, 8, 0x12345678)
    pool = parse_data(bytes(blob))
    # A5 = index 16. Offset -8 -> index 8.
    assert read_initial_long(-8, pool) == 0x12345678


def test_read_initial_long_returns_none_for_bss():
    pool = parse_data(b"\x00" * 4)          # A5 = index 4
    assert read_initial_long(-100, pool) is None        # below data
    assert read_initial_long(4, pool) is None           # past A5


def test_parsed_offset_is_always_even():
    """The base-select bit lives in bit 0 — every parsed a5_offset
    has it masked off, leaving an even number."""
    blob = struct.pack(">4H", 0xFFE0, 0xFFE1, 0xFFFC, 0xFFFD)
    table = parse_drel(blob)
    for entry in table.entries:
        assert entry.a5_offset % 2 == 0
