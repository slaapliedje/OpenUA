"""Tests for the JT[1] sparse-switch extractor (tools/jt1_extract.py)."""
import struct

import pytest

from jt1_extract import (
    Jt1Table,
    emit_c_switch,
    parse_table,
)


def _synth_table(table_at, cases, default_target):
    """Build a blob holding a JT[1] table at `table_at`.

    `cases` is a list of (value, target). The per-entry offset is
    chosen so JT[1]'s a0/dbeq math resolves to the requested target:
    for case k, target = table_at + 2 + 4*k + offset_k; the default
    (entry N) is table_at + 2 + 4*N + offset_default.
    """
    count = len(cases)
    need = max([table_at + 2 + 4 * count + 2, default_target + 2]
               + [t + 2 for _, t in cases])
    blob = bytearray(0 for _ in range(need))
    struct.pack_into(">H", blob, table_at, count)
    for k, (value, target) in enumerate(cases):
        off = target - (table_at + 2 + 4 * k)
        struct.pack_into(">H", blob, table_at + 2 + 4 * k, off & 0xffff)
        struct.pack_into(">h", blob, table_at + 4 + 4 * k, value)
    default_off = default_target - (table_at + 2 + 4 * count)
    struct.pack_into(">H", blob, table_at + 2 + 4 * count, default_off & 0xffff)
    return bytes(blob)


def test_roundtrip_synthetic():
    cases = [(1, 0x200), (5, 0x210), (10, 0x220)]
    blob = _synth_table(0x100, cases, default_target=0x230)
    t = parse_table(blob, 0x100)
    assert t.count == 3
    assert list(t.cases) == cases
    assert t.default_target == 0x230
    assert t.table_size == 2 + 4 * 3 + 2


def test_decodes_jt251_real_table():
    """The mode-4 handler (JT[251], CODE 2 + 0x4284) dispatches its
    command via a JT[1] table at 0x42b6. Decoded from the raw words it
    must resolve: cmd 1 -> 0x42ce, 5 -> 0x42e0, 10 -> 0x42f2, 8 ->
    0x4310, 11 -> 0x4310, default -> 0x4310 (the L4310 convergence)."""
    words = [0x0005,
             0x0016, 0x0001,   # cmd 1
             0x0024, 0x0005,   # cmd 5
             0x0032, 0x000a,   # cmd 10
             0x004c, 0x0008,   # cmd 8
             0x0048, 0x000b,   # cmd 11
             0x0044]           # default
    table_at = 0x42b6
    blob = bytearray(0 for _ in range(table_at + 2 * len(words) + 8))
    for i, w in enumerate(words):
        struct.pack_into(">H", blob, table_at + 2 * i, w)

    t = parse_table(bytes(blob), table_at)
    assert t.count == 5
    assert dict(t.cases) == {1: 0x42ce, 5: 0x42e0, 10: 0x42f2,
                             8: 0x4310, 11: 0x4310}
    assert t.default_target == 0x4310


def test_signed_backward_offset():
    """A case may jump backward (negative offset); it must sign-extend."""
    blob = _synth_table(0x400, [(7, 0x300)], default_target=0x500)
    t = parse_table(blob, 0x400)
    assert t.cases[0] == (7, 0x300)


def test_rejects_implausible_count():
    blob = bytes([0xff, 0xff]) + bytes(64)
    with pytest.raises(ValueError):
        parse_table(blob, 0)


def test_rejects_truncated_table():
    blob = _synth_table(0x10, [(1, 0x40), (2, 0x44)], default_target=0x48)
    with pytest.raises(ValueError):
        parse_table(blob[:0x14], 0x10)   # cut off mid-table


def test_emit_c_switch_mentions_arms():
    blob = _synth_table(0x100, [(1, 0x200), (5, 0x210)], default_target=0x220)
    t = parse_table(blob, 0x100)
    txt = emit_c_switch(t)
    assert "case 1:" in txt and "0x200" in txt
    assert "case 5:" in txt and "0x210" in txt
    assert "default:" in txt and "0x220" in txt
