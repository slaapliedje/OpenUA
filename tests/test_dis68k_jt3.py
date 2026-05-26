"""JT[3] table-detection tests for tools/dis68k.py.

The detector is the runtime sibling of tools/jt3_extract.py — it spots
`jsr JT[3]` in the disassembled stream and decodes the inline table.
These tests exercise the table-decode primitive (decode_jt3_table)
and the row-walker (find_jt3_tables) over a synthetic blob; the
listing-writer integration is covered indirectly by the real
disassembly being re-runnable against the FRUA fork.
"""
import struct

from dis68k import decode_jt3_table, find_jt3_tables


def _l12a0_blob():
    """Build a blob whose layout matches CODE 12's L12a0 dispatcher:
    a `jsr JT[3]` at 0x12e6 followed by the inline table at 0x12ea
    (min=0, max=2, default -> 0x1306, cases -> 0x12f6 / 0x12fe / 0x15de).
    """
    blob = bytearray(0xff for _ in range(0x1700))
    # JSR to JT[3] at 0x12e6. The mnem doesn't matter to the decoder,
    # only the (signed) shorts of the table at 0x12ea do.
    struct.pack_into(">3h", blob, 0x12ea, 0, 2, 0x18)             # min, max, default
    struct.pack_into(">3h", blob, 0x12f0, 0x06, 0x0c, 0x02ea)     # case 0/1/2
    return bytes(blob)


def test_decode_returns_l12a0_arms():
    blob = _l12a0_blob()
    decoded = decode_jt3_table(blob, 0x12ea)
    assert decoded is not None
    table_end, min_c, max_c, default_target, case_targets = decoded
    assert (min_c, max_c) == (0, 2)
    assert default_target == 0x1306
    assert case_targets == [0x12f6, 0x12fe, 0x15de]
    assert table_end == 0x12f6              # 6 prefix + 3*2 case bytes


def test_decode_rejects_implausible_bounds():
    """A wrong table address typically lands on garbage that decodes
    as huge min..max; the helper bails so the row stream isn't
    contaminated with bogus skip ranges."""
    blob = struct.pack(">3h", 0, 5000, 0) + b"\x00" * 32
    assert decode_jt3_table(blob, 0) is None


def test_decode_rejects_max_below_min():
    blob = struct.pack(">3h", 7, 3, 0) + b"\x00" * 16
    assert decode_jt3_table(blob, 0) is None


def test_decode_rejects_table_past_end():
    blob = struct.pack(">3h", 0, 10, 0)     # 11 cases needed, only 6 bytes left
    assert decode_jt3_table(blob, 0) is None


def test_decode_handles_negative_offsets():
    """Case offsets are signed shorts — arms before the table address
    resolve to lower addresses without underflow."""
    blob = bytearray(b"\x00" * 0x400)
    struct.pack_into(">3h", blob, 0x200, 0, 0, -0x80)   # default at 0x184
    struct.pack_into(">h",  blob, 0x206, -0x10)         # case 0 at 0x1f6
    decoded = decode_jt3_table(bytes(blob), 0x200)
    assert decoded is not None
    _, _, _, default_target, case_targets = decoded
    assert default_target == 0x184
    assert case_targets == [0x1f6]


def test_find_walks_rows_for_jt3_calls():
    """find_jt3_tables matches on the comment annotation dis68k
    attaches to JT[3] JSRs and decodes the table at the next row's
    address."""
    blob = _l12a0_blob()
    # rows = (addr, raw, mnem, ops, comment) — only mnem + comment
    # are inspected; address + comment shape mirror dis68k's output.
    rows = [
        (0x12e6, "4ead003a", "jsr", "%a5@(58)",
         "-> CODE 1+0x158  (JT[3])"),
        (0x12ea, "00000002", "orib",  "#2,%d0",  ""),
        (0x12ee, "00180006", "orib",  "#6,%a0@+", ""),
        (0x12f2, "000c",     ".short", "0x000c",  ""),
        (0x12f4, "02ea",     ".short", "0x02ea",  ""),
    ]
    tables = find_jt3_tables(rows, blob)
    assert 0x12ea in tables
    table_end, min_c, max_c, default_target, case_targets = tables[0x12ea]
    assert (min_c, max_c) == (0, 2)
    assert default_target == 0x1306
    assert case_targets == [0x12f6, 0x12fe, 0x15de]


def test_find_ignores_non_jt3_jsrs():
    """A JSR through a non-JT[3] jump-table slot looks similar but
    doesn't carry the inline-switch table — the detector must skip it."""
    blob = bytes(0x100)
    rows = [
        (0x40, "4ead0040", "jsr", "%a5@(64)",
         "-> CODE 6+0x4bf6  (JT[103])"),
        (0x44, "4e75",     "rts", "",                       ""),
    ]
    assert find_jt3_tables(rows, blob) == {}
