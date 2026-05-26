"""Tests for the JT[3] inline-table extractor (tools/jt3_extract.py)."""
import struct

import pytest

from jt3_extract import (
    Jt3Table,
    emit_c_switch,
    parse_table,
)


def _synth_table(table_at, min_case, max_case, default_target, case_targets):
    """Build a blob with a JT[3] table at `table_at`, padded so the
    PC-relative offsets resolve to the requested targets."""
    n_cases = max_case - min_case + 1
    assert len(case_targets) == n_cases
    default_off = default_target - (table_at + 4)
    case_offs = [case_targets[i] - (table_at + 6 + 2 * i)
                 for i in range(n_cases)]

    blob = bytearray(0xff for _ in range(max(table_at + 2 * (3 + n_cases),
                                              default_target + 2,
                                              *[t + 2 for t in case_targets])))
    struct.pack_into(">3h", blob, table_at, min_case, max_case, default_off)
    for i, off in enumerate(case_offs):
        struct.pack_into(">h", blob, table_at + 6 + 2 * i, off)
    return bytes(blob)


def test_decodes_l12a0_real_table():
    """The L12a0 table at CODE 12 + 0x12ea decodes to the same arms
    the hand lift uses: default → 0x1306, case 0 → 0x12f6, case 1 →
    0x12fe, case 2 → 0x15de."""
    blob = bytearray(0xff for _ in range(0x1700))
    struct.pack_into(">3h", blob, 0x12ea, 0, 2, 0x18)         # min, max, default
    struct.pack_into(">3h", blob, 0x12f0, 0x06, 0x0c, 0x02ea) # case 0/1/2

    table = parse_table(bytes(blob), 0x12ea)
    assert table.min_case == 0
    assert table.max_case == 2
    assert table.default_target == 0x1306
    assert list(table.case_targets) == [0x12f6, 0x12fe, 0x15de]
    assert table.n_cases == 3
    assert table.table_size == 12


def test_emit_c_switch_renders_each_arm():
    table = Jt3Table(address=0x100, min_case=0, max_case=2,
                     default_target=0x200,
                     case_targets=[0x150, 0x160, 0x170])
    rendered = emit_c_switch(table, indent="    ")
    assert "case 0:" in rendered
    assert "case 1:" in rendered
    assert "case 2:" in rendered
    assert "default:" in rendered
    assert "0x150" in rendered
    assert "0x160" in rendered
    assert "0x170" in rendered
    assert "0x200" in rendered
    # Every emitted line carries the requested indent.
    for line in rendered.splitlines():
        assert line.startswith("    "), line


def test_negative_offsets_decode():
    """Case offsets are signed; an arm BEFORE the table resolves
    to a lower address than the table itself."""
    blob = _synth_table(0x200, 0, 0, 0x180, [0x190])
    table = parse_table(blob, 0x200)
    assert table.default_target == 0x180
    assert list(table.case_targets) == [0x190]


def test_rejects_table_past_end():
    blob = bytes(8)
    with pytest.raises(ValueError, match="doesn't fit"):
        parse_table(blob, 4)


def test_rejects_max_below_min():
    blob = struct.pack(">3h", 5, 3, 0) + b"\x00" * 10
    with pytest.raises(ValueError, match="bogus table"):
        parse_table(blob, 0)


def test_rejects_runaway_case_count():
    """A wrong table address typically lands on garbage that decodes
    as huge min..max bounds; reject early so the caller knows."""
    blob = struct.pack(">3h", 0, 5000, 0) + b"\x00" * 32
    with pytest.raises(ValueError, match="implausible case count"):
        parse_table(blob, 0)


def test_jt3_jsr_offset_adds_four():
    """`--jsr-at` is equivalent to `--table-at JSR+4` — the JT[3]
    JSR is `4e ad XX YY` (jsr %a5@(disp16)), a 4-byte instruction."""
    blob = _synth_table(0x104, 0, 1, 0x130, [0x140, 0x150])
    table = parse_table(blob, 0x100 + 4)
    assert table.address == 0x104
    assert list(table.case_targets) == [0x140, 0x150]
